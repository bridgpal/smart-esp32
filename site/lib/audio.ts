// PCM helpers for the device audio path. All PCM is 16-bit signed little-endian mono.

export const DEVICE_RATE = 16000; // what the board records and plays
export const OPENAI_TTS_RATE = 24000; // OpenAI TTS `pcm` output

export function bytesToInt16(bytes: Uint8Array): Int16Array {
  const out = new Int16Array(bytes.length >> 1);
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  for (let i = 0; i < out.length; i++) out[i] = view.getInt16(i * 2, true);
  return out;
}

export function int16ToBytes(samples: Int16Array): Uint8Array<ArrayBuffer> {
  const out = new Uint8Array(samples.length * 2);
  const view = new DataView(out.buffer);
  for (let i = 0; i < samples.length; i++) view.setInt16(i * 2, samples[i], true);
  return out;
}

// Windowed-sinc resampler (Hann window, 32 taps). Low-passes below the target Nyquist when
// downsampling, so 24 kHz -> 16 kHz doesn't alias.
export function resample(input: Int16Array, from: number, to: number): Int16Array {
  if (from === to) return input;
  const ratio = from / to;
  const out = new Int16Array(Math.floor(input.length / ratio));
  const cutoff = Math.min(1, to / from) * 0.92; // fraction of the input Nyquist
  const half = 16;
  for (let j = 0; j < out.length; j++) {
    const p = j * ratio;
    const i0 = Math.floor(p);
    let acc = 0;
    let norm = 0;
    for (let k = -half + 1; k <= half; k++) {
      const i = i0 + k;
      if (i < 0 || i >= input.length) continue;
      const x = i - p;
      const sinc = x === 0 ? 1 : Math.sin(Math.PI * cutoff * x) / (Math.PI * cutoff * x);
      const window = 0.5 + 0.5 * Math.cos((Math.PI * x) / half);
      const h = sinc * window;
      acc += input[i] * h;
      norm += h;
    }
    out[j] = Math.max(-32768, Math.min(32767, Math.round(norm ? acc / norm : 0)));
  }
  return out;
}

// Minimal 44-byte RIFF/WAVE header + data, for APIs that want a file.
export function pcmToWav(pcm: Uint8Array, sampleRate: number): Uint8Array<ArrayBuffer> {
  const out = new Uint8Array(44 + pcm.length);
  const v = new DataView(out.buffer);
  const ascii = (off: number, s: string) => [...s].forEach((c, i) => v.setUint8(off + i, c.charCodeAt(0)));
  ascii(0, "RIFF");
  v.setUint32(4, 36 + pcm.length, true);
  ascii(8, "WAVE");
  ascii(12, "fmt ");
  v.setUint32(16, 16, true); // fmt chunk size
  v.setUint16(20, 1, true); // PCM
  v.setUint16(22, 1, true); // mono
  v.setUint32(24, sampleRate, true);
  v.setUint32(28, sampleRate * 2, true); // byte rate
  v.setUint16(32, 2, true); // block align
  v.setUint16(34, 16, true); // bits per sample
  ascii(36, "data");
  v.setUint32(40, pcm.length, true);
  out.set(pcm, 44);
  return out;
}
