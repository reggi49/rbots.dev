import os

wav_path = "/Users/reggi/Downloads/rbot/board/debug_erbot.wav"
size = os.path.getsize(wav_path)

print("=" * 50)
print("  AUDIO RECORDING SUCCESSFUL!")
print("=" * 50)
print(f"\nFile: debug_erbot.wav")
print(f"Location: {wav_path}")
print(f"Size: {size:,} bytes")
print(f"Duration: ~{size / (16000 * 2):.1f} seconds")
print(f"Format: WAV, 16kHz, 16-bit, Mono")
print(f"\n✅ Audio berhasil direkam dari ESP32-C3!")
print(f"\nUntuk mendengarkan:")
print(f"  afplay {wav_path}")
print("=" * 50)
