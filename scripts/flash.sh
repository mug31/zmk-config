#!/bin/bash
# Usage: scripts/flash.sh [left] [right]   (default: left right)
# Waits for each half's NICENANO bootloader drive (double-tap reset), copies its uf2.
F=${FW_DIR:-~/Downloads/corne-mouse-firmware}
flash() {
  local side=$1 file=$F/corne_${1}-nice_nano_v2-zmk.uf2 dev="" mnt
  [ -f "$file" ] || { echo "missing $file"; exit 1; }
  echo "[$(date +%T)] waiting for NICENANO ($side) — double-tap reset now"
  for i in $(seq 1 600); do
    dev=$(lsblk -rpo NAME,LABEL | awk '$2=="NICENANO"{print $1; exit}')
    [ -n "$dev" ] && break; sleep 1
  done
  [ -z "$dev" ] && { echo "timeout waiting for $side"; exit 1; }
  sleep 1
  mnt=$(lsblk -no MOUNTPOINT "$dev" | head -1)
  [ -z "$mnt" ] && { udisksctl mount -b "$dev" >/dev/null; mnt=$(lsblk -no MOUNTPOINT "$dev" | head -1); }
  cp "$file" "$mnt/"; sync
  echo "[$(date +%T)] $side: copied $(basename "$file")"
  for i in $(seq 1 30); do lsblk -rpo LABEL | grep -qx NICENANO || break; sleep 1; done
  echo "[$(date +%T)] $side: rebooted into new firmware"
}
for side in ${@:-left right}; do flash "$side"; done
echo DONE
