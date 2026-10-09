package com.timmyzstuff.patcher

import android.content.Context

/**
 * The "Get facts" button (used with the test payload, "Stage B").
 *
 * The payload runs inside the game and writes a small text file of facts into the game's own
 * folder. This button uses root to find it, copies it to your Download folder, and shows it in
 * the progress log.
 */
object Facts {
    private const val FILE_NAME = "timmyzstuff_facts.txt"

    fun collect(@Suppress("UNUSED_PARAMETER") context: Context, log: (String) -> Unit): ActionResult {
        val user = android.os.Process.myUid() / 100000
        val pkg = PatcherConfig.TARGET_PACKAGE
        val dst = "/storage/emulated/$user/Download/$FILE_NAME"
        val folders = listOf(
            "/storage/emulated/$user/Android/data/$pkg/files",
            "/data/user/$user/$pkg/files"
        )
        val sb = StringBuilder()
        for (d in folders) {
            val f = "$d/$FILE_NAME"
            sb.append("if [ -f '$f' ]; then cp '$f' '$dst' && chmod 666 '$dst'; echo 'FOUND $f'; head -n 150 '$f'; exit 0; fi; ")
        }
        sb.append("echo NOTFOUND")

        val result = RootShell.run(sb.toString(), 30)
        if (result.launchError != null) return ActionResult(false, result.launchError)
        if (result.lines.none { it.startsWith("FOUND ") }) {
            return ActionResult(
                false,
                "No facts file yet. Start the game with the test payload installed, reach the lobby, " +
                    "press the controller buttons for a minute, then try again."
            )
        }
        for (line in result.lines) log(line)
        return ActionResult(true, "Copied to Download/$FILE_NAME (also shown above).")
    }
}
