#!/usr/bin/env bash
set -euo pipefail

APP_DIR="/home/aibackend"
VENV_DIR="$APP_DIR/.venv"
LOG_DIR="$APP_DIR/logs"
PID_DIR="$APP_DIR/pids"
ENV_FILE="$APP_DIR/.env"

API_LOG="$LOG_DIR/api.log"
WORKER_LOG="$LOG_DIR/worker.log"
API_PID_FILE="$PID_DIR/api.pid"
WORKER_PID_FILE="$PID_DIR/worker.pid"
WORKER_CHILD_PID_FILE="$PID_DIR/worker_child.pid"

echo "[deploy] Start deploy at $(date -u)"

# ensure working dir
cd "$APP_DIR"

# create dirs
mkdir -p "$LOG_DIR"
mkdir -p "$PID_DIR"

# load env if exists
if [ -f "$ENV_FILE" ]; then
  # shellcheck disable=SC1090
  source "$ENV_FILE"
  echo "[deploy] Loaded env from $ENV_FILE"
fi

# create venv if missing
if [ ! -d "$VENV_DIR" ]; then
  echo "[deploy] Venv belum ada, buat baru..."
  python3 -m venv "$VENV_DIR"
fi

# activate venv
# shellcheck disable=SC1090
source "$VENV_DIR/bin/activate"

echo "[deploy] Upgrading pip..."
pip install --upgrade pip

echo "[deploy] Installing requirements..."
pip install -r requirements.txt

# helper to stop by pidfile
stop_pidfile() {
  local pidfile="$1"
  if [ -f "$pidfile" ]; then
    pid=$(cat "$pidfile" 2>/dev/null || true)
    if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
      echo "[deploy] Stopping pid $pid (file $pidfile)"
      kill "$pid" || true
      sleep 1
      # wait for process to exit
      for i in {1..10}; do
        if kill -0 "$pid" 2>/dev/null; then
          sleep 1
        else
          break
        fi
      done
      if kill -0 "$pid" 2>/dev/null; then
        echo "[deploy] Process $pid did not stop, forcing..."
        kill -9 "$pid" || true
      fi
    fi
    rm -f "$pidfile" || true
  fi

  # If we stopped the worker wrapper, also stop any running worker child
  if [ "$pidfile" = "$WORKER_PID_FILE" ]; then
    if [ -f "$WORKER_CHILD_PID_FILE" ]; then
      child_pid=$(cat "$WORKER_CHILD_PID_FILE" 2>/dev/null || true)
      if [ -n "$child_pid" ] && kill -0 "$child_pid" 2>/dev/null; then
        echo "[deploy] Stopping worker child pid $child_pid (file $WORKER_CHILD_PID_FILE)"
        kill "$child_pid" || true
        sleep 1
        for i in {1..10}; do
          if kill -0 "$child_pid" 2>/dev/null; then
            sleep 1
          else
            break
          fi
        done
        if kill -0 "$child_pid" 2>/dev/null; then
          echo "[deploy] Worker child $child_pid did not stop, forcing..."
          kill -9 "$child_pid" || true
        fi
      fi
      rm -f "$WORKER_CHILD_PID_FILE" || true
    fi
  fi
}

wait_for_worker_child() {
  echo "[deploy] Checking worker child health..."
  for i in {1..15}; do
    if [ -f "$WORKER_CHILD_PID_FILE" ]; then
      child_pid=$(cat "$WORKER_CHILD_PID_FILE" 2>/dev/null || true)
      if [ -n "$child_pid" ] && kill -0 "$child_pid" 2>/dev/null; then
        echo "[deploy] Worker child running (pid=$child_pid)"
        return 0
      fi
    fi
    echo "[deploy] Worker child not ready (attempt $i), waiting..."
    sleep 1
  done
  echo "[deploy] Worker child failed to start. Showing last 40 lines of worker log:"
  tail -n 40 "$WORKER_LOG" || true
  return 1
}

# stop old processes gracefully
echo "[deploy] Stopping old processes (if any)..."
# try pidfiles first
stop_pidfile "$API_PID_FILE"
stop_pidfile "$WORKER_PID_FILE"
# fallback to pkill patterns
pkill -f "uvicorn api.main:app" >/dev/null 2>&1 || true
pkill -f "python -u worker/worker.py" >/dev/null 2>&1 || true
pkill -f "python worker/worker.py" >/dev/null 2>&1 || true

# Start API (no restart loop — keep simple)
API_CMD="cd $APP_DIR && uvicorn api.main:app --host 0.0.0.0 --port 8000 --workers 1"
echo "[deploy] Starting API with: $API_CMD"
nohup bash -lc "$API_CMD" >> "$API_LOG" 2>&1 &
api_pid=$!
echo "$api_pid" > "$API_PID_FILE"
echo "[deploy] API started (pid=$api_pid)"

