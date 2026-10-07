"""
RBOT MVP Conversational Backend
Pipeline: Audio In (WAV/PCM) -> STT (Azure / Gemini Fallback) -> LLM (Gemini 3.5 Flash Lite) -> TTS (Azure / Edge-TTS Fallback) -> Audio Out (WAV PCM 16kHz mono 16-bit)
"""

import os
import sys
import time
import asyncio
import tempfile
import subprocess
from pathlib import Path
from xml.sax.saxutils import escape

from dotenv import load_dotenv
load_dotenv(Path(__file__).parent / ".env")

import requests
from fastapi import FastAPI, Request, Response, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from google import genai
from google.genai import types
import edge_tts

# --- Configuration ---
AZURE_SPEECH_KEY = (os.getenv("AZURE_SPEECH_KEY") or "").strip()
AZURE_SPEECH_REGION = (os.getenv("AZURE_SPEECH_REGION") or "southeastasia").strip()
AISTUDIO_API_KEY = (os.getenv("AISTUDIO_API_KEY") or "").strip()

GEMINI_MODEL = "gemini-3.5-flash-lite"
TTS_VOICE = "id-ID-ArdiNeural"
TARGET_SAMPLE_RATE = 16000

# Initialize Gemini Client
if not AISTUDIO_API_KEY:
    raise RuntimeError("AISTUDIO_API_KEY is not set in backend/.env")

gemini_client = genai.Client(api_key=AISTUDIO_API_KEY)

# Check if Azure Key is standard 32-char hex key
def is_valid_azure_key(key: str) -> bool:
    if len(key) == 32 and all(c in "0123456789abcdefABCDEF" for c in key):
        return True
    return False

AZURE_ACTIVE = is_valid_azure_key(AZURE_SPEECH_KEY)

SYSTEM_INSTRUCTION = (
    "Kamu adalah robot kecil bernama Reggi Bot. Jawablah dengan ramah, singkat, "
    "natural, dan mudah didengar melalui speaker kecil. Gunakan Bahasa Indonesia "
    "kecuali pengguna meminta bahasa lain. Jawaban maksimal 40 sampai 50 kata, "
    "cocok untuk diucapkan, tanpa format markdown, tanpa bullet point, tanpa simbol rumit, "
    "dan jangan awali jawaban dengan kata 'Sebagai AI'."
)

app = FastAPI(title="Reggi Bot Conversational Audio Pipeline")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)


# --- 1. Speech-to-Text Service ---
def transcribe_azure(audio_bytes: bytes, language: str = "id-ID") -> str:
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
    resp = requests.post(url, headers=headers, data=audio_bytes, timeout=15)
    if resp.status_code != 200:
        raise RuntimeError(f"Azure STT failed {resp.status_code}: {resp.text[:200]}")
    data = resp.json()
    status = data.get("RecognitionStatus")
    if status not in ("Success", "InitialSilenceTimeout", "NoMatch"):
        raise RuntimeError(f"Azure STT status: {status}")
    return data.get("DisplayText", "")


def transcribe_gemini(audio_bytes: bytes) -> str:
    prompt = (
        "Transkripsikan isi rekaman suara ini persis dalam Bahasa Indonesia. "
        "Kembalikan teks transkripsinya saja secara langsung tanpa kata pengantar, "
        "tanpa tanda kutip, dan tanpa penjelasan apapun."
    )
    res = gemini_client.models.generate_content(
        model=GEMINI_MODEL,
        contents=[
            types.Part.from_bytes(data=audio_bytes, mime_type="audio/wav"),
            prompt,
        ],
    )
    return res.text.strip() if res.text else ""


def transcribe_audio(audio_bytes: bytes) -> tuple[str, str]:
    """Returns (transcribed_text, engine_used)"""
    if AZURE_ACTIVE:
        try:
            txt = transcribe_azure(audio_bytes)
            return txt, "azure"
        except Exception as e:
            print(f"[STT] Azure STT failed ({e}), falling back to Gemini Transcribe...")
    
    txt = transcribe_gemini(audio_bytes)
    return txt, "gemini"


