# Hitbox expander + grab reach (stage D11b)

**Short version:** Menu -> **Troll** tab -> **Hitbox expander**: one switch and one slider.
- The slider goes from **1.0x** (normal) to **10.0x** (10 times as big), in steps of 0.5.
- When the switch is ON, your **two hands** get bigger in **two ways**:
  1. their hitboxes (the invisible solid shapes - this is what you could already *touch* the ball with),
  2. the game's own **grab-reach numbers** (how far away a hand can still grab the ball) - **this is new** and is what should fix "I feel the vibration but I can't grab it".
- The switch starts **OFF** every time the game starts (never saved, on purpose). The slider remembers where you left it.

## What changed since the last version (stage D11)
- **See hitbox is removed** (you said it did not really work, and nothing needs it).
- **Slider is now 1x to 10x** (was 1x to 5x).
- **The fix for "touch works, grab does not":** last time only the hitboxes got bigger. Hitboxes decide *touching* (bounce + vibration). The game decides *grabbing* with other numbers. Now those grab numbers get the same factor.

## Honest status (read this first)
- **Tested only on a PC against a PRETEND game.** It has never run in your real game.
- What the PC tests prove: the menu and the slider work (1.0x ... 10.0x), the mod finds your two hands, makes the hitboxes and the grab numbers exactly that many times bigger **starting from the game's own values** (moving the slider never piles up), puts the game's own values back when you switch off, ignores other players' hands, copes with the game changing values itself, with destroyed hands, with engine errors, and never disturbs your Aimbot or Shot points.
- **What I could NOT verify (and cannot from here):**
  1. **Which of the grab numbers the game really uses.** From the game's files I found four numbers that look like "how far can a hand grab": `Hand.reachDistance`, `Hand.palmRadius`, `BallControl._gravityDistance` (the game's value was 0.07 in the last file) and `BallControl._grabVolume` (the size of the grab area on the hand). I scale **all four together**. I do not know which one(s) the game checks. The facts file will tell me what each one was and whether the game changed it back.
  2. **A number that is written into the game's code cannot be changed.** The game also has a fixed grab-volume constant (`_GRAB_VOLUME_BASKETBALL`). If the game reads that one and ignores the others, the grab will not reach further. The new scan writes out the grab code so I can check this.
  3. **Big values can make things jumpy.** Bigger hitboxes can push the ball away, make it bounce or shake in your hand, or make your hands feel strange. **Start at 2x - 3x** and go up slowly. 10x may be too much.
  4. **Other players.** In an online game the other player's phone/headset may decide steals and grabs on *their* side. They may not see or feel your bigger reach. I can't know this without a test.
  5. **The engine calls.** I use the engine's normal "set size" calls for the hitboxes. If a call is missing, the page says so and the mod does only what it can.

## What it touches
- **Only your two hands:** their hitboxes and the four grab-reach numbers. **Other players' hands are never touched.**
- Hitboxes of kinds that can't be resized (mesh) are only counted.
- Everything is **put back to the game's own value** when you switch the expander OFF, move the slider back to 1.0x, the game replaces your hands, or you die/respawn. If the game changes a value itself, the mod notices (it looks 10 times a second for grab numbers, every 2 seconds for hitboxes), sets it again, and writes a line about it.
- A hitbox that would end up bigger than 50 m is left alone.
- If the engine refuses 12 calls in a row, the mod stops by itself and the page says why.
- It works **alone** and **together** with the Aimbot, the Aimbot Bank and Shot points.
- **Heads-up:** this changes your own hands in a game other people are in. Use it where that is OK.

## Short test checklist (about 10 minutes)
1. Start the game -> menu -> **Troll** tab. Turn **Hitbox expander** ON (slider at 2.0x). Under the slider the line should change from "looking for your hands..." to something like **"connected: 2 hands, 8 hitboxes + 6 grab values made 2.0x bigger"** within a few seconds. If it says **FAILED**, press **Get facts** in the patcher and send me the file.
2. **Grab test:** throw the ball and try to grab it again from **further away** than you normally could. Does it work now? (At 1.0x / switch OFF, note how far you can grab. Then compare at 2x.)
3. Try **3x**, then **5x**, then **10x**. Each time: can you grab from further? Does the ball **shake, bounce off your hand, stick, or fly away**? Do your **hands** shake?
4. Try a **steal** and a **block** with a bot or another player. Is it easier?
5. If you can, ask the other player: do they see anything different? Do your grabs and steals work on them the same as on a bot?
6. Turn the switch **OFF**: everything should feel normal again right away.
7. Optional: turn the Aimbot at the same time. It should behave as before.
8. Press **Basketball -> Scan ball and hoops** once, then **Get facts** in the patcher and send me the file. Tell me in a few words what you felt (which factor was the first where the grab reached further? anything weird?).

## What to send me
The **facts file** after the test, plus a few words on what you saw and felt. In the facts file look for lines starting with `hitbox:`. They say:
- `found where your hands keep their hitboxes ...` - the hands were found.
- `GRAB-REACH values found (...) | not in this game: ...` - which of the four grab numbers exist in your game.
- `the game's own grab-reach values - left hand: ... | right hand: ...` - the real numbers before I touched them.
- `set N grab-reach values of your hands to 2.0x their own value` - the mod did its job. (And `set N hitboxes ...` for the hitboxes.)
- `the game changed N of your grab-reach values since I set them ...` - the game puts its own numbers back (how often).
- `your hands have N hitboxes ...` with the names and sizes of every hitbox.
- the final `hitbox: link ...` summary with all counters.

The **Scan ball and hoops** button now also writes the **machine code** of the game's grab functions (`code640` lines: `Basketball.SetInSphereGrab`, `OnTriggerEnter`, `OnTriggerExit`, `BallControl.IsHandNearBasketball`, `InitGrabVolumes`, `ProcessHand`, `IsGripped`, `Hand.TryGrab`, `GetReachTarget`, `OverlapPalm` and other grab functions of the hand). From this I can read which number the game checks, even if the simple version does not help. (To make room, the older classes - shot assist, hoops, scoring - are written shorter than before.)

## Safety
- Nothing is saved except the slider position. The switch is OFF at every start.
- If anything is missing or wrong (a field renamed after a game update, a hand that was destroyed, an engine call that fails), the mod stops, says why on the Troll page and in the facts file, and changes nothing else. The game keeps running.
- The updater, the backup and Restore are not touched.

## Parked
**Shot points** (the points slider) is still on the Troll page but did not work in the last test (the game rewrites its own number). I have kept it for later, as you asked.
