English | [Русский](engine.md)

# Face engine

The engine draws the keychain's face on a 128×64 monochrome display (SSD1306) and decides which emotion to show. The code lives in `engine/`: C++20 with no exceptions, RTTI, heap or libm. That is why the same code builds into the nRF52840 firmware, the desktop simulators and WebAssembly for the web page.

![Emotions in motion](img/highlights.gif)

## What it is made of

| File | What it does |
|---|---|
| `framebuffer.h/.cpp` | 1 KB framebuffer in native SSD1306 format. Finds changed page windows: only those are sent to the display |
| `raster.h/.cpp` | Primitives: rounded rectangle, ellipse and ring, thick lines, arcs, Bézier curves, polygons, star, heart |
| `params.h` | Face parameters: eyes, eyelids, brows, mouth, layout, plus builders for presets |
| `expressions.h/.cpp` | Emotion catalog (139 presets), overlay icons, motions, signature gestures |
| `render_face.cpp` | Draws the eyes (ring, filled, ∩, ∪, `> <`, heart, star, sparkle, spiral, cross, `$`), brows and 12 mouth types |
| `render_overlays.cpp` | 38 icons: blush, drops, Zzz, notes, "?", glasses, stubble, cotton swab, bat and more |
| `engine.h/.cpp` | `FaceEngine`: transitions, blinking, gaze, motions, gestures, clips, anti-burn-in drift, frame scheduling |
| `clock.h` | Time: instants are compared via signed difference (`time_reached`), sleep is capped at one minute |
| `behavior.h/.cpp` | `Behavior`: turns sensor events into emotions, plus mood, daily rhythm, idle activities, surprises, holidays and encounters |
| `identity.h/.cpp` | The keychain's personality from the chip serial number: archetype, traits, appearance quirks, rarity |
| `progress.h/.cpp` | Growth: experience, implants and unlocked emotions. Fixed-size flash record with a checksum: emotions unlocked beyond the implants are stored keyed by emotion name, older record versions are migrated |
| `scenes.h/.cpp` | 20 full-screen scenes (`FACE_SCENE_LIST`): boot, hacking, data rain, ERROR 404, reboot, hide and seek, upgrade, New Year, birthday, Halloween, meeting, duel, secret chat, achievement, charging, shutdown, banner, blue screen, visor, screensaver |
| `text.h/.cpp` | Text in a 3×5 font with 2×2 cells: Latin letters, digits, symbols |

## Using it in firmware

```cpp
#include "face/behavior.h"

face::FaceEngine engine;
face::Behavior character;

void setup() {
  engine.init(seed, millis());
  character.init(&engine, seed, millis());
}

void on_imu_double_tap() { character.on_event(face::Event::double_tap, millis()); }

void loop() {
  const uint32_t now = millis();
  if (face::time_reached(now, character.next_update_ms())) character.update(now);
  if (face::time_reached(now, engine.next_update_ms()) && engine.update(now)) {
    face::DirtyWindow w[face::kPages];
    int n = engine.dirty(w);
    for (int i = 0; i < n; ++i) ssd1306_send_window(w[i], engine.frame());  // 0x21/0x22 + window bytes
  }
  sleep_until_soonest(engine.next_update_ms(), character.next_update_ms());
}
```

When the face is static, `next_update_ms()` points to the next scheduled event: a blink, a glance, a gesture or a drift step. Until then the CPU sleeps, and the display holds the picture from its own memory.

Neither the engine nor the behavior schedules more than a minute ahead (`kMaxSleepMs`). `millis()` wraps around every 49.7 days, so instants are compared only through `time_reached` from `face/clock.h`: the schedule survives the wraparound, and "never" does not have to be expressed as a number.

## How an emotion is built

An emotion is a preset in `engine/src/expressions.cpp`:

