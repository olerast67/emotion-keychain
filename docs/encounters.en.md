English | [Русский](encounters.md)

# Keychain encounters

Two keychains close to each other recognize one another over Bluetooth Low Energy: they say hello, remember the acquaintance, and sometimes stage a hacker duel. The winner unlocks an emotion from the next implant early. After the duel the keychains talk to each other through their buzzers in a cipher their owners can't understand. Owners find each other, and you get a reason to build a community and make videos.

The engine already does everything you can see and hear:
- the `greet`, `duel` and `chat` scenes;
- experience for meetings and a reward for winning (`Behavior::meet`, `Behavior::duel`);
- the secret chat with a shared key (`chat_key`, [keychain voice](sound.en.md#secret-chat)).

Below is how this works over radio in the rev B board firmware (`firmware/src/keychain/radio.cpp`). The radio-independent logic (packet format, meeting confirmation, countdown and duel outcome) lives in the engine (`face/encounter.h`) and is covered by tests: two keychains exchange packets, and their scenes match.

The ESP32 test stand meets over radio the same way (`firmware/src/stand_radio.cpp`): same packet, same schedule and same engine logic. So two stands on a desk greet each other, duel and chat just like two keychains, and a stand and a rev B keychain do the same with each other. All four stand boards work: ESP32-C5, C3 and S3 (NimBLE stack) and the plain ESP32 (Bluedroid). On the stand, encounters only happen in live mode. The `meet`, `duel` and `chat` commands still play an encounter with a made-up neighbor without radio. How to test on two boards and what differs from the keychain is in the [firmware README](../firmware/README.en.md).

## The key idea: no connection

The keychains **never connect** to each other. Each one only broadcasts a short advertising packet and occasionally listens to the air. Everything needed for a meeting and a duel is in the packet, and both keychains compute the duel outcome **identically and independently**. So:

- no pairing, passwords or apps;
- no waiting for one to connect to the other;
- minimal current: one transmission per second (at night, while asleep and in the dark, once every 10 s) and brief listening.

## Advertising packet

The BLE packet is service data with a 128-bit UUID (Service Data, type 0x21), 27 bytes. The keychain UUID `5f4c45b9-ab1b-4dd4-bfea-54bcf4274da6` was generated at random and belongs to no one: unlike a Company ID in manufacturer data, it doesn't need to be bought from the Bluetooth SIG. There are no flags in the packet (non-connectable advertising); the field takes 29 of 31 bytes.

| Bytes | Field | Value |
|---|---|---|
| 0–15 | UUID | keychain UUID in over-the-air order (least significant byte first): this is a keychain, not someone's earbuds |
| 16 | Version | `3` (service data with UUID; packets of earlier versions are not accepted) |
| 17–20 | Call sign | `Identity::hash`: the receiver reconstructs a code like `K7-3F2A` from it |
| 21 | Level | number of the installed implant (0–11) |
| 22 | Flags | bit 0: ready to duel, bit 1: cyberpunk theme, bit 2: legendary, bit 3: the scene is a duel (for the announcer: announced; for the responder: accepted), bit 4: "accepted" |
| 23 | Mood | `valence` in the range 0–255 |
| 24 | Countdown | in how many ×100 ms the shared scene (meeting or duel) starts with the one in bytes 25–26 (0: not announced) |
| 25–26 | To | low 16 bits of the neighbor's call sign |

## How often

| What | Mode | Average nRF52840 current (estimate) |
|---|---|---|
| Advertising | once per 1 s, 0 dBm | ~10–15 µA |
| Listening | 150 ms window every 30 s | ~30 µA |
| A neighbor nearby with whom a meeting is still possible, or waiting for a shared scene | advertising every 100 ms, 300 ms window every 1.5 s | ≈+1.2 mA (calculated from the code, as in the [power budget](power.en.md)), usually for tens of seconds; if the scene with the neighbor hasn't started within 40 s, retries follow after 1, 2, 4, 8 minutes; after a meeting the neighbor is on a break (10 minutes or longer) |
| At night, while asleep and in the dark | advertising every 10 s, no listening | ~3 µA |

**How long until a meeting.** While no neighbor is heard, the keychain listens with a 150 ms window every 30 s, and the neighbor sends a packet once per second. So the first packet isn't caught right away: according to the model, usually within 40–60 s, and occasionally after 5–7 minutes (a window catches a packet in roughly one case out of seven). The one that hears switches to frequent listening and advertising every 100 ms; the other hears it in its next window, and the scene starts about 20 seconds later. In total a meeting usually happens within 1–3 minutes, occasionally up to 5–7.

At night, while asleep and in the dark, frequent listening is not turned on, even if a neighbor is nearby: only once a shared scene has already been announced. "Night" is determined only by a set clock ([clock](../firmware/README.en.md)); without it, it's just darkness and a sleepy or sleeping face.

For comparison: the face on the screen draws 2.3–2.7 mA. One meeting is tens of seconds at +1.2 mA, about 0.01 mAh: less than 0.1% of a normal day (≈12.9 mAh). A neighbor that doesn't hear us (our signal is below its threshold) or doesn't listen itself keeps frequent listening on for no longer than 40 s per attempt: roughly 10–13% of the time it's nearby, not all the time.

## Meeting

1. The keychain hears a packet with the `"KC"` tag and a signal stronger than −70 dBm: roughly 1–3 m.
2. If the same call sign is heard twice within a minute, it's a meeting and not a passerby on an escalator.
3. The keychain with the smaller call sign announces a countdown (4 s) in its packet. The other, on hearing it, replies in its own packet with the "accepted" flag (bit 4) and the same countdown, and if it accepts a duel, also with bit 3. The announcer starts the scene only if it heard the reply; otherwise it withdraws the announcement and repeats it no sooner than after 1, 2, 4, 8 minutes. This way the scene doesn't play only on the announcer when the other didn't hear it or is busy with another one. If the other accepted a meeting instead of the announced duel (it has duels turned off), the scene for both is a meeting. Both start it at the same moment. The responder treats a nearby announcement as an ordinary neighbor packet: if it had heard the announcer only once before, it will accept the scene on the next countdown packet.

   The responder does not accept an announcement with less than 2 s left before the scene: the announcer would no longer have time to hear "accepted", because while waiting for the reply it listens with 300 ms windows every 1.5 s (`Encounters::Config::accept_min_ms`). Without a connection it's impossible to rule out a one-sided scene completely: if the "accepted" reply itself didn't reach the announcer, the responder will play the scene alone and the announcer will withdraw it. After such a scene the responder is on a break and doesn't answer repeated announcements either; once the break is over, the meeting will happen for both.
4. If the neighbor has been heard twice but the scene with it hasn't started within 40 s, the keychain stops frequent listening and tries again after 1, 2, 4, 8 minutes. This happens when the keychain with the smaller call sign doesn't hear us: it doesn't listen in the dark or at night, it hears us below its threshold, or it has hit its meeting limit. 40 s leaves margin for the neighbor's listening window every 30 s: a normal meeting has time to start within that time.
5. The call sign is looked up in the table of acquaintances in flash (`Friends`, 16 keychains). For each one it stores on how many different days they've met and when they last met.
   - **New acquaintance:** the `greet` scene "NEW FRIEND [code]", +40 experience, entry added to the table.
   - **Old acquaintance:** "HI AGAIN" and +10 experience, but no more than once a day per acquaintance. The first meeting of the day can also be a duel; it gives experience for the acquaintance just the same.
   - **Legendary keychain:** stars in the eyes after the greeting and the LEGEND MET achievement.

**Break.** With the same neighbor: no more than once every 10 minutes. If the keychains lie together and meet again and again (on the same key ring, on a desk), the break doubles each time: 10, 20, 40, 80, 160 minutes, then 160 from there on. Over 8 hours together that's 7 greetings, not 48. The streak is forgotten if there's been no meeting with this neighbor for 3 hours after the break ended (the keychains were apart), and when a new day starts; a break that's already running lasts its full term.

## Spoofing protection

Any phone or board with Bluetooth can send a packet, and packets have no signature. So a fake meeting must give no more than a real one, and rewards for meetings are limited per day:

- a scene announcement is accepted only from a neighbor that has already been heard nearby in ordinary packets twice within the last minute, and not from too far away (stronger than −85 dBm). A countdown longer than its own firmware announces (4 s) is discarded, one shorter than 2 s is not accepted, and repeated packets can only bring the start closer, never postpone it;
- a duel starts only if the owner has allowed duels. The announcer enforces one duel per day with a given neighbor and no more than 16 per day; a keychain that has already had 16 duels stops advertising readiness in its packet. This way both get the same single scene even if the responder rebooted and forgot today's duels. Experience and win counters are given for no more than 3 duels per day;
- if the neighbor's level is greater than the number of implants, the packet is discarded;
- breaks after meetings are stored in a separate table by call sign: a flood of new call signs pushes neighbors out of memory, but not the break of a real one, and doesn't stop you from meeting it; the table stores the start of the break, its length and how many meetings there were in a row, so the break stays correct even after 24.8 days without a reboot;
- meetings overall: no more than once every 20 s and no more than 6 per 15 minutes. Friends don't use up this limit and don't count towards it; the firmware considers a friend anyone with friendship of at least BUDDY, that is, met on at least 3 different days (`friend_level_days(1)`). A flood of fakes can't take away a meeting with a friend, and a call sign invented today doesn't become a friend. The break with a friend is the same as with everyone: 10 minutes or more for repeated meetings;
- per day: no more than 5 new acquaintances that give experience and count towards the friend counter, experience for 10 old acquaintances, 1 gift from an older keychain and 2 emotions won in duels; vibration only for meetings that count: a friend that's nearby all day buzzes once a day, and repeats the greeting on screen and buzzer less and less often (see "Break");
- an invisible keychain (`ghost`) meets no one: nobody hears it, and the scene would play only on its side;
- when neighbors are pushed out of memory, those heard only once go first;
- in the table of acquaintances, a new keychain doesn't push out those seen today or close friends (two days of meetings or more) until a month has passed without meeting them;
- ordinary writes of growth and acquaintances to flash happen no more than once every 10 minutes; new emotions are written immediately, but rewards for meetings are limited per day, so a flood of meetings doesn't wear out the flash;
- the firmware's packet queue doesn't accept anything weaker than −90 dBm, nor a second packet with the same call sign until the first has been processed; service data is parsed by our own parser with field length checks.

A completely fair duel is impossible without a connection: the announcer chooses the mood byte that the roll depends on. So the main protection is the limits on rewards.

## Friendship

Friendship grows with the number of days the keychains have met. However many times they meet in a day, it counts as one day.

| Days met | Level |
|---|---|
| 1 | NEW FRIEND: a new acquaintance |
| 3 | BUDDY |
| 7 | FRIENDS |
| 15 | BEST FRIENDS, an achievement |
| 30 | BFF: friends forever |

A new level brings a scene with a heart and the friend's call sign, and 15 × level experience. When the table is full, a new acquaintance pushes out the one with the weakest friendship, and on a tie, the one met longest ago. Nobody pushes out today's acquaintances or close friends (two days or more) met within the last month; if there's nobody to push out, the new acquaintance isn't recorded.

**Gift from an elder.** A keychain whose implant is two or more levels higher gives the younger one an emotion once a day (no more than one gift per day from all keychains combined) from its next implant: the GIFT scene with the giver's call sign, then the new emotion with NEW. The neighbor's level is byte 21 of the packet.

**Whisper.** Old acquaintances sometimes talk in cipher, as after a duel. The decision is made by the shared key: `chat_key(…) % 4 == 0`, on average every fourth meeting. Both keychains have the same key without exchanging messages. But each keychain decides "is this an old acquaintance" from its own table of acquaintances. Usually the tables agree: the acquaintance is recorded for both in the same meeting. But if one keychain has forgotten the other (a reset, or a full table with nobody to push out), only the one that remembers will whisper.

**Shared scenes go first.** The meeting, duel and chat go to the front of the scene queue (`Behavior::queue_shared`) and interrupt the keychain's own scene, such as an achievement. The interrupted scene finishes playing later. This way two keychains' shared scenes line up, even if one of them had something of its own in the queue.

## Duel

A duel happens if both keychains have raised the "ready to duel" flag and there hasn't been a duel with that keychain today yet (the announcer keeps track of this). Readiness is toggled with a double press of the button and is on by default, so only those who want to duel take part.

A duel is also a meeting. It counts as a day of friendship: level growth with a heart and experience, BEST FRIENDS, a gift from an elder. It also gives experience for a new or old acquaintance and the achievements for a twin and a legend. A duel has no greeting scene of its own: the duel and the chat took its place. This matters because the first meeting of the day with a neighbor that also has duels on (and they are on by default) is usually a duel. That's why two new keychains don't show each other NEW FRIEND, and a short meeting on the subway can only be a duel.

Both keychains compute the outcome identically, without exchanging messages:

```
a, b   = call signs in ascending order
nonce  = hash32(a ^ rotl(b, 13) ^ announcer_mood × 0x9E3779B9)
roll   = hash32(a ^ rotl(b, 13) ^ nonce)
bonus  = (level_a − level_b) × 4    // level helps, but doesn't decide
a wins if (roll % 100) + bonus ≥ 50
```

The keychains' day counters differ, so instead of a day number the roll uses the mood byte from the packet of whoever announced the scene. It doesn't change while the countdown is running, and both keychains know it identically.

Levels are frozen for the countdown too. Each keychain advertises, in its countdown and "accepted" packets, the level it uses itself to compute the outcome. The announcer recomputes the outcome using the level from the responder's reply. So a new implant in the middle of the countdown can't produce two winners or two losers.

Both keychains show the `duel` scene at the same time. The start moment is announced by the keychain with the smaller call sign: in its packet it writes the countdown and the low bits of the neighbor's call sign. On hearing the packet, the neighbor schedules the duel for the same moment. Advertising arrives with a delay of a few milliseconds, so the mismatch is tens of milliseconds even without a connection.

The winner gets 25 experience and unlocks an emotion from the nearest implant early (`Progress::duel_prize`, no more than two per day). The loser gets 10 experience and sulks.

## Secret chat

Right after the duel the keychains talk to each other through their buzzers: four lines in turn, six cipher signs each. Both screens show the same bubbles with signs, and at the end a "decryption" flashes for a moment followed by ACCESS DENIED. What it sounds and looks like is described in [keychain voice](sound.en.md#secret-chat).

Nothing is transmitted over radio for the chat. All it needs is what's already there:

- **Shared key** `chat_key(call_sign_a, call_sign_b, nonce)`. The order of call signs doesn't matter. `nonce` is the same as for the duel: from the call signs and the announcer's mood, so the same pair talks about something new each time.
- **Who starts**: the one with the smaller call sign (`chat_first`).
- **When**: after the duel and a fixed 2.5 s pause (`Behavior::duel` queues both scenes at once). If the duel started at the same time, so does the chat. Lines don't overlap with a mismatch of up to ±0.45 s.

## Privacy

The call sign is permanent, so in theory a keychain can be tracked over the air, like any earbuds or watch. What's provided for:

- the BLE address is a random static one, not the chip's MAC, but it's also permanent: together with the call sign the keychain is recognizable over the air. On the ESP32 stand the address is the chip's MAC: the stand is a test board, not a keychain for your pocket;
- "invisible mode" (the `ghost` command, on the keychain and on the stand): the keychain only listens and broadcasts nothing;
- there's nothing personal in the packet: only the call sign, level and mood.

## What's next

- [x] nRF52840 firmware: scheduled advertising and scanning, filtering by tag and RSSI.
- [x] List of acquaintances in flash, days by clock or by day counter.
- [x] "Ready to duel" flag via a double press of the button.
- [x] Countdown to meeting and duel in the packet, with the second keychain syncing to it; a test that the scenes match.
- [x] Packet without a Company ID: our own UUID in service data, nothing to buy.
- [x] Encounters on the ESP32 stand using the same packet: stand with stand and stand with keychain; a PC test of the stand meets it with a keychain through the test's simulated air.
- [ ] Testing on real boards: two stands on a desk, a stand and a keychain, two keychains on a desk, in backpacks and on the subway.
