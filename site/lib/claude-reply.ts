// Short spoken reply from Claude, via Netlify AI Gateway (credentials injected at runtime).
import Anthropic from "@anthropic-ai/sdk";

export const SYSTEM = `You are the voice of a small smart speaker on someone's desk. What you write is
converted to speech and played aloud, and every extra word adds waiting time before they hear you.
Answer in 25 words or fewer: usually one short sentence, at most two. Give the single most useful
fact or answer, not an overview; if they want more, they will ask. Use plain spoken language: no
markdown, lists, emoji, URLs or symbols that read badly aloud. If the request is unclear or was
probably mis-transcribed, say briefly what you heard and ask them to try again.`;

// Safety net if the model runs long: keep whole sentences up to MAX_WORDS, else hard-cut.
const MAX_WORDS = 40;
export function trimForSpeech(text: string): string {
  // Sentence break = . ! or ? (plus closing quotes) followed by whitespace and a new sentence,
  // so abbreviations like "p.m." or "e.g." mid-sentence don't split.
  const sentences = text.trim().split(/(?<=[.!?]["')\]]*)\s+(?=["'(]?[A-Z0-9])/);
  let out = "";
  for (const s of sentences.slice(0, 2)) {
    const next = (out + s).trim();
    if (out && next.split(/\s+/).length > MAX_WORDS) break;
    out = next + " ";
  }
  const words = out.trim().split(/\s+/);
  return words.length > MAX_WORDS ? words.slice(0, MAX_WORDS).join(" ") + "." : out.trim();
}

let client: Anthropic | null = null;

export function gatewayActive(): boolean {
  return Boolean(process.env.ANTHROPIC_BASE_URL || process.env.ANTHROPIC_API_KEY);
}

export async function reply(transcript: string): Promise<string> {
  client ??= new Anthropic(); // ANTHROPIC_API_KEY / ANTHROPIC_BASE_URL from the gateway
  const message = await client.messages.create({
    model: process.env.CLAUDE_MODEL ?? "claude-opus-5-5",
    max_tokens: 1024,
    output_config: { effort: "low" }, // latency matters more than depth for a spoken one-liner
    system: SYSTEM,
    messages: [{ role: "user", content: transcript }],
  });

  if (message.stop_reason === "refusal") return "Sorry, I can't help with that one.";
  const text = message.content
    .filter((b): b is Anthropic.TextBlock => b.type === "text")
    .map((b) => b.text)
    .join(" ")
    .trim();
  return text ? trimForSpeech(text) : "Sorry, I don't have an answer for that.";
}

// Same sentence-break rule as trimForSpeech: punctuation + whitespace + start of a new sentence.
const SENTENCE_END = /[.!?]["')\]]*\s+(?=["'(]?[A-Z0-9])/;

/**
 * Streams the reply and yields one sentence at a time as soon as it's complete, so speech for
 * sentence 1 can start while Claude is still writing sentence 2. Applies the same caps as
 * trimForSpeech (2 sentences, MAX_WORDS) and stops the stream once they're hit.
 */
export async function* replySentences(transcript: string): AsyncGenerator<string> {
  client ??= new Anthropic();
  const stream = client.messages.stream({
    model: process.env.CLAUDE_MODEL ?? "claude-opus-5-5",
    max_tokens: 1024,
    output_config: { effort: "low" },
    system: SYSTEM,
    messages: [{ role: "user", content: transcript }],
  });
  // Errors still surface through the for-await below; this only stops the abort() we issue on
  // an early exit from becoming an unhandled rejection.
  stream.on("error", () => {});

  let pending = "";
  let sentences = 0;
  let words = 0;
  const emit = (s: string): string | null => {
    const text = s.trim();
    if (!text) return null;
    const n = text.split(/\s+/).length;
    if (sentences > 0 && words + n > MAX_WORDS) return null;
    sentences++;
    words += n;
    return n > MAX_WORDS ? text.split(/\s+/).slice(0, MAX_WORDS).join(" ") + "." : text;
  };

  try {
    for await (const event of stream) {
      if (event.type !== "content_block_delta" || event.delta.type !== "text_delta") continue;
      pending += event.delta.text;
      let m: RegExpExecArray | null;
      while ((m = SENTENCE_END.exec(pending))) {
        const sentence = emit(pending.slice(0, m.index + m[0].length));
        pending = pending.slice(m.index + m[0].length);
        if (sentence) yield sentence;
        if (!sentence || sentences >= 2) return;
      }
    }
    const final = await stream.finalMessage();
    if (final.stop_reason === "refusal") {
      if (sentences === 0) yield "Sorry, I can't help with that one.";
      return;
    }
    const last = emit(pending);
    if (last) yield last;
    else if (sentences === 0) yield "Sorry, I don't have an answer for that.";
  } finally {
    stream.abort(); // no-op if already finished; stops generation when we hit the cap early
  }
}
