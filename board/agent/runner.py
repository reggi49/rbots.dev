import subprocess
import json
import yaml
import sys
import time
from pathlib import Path
import subprocess
import threading
import time
import json
import sys
import os
import signal
import re
import serial
import time
import json
import sys

# =========================
# Load config
# =========================
CONFIG_PATH = Path(__file__).parent / "config.yaml"

with open(CONFIG_PATH) as f:
    CONFIG = yaml.safe_load(f)

IDF_PATH = CONFIG["idf_path"]
PROJECT_PATH = CONFIG["project_path"]
SHELL = CONFIG.get("shell", "/bin/zsh")
DEFAULT_PORT = CONFIG.get("port")

EXPORT_SCRIPT = f"{IDF_PATH}/export.sh"

# =========================
# Action whitelist
# =========================
ACTIONS = {
    "build": "idf.py --no-hints build",
    "flash": "idf.py --no-hints flash",
    "flash_monitor": "idf.py --no-hints flash monitor",
    "clean": "idf.py --no-hints fullclean"
}

# =========================
# Helper: run command
# =========================
def _prepare_project_dirs():
    # Some ESP-IDF tooling expects build/log to exist.
    try:
        Path(PROJECT_PATH, "build", "log").mkdir(parents=True, exist_ok=True)
    except Exception:
        pass


def run_action(action: str, port: str | None = None):
    if action not in ACTIONS:
        return {
            "status": "rejected",
            "reason": f"Action '{action}' is not allowed"
        }

    cmd = ACTIONS[action]
    if action in ("flash", "flash_monitor") and (port or DEFAULT_PORT):
        cmd = f"idf.py --no-hints -p \"{port or DEFAULT_PORT}\" {action.replace('_', ' ')}"

    full_cmd = f"""
    source "{EXPORT_SCRIPT}" &&
    cd "{PROJECT_PATH}" &&
    {cmd}
    """

    try:
        _prepare_project_dirs()
        process = subprocess.run(
            full_cmd,
            shell=True,
            executable=SHELL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=1800  # 30 menit
        )

        stdout_lines = process.stdout.strip().splitlines()
        stderr_lines = process.stderr.strip().splitlines()

        result = {
            "status": "success" if process.returncode == 0 else "error",
            "action": action,
            "returncode": process.returncode,
            "stdout_tail": stdout_lines[-10:],
            "stderr_tail": stderr_lines[-10:]
        }
        stderr_text = process.stderr or ""
        if (
            process.returncode != 0
            and "FileNotFoundError" in stderr_text
            and "idf_py_stderr_output_" in stderr_text
            and "/build/log/" in stderr_text
        ):
            result["status"] = "runner_error"
            result["reason"] = "ESP-IDF log FileNotFoundError (runner/tooling issue)"

        return result

    except subprocess.TimeoutExpired:
        return {
            "status": "timeout",
            "action": action
        }

    except Exception as e:
        return {
            "status": "exception",
            "error": str(e)
        }
    
def run_monitor_with_timeout(port, duration_sec):
    cmd = [
        "idf.py",
        "-p", port,
        "monitor"
    ]

    process = subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1,
        universal_newlines=True,
        preexec_fn=os.setsid  # important: allow kill whole process group
    )

    log_lines = []
    flags = {
        "panic_detected": False,
        "assert_detected": False,
        "reboot_detected": False
    }

    start_time = time.time()

    try:
        for line in process.stdout:
            line = line.rstrip()
            log_lines.append(line)

            # --- lightweight pattern detection (NO reasoning) ---
            if "assert failed" in line:
                flags["assert_detected"] = True
            if "panic_abort" in line or "Stack dump detected" in line:
                flags["panic_detected"] = True
            if "Rebooting" in line:
                flags["reboot_detected"] = True

            # --- timeout check ---
            if time.time() - start_time >= duration_sec:
                break

    finally:
        # HARD TERMINATE monitor (required)
        try:
            os.killpg(os.getpgid(process.pid), signal.SIGTERM)
        except Exception:
            pass

    return {
        "status": "ok",
        "duration_sec": duration_sec,
        "log_lines": log_lines[-200:],  # keep tail only
        "summary": flags
    }

def serial_read_with_timeout(port, baudrate=115200, duration_sec=15):
    ser = None
    log_lines = []

    summary = {
        "panic_detected": False,
        "assert_detected": False,
        "fault_detected": False,
        "running_detected": False
    }

    try:
        ser = serial.Serial(
            port=port,
            baudrate=baudrate,
            timeout=0.1
        )

        start_time = time.time()

        while time.time() - start_time < duration_sec:
            try:
                line = ser.readline()
                if not line:
                    continue

                decoded = line.decode(errors="ignore").strip()
                if not decoded:
                    continue

                log_lines.append(decoded)

                # --- lightweight detection (NO reasoning) ---
                if "assert failed" in decoded:
                    summary["assert_detected"] = True
                if (
                    "panic" in decoded.lower()
                    or "guru meditation error" in decoded.lower()
                    or "abort() was called" in decoded.lower()
                    or "backtrace:" in decoded.lower()
                    or "stack memory:" in decoded.lower()
                    or "register dump:" in decoded.lower()
                ):
                    summary["panic_detected"] = True
                if "FAULT:" in decoded:
                    summary["fault_detected"] = True
                if "STATE: RUNNING" in decoded:
                    summary["running_detected"] = True

            except Exception:
                # ignore line-level read errors
                pass

    finally:
        if ser and ser.is_open:
            ser.close()

    return {
        "status": "ok",
        "port": port,
        "baudrate": baudrate,
        "duration_sec": duration_sec,
        "log_lines": log_lines[-200:],  # tail only
        "summary": summary
    }
# =========================
# Entry point
# =========================
def main():
    if len(sys.argv) < 2:
        print(json.dumps({
            "status": "error",
            "reason": "no action provided"
        }))
        return

    try:
        action = json.loads(sys.argv[1])
    except json.JSONDecodeError as e:
        print(json.dumps({
            "status": "error",
            "reason": f"invalid JSON action: {e}"
        }))
        return

    if not isinstance(action, dict):
        print(json.dumps({
            "status": "error",
            "reason": "expected JSON object for action"
        }))
        return

    if action["action"] == "serial_read_with_timeout":
        port = action["port"]
        baudrate = action.get("baudrate", 115200)
        duration = action.get("duration_sec", 15)

        result = serial_read_with_timeout(
            port=port,
            baudrate=baudrate,
            duration_sec=duration
        )

        print(json.dumps(result, indent=2))
        return

    if action["action"] in ACTIONS:
        result = run_action(action["action"], port=action.get("port"))
        print(json.dumps(result, indent=2))
        return

    # fallback
    print(json.dumps({
        "status": "error",
        "reason": f"unknown action {action.get('action')}"
    }))


if __name__ == "__main__":
    main()