# --- Multi-turn Session Manager ---
import re

EXIT_PHRASES = {
    "sudah dulu", "cukup", "udahan", "selesai", "sampai nanti",
    "selamat tinggal", "kembali tidur", "berhenti ngobrol", "itu saja",
    "bye", "bye bye", "goodbye", "stop"
}

COURTESY_WORDS = {
    "terima", "kasih", "makasih", "banyak", "oke", "ok", "baiklah",
    "ya", "dong", "deh", "kak", "bot", "saja"
}

def is_explicit_exit(text: str) -> bool:
    clean = re.sub(r"[^\w\s]", "", text.lower()).strip()
    if not clean:
        return False
    if clean in EXIT_PHRASES:
        return True
    
    words = clean.split()
    start_idx = 0
    while start_idx < len(words) and words[start_idx] in COURTESY_WORDS:
        start_idx += 1
    end_idx = len(words)
    while end_idx > start_idx and words[end_idx - 1] in COURTESY_WORDS:
        end_idx -= 1
        
    core = " ".join(words[start_idx:end_idx]).strip()
    if core in EXIT_PHRASES:
        return True
    return False


class SessionManager:
    def __init__(self, ttl_seconds: int = 300):
        self.ttl = ttl_seconds
        self.sessions: dict[str, dict] = {}

    def get_history(self, session_id: str) -> list[types.Content]:
        self.cleanup()
        if session_id and session_id in self.sessions:
            self.sessions[session_id]["last_active"] = time.monotonic()
            return list(self.sessions[session_id]["history"])
        return []

    def add_turn(self, session_id: str, user_text: str, bot_reply: str):
        if not session_id:
            return
        self.cleanup()
        if session_id not in self.sessions:
            self.sessions[session_id] = {
                "history": [],
                "created_at": time.monotonic(),
                "last_active": time.monotonic(),
                "turns": 0,
            }
        s = self.sessions[session_id]
        s["history"].append(
            types.Content(role="user", parts=[types.Part.from_text(text=user_text)])
        )
        s["history"].append(
            types.Content(role="model", parts=[types.Part.from_text(text=bot_reply)])
        )
        if len(s["history"]) > 20:
            s["history"] = s["history"][-20:]
        s["last_active"] = time.monotonic()
        s["turns"] += 1

    def close_session(self, session_id: str):
        if session_id in self.sessions:
            del self.sessions[session_id]

    def cleanup(self):
        now = time.monotonic()
        expired = [sid for sid, data in self.sessions.items() if now - data["last_active"] > self.ttl]
        for sid in expired:
            del self.sessions[sid]

session_manager = SessionManager(ttl_seconds=300)


# --- 2. LLM Service (Gemini Multi-Turn) ---
def generate_llm_response(prompt_text: str, session_id: str = "") -> str:
    history = session_manager.get_history(session_id)
    contents = list(history)
    contents.append(
        types.Content(role="user", parts=[types.Part.from_text(text=prompt_text)])
    )

    res = gemini_client.models.generate_content(
        model=GEMINI_MODEL,
        contents=contents,
        config=types.GenerateContentConfig(
            system_instruction=SYSTEM_INSTRUCTION,
            max_output_tokens=160,
            temperature=0.7,
        ),
    )
    bot_reply = res.text.strip() if res.text else ""
    if not bot_reply:
        bot_reply = "Halo, saya Reggi Bot. Ada yang bisa saya bantu?"

    if session_id:
        session_manager.add_turn(session_id, prompt_text, bot_reply)

    return bot_reply


