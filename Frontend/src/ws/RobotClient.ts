import { RobotToPhoneMessage, PhoneToRobotMessage, parseRobotToPhone } from "./types";

// Role: WebSocket client wrapper for Robot <-> Phone communication.
// Responsibility:
// - Manage a single WebSocket connection to the robot.
// - Reconnect with backoff when connection drops.
// - Parse incoming JSON safely and forward validated messages via onMessage callback.
// - Provide convenience methods to send reply/pong messages.
// Notes/TODO:
// - Uses the global WebSocket provided by React Native runtime.
// - For production consider adding binary support, TLS (wss), authentication, and better error handling.

type ConnectionStatus = "connecting" | "open" | "closed" | "error";

export type RobotClientOptions = {
  onMessage: (msg: RobotToPhoneMessage) => void;
  onConnectionChange: (status: ConnectionStatus) => void;
  onError?: (err: Error) => void;
};

export class RobotClient {
  private ws: WebSocket | null = null;
  private url: string;
  private reconnectAttempts = 0;
  private maxReconnectDelay = 10000; // ms
  private options: RobotClientOptions;
  private manuallyClosed = false;

  constructor(url: string, options: RobotClientOptions) {
    this.url = url;
    this.options = options;
  }

  connect() {
    this.manuallyClosed = false;
    this.setStatus("connecting");
    this.openWebSocket();
  }

  disconnect() {
    this.manuallyClosed = true;
    if (this.ws) {
      this.ws.close();
      this.ws = null;
    }
    this.setStatus("closed");
  }

  private setStatus(s: ConnectionStatus) {
    console.log(`[RobotClient] status=${s} url=${this.url}`);
    this.options.onConnectionChange(s);
  }

  private openWebSocket() {
    console.log(`[RobotClient] connecting to ${this.url}`);
    try {
      this.ws = new WebSocket(this.url);
    } catch (err) {
      console.warn("[RobotClient] WebSocket constructor failed", err);
      this.scheduleReconnect();
      return;
    }

    this.ws.onopen = () => {
      console.log("[RobotClient] open");
      this.reconnectAttempts = 0;
      this.setStatus("open");
    };

    this.ws.onmessage = (ev) => {
      const text = typeof ev.data === "string" ? ev.data : null;
      if (!text) {
        console.warn("[RobotClient] received non-text message (not handled in skeleton)");
        return;
      }
      const parsed = parseRobotToPhone(text);
      if (parsed) {
        try {
          this.options.onMessage(parsed);
        } catch (err) {
          console.error("[RobotClient] onMessage threw", err);
        }
      } else {
        console.warn("[RobotClient] received message that failed validation");
      }
    };

    this.ws.onerror = (ev: any) => {
      console.warn("[RobotClient] error", ev?.message || ev);
      this.setStatus("error");
      if (this.options.onError) this.options.onError(new Error(ev?.message || "WebSocket error"));
      // Note: onclose will be called after onerror in many implementations
    };

    this.ws.onclose = (ev) => {
      console.log("[RobotClient] closed", ev?.code, ev?.reason);
      this.ws = null;
      if (!this.manuallyClosed) {
        this.scheduleReconnect();
      } else {
        this.setStatus("closed");
      }
    };
  }

  private scheduleReconnect() {
    this.reconnectAttempts += 1;
    const delays = [1000, 2000, 5000, 10000];
    const idx = Math.min(this.reconnectAttempts - 1, delays.length - 1);
    const delay = delays[idx];
    console.log(`[RobotClient] reconnecting in ${delay}ms (attempt ${this.reconnectAttempts})`);
    this.setStatus("connecting");
    setTimeout(() => {
      if (!this.manuallyClosed) this.openWebSocket();
    }, delay);
  }

  send(message: PhoneToRobotMessage) {
    const json = JSON.stringify(message);
    if (this.ws && this.ws.readyState === WebSocket.OPEN) {
      this.ws.send(json);
    } else {
      console.warn("[RobotClient] send failed: socket not open. message=", message);
    }
  }

  sendReply(sessionId: string, text: string, audioBase64?: string) {
    const msg: PhoneToRobotMessage = {
      type: "reply",
      sessionId,
      text,
      audio: audioBase64,
    };
    this.send(msg);
  }

  sendPong(ts: number) {
    const msg: PhoneToRobotMessage = { type: "pong", ts } as any;
    this.send(msg);
  }
}

export default RobotClient;
