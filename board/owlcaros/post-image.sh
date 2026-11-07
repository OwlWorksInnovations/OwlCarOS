#!/bin/sh
set -eu

BINARIES_DIR="$1"
SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
STAGING_DIR="$(mktemp -d)"

trap 'rm -rf "$STAGING_DIR"' EXIT

mkdir -p "$STAGING_DIR/EFI/BOOT"
mkdir -p "$STAGING_DIR/boot"

# Copy generated boot files into temporary staging
cp "$BINARIES_DIR/efi-part/EFI/BOOT/bootx64.efi" \
   "$STAGING_DIR/EFI/BOOT/BOOTX64.EFI"

cp "$SCRIPT_DIR/staging/EFI/BOOT/grub.cfg" \
   "$STAGING_DIR/EFI/BOOT/grub.cfg"

cp "$BINARIES_DIR/bzImage" \
   "$STAGING_DIR/boot/bzImage"

cp "$BINARIES_DIR/rootfs.ext2" \
   "$STAGING_DIR/rootfs.ext2"

# Assemble the complete disk image
"$HOST_DIR/bin/genimage" \
    --rootpath "$TARGET_DIR" \
    --inputpath "$STAGING_DIR" \
    --outputpath "$BINARIES_DIR" \
    --config "$SCRIPT_DIR/genimage.cfg"

echo "OwlCarOS disk image created: $BINARIES_DIR/OwlCarOS.img"
