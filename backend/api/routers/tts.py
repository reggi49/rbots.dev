import os
import uuid
from pydantic import BaseModel, Field
from fastapi import APIRouter, Response, HTTPException

from ..config import logger
from ..services.azure_tts import azure_tts, ensure_azure_env, DEFAULT_OUTPUT_FORMAT, DEFAULT_VOICE

router = APIRouter(prefix="/tts", tags=["tts"])


class TtsAzureRequest(BaseModel):
    text: str = Field(..., min_length=1)
    voice: str | None = None
    format: str | None = None


@router.post("/azure")
def tts_azure(payload: TtsAzureRequest):
    request_id = uuid.uuid4().hex
    text = payload.text.strip()
    if not text:
        raise HTTPException(status_code=400, detail="text required")
    if len(text) > 500:
        raise HTTPException(status_code=400, detail="text terlalu panjang (maks 500 karakter)")

    ensure_azure_env()
    if not os.getenv("AZURE_SPEECH_KEY") or not os.getenv("AZURE_SPEECH_REGION"):
        raise HTTPException(status_code=500, detail="Azure TTS not configured")

    output_format = payload.format or DEFAULT_OUTPUT_FORMAT
    if output_format != DEFAULT_OUTPUT_FORMAT:
        raise HTTPException(status_code=400, detail=f"format harus '{DEFAULT_OUTPUT_FORMAT}'")

    voice = payload.voice or DEFAULT_VOICE

    logger.info(
        "tts_request",
        extra={
            "request_id": request_id,
            "text_len": len(text),
            "voice": voice,
            "format": output_format,
        },
    )

    try:
        audio = azure_tts(
            text=text,
            voice=voice,
            output_format=output_format,
            timeout_sec=20,
            request_id=request_id,
        )
    except Exception as e:
        raise HTTPException(status_code=502, detail=str(e))

    if os.getenv("AZURE_TTS_DEBUG_SAVE") == "1":
        try:
            with open("/tmp/tts_last.wav", "wb") as f:
                f.write(audio)
        except Exception:
            pass

    return Response(
        content=audio,
        media_type="audio/wav",
        headers={"X-Request-Id": request_id},
    )