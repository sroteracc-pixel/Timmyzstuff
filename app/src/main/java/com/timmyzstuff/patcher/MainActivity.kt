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

    private lateinit var statusText: TextView
    private lateinit var logText: TextView
    private lateinit var logScroll: ScrollView
    private lateinit var progress: ProgressBar
    private lateinit var btnStatus: Button
    private lateinit var btnInstall: Button
    private lateinit var btnRestore: Button

    // Remember the last status so we know which buttons to allow.
    private var canInstall = false
    private var canRestore = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        statusText = findViewById(R.id.statusText)
        logText = findViewById(R.id.logText)
        logScroll = findViewById(R.id.logScroll)
        progress = findViewById(R.id.progress)
        btnStatus = findViewById(R.id.btnStatus)
        btnInstall = findViewById(R.id.btnInstall)
        btnRestore = findViewById(R.id.btnRestore)

        btnStatus.setOnClickListener { runJob("Checking status") { patcher -> showStatus(patcher) } }
        btnInstall.setOnClickListener { runJob("Installing") { patcher -> showResult(patcher.install()) } }
        btnRestore.setOnClickListener { runJob("Restoring") { patcher -> showResult(patcher.restore()) } }

        applyButtons(busy = false)
        // Check things as soon as the app opens. (This is when the root prompt appears.)
        btnStatus.performClick()
    }

    override fun onDestroy() {
        worker.shutdownNow()
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
    }

    /** Adds a line to the progress log (safe to call from any thread). */
    private fun appendLog(line: String) {
        runOnUiThread {
            logText.append(line + "\n")
            logScroll.post { logScroll.fullScroll(ScrollView.FOCUS_DOWN) }
        }
    }
}
