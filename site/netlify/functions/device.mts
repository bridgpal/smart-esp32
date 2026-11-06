// Device API. The ESP32 calls these with `Authorization: Bearer <DEVICE_TOKEN>` and `X-Device-Id`.
//   GET  /api/device/ping   -> { ok, deviceId, serverTime }
//   POST /api/device/event  -> { ok, received, led: [r, g, b] }   (ring color for the board to show)
//   POST /api/device/say    -> raw PCM audio (24 kHz s16le mono) for { text, voice? }
import type { Config, Context } from "@netlify/functions";
import { timingSafeEqual } from "node:crypto";

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

// Text-to-speech via OpenAI. Streams back raw PCM: 24 kHz, 16-bit signed little-endian, mono,
// which the board writes straight to the ES8311 over I2S.
async function say(req: Request): Promise<Response> {
  const apiKey = Netlify.env.get("OPENAI_TTS_API_KEY");
  if (!apiKey) return json(500, { ok: false, error: "OPENAI_TTS_API_KEY is not set on this site" });

  const body = (await req.json().catch(() => null)) as { text?: unknown; voice?: unknown } | null;
  const text = typeof body?.text === "string" ? body.text.trim() : "";
  if (!text) return json(400, { ok: false, error: "body must be JSON with a non-empty string `text`" });
  if (text.length > 1000) return json(400, { ok: false, error: "text is limited to 1000 characters" });

  const upstream = await fetch("https://api.openai.com/v1/audio/speech", {
    method: "POST",
    headers: { Authorization: `Bearer ${apiKey}`, "Content-Type": "application/json" },
    body: JSON.stringify({
      model: Netlify.env.get("OPENAI_TTS_MODEL") ?? "gpt-4o-mini-tts",
      voice: typeof body?.voice === "string" ? body.voice : "alloy",
      input: text,
      response_format: "pcm",
    }),
  });

  if (!upstream.ok || !upstream.body) {
    const detail = await upstream.text().catch(() => "");
    console.error("openai tts failed", upstream.status, detail.slice(0, 500));
    return json(502, { ok: false, error: `openai tts returned ${upstream.status}`, detail: detail.slice(0, 300) });
  }

  return new Response(upstream.body, {
    status: 200,
    headers: {
      "Content-Type": "application/octet-stream",
      "X-Audio-Format": "pcm_s16le;rate=24000;channels=1",
    },
  });
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

  return json(404, { ok: false, error: `no route ${req.method} /api/device/${route}` });
};

export const config: Config = {
  path: "/api/device/*",
};
