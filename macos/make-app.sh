#!/bin/sh

set -e

TOPDIR=`pwd`
BIN="$TOPDIR/coma"
APP="$TOPDIR/Coma.app"

if [ ! -x "$BIN" ]; then
	echo "error: $BIN not found, run 'make' first" >&2
	exit 1
fi

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS"
mkdir -p "$APP/Contents/Resources"

cp "$BIN" "$APP/Contents/MacOS/coma"
cp "$TOPDIR/macos/Info.plist" "$APP/Contents/Info.plist"
cp "$TOPDIR/macos/Coma.icns" "$APP/Contents/Resources/Coma.icns"
cp "$TOPDIR/coma.sb" "$APP/Contents/Resources/coma.sb"

codesign --force --deep --sign - "$APP" || true
