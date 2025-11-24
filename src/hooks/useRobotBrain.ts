import { useEffect, useRef, useState } from "react";
import RobotClient from "../ws/RobotClient";
import { ROBOT_WS_URL } from "../config";
import DummySpeechToTextEngine from "../audio/SpeechToText";
import NativeTtsEngine from "../audio/TextToSpeech";
import DummyLlamaEngine from "../llm/LlamaEngine";
import BrainPipeline from "../BrainPipeline";

// Role: React hook to manage Robot connection and BrainPipeline state for UI
// Responsibility:
// - Initialize RobotClient and BrainPipeline once
// - Expose connection status, last transcript, last answer, logs
// - Expose connect/disconnect functions
// Notes/TODO:
// - In a real app, engines would be injected from context or configured in UI.

type ConnectionStatus = "connecting" | "open" | "closed" | "error";

export function useRobotBrain() {
  const [connectionStatus, setConnectionStatus] = useState<ConnectionStatus>("closed");
  const [lastTranscript, setLastTranscript] = useState<string | null>(null);
  const [lastAnswer, setLastAnswer] = useState<string | null>(null);
  const [logs, setLogs] = useState<string[]>([]);

  const clientRef = useRef<RobotClient | null>(null);
  const pipelineRef = useRef<BrainPipeline | null>(null);

  useEffect(() => {
    // Initialize client and pipeline once
    if (clientRef.current) return;

    const client = new RobotClient(ROBOT_WS_URL, {
      onMessage: async (msg) => {
        pipelineRef.current?.handleMessage(msg);
      },
      onConnectionChange: (s) => {
        setConnectionStatus(s as ConnectionStatus);
        appendLog(`connection=${s}`);
      },
      onError: (e) => {
        appendLog(`error: ${String(e)}`);
      },
    });

    const stt = new DummySpeechToTextEngine();
    const tts = new NativeTtsEngine();
    const llm = new DummyLlamaEngine();

    const pipeline = new BrainPipeline(client, stt, tts, llm, {
      onTranscript: (sessionId, transcript) => {
        setLastTranscript(transcript);
        appendLog(`transcript [${sessionId}]=${transcript}`);
      },
      onAnswer: (sessionId, answer) => {
        setLastAnswer(answer);
        appendLog(`answer [${sessionId}]=${answer}`);
      },
      onLog: (m) => appendLog(m),
    });

    clientRef.current = client;
    pipelineRef.current = pipeline;

    return () => {
      client.disconnect();
      clientRef.current = null;
      pipelineRef.current = null;
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  function appendLog(entry: string) {
    setLogs((s) => [...s.slice(-200), `[${new Date().toLocaleTimeString()}] ${entry}`]);
  }

  function connect() {
    const c = clientRef.current;
    if (!c) return appendLog("client not ready");
    c.connect();
  }

  function disconnect() {
    const c = clientRef.current;
    if (!c) return appendLog("client not ready");
    c.disconnect();
  }

  return {
    connectionStatus,
    lastTranscript,
    lastAnswer,
    logs,
    connect,
    disconnect,
  } as const;
}

export default useRobotBrain;