# --- 3. Text-to-Speech Service ---
def tts_azure(text: str, voice: str = TTS_VOICE) -> bytes:
    url = f"https://{AZURE_SPEECH_REGION}.tts.speech.microsoft.com/cognitiveservices/v1"
    headers = {
        "Ocp-Apim-Subscription-Key": AZURE_SPEECH_KEY,
        "Ocp-Apim-Subscription-Region": AZURE_SPEECH_REGION,
        "Content-Type": "application/ssml+xml",
        "X-Microsoft-OutputFormat": "riff-16khz-16bit-mono-pcm",
        "User-Agent": "rbots-backend",
    }
    safe_text = escape(text)
    ssml = f"""<speak version='1.0' xml:lang='id-ID'><voice name='{voice}'>{safe_text}</voice></speak>"""
    resp = requests.post(url, headers=headers, data=ssml.encode("utf-8"), timeout=15)
    if resp.status_code != 200:
        raise RuntimeError(f"Azure TTS failed {resp.status_code}: {resp.text[:200]}")
    return resp.content


async def tts_edge(text: str, voice: str = TTS_VOICE) -> bytes:
    comm = edge_tts.Communicate(text, voice)
    with tempfile.NamedTemporaryFile(suffix=".mp3", delete=False) as mp3_f:
        mp3_path = mp3_f.name
    await comm.save(mp3_path)

    wav_path = mp3_path.replace(".mp3", ".wav")
    cmd = [
        "ffmpeg", "-y", "-i", mp3_path,
        "-ar", str(TARGET_SAMPLE_RATE),
        "-ac", "1",
        "-c:a", "pcm_s16le",
        wav_path,
    ]
    subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
    with open(wav_path, "rb") as f:
        wav_data = f.read()

    try:
        os.remove(mp3_path)
        os.remove(wav_path)
    except Exception:
        pass

    return wav_data


async def synthesize_speech(text: str) -> tuple[bytes, str]:
    """Returns (wav_bytes, engine_used)"""
    if AZURE_ACTIVE:
        try:
            wav = tts_azure(text)
            return wav, "azure"
        except Exception as e:
            print(f"[TTS] Azure TTS failed ({e}), falling back to Edge-TTS...")
    
    wav = await tts_edge(text)
    return wav, "edge_tts"


# --- Helpers: Ensure WAV Header ---
def ensure_wav_bytes(raw_bytes: bytes) -> bytes:
    """If incoming bytes lack a RIFF header, wrap as 16kHz mono 16-bit WAV."""
    if raw_bytes.startswith(b"RIFF"):
        return raw_bytes
    
    pcm_len = len(raw_bytes)
    total_len = 44 + pcm_len
    header = bytearray(44)
    header[0:4] = b"RIFF"
    chunk_size = total_len - 8
    header[4:8] = chunk_size.to_bytes(4, "little")
    header[8:16] = b"WAVEfmt "
    header[16:20] = (16).to_bytes(4, "little")
    header[20:22] = (1).to_bytes(2, "little")   # PCM
    header[22:24] = (1).to_bytes(2, "little")   # Mono
    header[24:28] = (16000).to_bytes(4, "little") # 16kHz
    header[28:32] = (32000).to_bytes(4, "little") # Byte rate (16000*2)
    header[32:34] = (2).to_bytes(2, "little")   # Block align
    header[34:36] = (16).to_bytes(2, "little")  # Bits per sample
    header[36:40] = b"data"
    header[40:44] = pcm_len.to_bytes(4, "little")
    return bytes(header) + raw_bytes


# --- API Endpoints ---

@app.get("/health")
def health():
    return {
        "status": "online",
        "service": "rbot-backend",
        "gemini_model": GEMINI_MODEL,
        "azure_active": AZURE_ACTIVE,
        "azure_speech_region": AZURE_SPEECH_REGION,
        "voice": TTS_VOICE,
    }


