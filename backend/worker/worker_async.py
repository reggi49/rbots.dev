import asyncio
import json
import os
import time

import httpx
from redis.asyncio import Redis

from .settings import settings

GRADIENT_API_KEY = settings.gradient_api_key or os.getenv("GRADIENT_API_KEY")
GRADIENT_INFERENCE_URL = (
    settings.gradient_inference_url
    or os.getenv("GRADIENT_INFERENCE_URL")
    or "https://inference.do-ai.run/v1/chat/completions"
)


async def call_gradient_model(payload: dict) -> tuple[str, str]:
    """
    Async call to Gradient inference endpoint using httpx.
    Uses primary model, falls back to secondary if needed.
    """
    if not GRADIENT_API_KEY:
        raise RuntimeError("GRADIENT_API_KEY not set")

    headers = {
        "Authorization": f"Bearer {GRADIENT_API_KEY}",
        "Content-Type": "application/json",
    }

    primary_model = settings.model_primary
    fallback_model = settings.model_fallback
    used_model = primary_model

    # Try primary
    try:
        primary_payload = payload.copy()
        primary_payload["model"] = primary_model
        async with httpx.AsyncClient(timeout=60) as client:
            resp = await client.post(
                GRADIENT_INFERENCE_URL, json=primary_payload, headers=headers
            )
            resp.raise_for_status()
            data = resp.json()
            return data["choices"][0]["message"]["content"], used_model
    except Exception as e:
        print(f"Primary model failed: {e}")

    # Fallback
    try:
        used_model = fallback_model
        fallback_payload = payload.copy()
        fallback_payload["model"] = fallback_model
        async with httpx.AsyncClient(timeout=60) as client:
            resp = await client.post(
                GRADIENT_INFERENCE_URL, json=fallback_payload, headers=headers
            )
            resp.raise_for_status()
            data = resp.json()
            return data["choices"][0]["message"]["content"], used_model
    except Exception as e2:
        raise Exception(f"Both primary and fallback failed: {e2}")


async def process_job_async(job: dict):
    print("processing job (async):", job.get("job_id"))
    start_time = time.monotonic()

    system_prompt = job.get("system_prompt") or settings.system_prompt_qwen25
    payload = {
        "model": job.get("model", settings.model_primary),
        "messages": [
            {"role": "system", "content": system_prompt},
            {"role": "user", "content": job.get("prompt", "").strip()},
        ],
        "max_tokens": job.get("max_tokens", 200),
    }

    try:
        answer, model_used = await call_gradient_model(payload)
        processing_ms = int((time.monotonic() - start_time) * 1000)
        print("job result:", answer)
        return {
            "status": "success",
            "answer": answer,
            "model_used": model_used,
            "processing_duration_ms": processing_ms,
        }
    except Exception as e:
        processing_ms = int((time.monotonic() - start_time) * 1000)
        print("job failed:", e)
        return {
            "status": "error",
            "error": str(e),
            "processing_duration_ms": processing_ms,
        }


async def worker_loop_async():
    redis_client = Redis.from_url(settings.redis_url_async)
    print("[Async Worker] connected to redis", settings.redis_url_async)

    try:
        while True:
            item = await redis_client.brpop(settings.queue_jobs, timeout=5)
            if not item:
                await asyncio.sleep(0.1)
                continue

            _, value = item
            try:
                job = json.loads(value.decode() if isinstance(value, bytes) else value)
            except Exception:
                print("invalid job payload", value)
                continue

            job_id = job.get("job_id")
            if not job_id:
                print("job tanpa job_id diabaikan", job)
                continue

            result_key = f"result:{job_id}"
            result_payload = await process_job_async(job)
            result_payload["job_id"] = job_id
            try:
                await redis_client.lpush(result_key, json.dumps(result_payload))
            except Exception as e:
                print("job processing error:", e)
    finally:
        await redis_client.close()
