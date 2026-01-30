#!/usr/bin/env bash
set -euo pipefail

# Fresh Ubuntu server bootstrap for aibackend
# - Installs system packages (git, python, redis, nginx)
# - Creates app directory, Python venv, installs requirements
# - Sets up systemd services for API and Worker
# - Configures Nginx reverse proxy
# - Enables UFW firewall

# Usage:
#   sudo bash deploy/setup_server.sh \
#     --app-dir /opt/aibackend \
#     --domain your.domain.or.ip \
#     --user ubuntu
#
# Notes:
# - Place your project files under the chosen --app-dir before starting services
#   (rsync or scp from your local machine).
# - Ensure an `.env` file exists at `$APP_DIR/.env` with required variables
#   (e.g., BRAVE_API_KEY, REDIS_HOST, OLLAMA_URL, etc.).

APP_DIR="/opt/aibackend"
DOMAIN_OR_IP=""
RUN_USER="${SUDO_USER:-${USER}}"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --app-dir)
      APP_DIR="$2"; shift 2;;
    --domain)
      DOMAIN_OR_IP="$2"; shift 2;;
    --user)
      RUN_USER="$2"; shift 2;;
    *)
      echo "Unknown argument: $1"; exit 1;;
  esac
done

echo "==> Settings"
echo "APP_DIR     : $APP_DIR"
echo "DOMAIN_OR_IP: ${DOMAIN_OR_IP:-(none)}"
echo "RUN_USER    : $RUN_USER"

echo "==> Updating apt and installing base packages"
apt-get update -y
apt-get upgrade -y
apt-get install -y \
  git curl ca-certificates \
  python3 python3-venv python3-pip \
  redis-server \
  nginx \
  ufw

echo "==> Configuring Redis to start on boot"
systemctl enable redis-server
systemctl start redis-server

echo "==> Creating app directory: $APP_DIR"
mkdir -p "$APP_DIR"
chown -R "$RUN_USER":"$RUN_USER" "$APP_DIR"

echo "==> Creating Python venv and installing requirements (if present)"
sudo -u "$RUN_USER" bash -c "cd '$APP_DIR' && python3 -m venv .venv"
source "$APP_DIR/.venv/bin/activate"
if [[ -f "$APP_DIR/requirements.txt" ]]; then
  pip install --upgrade pip
  pip install -r "$APP_DIR/requirements.txt"
else
  echo "WARNING: requirements.txt not found in $APP_DIR; skipping pip install"
fi

echo "==> Creating systemd service for API"
cat >/etc/systemd/system/aibackend-api.service <<EOF
[Unit]
Description=aibackend API (Uvicorn)
After=network.target

[Service]
User=$RUN_USER
WorkingDirectory=$APP_DIR
EnvironmentFile=$APP_DIR/.env
ExecStart=$APP_DIR/.venv/bin/uvicorn api.main:app --host 0.0.0.0 --port 8000
Restart=always

[Install]
WantedBy=multi-user.target
EOF

echo "==> Creating systemd service for Worker"
cat >/etc/systemd/system/aibackend-worker.service <<EOF
[Unit]
Description=aibackend Worker
After=network.target

[Service]
User=$RUN_USER
WorkingDirectory=$APP_DIR
EnvironmentFile=$APP_DIR/.env
ExecStart=$APP_DIR/.venv/bin/python worker/worker.py
Restart=always

[Install]
WantedBy=multi-user.target
EOF

echo "==> Reloading systemd and enabling services"
systemctl daemon-reload
systemctl enable aibackend-api aibackend-worker

echo "==> Creating Nginx site configuration"
NGINX_SITE="/etc/nginx/sites-available/aibackend"
cat > "$NGINX_SITE" <<EOF
server {
    listen 80;
    ${DOMAIN_OR_IP:+server_name $DOMAIN_OR_IP;}

    location / {
        proxy_pass http://127.0.0.1:8000;
        proxy_set_header Host \$host;
        proxy_set_header X-Real-IP \$remote_addr;
        proxy_set_header X-Forwarded-Proto \$scheme;
        proxy_set_header X-Forwarded-For \$proxy_add_x_forwarded_for;
    }

    location /static/ {
        alias $APP_DIR/api/static/;
    }
}
EOF

ln -sf "$NGINX_SITE" /etc/nginx/sites-enabled/aibackend
nginx -t
systemctl reload nginx

echo "==> Configuring UFW firewall (OpenSSH + Nginx Full)"
ufw allow OpenSSH
ufw allow 'Nginx Full'
yes | ufw enable || true
ufw status

echo "==> Startup instructions"
echo "Ensure your project files are present in $APP_DIR and .env exists."
echo "Then start services:"
echo "  systemctl start aibackend-api"
echo "  systemctl start aibackend-worker"
echo "Check status:"
echo "  systemctl status aibackend-api --no-pager"
echo "  systemctl status aibackend-worker --no-pager"
echo "Test locally on server: curl -s http://127.0.0.1:8000/health"
echo "If you set --domain, visit http://$DOMAIN_OR_IP/"

echo "==> Done"