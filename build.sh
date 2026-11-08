#!/usr/bin/env bash
# Linux 构建脚本：基础框架走 epoll 后端
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

CXX="${CXX:-g++}"
mkdir -p build

COMMON=(-std=c++17 -O2 -g -Wall -Wextra -Wno-unused-parameter -pthread -Isrc/include)

echo "[1/2] build server ..."
"$CXX" "${COMMON[@]}" src/*.cpp -o build/mmo_server

echo "[2/2] build bot_client ..."
"$CXX" "${COMMON[@]}" tools/bot_client.cpp \
  src/Protocol.cpp src/Messages.cpp src/util.cpp \
  -o build/bot_client

echo "OK: build/mmo_server build/bot_client"
