# aibackend/api/services/gradient_client.py
import os
import httpx
from typing import Any, Dict

GRADIENT_API_KEY = os.getenv("GRADIENT_API_KEY") or os.getenv("GRADIENT_API_KEY")
GRADIENT_INFERENCE_URL = os.getenv("GRADIENT_INFERENCE_URL", "https://inference.do-ai.run/v1/chat/completions")

if not GRADIENT_API_KEY:
    # jangan raise supaya import tetap aman di dev tanpa env; handler akan cek lagi
    GRADIENT_API_KEY = None

async def call_gradient_model(payload: Dict[str, Any], timeout: int = 60) -> str:
    """
    Generic async call to Gradient inference endpoint.
    payload: dict sesuai docs Gradient (sesuaikan model/input key)
    """
    if not GRADIENT_API_KEY:
        raise RuntimeError("GRADIENT_API_KEY not set in environment")

    headers = {
        "Authorization": f"Bearer {GRADIENT_API_KEY}",
        "Content-Type": "application/json",
    }

    async with httpx.AsyncClient(timeout=timeout) as client:
        resp = await client.post(GRADIENT_INFERENCE_URL, json=payload, headers=headers)
        # kamu bisa menyesuaikan handling error / retry disini
        resp.raise_for_status()
        data = resp.json()
        return data["choices"][0]["message"]["content"]