```cpp
{.id = "lenny",
 .name_en = "Lenny ( ͡° ͜ʖ ͡°)",                                  // plus .name in Russian
 .cat = Category::Meme,
 .eyes = both(ring(22, 3).pupil(0, 1, 3).lid(0.35f, -10)),  // Ø22 rings, pupil, half-closed lids
 .brows = brows(brow(16, -6, 3).at(0, 4)),                    // arched brows
 .mouth = smile(20, 4, 2),                                    // smirk
 .lay = layout(50, 26, 50),                                   // eyes closer together
 .overlays = ov(O::Nose),                                     // "ʖ" nose
 .accent = Accent::Brows,                                     // signature gesture: wiggles brows
 .accent_ms = 2400,                                           // about every 2.4 s
 .hold_ms = 5000,                                             // after 5 s
 .next = Expr::smug,                                          // turns into "Smug"
 .when_en = "A sly hint, an easter egg, “you know what I mean”."},  // plus .when in Russian
```

- **Eye.** A shape plus parameters: size, radius, outline thickness, pupil or highlights, upper lid (fraction, angle, curve), lower lid, offset, scale. Everything side-dependent is specified as "positive toward the nose", so the right eye mirrors the left automatically.
- **Brows:** length, angle (+ means the inner end is raised), curve, thickness, gap above the eye.
- **Mouth:** a line or a smile that smoothly opens into "D", "o", a wave, "ω", "3", a grimace, a tongue, a scream, an open outlined mouth with teeth, a toothy grin, "□".
- **Icons** — a bitmask of 38 overlays, each with its own animation.
- **Motion** — continuous: bouncing, trembling, swaying, pulsing hearts.
- **Signature gesture** — a short movement every few seconds: breathing, brows, "chatter", squint, tilt, side-eye, eye twitch. Thanks to gestures no emotion ever looks frozen, and frames are only needed while a gesture plays.
- **`hold_ms` and `next`** — how long the emotion lasts and what it turns into. Reactions end on their own, even without the behavior module.

### Adding an emotion

1. Add the id to `FACE_EXPR_LIST` (`engine/include/face/expressions.h`).
2. Add the preset to `kExpressions` at the same position in the list. If the order does not match, a `static_assert` and a test will catch it.
3. Look at the result: `facegen still my_emotion out.gif --scale 6` and `facegen anim my_emotion out.gif`.
4. Rebuild the catalog: `facegen catalog docs/emotions.md`.

## Transitions and "liveliness"

- **Matching eye and mouth shapes** (ring → ring, smile → open mouth) are interpolated smoothly over 150–500 ms with ease-in-out.
- **A shape change** (ring → `> <`, heart, spiral) is hidden behind a blink: the eye closes, the shape is swapped on the closed frame, the eye opens. When the mouth changes type it squeezes shut and opens again.
- **A blink** lasts about 180 ms, with squash and stretch. Pauses are 2.5–6.5 s (each emotion has its own), sometimes a double blink.
- **Gaze** — saccades over 50–100 ms with a squash frame. Eyes, pupils and mouth shift by different amounts, and the eye on the side of the gaze gets slightly bigger.
- **The pupil** never hides fully under the lid: it peeks out from under it.
- **Drift** — the whole face shifts by 1 px every 20–40 s within ±3 px so the OLED does not burn in.

## Power saving

