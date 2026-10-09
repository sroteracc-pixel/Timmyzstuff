#!/bin/bash
# PC test of the update line's decision logic (UpdateCheck.kt). Needs a Kotlin compiler (kotlinc) - skipped politely if missing.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"; W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
KC="${KOTLINC:-$(command -v kotlinc || echo /tmp/kt/kotlinc/bin/kotlinc)}"
if [ ! -x "$KC" ]; then echo "SKIPPED: no kotlinc found (set KOTLINC=/path/to/kotlinc)"; exit 0; fi
"$KC" -nowarn "$HERE/../app/src/main/java/com/timmyzstuff/patcher/UpdateCheck.kt" "$HERE/UpdateCheckTest.kt" -include-runtime -d "$W/t.jar" 2>&1 | grep -v "^warning\|JAVA_TOOL" 
java -jar "$W/t.jar" 2>&1 | grep -v JAVA_TOOL
