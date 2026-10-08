package com.timmyzstuff.patcher

import android.content.Context
import java.io.File
import java.io.IOException
import java.security.MessageDigest

/** Everything we know about the payload library bundled inside this app. */
class PayloadInfo(
    /** Is a file with the right name inside the app? */
    val present: Boolean,
    /** Is it present AND does it look like a 64-bit ARM library? */
    val valid: Boolean,
    /** A copy of the payload in the app's private folder (what root will copy from). */
    val stagedFile: File?,
    val sha256: String?,
    val sizeBytes: Long,
    /** Plain-English explanation if something is wrong. */
    val problem: String?
)

/**
 * Looks at the payload BEFORE we touch the game.
 * We can prove it "looks like an arm64 Android library".
 * We can NOT prove it is compatible with the game - only testing on the headset shows that.
 */
object PayloadChecker {

    fun inspect(context: Context): PayloadInfo {
        val names = try {
            context.assets.list(PatcherConfig.PAYLOAD_DIR)?.toList() ?: emptyList()
        } catch (e: IOException) {
            emptyList()
        }
        if (PatcherConfig.PAYLOAD_FILE !in names) {
            return PayloadInfo(
                false, false, null, null, 0,
                "No payload found. Put your library at " +
                    "app/src/main/assets/${PatcherConfig.PAYLOAD_DIR}/${PatcherConfig.PAYLOAD_FILE} and rebuild the app."
            )
        }

        val staged = File(context.filesDir, "payload_staged.so")
        val temp = File(context.filesDir, "payload_staged.so.tmp")
        return try {
            val digest = MessageDigest.getInstance("SHA-256")
            val header = ByteArray(20)
            var headerFilled = 0
            var total = 0L

            context.assets.open("${PatcherConfig.PAYLOAD_DIR}/${PatcherConfig.PAYLOAD_FILE}").use { input ->
                temp.outputStream().use { output ->
                    val buffer = ByteArray(64 * 1024)
                    while (true) {
                        val n = input.read(buffer)
                        if (n < 0) break
                        if (headerFilled < header.size) {
                            val take = minOf(n, header.size - headerFilled)
                            System.arraycopy(buffer, 0, header, headerFilled, take)
                            headerFilled += take
                        }
                        digest.update(buffer, 0, n)
                        output.write(buffer, 0, n)
                        total += n
                    }
                }
            }

            val problem = describeHeaderProblem(header, headerFilled, total)
            if (problem != null) {
                temp.delete()
                return PayloadInfo(true, false, null, null, total, problem)
            }

            staged.delete()
            if (!temp.renameTo(staged)) throw IOException("could not save the staged payload")
            staged.setReadable(true, false)

            val hex = digest.digest().joinToString("") { "%02x".format(it) }
            PayloadInfo(true, true, staged, hex, total, null)
        } catch (e: IOException) {
            temp.delete()
            PayloadInfo(true, false, null, null, 0, "Could not read the payload: ${e.message}")
        }
    }

    /** Returns null if the header looks right, otherwise a plain-English problem. */
    private fun describeHeaderProblem(h: ByteArray, filled: Int, totalSize: Long): String? {
        if (totalSize < 1024 || filled < 20) return "The payload file is too small to be a real library ($totalSize bytes)."
        val isElf = h[0] == 0x7F.toByte() && h[1] == 'E'.code.toByte() && h[2] == 'L'.code.toByte() && h[3] == 'F'.code.toByte()
        if (!isElf) return "The payload is not a native library (it does not start with the ELF marker)."
        if (h[4] != 2.toByte()) return "The payload is not a 64-bit library."
        if (h[5] != 1.toByte()) return "The payload is not little-endian (Quest needs little-endian)."
        if (h[16] != 3.toByte()) return "The payload is not a shared library (.so)."
        val machine = (h[18].toInt() and 0xFF) or ((h[19].toInt() and 0xFF) shl 8)
        if (machine != 0xB7) return "The payload is not built for arm64 (AArch64). Quest needs arm64."
        return null
    }
}
