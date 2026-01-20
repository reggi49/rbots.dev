Rabot — Otak Robot Belajar
=================================

This project is the React Native (TypeScript) "brain" for an ESP32-C3 robot.

Overview
--------
- Android app (React Native, TypeScript) connects to the robot over WebSocket.
- Robot streams PCM16 audio chunks to the phone; phone transcribes (STT) → LLM → TTS → sends reply back to robot.
- The repository contains a JS/TS skeleton and placeholder implementations for STT/TTS/LLM.

Where to place the model
------------------------
If you later add an on-device LLM (GGUF model), place it here:

  models/phi3-mini-q2.gguf

Do NOT commit large model files to the repo. `.gitignore` already excludes `models/*.gguf`.

Quick start
-----------
1) Install dependencies

    cd /Users/reggi/Downloads/rabot
    npm install

2) TypeScript check

    npx tsc --noEmit

3) Start Metro

    npx react-native start --reset-cache

4) Run on Android (device/emulator ready)

    npx react-native run-android

Notes & tips
-----------
- The app entry is `src/App.tsx`.
- `index.js` includes runtime polyfills for `atob`/`btoa`/`Buffer`. If you add or remove polyfill packages, update `package.json`.
- `src/audio/AudioBuffer.ts` expects base64 PCM16LE audio; ensure the robot sends `pcm16-mono-16k` base64 chunks.
- STT/TTS/LLM are dummy implementations in `src/audio` and `src/llm`. See `// TODO` comments for integration points.

Cleaning up the temporary template
---------------------------------
This repo previously contained a template folder `rabottmp/`. After you verify the project, you can remove or rename it. Example commands:

    # rename to keep backup
    mv rabottmp rabottmp.orig

    # or remove entirely (only when you're sure)
    rm -rf rabottmp

If you accidentally committed `node_modules` or model files, follow the guidance in the repository docs or run:

    # remove node_modules from git index (keeps local folder)
    git rm -r --cached node_modules
    git commit -m "Remove node_modules from repo"

Next steps
----------
- Want help integrating `react-native-tts`, `react-native-voice`, or on-device LLM (llama.cpp) build steps? I can provide an integration plan and code examples.
