#!/usr/bin/env bash
# Build the P12 (make-p12.sh) and deliver it ONLY as an age-encrypted file for the owner's public key.
# The plaintext .p12 lives in a private temp dir and is removed on exit; it is never in the upload directory.
# Usage: package-p12.sh CERT ENCRYPTED_KEY OUT_DIR AGE_RECIPIENT      (env: KEY_PASS, P12_PASS)
set -euo pipefail
umask 077
CERT=${1:?}; KEY=${2:?}; OUTDIR=${3:?}; RCPT=${4:-}
case "$RCPT" in age1[0-9a-z]*) ;; *) echo "FAIL: P12_AGE_RECIPIENT (a public key starting 'age1') is missing or invalid. Nothing was created."; exit 6;; esac
[ "$(printf '%s' "$RCPT" | wc -l)" -eq 0 ] || { echo "FAIL: recipient must be a single line"; exit 6; }
command -v age >/dev/null || { echo "FAIL: age not installed"; exit 6; }
HERE=$(cd "$(dirname "$0")" && pwd)
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
bash "$HERE/make-p12.sh" "$CERT" "$KEY" "$TMP/VekturVortex_Distribution.p12"     # exits non-zero on mismatch/wrong password
mkdir -p "$OUTDIR"
age -r "$RCPT" -o "$OUTDIR/VekturVortex_Distribution.p12.age" "$TMP/VekturVortex_Distribution.p12"
head -c 40 "$OUTDIR/VekturVortex_Distribution.p12.age" | grep -q '^age-encryption.org' || { echo "FAIL: output is not age-encrypted"; rm -f "$OUTDIR"/*; exit 7; }
ls "$OUTDIR" | grep -qv '\.age$' && { echo "FAIL: unencrypted file in upload dir"; rm -rf "$OUTDIR"; exit 7; } || true
echo "OK: encrypted P12 written: $(basename "$OUTDIR")/VekturVortex_Distribution.p12.age ($(wc -c < "$OUTDIR/VekturVortex_Distribution.p12.age") bytes)"
