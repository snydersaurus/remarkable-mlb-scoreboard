#!/usr/bin/env bash
# Install the scoreboard onto a reMarkable running xovi + AppLoad.
#
#   RM_HOST=10.11.99.1 ./install.sh
#
# Requires developer mode and SSH access. You will be asked for the tablet's
# root password unless you have already installed a key; it is on the device
# under Settings > About > Copyright and licenses.
set -euo pipefail

RM_HOST="${RM_HOST:-10.11.99.1}"
HERE="$(cd "$(dirname "$0")" && pwd)"
APPDIR=/home/root/xovi/exthome/appload/mlb-scoreboard

[ -d "$HERE/mlb-scoreboard" ] || { echo "run this from the unzipped dist/ folder"; exit 1; }

echo "==> connecting to $RM_HOST"
if ! ssh -o ConnectTimeout=10 "root@$RM_HOST" true 2>/dev/null; then
    cat <<MSG
Cannot reach the tablet at $RM_HOST over SSH.

  - Plugged in? 10.11.99.1 is the USB address.
  - Over wifi, use the address in Settings > About, and note that SSH over
    WLAN is off by default (enable with: rm-ssh-over-wlan on, over USB).
  - Developer mode has to be on.
MSG
    exit 1
fi

echo "==> checking the tablet has AppLoad"
if ! ssh "root@$RM_HOST" '[ -d /home/root/xovi/exthome/appload ]'; then
    echo "Connected, but AppLoad is not installed. Install xovi + AppLoad first:"
    echo "  https://github.com/asivery/rm-appload"
    exit 1
fi

echo "==> copying the app"
ssh "root@$RM_HOST" "mkdir -p $APPDIR/backend"

# Everything except the backend can be overwritten in place.
scp -q "$HERE/mlb-scoreboard/manifest.json" "$HERE/mlb-scoreboard/icon.png" \
       "$HERE/mlb-scoreboard/resources.rcc" "root@$RM_HOST:$APPDIR/"

# The backend cannot. AppLoad leaves it running after the app is closed, so
# writing over it fails with ETXTBSY -- reported only as "dest open ... Failure"
# -- and the install half-happens, leaving new QML against an old binary. A
# move just relinks the directory entry: the running process keeps the old
# inode and the next launch picks up the new one.
scp -q "$HERE/mlb-scoreboard/backend/entry" "root@$RM_HOST:$APPDIR/backend/entry.new"
ssh "root@$RM_HOST" "chmod +x $APPDIR/backend/entry.new && mv -f $APPDIR/backend/entry.new $APPDIR/backend/entry"

# The device restricts TLS 1.2 to ECDHE-ECDSA suites (SOG-IS, for EU-RED), and
# statsapi.mlb.com serves an RSA certificate without TLS 1.3 -- no overlap, so
# the handshake fails and every screen reads OFFLINE. This per-process config
# restores the default cipher list for the backend only.
echo "==> installing the TLS config"
scp -q "$HERE/openssl-scoreboard.cnf" "root@$RM_HOST:/home/root/openssl-scoreboard.cnf"

cat <<MSG

Installed.

AppLoad only reads app manifests and icons when xochitl starts, so restart it:

  ssh root@$RM_HOST 'systemctl restart xochitl'

Then open the sidebar, tap AppLoad, and tap MLB.
MSG
