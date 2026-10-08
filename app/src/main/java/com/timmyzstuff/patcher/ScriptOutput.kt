package com.timmyzstuff.patcher

/**
 * The root script talks to the app with simple lines:
 *   STEP some progress text
 *   key=value
 *   OK            (success, last line)
 *   ERR message   (failure, last line)
 * This class turns those lines into something easy to use.
 */
class ScriptOutput(
    val values: Map<String, String>,
    val ok: Boolean,
    val error: String?
) {
    companion object {
        fun parse(lines: List<String>): ScriptOutput {
            val values = HashMap<String, String>()
            var ok = false
            var error: String? = null
            for (raw in lines) {
                val line = raw.trim()
                when {
                    line == "OK" -> ok = true
                    line.startsWith("ERR ") -> error = line.removePrefix("ERR ")
                    line.startsWith("STEP ") -> { /* progress is shown live, not parsed */ }
                    Regex("^[a-z_]+=").containsMatchIn(line) -> {
                        val i = line.indexOf('=')
                        values[line.substring(0, i)] = line.substring(i + 1)
                    }
                }
            }
            return ScriptOutput(values, ok && error == null, error)
        }
    }
}
