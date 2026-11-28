#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

# USE_MOCK=0 ./start.sh tests against real servers
USE_MOCK="${USE_MOCK:-1}"
SERVER_PID=""

build_app() {
  if [[ -d build ]]; then
    meson setup build -Dprofile=development --reconfigure
  else
    meson setup build -Dprofile=development
  fi
  meson compile -C build
  glib-compile-schemas --targetdir=build data
}

stop_mock_server() {
  [[ -n "$SERVER_PID" ]] && kill "$SERVER_PID" 2>/dev/null || true
}

find_free_port() {
  python3 -c 'import socket; s = socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1])'
}

wait_for_port() {
  for _ in {1..50}; do
    (exec 3<>"/dev/tcp/127.0.0.1/$1") 2>/dev/null && return 0
    sleep 0.1
  done
  echo "mock server did not start" >&2
  return 1
}

start_mock_server() {
  local port
  port="$(find_free_port)"
  ./build-aux/mock-librespeed.py "$port" &
  SERVER_PID=$!
  wait_for_port "$port"
  export VELOCITY_SERVER_URL="http://127.0.0.1:$port/"
}

trap stop_mock_server EXIT INT TERM

build_app
export GSETTINGS_SCHEMA_DIR="$PWD/build"

if [[ "$USE_MOCK" == "1" ]]; then
  start_mock_server
fi

./build/src/velocity "$@"