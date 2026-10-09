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
1. Menu -> Movement -> **Scan game code** (stage D5 takes up to about a minute).
2. In the patcher press **Get facts**, send me the file.
What the file now contains (read-only, nothing in the game is changed):
- an INDEX line for every class of the game's own code whose name looks like movement / player / character / parameters;
- the full list of fields and methods of the important classes (MobilePlayerLocomotion, MobileVerticalMotion, ...),
  with where each method's code starts inside libil2cpp.so (`rva=`);
- the LIVE values: the scan looks through the game's memory for the running copy of those classes and writes down what
  their fields hold right now (speed, jump numbers, gravity ...). That tells me which field is the real speed/jump.

What the first real scan (stage D4) showed: the game's own walking/jumping class is `MobilePlayerLocomotion`
(it has SetJumpHeight, SetMaxJumpSpeed, SetJumpAcceleration ... and a vertical-motion helper). Its method locations came
out as 0 (a bug in the first version; fixed in D5, unverified on the headset until you send the next file).

If the game closes while scanning, tell me.
