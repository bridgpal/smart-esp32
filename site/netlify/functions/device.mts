// Device API. The ESP32 calls these with `Authorization: Bearer <DEVICE_TOKEN>` and `X-Device-Id`.
//   GET  /api/device/ping        -> { ok, deviceId, serverTime, deployId, clientIp, openai }
//   POST /api/device/event       -> { ok, received, led: [r, g, b] }   (ring color for the board to show)
//   POST /api/device/say         -> spoken audio for { text, voice? }
//   POST /api/device/talk-audio  -> body: recorded speech; streams the spoken answer as frames (below)
// All audio is raw PCM, 16 kHz, 16-bit signed little-endian, mono. Everything AI is OpenAI, using
// the site's OPENAI_TTS_API_KEY (see lib/openai.ts).
import type { Config, Context } from "@netlify/functions";
import { timingSafeEqual } from "node:crypto";

import { DEVICE_RATE } from "../../lib/audio.ts";
import { describeError, hasOpenAIKey } from "../../lib/openai.ts";
import { audioReply } from "../../lib/openai-audio.ts";
import { speak } from "../../lib/openai-speech.ts";

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

// { text, voice? } -> spoken audio.
async function say(req: Request): Promise<Response> {
  const body = (await req.json().catch(() => null)) as { text?: unknown; voice?: unknown } | null;
  const text = typeof body?.text === "string" ? body.text.trim() : "";
  if (!text) return json(400, { ok: false, error: "body must be JSON with a non-empty string `text`" });
  if (text.length > 1000) return json(400, { ok: false, error: "text is limited to 1000 characters" });
  try {
    const pcm = await speak(text, typeof body?.voice === "string" ? body.voice : undefined);
    return new Response(pcm, {
      headers: { "Content-Type": "application/octet-stream", "X-Audio-Format": `pcm_s16le;rate=${DEVICE_RATE};channels=1` },
    });
  } catch (e) {
    console.error("say failed", e);
    return json(502, { ok: false, error: describeError(e) });
  }
}

// Framed stream: [1 byte type][4 byte little-endian length][payload], repeated.
//   R reply text | A audio (PCM chunk) | M model name | E error text | Z end (empty)
function frame(type: string, payload: Uint8Array = new Uint8Array()): Uint8Array<ArrayBuffer> {
  const out = new Uint8Array(5 + payload.length);
  out[0] = type.charCodeAt(0);
  new DataView(out.buffer).setUint32(1, payload.length, true);
  out.set(payload, 5);
  return out;
}
const utf8 = (s: string) => new TextEncoder().encode(s);

// Push-to-talk: the recording goes to an OpenAI audio model in one call; its spoken answer streams
// back as it's generated. The answer's text is sent as one R frame per sentence.
function talkAudio(pcm: Uint8Array, deviceId: string): Response {
  const body = new ReadableStream<Uint8Array>({
    async start(controller) {
      const send = (type: string, payload?: Uint8Array) => controller.enqueue(frame(type, payload));
      const t0 = Date.now();
      let text = "";
      let pending = "";
      let firstAudioMs: number | undefined;
      let model = "";
      try {
        model = await audioReply(pcm, {
          onText: (delta) => {
            text += delta;
            pending += delta;
            const cut = pending.search(/[.!?]["')\]]*\s+\S/);
            if (cut >= 0) {
              const end = pending.slice(cut).search(/\s/) + cut;
              send("R", utf8(pending.slice(0, end).trim()));
              pending = pending.slice(end).trimStart();
            }
          },
          onAudio: (chunk) => {
            firstAudioMs ??= Date.now() - t0;
            send("A", chunk);
          },
        });
        if (pending.trim()) send("R", utf8(pending.trim()));
        send("M", utf8(model)); // which model answered (devices ignore unknown frame types)
      } catch (e) {
        console.error("talk-audio failed", e);
        send("E", utf8(describeError(e)));
      }
      console.log("talk-audio", { deviceId, model, said: text, firstAudioMs, totalMs: Date.now() - t0 });
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
      openai: hasOpenAIKey(),
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

  if (req.method === "POST" && route === "talk-audio") {
    const pcm = new Uint8Array(await req.arrayBuffer());
    const seconds = pcm.length / 2 / DEVICE_RATE;
    if (seconds < 0.3) return json(400, { ok: false, error: "recording too short" });
    if (seconds > 30) return json(413, { ok: false, error: "recording longer than 30 s" });
    return talkAudio(pcm, deviceId);
  }

  return json(404, { ok: false, error: `no route ${req.method} /api/device/${route}` });
};

export const config: Config = {
  path: "/api/device/*",
};
