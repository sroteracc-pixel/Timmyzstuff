package com.timmyzstuff.patcher

/** The four things the "update line" at the top of the app can say. */
enum class UpdateState { CHECKING, AVAILABLE, UP_TO_DATE, FAILED }

/** The outcome of one check, with the exact words to show. */
class UpdateCheckResult(val state: UpdateState, val latestBuild: Long?) {
    val label: String
        get() = when (state) {
            UpdateState.CHECKING -> "Checking for updates\u2026"
            UpdateState.AVAILABLE -> "Update available \u2014 build $latestBuild"
            UpdateState.UP_TO_DATE -> "Up to date"
            UpdateState.FAILED -> "Couldn't check for updates"
        }
}

/**
 * Decides what the update line says. No Android code in here, so it is tested on a PC.
 *
 * It only LOOKS at GitHub (via [Updater.latestBuildCode]). It never downloads or installs anything:
 * installing stays behind the Update button.
 */
object UpdateCheck {

    /** Compares build numbers. `latest == null` (or nonsense) means the check failed. */
    fun decide(installed: Long, latest: Long?): UpdateCheckResult = when {
        latest == null || latest <= 0L -> UpdateCheckResult(UpdateState.FAILED, null)
        latest > installed -> UpdateCheckResult(UpdateState.AVAILABLE, latest)
        else -> UpdateCheckResult(UpdateState.UP_TO_DATE, latest)       // same build, or this one is even newer
    }

    /**
     * Runs a check. [fetchLatestCode] asks GitHub and may throw (no internet, GitHub down, 404,
     * unreadable answer...). Any problem becomes "Couldn't check for updates" - never a crash.
     * Call from a BACKGROUND thread.
     */
    fun run(installed: Long, fetchLatestCode: () -> Long): UpdateCheckResult =
        try {
            decide(installed, fetchLatestCode())
        } catch (e: Exception) {
            UpdateCheckResult(UpdateState.FAILED, null)
        }
}
