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
1. Menu -> Movement -> **Scan game code**.
2. In the patcher press **Get facts**, send me the file.
3. The file lists classes / fields / methods with names like Move, Jump, Speed, Gravity, Fly, Walk, Sprint, plus any
   class holding a CharacterController or Rigidbody. From that I can see how the game really moves the player.
The scan only reads names. It changes nothing in the game. If the game closes when you press it, tell me.
