#!/usr/bin/env bash
# Windows（MSYS2 / MinGW-w64）构建脚本：基础框架走 WSAPoll 后端
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

CXX="${CXX:-g++}"
mkdir -p build

# -static：把 libstdc++/libgcc/winpthread 静态链接进 exe，避免 PATH 中
# 旧 mingw64 运行库导致的 0xc0000139（entry point not found）
COMMON=(-std=c++17 -O2 -g -Wall -Wextra -Wno-unused-parameter -pthread -static -Isrc/include)

echo "[1/2] build server ..."
"$CXX" "${COMMON[@]}" src/*.cpp -lws2_32 -o build/mmo_server.exe

echo "[2/2] build bot_client ..."
"$CXX" "${COMMON[@]}" tools/bot_client.cpp \
  src/Protocol.cpp src/Messages.cpp src/util.cpp \
  -lws2_32 -o build/bot_client.exe

echo "OK: build/mmo_server.exe build/bot_client.exe"
