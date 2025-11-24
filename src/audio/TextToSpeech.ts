// Role: Text-to-Speech abstraction and a native stub implementation
// Responsibility:
// - Define `TextToSpeechEngine` used by BrainPipeline for speaking and optional synthesis to base64.
// - Provide `NativeTtsEngine` stub that currently logs and resolves.
// Notes/TODO:
// - For real TTS on Android use `react-native-tts` or implement a native bridge to Android TextToSpeech.
// - For sending audio to the robot we will need `synthesizeToBase64` to produce WAV/PCM data encoded in base64.

export interface TextToSpeechEngine {
  speak(text: string): Promise<void>;
  synthesizeToBase64?(text: string): Promise<string>;
}

export class NativeTtsEngine implements TextToSpeechEngine {
  async speak(text: string): Promise<void> {
    // Stub: in development just log. Replace with react-native-tts calls.
    console.log("[TTS] speak:", text);
    return Promise.resolve();
  }

  async synthesizeToBase64(_text: string): Promise<string> {
    // TODO: Implement synthesis to WAV/PCM and return base64 string.
    // This placeholder returns empty string meaning 'no audio provided'.
    console.log("[TTS] synthesizeToBase64 placeholder");
    return Promise.resolve("");
  }
}

export default NativeTtsEngine;
