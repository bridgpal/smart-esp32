// The one OpenAI client the site uses, authenticated with the site's own key (OPENAI_TTS_API_KEY).
import OpenAI from "openai";

export class OpenAIConfigError extends Error {}

let client: OpenAI | null = null;

export function hasOpenAIKey(): boolean {
  return Boolean(Netlify.env.get("OPENAI_TTS_API_KEY"));
}

export function openai(): OpenAI {
  const apiKey = Netlify.env.get("OPENAI_TTS_API_KEY");
  if (!apiKey) throw new OpenAIConfigError("OPENAI_TTS_API_KEY is not set on this site");
  // baseURL is explicit because Netlify's AI Gateway may inject OPENAI_BASE_URL into functions,
  // which the SDK would otherwise pick up and send this key to.
  client ??= new OpenAI({ apiKey, baseURL: "https://api.openai.com/v1" });
  return client;
}

/** Short, safe-to-show description of an error from OpenAI or our own config. */
export function describeError(e: unknown): string {
  if (e instanceof OpenAI.APIError) return `openai ${e.status ?? ""}: ${e.message}`.slice(0, 300);
  if (e instanceof Error) return e.message.slice(0, 300);
  return String(e).slice(0, 300);
}
