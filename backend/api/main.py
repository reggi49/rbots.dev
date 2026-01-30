
import requests
import httpx
from fastapi import FastAPI, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from fastapi.staticfiles import StaticFiles
from fastapi.responses import FileResponse

from .config import r, BRAVE_API_KEY, OLLAMA_URL
from .routers import chat as chat_router
from .routers import heartbeat as heartbeat_router
from .routers import tts as tts_router
from .routers import speech_to_text as stt_router


app = FastAPI(title="AI Gateway (Qwen3 + Gemma3)")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)

# Serve file HTML statis
app.mount("/static", StaticFiles(directory="api/static"), name="static")

# Include routers
app.include_router(chat_router.router)
app.include_router(heartbeat_router.router)
app.include_router(tts_router.router)
app.include_router(stt_router.router)

@app.get("/", include_in_schema=False)
def root():
    return FileResponse("api/static/index.html")

@app.get("/health")
async def health():
    services = {
        "redis": "unknown",
        "ollama": "unknown",
        "worker": "unknown",
        "chat_gradient": "unknown",
    }

    # 1) Redis
    redis_up = False
    try:
        r.ping()
        redis_up = True
        services["redis"] = "up"
    except Exception as e:
        services["redis"] = f"down: {e}"

    # 2) Worker + Queue (hanya kalau Redis up)
    if redis_up:
        try:
            # worker menulis key ini secara periodik (heartbeat)
            ts = r.get("worker:gradient:last_seen")
            services["worker"] = "up" if ts else "down (no heartbeat)"
        except Exception as e:
            services["worker"] = f"check failed: {e}"

        # Visibility: skip queue length check here (may be flaky);
        # worker heartbeat presence is sufficient to mark the worker up

    # 3) Ollama (async, non-blocking)
    ollama_up = False
    try:
        timeout = httpx.Timeout(connect=2.0, read=3.0, write=3.0, pool=3.0)
        async with httpx.AsyncClient(timeout=timeout) as client:
            resp = await client.get(f"{OLLAMA_URL}/api/tags")

        if resp.status_code == 200:
            ollama_up = True
            data = resp.json() if resp.headers.get("content-type", "").startswith("application/json") else {}
            models = data.get("models", [])
            names = [m.get("name") for m in models if isinstance(m, dict) and m.get("name")]
            services["ollama"] = {
                "status": "up",
                "models_available": len(names),
                "names": names[:5],
            }
        else:
            services["ollama"] = f"down: status {resp.status_code}"
    except Exception as e:
        services["ollama"] = f"down: {e}"

    # 4) Chat-gradient readiness (tanpa POST job di /health)
    if redis_up:
        worker_up = isinstance(services["worker"], str) and services["worker"] == "up"
        services["chat_gradient"] = "ready" if worker_up else "not ready (worker down)"

    # 5) Overall status (anggap down kalau Redis atau Ollama down)
    if not redis_up or not ollama_up:
        raise HTTPException(status_code=500, detail=services)

    return {"status": "up", "details": services}
