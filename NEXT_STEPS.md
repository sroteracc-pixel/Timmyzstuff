# Next steps (small steps, Windows laptop)

## Where things stand
- DONE and tested on your Quest: the patcher app opens, gets root, sees the game (1.8.046), reads "original".
- NEW, written but not yet built or run on a Quest: `payload/` (a pass-through library, "Stage A").
- NOT connected to the game yet: the menu in `menu/` (see "Why the menu can't show up yet" below).

## Step 1 - Update your GitHub copy
1. In your repository click **Add file -> Upload files**. Drag in the new `payload` folder and the replaced `menu` folder
   (and `NEXT_STEPS.md`, `TESTPLAN.md`). Click **Commit changes**.
2. Open `.github/workflows/build-apk.yml` on GitHub, click the pencil (Edit), delete everything, paste the new
   file from this zip, and Commit.
3. **Actions -> Build APK -> Run workflow**. Wait for the green check.
   - Download **Timmyzstuff-debug-apk** (this time the payload is already inside it).
   - If the payload step shows a red "x" but the run is still green, the APK has no payload. Send me that step's error text.

## Step 2 - Get the game's original libmain.so to me (so I can check it, not guess)
Why: I need to see which functions the REAL libmain.so exports. The pass-through payload only forwards `JNI_OnLoad`;
if the real one exports more, the game could fail to start.
1. Install Android "platform-tools" on Windows (search "adb platform-tools download", unzip it, open a Command Prompt in that folder).
2. Quest plugged in, developer mode on, tap Allow on the USB prompt. Run:
       adb devices
   (your Quest should be listed as "device").
3. Run:
       adb shell pm path com.IRLStudios.GymClass
   It prints lines like `package:/data/app/.../base.apk` and `split_config.arm64_v8a.apk`.
4. For EACH line, run `adb pull <that path>`  (copy what comes after `package:`).
5. Right-click each pulled .apk -> open with 7-Zip (a free zip program) and look inside `lib/arm64-v8a/`.
   If you see `libmain.so`, extract it and send it to me. If you do NOT see it there, tell me which APK files you have
   and what's inside their `lib` folders - the game's extracted copy may be elsewhere (the patcher found one on the device).
6. At the same time, from the same files, send `libil2cpp.so` and `global-metadata.dat`
   (`assets/bin/Data/Managed/Metadata/global-metadata.dat` in the base APK) - or, better, run Il2CppDumper on them
   and send `dump.cs` (see the earlier steps). Needed for Step 3 below, not for the Stage A test.

## Step 3 (later) - Why the menu can't show up yet
A menu needs three connections to the game. None exists yet, and I won't guess them:
1. **Drawing:** a way to put ImGui's output in front of your eyes in VR. This depends on how the game renders
   (needs the dump + libil2cpp.so, and a hooking approach that we choose together).
2. **Controller input:** reading both triggers, A and B. Same: it comes from the game's own input code or the VR runtime.
3. **Blocking game clicks** while the menu is open.
These are the 3 placeholders in `menu/src/integration_stubs.cpp` plus the pointer position (`pointerX/Y`).

## What the in-game logcat message should look like (Stage A)
With the Quest plugged in: `adb logcat -s Timmyzstuff`  then start the game. Expected lines:
    payload loaded (stage A: pass-through only)
    I am /data/app/.../libmain.so -> loading original /data/app/.../libmain_orig.so
    original JNI_OnLoad returned 0x...
If you instead see "ERROR: could not load the original library", press Restore in the patcher and send me the line.