@app.post("/chat/voice")
async def chat_voice(request: Request):
    """
    Main endpoint for ESP32-S3:
    Input: Audio WAV or PCM (16kHz mono 16-bit)
    Output: Response Audio WAV (16kHz mono 16-bit PCM)
    """
    t_start = time.monotonic()
    raw_audio = await request.body()
    if not raw_audio:
        raise HTTPException(status_code=400, detail="Audio body is empty")

    wav_audio = ensure_wav_bytes(raw_audio)
    
    # 1. Speech-to-Text
    t_stt_0 = time.monotonic()
    user_text, stt_engine = transcribe_audio(wav_audio)
    t_stt = int((time.monotonic() - t_stt_0) * 1000)
    print(f"\n[STT - {stt_engine.upper()}] ({t_stt}ms): '{user_text}'")

    if not user_text.strip():
        user_text = "Halo?"

    # 2. LLM (Gemini with Multi-turn Session & Explicit Exit)
    session_id = (request.headers.get("X-Session-Id") or "").strip()
    is_exit = is_explicit_exit(user_text)
    
    t_llm_0 = time.monotonic()
    if is_exit:
        session_action = "end"
        bot_reply = "Sama-sama! Senang mengobrol denganmu. Sampai jumpa lagi!"
        if session_id:
            session_manager.close_session(session_id)
        print(f"[SESSION] Explicit exit detected for session '{session_id}': action=end")
    else:
        session_action = "continue"
        bot_reply = generate_llm_response(user_text, session_id=session_id)

    t_llm = int((time.monotonic() - t_llm_0) * 1000)
    print(f"[LLM - GEMINI] ({t_llm}ms) [Session: {session_id or 'none'} | Action: {session_action}]: '{bot_reply}'")

    # 3. Text-to-Speech
    t_tts_0 = time.monotonic()
    response_wav, tts_engine = await synthesize_speech(bot_reply)
    t_tts = int((time.monotonic() - t_tts_0) * 1000)
    t_total = int((time.monotonic() - t_start) * 1000)
    print(f"[TTS - {tts_engine.upper()}] ({t_tts}ms): {len(response_wav)} bytes WAV")
    print(f"[TOTAL LATENCY] {t_total}ms (STT: {t_stt}ms, LLM: {t_llm}ms, TTS: {t_tts}ms)\n")

    return Response(
        content=response_wav,
        media_type="audio/wav",
        headers={
            "X-Transcription": user_text.replace("\n", " "),
            "X-Response-Text": bot_reply.replace("\n", " "),
            "X-STT-Engine": stt_engine,
            "X-TTS-Engine": tts_engine,
            "X-STT-Latency-Ms": str(t_stt),
            "X-LLM-Latency-Ms": str(t_llm),
            "X-TTS-Latency-Ms": str(t_tts),
            "X-Total-Latency-Ms": str(t_total),
            "X-Session-Id": session_id,
            "X-Session-Action": session_action,
        },
    )


@app.post("/chat/session/close")
async def close_session_endpoint(request: Request):
    """Explicitly clean up an active session context in memory."""
    body_data = {}
    try:
        body_data = await request.json()
    except Exception:
        pass
    session_id = request.headers.get("X-Session-Id") or body_data.get("session_id", "")
    if session_id:
        session_manager.close_session(session_id)
        print(f"[SESSION] Session '{session_id}' explicitly closed.")
    return {"status": "closed", "session_id": session_id}


# Individual test endpoints per requirements
@app.post("/test/tts")
async def test_tts(request: Request):
    data = await request.json()
    text = data.get("text", "Halo Reggi. Sistem suara robot berhasil terhubung.")
    wav, engine = await synthesize_speech(text)
    return Response(
        content=wav,
        media_type="audio/wav",
        headers={"X-TTS-Engine": engine},
    )


@app.post("/test/stt")
async def test_stt(request: Request):
    raw_audio = await request.body()
    wav_audio = ensure_wav_bytes(raw_audio)
    text, engine = transcribe_audio(wav_audio)
    return {"status": "success", "text": text, "engine": engine}


if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host="0.0.0.0", port=8000)
