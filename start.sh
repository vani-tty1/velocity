#!/usr/bin/env bash
set -euo pipefail

#Feel free to edit this to your liking :)
rm -rf build
meson setup build -Dprofile="development"
meson compile -C build
glib-compile-schemas --targetdir=build data
export GSETTINGS_SCHEMA_DIR="$PWD/build"


./build-aux/mock-librespeed.py &
SERVER_PID=$!
# uncomment the line below to use the mock server
# then comment out the blank one:
# SERVER_URL="http://127.0.0.1:8000/"
SERVER_URL=""

trap 'kill "$SERVER_PID" 2>/dev/null || true' EXIT INT TERM

G_MESSAGES_DEBUG=all VELOCITY_SERVER_URL="$SERVER_URL" VELOCITY_SERVER_TYPE="${VELOCITY_SERVER_TYPE:-librespeed}" ./build/src/velocity "$@"
