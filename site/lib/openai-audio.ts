// One-call voice reply: the recording goes in as audio, the spoken answer streams back as audio
// plus a text transcript of the answer. Replaces transcribe -> LLM -> TTS with a single request.
// Uses OPENAI_TTS_API_KEY directly (the AI Gateway doesn't serve OpenAI's audio chat models).
import OpenAI from "openai";

import { DEVICE_RATE, OPENAI_TTS_RATE, StreamingResampler, pcmToWav } from "./audio.ts";
import { SYSTEM } from "./claude-reply.ts";
import { SpeechError } from "./openai-speech.ts";

// Tried in order until one is accepted; OPENAI_AUDIO_MODEL (if set) goes first.
const CANDIDATE_MODELS = ["gpt-audio-mini", "gpt-audio", "gpt-4o-mini-audio-preview", "gpt-4o-audio-preview"];

const AUDIO_SYSTEM = `${SYSTEM}
The user's question arrives as audio. Answer it out loud.`;

let client: OpenAI | null = null;
function openai(): OpenAI {
  const apiKey = Netlify.env.get("OPENAI_TTS_API_KEY");
  if (!apiKey) throw new SpeechError("OPENAI_TTS_API_KEY is not set on this site", 500, "");
  // baseURL is explicit: the AI Gateway injects OPENAI_BASE_URL, which would send this key to it.
  client ??= new OpenAI({ apiKey, baseURL: "https://api.openai.com/v1" });
  return client;
}

function isModelRejection(e: unknown): boolean {
  if (!(e instanceof OpenAI.APIError)) return false;
  if (e.status === 404) return true;
  const msg = `${e.message} ${JSON.stringify(e.error ?? "")}`.toLowerCase();
  return e.status === 400 && (msg.includes("model") || msg.includes("audio"));
}

export interface AudioReplyHandlers {
  onAudio: (pcm16k: Uint8Array<ArrayBuffer>) => void;
  onText: (delta: string) => void;
}

/** Streams a spoken reply to recorded speech. Resolves with the model that answered. */
export async function audioReply(pcm16k: Uint8Array, handlers: AudioReplyHandlers, voice = "alloy"): Promise<string> {
  const wavB64 = Buffer.from(pcmToWav(pcm16k, DEVICE_RATE)).toString("base64");
  const preferred = Netlify.env.get("OPENAI_AUDIO_MODEL");
  const models = preferred ? [preferred, ...CANDIDATE_MODELS.filter((m) => m !== preferred)] : CANDIDATE_MODELS;

  let lastError: unknown;
  for (const model of models) {
    let stream;
    try {
      stream = await openai().chat.completions.create({
        model,
        modalities: ["text", "audio"],
        audio: { voice, format: "pcm16" }, // 24 kHz s16le mono
        stream: true,
        messages: [
          { role: "system", content: AUDIO_SYSTEM },
          { role: "user", content: [{ type: "input_audio", input_audio: { data: wavB64, format: "wav" } }] },
        ],
      });
    } catch (e) {
      lastError = e;
      if (isModelRejection(e)) {
        console.warn(`audio model ${model} rejected, trying next`, (e as Error).message);
        continue;
      }
      throw e;
    }

    const resampler = new StreamingResampler(OPENAI_TTS_RATE, DEVICE_RATE);
    for await (const chunk of stream) {
      const audio = chunk.choices[0]?.delta?.audio;
      if (audio?.transcript) handlers.onText(audio.transcript);
      if (audio?.data) {
        const out = resampler.pushBytes(Buffer.from(audio.data, "base64"));
        if (out.length) handlers.onAudio(out);
      }
    }
    const tail = resampler.flush();
    if (tail.length) handlers.onAudio(tail);
    return model;
  }
  throw lastError ?? new Error("no audio model available");
}
