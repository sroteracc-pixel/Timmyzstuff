package com.timmyzstuff.patcher

import android.app.Activity
import android.os.Bundle
import android.widget.Button
import android.widget.ProgressBar
import android.widget.ScrollView
import android.widget.TextView
import java.util.concurrent.Executors

/**
 * The one and only screen.
 *
 * Golden rule of Android: never do slow work (like asking for root) on the "main"
 * thread, or the screen freezes. So we hand each job to `worker`, and use
 * runOnUiThread { ... } to update the screen when we have something to show.
 */
class MainActivity : Activity() {

    private val worker = Executors.newSingleThreadExecutor()
    private val checker = Executors.newSingleThreadExecutor()     // the update check has its own thread so it never waits for the root prompt
    @Volatile private var checkNumber = 0

    private lateinit var updateStatus: TextView
    private lateinit var statusText: TextView
    private lateinit var logText: TextView
    private lateinit var logScroll: ScrollView
    private lateinit var progress: ProgressBar
    private lateinit var btnStatus: Button
    private lateinit var btnInstall: Button
    private lateinit var btnRestore: Button
    private lateinit var btnUpdate: Button
    private lateinit var btnFacts: Button

    // Remember the last status so we know which buttons to allow.
    private var canInstall = false
    private var canRestore = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        updateStatus = findViewById(R.id.updateStatus)
        statusText = findViewById(R.id.statusText)
        logText = findViewById(R.id.logText)
        logScroll = findViewById(R.id.logScroll)
        progress = findViewById(R.id.progress)
        btnStatus = findViewById(R.id.btnStatus)
        btnInstall = findViewById(R.id.btnInstall)
        btnRestore = findViewById(R.id.btnRestore)
        btnUpdate = findViewById(R.id.btnUpdate)
        btnFacts = findViewById(R.id.btnFacts)

        // Show which build this is, so you can see an update really happened.
        val build = try { packageManager.getPackageInfo(packageName, 0).longVersionCode } catch (e: Exception) { 0L }
        findViewById<TextView>(R.id.tagline).text = "Patcher - build $build"

        btnStatus.setOnClickListener { runJob("Checking status") { patcher -> showStatus(patcher) } }
        btnInstall.setOnClickListener { runJob("Installing") { patcher -> showResult(patcher.install()) } }
        btnRestore.setOnClickListener { runJob("Restoring") { patcher -> showResult(patcher.restore()) } }

        btnUpdate.setOnClickListener {
            runJob("Updating") { _ ->
                val r = Updater(this) { message -> appendLog(message) }.run()
                appendLog(if (r.success) "DONE: ${r.message}" else "FAILED: ${r.message}")
                checkForUpdate()          // refresh the update line (if an install started, the app restarts and checks again anyway)
            }
        }
        btnFacts.setOnClickListener {
            runJob("Getting facts") { _ ->
                val r = Facts.collect(this) { line -> appendLog(line) }
                appendLog(if (r.success) "DONE: ${r.message}" else "FAILED: ${r.message}")
            }
        }

        applyButtons(busy = false)
        // Check things as soon as the app opens. (This is when the root prompt appears.)
        btnStatus.performClick()
        // Also look (only look!) for a newer build. Installing still needs the Update button.
        checkForUpdate()
    }

    /** Asks GitHub for the newest build number and shows the result on the update line. Never installs anything. */
    private fun checkForUpdate() {
        val mine = ++checkNumber
        runOnUiThread { showUpdateLine(UpdateCheckResult(UpdateState.CHECKING, null)) }
        try {
            checker.execute {
                val installed = try { packageManager.getPackageInfo(packageName, 0).longVersionCode } catch (e: Exception) { 0L }
                val updater = Updater(this) { }
                val result = UpdateCheck.run(installed) { updater.latestBuildCode() }
                if (mine == checkNumber) runOnUiThread { if (!isFinishing) showUpdateLine(result) }
            }
        } catch (e: java.util.concurrent.RejectedExecutionException) {
            // the screen is closing; nothing to show
        }
    }

    private fun showUpdateLine(result: UpdateCheckResult) {
        updateStatus.text = result.label
        updateStatus.setTextColor(
            when (result.state) {
                UpdateState.AVAILABLE -> getColor(R.color.tz_accent)
                UpdateState.UP_TO_DATE -> getColor(R.color.tz_ok)
                else -> getColor(R.color.tz_muted)
            }
        )
    }

    override fun onDestroy() {
        worker.shutdownNow()
        checker.shutdownNow()
        super.onDestroy()
    }

    /** Runs [job] in the background, with the buttons locked and a progress bar showing. */
    private fun runJob(title: String, job: (Patcher) -> Unit) {
        applyButtons(busy = true)
        appendLog("--- $title ---")
        worker.execute {
            try {
                job(Patcher(this) { message -> appendLog(message) })
            } catch (e: Exception) {
                // Safety net: any surprise becomes a readable message instead of a crash.
                appendLog("ERROR: unexpected problem: ${e.javaClass.simpleName}: ${e.message}")
            } finally {
                runOnUiThread { applyButtons(busy = false) }
            }
        }
    }

    /** Status button: show the checklist, then refresh which buttons are allowed. */
    private fun showStatus(patcher: Patcher) {
        val report = patcher.status()
        canInstall = report.canInstall
        canRestore = report.canRestore
        runOnUiThread { statusText.text = report.lines.joinToString("\n") }
        appendLog("Status check finished.")
    }

    /** Install/Restore: print the outcome, then re-check status so the screen stays accurate. */
    private fun showResult(result: ActionResult) {
        appendLog(if (result.success) "SUCCESS: ${result.message}" else "FAILED: ${result.message}")
        // Refresh the checklist (the log keeps the result message).
        val report = Patcher(this) { }.status()
        canInstall = report.canInstall
        canRestore = report.canRestore
        runOnUiThread { statusText.text = report.lines.joinToString("\n") }
    }

    /** Turns buttons on/off. While busy, everything is locked. */
    private fun applyButtons(busy: Boolean) {
        progress.visibility = if (busy) android.view.View.VISIBLE else android.view.View.INVISIBLE
        btnStatus.isEnabled = !busy
        btnInstall.isEnabled = !busy && canInstall
        btnRestore.isEnabled = !busy && canRestore
        btnUpdate.isEnabled = !busy
        btnFacts.isEnabled = !busy
    }

    /** Adds a line to the progress log (safe to call from any thread). */
    private fun appendLog(line: String) {
        runOnUiThread {
            logText.append(line + "\n")
            logScroll.post { logScroll.fullScroll(ScrollView.FOCUS_DOWN) }
        }
    }
}
