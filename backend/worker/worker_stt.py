import json
import os
import sys
import time
import redis
from pathlib import Path
from pathlib import Path as _Path

if __package__ is None or __package__ == "":
    repo_root = _Path(__file__).resolve().parent.parent
    if str(repo_root) not in sys.path:
        sys.path.insert(0, str(repo_root))
    if str(repo_root / "api") not in sys.path:
        sys.path.insert(0, str(repo_root / "api"))
    from worker.settings import settings  # type: ignore
    from api.services.azure_stt import azure_stt_transcribe  # type: ignore
else:
    from .settings import settings
    from api.services.azure_stt import azure_stt_transcribe

QUEUE_STT = os.getenv("QUEUE_STT", settings.queue_stt)
AZURE_SPEECH_KEY = (os.getenv("AZURE_SPEECH_KEY") or "").strip()
AZURE_SPEECH_REGION = (os.getenv("AZURE_SPEECH_REGION") or "").strip()


def get_redis_client():
    return redis.Redis(host=settings.redis_host, port=settings.redis_port, db=0)


def worker_loop_stt():
    if not AZURE_SPEECH_KEY or not AZURE_SPEECH_REGION:
        raise RuntimeError("AZURE_SPEECH_KEY/REGION not set for STT worker")

    r = get_redis_client()
    print(f"[STT Worker] connected to redis {settings.redis_host}:{settings.redis_port}, queue={QUEUE_STT}")

    while True:
        item = r.blpop(QUEUE_STT, timeout=5)
        if not item:
            continue

        _, raw = item
        try:
            job = json.loads(raw.decode("utf-8"))
        except Exception as e:
            print("[STT Worker] bad job json", e, raw)
            continue

        job_id = job.get("job_id")
        audio_path = job.get("audio_path")
        language = job.get("language", "id-ID")
        result_key = f"result:stt:{job_id}"

        if not job_id or not audio_path:
            print("[STT Worker] missing job_id/audio_path", job)
            continue

        start_ms = int(time.time() * 1000)
        try:
            text = azure_stt_transcribe(audio_path, language=language)
            payload = {"status": "success", "text": text, "job_id": job_id}
        except Exception as e:
            payload = {"status": "error", "error": str(e), "job_id": job_id}
            print(f"[STT Worker] job {job_id} failed: {e}")
        finally:
            try:
                Path(audio_path).unlink(missing_ok=True)
            except Exception:
                pass

        payload["latency_ms"] = int(time.time() * 1000) - start_ms
        try:
            r.lpush(result_key, json.dumps(payload))
        except Exception as e:
            print(f"[STT Worker] failed to push result for {job_id}: {e}")


if __name__ == "__main__":
    worker_loop_stt()
