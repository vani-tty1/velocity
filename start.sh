#!/usr/bin/env bash
set -euo pipefail

#Feel free to edit this to your liking :)
rm -rf build
meson setup build -Dprofile="development"
meson compile -C build


./build-aux/mock-librespeed.py &
SERVER_PID=$!
# uncomment the line below to use the mock server
# then comment out the blank one:
# SERVER_URL="http://127.0.0.1:8000/"
SERVER_URL=""

trap 'kill "$SERVER_PID" 2>/dev/null || true' EXIT INT TERM

VELOCITY_SERVER_URL="$SERVER_URL" ./build/src/velocity "$@"
