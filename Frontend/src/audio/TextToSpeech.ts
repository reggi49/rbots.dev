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

// Optional runtime adapter for `react-native-tts`.
// To enable, install:
//   npm install react-native-tts
// then rebuild the app. This adapter calls the native TTS engine to speak text.
// `react-native-tts` does not directly provide synthesize-to-base64; for
// that you would need a custom native bridge to Android TextToSpeech.synthesizeToFile
// or use a separate library that writes files and returns base64.

export class ReactNativeTtsEngine implements TextToSpeechEngine {
  private Tts: any;

  constructor() {
    try {
      // Support both CommonJS and transpiled ES module shapes for react-native-tts.
      // eslint-disable-next-line @typescript-eslint/no-var-requires
      const mod = require('react-native-tts');
      this.Tts = (mod && mod.default) ? mod.default : mod;
    } catch (e) {
      this.Tts = null;
      console.warn('[TextToSpeech] react-native-tts not installed');
    }
  }

  async speak(text: string): Promise<void> {
    if (!this.Tts) {
      console.log('[TTS] speak (fallback):', text);
      return Promise.resolve();
    }
    return new Promise((resolve) => {
      try {
        if (typeof this.Tts.speak !== 'function') {
          throw new Error('react-native-tts.speak is not a function; wrong import shape or native module not linked');
        }
        this.Tts.speak(text);
        // react-native-tts does not provide a callback by default; resolve immediately
        resolve();
      } catch (e) {
        console.warn('[TTS] speak failed', e);
        resolve();
      }
    });
  }

  async synthesizeToBase64(_text: string): Promise<string> {
    // Placeholder: implementing synthesize-to-base64 requires a native bridge
    // or writing a WAV file then encoding it. Return empty string for now.
    return Promise.resolve('');
  }
}
