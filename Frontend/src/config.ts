// Role: Configuration for Robot WebSocket and related defaults.
// Responsibility:
// - Provide a default WebSocket URL used to connect to the ESP32-C3 robot.
// - Allow updating the URL at runtime via `setRobotWsUrl`.
// TODO:
// - Wire this to persistent storage (AsyncStorage) and a UI to edit the URL.

export let ROBOT_WS_URL = "ws://192.168.4.1:8080/robot";

export function setRobotWsUrl(url: string) {
  ROBOT_WS_URL = url;
}

export default {
  ROBOT_WS_URL,
  setRobotWsUrl,
};
