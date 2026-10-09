package com.timmyzstuff.patcher

import android.content.Context
import android.content.pm.PackageManager
import android.content.pm.SigningInfo
import org.json.JSONObject
import java.io.File
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL
import java.security.MessageDigest

/** What the newest build on GitHub looks like. */
class UpdateInfo(val code: Long, val apkName: String, val apkUrl: String, val shaUrl: String?)

/** The outcome of the Update button. */
class UpdateResult(val success: Boolean, val message: String)

/**
 * The Update button.
 *
 * In plain words:
 *   1. Ask GitHub for the newest build (the "latest" release your build publishes).
 *   2. If it is newer than this app, download it and check the download is not damaged.
 *   3. Check it was signed with the SAME key as this app (Android would refuse it otherwise,
 *      and we never hand an unknown file to root).
 *   4. Use root to install it over this app. The app closes and opens again by itself.
 *
 * Call [run] from a BACKGROUND thread (MainActivity does this) - it waits for the network.
 */
class Updater(private val context: Context, private val log: (String) -> Unit) {

    private val tmpApk = "/data/local/tmp/tz_update.apk"
    private val resultFile = "/data/local/tmp/tz_update.log"

    fun installedCode(): Long =
        context.packageManager.getPackageInfo(context.packageName, 0).longVersionCode

    private fun open(url: String, accept: String): HttpURLConnection {
        val c = URL(url).openConnection() as HttpURLConnection
        c.connectTimeout = 15_000
        c.readTimeout = 60_000
        c.setRequestProperty("User-Agent", "Timmyzstuff-Updater")
        c.setRequestProperty("Accept", accept)
        return c
    }

    private fun getText(url: String): String {
        val c = open(url, "application/vnd.github+json")
        try {
            val code = c.responseCode
            if (code == 404) throw IOException("no build has been published on GitHub yet (404)")
            if (code != 200) throw IOException("GitHub answered with code $code")
            return c.inputStream.bufferedReader().use { it.readText() }
        } finally {
            c.disconnect()
        }
    }

    fun fetchLatest(): UpdateInfo {
        val url = "https://api.github.com/repos/${PatcherConfig.UPDATE_REPO}/releases/tags/${PatcherConfig.UPDATE_TAG}"
        val json = JSONObject(getText(url))
        val assets = json.getJSONArray("assets")
        val apkPattern = Regex("^Timmyzstuff-(\\d+)\\.apk$")

        var apkName: String? = null
        var apkUrl: String? = null
        var code = 0L
        for (i in 0 until assets.length()) {
            val a = assets.getJSONObject(i)
            val name = a.getString("name")
            val m = apkPattern.find(name)
            if (m != null) {
                apkName = name
                apkUrl = a.getString("browser_download_url")
                code = m.groupValues[1].toLong()
            }
        }
        if (apkName == null || apkUrl == null) throw IOException("the GitHub release has no Timmyzstuff-<number>.apk file")

        var shaUrl: String? = null
        for (i in 0 until assets.length()) {
            val a = assets.getJSONObject(i)
            if (a.getString("name") == "$apkName.sha256") shaUrl = a.getString("browser_download_url")
        }
        return UpdateInfo(code, apkName, apkUrl, shaUrl)
    }

    /** Just the newest build number on GitHub (for the automatic check when the app opens). Changes nothing. */
    fun latestBuildCode(): Long = fetchLatest().code

    private fun download(info: UpdateInfo, target: File): String {
        val temp = File(target.path + ".part")
        temp.delete()
        val digest = MessageDigest.getInstance("SHA-256")
        val c = open(info.apkUrl, "application/octet-stream")
        try {
            if (c.responseCode != 200) throw IOException("download failed (code ${c.responseCode})")
            val total = c.contentLengthLong
            var done = 0L
            var nextReport = 25
            c.inputStream.use { input ->
                temp.outputStream().use { output ->
                    val buffer = ByteArray(64 * 1024)
                    while (true) {
                        val n = input.read(buffer)
                        if (n < 0) break
                        output.write(buffer, 0, n)
                        digest.update(buffer, 0, n)
                        done += n
                        if (total > 0) {
                            val percent = (done * 100 / total).toInt()
                            if (percent >= nextReport) {
                                log("Downloaded $percent%")
                                nextReport += 25
                            }
                        }
                    }
                }
            }
        } finally {
            c.disconnect()
        }
        target.delete()
        if (!temp.renameTo(target)) throw IOException("could not save the downloaded file")
        return digest.digest().joinToString("") { "%02x".format(it) }
    }

