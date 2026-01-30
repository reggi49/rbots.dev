import os
import json
import time
import redis
import requests
from .settings import settings

OLLAMA_URL = settings.ollama_url
QUEUE_NAME = settings.queue_chat
QWEN25_MODEL = settings.qwen25_model
QWEN3_MODEL = settings.qwen3_model
GEMMA_MODEL_NAME = settings.gemma_model

SYSTEM_PROMPT_QWEN = settings.system_prompt_qwen
SYSTEM_PROMPT_GEMMA = settings.system_prompt_gemma
SYSTEM_PROMPT_QWEN25 = settings.system_prompt_qwen25

def get_redis_client():
    return redis.Redis(host=settings.redis_host, port=settings.redis_port, db=0)

r = get_redis_client()

def call_ollama(model: str, system_prompt: str, user_message: str) -> str:
    payload = {
        "model": model,
        "stream": False,
        "messages": [
            {"role": "system", "content": system_prompt},
            {"role": "user", "content": user_message},
        ],
    }
    
    resp = requests.post(f"{OLLAMA_URL}/api/chat", json=payload, timeout=360)
    resp.raise_for_status()
    try:
        data = resp.json()
        content = data["message"]["content"].strip()
        if not content:
            raise ValueError("Empty content")
        return content
    except Exception:
        return "[ERROR] Model tidak mengembalikan jawaban."

def need_escalation(answer: str) -> bool:
    """
    Heuristik sederhana untuk mendeteksi jawaban yang kurang bagus.
    """
    text = answer.strip()

    low = text.lower()

    if text.startswith("[ERROR]"):
        return True
    
    bad_patterns = [
        "xen x",
        "/home/",
        "�",
        "secd ",
    ]
    
    if any(p in low for p in bad_patterns):
        return True

    if len(text) > 1500:
        return True

    return False

def main():
    global r
    print("Worker started (sync). Menunggu job dari Redis...")

    while True:
        try:
            item = r.blpop(QUEUE_NAME, timeout=0)
        except redis.exceptions.RedisError as e:
            print(f"[Worker] Redis error saat BLPOP: {e}. Coba reconnect dalam 2 detik...")
            time.sleep(2)
            try:
                r = get_redis_client()
                r.ping()
                print("[Worker] Redis reconnect OK.")
            except Exception as e2:
                print(f"[Worker] Gagal reconnect Redis: {e2}")
            continue

        if not item:
            continue

        _, job_raw = item

        try:
            job = json.loads(job_raw.decode("utf-8"))
        except Exception as e:
            print(f"[Worker] Gagal parse job: {e} | raw={job_raw!r}")
            continue

        job_id = job.get("job_id") or f"noid-{int(time.time())}"
        model = job.get("model", QWEN25_MODEL)
        message = job.get("message", "")

        print(f"[Worker] Job {job_id} dengan model {model}")
        final_model = model

        try:
            if "qwen2.5" in model:
                system_prompt = SYSTEM_PROMPT_QWEN25
            elif "qwen3" in model:
                system_prompt = SYSTEM_PROMPT_QWEN
            else:
                system_prompt = SYSTEM_PROMPT_GEMMA

            answer = call_ollama(model, system_prompt, message)

            if model == QWEN25_MODEL and need_escalation(answer):
                try:
                    print(f"[Worker] Eskalasi job {job_id} ke Qwen3 (guru senior)")
                    senior_prompt = (
                        SYSTEM_PROMPT_QWEN
                        + "\n\nSeorang guru junior sudah mencoba menjawab pertanyaan murid, "
                          "tapi jawabannya kurang rapi atau kurang jelas. "
                          "Tolong jelaskan ulang dengan lebih baik untuk anak sekolah. "
                          "Perbaiki jika ada kesalahan dan buat penjelasan yang singkat, jelas, dan terstruktur.\n\n"
                          f"Pertanyaan murid: {message}\n\n"
                          f"Jawaban guru junior:\n{answer}\n\n"
                          "Sekarang berikan jawaban final sebagai guru sekolah senior."
                    )
                    senior_answer = call_ollama(QWEN3_MODEL, senior_prompt, message)
                    answer = senior_answer
                    final_model = QWEN3_MODEL
                except Exception as e2:
                    print(f"[Worker] Eskalasi ke Qwen3 gagal (job {job_id}): {e2}")

        except Exception as e:
            answer = f"Terjadi kesalahan saat memanggil model: {e}"

        result_key = f"result:{job_id}"
        result_payload = {
            "answer": answer,
            "model_used": final_model,
            "escalated": (final_model != model),
        }
        try:
            r.lpush(result_key, json.dumps(result_payload))
        except Exception as e:
            print(f"[Worker] Gagal push result ke Redis untuk job {job_id}: {e}")