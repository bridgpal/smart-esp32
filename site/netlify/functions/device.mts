// Device API. The ESP32 calls these with `Authorization: Bearer <DEVICE_TOKEN>` and `X-Device-Id`.
//   GET  /api/device/ping   -> { ok, deviceId, serverTime }
//   POST /api/device/event  -> { ok, received, led: [r, g, b] }   (ring color for the board to show)
//   POST /api/device/say    -> spoken audio for { text, voice? }
//   POST /api/device/talk   -> body: recorded speech; returns the spoken reply (+ X-Transcript, X-Reply)
//   POST /api/device/talk-stream -> same input; streams framed output as it's generated (see below)
// All audio is raw PCM, 16 kHz, 16-bit signed little-endian, mono.
import type { Config, Context } from "@netlify/functions";
import { timingSafeEqual } from "node:crypto";

import { DEVICE_RATE } from "../../lib/audio.ts";
import { gatewayActive, reply, replySentences } from "../../lib/claude-reply.ts";
import { SpeechError, speak, speakStream, transcribe } from "../../lib/openai-speech.ts";

const json = (status: number, body: unknown) => Response.json(body, { status });

function authorized(req: Request, expected: string): boolean {
  const given = Buffer.from(req.headers.get("authorization") ?? "");
  const want = Buffer.from(`Bearer ${expected}`);
  return given.length === want.length && timingSafeEqual(given, want);
}

const KEY_COLORS: Record<string, [number, number, number]> = {
  K1: [0, 60, 0],
  K2: [0, 0, 60],
  K3: [60, 20, 0],
  BOOT: [40, 0, 40],
};

const PCM_HEADERS = {
  "Content-Type": "application/octet-stream",
  "X-Audio-Format": `pcm_s16le;rate=${DEVICE_RATE};channels=1`,
};

// Header values must be ASCII; the device URL-decodes these.
const headerText = (s: string) => encodeURIComponent(s.slice(0, 500));

function speechError(e: unknown): Response {
  if (e instanceof SpeechError) return json(e.status, { ok: false, error: e.message, detail: e.detail });
  console.error("device audio route failed", e);
  return json(500, { ok: false, error: e instanceof Error ? e.message : String(e) });
}

// { text, voice? } -> spoken audio.
async function say(req: Request): Promise<Response> {
  const body = (await req.json().catch(() => null)) as { text?: unknown; voice?: unknown } | null;
  const text = typeof body?.text === "string" ? body.text.trim() : "";
  if (!text) return json(400, { ok: false, error: "body must be JSON with a non-empty string `text`" });
  if (text.length > 1000) return json(400, { ok: false, error: "text is limited to 1000 characters" });
  try {
    const pcm = await speak(text, typeof body?.voice === "string" ? body.voice : undefined);
    return new Response(pcm, { headers: PCM_HEADERS });
  } catch (e) {
    return speechError(e);
  }
}

// Push-to-talk: raw 16 kHz mono PCM from the mics -> transcript -> Claude -> spoken reply.
async function talk(req: Request, deviceId: string): Promise<Response> {
  const pcm = new Uint8Array(await req.arrayBuffer());
  const seconds = pcm.length / 2 / DEVICE_RATE;
  if (seconds < 0.3) return json(400, { ok: false, error: "recording too short" });
  if (seconds > 30) return json(413, { ok: false, error: "recording longer than 30 s" });

  try {
    const t0 = Date.now();
    const heard = await transcribe(pcm);
    const t1 = Date.now();
    const answer = heard ? await reply(heard) : "Sorry, I didn't catch that. Hold the button and try again.";
    const t2 = Date.now();
    const audio = await speak(answer);
    const t3 = Date.now();
    console.log("talk", { deviceId, seconds, heard, answer, sttMs: t1 - t0, llmMs: t2 - t1, ttsMs: t3 - t2 });
    return new Response(audio, {
      headers: {
        ...PCM_HEADERS,
        "X-Transcript": headerText(heard),
        "X-Reply": headerText(answer),
        "Server-Timing": `stt;dur=${t1 - t0}, llm;dur=${t2 - t1}, tts;dur=${t3 - t2}`,
      },
    });
  } catch (e) {
    return speechError(e);
  }
}

