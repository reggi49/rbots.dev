from fastapi import APIRouter
from pydantic import BaseModel
from typing import Optional
import logging

# Configure minimal logging as requested
logger = logging.getLogger("uvicorn.error")

router = APIRouter()


class Heartbeat(BaseModel):
    device: str
    uptime_sec: Optional[int] = None
    wifi: Optional[str] = None
    battery: Optional[str] = None
    mood: Optional[str] = None


@router.post("/heartbeat")
async def post_heartbeat(hb: Heartbeat):
    # Log a single line per heartbeat
    logger.info("HEARTBEAT from %s", hb.device)

    # RESPONSE FORMAT: return {} if no command, or an object describing a command
    return {}
