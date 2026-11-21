#!/usr/bin/env bash
# Chroma ColorPicker · macOS 编译并启动
#
# 用法：
#   ./launcher/build-and-run.sh              # 编译 + 启动
#   ./launcher/build-and-run.sh --build-only # 只编译
#   ./launcher/build-and-run.sh --help

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${CHROMA_COLORPICKER_BUILD_DIR:-${PROJECT_DIR}/build-macos}"

print_help() {
    cat <<'EOF'
用法:
  ./launcher/build-and-run.sh
  ./launcher/build-and-run.sh --build-only
  ./launcher/build-and-run.sh --help

环境变量:
  QT_ROOT                        Qt6 安装前缀（可选）
  CHROMA_COLORPICKER_BUILD_DIR   构建目录（默认 <project>/build-macos）
EOF
}

MODE="run"
if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
    print_help
    exit 0
elif [[ "${1:-}" == "--build-only" ]]; then
    MODE="build-only"
fi

if [[ -n "${CMAKE_PREFIX_PATH:-}" ]]; then
    QT_PREFIX="${CMAKE_PREFIX_PATH}"
elif [[ -n "${QT_ROOT:-}" ]]; then
    QT_PREFIX="${QT_ROOT}"
elif command -v brew >/dev/null 2>&1; then
    QT_PREFIX="$(brew --prefix qt 2>/dev/null || brew --prefix qtbase 2>/dev/null || true)"
else
    QT_PREFIX=""
fi

if [[ -z "${QT_PREFIX}" ]]; then
    echo "找不到 Qt6。请设置 QT_ROOT，例如："
    echo "  export QT_ROOT=\$(brew --prefix qtbase)"
    exit 1
fi

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "该启动脚本仅适用于 macOS。"
    exit 1
fi

echo "[launcher] project : ${PROJECT_DIR}"
echo "[launcher] build   : ${BUILD_DIR}"
echo "[launcher] Qt      : ${QT_PREFIX}"

cmake -S "${PROJECT_DIR}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="${QT_PREFIX}"

cmake --build "${BUILD_DIR}" --config Release

APP="${BUILD_DIR}/colorpicker.app/Contents/MacOS/colorpicker"
if [[ ! -x "${APP}" ]]; then
    echo "[launcher] 未找到可执行文件: ${APP}"
    exit 1
fi

if [[ "${MODE}" == "build-only" ]]; then
    echo "[launcher] 编译完成: ${APP}"
    exit 0
fi

echo "[launcher] 启动 ${APP}"
exec "${APP}"
