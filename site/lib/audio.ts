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

// Same filter as resample(), fed in chunks (e.g. a streaming TTS response). Holds back the last
// few input samples until the next chunk arrives, so output is identical to the batch version.
export class StreamingResampler {
  private buf: number[] = [];
  private pos = 0; // next output position, in input samples relative to buf[0]
  private carry: number | null = null; // odd trailing byte from the previous chunk
  private readonly ratio: number;
  private readonly cutoff: number;
  private readonly half = 16;

  constructor(from: number, to: number) {
    this.ratio = from / to;
    this.cutoff = Math.min(1, to / from) * 0.92;
  }

  /** Raw s16le bytes in (any length), s16le bytes out. */
  pushBytes(bytes: Uint8Array): Uint8Array<ArrayBuffer> {
    let data = bytes;
    if (this.carry !== null) {
      data = new Uint8Array(bytes.length + 1);
      data[0] = this.carry;
      data.set(bytes, 1);
      this.carry = null;
    }
    if (data.length % 2) {
      this.carry = data[data.length - 1];
      data = data.subarray(0, data.length - 1);
    }
    for (const s of bytesToInt16(data)) this.buf.push(s);
    return int16ToBytes(this.drain(false));
  }

  flush(): Uint8Array<ArrayBuffer> {
    return int16ToBytes(this.drain(true));
  }

  private drain(final: boolean): Int16Array {
    const out: number[] = [];
    for (;;) {
      const i0 = Math.floor(this.pos);
      if (final ? i0 >= this.buf.length : i0 + this.half >= this.buf.length) break;
      let acc = 0;
      let norm = 0;
      for (let k = -this.half + 1; k <= this.half; k++) {
        const i = i0 + k;
        if (i < 0 || i >= this.buf.length) continue;
        const x = i - this.pos;
        const sinc = x === 0 ? 1 : Math.sin(Math.PI * this.cutoff * x) / (Math.PI * this.cutoff * x);
        const h = sinc * (0.5 + 0.5 * Math.cos((Math.PI * x) / this.half));
        acc += this.buf[i] * h;
        norm += h;
      }
      out.push(Math.max(-32768, Math.min(32767, Math.round(norm ? acc / norm : 0))));
      this.pos += this.ratio;
    }
    const keepFrom = Math.max(0, Math.floor(this.pos) - this.half + 1);
    if (keepFrom > 0) {
      this.buf.splice(0, keepFrom);
      this.pos -= keepFrom;
    }
    return Int16Array.from(out);
  }
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
