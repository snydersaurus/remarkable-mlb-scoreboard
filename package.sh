#!/usr/bin/env bash
# Build the installable AppLoad app into dist/.
#
#   ./package.sh ~/Downloads/remarkable-production-image-<ver>-<codename>-public-x86_64-toolchain.sh
#
# Produces dist/mlb-scoreboard/ (the app directory) and dist/mlb-scoreboard.zip.
set -euo pipefail

SDK_SH="${1:?usage: ./package.sh /path/to/remarkable-...-toolchain.sh}"
HERE="$(cd "$(dirname "$0")" && pwd)"
IMAGE="${IMAGE:-rmpp-sdk}"
OUT="$HERE/dist/mlb-scoreboard"

"$HERE/build.sh" "$SDK_SH"

echo "==> linting QML"
# rcc packages files without parsing them, so broken QML would bundle and deploy
# happily and only show up as a blank app on the device.
docker run --rm --platform linux/amd64 -v "$HERE/appload-native":/app "$IMAGE" bash -lc '
  source /opt/rmpp-sdk/environment-setup-*-remarkable-linux 2>/dev/null
  LINT="${OECORE_NATIVE_SYSROOT}/usr/bin/qmllint"
  bad=0
  for f in /app/ui/*.qml; do
    out=$("$LINT" --bare "$f" 2>&1)
    if echo "$out" | grep -qiE "expected|unexpected|syntax"; then
      bad=1; echo "SYNTAX ERROR in $(basename "$f")"; echo "$out" | head -5
    fi
  done
  [ $bad -eq 0 ] || exit 1
  echo "==> building resources.rcc"
  # --no-compress so the bundle stays greppable. rcc does not always rerun, and a
  # silently stale bundle is indistinguishable from "my change did nothing" until
  # you are standing at the tablet an hour later -- the verification below is the
  # whole point, and it cannot read a compressed bundle.
  "${OECORE_NATIVE_SYSROOT}/usr/libexec/rcc" --binary --no-compress -o /app/resources.rcc /app/application.qrc
'

echo "==> verifying the bundle"
# rcc packages files without parsing them and does not always rerun, so check
# the bundle rather than trusting the exit code. Two distinct failures:
#   stale  -- rcc silently did not rerun, so the bundle predates an edit
#   partial -- a qrc entry did not make it in
# Note rcc stores resource NAMES as UTF-16BE, so grepping the bundle for
# "Main.qml" in ASCII always fails and proves nothing. Search the encoded form.
# (File CONTENT is plain text, which is what --no-compress is for.)
python3 - "$HERE" <<'PYEOF' || exit 1
import os, re, sys
here = sys.argv[1]
rcc  = os.path.join(here, "appload-native", "resources.rcc")
qrc  = os.path.join(here, "appload-native", "application.qrc")
blob = open(rcc, "rb").read()
names = re.findall(r"<file>(.*?)</file>", open(qrc).read())

bad = False
for n in names:
    if os.path.basename(n).encode("utf-16-be") not in blob:
        print("  MISSING from resources.rcc: %s" % n); bad = True

rcc_mtime = os.path.getmtime(rcc)
for n in names:
    src = os.path.join(here, "appload-native", n)
    if os.path.getmtime(src) > rcc_mtime:
        print("  NEWER than the bundle: %s" % n); bad = True

if bad:
    print("bundle is stale or incomplete -- refusing to package"); sys.exit(1)
print("  %d qml files present, bundle newer than all of them" % len(names))
PYEOF

echo "==> assembling $OUT"
rm -rf "$HERE/dist"
mkdir -p "$OUT/backend"
cp "$HERE/appload-native/manifest.json" "$OUT/"
cp "$HERE/appload-native/icon.png"      "$OUT/"
cp "$HERE/appload-native/resources.rcc" "$OUT/"
cp "$HERE/build-rmpp/scoreboard_backend" "$OUT/backend/entry"
chmod +x "$OUT/backend/entry"
cp "$HERE/openssl-scoreboard.cnf" "$HERE/dist/"
cp "$HERE/install.sh" "$HERE/dist/"

( cd "$HERE/dist" && zip -qr mlb-scoreboard.zip mlb-scoreboard openssl-scoreboard.cnf install.sh )

echo
echo "==> dist/mlb-scoreboard.zip"
unzip -l "$HERE/dist/mlb-scoreboard.zip"
