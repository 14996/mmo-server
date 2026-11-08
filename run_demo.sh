#!/usr/bin/env bash
# 一键联调/压测演示：启动服务器（自动退出）+ 一批机器人，打印汇总指标
# 用法: ./run_demo.sh [bot数量=20] [运行秒数=30] [端口=9100]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

BOTS="${1:-20}"
DURATION="${2:-30}"
PORT="${3:-9100}"

case "$(uname -s)" in
  MINGW*|MSYS*)
    SERVER=build/mmo_server.exe
    BOT=build/bot_client.exe
    BUILD=./build_win.sh
    ;;
  *)
    SERVER=build/mmo_server
    BOT=build/bot_client
    BUILD=./build.sh
    ;;
esac

if [ ! -x "$SERVER" ] || [ ! -x "$BOT" ]; then
  "$BUILD"
fi

mkdir -p build data
rm -rf data/*
: > build/server.log
: > build/bot.log

SERVER_SECONDS=$((DURATION + 10))
echo "[demo] server port=$PORT bots=$BOTS duration=${DURATION}s"

"$SERVER" --port "$PORT" --run-seconds "$SERVER_SECONDS" > build/server.log 2>&1 &
SERVER_PID=$!
sleep 1

set +e
"$BOT" --host 127.0.0.1 --port "$PORT" --bots "$BOTS" --duration "$DURATION" \
  > >(tee build/bot.log) 2>&1
BOT_EXIT=$?
set -e

set +e
wait "$SERVER_PID"
SERVER_EXIT=$?
set -e

echo
echo "[demo] bot_exit=$BOT_EXIT server_exit=$SERVER_EXIT"
echo "[demo] server log (tail):"
tail -n 20 build/server.log
echo "[demo] persisted characters: $(wc -l < data/characters.dat)"
head -n 3 data/characters.dat