    private fun expectedHash(info: UpdateInfo): String {
        val shaUrl = info.shaUrl ?: throw IOException("the GitHub release has no .sha256 file")
        val c = open(shaUrl, "application/octet-stream")
        try {
            if (c.responseCode != 200) throw IOException("could not read the .sha256 file (code ${c.responseCode})")
            val text = c.inputStream.bufferedReader().use { it.readText() }
            return text.trim().split(Regex("\\s+")).first().lowercase()
        } finally {
            c.disconnect()
        }
    }

    private fun signers(info: SigningInfo?): Set<String> =
        info?.apkContentsSigners?.map { it.toCharsString() }?.toSet() ?: emptySet()

    /** Returns null when the file is fine, or a plain-English problem. */
    private fun checkApk(file: File, info: UpdateInfo): String? {
        val pm = context.packageManager
        val flags = PackageManager.GET_SIGNING_CERTIFICATES
        val archive = pm.getPackageArchiveInfo(file.absolutePath, flags)
            ?: return "the downloaded file is not a valid app"
        if (archive.packageName != context.packageName) return "the downloaded app is a different app (${archive.packageName})"
        if (archive.longVersionCode != info.code) return "the build number inside the file does not match its name"
        val mine = signers(pm.getPackageInfo(context.packageName, flags).signingInfo)
        val theirs = signers(archive.signingInfo)
        if (mine.isEmpty() || theirs.isEmpty()) return "could not read the signing key"
        if (mine != theirs) {
            return "the new build was signed with a different key, so Android would refuse it. " +
                "Uninstall this app once and install the newest build by hand (see UPDATES.md)."
        }
        return null
    }

    private fun installWithRoot(file: File): UpdateResult {
        val user = android.os.Process.myUid() / 100000     // which Android user (profile) we run in
        val pkg = context.packageName
        // The install runs in the background so it survives this app being replaced.
        val inner = "pm install -r --user $user $tmpApk > $resultFile 2>&1; rm -f $tmpApk; " +
            "am start --user $user -n $pkg/.MainActivity > /dev/null 2>&1"
        val command = "rm -f $resultFile; cp ${RootShell.quote(file.absolutePath)} $tmpApk && chmod 644 $tmpApk && " +
            "( sh -c '$inner' < /dev/null > /dev/null 2>&1 & ) ; echo STARTED"
        val start = RootShell.run(command, 60)
        if (start.launchError != null) return UpdateResult(false, start.launchError)
        if (start.lines.none { it.contains("STARTED") }) {
            return UpdateResult(false, "could not start the install as root: ${start.lines.takeLast(3).joinToString(" | ")}")
        }

        log("Installing... the app will close and open again by itself.")
        for (i in 1..45) {
            Thread.sleep(2000)
            val r = RootShell.run("cat $resultFile 2>/dev/null", 20)
            val text = r.lines.joinToString(" ")
            if (text.contains("Success")) return UpdateResult(true, "Installed. If this screen is still here, open the app again.")
            if (text.contains("Failure")) return UpdateResult(false, "Android refused the install: $text")
        }
        return UpdateResult(false, "the install did not report back after 90 seconds")
    }

    fun run(): UpdateResult {
        try {
            log("Asking GitHub for the newest build...")
            val info = fetchLatest()
            val mine = installedCode()
            if (info.code <= mine) return UpdateResult(true, "Already up to date (this is build $mine).")

            log("Found build ${info.code} (you have build $mine). Downloading...")
            val file = File(context.filesDir, "update.apk")
            val gotHash = download(info, file)
            val wantHash = expectedHash(info)
            if (gotHash != wantHash) {
                file.delete()
                return UpdateResult(false, "the download is damaged (hash check failed). Nothing was changed.")
            }
            log("Download checked.")

            val problem = checkApk(file, info)
            if (problem != null) {
                file.delete()
                return UpdateResult(false, "Not installed: $problem")
            }
            log("The new build has the same signing key. Installing...")
            return installWithRoot(file)
        } catch (e: IOException) {
            return UpdateResult(false, "Update failed: ${e.message}")
        } catch (e: org.json.JSONException) {
            return UpdateResult(false, "Update failed: could not read GitHub's answer (${e.message})")
        }
    }
}
