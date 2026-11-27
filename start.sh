#!/usr/bin/env bash
set -euo pipefail

#Feel free to edit this to your liking :)
meson setup build -Dprofile="development" --reconfigure
meson compile -C build
glib-compile-schemas --targetdir=build data
export GSETTINGS_SCHEMA_DIR="$PWD/build"


./build-aux/mock-librespeed.py &
SERVER_PID=$!
SERVER_URL="http://127.0.0.1:8000/"

trap 'kill "$SERVER_PID" 2>/dev/null || true' EXIT INT TERM

VELOCITY_SERVER_URL="$SERVER_URL" ./build/src/velocity "$@"