# Ensure Redis is available (try local port check, then start Docker container if needed)
ensure_redis() {
  echo "[deploy] Checking Redis availability on 127.0.0.1:6379..."
  if command -v nc >/dev/null 2>&1 && nc -z 127.0.0.1 6379 >/dev/null 2>&1; then
    echo "[deploy] Redis port 127.0.0.1:6379 is open"
    if command -v redis-cli >/dev/null 2>&1; then
      if redis-cli -h 127.0.0.1 -p 6379 ping >/dev/null 2>&1; then
        echo "[deploy] Redis responded to PING"
        return 0
      else
        echo "[deploy] redis-cli present but PING failed"
      fi
    else
      echo "[deploy] redis-cli not installed; assuming service listening on port"
      return 0
    fi
  fi

  # If Redis not available, try starting a Docker container
  if command -v docker >/dev/null 2>&1; then
    # If container already running, assume ok
    if docker ps --filter "name=redis-queue" --filter "status=running" --format '{{.Names}}' | grep -q '^redis-queue$'; then
      echo "[deploy] Docker container redis-queue already running"
      return 0
    fi

    echo "[deploy] Starting Redis via Docker (redis:7)..."
    docker run -d --name redis-queue --restart unless-stopped -p 127.0.0.1:6379:6379 redis:7 >/dev/null 2>&1 || {
      echo "[deploy] Failed to start docker redis container"
      return 1
    }

    # Wait for Redis to accept connections
    for i in {1..15}; do
      if command -v nc >/dev/null 2>&1 && nc -z 127.0.0.1 6379 >/dev/null 2>&1; then
        echo "[deploy] Redis is up (after docker start)"
        return 0
      fi
      echo "[deploy] Waiting for Redis to be ready... (attempt $i)"
      sleep 1
    done
    echo "[deploy] Redis did not become ready after docker start"
    return 1
  else
    echo "[deploy] docker not found; cannot start redis automatically"
    return 1
  fi
}

# Try to ensure Redis is available before starting the worker
if ! ensure_redis; then
  echo "[deploy] Redis unavailable and could not be started automatically. Aborting deploy."
  exit 1
fi

# Create a robust worker wrapper script that writes child pid and forwards signals
PID_FILE_DIR=$(dirname "$WORKER_PID_FILE")
cat > "$APP_DIR/worker_wrapper.sh" <<SH
#!/usr/bin/env bash
set -euo pipefail

PID_FILE="$WORKER_PID_FILE"
CHILD_PID_FILE="$WORKER_CHILD_PID_FILE"
PID_DIR="$PID_FILE_DIR"

# ensure pid files dir exists
mkdir -p "$PID_DIR"

echo "[worker-wrapper] starting wrapper"

terminate() {
  echo "[worker-wrapper] received termination signal, forwarding to child"
  if [ -f "\$CHILD_PID_FILE" ]; then
    cpid=$(cat "\$CHILD_PID_FILE" 2>/dev/null || true)
    if [ -n "\$cpid" ] && kill -0 "\$cpid" 2>/dev/null; then
      kill "\$cpid" || true
      sleep 1
      if kill -0 "\$cpid" 2>/dev/null; then
        kill -9 "\$cpid" || true
      fi
    fi
  fi
  exit 0
}

trap terminate SIGTERM SIGINT

while true; do
  # start child
  python -u worker/worker.py &
  child_pid=\$!
  echo "\$child_pid" > "\$CHILD_PID_FILE"
  echo "[worker-wrapper] spawned child pid \$child_pid"

  # wait for child to exit
  wait \$child_pid || true

  echo "[worker-wrapper] child \$child_pid exited, wrapper will restart after 2s"
  rm -f "\$CHILD_PID_FILE" || true
  sleep 2
done
SH

# make wrapper executable
chmod +x "$APP_DIR/worker_wrapper.sh"

# Start the wrapper under nohup and record its pid
nohup bash -lc "$APP_DIR/worker_wrapper.sh" >> "$WORKER_LOG" 2>&1 &
worker_loop_pid=$!
# Save pid of the wrapper
echo "$worker_loop_pid" > "$WORKER_PID_FILE"
echo "[deploy] Worker wrapper started (pid=$worker_loop_pid)"

# Ensure worker child actually boots
if ! wait_for_worker_child; then
  echo "[deploy] Worker health check failed"
  exit 1
fi

# Optional: warmup API
echo "[deploy] Warming up API (optional)..."
for i in {1..10}; do
  if curl -sS http://localhost:8000/health >/dev/null 2>&1; then
    echo "[deploy] API up (health check passed)."
    break
  else
    echo "[deploy] API not ready yet, retry $i..."
    sleep 1
  fi
done

# rotate logs (simple rotation to avoid unlimited growth)
rotate_logs() {
  local logfile="$1"
  local max_mb=20
  if [ -f "$logfile" ]; then
    size_kb=$(du -k "$logfile" | cut -f1)
    size_mb=$((size_kb / 1024))
    if [ "$size_mb" -ge "$max_mb" ]; then
      echo "[deploy] Rotating $logfile (size ${size_mb}MB >= ${max_mb}MB)"
      mv "$logfile" "${logfile}.1" || true
      touch "$logfile"
    fi
  fi
}
rotate_logs "$API_LOG"
rotate_logs "$WORKER_LOG"

echo "[deploy] Done at $(date -u)"