// Framed stream: [1 byte type][4 byte little-endian length][payload], repeated.
//   T transcript text | R reply sentence text | A audio (PCM chunk) | E error text | Z end (empty)
function frame(type: string, payload: Uint8Array = new Uint8Array()): Uint8Array<ArrayBuffer> {
  const out = new Uint8Array(5 + payload.length);
  out[0] = type.charCodeAt(0);
  new DataView(out.buffer).setUint32(1, payload.length, true);
  out.set(payload, 5);
  return out;
}
const utf8 = (s: string) => new TextEncoder().encode(s);

function talkStream(pcm: Uint8Array, deviceId: string): Response {
  const body = new ReadableStream<Uint8Array>({
    async start(controller) {
      const send = (type: string, payload?: Uint8Array) => controller.enqueue(frame(type, payload));
      const t0 = Date.now();
      const timing: Record<string, number> = {};
      const said: string[] = [];
      let heard = "";
      try {
        heard = await transcribe(pcm);
        timing.sttMs = Date.now() - t0;
        send("T", utf8(heard));
        const sentences = heard
          ? replySentences(heard)
          : (async function* () {
              yield "Sorry, I didn't catch that. Hold the button and try again.";
            })();
        for await (const sentence of sentences) {
          timing[`sentence${said.length + 1}TextMs`] ??= Date.now() - t0;
          said.push(sentence);
          send("R", utf8(sentence));
          let first = true;
          await speakStream(sentence, (chunk) => {
            if (first) timing[`sentence${said.length}AudioMs`] ??= Date.now() - t0;
            first = false;
            send("A", chunk);
          });
        }
      } catch (e) {
        console.error("talk-stream failed", e);
        send("E", utf8(e instanceof SpeechError ? `${e.message} ${e.detail}` : String(e)));
      }
      timing.totalMs = Date.now() - t0;
      console.log("talk-stream", { deviceId, heard, said, ...timing });
      send("Z");
      controller.close();
    },
  });
  return new Response(body, { headers: { "Content-Type": "application/x-smart-esp32-frames" } });
}

export default async (req: Request, context: Context) => {
  const expected = Netlify.env.get("DEVICE_TOKEN");
  if (!expected) return json(500, { ok: false, error: "DEVICE_TOKEN is not set on this site" });
  if (!authorized(req, expected)) return json(401, { ok: false, error: "unauthorized" });

  const deviceId = req.headers.get("x-device-id") ?? "unknown";
  const route = new URL(req.url).pathname.replace(/^\/api\/device\/?/, "");

  if (req.method === "GET" && route === "ping") {
    return json(200, {
      ok: true,
      deviceId,
      serverTime: new Date().toISOString(),
      deployId: context.deploy.id,
      clientIp: context.ip,
      ai: { claudeGateway: gatewayActive(), openaiSpeech: Boolean(Netlify.env.get("OPENAI_TTS_API_KEY")) },
    });
  }

  if (req.method === "POST" && route === "event") {
    const event = (await req.json().catch(() => null)) as { type?: unknown; key?: unknown } | null;
    if (!event || typeof event.type !== "string") {
      return json(400, { ok: false, error: "body must be JSON with a string `type`" });
    }
    console.log("device event", { deviceId, ...event });
    const led = KEY_COLORS[String(event.key)] ?? [30, 30, 30];
    return json(200, { ok: true, received: event, led });
  }

  if (req.method === "POST" && route === "say") return say(req);
  if (req.method === "POST" && route === "talk") return talk(req, deviceId);
  if (req.method === "POST" && route === "talk-stream") {
    const pcm = new Uint8Array(await req.arrayBuffer());
    const seconds = pcm.length / 2 / DEVICE_RATE;
    if (seconds < 0.3) return json(400, { ok: false, error: "recording too short" });
    if (seconds > 30) return json(413, { ok: false, error: "recording longer than 30 s" });
    return talkStream(pcm, deviceId);
  }

  return json(404, { ok: false, error: `no route ${req.method} /api/device/${route}` });
};

export const config: Config = {
  path: "/api/device/*",
};
