import os
import redis
import redis.asyncio as redis_async
import logging
from pathlib import Path
from dotenv import load_dotenv

# --- Load .env ---
BASE_DIR = Path(__file__).resolve().parent.parent  # project root (contains api/ and worker/)
for candidate in [BASE_DIR / ".env", Path("/home/aibackend/.env")]:
    if candidate.exists():
        load_dotenv(candidate)
        break

# --- Constants ---
BRAVE_API_KEY = os.getenv("BRAVE_API_KEY")
REDIS_HOST = os.getenv("REDIS_HOST", "localhost")
REDIS_PORT = int(os.getenv("REDIS_PORT", "6379"))
QWEN25_MODEL = os.getenv("QWEN25_MODEL", "qwen2.5:1.5b-instruct")
QWEN3_MODEL = os.getenv("QWEN3_MODEL", "qwen3:1.7b")
GEMMA_MODEL = os.getenv("GEMMA_MODEL", "gemma3:1b")
OLLAMA_URL = os.getenv("OLLAMA_URL", "http://localhost:11434")
DO_ACCESS_TOKEN = os.getenv("DO_ACCESS_TOKEN")
WORKSPACE_ID = os.getenv("WORKSPACE_ID")

QUEUE_NAME = "queue:chat"
QUEUE_GRADIENT = os.getenv("QUEUE_GRADIENT") or os.getenv("QUEUE_JOBS", "aibackend:jobs")
QUEUE_STT = os.getenv("QUEUE_STT", "queue:stt")
MAX_INPUT_CHARS = 400
RESULT_TIMEOUT_SEC = 360
STT_RESULT_TIMEOUT_SEC = int(os.getenv("STT_RESULT_TIMEOUT_SEC", "20"))

SYSTEM_PROMPT_QWEN25 = (
    "Kamu adalah guru yang menjawab singkat, langsung ke hasilnya, dan tidak perlu bertele-tele.\n"
    "Gunakan bahasa Indonesia sederhana.\n"
    "Jawaban maksimum 4 kalimat untuk pertanyaan biasa.\n"
    "Jika pertanyaan sangat sederhana, jawab 1 hingga 2 kalimat saja.\n"
    "Jangan mengulang-ulang kalimat yang sama.\n"
    "Jangan menambah penjelasan yang tidak diminta.\n"
)

SYSTEM_PROMPT_QWEN = (
    "Kamu adalah guru yang sangat sabar dan pintar."
)

SYSTEM_PROMPT_GEMMA = (
    "Kamu adalah guru yang menjelaskan berbagai pelajaran. "
    "Gunakan kalimat sederhana,mudah dipahami dan jelas. "
    "Jawaban cukup 1 hingga 4 Kalimat tapi Lebih singkat lebih baik. "
    "Tidak boleh memakai istilah teknis, kata asing, atau simbol aneh. "
)

# --- Logging Setup ---
LOG_DIR = Path("logs")
LOG_DIR.mkdir(exist_ok=True)

logging.basicConfig(
    filename=LOG_DIR / "gateway.log",
    level=logging.INFO,
    format="%(asctime)s %(levelname)s %(message)s",
)
logger = logging.getLogger("gateway")

# --- Redis Connection ---
r = redis.Redis(host=REDIS_HOST, port=REDIS_PORT, db=0)
r_async = redis_async.Redis(host=REDIS_HOST, port=REDIS_PORT, db=0)

# --- Environment warnings (helpful during misconfiguration) ---
env_vars = {
    "BRAVE_API_KEY": BRAVE_API_KEY,
    "DO_ACCESS_TOKEN": DO_ACCESS_TOKEN,
    "WORKSPACE_ID": WORKSPACE_ID,
}

missing_envs = [key for key, val in env_vars.items() if not val]

if missing_envs:
    logger.warning("Missing envs: %s", ", ".join(missing_envs))
    print(f"WARNING: Missing environment variables: {', '.join(missing_envs)}")
