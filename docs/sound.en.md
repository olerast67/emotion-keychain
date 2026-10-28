English | [Русский](sound.md)

# Keychain voice

The keychain has a 9×9 mm piezo buzzer. It answers touches with short "words" made of notes, like R2-D2, plays music for scenes, and after a duel talks to another keychain in a cipher its owner can't understand.

The sound engine (`engine/include/face/sound.h`) outputs only note frequencies and durations. The firmware plays them with PWM: LEDC on the ESP32, PWM on the nRF52840. The same code plays in the browser simulator and can be saved to WAV on a PC.

## Personality voices

The same line sounds different for different personalities: each has its own base note, its own manner and its own talkativeness.

| Personality | Note | Manner | Talkativeness |
|---|---|---|---|
| playful | 1568 Hz | staccato, with a hop before the last note | 0.9 |
| grump | 1046 Hz | low, with intervals half as wide, grumbles | 0.5 |
| sleepyhead | 1568 Hz | slow and slides down | 0.35 |
| shy one | 1319 Hz | fast, trails off on long phrases, always quiet | 0.3 |
| curious | 1175 Hz | every phrase ends with an upward question | 0.8 |
| chill | 1318 Hz | smooth glides between notes | 0.45 |
| drama queen | 1976 Hz | wide theatrical leaps, holds the last note | 1.0 |
| glitch | 1046 Hz | random digital clicks between notes | 0.7 |
| trickster | 1760 Hz | "heh-heh-heh": every note in threes | 0.75 |
| sweetie | 1397 Hz | a soft grace note a third away before each note | 0.7 |

All notes lie within 600–5400 Hz. Below that the piezo is barely audible; above it, it hurts the ears. The buzzer is loudest around 4 kHz, so the high notes of phrases stand out most.

## When it talks

The keychain doesn't beep at every change of face. The line is chosen by the emotion it has just shown: laughter gives "heh-heh", sadness three falling notes, fright a trill.

| What happened | Chance of a line |
|---|---|
| Tap | 45% × talkativeness; if the emotion has nothing to say, a "beep" |
| Double tap | 80% × talkativeness |
| Button | 90% × talkativeness |
| Shaking, a bump, picked up, a loud sound, upside down | 70% × talkativeness |
| Falling | 95% × talkativeness |
| Its own idle activities out of boredom | 10–15% × talkativeness |
| Charger plugged in, fully charged, low battery, power-off | always |

Ordinary lines play no more than once every 1.5 s and don't interrupt each other. Important ones (charging, battery, upgrade, meeting, boot) interrupt an ordinary line, but not scene music.

**Quiet hours** are from 23:00 to 6:00, and only when the time has been set (`Behavior::set_clock_known`): the keychain has no battery-backed clock, and after turning off with the button the time is unknown until it's set again ([clock](../firmware/README.en.md#console)). Without a set time there are no quiet hours, but in the dark the keychain sleeps anyway. At night the keychain doesn't chatter: only important lines play, and quietly. Scene music is silent at night, except for boot and upgrade, and the secret chat happens only on screen. The shy one speaks quietly during the day too. Sound is turned off completely with a triple press of the keychain button (rev B board only) or with the `snd` console command (on the stand and on the board).

## Lines

