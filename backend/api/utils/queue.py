# aibackend/api/utils/queue.py
import os
import json
import redis

REDIS_URL = os.getenv("REDIS_URL", "redis://localhost:6379/0")
QUEUE_KEY = "aibackend:jobs"

redis_client = redis.from_url(REDIS_URL)

def enqueue_job(job: dict):
    redis_client.lpush(QUEUE_KEY, json.dumps(job))