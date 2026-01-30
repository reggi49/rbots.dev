#!/usr/bin/env bash
set -eo pipefail

PID_FILE="/home/aibackend/pids/worker.pid"
CHILD_PID_FILE="/home/aibackend/pids/worker_child.pid"

mkdir -p "$(dirname "$PID_FILE")"

echo "[worker-wrapper] starting wrapper"

echo "$$" > "$PID_FILE"

terminate() {
  echo "[worker-wrapper] received termination signal, forwarding to child"
  if [ -f "$CHILD_PID_FILE" ]; then
    cpid=$(cat "$CHILD_PID_FILE" 2>/dev/null || true)
    if [ -n "$cpid" ] && kill -0 "$cpid" 2>/dev/null; then
      kill "$cpid" || true
      sleep 1
      if kill -0 "$cpid" 2>/dev/null; then
        kill -9 "$cpid" || true
      fi
    fi
  fi
  rm -f "$CHILD_PID_FILE" "$PID_FILE" || true
  exit 0
}

trap terminate SIGTERM SIGINT

while true; do
  python -u worker/worker.py &
  child_pid=$!
  echo "$child_pid" > "$CHILD_PID_FILE"
  echo "[worker-wrapper] spawned child pid $child_pid"

  wait $child_pid || true

  echo "[worker-wrapper] child $child_pid exited, restarting in 2s"
  rm -f "$CHILD_PID_FILE" || true
  sleep 2

done
