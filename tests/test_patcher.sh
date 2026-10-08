#!/bin/bash
# Runs patcher.sh on a normal Linux/macOS computer with FAKE Android commands.
# It proves the backup/verify/recovery LOGIC works. It does NOT prove anything
# about a real Quest (permissions, SELinux, the real game) - that needs your headset.
# Usage: bash tests/test_patcher.sh
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
SCRIPT="$HERE/../app/src/main/assets/patcher.sh"
W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
PKG="com.example.game"
LIB="$W/data/app/game-1/lib/arm64"; mkdir -p "$LIB" "$W/bin"
SHIM="$W/bin"

# --- fake Android commands ---
cat > "$SHIM/pm" <<S
#!/bin/sh
[ "\$1" = path ] && [ -z "\${TZ_NOGAME:-}" ] && echo "package:$W/data/app/game-1/base.apk"
exit 0
S
printf '#!/bin/sh\necho " versionName=1.2.3"\n' > "$SHIM/dumpsys"
printf '#!/bin/sh\necho "force-stop $*" >> "'"$W"'/am.log"\n' > "$SHIM/am"
printf '#!/bin/sh\nexit 0\n' > "$SHIM/chown"
printf '#!/bin/sh\nexit 0\n' > "$SHIM/chcon"
# cp that can be told to write a damaged copy of the payload
cat > "$SHIM/cp" <<S
#!/bin/sh
/bin/cp "\$@" || exit \$?
if [ "\${TZ_BADCOPY:-}" = 1 ]; then case "\$2" in *.tz_new) case "\$1" in *payload*) echo junk >> "\$2";; esac;; esac; fi
S
# mv that can damage the file right after moving it (once)
cat > "$SHIM/mv" <<S
#!/bin/sh
/bin/mv "\$@" || exit \$?
if [ "\${TZ_BADMOVE:-}" = 1 ] && [ ! -e "$W/badmove.done" ]; then
  for last; do :; done
  case "\$last" in */libmain.so) echo junk >> "\$last"; touch "$W/badmove.done";; esac
fi
S
chmod +x "$SHIM"/*
export PATH="$SHIM:$PATH"

mkelf() { printf '\x7fELF%s' "$2" > "$1"; }
mkelf "$W/stock.so" "stock-library-bytes"
mkelf "$W/payload1.so" "payload-one"
mkelf "$W/payload2.so" "payload-two"
H() { sha256sum "$1" | cut -d' ' -f1; }
reset() { rm -f "$LIB"/* "$W/badmove.done"; cp "$W/stock.so" "$LIB/libmain.so"; }
run() { sh "$SCRIPT" "$PKG" "$@"; }
pass=0; failn=0
check() { if eval "$2"; then pass=$((pass+1)); echo "  PASS  $1"; else failn=$((failn+1)); echo "  FAIL  $1"; fi; }
last() { tail -n 1 <<<"$1"; }

P1=$(H "$W/payload1.so"); P2=$(H "$W/payload2.so"); S=$(H "$W/stock.so")

echo "== status"
reset; out=$(run status - -); check "stock game reports libmain=stock, backup=none" '[[ "$out" == *libmain=stock* && "$out" == *backup=none* && "$(last "$out")" == OK ]]'
out=$(TZ_NOGAME=1 run status - -); check "missing game reports game=missing" '[[ "$out" == *game=missing* ]]'
out=$(TZ_NOGAME=1 run install "$W/payload1.so" "$P1"); check "install without game fails clearly" '[[ "$(last "$out")" == ERR*not\ installed* ]]'

echo "== install"
reset; out=$(run install "$W/payload1.so" "$P1")
check "install ends with OK" '[[ "$(last "$out")" == OK ]]'
check "libmain.so is now the payload" '[[ "$(H "$LIB/libmain.so")" == "$P1" ]]'
check "backup equals the original" '[[ "$(H "$LIB/libmain_orig.so")" == "$S" ]]'
check "no temp files left behind" '[[ -z "$(ls "$LIB" | grep -E "tz_new|\.tmp")" ]]'
out=$(run status "$W/payload1.so" "$P1"); check "status says patched + current=yes" '[[ "$out" == *libmain=patched* && "$out" == *current=yes* && "$out" == *backup=valid* ]]'

echo "== backup is never overwritten"
out=$(run install "$W/payload2.so" "$P2"); check "installing a different payload works" '[[ "$(last "$out")" == OK && "$(H "$LIB/libmain.so")" == "$P2" ]]'
check "backup is still the ORIGINAL (not a payload)" '[[ "$(H "$LIB/libmain_orig.so")" == "$S" ]]'
out=$(run install "$W/payload2.so" "$P2"); check "re-installing the same payload is harmless" '[[ "$(last "$out")" == OK && "$(H "$LIB/libmain_orig.so")" == "$S" ]]'

echo "== restore"
out=$(run restore - -); check "restore ends with OK" '[[ "$(last "$out")" == OK ]]'
check "libmain.so is the original again" '[[ "$(H "$LIB/libmain.so")" == "$S" ]]'
check "backup + state removed after restore" '[[ ! -e "$LIB/libmain_orig.so" && ! -e "$LIB/timmyzstuff.state" ]]'
out=$(run restore - -); check "restore with no backup gives a clear error" '[[ "$(last "$out")" == ERR*backup* ]]'

echo "== payload problems"
reset; out=$(run install "$W/payload1.so" "$P2"); check "wrong hash is rejected, nothing changed" '[[ "$(last "$out")" == ERR* && "$(H "$LIB/libmain.so")" == "$S" && ! -e "$LIB/libmain_orig.so" ]]'
reset; out=$(run install "$W/missing.so" "$P1"); check "missing payload file is rejected" '[[ "$(last "$out")" == ERR* && ! -e "$LIB/libmain_orig.so" ]]'

echo "== recovery"
reset; out=$(TZ_BADCOPY=1 run install "$W/payload1.so" "$P1")
check "damaged copy -> ERR, game left original" '[[ "$(last "$out")" == ERR*unchanged* && "$(H "$LIB/libmain.so")" == "$S" ]]'
check "damaged copy -> backup still valid" '[[ "$(H "$LIB/libmain_orig.so")" == "$S" ]]'
reset; out=$(TZ_BADMOVE=1 run install "$W/payload1.so" "$P1")
check "damage AFTER moving -> original restored automatically" '[[ "$(last "$out")" == ERR*put\ back* && "$(H "$LIB/libmain.so")" == "$S" ]]'

echo "== refuses unsafe situations"
reset; echo x > "$LIB/libmain_orig.so"; out=$(run install "$W/payload1.so" "$P1")
check "damaged backup -> refuses, libmain untouched" '[[ "$(last "$out")" == ERR*backup* && "$(H "$LIB/libmain.so")" == "$S" ]]'
reset; run install "$W/payload1.so" "$P1" >/dev/null; mkelf "$LIB/libmain.so" "game-updated-in-place"
out=$(run install "$W/payload2.so" "$P2"); check "unknown libmain -> refuses to install" '[[ "$(last "$out")" == ERR*not\ the\ original* ]]'
out=$(run restore - -); check "unknown libmain -> refuses to restore stale backup" '[[ "$(last "$out")" == ERR* && "$(H "$LIB/libmain_orig.so")" == "$S" ]]'
reset; run install "$W/payload1.so" "$P1" >/dev/null; rm -f "$LIB/libmain_orig.so"
out=$(run install "$W/payload2.so" "$P2"); check "patched but backup missing -> refuses" '[[ "$(last "$out")" == ERR*backup* ]]'

echo; echo "passed: $pass  failed: $failn"; [ "$failn" -eq 0 ]
