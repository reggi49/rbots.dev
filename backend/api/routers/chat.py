import json
import time
import uuid
import requests
from fastapi import APIRouter, HTTPException
from fastapi.responses import StreamingResponse

from ..config import (
    r,
    r_async,
    logger,
    QUEUE_NAME,
    QUEUE_GRADIENT,
    MAX_INPUT_CHARS,
    RESULT_TIMEOUT_SEC,
    OLLAMA_URL,
    QWEN25_MODEL,
    QWEN3_MODEL,
    GEMMA_MODEL,
)
from ..schemas import ChatRequest, ChatResponse
from ..utils.text import is_fact_question
from ..services.brave import call_brave_summary, build_fact_message_with_brave
from ..services.gradient_ai import call_gradient_model


router = APIRouter()


@router.post("/chat", response_model=ChatResponse)
def chat(req: ChatRequest):
    brave_used = False
    brave_status = "not_called"
    escalated = False

    original_msg = req.message.strip()
    if not original_msg:
        raise HTTPException(status_code=400, detail="Pesan kosong.")
    if len(original_msg) > MAX_INPUT_CHARS:
        raise HTTPException(status_code=400, detail=f"Input terlalu panjang (maks {MAX_INPUT_CHARS} karakter).")

    msg = original_msg
    lower = msg.lower()

    # thinking mode keyword → Qwen3
    use_qwen3_keyword = False
    for kw in ["mode berpikir", "mode berfikir"]:
        if lower.startswith(kw):
            use_qwen3_keyword = True
            msg = msg[len(kw):].lstrip()
            lower = msg.lower()
            break

    # Detect if message is a fact question
    is_fact = any(keyword in lower for keyword in ["apa", "siapa", "dimana", "kapan", "berapa"])

    # select model (Qwen/Gemma path)
    if req.force_model:
        fm = req.force_model.lower()
        if fm == "qwen":
            model = QWEN3_MODEL
        elif fm == "gemma":
            model = GEMMA_MODEL
        elif fm in ("qwen2", "qwen25", "qwen2.5"):
            model = QWEN25_MODEL
        else:
            raise HTTPException(status_code=400, detail="force_model harus 'qwen', 'gemma', atau 'qwen2.5'.")
    else:
        model = QWEN3_MODEL if use_qwen3_keyword else QWEN25_MODEL

    job_id = uuid.uuid4().hex
    msg_for_model = msg

    # Brave enrichment only for fact questions on qwen2.5
    if is_fact and model == QWEN25_MODEL:
        brave_status = "calling"
        try:
            brave_text = call_brave_summary(original_msg)
            msg_for_model = build_fact_message_with_brave(original_msg, brave_text)
            brave_used = True
            brave_status = "ok"
        except Exception as e:
            brave_status = f"error: {type(e).__name__}"
            logger.error(json.dumps({"event": "brave_web_error", "job_id": job_id, "error": str(e)}, ensure_ascii=False))

    # push job
    job = {"job_id": job_id, "model": model, "message": msg_for_model}
    try:
        r.rpush(QUEUE_NAME, json.dumps(job))
        res = r.blpop(f"result:{job_id}", timeout=RESULT_TIMEOUT_SEC)
        if res is None:
            raise HTTPException(status_code=504, detail="Worker tidak merespons dalam batas waktu.")

        raw = res[1].decode("utf-8")
        answer = raw
        final_model = model
        escalated = False
        try:
            obj = json.loads(raw)
            if isinstance(obj, dict):
                answer = obj.get("answer", raw)
                final_model = obj.get("model_used", model)
                escalated = bool(obj.get("escalated", False))
        except json.JSONDecodeError:
            pass

        # pipeline label
        if final_model.startswith("qwen3"):
            if brave_used:
                pipeline = "brave+qwen3"
            elif escalated:
                pipeline = "qwen2.5→qwen3"
            else:
                pipeline = "qwen3_only"
        elif final_model.startswith("qwen2.5"):
            pipeline = "brave+qwen2.5" if brave_used else "qwen2.5_only"
        elif final_model.startswith("gemma3"):
            pipeline = "brave+gemma" if brave_used else "gemma_only"
        else:
            pipeline = "other"

        logger.info(
            json.dumps(
                {
                    "event": "chat",
                    "job_id": job_id,
                    "original_message": original_msg,
                    "use_qwen3_keyword": use_qwen3_keyword,
                    "is_fact": is_fact,
                    "brave_used": brave_used,
                    "brave_status": brave_status,
                    "escalated": escalated,
                    "model_initial": model,
                    "model_used": final_model,
                    "pipeline": pipeline,
                },
                ensure_ascii=False,
            )
        )

        return ChatResponse(
            job_id=job_id,
            model_used=final_model,
            answer=answer,
            brave_used=brave_used,
            escalated=escalated,
            pipeline=pipeline,
        )
    except HTTPException:
        raise
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Gateway error: {e}")