- **A frame is drawn only when the face changes.** If the hash of all parameters matches, rendering is skipped entirely.
- **A frame is needed exactly when the picture changes.** A preset's motion moves the face in whole pixels, and the engine wakes up at the moment of the next shift. A step of a slow motion (swaying while asleep) waits up to 200 ms and goes out together with an already scheduled frame.
- **Each icon has its own clock.** Blinking icons (ellipsis, battery, sound waves, light bulb) wake the CPU only when the picture changes, smooth ones roughly once per pixel of movement: Zzz 5 times per second, even if a fast animation is running next to it. The "!" freezes once the shake is over. An icon's time never grows past its animation cycle, so after a week on one emotion everything moves just as it did in the first minute.
- **Nearby wakeups are merged:** events closer than 20 ms to each other are handled in a single wakeup.
- **Calm face.** After 3 minutes without attention (`EngineOptions::calm_after_ms`; attention means sensor events, the button, commands), signature gestures come three times less often and glances half as often. A neutral face runs at about 3 frames per second instead of 5.
- **Only changed page windows are sent to the display.** A neutral face is about 1.2 KB/s over I2C, roughly 3% of a 400 kHz bus; once calm, about 0.8 KB/s.
- **Long-lasting states are made cheap:** sleep, tiredness, low battery. Shivering from cold comes in bouts, 1 s out of 4, and the CPU sleeps between bouts.
- **Brightness follows the surroundings.** `Behavior::brightness()` is normal, dimmed at night and during sleep, minimal in the dark and in a pocket. The firmware maps the level to contrast.
- **The time of day may be unknown.** `set_clock(minutes)` sets the minutes of the current day, `set_clock_known(false)` says there is no real time (after being turned off with the button, the keychain loses its clock). Then the minutes are only a day counter for the feeding limit, and everything "by the clock" is off: night (dimmed screen, quiet voice, falling asleep when idle, tiring three times faster), sleep deprivation, evening contentment, time-based easter eggs and the NIGHT OWL and EARLY BIRD achievements. The keychain still sees darkness through the light sensor without a clock. By default the clock is known.

`facegen bench` shows the load for each emotion: frame time, frames per second, bytes per second, bus utilization.

## Behavior

`Behavior` maps events to emotions using a table of situations:

| Event | Reaction (pool of options) |
|---|---|
| Tap | Boop!, Wink, Curious, OwO, Awkward •_•, What? o_O |
| Double tap | Laughing, Happy, Playful, Giggling, Tears of joy, Kiss — that is without a personality. On the keychain a personality is always present, and a double tap uses its answer (`dtap`, see "Personality") |
| Triple tap | Playful, Giggling, Tears of joy, Dying of laughter, Troll grin |
| 4–5 taps in a row | Annoyed, Unamused -_-, Bruh, Skeptical, Disgusted |
| 6–8 taps | Angry, Eye twitch, Disapproval ಠ_ಠ |
| 9+ taps within 3 s | Furious, FFFUUU, Table flip |
| A tap within 3 minutes of an outburst (6+ taps), once the anger has passed | Awkward: Apologetic, Guilty, Embarrassed |
| Picked up | Happy, Curious, Alert, Suspicious; in a good mood also Kiss, UwU |
| Shaken | Surprised and "shakes it off"; harder: Dizzy, Hypnotized, Screaming; harder still: Motion-sick |
| Fall and impact | Terrified, then Knocked out; the second time Crying or Sobbing; the third within 10 minutes: GAME OVER |
| Loud sound | Startled, What? o_O, Screaming soyjak, Mind blown; repeated: Annoyed, Disgusted |
| Speech, music | Listening, Alert; music: Vibing, dancing, Singing |
| Dark, light | Who turned off the lights?, then sleep; light after darkness: Squinting at the light, then Hello! |
| Walking, running | Excited, "going for a walk" (after a break longer than 2 minutes), Walking; Running, Challenge accepted, Determined; running longer than 5 minutes: Exhausted. When it stops, after 2.5 s without steps the face returns to normal |
| Charging | Grateful if it was hungry; "eating"; full charge: Charged, Pog!, `$_$`; unplugged before full: Disappointed |
| Night (only with the clock set) | Played with after 1 a.m.: Sleep-deprived; falls asleep when idle. Does not fall asleep on the move (walking or running) |
| Alone for a long time | Bored, Hey! (calling) (if the keychain is moving), Lonely, Sulking; when you come back: Touched |
| Meetings and duels | A greeting depending on the pair of personalities, Starstruck eyes for a legendary neighbor; after a duel: Proud, Smug, Starstruck or Sulking, Grumpy, Sobbing |

