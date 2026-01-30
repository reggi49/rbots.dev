import requests
import json
from datetime import datetime, timezone
from typing import List
from ..config import BRAVE_API_KEY, logger


def call_brave_summary(query: str) -> str:
    """Calls Brave Web Search API and returns a summary string (up to 3 bullets)."""
    if not BRAVE_API_KEY:
        raise RuntimeError("BRAVE_API_KEY belum di-set di environment.")

    url = "https://api.search.brave.com/res/v1/web/search"
    headers = {
        "Accept": "application/json",
        "Accept-Encoding": "gzip",
        "X-Subscription-Token": BRAVE_API_KEY,
    }
    params = {"q": query, "count": 5, "country": "ID", "lang": "en-ID"}

    logger.info(json.dumps({"event": "brave_web_request", "query": query}, ensure_ascii=False))

    resp = requests.get(url, headers=headers, params=params, timeout=8)
    raw_text = resp.text  # fallback sample

    try:
        resp.raise_for_status()
    except requests.HTTPError:
        logger.error(
            json.dumps(
                {
                    "event": "brave_web_http_error",
                    "query": query,
                    "status": resp.status_code,
                    "body": raw_text[:800],
                },
                ensure_ascii=False,
            )
        )
        return raw_text[:600]

    try:
        data = resp.json()
    except Exception as e:
        logger.error(
            json.dumps(
                {"event": "brave_web_json_error", "query": query, "error": str(e), "raw_sample": raw_text[:500]},
                ensure_ascii=False,
            )
        )
        return raw_text[:600]

    snippets: List[str] = []

    def add_snippet_from_item(item: dict):
        desc = item.get("description") or item.get("snippet") or ""
        desc = desc.strip()
        if not desc:
            return
        max_len = 220
        if len(desc) > max_len:
            desc = desc[:max_len].rsplit(" ", 1)[0] + "..."
        snippets.append(desc)

    try:
        web_block = data.get("web") or {}
        for item in web_block.get("results", []):
            if len(snippets) >= 3:
                break
            if isinstance(item, dict):
                add_snippet_from_item(item)

        if not snippets:
            for key, val in data.items():
                if len(snippets) >= 3:
                    break
                if isinstance(val, dict) and "results" in val:
                    for item in val.get("results", []):
                        if len(snippets) >= 3:
                            break
                        if isinstance(item, dict):
                            add_snippet_from_item(item)
    except Exception as e:
        logger.error(
            json.dumps(
                {"event": "brave_web_parse_error", "query": query, "error": str(e), "raw_sample": raw_text[:500]},
                ensure_ascii=False,
            )
        )
        return raw_text[:600]

    if not snippets:
        logger.warning(json.dumps({"event": "brave_web_no_snippets", "query": query, "raw_sample": raw_text[:300]}, ensure_ascii=False))
        return raw_text[:600]

    return "\n".join(f"- {s}" for s in snippets[:3])


def build_fact_message_with_brave(original_msg: str, brave_text: str) -> str:
    now = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S %Z")
    return (
        f"Hari ini {now}."
        "Baca lalu pahami dan jangan ditambah hal lain, pada kalimat ini:\n"
        f"{brave_text}\n\n"
        f"lalu ambil poin atau intinya untuk menjawab pertanyaan berikut: {original_msg}\n"
        "Jawaban singkat terdiri dari 1 kalimat."
    )
