#!/bin/bash
WAV_FILE="/Users/reggi/Downloads/rbot/board/debug_erbot.wav"

echo "=== Audio File Info ==="
ls -lh "$WAV_FILE"
file "$WAV_FILE"

echo -e "\n=== Playing Audio ==="
afplay "$WAV_FILE"
echo "✅ Playback complete!"
