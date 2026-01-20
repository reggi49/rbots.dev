// Role: Collect and combine PCM16 audio chunks sent from the robot
// Responsibility:
// - Accept base64 PCM16LE chunks per sessionId
// - Combine into a single Int16Array when stream ends
// Notes/TODO:
// - Assumes data is PCM16LE mono 16kHz
// - base64 decoding uses `atob` or Node Buffer if available. On Android/React-Native
//   ensure a base64 decoder is available or provide a polyfill.

// Provide minimal ambient declarations so TypeScript won't error during tsc.
// In a real React Native project you may want to add a proper polyfill
// (e.g. `globalThis.atob = require('base-64').decode`) or install `@types/node`
// and/or `buffer` for Node-style Buffer support.
declare function atob(input: string): string;
declare const Buffer: any;

type SessionState = {
  chunks: Int16Array[];
  totalLength: number;
  ended: boolean;
};

export class AudioBufferAccumulator {
  private sessions: Map<string, SessionState> = new Map();

  addChunk(sessionId: string, base64: string) {
    const pcm = this.base64ToInt16Array(base64);
    if (!pcm) return;
    let s = this.sessions.get(sessionId);
    if (!s) {
      s = { chunks: [], totalLength: 0, ended: false };
      this.sessions.set(sessionId, s);
    }
    s.chunks.push(pcm);
    s.totalLength += pcm.length;
  }

  markEnd(sessionId: string) {
    const s = this.sessions.get(sessionId);
    if (s) s.ended = true;
  }

  getCombinedPcm(sessionId: string): Int16Array | null {
    const s = this.sessions.get(sessionId);
    if (!s) return null;
    if (s.totalLength === 0) return new Int16Array(0);
    const out = new Int16Array(s.totalLength);
    let offset = 0;
    for (const chunk of s.chunks) {
      out.set(chunk, offset);
      offset += chunk.length;
    }
    return out;
  }

  clear(sessionId: string) {
    this.sessions.delete(sessionId);
  }

  private base64ToArrayBuffer(base64: string): ArrayBuffer {
    // Try atob (browser/react-native) first
    if (typeof (globalThis as any).atob === "function") {
      const binary = (globalThis as any).atob(base64);
      const len = binary.length;
      const bytes = new Uint8Array(len);
      for (let i = 0; i < len; i++) {
        bytes[i] = binary.charCodeAt(i);
      }
      return bytes.buffer;
    }

    // Try Node Buffer if present (e.g. jest/node)
    if (typeof (globalThis as any).Buffer !== "undefined") {
      return Uint8Array.from((globalThis as any).Buffer.from(base64, "base64")).buffer;
    }

    console.warn("[AudioBuffer] No base64 decoder found (atob/Buffer). Returning empty buffer.");
    return new ArrayBuffer(0);
  }

  private base64ToInt16Array(base64: string): Int16Array {
    const ab = this.base64ToArrayBuffer(base64);
    // Interpret as 16-bit little-endian PCM
    const view = new DataView(ab);
    const len = Math.floor(view.byteLength / 2);
    const out = new Int16Array(len);
    for (let i = 0; i < len; i++) {
      out[i] = view.getInt16(i * 2, true);
    }
    return out;
  }
}

export default AudioBufferAccumulator;
