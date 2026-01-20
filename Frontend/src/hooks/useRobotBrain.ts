import { useEffect, useRef, useState } from "react";
import { PermissionsAndroid, Platform } from "react-native";
import RobotClient from "../ws/RobotClient";
import { ROBOT_WS_URL } from "../config";
import DummySpeechToTextEngine, { ReactNativeVoiceSpeechToTextEngine } from "../audio/SpeechToText";
import NativeTtsEngine, { ReactNativeTtsEngine } from "../audio/TextToSpeech";
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
  const sttRef = useRef<any>(null);
  const ttsRef = useRef<any>(null);

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

    // Try native engines first (they use dynamic require internally).
    let stt: any;
    try {
      stt = new ReactNativeVoiceSpeechToTextEngine();
      // if native module not present, the adapter sets internal handler to null
      if (!((stt as any).Voice)) {
        stt = new DummySpeechToTextEngine();
      }
    } catch (e) {
      stt = new DummySpeechToTextEngine();
    }

    let tts: any;
    try {
      tts = new ReactNativeTtsEngine();
      if (!((tts as any).Tts)) {
        tts = new NativeTtsEngine();
      }
    } catch (e) {
      tts = new NativeTtsEngine();
    }
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
    sttRef.current = stt;
    ttsRef.current = tts;

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

  // Request microphone permission on Android
  async function requestAudioPermission(): Promise<boolean> {
    if (Platform.OS !== 'android') return true;
    try {
      const granted = await PermissionsAndroid.request(
        PermissionsAndroid.PERMISSIONS.RECORD_AUDIO,
        {
          title: 'Microphone Permission',
          message: 'App needs access to your microphone to transcribe audio',
          buttonNeutral: 'Ask Me Later',
          buttonNegative: 'Cancel',
          buttonPositive: 'OK',
        }
      );
      return granted === PermissionsAndroid.RESULTS.GRANTED;
    } catch (err) {
      console.warn('requestAudioPermission failed', err);
      return false;
    }
  }

  // Debug helper: start mic STT and update lastTranscript
  async function testMic() {
    const ok = await requestAudioPermission();
    if (!ok) {
      appendLog('Microphone permission denied');
      return;
    }
    const engine = sttRef.current;
    if (!engine || !engine.transcribeFromMic) {
      appendLog('No STT engine available for microphone');
      return;
    }
    appendLog('Starting mic STT...');
    try {
      const txt = await engine.transcribeFromMic();
      setLastTranscript(txt);
      appendLog(`Mic STT result: ${txt}`);
    } catch (e) {
      appendLog(`Mic STT failed: ${String(e)}`);
    }
  }

  // Debug helper: speak a sample text via TTS engine
  async function testSpeak(sampleText = 'Halo, ini uji suara') {
    const engine = ttsRef.current;
    if (!engine || !engine.speak) {
      appendLog('No TTS engine available');
      return;
    }
    appendLog(`TTS speak: ${sampleText}`);
    try {
      await engine.speak(sampleText);
    } catch (e) {
      appendLog(`TTS speak failed: ${String(e)}`);
    }
  }

  return {
    connectionStatus,
    lastTranscript,
    lastAnswer,
    logs,
    connect,
    disconnect,
    testMic,
    testSpeak,
  } as const;
}

export default useRobotBrain;
