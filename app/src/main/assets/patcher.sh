#!/system/bin/sh
# =====================================================================
# Timmyzstuff patcher helper.  This runs as ROOT (the app starts it with su).
#
# Usage:  sh patcher.sh <package> <command> <payload_path|-> <payload_sha256|->
#   command = status | install | restore
#
# Output (read by the app):
#   KEY=VALUE   facts about the game, e.g.  backup=valid
#   STEP text   a progress message
#   OK          the very last line when everything worked
#   ERR text    the very last line when something failed
#
# THE SAFETY RULES THIS SCRIPT FOLLOWS
#   1. A valid libmain_orig.so backup is NEVER overwritten.
#   2. New files are written under a temporary name, checked with a
#      SHA-256 hash, and only then renamed into place.
#   3. If installing fails halfway, the original library is put back.
# =====================================================================

PKG="$1"
CMD="$2"
PAYLOAD="$3"
PAYLOAD_SHA="$4"
[ "$PAYLOAD" = "-" ] && PAYLOAD=""
[ "$PAYLOAD_SHA" = "-" ] && PAYLOAD_SHA=""

CTX="u:object_r:apk_data_file:s0"   # SELinux label the game's libraries use
OWNER="system:system"               # owner the game's libraries use
SUFFIX_NEW=".tz_new"                # temporary name while copying

# ---------- tiny helpers ----------
step() { echo "STEP $1"; }
fail() { echo "ERR $1"; exit 1; }

# SHA-256 of a file (prints nothing if the file is missing)
sha() { sha256sum "$1" 2>/dev/null | cut -d' ' -f1; }

# Does the file start with the 4 bytes of an ELF library (7f 45 4c 46)?
is_elf() {
  [ "$(dd if="$1" bs=1 count=4 2>/dev/null | od -An -tx1 | tr -d ' \n')" = "7f454c46" ]
}

# Give a file the same permissions/owner/label as the game's own libraries.
fixperm() { chmod 755 "$1" && chown "$OWNER" "$1" && chcon "$CTX" "$1"; }

case "$PKG" in
  ""|*[!A-Za-z0-9._]*) fail "invalid package name" ;;
esac
case "$CMD" in
  status|install|restore) ;;
  *) fail "unknown command: $CMD" ;;
esac

# ---------- 1. find the game ----------
APK=""
for u in "" "--user 0" "--user 10" "--user current"; do
  APK=$(pm path $u "$PKG" 2>/dev/null | head -n 1 | sed 's/^package://')
  [ -n "$APK" ] && break
done
if [ -z "$APK" ]; then
  if [ "$CMD" = "status" ]; then echo "game=missing"; echo OK; exit 0; fi
  fail "The game ($PKG) is not installed"
fi
echo "game=ok"
echo "version=$(dumpsys package "$PKG" 2>/dev/null | grep -m 1 versionName | sed 's/.*versionName=//')"

# ---------- 2. find the game's library folder ----------
BASE="${APK%/*}"
LIB=""
for d in "$BASE/lib/arm64" "$BASE/lib/arm64-v8a"; do
  if [ -f "$d/libmain.so" ]; then LIB="$d"; break; fi
done
if [ -z "$LIB" ]; then
  if [ "$CMD" = "status" ]; then echo "libdir=missing"; echo OK; exit 0; fi
  fail "Could not find libmain.so under $BASE/lib/ (arm64)"
fi
echo "libdir=$LIB"

MAIN="$LIB/libmain.so"
BAK="$LIB/libmain_orig.so"
STATEF="$LIB/timmyzstuff.state"

