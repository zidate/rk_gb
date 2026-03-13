#!/usr/bin/env bash
set -euo pipefail

REPO_DIR="${1:-${REPO_DIR:-}}"
if [ -z "${REPO_DIR}" ]; then
    echo "[issue-bot] REPO_DIR is required" >&2
    exit 2
fi

resolve_repo_sibling_dir() {
    local name="$1"
    local repo_parent
    repo_parent="$(cd "${REPO_DIR}/.." && pwd)"
    printf '%s/%s' "$repo_parent" "$name"
}

discover_cmake() {
    local tools_dir candidate
    tools_dir="$(resolve_repo_sibling_dir ".tools")"
    for candidate in "$tools_dir"/cmake-*/bin/cmake; do
        if [ -x "$candidate" ]; then
            printf '%s' "$candidate"
            return 0
        fi
    done
    return 1
}

discover_toolchain_bin() {
    local toolchain_bin
    toolchain_bin="$(resolve_repo_sibling_dir "RK/arm-rockchip830-linux-uclibcgnueabihf/bin")"
    if [ -d "$toolchain_bin" ]; then
        printf '%s' "$toolchain_bin"
        return 0
    fi
    return 1
}

if [ -z "${RK_TOOLCHAIN_BIN:-}" ]; then
    if detected_toolchain="$(discover_toolchain_bin)"; then
        export RK_TOOLCHAIN_BIN="$detected_toolchain"
    fi
fi

if [ -z "${RK_TOOLCHAIN_BIN:-}" ] || [ ! -d "${RK_TOOLCHAIN_BIN}" ]; then
    echo "[issue-bot] RK_TOOLCHAIN_BIN is not configured or does not exist" >&2
    exit 3
fi

if [ -n "${CMAKE_BIN:-}" ]; then
    CMAKE="${CMAKE_BIN}"
elif detected_cmake="$(discover_cmake)"; then
    CMAKE="${detected_cmake}"
elif command -v cmake >/dev/null 2>&1; then
    CMAKE="$(command -v cmake)"
else
    echo "[issue-bot] CMAKE_BIN is not configured and cmake is unavailable" >&2
    exit 4
fi

TOOLCHAIN_PREFIX="${TOOLCHAIN_PREFIX:-arm-rockchip830-linux-uclibcgnueabihf-}"
CC_BIN="${CC_BIN:-${TOOLCHAIN_PREFIX}gcc}"
CXX_BIN="${CXX_BIN:-${TOOLCHAIN_PREFIX}g++}"
BUILD_ROOT="${ISSUE_BOT_BUILD_ROOT:-$(mktemp -d "${TMPDIR:-/tmp}/rk_gb-build-verify.XXXXXX")}"
LOG_DIR="${BUILD_ROOT}/logs"
MID_BUILD_DIR="${BUILD_ROOT}/middleware"
APP_BUILD_DIR="${BUILD_ROOT}/app"

mkdir -p "${LOG_DIR}" "${MID_BUILD_DIR}" "${APP_BUILD_DIR}"

run_step() {
    local name="$1"
    shift
    local logfile="${LOG_DIR}/${name}.log"
    echo "[issue-bot] running ${name}, log=${logfile}"
    env PATH="${RK_TOOLCHAIN_BIN}:${PATH}" \
        CC="${CC_BIN}" \
        CXX="${CXX_BIN}" \
        "$@" >"${logfile}" 2>&1
}

run_step middleware-config \
    "${CMAKE}" \
    -S "${REPO_DIR}/Middleware" \
    -B "${MID_BUILD_DIR}" \
    -DRV1106_DUAL_IPC=ON \
    -DRC0240_LGV10=ON \
    -Drelease=ON \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DCMAKE_C_COMPILER="${CC_BIN}" \
    -DCMAKE_CXX_COMPILER="${CXX_BIN}"

run_step middleware-build \
    "${CMAKE}" \
    --build "${MID_BUILD_DIR}" \
    -j4

run_step app-config \
    "${CMAKE}" \
    -S "${REPO_DIR}" \
    -B "${APP_BUILD_DIR}" \
    -DRV1106_DUAL_IPC=ON \
    -DRC0240_LGV10=ON \
    -DAIC8800DL=ON \
    -Drelease=ON \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DCMAKE_C_COMPILER="${CC_BIN}" \
    -DCMAKE_CXX_COMPILER="${CXX_BIN}"

run_step app-build \
    "${CMAKE}" \
    --build "${APP_BUILD_DIR}" \
    -j4

echo "[issue-bot] build verify passed; logs=${LOG_DIR}"
