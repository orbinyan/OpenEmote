#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

BUILD_DIRS=(
  "${OPENEMOTE_BUILD_DIR:-}"
  "${ROOT}/build/release"
  "${ROOT}/build/debug"
  "${ROOT}/build"
  "${ROOT}/build-crashpad"
)

find_binary() {
  local dir
  local openemote_bin
  local chatterino_bin

  for dir in "${BUILD_DIRS[@]}"; do
    [[ -n "${dir}" ]] || continue

    openemote_bin="${dir}/bin/openemote"
    chatterino_bin="${dir}/bin/chatterino"

    if [[ -x "${openemote_bin}" && -x "${chatterino_bin}" ]]; then
      if [[ "${chatterino_bin}" -nt "${openemote_bin}" ]]; then
        printf '%s\n' "${chatterino_bin}"
      else
        printf '%s\n' "${openemote_bin}"
      fi
      return 0
    fi

    if [[ -x "${openemote_bin}" ]]; then
      printf '%s\n' "${openemote_bin}"
      return 0
    fi

    if [[ -x "${chatterino_bin}" ]]; then
      printf '%s\n' "${chatterino_bin}"
      return 0
    fi
  done

  return 1
}

find_test_binary() {
  local dir
  local test_bin

  for dir in "${BUILD_DIRS[@]}"; do
    [[ -n "${dir}" ]] || continue
    test_bin="${dir}/bin/chatterino-test"
    if [[ -x "${test_bin}" ]]; then
      printf '%s\n' "${test_bin}"
      return 0
    fi
  done

  return 1
}

build_cmd() {
  if command -v nix >/dev/null 2>&1; then
    nix --extra-experimental-features 'nix-command flakes' develop -c cmake --preset release
    nix --extra-experimental-features 'nix-command flakes' develop -c cmake --build --preset release -j"$(nproc)"
    return 0
  fi
  cmake --preset release
  cmake --build --preset release -j"$(nproc)"
}

cmd="${1:-run}"
if [[ $# -gt 0 ]]; then
  shift
fi

case "${cmd}" in
  build)
    build_cmd
    ;;
  run)
    BIN_PATH="$(find_binary || true)"
    if [[ -z "${BIN_PATH}" ]]; then
      echo "[openemote] missing binary" >&2
      echo "Run scripts/openemote-dev.sh build first." >&2
      exit 1
    fi
    if [[ "${OPENEMOTE_VERBOSE:-0}" != "0" ]]; then
      repo_branch="$(git -C "${ROOT}" rev-parse --abbrev-ref HEAD 2>/dev/null || echo unknown)"
      repo_rev="$(git -C "${ROOT}" rev-parse --short HEAD 2>/dev/null || echo unknown)"
      echo "[openemote] repo: ${ROOT} (${repo_branch}/${repo_rev})"
      echo "[openemote] exec path: ${BIN_PATH}"
      echo "[openemote] version: $(${BIN_PATH} --version 2>/dev/null || echo unknown)"
    fi
    exec "${BIN_PATH}" "$@"
    ;;
  test)
    TEST_PATH="$(find_test_binary || true)"
    if [[ -z "${TEST_PATH}" ]]; then
      echo "[openemote] missing test binary (chatterino-test)" >&2
      echo "Run scripts/openemote-dev.sh build first." >&2
      exit 1
    fi
    exec "${TEST_PATH}" "$@"
    ;;
  env)
    printf 'ROOT=%s\n' "${ROOT}"
    printf 'BUILD_DIRS=%s\n' "${BUILD_DIRS[*]}"
    ;;
  *)
    cat <<'USAGE'
Usage:
  scripts/openemote-dev.sh build
  scripts/openemote-dev.sh run [args...]
  scripts/openemote-dev.sh test [gtest args...]
  scripts/openemote-dev.sh env
USAGE
    exit 2
    ;;
esac

