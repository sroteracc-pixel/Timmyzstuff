# Building Timmyzstuff (beginner steps)

## One-time setup
1. Install **Android Studio** (free, from developer.android.com/studio). Accept the default options.
2. Open Android Studio -> **Open** -> choose this `Timmyzstuff` folder.
3. Wait for "Gradle sync" to finish (bottom bar). The first time it downloads a lot.
   If it asks to install **Android SDK 34**, click Install/Accept.
   If it offers to upgrade Gradle/plugin versions, accepting is fine.

## Build the app
- Menu: **Build -> Build Bundle(s) / APK(s) -> Build APK(s)**.
- The file appears at `app/build/outputs/apk/debug/app-debug.apk` (Studio shows a "locate" link).

## Put it on the Quest 3S
1. In the Meta Horizon phone app, enable **Developer Mode** for the headset.
2. Connect the headset by USB and allow debugging inside the headset.
3. Install: `adb install -r app-debug.apk` (or use SideQuest's "install APK").
4. In the headset: App Library -> filter **Unknown Sources** -> **Timmyzstuff**.
   (That is where sideloaded 2D apps normally appear - I could not test it.)

## Using it
- **Status** - runs the checks (root, architecture, payload, game, backup). The first run triggers your root manager's prompt: tap Allow.
- **Install** - backs up the original, copies the payload, verifies it. Only enabled when every check passes.
- **Restore** - puts the original library back.

## Add your payload later
Copy it to `app/src/main/assets/payload/libmain_payload.so`, rebuild, reinstall the APK.

## Test the safety logic on your computer (no Quest needed)
    bash tests/test_patcher.sh
This runs the root script against fake files. It proves the backup / verify / recovery logic,
not how a real headset behaves.

## Command-line build (optional)
The Gradle "wrapper" jar is not included. Either use Android Studio, or run
`gradle wrapper --gradle-version 8.9` once (needs Gradle installed), then `./gradlew assembleDebug`.

## No computer? Build in the cloud (untested)
1. Make a free GitHub account and a new repository.
2. Upload the CONTENTS of this folder (including the hidden `.github` folder).
3. Open the repository -> **Actions** -> **Build APK** -> **Run workflow**.
4. When it finishes (a few minutes), open the run and download **Timmyzstuff-debug-apk**. Unzip it to get `app-debug.apk`.
If the run turns red, send me the error text from the failed step.
