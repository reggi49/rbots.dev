// Role: Prompt builder to format user transcripts into LLM prompts suited for children
// Responsibility:
// - Build a succinct prompt asking the LLM to respond in simple language suitable for SD–SMP students.
// - Keep responses short, kind, and explanatory.

export function buildTutorPrompt(userText: string): string {
  // Wrap the user's transcript into a prompt with explicit instructions for style and length.
  return `You are a friendly tutor for elementary/middle school students. Explain things simply and briefly. Use easy words and an encouraging tone. Keep the answer short (1-3 sentences) and avoid technical jargon.

User asked: "${userText}"

Answer:`;
}

export default buildTutorPrompt;
