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

export default DummySpeechToTextEngine;
