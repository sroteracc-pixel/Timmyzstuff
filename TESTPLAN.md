# Test plan for when you get home

Do these in order. Stop at the first thing that goes wrong and send me what you see.

## Stage 1 - Build the app on your computer
1. Follow BUILD.md (Android Studio -> open the folder -> wait for sync -> Build APK).
2. If Gradle/sync shows a red error, **screenshot or copy the error text**. This is the most likely first problem
   (I could not compile the Kotlin here).

## Stage 2 - Install and open it on the Quest 3S
1. Install the APK (adb install or SideQuest).
2. Open it from App Library -> Unknown Sources -> Timmyzstuff.
3. Your root manager (e.g. Magisk) should pop up a request. Tap **Allow**.

## Stage 3 - Read the Status screen (this is the real test)
Expected with NO payload added yet:
- Root: [OK] granted  (or [!!] with a reason)
- Architecture: [OK] arm64-v8a
- Payload: [!!] No payload found...   <- this is CORRECT for now
- Game: [OK] installed, version ...
- Game library / Backup lines (only if root worked)
- Install button greyed out (correct while there is no payload)

Send me a screenshot of the Status screen and the Progress log.
Things I can't know until you try: does the app show in Unknown Sources, does root work for it,
and does the script find `lib/arm64` inside the game folder.

## Stage 4 - Only after Stage 3 looks right
- Add a payload at `app/src/main/assets/payload/libmain_payload.so`, rebuild, reinstall.
- Press Install. The log should show: backup saved and verified -> copying -> verified.
- Press Restore to prove you can always get back to the original. **Do this once before trusting anything.**
- If anything fails, the original is put back automatically; the message tells you what happened.

## Stage 5 - The menu (later)
Needs the real payload plus the 3 placeholders in `menu/src/integration_stubs.cpp`
(read controllers, block game input, a save folder). Not part of today's test.

## What to send me
1. Status screenshot  2. Progress log  3. Any red build error  4. Whether root prompt appeared


---
# UPDATE: test checklist for the first payload (Stage A)
Do these in order. Stop at the first problem and send me what you see.

1. **Game starts normally BEFORE patching.** Open Gym Class, let it reach the main menu, close it.
2. **Status in the patcher** should now say `Payload: [OK] ...` (the payload is inside the new APK). Install becomes available.
3. **Install.** Read the Progress log. Good = "Backup saved and verified", then "Copying the payload", then SUCCESS.
   Status should then say `Game library: patched` and `Backup: original saved`.
4. **Start the game.** It should start and behave exactly like before. (There is NO menu yet - nothing visible changes.)
   Optional proof: `adb logcat -s Timmyzstuff` shows the three lines listed in NEXT_STEPS.md.
5. **Restore.** Press Restore. Status should say `original (not patched)` again.
6. **Start the game again.** It should start normally.
If step 4 fails (crash/black screen): close the game, open the patcher, press **Restore**, confirm step 6 works, and send me
the logcat lines. Restore does not need the game to run.
