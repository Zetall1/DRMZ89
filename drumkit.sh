#!/bin/sh
# KNULLI ports launcher: kopieer deze map (drumkit inclusief drumkit.sh naar
# /roms/ports/ op je SD-kaart en ververs de gamelijst.
DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR"
if [ -x "$DIR/drumkit" ]; then
    exec "$DIR/drumkit"
fi
echo "drumkit binary niet gevonden - bouw eerst: make"
read -r _
