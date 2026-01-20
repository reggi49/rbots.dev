// Role: WebSocket message types and simple guards
// Responsibility:
// - Define TypeScript types that describe messages between Robot (ESP32-C3) and Phone (React Native app).
// - Provide minimal parsing and type guard helpers to validate incoming JSON payloads.
// Notes / TODO:
// - Guards are intentionally lightweight. For production, consider using a schema validator (zod/io-ts) and stricter checks.

export type AudioChunkMessage = {
  type: "audio_chunk";
  sessionId: string;
  chunkId: number;
  isLast?: boolean;
  format: "pcm16-mono-16k";
  data: string; // base64 PCM16LE
};

export type PingMessage = {
  type: "ping";
  ts: number; // ms
};

export type TextAckMessage = {
  type: "text_ack";
  sessionId: string;
};

export type StartStreamMessage = {
  type: "start_stream";
  sessionId: string;
};

export type EndStreamMessage = {
  type: "end_stream";
  sessionId: string;
};

export type RobotToPhoneMessage =
  | AudioChunkMessage
  | PingMessage
  | TextAckMessage
  | StartStreamMessage
  | EndStreamMessage;

// Messages from Phone to Robot
export type ReplyMessage = {
  type: "reply";
  sessionId: string;
  text: string;
  audio?: string; // base64 (WAV/PCM) optional
};

export type PartialTextMessage = {
  type: "partial_text";
  sessionId: string;
  text: string;
  isFinal?: boolean;
};

export type PongMessage = {
  type: "pong";
  ts: number;
};

export type ErrorMessage = {
  type: "error";
  code: string;
  message: string;
};

export type PhoneToRobotMessage = ReplyMessage | PartialTextMessage | PongMessage | ErrorMessage;

// Minimal runtime guards
export function isObject(v: any): v is Record<string, any> {
  return v !== null && typeof v === "object";
}

export function isAudioChunkMessage(v: any): v is AudioChunkMessage {
  return (
    isObject(v) &&
    v.type === "audio_chunk" &&
    typeof v.sessionId === "string" &&
    typeof v.chunkId === "number" &&
    typeof v.format === "string" &&
    typeof v.data === "string"
  );
}

export function isPingMessage(v: any): v is PingMessage {
  return isObject(v) && v.type === "ping" && typeof v.ts === "number";
}

export function isTextAckMessage(v: any): v is TextAckMessage {
  return isObject(v) && v.type === "text_ack" && typeof v.sessionId === "string";
}

export function isStartStreamMessage(v: any): v is StartStreamMessage {
  return isObject(v) && v.type === "start_stream" && typeof v.sessionId === "string";
}

export function isEndStreamMessage(v: any): v is EndStreamMessage {
  return isObject(v) && v.type === "end_stream" && typeof v.sessionId === "string";
}

export function isRobotToPhoneMessage(v: any): v is RobotToPhoneMessage {
  if (!isObject(v) || typeof v.type !== "string") return false;
  switch (v.type) {
    case "audio_chunk":
      return isAudioChunkMessage(v);
    case "ping":
      return isPingMessage(v);
    case "text_ack":
      return isTextAckMessage(v);
    case "start_stream":
      return isStartStreamMessage(v);
    case "end_stream":
      return isEndStreamMessage(v);
    default:
      return false;
  }
}

export function isReplyMessage(v: any): v is ReplyMessage {
  return isObject(v) && v.type === "reply" && typeof v.sessionId === "string" && typeof v.text === "string";
}

export function isPartialTextMessage(v: any): v is PartialTextMessage {
  return isObject(v) && v.type === "partial_text" && typeof v.sessionId === "string" && typeof v.text === "string";
}

export function isPongMessage(v: any): v is PongMessage {
  return isObject(v) && v.type === "pong" && typeof v.ts === "number";
}

export function isErrorMessage(v: any): v is ErrorMessage {
  return isObject(v) && v.type === "error" && typeof v.code === "string" && typeof v.message === "string";
}

export function isPhoneToRobotMessage(v: any): v is PhoneToRobotMessage {
  if (!isObject(v) || typeof v.type !== "string") return false;
  switch (v.type) {
    case "reply":
      return isReplyMessage(v);
    case "partial_text":
      return isPartialTextMessage(v);
    case "pong":
      return isPongMessage(v);
    case "error":
      return isErrorMessage(v);
    default:
      return false;
  }
}

// Helper: parse JSON safely and return a RobotToPhoneMessage if valid
export function parseRobotToPhone(raw: string): RobotToPhoneMessage | null {
  try {
    const parsed = JSON.parse(raw);
    if (isRobotToPhoneMessage(parsed)) return parsed;
    console.warn("[ws/types] Received JSON but it is not a RobotToPhoneMessage", parsed);
    return null;
  } catch (e) {
    console.warn("[ws/types] Failed to parse JSON", e);
    return null;
  }
}
