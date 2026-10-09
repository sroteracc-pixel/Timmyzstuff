import com.timmyzstuff.patcher.UpdateCheck
import com.timmyzstuff.patcher.UpdateState
import java.io.IOException

var passed = 0
var failed = 0
fun check(name: String, ok: Boolean) { if (ok) { passed++; println("  PASS  $name") } else { failed++; println("  FAIL  $name") } }

fun main() {
    // newer build exists
    val a = UpdateCheck.run(12) { 15 }
    check("newer build -> AVAILABLE", a.state == UpdateState.AVAILABLE && a.latestBuild == 15L)
    check("newer build text is exactly: Update available — build 15", a.label == "Update available — build 15")
    // same build
    val b = UpdateCheck.run(15) { 15 }
    check("same build -> UP_TO_DATE", b.state == UpdateState.UP_TO_DATE && b.label == "Up to date")
    // this app is newer than the release (local/dev build)
    val c = UpdateCheck.run(20) { 15 }
    check("this app newer than GitHub -> Up to date (not 'available')", c.state == UpdateState.UP_TO_DATE && c.label == "Up to date")
    // failures never crash and say so
    val d = UpdateCheck.run(12) { throw IOException("no internet") }
    check("no internet -> Couldn't check for updates", d.state == UpdateState.FAILED && d.label == "Couldn't check for updates" && d.latestBuild == null)
    val e = UpdateCheck.run(12) { throw IllegalArgumentException("bad JSON") }
    check("unreadable answer from GitHub -> Couldn't check", e.state == UpdateState.FAILED)
    val f = UpdateCheck.run(12) { throw IllegalStateException("anything else") }
    check("any other surprise -> Couldn't check (no crash)", f.state == UpdateState.FAILED)
    check("nonsense build numbers (0, -3) are treated as a failed check", UpdateCheck.decide(12, 0).state == UpdateState.FAILED && UpdateCheck.decide(12, -3).state == UpdateState.FAILED && UpdateCheck.decide(12, null).state == UpdateState.FAILED)
    check("checking text", com.timmyzstuff.patcher.UpdateCheckResult(UpdateState.CHECKING, null).label == "Checking for updates…")
    check("a very large build number still compares correctly", UpdateCheck.decide(99, 100).state == UpdateState.AVAILABLE && UpdateCheck.decide(100, 100).state == UpdateState.UP_TO_DATE)
    println("\npassed: $passed  failed: $failed")
    if (failed > 0) System.exit(1)
}
