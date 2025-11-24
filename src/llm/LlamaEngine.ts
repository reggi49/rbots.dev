// Role: LLM engine abstraction and dummy implementation
// Responsibility:
// - Define `LlamaEngine` interface used by BrainPipeline.
// - Provide `DummyLlamaEngine` that returns a simple tutor-style reply.
// Notes/TODO:
// - Later we will integrate on-device models via llama.cpp and a GGUF model (e.g. Phi-3 Mini q2).
// - Model path (informational): ../models/phi3-mini-q2.gguf

export interface LlamaEngine {
  generateAnswer(prompt: string): Promise<string>;
}

export class DummyLlamaEngine implements LlamaEngine {
  async generateAnswer(prompt: string): Promise<string> {
    // Simple canned reply that includes the prompt for clarity during development.
    return Promise.resolve(`Ini jawaban tutor dummy berdasarkan prompt: ${prompt}`);
  }
}

export default DummyLlamaEngine;
