# What I found in the reference APK (AstryxPatcher.zip)

Everything under **Verified** was read directly from the file you uploaded.
Everything under **Not verified** is an assumption or needs your Quest 3S.
The reference is used for study only; none of its code or assets is copied into this project.

## Verified

**The APK (the patcher app)**
- Package `com.astryx.gympatcher`, one activity `MainActivity`, built with Android Gradle Plugin 9.0.0 and Kotlin, min API 30.
- Contains `assets/patch.sh` (2,663 bytes) and `assets/gymmod.so` (819,392 bytes).
- Its manifest declares it may look up `com.IRLStudios.GymClass`.
- Its text mentions `su`, `-c`, `sh '`, `ProcessBuilder`, `getFilesDir`, and the messages
  "Root (su) not found - this patcher needs a rooted Quest" and "Needs root (Magisk)".
  So it copies the script/payload to its own folder and runs the script through `su`.

**patch.sh** (read in full)
- Target package `com.IRLStudios.GymClass`.
- Finds the install with `pm path` (tries default user, `--user 0`, `--user 10`, `--user current`).
- Library folder = (folder of the base APK)`/lib/arm64`.
- Commands: `status`, `install <mod.so>`, `restore`. Output is `KEY=VALUE` lines then `OK` or `ERR <reason>`.
- Install: refuses if the payload lacks the text marker `gymmod JNI_OnLoad`; runs `am force-stop`;
  if `libmain_orig.so` is missing it copies `libmain.so` to `libmain_orig.so` (and refuses if `libmain.so`
  already contains the marker); copies the payload straight over `libmain.so`;
  sets mode 755, owner `system:system`, SELinux label `u:object_r:apk_data_file:s0`; compares MD5 of the copy.
- Restore: copies `libmain_orig.so` back over `libmain.so`, checks MD5, deletes the backup.
- If the copy fails halfway, the script reports an error but does **not** put the original back.

**gymmod.so** (inspected with readelf/nm/strings; not disassembled)
- 64-bit ARM (AArch64) shared library, stripped. Its internal name (SONAME) is `libcz_imgui.so`.
- Exports exactly one function: `JNI_OnLoad`.
- Imports `dlopen`, `dlsym`, `dlerror`, `mprotect`.
- Contains these strings: `%s%s_orig.so`, `chain-loaded stock libmain from %s`,
  `stock JNI_OnLoad returned`, `stock libmain missing JNI_OnLoad`, `FAILED to chain-load stock libmain (%s)`.

## What that tells us about "preserving the original startup" (a reasoned reading, not a proof)
The strings and imports are consistent with this design: the game loads `libmain.so` (now the mod),
the mod's `JNI_OnLoad` builds the path of `libmain_orig.so`, loads it with `dlopen`, finds the original
`JNI_OnLoad` with `dlsym`, and calls it. That is why the patcher must keep the original as `libmain_orig.so`.

## Not verified (assumptions)
- That chain-loading really works exactly as described above (needs disassembly or a run).
- That the mod works with your current game version.
- That your Quest 3S is rooted, grants `su` to a new app, and lets root write into the game's `lib/arm64` folder.
- That a game update wipes that folder (the reference's comments say so; I could not confirm it).
- That `chcon`/`chown` behave on your device (the labels come from the reference script).
- Exact `su` call details inside the reference app (only strings were read; the app was not decompiled).
- Anything about the original mod's source. I did not recover it and have not tried to.

## Where Timmyzstuff deliberately differs
| Reference | Timmyzstuff |
|---|---|
| Tells "modded" from "stock" by searching for a text marker | Uses SHA-256 hashes plus a small state file, so it works with any payload |
| Copies straight over `libmain.so` | Copies to a temporary name, checks the hash, then renames into place |
| No automatic recovery if a copy fails | Puts the original back from the backup and says so |
| MD5 | SHA-256 |
| No payload or architecture check | Checks the payload is an arm64 shared library and the device is arm64 |
