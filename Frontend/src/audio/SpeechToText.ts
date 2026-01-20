// Role: Speech-to-Text abstraction and dummy implementation
// Responsibility:
// - Define a `SpeechToTextEngine` interface used by BrainPipeline.
// - Provide a `DummySpeechToTextEngine` that returns a canned transcript.
// TODO:
// - Integrate Android SpeechRecognizer (react-native-voice) or an offline model (Whisper) behind this interface.

export interface SpeechToTextEngine {
  transcribePcm16(buffer: Int16Array): Promise<string>;
  transcribeFromMic?(): Promise<string>;
}

export class DummySpeechToTextEngine implements SpeechToTextEngine {
  async transcribePcm16(_buffer: Int16Array): Promise<string> {
    // Dummy implementation: return fixed transcript.
    // Replace with real STT engine: Whisper, Android SpeechRecognizer, etc.
    return Promise.resolve("dummy transcript from audio");
  }

  async transcribeFromMic(): Promise<string> {
    return Promise.resolve("dummy transcript from mic");
  }
}

// Optional runtime adapter for `react-native-voice` (Android SpeechRecognizer)
// This adapter uses dynamic require so TypeScript/Metro won't fail if the
// native module is not installed. To enable, install:
//   npm install react-native-voice
// then rebuild the app (autolinking will pick up the native module).
// Notes:
// - `react-native-voice` provides live microphone streaming STT via OS.
// - This adapter implements only `transcribeFromMic()`; it does not support
//   `transcribePcm16` (audio chunk transcription) — for that you will need
//   an offline model (Whisper) or a server-side STT.

export class ReactNativeVoiceSpeechToTextEngine implements SpeechToTextEngine {
  private Voice: any;

  constructor() {
    try {
      // dynamic require to avoid hard dependency at build time
      // package name: 'react-native-voice'
      // usage: Voice.onSpeechResults = handler; Voice.start(locale)
      // See: https://github.com/react-native-voice/voice
      // Support both CommonJS and transpiled ES module shapes:
      // - some bundlers return the default export as `module.exports`
      // - others put it on `module.exports.default`
      // prefer the exported instance when available.
      // eslint-disable-next-line @typescript-eslint/no-var-requires
      const mod = require('react-native-voice');
      this.Voice = (mod && mod.default) ? mod.default : mod;
    } catch (e) {
      this.Voice = null;
      console.warn('[SpeechToText] react-native-voice not installed');
    }
  }

  async transcribePcm16(_buffer: Int16Array): Promise<string> {
    // Not supported by react-native-voice (which listens to mic). Return empty.
    return Promise.resolve('');
  }

  transcribeFromMic(): Promise<string> {
    if (!this.Voice) return Promise.resolve('');

    return new Promise((resolve, reject) => {
      let done = false;
      const onResults = (e: any) => {
        if (done) return;
        const results = e?.value || e?.results || [];
        done = true;
        cleanup();
        resolve(results.join(' '));
      };

      const onError = (err: any) => {
        if (done) return;
        done = true;
        cleanup();
        // Log full error object for easier debugging
        console.error('[SpeechToText] transcribeFromMic error:', err);
        reject(err);
      };

      const cleanup = () => {
        try {
          // react-native-voice exposes event setters; remove listeners if available
          if (this.Voice && this.Voice.destroy) {
            this.Voice.destroy();
          } else if (this.Voice && this.Voice.removeAllListeners) {
            this.Voice.removeAllListeners();
          }
        } catch (e) {
          // ignore
        }
      };

      try {
        // react-native-voice commonly uses global event handlers; adapt accordingly
        if (!this.Voice) throw new Error('react-native-voice module not available');
        if (typeof this.Voice.start !== 'function') {
          throw new Error('react-native-voice.start is not a function; wrong import shape or module not linked');
        }

        this.Voice.onSpeechResults = onResults;
        this.Voice.onSpeechError = onError;
        // start listening with default locale
        this.Voice.start('en-US');
      } catch (e) {
        cleanup();
        reject(e);
      }
    });
  }
}

export default DummySpeechToTextEngine;
