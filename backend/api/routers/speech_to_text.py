import json
import uuid
import tempfile
from pathlib import Path

from fastapi import APIRouter, HTTPException, Request

from ..config import r, QUEUE_STT, STT_RESULT_TIMEOUT_SEC

router = APIRouter()


@router.post("/speech-to-text")
async def speech_to_text(request: Request, language: str = "id-ID"):
    """
    Accept short WAV/PCM (16 kHz mono) audio, enqueue to Redis, wait for worker result.
    """
    ctype = request.headers.get("content-type", "")
    expected = "audio/wav"
    if expected not in ctype:
        raise HTTPException(
            status_code=400,
            detail=f"Content-Type harus {expected}; codecs=audio/pcm; samplerate=16000",
        )

    body = await request.body()
    if not body:
        raise HTTPException(status_code=400, detail="Audio kosong")

    job_id = uuid.uuid4().hex
    tmp_dir = Path(tempfile.gettempdir())
    audio_path = tmp_dir / f"stt-{job_id}.wav"
    try:
        audio_path.write_bytes(body)
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Gagal simpan audio: {e}")

    job = {
        "job_id": job_id,
        "audio_path": str(audio_path),
        "language": language or "id-ID",
    }

    try:
        r.rpush(QUEUE_STT, json.dumps(job))
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Gagal push job: {e}")

    try:
        res = r.blpop(f"result:stt:{job_id}", timeout=STT_RESULT_TIMEOUT_SEC)
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Gagal baca hasil: {e}")

    if res is None:
        raise HTTPException(status_code=504, detail="Worker tidak merespons dalam batas waktu")

    try:
        payload = json.loads(res[1].decode("utf-8"))
    except Exception:
        raise HTTPException(status_code=500, detail="Hasil worker tidak valid")

    if payload.get("status") != "success":
        err = payload.get("error", "unknown")
        raise HTTPException(status_code=500, detail=err)

    return {
        "status": "success",
        "job_id": job_id,
        "text": payload.get("text", ""),
    }
