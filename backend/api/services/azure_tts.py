import os
import time
import requests
from xml.sax.saxutils import escape

from ..config import logger

SPEECH_KEY = (os.getenv("AZURE_SPEECH_KEY") or "").strip()
SPEECH_REGION = (os.getenv("AZURE_SPEECH_REGION") or "").strip()
DEFAULT_OUTPUT_FORMAT = "riff-16khz-16bit-mono-pcm"
DEFAULT_VOICE = "id-ID-ArdiNeural"

_ENV_CHECKED = False


def ensure_azure_env() -> None:
    global _ENV_CHECKED
    if _ENV_CHECKED:
        return
    _ENV_CHECKED = True
    if not SPEECH_KEY or not SPEECH_REGION:
        logger.warning(
            "azure_tts_env_missing",
            extra={"has_key": bool(SPEECH_KEY), "has_region": bool(SPEECH_REGION)},
        )


def azure_tts(
    text: str,
    voice: str = DEFAULT_VOICE,
    output_format: str = DEFAULT_OUTPUT_FORMAT,
    timeout_sec: int = 20,
    request_id: str | None = None,
) -> bytes:
    if not text:
        raise ValueError("Text empty")

    ensure_azure_env()
    if not SPEECH_KEY or not SPEECH_REGION:
        raise RuntimeError("Azure env not loaded")

    url = f"https://{SPEECH_REGION}.tts.speech.microsoft.com/cognitiveservices/v1"

    headers = {
        "Ocp-Apim-Subscription-Key": SPEECH_KEY,
        "Ocp-Apim-Subscription-Region": SPEECH_REGION,
        "Content-Type": "application/ssml+xml",
        "X-Microsoft-OutputFormat": output_format,
        "User-Agent": "rbots-backend"
    }

    safe_text = escape(text)
    ssml = f"""
    <speak version='1.0' xml:lang='id-ID'>
      <voice name='{voice}'>
        {safe_text}
      </voice>
    </speak>
    """

    start_time = time.monotonic()
    r = requests.post(
        url,
        headers=headers,
        data=ssml.encode("utf-8"),
        timeout=timeout_sec,
    )
    latency_ms = int((time.monotonic() - start_time) * 1000)

    if r.status_code != 200:
        err_text = r.text[:500]
        logger.error(
            "azure_tts_failed",
            extra={
                "request_id": request_id,
                "status": r.status_code,
                "body": err_text,
                "headers": dict(r.headers),
                "latency_ms": latency_ms,
            },
        )
        raise RuntimeError(f"Azure TTS error: {r.status_code} {err_text}")

    if not r.content.startswith(b"RIFF"):
        snippet = r.content[:40]
        logger.error(
            "azure_tts_non_wav",
            extra={
                "request_id": request_id,
                "status": r.status_code,
                "body_prefix": snippet,
                "latency_ms": latency_ms,
            },
        )
        raise RuntimeError("Azure response is not WAV")

    logger.info(
        "azure_tts_ok",
        extra={
            "request_id": request_id,
            "voice": voice,
            "format": output_format,
            "latency_ms": latency_ms,
            "bytes": len(r.content),
        },
    )

    return r.content