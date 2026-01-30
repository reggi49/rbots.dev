#!/usr/bin/env bash
set -e

# Commit message bisa dikirim sebagai argumen:
# ./deploy_local.sh "update worker logic"
MSG=${1:-"deploy from VS Code"}

echo "[deploy] git add -A"
git add -A

# Cek apakah ada perubahan
if git diff --cached --quiet; then
  echo "[deploy] Tidak ada perubahan untuk di-commit."
else
  echo "[deploy] Commit dengan pesan: $MSG"
  git commit -m "$MSG"
fi

echo "[deploy] Push ke droplet..."
git push droplet main

echo "[deploy] Selesai."