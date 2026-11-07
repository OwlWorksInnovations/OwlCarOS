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
