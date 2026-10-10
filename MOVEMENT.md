# Movement page (stage D6)

## What is real, what is not
| Part | State |
|---|---|
| Movement page in the menu (4 switches, 4 sliders with exact numbers, saved slider values) | Built, works on the headset |
| The rules (off = original value back, no stacking, one gravity mode, Fly wins) | Built and tested on a PC (38 checks) |
| "Scan game code" button | Works on the headset (stage D5c file) |
| **Game link: finding the headset's player object and writing into it (Speed, Jump, Low / High Gravity)** | **Built in stage D6. Tested on a PC against a pretend game (77 link checks + a whole-payload run). NOT yet seen working in the real game.** |
| **Fly** | **Does not exist yet.** The rules already have a "Fly is on" input so Fly will win over the boosts once it exists. |

## What the game link does (game_link.cpp)
Everything below was read from the real game by the stage D5c scan (names and exact positions), nothing is guessed:
- The headset game keeps the player's movement numbers in ONE running object of the class `ShovelTools.PlayerLocomotion`
  (the stage D5c file found 2 copies: the active player, and an inactive one with head tracking never started).
- Nothing happens until you turn a movement switch on. Then the link (on its own thread) asks the game's runtime for the class and its
  field positions, looks through the game's memory (about 3 seconds, low priority, read-only) and keeps the copy that is the ACTIVE player
  (`_hmdTrackingInitialized` is true and all the numbers are believable).
- Fields it changes, always as `the game's own value x factor` (never "current x factor", so nothing can pile up):
  | Switch | Fields (all floats) |
  |---|---|
  | Speed Boost | `_forwardMaxSpeed`, `_lateralMaxSpeed`, `_backwardMaxSpeed` and their accelerations / decelerations (9 numbers: the top speed rises and is reached just as fast) |
  | Jump Boost | `_jumpHeightMultiplier` |
  | Low / High Gravity | `_gravity` (all 3 numbers of the vector) |
- The setter code bytes in the facts file confirm the positions (`str s0,[x0,#684]` = `_forwardMaxSpeed`, `#732` = `_jumpHeightMultiplier`).
- It re-checks the object about 100 times a second while a switch is on. If the game writes its own value back, ours is put back.
  If the game sets a NEW value of its own (a new setting arrived), that becomes the new "original". A value that is far off the usual
  (more than 5x) is left alone and reported.
- Turning a switch off, or the slider going back to "no change", writes the game's own value back exactly.
- If the object disappears (new scene) the link notices, shows "looking..." in the menu and finds the new one by itself.
- Every read and write goes through a "safe copy" (the kernel says "no" for a bad address instead of the game crashing).

NOT verified (only the headset can tell): that writing these fields really changes the movement; that `_jumpHeightMultiplier` makes the
jump 2x as HIGH at 2.0x (if the game uses it as launch speed the jump would be even higher - the facts file records the peak jump speed);
how `_gravity` interacts with the jump arc; whether the game re-applies its config over our numbers (the facts file counts it);
whether the online game notices or dislikes changed speeds.

## The rules (movement.cpp)
- Everything starts OFF. Switches are never saved; slider values are saved.
- A value is always `original x factor`, never `current x factor`, so nothing can stack.
- Turning a switch off, or moving to "no change", puts the game's original value back exactly.
- Low Gravity multiplies gravity by `1 - pct/100` (90% -> 10% of normal). High Gravity by `1 + pct/100` (90% -> 190%).
  Only one gravity mode is on at a time (one setting, 0 / low / high).
- Jump Boost is meant as jump HEIGHT. The game has a number literally called `_jumpHeightMultiplier`, so that is what is scaled
  (1.5 in the game -> 3.0 at 2.0x). Whether that gives exactly twice the height is one of the things to check in the headset.
- When Fly is active (not built yet) the boosts step aside and come back afterwards.

## Slider ranges
| Slider | Range | Step | Starts at |
|---|---|---|---|
| Speed Boost | 1.1x - 5.0x | 0.1x | 1.1x |
| Jump Boost | 1.1x - 5.0x | 0.1x | 1.1x |
| Low Gravity | 0% - 90% | 5% | 0% |
| High Gravity | 0% - 90% | 5% | 0% |

