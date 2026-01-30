Panduan Deploy Terurut (tanpa rsync, pakai Git + Hook)
Agar rapi untuk install di fresh install.

1) Install paket dasar + Python

```
sudo apt update && sudo apt upgrade -y
sudo apt install -y git curl ca-certificates python3 python3-venv python3-pip nginx ufw uvicorn
sudo ufw allow OpenSSH
sudo ufw allow 'Nginx Full'
sudo ufw enable
sudo ufw status
```

Tambahan: Setup Git bare repository (server) + remote (lokal)

```
# Di server: buat bare repo
mkdir -p /home/git && cd /home/git
git init --bare aibackend.git

# Di server: siapkan working directory (akan diisi oleh hook)
mkdir -p /home/aibackend

# Di laptop/PC: inisialisasi repo lokal & push pertama
cd /path/to/aibackend   # berisi api/ dan worker/
git init
git add .
git commit -m "init backend"
git remote add droplet ssh://root@IP_SERVER/home/git/aibackend.git
git push -u droplet main
```

2) Install Docker (untuk Redis)

```
sudo apt install -y apt-transport-https gnupg lsb-release
curl -fsSL https://download.docker.com/linux/ubuntu/gpg | sudo gpg --dearmor -o /usr/share/keyrings/docker.gpg
echo "deb [arch=$(dpkg --print-architecture) signed-by=/usr/share/keyrings/docker.gpg] https://download.docker.com/linux/ubuntu \
$(lsb_release -cs) stable" | sudo tee /etc/apt/sources.list.d/docker.list >/dev/null
sudo apt update
sudo apt install -y docker-ce docker-ce-cli containerd.io
sudo usermod -aG docker $USER
newgrp docker
docker --version
```

3) Jalankan Redis dengan Docker

```
docker run -d \
  --name redis-queue \
  --restart unless-stopped \
	-p 127.0.0.1:6379:6379 \
  redis:7
docker ps | grep redis
sudo apt install -y redis-tools
redis-cli -h 127.0.0.1 -p 6379 ping  # PONG
```

4) Siapkan project Python (API Gateway + Worker)

```
# Hook post-receive (di server)
cd /home/git/aibackend.git/hooks
cat > post-receive <<'EOF'
#!/bin/bash
set -e
REPO_DIR="/home/git/aibackend.git"
WORK_TREE="/home/aibackend"
echo "[post-receive] Deploying to $WORK_TREE"
GIT_WORK_TREE="$WORK_TREE" GIT_DIR="$REPO_DIR" git checkout -f main
echo "[post-receive] Running deploy.sh..."
/home/aibackend/deploy.sh || { echo "[post-receive] deploy.sh failed!"; exit 1; }
echo "[post-receive] Done."
EOF
chmod +x post-receive
```

5) Bikin virtualenv & install dependency (sekali saja di server)

```
cd /home/aibackend
python3 -m venv .venv
source .venv/bin/activate
python -m pip install --upgrade pip
python -m pip install -r requirements.txt

# Buat .env
cat > /home/aibackend/.env <<'EOF'
BRAVE_API_KEY=your_key
REDIS_HOST=127.0.0.1
REDIS_PORT=6379
OLLAMA_URL=http://localhost:11434
QWEN25_MODEL=qwen2.5:1.5b-instruct
QWEN3_MODEL=qwen3:1.7b
GEMMA_MODEL=gemma3:1b
EOF
```

6) Tes semua services dari server

```
# API
cd /home/aibackend && source .venv/bin/activate
uvicorn api.main:app --host 0.0.0.0 --port 8000
curl -s http://127.0.0.1:8000/health

# Worker (shell/tmux lain)
cd /home/aibackend && source .venv/bin/activate
python worker/worker.py

# (Opsional) Nginx reverse proxy
sudo tee /etc/nginx/sites-available/aibackend >/dev/null <<'EOF'
server {
	listen 80;
	server_name _;
	location / {
		proxy_pass http://127.0.0.1:8000;
		proxy_set_header Host $host;
		proxy_set_header X-Real-IP $remote_addr;
		proxy_set_header X-Forwarded-Proto $scheme;
		proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
	}
	location /static/ {
		alias /home/aibackend/api/static/;
	}
}
EOF
sudo ln -sf /etc/nginx/sites-available/aibackend /etc/nginx/sites-enabled/aibackend
sudo nginx -t && sudo systemctl reload nginx
```

