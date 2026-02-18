#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PORT="${OPENEMOTE_MOCK_PORT:-18080}"
HOST="${OPENEMOTE_MOCK_HOST:-127.0.0.1}"
MOCK_PID=""

cleanup() {
  if [[ -n "${MOCK_PID}" ]] && kill -0 "${MOCK_PID}" 2>/dev/null; then
    kill "${MOCK_PID}" || true
  fi
}
trap cleanup EXIT

cd "${ROOT}"

echo "[openemote] starting mock provider at http://${HOST}:${PORT}"
OPENEMOTE_MOCK_HOST="${HOST}" OPENEMOTE_MOCK_PORT="${PORT}" \
  python3 scripts/openemote_mock_server.py &
MOCK_PID="$!"

echo "[openemote] configuring + building release preset"
nix --extra-experimental-features 'nix-command flakes' develop -c cmake --preset release
nix --extra-experimental-features 'nix-command flakes' develop -c cmake --build --preset release -j"$(nproc)"

echo
echo "[openemote] build done: ${ROOT}/build/release/bin/openemote"
echo "[openemote] launch command:"
echo "  nix --extra-experimental-features 'nix-command flakes' develop -c ./build/release/bin/openemote"
echo
echo "[openemote] in OpenEmote set:"
echo "  Settings -> General -> Custom provider (open path)"
echo "  API base URLs: http://${HOST}:${PORT}"
echo "  Enable global + channel custom provider toggles"
echo "  Whispers emote context channel: <an active Twitch channel>"
echo
echo "[openemote] smoke message checks:"
echo "  OPENHYPE (global), OPENWAVE (channel), OPENWAVE in whispers"
echo
echo "[openemote] credential safety checks:"
echo "  - keep 'Allow insecure plaintext credential storage' OFF by default"
echo "  - if turned ON, warning should be emitted to logs"
