// Device API. The ESP32 calls these with `Authorization: Bearer <DEVICE_TOKEN>` and `X-Device-Id`.
//   GET  /api/device/ping   -> { ok, deviceId, serverTime }
//   POST /api/device/event  -> { ok, received, led: [r, g, b] }   (ring color for the board to show)
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

  return json(404, { ok: false, error: `no route ${req.method} /api/device/${route}` });
};

export const config: Config = {
  path: "/api/device/*",
};