# ---------- 3. work out what state things are in ----------
# Sets BAK_STATE (none|valid|invalid) and LIB_STATE (stock|patched|unknown)
classify() {
  CUR_SHA=$(sha "$MAIN")
  ST_ORIG=""; ST_PATCHED=""
  if [ -f "$STATEF" ]; then
    ST_ORIG=$(grep '^orig_sha=' "$STATEF" | head -n 1 | cut -d= -f2)
    ST_PATCHED=$(grep '^patched_sha=' "$STATEF" | head -n 1 | cut -d= -f2)
  fi

  BAK_SHA=""; BAK_STATE="none"
  if [ -e "$BAK" ]; then
    BAK_SHA=$(sha "$BAK")
    # valid = not empty, looks like a library, and matches the hash we recorded
    if [ -s "$BAK" ] && is_elf "$BAK" && { [ -z "$ST_ORIG" ] || [ "$BAK_SHA" = "$ST_ORIG" ]; }; then
      BAK_STATE="valid"
    else
      BAK_STATE="invalid"
    fi
  fi

  LIB_STATE="unknown"
  if [ -n "$CUR_SHA" ]; then
    if { [ -n "$ST_PATCHED" ] && [ "$CUR_SHA" = "$ST_PATCHED" ]; } || \
       { [ -n "$PAYLOAD_SHA" ] && [ "$CUR_SHA" = "$PAYLOAD_SHA" ]; }; then
      LIB_STATE="patched"
    elif [ "$BAK_STATE" = "valid" ] && [ "$CUR_SHA" = "$BAK_SHA" ]; then
      LIB_STATE="stock"
    elif [ "$BAK_STATE" = "none" ] && [ -z "$ST_PATCHED" ]; then
      LIB_STATE="stock"   # no backup, nothing of ours recorded: assume untouched
    fi
  fi
}

# Copy $1 over libmain.so safely.  $2 = the SHA-256 it must have.
# Returns 0 on success, or a number saying which stage failed.
swap_in() {
  NEW="$MAIN$SUFFIX_NEW"
  rm -f "$NEW"
  cp "$1" "$NEW"                            || { rm -f "$NEW"; return 1; }
  fixperm "$NEW"                            || { rm -f "$NEW"; return 2; }
  [ "$(sha "$NEW")" = "$2" ]                || { rm -f "$NEW"; return 3; }
  mv -f "$NEW" "$MAIN"                      || { rm -f "$NEW"; return 4; }
  [ "$(sha "$MAIN")" = "$2" ]               || return 5
  return 0
}

swap_msg() {
  case "$1" in
    1) echo "could not copy the file into the game folder" ;;
    2) echo "could not set permissions / SELinux label" ;;
    3) echo "the copy did not match (hash check failed)" ;;
    4) echo "could not move the new file into place" ;;
    5) echo "the final library did not match after moving" ;;
    *) echo "unknown problem" ;;
  esac
}

write_state() {  # $1 = original hash, $2 = patched hash
  t="$STATEF.tmp"
  { echo "orig_sha=$1"; echo "patched_sha=$2"; echo "package=$PKG"; } > "$t" \
    && fixperm "$t" && mv -f "$t" "$STATEF"
}

# Put the original library back from the backup (used when an install fails)
recover() {
  step "Putting the original library back"
  if [ "$(sha "$MAIN")" = "$BAK_SHA" ]; then
    step "The original library is already in place"
    return 0
  fi
  swap_in "$BAK" "$BAK_SHA"
}

classify

# ---------- STATUS ----------
if [ "$CMD" = "status" ]; then
  echo "backup=$BAK_STATE"
  echo "libmain=$LIB_STATE"
  if [ -n "$PAYLOAD_SHA" ]; then
    [ "$CUR_SHA" = "$PAYLOAD_SHA" ] && echo "current=yes" || echo "current=no"
  fi
  echo OK
  exit 0
fi

