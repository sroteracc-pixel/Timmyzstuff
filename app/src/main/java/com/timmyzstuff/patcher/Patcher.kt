package com.timmyzstuff.patcher

import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import java.io.File
import java.io.IOException

/** What the Status button shows, plus which buttons make sense right now. */
class StatusReport(
    val lines: List<String>,
    val canInstall: Boolean,
    val canRestore: Boolean
)

/** The outcome of Install or Restore. */
class ActionResult(val success: Boolean, val message: String)

/**
 * The "brain" of the app. It runs the checks and starts the root script.
 * Every function here must be called from a BACKGROUND thread (MainActivity does this).
 *
 * @param log called with each progress message so the screen can show it live.
 */
class Patcher(private val context: Context, private val log: (String) -> Unit) {

    private val pkg = PatcherConfig.TARGET_PACKAGE

    // ------------------------------------------------------------------
    // Individual checks
    // ------------------------------------------------------------------

    /** Is the phone/headset rooted AND did the user allow this app? */
    private fun checkRoot(): Pair<Boolean, String> {
        val r = RootShell.run("id", 120L) // 120s: the first run waits for you to tap "Allow"
        return when {
            r.launchError != null -> false to "su not found - this device does not appear to be rooted"
            r.timedOut -> false to "root request timed out - tap Allow on the root prompt and try again"
            r.lines.any { it.contains("uid=0") } -> true to "granted"
            else -> false to "root was denied (check your root manager's app list)"
        }
    }

    /** Is this device arm64? (Checked from Android itself, no root needed.) */
    private fun checkArch(): Pair<Boolean, String> {
        val abis = Build.SUPPORTED_ABIS.toList()
        return if (PatcherConfig.REQUIRED_ABI in abis) true to PatcherConfig.REQUIRED_ABI
        else false to "needs ${PatcherConfig.REQUIRED_ABI}, this device reports ${abis.joinToString()}"
    }

    /** Is the game installed? Answered by Android itself, so it works even without root. */
    private fun gameVersionWithoutRoot(): String? = try {
        context.packageManager.getPackageInfo(pkg, 0).versionName ?: "unknown version"
    } catch (e: PackageManager.NameNotFoundException) {
        null
    }

    /** Copies patcher.sh out of the app into a place root can run it from. */
    private fun stageScript(): File {
        val out = File(context.filesDir, PatcherConfig.SCRIPT_ASSET)
        context.assets.open(PatcherConfig.SCRIPT_ASSET).use { input ->
            out.outputStream().use { input.copyTo(it) }
        }
        out.setReadable(true, false)
        return out
    }

    /** Runs patcher.sh as root with one of: status, install, restore. */
    private fun runScript(command: String, payload: PayloadInfo?): ScriptOutput {
        val script = try {
            stageScript()
        } catch (e: IOException) {
            return ScriptOutput(emptyMap(), false, "Could not prepare the root helper: ${e.message}")
        }
        val path = payload?.stagedFile?.absolutePath ?: "-"
        val sha = payload?.sha256 ?: "-"
        val cmd = "sh ${RootShell.quote(script.absolutePath)} ${RootShell.quote(pkg)} $command " +
            "${RootShell.quote(path)} ${RootShell.quote(sha)}"

        val result = RootShell.run(cmd, 120L) { line ->
            if (line.startsWith("STEP ")) log(line.removePrefix("STEP "))
        }
        return when {
            result.launchError != null -> ScriptOutput(emptyMap(), false, result.launchError)
            result.timedOut -> ScriptOutput(emptyMap(), false, "The root helper timed out and was stopped.")
            else -> {
                val parsed = ScriptOutput.parse(result.lines)
                if (parsed.ok || parsed.error != null) parsed
                else ScriptOutput(
                    parsed.values, false,
                    "The root helper ended without a result (exit code ${result.exitCode}). " +
                        "Last output: ${result.lines.takeLast(3).joinToString(" | ")}"
                )
            }
        }
    }

