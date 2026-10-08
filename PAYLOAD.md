# The payload (kept separate on purpose)

The **patcher app** and the **payload library** are two different programs.

| | Patcher app (this project) | Payload library |
|---|---|---|
| Written in | Kotlin + a shell script | C/C++ (usually) |
| Built with | Android Studio / Gradle | Android NDK (CMake or ndk-build) |
| Output | `app-debug.apk` (installed like any app) | a `.so` file |
| Job | Copy files around safely, as root | Run *inside the game* once installed |

The app only **carries** the payload as a file and copies it into the game's folder.
Building the app never compiles the payload.

## Where to put it
`app/src/main/assets/payload/libmain_payload.so`, then rebuild the app.
No file there = the app says "No payload found" and Install stays disabled.

## What the app checks (and what it cannot)
It CAN check: the file exists, is not tiny, and is a 64-bit little-endian ARM shared library.
It CANNOT check: that the library is compatible with the game, or that it works.
Only running the game on your Quest 3S can show that.

## What a compatible payload probably needs (from how the reference worked - not a guarantee)
1. Built for `arm64-v8a`.
2. It takes the place of the game's `libmain.so`, so it must provide whatever the game expects from that file.
   In the reference that was a `JNI_OnLoad` function.
3. Because the original `libmain.so` is renamed to `libmain_orig.so` in the same folder, the payload must
   load that file and hand control to it, or the game will not start normally.
