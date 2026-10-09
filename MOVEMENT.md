# Movement page (stage D4)

## What is real, what is not
| Part | State |
|---|---|
| Movement page in the menu (4 switches, 4 sliders with exact numbers, saved slider values) | Built and tested on a PC |
| The rules (off = original value back, no stacking, one gravity mode, Fly wins) | Built and tested on a PC (37 checks) |
| "Scan game code" button (reads the game's class/field/method names into the facts file) | Built; tested ONLY against a pretend game on a PC |
| **Changing the game's real speed / jump / gravity** | **NOT built. The switches do nothing in the game yet.** |
| **Fly** | **Does not exist yet.** The rules already have a "Fly is on" input so Fly will win over the boosts once it exists. |

Why nothing happens in the game: no game code (libil2cpp.so, global-metadata.dat) is available to read, so there is
no verified place to hook. I do not guess game addresses. The menu says "Game link: not connected" until a real
connection exists.

## The rules (movement.cpp)
- Everything starts OFF. Switches are never saved; slider values are saved.
- A value is always `original x factor`, never `current x factor`, so nothing can stack.
- Turning a switch off, or moving to "no change", puts the game's original value back exactly.
- Low Gravity multiplies gravity by `1 - pct/100` (90% -> 10% of normal). High Gravity by `1 + pct/100` (90% -> 190%).
  Only one gravity mode is on at a time (one setting, 0 / low / high).
- Jump Boost means jump HEIGHT. Height goes with the square of the launch speed, so the launch speed is scaled by
  the square root of the factor (2.0x height -> about 1.41x launch speed under the same gravity).
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
