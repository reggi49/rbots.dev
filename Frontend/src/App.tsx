import React from "react";
import { SafeAreaView, View, Text, Button, ScrollView, StyleSheet } from "react-native";
import useRobotBrain from "./hooks/useRobotBrain";
import { ROBOT_WS_URL } from "./config";

// Role: Minimal debug UI to view connection status, last transcript/answer and logs
// Responsibility:
// - Provide simple controls to connect/disconnect
// - Display latest events for debugging and development

export default function App() {
  const { connectionStatus, lastTranscript, lastAnswer, logs, connect, disconnect, testMic, testSpeak } = useRobotBrain();

  const statusColor = connectionStatus === "open" ? styles.green : styles.red;

  return (
    <SafeAreaView style={styles.container}>
      <View style={styles.header}>
        <Text style={styles.title}>Otak Robot Belajar (Debug)</Text>
        <Text>WS: <Text style={styles.mono}>{ROBOT_WS_URL}</Text></Text>
        <Text>Status: <Text style={[styles.status, statusColor]}>{connectionStatus}</Text></Text>
        <View style={styles.buttons}>
          <Button title="Connect to Robot" onPress={connect} />
          <View style={{ width: 12 }} />
          <Button title="Disconnect" onPress={disconnect} color="#aa3333" />
        </View>
        <View style={{ height: 12 }} />
        <View style={styles.buttons}>
          <Button title="Test Mic STT" onPress={() => testMic()} />
          <View style={{ width: 12 }} />
          <Button title="Test TTS" onPress={() => testSpeak('Halo, ini uji suara dari HP')} />
        </View>
      </View>

      <View style={styles.section}>
        <Text style={styles.sectionTitle}>Last Transcript</Text>
        <Text style={styles.card}>{lastTranscript ?? "-"}</Text>
      </View>

      <View style={styles.section}>
        <Text style={styles.sectionTitle}>Last Answer</Text>
        <Text style={styles.card}>{lastAnswer ?? "-"}</Text>
      </View>

      <View style={styles.section}>
        <Text style={styles.sectionTitle}>Logs</Text>
        <ScrollView style={styles.logPanel}>
          {logs.map((l, idx) => (
            <Text key={idx} style={styles.logLine}>{l}</Text>
          ))}
        </ScrollView>
      </View>
    </SafeAreaView>
  );
}

const styles = StyleSheet.create({
  container: { flex: 1, backgroundColor: "#fff" },
  header: { padding: 16 },
  title: { fontSize: 18, fontWeight: "600", marginBottom: 8 },
  mono: { fontFamily: "monospace" as any },
  status: { fontWeight: "700" },
  green: { color: "green" },
  red: { color: "red" },
  buttons: { flexDirection: "row", marginTop: 8 },
  section: { paddingHorizontal: 16, marginTop: 12 },
  sectionTitle: { fontWeight: "600", marginBottom: 6 },
  card: { backgroundColor: "#f4f4f4", padding: 8, borderRadius: 6 },
  logPanel: { maxHeight: 220, backgroundColor: "#111", padding: 8, borderRadius: 6 },
  logLine: { color: "#ddd", fontSize: 12, marginBottom: 4 },
});