---

## Production Hardening (Worker Async + Queue Consistency + Rate Limit)

Gunakan section ini saat setup di server baru agar worker async selalu hidup, queue konsisten, dan endpoint mahal terlindungi.

### 1) Pastikan Queue Konsisten

Gunakan 1 queue standar: `aibackend:jobs` untuk `/chat-gradient` dan worker async.

Tambahkan/cek env di `/home/aibackend/.env`:

```
QUEUE_JOBS=aibackend:jobs
QUEUE_GRADIENT=aibackend:jobs
WORKER_MODE=async
```

### 2) Systemd untuk Worker Async

Buat service file:

```
sudo tee /etc/systemd/system/aibackend-worker.service >/dev/null <<'EOF'
[Unit]
Description=AI backend gradient worker
After=network.target

[Service]
WorkingDirectory=/home/aibackend
Environment=WORKER_MODE=async
ExecStart=/home/aibackend/.venv/bin/python -u worker/worker.py
Restart=always

[Install]
WantedBy=multi-user.target
EOF
```

Aktifkan dan jalankan:

```
sudo systemctl daemon-reload
sudo systemctl enable --now aibackend-worker
sudo systemctl status aibackend-worker --no-pager
sudo journalctl -u aibackend-worker -n 100 --no-pager
```

### 3) Rate Limit Nginx (Proteksi Bot)

Set rate limit di `/etc/nginx/nginx.conf` (di blok `http`):

```
limit_req_zone $binary_remote_addr zone=chat_zone:10m rate=60r/m;
```

Terapkan limit di `/etc/nginx/sites-available/default` (lokasi chat):

```
location ~ ^/(chat|chat-gradient|chat-stream)$ {
		limit_req zone=chat_zone burst=20 nodelay;
		limit_req_status 429;
		proxy_pass http://127.0.0.1:8000;
		proxy_set_header Host $host;
		proxy_set_header X-Real-IP $remote_addr;
		proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
		proxy_set_header X-Forwarded-Proto $scheme;
}
```

Reload nginx:

```
sudo nginx -t && sudo systemctl reload nginx
```

### 4) Observability Minimal (Queue + Timing)

Log yang direkam oleh API untuk `/chat-gradient`:

- `gradient_enqueue`: `queue_len`, `enqueue_ms`
- `gradient_result`: `wait_ms`, `processing_ms`
- `gradient_timeout`: `wait_ms`

Log worker async menambahkan `processing_duration_ms` di payload hasil.

### 5) Quick Verification

```
redis-cli LLEN aibackend:jobs
curl -s -o /tmp/chat_gradient.json -w '%{http_code}' \
	-X POST http://127.0.0.1:8000/chat-gradient \
	-H 'Content-Type: application/json' \
	-d '{"message":"Tes singkat, jawab satu kalimat."}'
cat /tmp/chat_gradient.json
```

Expected: HTTP `200` dan queue `aibackend:jobs` tidak menumpuk.


#troubleshooting
Testing: Git Push dari Lokal → Lihat Efek di Server

Di Mesin Lokal:

```
bash
cd /path/to/aibackend
git push droplet main
```

Output yang Diharapkan:

```
text
[post-receive] Deploying to /home/aibackend
[post-receive] Done.
```

Verifikasi di Server:

```
bash
cd /home/aibackend
ls -la
```

Hasil: Isi repository Anda (folder `api`, `worker`, dll.) sekarang muncul di `/home/aibackend`.

📝 Catatan Penting & Troubleshooting

🔑 1. SSH Key Authentication

Pastikan SSH key lokal sudah ditambahkan ke server (~/.ssh/authorized_keys)

Test koneksi SSH: ssh root@IP_SERVER

🌿 2. Branch Management

Sesuaikan nama branch (main vs master)

Untuk branch selain main: git push droplet nama-branch:main