Recent reactions come up less often so the character does not repeat itself. The engine's emotion chains (Laughing → Happy) play out: the personality does not interrupt the continuation, but only yields to chains for up to 20 s after its own pick. When nothing is happening, it finds something to do on its own every 12–40 seconds: looks around, yawns, sighs, hums, gets thoughtful, focuses, gets nervous. Meme emotions are rare, like seasoning: UwU and Dying of laughter in a good mood, Doomer out of boredom or sadness. Cyclops at most once every 2 hours.

The character shows every emotion in the catalog on its own, without reward scenes: the `test_emotion_reachability` test lives through a scripted day with each personality and checks this. Only three appear after scenes on purpose: Proud and Starstruck after winning a duel, an achievement or meeting a legend, and the ghost (the Halloween emotion) on Halloween and Friday the 13th.

## Personality

`make_identity(serial)` turns the chip's serial number into a character. The same number always gives the same character, so nothing needs to be stored.

- **Personality** — one of ten archetypes, each with its own temperament (`temperament.h`). It changes behavior, not percentages:
  - the face at rest, when happy and when bored;
  - the response to a touch and a double touch;
  - what it does when bored;
  - the pace of transitions, blinking, the appetite for memes;
  - its favorite surprise;
  - the buzzer voice: base note, manner and talkativeness ([the keychain's voice](sound.en.md)).

  A personality shows its own emotions from day one, even if their implant is not installed yet.

  | Personality | At rest | Happy | Bored | On touch | Trait |
  |---|---|---|---|---|---|
  | playful `PLAYFUL` | happy | joy | playful | giggles, winks | most active, abrupt transitions |
  | grump `GRUMPY` | unamused | smug | poker_face | rolls its eyes | a smile has to be earned |
  | sleepyhead `SLEEPY` | tired | bliss | sleepy | wakes up | slow, blinks less, tires sooner |
  | shy one `SHY` | shy | content | lonely | gets embarrassed, blushes | hides |
  | curious `CURIOUS` | curious | excited | thinking | "oh?", "Idea!" | always looking around |
  | chill `CHILL` | calm | vibing | zoned_out | "Cool" | smooth and unhurried |
  | drama queen `DRAMA` | neutral | tears_of_joy | pleading | gasps, touched | everything over the top, fastest transitions |
  | glitch `GLITCH` | robot | owo | processing | Error, Processing | small picture glitches in any theme |
  | trickster `TRICKSTER` | smug | mischievous | lenny | devilish grin | the most memes |
  | sweetie `SWEET` | content | adoring | daydreaming | hearts | loves everyone |

  The personality is derived from the keychain's hash, so a neighbor can tell it from the BLE advertising packet (`identity_from_hash`). The meeting depends on the pair: on the meeting screen, the neighbor's personality appears under its call sign, or `TWIN!` if it is the same one. Then the reaction follows the personality: the shy one gets embarrassed, the sweetie falls in love, the grump rolls its eyes at the playful one, the playful one teases the grump, two glitches start a data rain.
- **Appearance** (`Quirks`):
  - eye placement ±2 px;
  - eye size ±5%;
  - pupil size;
  - blink rate;
  - brow height.

  Two keychains showing the same emotion look slightly different.
- **Rarity:**
  - ~10% of keychains are rare: noticeably bigger pupils or wide-set eyes;
  - ~1% are legendary, with a cyber eye: a square right pupil and a flash every 5 seconds, and tick marks around the eye while the keychain is "scanning".
- **Call sign** like `K7-3F2A`: for the screen and for meeting other keychains.

`facegen id <number>` shows who you get from a serial number.

## Growth and implants

A new keychain knows 44 base emotions; the remaining 95 arrive as 11 implants earned with experience:

| Implant | XP | What it unlocks |
|---|---|---|
| EMPATHY.MOD — Empathy module | 150 | Giggling, Adoring, Grateful, Embarrassed, Guilty… |
| REFLEX.SYS — Reflexes | 350 | Excited, Shocked, Amazed, Terrified, Nervous, Worried |
| SOCIAL.LIB — Social skills | 600 | Kiss, Playful, Proud, Pretty please, Idea! |
| MEME.DRV — Meme driver | 900 | Shrug ¯\_(ツ)_/¯, Poker face, NPC, Bruh, Lenny |
| DRAMA.EXE — Drama | 1250 | Sobbing, Furious, Disgusted, Knocked out |
| SARCASM.DLL — Sarcasm | 1650 | Smug, Disapproval ಠ_ಠ, Cringe, Sigma stare, What? o_O |
| MEME.DRV2 — Meme driver 2 | 2100 | Hmm…, Me gusta, Cat with a cotton swab, soyjaks, Doomer |
| PARTY.PKG — Party | 2600 | Party, Singing, Vibing, Cool, UwU, `$_$` |
| GLITCH.CORE — Glitch core | 3150 | Robot, Derp, Hypnotized, Sleep-deprived, Motion-sick |
| CHAOS.BIN — Chaos | 3750 | FFFUUU, Table flip, Troll grin, Evil laugh, Mind blown |
| GHOST.SHELL — Ghost in the shell | 4400 | Halloween (ghost), Boo!, Dying of laughter, GAME OVER, Cyclops |

Experience comes from:
- **a day together** — 60;
- **a touch** — 2, up to 40 per day;
- **steps** — 1 per 100 steps, up to 50 per day;
- **a new keychain met** — 40, a familiar one — 10;
- **a duel** — 25 for a win, 10 for a loss;
- **charging it while hungry** (at low battery) — 15, once a day;
- **achievements** — 20–100 each, see below;
- **a growing friendship** — 15 × friendship level.

With active wear everything unlocks in about a month. Touches and steps are capped per day so the keychain grows from time spent together rather than from shaking. Winning a duel unlocks one emotion from the next implant early.

An emotion is unlocked if its implant is installed or it is on the extra list. That list holds emotions won in duels and gifted by older keychains, up to 16 of them. The list is keyed by emotion name (FNV-1a of the id) rather than by catalog index, so new emotions and reordering in the catalog do not corrupt saved progress. Installing an implant frees up space in the list. The `Progress` record has a fixed size (128 bytes) with room for future fields. `Progress::load` reads version 1 and 2 records and converts them to the current, third version.

`Behavior` with progress attached (`attach_progress`) shows only unlocked emotions. A locked one is replaced by the nearest unlocked one (`furious` → `angry`), including inside chains like Laughing → Happy. Installing an implant plays the `upgrade` scene, after which the character shows one of the new emotions with a blinking NEW label in the corner (`FaceEngine::badge`). An emotion won in a duel or gifted is marked the same way.

### Streak and achievements

**Streak** — how many days in a row the owner was around, i.e. touched the keychain at least once. A day without touches resets the streak; the record stays.

**Achievements** unlock once: the `achievement` scene with a trophy and the title, then experience. There are 24:

| Achievement | For |
|---|---|
| FIRST FRIEND, 10 FRIENDS | the first and the tenth keychain met |
| FIRST WIN, 10 WINS | the first and the tenth duel win |
| NEVER GIVE UP | five defeats and still fighting |
| 100 TAPS, 1000 TAPS | touches |
| 10K STEPS, MARATHON | 10,000 and 55,000 steps together |
| 3, 7 and 30 DAY STREAK | a streak of days with the owner |
| DAY 100 | a hundred days together |
| FULL UPGRADE | all implants installed |
| 5 SECRETS | five secret chats |
| CARING HUMAN | charged five times while hungry |
| SURVIVOR | survived ten falls |
| TWIN FOUND | met a keychain with the same personality |
| LEGEND MET | met a legendary keychain |
| NIGHT OWL, EARLY BIRD | played with at 3 a.m. and at 5 a.m. |
| KNOCK KNOCK | the secret knock (cannot be unlocked on the rev B board yet) |
| 1337 | was online at 13:37 |
| BEST FRIENDS | friendship with one keychain up to the BEST FRIENDS level |

The list with descriptions is `achievement(Ach)` in `progress.h`; on the stand, the `ach` command.

## Scenes and surprises

`FaceEngine::play_scene` shows a scene instead of the face for a few seconds. The face keeps living underneath, and the scene ends on its own.

- **Surprises** are launched by the behavior every 1–2.5 days of waking time, except in eco mode:
  - hacking with scrolling code and ACCESS GRANTED;
  - data rain;
  - ERROR 404: MOOD NOT FOUND;
  - reboot, where the frame collapses like an old CRT;
  - hide and seek, where the character slides down and peeks out;
  - blue screen: ":( YOUR KEYCHAIN CRASHED" and a percentage up to 100;
  - visor: a scanner light runs back and forth, like the Cylons and KITT.

  After a surprise comes a fitting emotion: Troll grin after hacking, Hypnotized after data rain, Dizzy after the blue screen. Each personality's favorite surprise comes up three times as often.
- **Holidays and date-based easter eggs** — once per date, while the character is awake:

  | Date | On screen |
  |---|---|
  | December 31 and January 1 | a tree and HAPPY NEW YEAR |
  | February 14 | LOVE DAY <3 |
  | February 29 | FEB 29 — BONUS DAY |
  | March 14 | PI DAY 3.14159265 |
  | April 1 | a blue screen, then GOTCHA! |
  | April 12 | POEKHALI! with a rocket (Cosmonautics Day; "Poyekhali!" is Gagarin's "Let's go!") |
  | May 4 | MAY THE 4TH — BE WITH YOU |
  | May 25 | DONT PANIC — TOWEL DAY 42 |
  | 256th day of the year | HELLO WORLD — DAY 256 |
  | Friday the 13th | FRIDAY 13 — BE CAREFUL, a skull |
  | October 31 | a pumpkin |
  | the owner's birthday (`set_birthday`) | a cake; takes priority over any holiday on the same day |
- **Time-based easter eggs** — once a day for each time (the day starts at midnight), only with the clock set: without it (`set_clock_known(false)`) "13:37" and "3 AM" would show up at random times:
  - 13:37 — "1337 H4X0R MODE" and an achievement;
  - 11:11 — MAKE A WISH with a star;
  - 12:34 — "1 2 3 4!";
  - 03:00, if awake — GO TO SLEEP;
  - 04:04 — ERROR 404: TIME NOT FOUND.
- **Secret knock.** The "Shave and a haircut" rhythm in five taps: quarter, two eighths, two quarters, with ±35% tolerance. The keychain answers with "two bits" on the buzzer and winks. It needs single taps: the stand knocks out the rhythm with the `knock` command, and in the simulator you can tap on the screen. On the rev B board the knock does not work yet: the firmware waits 0.5 s to see whether a touch becomes a double tap, skips touches inside that window, and the sensor itself merges "knock-knock" into a double tap (`poll_motion` in `firmware/src/keychain/main.cpp`). That is why 23 of the 24 achievements can be unlocked on the keychain ([plan](plan.en.md)).
- **Charging screensaver.** If the keychain is charging and nobody touches it for ten minutes, the OLERAST logo bounces off the edges like the old DVD screensaver. The paths are chosen so that after 10 seconds it hits a corner exactly: CORNER!
- **"OLERAST LINK" boot** — at power-on.

All scenes keep the same safe margin and minimum line thickness as the emotions. A test checks this.

## Power

- **Charging.** When the charger is plugged in, the `charge` scene plays: a battery fills up to its percentage (`set_battery_percent`) with a lightning bolt on it. Then comes a reaction: ordinary joy, gratitude if it was hungry, or relief at the last moment.
- **Eco mode.** At low battery, frames are less frequent (50 ms instead of 33, at critical level 80), idle activities half as often, no surprises. Everything returns to normal while charging.
- **Care.** Charging it while hungry gives experience once a day and counts toward the CARING HUMAN achievement. It counts at most once every 3 hours within one day (by the minutes of the day from `set_clock`). The feeding mark is stored in the growth record (`Progress::fed_at`, from the reserve, without a new record version) and is reset by a new day: neither the cable nor turning it off and on with the button helps game the achievement.
- **After charging.** Unplugged before full, hunger and eco mode do not stick until the next full charge: the charge is measured again without USB, and if the cell never filled up, `BatterySensor` reports "low battery" again after 30 s. For the first 30 s after unplugging (and after power-on without USB), the percentage is taken from the highest reading: right after unplugging, the system runs through the diode and sees the cell half a volt lower. "Low", "almost empty" and "time to shut down" are not reported during those 30 s.
- **Shutdown.** `power_off()` starts the farewell immediately: the running scene is cut off together with its music, the scene queue (own and shared) is cleared, and a shared scene that arrives during the farewell does not interrupt it. The scene is GOODNIGHT; `power_off(now, true)` (empty cell) first shows LOW BATTERY with a battery. The frame collapses like a CRT, and the character sleeps. On the rev B board only the button and plugging in USB wake it from shutdown (a touch does not). The firmware powers off based on `scene_ends_ms()`.

## Themes

`EngineOptions::theme`: classic, cyberpunk (`Theme::Cyber`) and retro (`Theme::Retro`).

**Retro** draws the face with large 2×2 pixels, like the screen of an old handheld console. The squares start at odd coordinates, so the safe margin does not shrink. Scenes and text stay sharp. Surprises in this theme lean toward reboot and the blue screen.

**Cyberpunk** adds:
- **a targeting reticle when it matters, not all the time.** The corners slide in toward the face, hold for a couple of seconds and blink out (`scan()`, `reticle_visible()`). This happens in three cases:
  - on "sharp" emotions: Focused, Suspicious, Determined, Alert, Curious, Skeptical, Squinting at the light, Challenge accepted, Sigma stare;
  - occasionally on its own, every 3–8 minutes: "scanning";
  - on an external command, for example when another keychain is spotted nearby. In the meeting scene the friend's call sign is also put in the reticle.
- **rare short signal glitches:** every 45–150 seconds for 140 ms.

Surprises in this theme lean toward hacking, data and the visor. A legendary keychain's cyber eye is visible in any theme.

## Tilt

`set_tilt(x, y)` takes the gravity projection from the accelerometer, and the pupils smoothly roll toward the lower side. The stronger the tilt, the less is left of the character's own gaze. `jerk(dx, dy)` is a sudden movement: the character glances that way for a moment.

Tilt changes below 0.05 are sensor noise: they do not wake the engine. The pupils follow the tilt in steps of 0.05, and the engine wakes only when the step changes. A jerk in the direction the character is already looking only extends the glance. NaN and infinities are discarded in all engine inputs.

Meetings with other keychains over Bluetooth are described in [encounters.en.md](encounters.en.md).

## Simulators

| Tool | What it does |
|---|---|
| `sim/web` | A page with the engine in WebAssembly. Includes: emotions, situations, gestures, personality and scenes; three themes and tilt; the buzzer; meetings, duels and friendship with a neighbor; growth "from scratch" and achievements; calendar and easter eggs; bus load. Build with `sim/web/build.sh`, output is `sim/web/dist/index.html`. Live at <https://olerast67.github.io/emotion-keychain/> |
| `facegen` | Command-line tool: emotion GIFs, transitions, clips, scenes (`scene`), contact sheets, scenarios, benchmark, catalog, personality from a serial number (`id`), voice to WAV (`sound`). Options `--theme cyber` and `--serial N` |
| `facesim` | SDL2 window: arrow keys page through emotions, number keys play clips, G shows changed windows |

## Memes and rights

The meme emotions (soyjak, the smug guy in glasses, Me gusta, FFFUUU, Lenny and others) are redrawn from scratch in our character's style: the same ring eyes and line thickness. The original images are not copied. But some memes have rights holders (Trollface, rage comic faces and others), and in the code and the catalog they still have recognizable names: `troll_grin`, `me_gusta`, "FFFUUU". For gifts this does not matter. If the keychains ever go on sale, these must be reviewed and, if needed, redrawn and renamed, and meme names must not be used on packaging or in advertising.
