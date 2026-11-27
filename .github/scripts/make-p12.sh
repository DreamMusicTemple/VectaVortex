#!/usr/bin/env bash
# Vektur Vortex - build a password-protected Apple Distribution .p12 from
#   (1) the Apple-issued certificate (.cer DER or .pem)
#   (2) the encrypted private key from the CSR workflow
# No plaintext private key is ever written to disk or printed.
# Usage:  ./make-p12.sh  CERT_FILE  ENCRYPTED_KEY_FILE  OUT.p12
# Passwords are read silently from the terminal (or from env KEY_PASS / P12_PASS for CI).
set -euo pipefail
umask 077
CERT=${1:?cert file}; KEY=${2:?encrypted key file}; OUT=${3:?output .p12}
: "${KEY_PASS:=}"; : "${P12_PASS:=}"
if [ -z "$KEY_PASS" ]; then read -r -s -p "Password of the encrypted private key (CSR_KEY_PASSWORD): " KEY_PASS; echo; fi
if [ -z "$P12_PASS" ]; then
  read -r -s -p "NEW password to protect the .p12: " P12_PASS; echo
  read -r -s -p "Repeat it: " P2; echo; [ "$P12_PASS" = "$P2" ] || { echo "passwords differ"; exit 1; }
fi
export KEY_PASS P12_PASS
WORK=$(mktemp -d); trap 'rm -rf "$WORK"; unset KEY_PASS P12_PASS P2' EXIT   # only public material lands here

# 1. certificate -> PEM (accept DER .cer or PEM)
if openssl x509 -in "$CERT" -inform DER -out "$WORK/cert.pem" 2>/dev/null; then :; \
elif openssl x509 -in "$CERT" -out "$WORK/cert.pem" 2>/dev/null; then :; \
else echo "FAIL: cannot parse certificate"; exit 2; fi
echo "Certificate subject/dates:"; openssl x509 -in "$WORK/cert.pem" -noout -subject -enddate
SUBJ=$(openssl x509 -in "$WORK/cert.pem" -noout -subject)
case "$SUBJ" in *"Apple Distribution"*|*"iPhone Distribution"*) ;; *) echo "WARNING: not an Apple Distribution certificate";; esac

# 2. key and certificate must share a public key (decrypted in memory only)
A=$(openssl pkey -in "$KEY" -passin env:KEY_PASS -pubout 2>/dev/null | openssl sha256) || { echo "FAIL: key will not decrypt (wrong CSR_KEY_PASSWORD or not a PKCS#8/PEM key)"; exit 3; }
B=$(openssl x509 -in "$WORK/cert.pem" -pubkey -noout | openssl sha256)
[ "$A" = "$B" ] && echo "OK: certificate and private key public keys MATCH" || { echo "FAIL: certificate was NOT issued for this key (different CSR)"; exit 4; }

# 3. Apple intermediate (WWDR) for a complete chain is optional; Keychain/Xcode already trust it.
# 4. export. Legacy 3DES/SHA1 wrapping because macOS 'security import' and older Keychain reject OpenSSL 3's default AES p12 ("MAC verification failed").
openssl pkcs12 -export -inkey "$KEY" -passin env:KEY_PASS -in "$WORK/cert.pem" \
  -name "Apple Distribution - Vektur Vortex" \
  -certpbe PBE-SHA1-3DES -keypbe PBE-SHA1-3DES -macalg sha1 \
  -out "$OUT" -passout env:P12_PASS
chmod 600 "$OUT"

# 5. verify it opens and still contains key + cert
openssl pkcs12 -in "$OUT" -passin env:P12_PASS -noout -info 2>&1 | grep -Ei "MAC|Shrouded|Certificate bag" | sed 's/^/  /' || true
C=$(openssl pkcs12 -in "$OUT" -passin env:P12_PASS -nodes 2>/dev/null | openssl pkey -pubout 2>/dev/null | openssl sha256)
[ "$C" = "$B" ] && echo "OK: .p12 opens with the new password and holds the matching key" || { echo "FAIL: p12 verification"; rm -f "$OUT"; exit 5; }
echo "Done: $OUT   (sha256: $(openssl sha256 < "$OUT" | awk '{print $2}'))"