🛠️ 3. Hook yang Lebih Kompleks

Tambahkan logika sesuai kebutuhan:

```
bash
#!/bin/bash
set -e

REPO_DIR="/home/git/aibackend.git"
WORK_TREE="/home/aibackend"
DEPLOY_LOG="/home/aibackend/deploy.log"

echo "$(date) - Starting deployment" >> "$DEPLOY_LOG"

export GIT_WORK_TREE="$WORK_TREE"
export GIT_DIR="$REPO_DIR"

# Pull changes
cd "$WORK_TREE"
git fetch --all
git reset --hard origin/main

# Install/update dependencies
if [ -f "requirements.txt" ]; then
	pip install -r requirements.txt --upgrade
fi

# Restart services
systemctl restart aibackend-api
systemctl restart aibackend-worker

echo "$(date) - Deployment completed" >> "$DEPLOY_LOG"
echo "[post-receive] Deployment successful"
```

🔍 4. Debugging Hook

Cek permissions: ls -la /home/git/aibackend.git/hooks/

Test hook manual: cd /home/git/aibackend.git && ./hooks/post-receive

Lihat git log: git --git-dir=/home/git/aibackend.git log --oneline

🗂️ 5. Struktur Direktori Final

```
text
/home/
├── git/
│   └── aibackend.git/     # Bare repository
└── aibackend/             # Working directory (auto-deployed)
	├── api/
	├── worker/
	└── (file lainnya)
```

📎 Contoh post-receive yang memanggil deploy.sh

```
#!/bin/bash
set -e

REPO_DIR="/home/git/aibackend.git"
WORK_TREE="/home/aibackend"

echo "[post-receive] Deploying to $WORK_TREE"

# Checkout isi repo ke working directory
GIT_WORK_TREE="$WORK_TREE" GIT_DIR="$REPO_DIR" git checkout -f main

# Optional: install dependencies (sekali-sekali aja biasanya)
# cd "$WORK_TREE"
# source .venv/bin/activate
# pip install -r requirements.txt

# Optional: restart service API & worker
# Contoh sederhana, kalau kamu belum pakai systemd:
# pkill -f "uvicorn api.main" || true
# pkill -f "worker/worker.py" || true
# nohup uvicorn api.main:app --host 0.0.0.0 --port 8000 > /home/aibackend/api.log 2>&1 &
# nohup python /home/aibackend/worker/worker.py > /home/aibackend/worker.log 2>&1 &

# Panggil script deploy
echo "[post-receive] Running deploy.sh..."
/home/aibackend/deploy.sh || {
	echo "[post-receive] deploy.sh failed!"
	exit 1
}

echo "[post-receive] Done."
```

🧯 Troubleshooting: Redis timeout (Gateway error 111)

Jika muncul error:

```
Gateway error: Error 111 connecting to localhost:6379. Connection refused.
```

Ikuti langkah-langkah berikut:

1️⃣ Cek dulu: Redis jalan atau nggak

Di server:

```
docker ps | grep redis
```

Kalau kosong → Redis belum jalan.

Cek kalau pernah dibuat tapi mati:

```
docker ps -a | grep redis
```

Kalau ada container `redis-queue` tapi statusnya Exited, kita bisa start lagi:

```
docker start redis-queue
```

2️⃣ Kalau belum pernah dibuat, jalankan Redis (sekalian auto-restart)

Rekomendasi komando:
harus pin point ke local
```
docker run -d \
  --name redis-queue \
  --restart unless-stopped \
	-p 127.0.0.1:6379:6379 \
  redis:7
```

Penjelasan singkat:

- `--restart unless-stopped` → kalau server reboot, Redis ikut hidup lagi.
- `-p 6379:6379` → expose ke localhost:6379 (ini yang dipakai gateway & worker).

Cek lagi:

```
docker ps | grep redis
```

Harusnya ada baris dengan `redis:7` dan port `0.0.0.0:6379->6379/tcp`.

Kalau mau ekstra yakin:

```
apt install -y redis-tools  # kalau belum
redis-cli ping
```

→ harus jawab: `PONG`.