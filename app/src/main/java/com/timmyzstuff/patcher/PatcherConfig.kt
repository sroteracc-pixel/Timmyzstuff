package com.timmyzstuff.patcher

/**
 * All the settings you are most likely to change live here, in one place.
 * (If you change TARGET_PACKAGE, also change it in AndroidManifest.xml under <queries>.)
 */
object PatcherConfig {
    /** The Android package name of the game we patch. */
    const val TARGET_PACKAGE = "com.IRLStudios.GymClass"

    /** The folder inside app/src/main/assets/ that holds your payload. */
    const val PAYLOAD_DIR = "payload"

    /** The payload file name. Put your compatible library here: assets/payload/libmain_payload.so */
    const val PAYLOAD_FILE = "libmain_payload.so"

    /** The root helper script (app/src/main/assets/patcher.sh). */
    const val SCRIPT_ASSET = "patcher.sh"

    /** The CPU type the game's library must be built for. Quest headsets are arm64. */
    const val REQUIRED_ABI = "arm64-v8a"

    /** GitHub repository the Update button reads from (owner/name). It must be public. */
    const val UPDATE_REPO = "sroteracc-pixel/Timmyzstuff"

    /** The GitHub release the build publishes every time (the workflow keeps it named "latest"). */
    const val UPDATE_TAG = "latest"
}
