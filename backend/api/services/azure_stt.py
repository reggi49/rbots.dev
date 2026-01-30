import os
import time
import requests

AZURE_SPEECH_KEY = (os.getenv("AZURE_SPEECH_KEY") or "").strip()
AZURE_SPEECH_REGION = (os.getenv("AZURE_SPEECH_REGION") or "").strip()


def azure_stt_transcribe(audio_path: str, language: str = "id-ID", timeout_sec: int = 20) -> str:
    if not AZURE_SPEECH_KEY or not AZURE_SPEECH_REGION:
        raise RuntimeError("AZURE_SPEECH_KEY/REGION not set")
    if not audio_path:
        raise ValueError("audio_path empty")

    url = (
        f"https://{AZURE_SPEECH_REGION}.stt.speech.microsoft.com/"
        f"speech/recognition/conversation/cognitiveservices/v1?language={language}"
    )

    headers = {
        "Ocp-Apim-Subscription-Key": AZURE_SPEECH_KEY,
        "Ocp-Apim-Subscription-Region": AZURE_SPEECH_REGION,
        "Content-Type": "audio/wav; codecs=audio/pcm; samplerate=16000",
        "Accept": "application/json",
    }

    with open(audio_path, "rb") as f:
        start = time.monotonic()
        resp = requests.post(url, headers=headers, data=f, timeout=timeout_sec)
        latency_ms = int((time.monotonic() - start) * 1000)

    if resp.status_code != 200:
        raise RuntimeError(f"Azure STT error {resp.status_code}: {resp.text[:200]}")

    data = resp.json()
    status = data.get("RecognitionStatus")
    if status != "Success":
        raise RuntimeError(f"Azure STT status={status}")

    text = data.get("DisplayText", "")
    return text or ""