| id | Sounds like | Example emotions |
|---|---|---|
| `boop` | rising "beep-beep" | Boop! |
| `happy` | rising chord | Happy, Joy, Party |
| `laugh` | "heh-heh-heh" | Laughing, Giggling, Evil laugh |
| `sad` | three falling notes | Sad, Crying, Sulking |
| `angry` | sharp repeat with a semitone down | Angry, Furious, Table flip |
| `surprise` | leap up an octave | Surprised, Shocked, What? o_O |
| `scared` | trembling trill | Scared, Terrified, Screaming |
| `love` | soft chord up to the octave | In love, Adoring, Touched |
| `sleepy` | falling yawn | Sleepy, Tired |
| `hello` / `goodbye` | up / down | Hello!, Waking up / Bye! |
| `question` | upward question | Curious, Confused |
| `idea` | "ding" | Idea! |
| `error` | "bee-boop-bee-boop" | Error, GAME OVER, Knocked out |
| `charge_start` / `charge_full` | arpeggio / arpeggio with a repeat | charging |
| `battery_low` | two falling notes | Low battery |
| `level_up` | fanfare | new implant |
| `friend_hi` | double "hello" | a newly met keychain |
| `duel` / `win` / `lose` | challenge / fanfare / "sad trombone" | duel |
| `boot` | rising scale | power-on |
| `two_bits` | a pause and two notes to the tonic | answer to the "Shave and a haircut" secret knock (stand and simulator; on the rev B board the knock doesn't work yet) |

## Scene music

The music is synced to the picture: the duel outcome sound plays together with the WIN! or LOST caption, and the upgrade fanfare on INSTALLED.

| Scene | Music |
|---|---|
| `boot` | rising scale |
| `hack` | modem handshake: a 2100 Hz answer tone, trills, noise, and a fanfare on ACCESS GRANTED |
| `data_rain` | data drops: clicks of different pitch, sparser towards the end |
| `error404` | "bee-boop-bee-boop" |
| `reboot` | a downward glide as the frame fades out, the boot scale, "hello" |
| `hide` | a downward "whoosh", then a question when it peeks out |
| `upgrade` | loading ticks rising higher and a fanfare on INSTALLED |
| `new_year` | Jingle Bells |
| `birthday` | Happy Birthday, two lines |
| `halloween` | the opening of Bach's Toccata in D minor |
| `greet` | a double "hello" to a new friend, a "hello" to an old one |
| `duel` | a challenge, then a fanfare or the "sad trombone" |
| `chat` | the secret chat (below) |
| `achievement` | a fanfare as the trophy drops into place |
| `charge` | no music: the `charge_start` line plays |
| `shutdown` | "bye", then a downward glide as the frame fades out |
| `banner` | depends on the icon: heart `love`, gift `level_up`, star `idea`, rocket a launch scale, skull the Toccata |
| `bsod` | "bee-boop", and at 100% the boot sound |
| `visor` | a tone sliding up and down with the scanner light |
| `screensaver` | silence, and a fanfare when the logo hits the corner |

The holiday tunes are in the public domain: Jingle Bells (1857), Happy Birthday, Bach's Toccata. There are no third-party tunes from games or films.

## Secret chat

After a duel two keychains "talk" through their buzzers: four lines in turn, six cipher signs in each. The owners hear a melodic chirping, but there's no meaning in it for them. It's a cipher between the keychains.

**How both play the same chat.**
- Shared key: `chat_key(hash_a, hash_b, nonce)`. The order of the hashes doesn't matter; `nonce` is the BLE session number.
- Both keychains compute the lines and signs from the key identically (`chat_line`).
- The one with the smaller hash starts (`chat_first`). Lines 0 and 2 are its own, 1 and 3 are the neighbor's.
- Each plays only its own lines. In place of the other's lines it stays silent: the neighbor is playing them at that moment.
- A sign is a pentatonic note with one of two durations. A line lasts up to 1.1 s, a line with its pause gets 1.6 s, and the whole chat takes 6.4 s.

**On screen** both show the same thing:
- at the top, the neighbor's call sign and "ME";
- lines appear as bubbles: the neighbor's on the left, its own on the right;
- signs appear together with the notes;
- at the end, DECRYPT with a progress bar. For half a second a "decryption" like `HUMAN IS OK`, `TOO MANY TAPS` or `DONT TELL HUMAN` emerges from the noise, immediately followed by ACCESS DENIED.

There are 16 cipher signs. They're custom 5×5 glyphs that don't look like letters of the font. After the chat the keychain puts on an innocent face: "we didn't discuss anything."

**Synchronization.**
- `Behavior::duel(code, win, now, neighbor_hash, nonce)` queues the duel and the chat right after it.
- The pause between queued scenes is fixed at 2.5 s. A touch during the pause doesn't shift it, so the chat starts at the same time on both if the duel started at the same time.
- To make that happen, the keychain with the smaller hash announces a countdown to the start in its advertising packet, and the other syncs to it. Details are in the [encounter protocol](encounters.en.md#secret-chat).
- Lines don't overlap with a mismatch of up to ±0.45 s.

On the stand and in the simulator there's no neighbor. So `Behavior::set_chat_both_sides(true)` plays both sides on one buzzer, with the neighbor's lines in the neighbor's voice.

## Volume

On the rev B board the buzzer sits between two nRF52840 pins (`BUZ_A`, `BUZ_B`, through 1 kΩ).
- **Normal volume:** the pins are driven in antiphase, as a bridge. The swing across the piezo is doubled, which is +6 dB.
- **Quiet** (`Voice::soft()`): one pin drives, the other is held at ground.

On the stand the buzzer is on a single pin. Quiet mode there uses a 1/8 duty cycle instead of 1/2, which is roughly 9 dB quieter.

The piezo draws a few milliamps, and only while it's sounding. Lines have no effect on average current.

## Where to listen

- **In the browser:** `sim/web/build.sh`, then `sim/web/dist/index.html` and the "Buzzer" button.
  - Taps on the screen, situations and scenes play in the voice of the selected keychain.
  - The "Another keychain" group has a meeting and a duel. After the duel comes the chat: you hear both sides.
- **On a PC, to WAV:**
  ```sh
  ./build/facegen sound all out/voice.wav --serial 5   # all lines in a row in the voice of keychain #5
  ./build/facegen sound laugh out/laugh.wav --serial 9
  ./build/facegen sound duel out/duel.wav --value 1     # scene music; cue:duel is the line
  ./build/facegen sound chat out/chat.wav               # secret chat, both sides
  ```
- **On the stand:** a passive buzzer between the pin listed in the table in [firmware/README.en.md](../firmware/README.en.md) and GND. Then the commands `say laugh`, `x birthday`, `duel win`, `chat`, `snd`.

## In code

```cpp
Voice voice;
voice.init(&temperament(me.archetype), me.hash);
behavior.attach_voice(&voice);           // lines, scene music, quiet hours
...
const uint16_t hz = voice.tone(now);     // 0 means silence
pwm_tone(hz, voice.soft());              // bridge or a single pin
sleep until the earliest of engine.next_update_ms(), behavior.next_update_ms(), voice.next_change_ms()
```

The voice uses no heap and no libm: frequencies come from a semitone table, and a melody holds up to 96 notes.
