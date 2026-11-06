// Text to speech for the device (`/api/device/say`). OpenAI returns 24 kHz PCM; the board plays 16 kHz.
import { DEVICE_RATE, OPENAI_TTS_RATE, bytesToInt16, int16ToBytes, resample } from "./audio.ts";
import { openai } from "./openai.ts";

export async function speak(text: string, voice = "alloy"): Promise<Uint8Array<ArrayBuffer>> {
  const res = await openai().audio.speech.create({
    model: Netlify.env.get("OPENAI_TTS_MODEL") ?? "gpt-4o-mini-tts",
    voice,
    input: text,
    response_format: "pcm",
  });
  const pcm24k = bytesToInt16(new Uint8Array(await res.arrayBuffer()));
  return int16ToBytes(resample(pcm24k, OPENAI_TTS_RATE, DEVICE_RATE));
}