# ---------- INSTALL ----------
if [ "$CMD" = "install" ]; then
  step "Checking the payload file"
  [ -f "$PAYLOAD" ] || fail "The payload file was not found (the app should have staged it)"
  [ -n "$PAYLOAD_SHA" ] || fail "No payload hash was given"
  [ "$(sha "$PAYLOAD")" = "$PAYLOAD_SHA" ] || fail "The staged payload is damaged (hash mismatch)"

  step "Checking the game's files"
  case "$BAK_STATE" in
    invalid) fail "The existing backup (libmain_orig.so) looks damaged. Nothing was changed. Reinstall the game, then try again." ;;
  esac
  case "$LIB_STATE" in
    unknown) fail "libmain.so is not the original and was not installed by Timmyzstuff. Nothing was changed. Reinstall the game, then try again." ;;
  esac
  if [ "$LIB_STATE" = "patched" ] && [ "$BAK_STATE" != "valid" ]; then
    fail "The game is patched but its original backup is missing. Nothing was changed. Reinstall the game, then try again."
  fi

  step "Stopping the game"
  am force-stop "$PKG"

  # --- backup (only when there is none; a valid one is never touched) ---
  if [ "$BAK_STATE" = "none" ]; then
    step "Backing up the original libmain.so"
    [ -e "$BAK" ] && fail "Backup appeared unexpectedly; refusing to overwrite it"
    TMPB="$BAK.tmp"
    rm -f "$TMPB"
    cp "$MAIN" "$TMPB" || { rm -f "$TMPB"; fail "Could not copy the backup"; }
    [ "$(sha "$TMPB")" = "$CUR_SHA" ] || { rm -f "$TMPB"; fail "Backup copy did not match the original (hash check failed)"; }
    fixperm "$TMPB" || { rm -f "$TMPB"; fail "Could not set permissions on the backup"; }
    [ -e "$BAK" ] && { rm -f "$TMPB"; fail "Backup appeared unexpectedly; refusing to overwrite it"; }
    mv "$TMPB" "$BAK" || { rm -f "$TMPB"; fail "Could not save the backup"; }
    BAK_SHA="$CUR_SHA"
    step "Backup saved and verified"
  else
    step "A valid backup already exists - keeping it untouched"
  fi

  # --- already up to date? ---
  if [ "$CUR_SHA" = "$PAYLOAD_SHA" ]; then
    step "This payload is already installed"
    write_state "$BAK_SHA" "$PAYLOAD_SHA" || fail "Could not write the state file"
    echo OK
    exit 0
  fi

  step "Copying the payload into the game"
  swap_in "$PAYLOAD" "$PAYLOAD_SHA"
  rc=$?
  if [ $rc -ne 0 ]; then
    why=$(swap_msg $rc)
    if recover; then
      fail "Install failed: $why. The original library was put back, so the game is unchanged."
    else
      fail "CRITICAL: install failed ($why) AND automatic recovery failed. Your backup is $BAK - copy it over $MAIN, or reinstall the game."
    fi
  fi

  step "Verifying the installed library"
  if ! write_state "$BAK_SHA" "$PAYLOAD_SHA"; then
    recover
    fail "Could not write the state file; the original library was put back."
  fi
  am force-stop "$PKG"
  echo OK
  exit 0
fi

# ---------- RESTORE ----------
if [ "$CMD" = "restore" ]; then
  step "Checking the backup"
  [ "$BAK_STATE" = "valid" ] || fail "No valid backup of the original library was found. If the game was never patched, there is nothing to restore."
  if [ "$LIB_STATE" = "unknown" ]; then
    fail "libmain.so is not something Timmyzstuff installed, so restoring the backup could be wrong (it may be from an older game version). Nothing was changed. Reinstall the game instead."
  fi

  step "Stopping the game"
  am force-stop "$PKG"

  if [ "$CUR_SHA" = "$BAK_SHA" ]; then
    step "The original library is already in place"
  else
    step "Copying the original library back"
    swap_in "$BAK" "$BAK_SHA"
    rc=$?
    [ $rc -eq 0 ] || fail "Restore failed: $(swap_msg $rc). The backup was kept at $BAK."
  fi

  step "Cleaning up"
  rm -f "$BAK" "$STATEF"
  am force-stop "$PKG"
  echo OK
  exit 0
fi
