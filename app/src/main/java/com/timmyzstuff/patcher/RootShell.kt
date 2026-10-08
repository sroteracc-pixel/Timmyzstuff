package com.timmyzstuff.patcher

import java.io.IOException
import java.util.Collections
import java.util.concurrent.TimeUnit

/** What came back from running one command as root. */
class ShellResult(
    val exitCode: Int,
    val lines: List<String>,
    /** Not null if we could not even start `su` (usually: the device is not rooted). */
    val launchError: String?,
    val timedOut: Boolean
)

/**
 * Runs a command as root by starting the `su` program (provided by Magisk or similar).
 * ALWAYS call this from a background thread - it waits for the command to finish.
 */
object RootShell {

    fun run(command: String, timeoutSeconds: Long, onLine: (String) -> Unit = {}): ShellResult {
        val process = try {
            ProcessBuilder("su", "-c", command)
                .redirectErrorStream(true) // merge error text into normal output
                .start()
        } catch (e: IOException) {
            return ShellResult(-1, emptyList(), "Root (su) was not found: ${e.message}", false)
        }

        val lines = Collections.synchronizedList(ArrayList<String>())

        // Read output line by line on a helper thread so we can show progress live.
        val reader = Thread {
            try {
                process.inputStream.bufferedReader().forEachLine { line ->
                    lines.add(line)
                    onLine(line)
                }
            } catch (ignored: IOException) {
                // the process ended; nothing more to read
            }
        }
        reader.start()

        val finished = process.waitFor(timeoutSeconds, TimeUnit.SECONDS)
        if (!finished) process.destroyForcibly()
        reader.join(2000)

        val code = if (finished) process.exitValue() else -1
        return ShellResult(code, ArrayList(lines), null, !finished)
    }

    /** Wraps text in single quotes so the shell treats it as one safe value. */
    fun quote(text: String): String = "'" + text.replace("'", "'\\''") + "'"
}
