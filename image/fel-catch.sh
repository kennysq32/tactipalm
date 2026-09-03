#!/usr/bin/env bash
# Catch the H618 FEL window and fire sunxi-fel into it.
#
# Observed on this board: FEL appears for a hard 3.10 s and disconnects. That is
# a software timeout, not hardware flakiness, and ModemManager probing the
# device is the usual cause — it also corrupts the AW protocol mid-transfer,
# which surfaces as:
#
#     Assertion `strcmp(buf, "AWUS") == 0' failed
#
# Run this FIRST, then plug the board in.
#
#   ./fel-catch.sh                 # identify the SoC
#   ./fel-catch.sh <cmd> [args]    # any sunxi-fel subcommand
set -uo pipefail

TIMEOUT="${TIMEOUT:-120}"
SETTLE="${SETTLE:-0.6}"     # wait for the uaccess ACL to land, not just enumeration
RETRIES="${RETRIES:-4}"      # FEL reappears on replug; try across windows

command -v sunxi-fel >/dev/null || { echo "sunxi-fel not found: sudo apt install sunxi-tools"; exit 1; }

if systemctl is-active --quiet ModemManager 2>/dev/null; then
  echo "!! ModemManager is ACTIVE. It probes unknown USB devices and is the most"
  echo "!! likely cause of both the 3.1 s disconnect and the AWUS assertion."
  echo "!! Either install the udev rule in docs/FEL.md, or for a one-off test:"
  echo "!!     sudo systemctl stop ModemManager"
  echo
fi

attempt=0
deadline=$(( $(date +%s) + TIMEOUT ))
while [ "$attempt" -lt "$RETRIES" ] && [ "$(date +%s)" -lt "$deadline" ]; do
  if lsusb -d 1f3a:efe8 >/dev/null 2>&1; then
    attempt=$((attempt+1))
    echo "=== FEL detected $(date +%H:%M:%S) (attempt ${attempt}/${RETRIES}) ==="
    sleep "$SETTLE"
    if [ $# -eq 0 ]; then sunxi-fel version; else sunxi-fel "$@"; fi
    rc=$?
    [ $rc -eq 0 ] && { echo "=== OK ==="; exit 0; }
    echo "=== failed (rc=${rc}); power-cycle the board and I will retry ==="
    while lsusb -d 1f3a:efe8 >/dev/null 2>&1; do sleep 0.1; done
  fi
  sleep 0.1
done
echo "Gave up after ${attempt} attempt(s)."
exit 1