    // ------------------------------------------------------------------
    // The three buttons
    // ------------------------------------------------------------------

    fun status(): StatusReport {
        val lines = ArrayList<String>()
        fun row(label: String, ok: Boolean?, text: String) {
            val mark = when (ok) { true -> "[OK]"; false -> "[!!]"; null -> "[..]" }
            lines.add("%-13s %s %s".format(label, mark, text))
        }

        log("Checking root access...")
        val (rootOk, rootMsg) = checkRoot()
        row("Root", rootOk, rootMsg)

        val (archOk, archMsg) = checkArch()
        row("Architecture", archOk, archMsg)

        log("Checking the payload...")
        val payload = PayloadChecker.inspect(context)
        when {
            payload.valid -> row("Payload", true, "${payload.sizeBytes / 1024} KB, id ${payload.sha256!!.take(10)}")
            else -> row("Payload", false, payload.problem ?: "unknown problem")
        }

        val versionNoRoot = gameVersionWithoutRoot()
        row("Game", versionNoRoot != null, if (versionNoRoot != null) "installed, version $versionNoRoot" else "$pkg is not installed")

        var canRestore = false
        var gameFolderOk = false
        if (rootOk) {
            log("Looking at the game's files...")
            val out = runScript("status", if (payload.valid) payload else null)
            if (out.error != null) {
                row("Game files", false, out.error)
            } else if (out.values["game"] == "missing") {
                row("Game files", false, "game not found by the root helper")
            } else if (out.values["libdir"] == "missing") {
                row("Game files", false, "libmain.so not found in the game's arm64 folder")
            } else {
                gameFolderOk = true
                val libmain = out.values["libmain"]
                val backup = out.values["backup"]
                row("Game library", libmain != "unknown", when (libmain) {
                    "patched" -> if (out.values["current"] == "yes") "patched (matches the bundled payload)" else "patched (older or different payload)"
                    "stock" -> "original (not patched)"
                    else -> "unrecognised - not original, not ours"
                })
                row("Backup", backup != "invalid", when (backup) {
                    "valid" -> "original saved (libmain_orig.so)"
                    "none" -> "none yet (created on first install)"
                    else -> "DAMAGED - reinstall the game"
                })
                canRestore = backup == "valid" && libmain != "unknown"
            }
        } else {
            row("Game files", null, "skipped (needs root)")
        }

        val canInstall = rootOk && archOk && payload.valid && gameFolderOk
        return StatusReport(lines, canInstall, canRestore)
    }

    fun install(): ActionResult {
        log("Starting install checks...")
        val (rootOk, rootMsg) = checkRoot()
        if (!rootOk) return ActionResult(false, "Cannot install: $rootMsg.")
        val (archOk, archMsg) = checkArch()
        if (!archOk) return ActionResult(false, "Cannot install: $archMsg.")

        val payload = PayloadChecker.inspect(context)
        if (!payload.valid) return ActionResult(false, "Cannot install: ${payload.problem}")

        if (gameVersionWithoutRoot() == null) {
            return ActionResult(false, "Cannot install: the game ($pkg) is not installed.")
        }

        val out = runScript("install", payload)
        return if (out.ok) ActionResult(true, "Installed and verified. Start the game to test it.")
        else ActionResult(false, out.error ?: "Install failed for an unknown reason.")
    }

    fun restore(): ActionResult {
        log("Starting restore checks...")
        val (rootOk, rootMsg) = checkRoot()
        if (!rootOk) return ActionResult(false, "Cannot restore: $rootMsg.")

        val payload = PayloadChecker.inspect(context)
        val out = runScript("restore", if (payload.valid) payload else null)
        return if (out.ok) ActionResult(true, "Original game library restored and verified.")
        else ActionResult(false, out.error ?: "Restore failed for an unknown reason.")
    }
}
