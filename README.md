# Timmyzstuff Patcher

A small Android app for a rooted Meta Quest that installs, checks, and restores a replacement
`libmain.so` for one game (`com.IRLStudios.GymClass` by default - change it in `PatcherConfig.kt`).
It contains **no gameplay code and no payload**: you add your own library later (see PAYLOAD.md).

## Files, in plain English
| File | What it does |
|---|---|
| `app/src/main/assets/patcher.sh` | The part that runs as root: finds the game, backs up, copies, verifies, recovers |
| `.../PatcherConfig.kt` | Settings you may edit: game package, payload name |
| `.../MainActivity.kt` | The screen: three buttons, a checklist, a progress log |
| `.../Patcher.kt` | Runs the checks and starts the root script |
| `.../RootShell.kt` | Starts `su` and reads its output |
| `.../ScriptOutput.kt` | Understands the script's `STEP / key=value / OK / ERR` lines |
| `.../PayloadChecker.kt` | Checks the payload file looks like an arm64 library |
| `app/src/main/res/` | Layout, colours (change the Timmyzstuff look here), icon |
| `app/src/main/assets/payload/` | Where your payload goes |
| `tests/test_patcher.sh` | Computer-side tests of the root script |
| `FINDINGS.md` / `PAYLOAD.md` / `BUILD.md` | Reference analysis / payload rules / how to build |

## Safety rules
- A valid `libmain_orig.so` backup is never overwritten.
- New files are copied under a temporary name, hash-checked, then renamed into place.
- If an install fails, the original is put back and the app tells you.
- If the payload is missing or not an arm64 library, Install stays disabled and says why.
