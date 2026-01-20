import { RobotClient } from "./ws/RobotClient";
import {
  RobotToPhoneMessage,
  AudioChunkMessage,
} from "./ws/types";
import AudioBufferAccumulator from "./audio/AudioBuffer";
import { SpeechToTextEngine } from "./audio/SpeechToText";
import { TextToSpeechEngine } from "./audio/TextToSpeech";
import { LlamaEngine } from "./llm/LlamaEngine";
import { buildTutorPrompt } from "./llm/PromptBuilder";

// Role: The Brain pipeline orchestrates audio → STT → LLM → TTS → reply
// Responsibility:
// - Collect audio chunks per session using AudioBufferAccumulator
// - When a session ends, transcribe audio, build prompt, get an LLM answer, synthesize audio, and reply to robot
// - Handle ping messages by replying with pong
// Notes/TODO:
// - This class is designed to be testable and to run headless. It accepts interfaces for STT/TTS/LLM.

export type BrainCallbacks = {
  onTranscript?: (sessionId: string, transcript: string) => void;
  onAnswer?: (sessionId: string, answer: string) => void;
  onLog?: (msg: string) => void;
};

export class BrainPipeline {
  private audioBuffer = new AudioBufferAccumulator();
  private robotClient: RobotClient;
  private stt: SpeechToTextEngine;
  private tts: TextToSpeechEngine;
  private llm: LlamaEngine;
  private callbacks: BrainCallbacks;

  constructor(
    robotClient: RobotClient,
    stt: SpeechToTextEngine,
    tts: TextToSpeechEngine,
    llm: LlamaEngine,
    callbacks: BrainCallbacks = {}
  ) {
    this.robotClient = robotClient;
    this.stt = stt;
    this.tts = tts;
    this.llm = llm;
    this.callbacks = callbacks;
  }

  private log(msg: string) {
    console.log(`[BrainPipeline] ${msg}`);
    this.callbacks.onLog?.(msg);
  }

  async handleMessage(msg: RobotToPhoneMessage): Promise<void> {
    if (msg.type === "audio_chunk") {
      const a = msg as AudioChunkMessage;
      this.log(`received audio chunk session=${a.sessionId} chunkId=${a.chunkId} isLast=${!!a.isLast}`);
      this.audioBuffer.addChunk(a.sessionId, a.data);
      if (a.isLast) {
        this.audioBuffer.markEnd(a.sessionId);
        const combined = this.audioBuffer.getCombinedPcm(a.sessionId);
        if (!combined) {
          this.log(`no audio for session ${a.sessionId}`);
          return;
        }
        // 1) STT
        let transcript = "";
        try {
          transcript = await this.stt.transcribePcm16(combined);
          this.log(`transcript: ${transcript}`);
          this.callbacks.onTranscript?.(a.sessionId, transcript);
        } catch (e) {
          this.log(`stt failed: ${String(e)}`);
        }

        // 2) Build prompt
        const prompt = buildTutorPrompt(transcript);

        // 3) LLM
        let answer = "";
        try {
          answer = await this.llm.generateAnswer(prompt);
          this.log(`answer: ${answer}`);
          this.callbacks.onAnswer?.(a.sessionId, answer);
        } catch (e) {
          this.log(`llm failed: ${String(e)}`);
        }

        // 4) TTS synthesize to base64 for robot (optional placeholder)
        let audioBase64 = "";
        try {
          if (this.tts.synthesizeToBase64) {
            audioBase64 = await this.tts.synthesizeToBase64(answer);
          }
        } catch (e) {
          this.log(`tts synth failed: ${String(e)}`);
        }

        // 5) Send reply back to robot
        try {
          this.robotClient.sendReply(a.sessionId, answer, audioBase64);
          this.log(`sent reply to robot session=${a.sessionId}`);
        } catch (e) {
          this.log(`failed to send reply: ${String(e)}`);
        }

        // 6) Optionally speak on phone as well
        try {
          await this.tts.speak(answer);
        } catch (e) {
          this.log(`tts speak failed: ${String(e)}`);
        }

        // 7) clear buffer
        this.audioBuffer.clear(a.sessionId);
      }
    } else if (msg.type === "ping") {
      this.log(`received ping ts=${msg.ts}`);
      this.robotClient.sendPong(msg.ts);
      this.log("sent pong");
    } else {
      this.log(`received unhandled message type=${(msg as any).type}`);
    }
  }
}

export default BrainPipeline;
