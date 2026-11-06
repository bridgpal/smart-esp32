// OpenAI speech endpoints (transcription + TTS), called with OPENAI_TTS_API_KEY.
import {
  DEVICE_RATE,
  OPENAI_TTS_RATE,
  StreamingResampler,
  bytesToInt16,
  int16ToBytes,
  pcmToWav,
  resample,
} from "./audio.ts";

export class SpeechError extends Error {
  constructor(
    message: string,
    readonly status: number,
    readonly detail: string,
  ) {
    super(message);
  }
}

function apiKey(): string {
  const key = Netlify.env.get("OPENAI_TTS_API_KEY");
  if (!key) throw new SpeechError("OPENAI_TTS_API_KEY is not set on this site", 500, "");
  return key;
}

async function fail(what: string, res: Response): Promise<never> {
  const detail = (await res.text().catch(() => "")).slice(0, 300);
  console.error(`openai ${what} failed`, res.status, detail);
  throw new SpeechError(`openai ${what} returned ${res.status}`, 502, detail);
}

/** 16 kHz mono PCM from the device -> text. */
export async function transcribe(pcm16k: Uint8Array): Promise<string> {
  const form = new FormData();
  form.append("file", new Blob([pcmToWav(pcm16k, DEVICE_RATE)], { type: "audio/wav" }), "speech.wav");
  form.append("model", Netlify.env.get("OPENAI_STT_MODEL") ?? "gpt-4o-mini-transcribe");
  form.append("response_format", "json");
  const res = await fetch("https://api.openai.com/v1/audio/transcriptions", {
    method: "POST",
    headers: { Authorization: `Bearer ${apiKey()}` },
    body: form,
  });
  if (!res.ok) await fail("transcription", res);
  const data = (await res.json()) as { text?: string };
  return (data.text ?? "").trim();
}

/** Text -> 16 kHz PCM chunks as OpenAI generates them. Resolves when the sentence is done. */
export async function speakStream(text: string, onPcm: (chunk: Uint8Array<ArrayBuffer>) => void, voice = "alloy") {
  const res = await fetch("https://api.openai.com/v1/audio/speech", {
    method: "POST",
    headers: { Authorization: `Bearer ${apiKey()}`, "Content-Type": "application/json" },
    body: JSON.stringify({
      model: Netlify.env.get("OPENAI_TTS_MODEL") ?? "gpt-4o-mini-tts",
      voice,
      input: text,
      response_format: "pcm",
    }),
  });
  if (!res.ok || !res.body) await fail("tts", res);
  const resampler = new StreamingResampler(OPENAI_TTS_RATE, DEVICE_RATE);
  const reader = res.body!.getReader();
  for (;;) {
    const { done, value } = await reader.read();
    if (done) break;
    const out = resampler.pushBytes(value);
    if (out.length) onPcm(out);
  }
  const tail = resampler.flush();
  if (tail.length) onPcm(tail);
}

/** Text -> 16 kHz mono PCM for the device (OpenAI returns 24 kHz; resampled here). */
export async function speak(text: string, voice = "alloy"): Promise<Uint8Array<ArrayBuffer>> {
  const res = await fetch("https://api.openai.com/v1/audio/speech", {
    method: "POST",
    headers: { Authorization: `Bearer ${apiKey()}`, "Content-Type": "application/json" },
    body: JSON.stringify({
      model: Netlify.env.get("OPENAI_TTS_MODEL") ?? "gpt-4o-mini-tts",
      voice,
      input: text,
      response_format: "pcm",
    }),
  });
  if (!res.ok) await fail("tts", res);
  const pcm24k = bytesToInt16(new Uint8Array(await res.arrayBuffer()));
  return int16ToBytes(resample(pcm24k, OPENAI_TTS_RATE, DEVICE_RATE));
}