## The scan (how we get real game code without guessing)
1. Menu -> Movement -> **Scan game code**. "Scanning..." shows from the moment you click - that is normal.
   It should end in about 10-60 seconds with **"Scan done"** (or "Scan failed"). The menu never stays on "Scanning" for more than about 4 minutes.
2. In the patcher press **Get facts**, send me the file.
What the file now contains (read-only, nothing in the game is changed):
- an INDEX line for every class of the game's own code whose name looks like movement / player / character / parameters;
- the full list of fields and methods of the important classes (MobilePlayerLocomotion, MobileVerticalMotion, ...),
  with where each method's code starts inside libil2cpp.so (`rva=`) - confirmed on the real game (876 method locations found);
- progress lines ("scan: step 4 of 6 ...") so I can see exactly how far it got;
- the LIVE values: the scan looks through the game's memory for the running copy of those classes and writes down what
  their fields hold right now (speed, jump numbers, gravity ...). That tells me which field is the real speed/jump.
  If the game has more than one copy (for example other players), only the fields that differ are listed for the extra copies.

What went wrong with the second real scan (stage D5): the class lists were written, then the scan went silent and never said "done".
Most likely cause (reproduced on a PC, NOT proven on the headset): the memory search wrote 64 KB at a time into a pipe; if the
system gave us a smaller pipe, that write waits forever. Stage D5b fixes it three ways:
- the pipe is never waited on (non-blocking), its real size is asked for and respected, and it is tested before use;
- the memory search runs on its own thread; if it makes no progress for 10 seconds the scan gives up on it, says WHERE it was stuck
  ("MEMORY SEARCH STUCK ... region N ...") and still finishes with "Scan done" (the class lists are complete);
- a second guard around the whole scan: after about 4 minutes it writes "GAVE UP waiting ... still in step ..." and the menu says "Scan failed".
Tested only on a PC with a pretend game (tiny pipe, frozen read, frozen runtime). Not yet tried on the headset.

## What the third scan (stage D5b) taught us - and what stage D5c does about it
Verified in the facts file from the headset:
- The memory search now works (about 3 seconds for 1.6 GB, 560 regions, no trouble). The copy pipe was a normal 128 KB one, so my
  "small pipe" guess for the old hang was probably NOT the cause. The old hang is still unexplained; the new design (own thread, non-blocking,
  watchdog, Java-heap memory skipped) simply does not hang.
- **The headset version of the game does NOT use `MobilePlayerLocomotion`** (that is the phone / PC version; its running copies were
  garbage-looking, probably stale). The headset class is **`ShovelTools.PlayerLocomotion`** (211 fields, 201 methods).
  It has one tiny setter per movement number: `SetForwardMaxSpeed`, `SetLateralMaxSpeed`, `SetBackwardMaxSpeed`, `SetJumpMaxSpeed`,
  `SetJumpAcceleration`, `SetJumpDeceleration`, `SetJumpHeightMultiplier` ... and the game's parameter system calls them
  (`OnLocomotionParameterForwardMaxSpeed`, `...WalkSpeedMultiplier`, `...JumpHeightMultiplier`, `...JumpHangTime` ...).
  The values come from the game's config (`LocomotionManager`, `LocomotionParams` keys), so the game can re-apply them at any time.
- The file that reached me was cut off at 265,741 bytes (the same size twice) - the live values of the end were lost. Stage D5c writes a
  much shorter file (BRIEF scan).
NOT verified yet: which field each setter writes (stage D5c prints the first bytes of each setter's code so I can read it), what the real
running `PlayerLocomotion` holds, whether writing those fields changes the movement, and whether the game re-applies its config over our change.

If the game closes while scanning, tell me.

## Stage D6 test checklist (in the headset)
1. Open the menu -> Movement. "Game link: waiting".
2. Turn **Speed Boost** on at 2.0x. The link says "looking..." for a few seconds, then "connected". Walk: you should be about twice as fast.
3. Turn it off: normal speed again, at once.
4. **Jump Boost** 2.0x: jump and compare. Then 3.0x. Then off.
5. **Low Gravity** 50%: floaty. Then **High Gravity** 50%: heavy. Off: normal.
6. Turn on all three together, then off one by one.
7. Open the patcher, press **Get facts**, send me the file (no need to press Scan).
The facts file lines starting with `link:` say what the link found and wrote, whether the game fought back, and the peak speed seen.