@router.post("/chat-stream")
def chat_stream(req: ChatRequest):
    msg = req.message.strip()
    if not msg:
        raise HTTPException(status_code=400, detail="Pesan kosong.")

    lower = msg.lower()
    use_qwen3_keyword = False
    for kw in ["mode berpikir", "mode berfikir"]:
        if lower.startswith(kw):
            use_qwen3_keyword = True
            msg = msg[len(kw):].lstrip()
            break

    if req.force_model:
        fm = req.force_model.lower()
        if fm == "qwen":
            model = QWEN3_MODEL
        elif fm == "gemma":
            model = GEMMA_MODEL
        elif fm in ("qwen2", "qwen25", "qwen2.5"):
            model = QWEN25_MODEL
        else:
            raise HTTPException(status_code=400, detail="force_model harus 'qwen', 'gemma', atau 'qwen2.5'.")
    else:
        model = QWEN3_MODEL if use_qwen3_keyword else QWEN25_MODEL

    def stream_generator():
        system_prompt = (
            "Kamu adalah guru SD yang menjelaskan dengan bahasa sederhana, "
            "singkat, dan mudah dipahami anak SD."
        )

        payload = {
            "model": model,
            "stream": True,
            "messages": [
                {"role": "system", "content": system_prompt},
                {"role": "user", "content": msg},
            ],
        }

        try:
            with requests.post(
            f"{OLLAMA_URL}/api/chat", json=payload, stream=True, timeout=360
            ) as resp:
                resp.raise_for_status()
                for line in resp.iter_lines():
                    if not line:
                        continue
                    try:
                        data = json.loads(line.decode("utf-8"))
                    except json.JSONDecodeError:
                        continue
                    chunk = data.get("message", {}).get("content", "")
                    if chunk:
                        yield chunk
        except Exception as e:
            yield f"\n[ERROR streaming]: {e}"

    return StreamingResponse(stream_generator(), media_type="text/plain; charset=utf-8")

# --- ENDPOINT BARU KHUSUS GRADIENT ---
@router.post("/chat-gradient")
async def chat_gradient_direct(req: ChatRequest):
    """Endpoint Gradient menggunakan Redis + worker async."""
    if not req.message or not req.message.strip():
        raise HTTPException(status_code=400, detail="Pesan tidak boleh kosong")

    job_id = uuid.uuid4().hex
    prompt = req.message.strip()
    job = {
        "job_id": job_id,
        "prompt": prompt,
        "max_tokens": 200,
    }

    enqueue_start = time.monotonic()
    try:
        await r_async.rpush(QUEUE_GRADIENT, json.dumps(job))
        queue_len = await r_async.llen(QUEUE_GRADIENT)
    except Exception as e:
        logger.error(json.dumps({"event": "gradient_enqueue_error", "job_id": job_id, "error": str(e)}))
        raise HTTPException(status_code=500, detail="Gagal mengirim job ke antrean")

    logger.info(
        json.dumps(
            {
                "event": "gradient_enqueue",
                "job_id": job_id,
                "queue": QUEUE_GRADIENT,
                "queue_len": queue_len,
                "enqueue_ms": int((time.monotonic() - enqueue_start) * 1000),
            }
        )
    )

    result_key = f"result:{job_id}"
    wait_start = time.monotonic()
    try:
        res = await r_async.blpop(result_key, timeout=RESULT_TIMEOUT_SEC)
    except Exception as e:
        logger.error(json.dumps({"event": "gradient_wait_error", "job_id": job_id, "error": str(e)}))
        raise HTTPException(status_code=500, detail=f"Gagal membaca hasil worker: {e}")

    if not res:
        logger.warning(
            json.dumps(
                {
                    "event": "gradient_timeout",
                    "job_id": job_id,
                    "wait_ms": int((time.monotonic() - wait_start) * 1000),
                }
            )
        )
        raise HTTPException(status_code=504, detail="Worker Gradient tidak merespons")

    _, payload_raw = res
    try:
        payload = json.loads(payload_raw.decode("utf-8"))
    except Exception as e:
        raise HTTPException(status_code=502, detail=f"Hasil worker tidak valid: {e}")

    if payload.get("status") == "error":
        logger.error(
            json.dumps(
                {
                    "event": "gradient_result_error",
                    "job_id": job_id,
                    "wait_ms": int((time.monotonic() - wait_start) * 1000),
                    "error": payload.get("error"),
                    "processing_ms": payload.get("processing_duration_ms"),
                }
            )
        )
        raise HTTPException(status_code=502, detail=payload.get("error", "Worker error"))

    logger.info(
        json.dumps(
            {
                "event": "gradient_result",
                "job_id": job_id,
                "wait_ms": int((time.monotonic() - wait_start) * 1000),
                "processing_ms": payload.get("processing_duration_ms"),
            }
        )
    )

    return {
        "status": payload.get("status", "success"),
        "job_id": job_id,
        "model_used": payload.get("model_used", "openai-gpt-5-nano"),
        "answer": payload.get("answer", ""),
        "error_primary": payload.get("error_primary"),
    }