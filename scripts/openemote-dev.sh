#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
active_repo="/home/jack/src/golden_eye/.overmind/external/chatterino-openemote"

if [[ "${OPENEMOTE_LANE_QUARANTINE_BYPASS:-0}" != "1" ]]; then
  cat <<EOF >&2
[openemote-lane] Quarantined worktree blocked.
[openemote-lane] This path is disabled to prevent stale launches:
  ${repo_root}
[openemote-lane] Use active path instead:
  ${active_repo}
[openemote-lane] To bypass intentionally (emergency only), set:
  OPENEMOTE_LANE_QUARANTINE_BYPASS=1
EOF
  exit 42
fi

build_dir="${repo_root}/build-crashpad"
cache_file="${build_dir}/CMakeCache.txt"

if [[ ! -f "${cache_file}" ]]; then
  echo "Missing CMake cache: ${cache_file}" >&2
  exit 1
fi

cache_path() {
  local key="$1"
  sed -n "s#^${key}:PATH=##p" "${cache_file}" | head -n 1
}

cache_internal() {
  local key="$1"
  sed -n "s#^${key}:INTERNAL=##p" "${cache_file}" | head -n 1
}

qt_dir="$(cache_path Qt6_DIR)"
boost_headers_dir="$(cache_path boost_headers_DIR)"
curl_include="$(cache_path CURL_INCLUDE_DIR)"
openssl_include="$(cache_path OPENSSL_INCLUDE_DIR)"
cmake_bin="$(cache_internal CMAKE_COMMAND)"

qt_prefix="${qt_dir%/lib/cmake/Qt6}"
boost_prefix="${boost_headers_dir%/lib/cmake/boost_headers-1.81.0}"
extra_include="${qt_prefix}/include:${boost_prefix}/include:${curl_include}:${openssl_include}"

export CPLUS_INCLUDE_PATH="${extra_include}${CPLUS_INCLUDE_PATH:+:${CPLUS_INCLUDE_PATH}}"
export C_INCLUDE_PATH="${extra_include}${C_INCLUDE_PATH:+:${C_INCLUDE_PATH}}"

cmd="${1:-}"
shift || true

case "${cmd}" in
  build)
    targets=("$@")
    if [[ ${#targets[@]} -eq 0 ]]; then
      targets=(chatterino)
    fi
    exec "${cmake_bin}" --build "${build_dir}" --target "${targets[@]}" -j8
    ;;
  run)
    exec "${build_dir}/bin/chatterino" "$@"
    ;;
  test)
    exec "${build_dir}/bin/chatterino-test" "$@"
    ;;
  env)
    printf 'CMAKE=%s\n' "${cmake_bin}"
    printf 'CPLUS_INCLUDE_PATH=%s\n' "${CPLUS_INCLUDE_PATH}"
    printf 'C_INCLUDE_PATH=%s\n' "${C_INCLUDE_PATH}"
    ;;
  *)
    cat <<'USAGE'
Usage:
  scripts/openemote-dev.sh build [target...]
  scripts/openemote-dev.sh run [args...]
  scripts/openemote-dev.sh test [gtest args...]
  scripts/openemote-dev.sh env
USAGE
    exit 2
    ;;
esac
