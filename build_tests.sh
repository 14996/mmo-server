#!/usr/bin/env bash
# 构建并运行核心单元测试（不需要启动服务器）
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

CXX="${CXX:-g++}"
mkdir -p build

EXE=""
STATIC=()
case "$(uname -s)" in
  MINGW* | MSYS* | CYGWIN*)
    EXE=".exe"
    # Windows 下静态链接，避免 PATH 中旧 libstdc++ 导致启动崩溃
    STATIC=(-static)
    ;;
esac

COMMON=(-std=c++17 -O2 -g -Wall -Wextra -Wno-unused-parameter -pthread
  "${STATIC[@]}" -Isrc/include)

echo "[1/2] build test_core ..."
"$CXX" "${COMMON[@]}" \
  tests/test_core.cpp src/Buffer.cpp src/Protocol.cpp src/ServerConfig.cpp \
  -o "build/test_core${EXE}"

echo "[2/2] run test_core ..."
"./build/test_core${EXE}"
