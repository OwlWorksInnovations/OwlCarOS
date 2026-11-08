#!/bin/sh
set -eu

TARGET_DIR="$1"

cat > "$TARGET_DIR/usr/lib/os-release" <<'OSRELEASE'
NAME="OwlCarOS"
PRETTY_NAME="OwlCarOS"
ID=owlcaros
VERSION="0.1.0"
VERSION_ID="0.1.0"
OSRELEASE

# Mount USB storage partitions read-only on insertion and release them on removal.
if ! grep -q 'owlcaros-usb-media' "$TARGET_DIR/etc/mdev.conf"; then
    printf '\nsd[a-z][0-9]* root:root 0660 */sbin/owlcaros-usb-media\n' >> "$TARGET_DIR/etc/mdev.conf"
fi
