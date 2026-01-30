import asyncio
import sys
from pathlib import Path

if __package__ is None or __package__ == "":
    repo_root = Path(__file__).resolve().parent.parent
    if str(repo_root) not in sys.path:
        sys.path.insert(0, str(repo_root))
    __package__ = "worker"
    from worker.settings import settings  # allows `python worker/worker.py`
else:
    from .settings import settings

if settings.worker_mode == "sync":
    from .worker_sync import worker_loop_sync
    worker_loop_sync()
elif settings.worker_mode == "async":
    from .worker_async import worker_loop_async
    asyncio.run(worker_loop_async())
else:
    raise ValueError(f"Invalid WORKER_MODE: {settings.worker_mode}")
