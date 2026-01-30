from pydantic import BaseModel
from typing import Optional


class ChatRequest(BaseModel):
    message: str
    force_model: Optional[str] = None  # "qwen" or "gemma" or "qwen2.5"


class ChatResponse(BaseModel):
    model_config = {"protected_namespaces": ()}
    
    job_id: str
    model_used: str
    answer: str
    brave_used: bool = False
    escalated: bool = False
    pipeline: Optional[str] = None
    status: Optional[str] = "success"
    enriched: Optional[bool] = False
