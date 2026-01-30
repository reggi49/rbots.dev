from pydantic_settings import BaseSettings, SettingsConfigDict

class Settings(BaseSettings):
    model_config = SettingsConfigDict(
        env_file=".env",
        protected_namespaces=("settings_",),
    )

    redis_host: str = "localhost"
    redis_port: int = 6379
    redis_url_async: str = "redis://localhost:6379/0"
    queue_chat: str = "queue:chat"
    queue_jobs: str = "aibackend:jobs"
    queue_gradient: str = "aibackend:jobs"
    queue_stt: str = "queue:stt"
    model_primary: str = "llama3-8b-instruct"
    model_fallback: str = "openai-gpt-oss-20b"
    ollama_url: str = "http://localhost:11434"
    # Add other env vars as needed
    brave_api_key: str = ""
    do_access_token: str = ""
    workspace_id: str = ""
    qwen25_model: str = "qwen2.5:1.5b-instruct"
    qwen3_model: str = "qwen3:1.7b"
    gemma_model: str = "gemma3:1b"
    system_prompt_qwen: str = (
        "Kamu adalah guru yang sangat sabar dan pintar."
    )
    system_prompt_gemma: str = (
        "Kamu adalah guru yang menjelaskan berbagai pelajaran. "
        "Gunakan kalimat sederhana,mudah dipahami dan jelas. "
        "Jawaban cukup 1 hingga 4 Kalimat tapi Lebih singkat lebih baik. "
        "Tidak boleh memakai istilah teknis, kata asing, atau simbol aneh. "
    )
    system_prompt_qwen25: str = (
        "Kamu adalah guru yang menjawab singkat, langsung ke hasilnya, dan tidak perlu bertele-tele.\n"
        "Gunakan bahasa Indonesia sederhana.\n"
        "Jawaban maksimum 4 kalimat untuk pertanyaan biasa.\n"
        "Jika pertanyaan sangat sederhana, jawab 1 hingga 2 kalimat saja.\n"
        "Jangan mengulang-ulang kalimat yang sama.\n"
        "Jangan menambah penjelasan yang tidak diminta.\n"
    )
    gradient_api_key: str = ""
    gradient_inference_url: str = ""
    max_input_chars: int = 400
    result_timeout_sec: int = 360
    worker_mode: str = "sync"


settings = Settings()
