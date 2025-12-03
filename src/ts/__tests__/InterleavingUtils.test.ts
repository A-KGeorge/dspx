import { describe, test } from "node:test";
import assert from "node:assert/strict";
import { DspUtils } from "../utils.js";

describe("Interleaving Utilities", () => {
  test("should interleave two channels correctly", () => {
    const ch0 = new Float32Array([1, 2, 3]);
    const ch1 = new Float32Array([4, 5, 6]);
    const planar = [ch0, ch1];

    const interleaved = DspUtils.interleave(planar);

    assert.strictEqual(interleaved.length, 6);
    assert.deepStrictEqual(Array.from(interleaved), [1, 4, 2, 5, 3, 6]);
  });

  test("should interleave three channels correctly", () => {
    const ch0 = new Float32Array([1, 2]);
    const ch1 = new Float32Array([3, 4]);
    const ch2 = new Float32Array([5, 6]);
    const planar = [ch0, ch1, ch2];

    const interleaved = DspUtils.interleave(planar);

    assert.strictEqual(interleaved.length, 6);
    assert.deepStrictEqual(Array.from(interleaved), [1, 3, 5, 2, 4, 6]);
  });

  test("should deinterleave two channels correctly", () => {
    const interleaved = new Float32Array([1, 4, 2, 5, 3, 6]);

    const planar = DspUtils.deinterleave(interleaved, 2);

    assert.strictEqual(planar.length, 2);
    assert.strictEqual(planar[0].length, 3);
    assert.strictEqual(planar[1].length, 3);
    assert.deepStrictEqual(Array.from(planar[0]), [1, 2, 3]);
    assert.deepStrictEqual(Array.from(planar[1]), [4, 5, 6]);
  });

  test("should deinterleave three channels correctly", () => {
    const interleaved = new Float32Array([1, 3, 5, 2, 4, 6]);

    const planar = DspUtils.deinterleave(interleaved, 3);

    assert.strictEqual(planar.length, 3);
    assert.strictEqual(planar[0].length, 2);
    assert.deepStrictEqual(Array.from(planar[0]), [1, 2]);
    assert.deepStrictEqual(Array.from(planar[1]), [3, 4]);
    assert.deepStrictEqual(Array.from(planar[2]), [5, 6]);
  });

  test("should handle single channel (no-op interleave)", () => {
    const ch0 = new Float32Array([1, 2, 3, 4, 5]);
    const planar = [ch0];

    const interleaved = DspUtils.interleave(planar);

    assert.strictEqual(interleaved.length, 5);
    assert.deepStrictEqual(Array.from(interleaved), [1, 2, 3, 4, 5]);
  });

  test("should handle single channel (no-op deinterleave)", () => {
    const interleaved = new Float32Array([1, 2, 3, 4, 5]);

    const planar = DspUtils.deinterleave(interleaved, 1);

    assert.strictEqual(planar.length, 1);
    assert.deepStrictEqual(Array.from(planar[0]), [1, 2, 3, 4, 5]);
  });

  test("should be reversible (interleave then deinterleave)", () => {
    const original = [
      new Float32Array([1.1, 2.2, 3.3]),
      new Float32Array([4.4, 5.5, 6.6]),
      new Float32Array([7.7, 8.8, 9.9]),
    ];

    const interleaved = DspUtils.interleave(original);
    const restored = DspUtils.deinterleave(interleaved, 3);

    assert.strictEqual(restored.length, original.length);
    for (let ch = 0; ch < original.length; ch++) {
      assert.strictEqual(restored[ch].length, original[ch].length);
      for (let i = 0; i < original[ch].length; i++) {
        assert.ok(
          Math.abs(restored[ch][i] - original[ch][i]) < 1e-6,
          `Channel ${ch}, sample ${i}: expected ${original[ch][i]}, got ${restored[ch][i]}`
        );
      }
    }
  });

  test("should handle empty channels", () => {
    const ch0 = new Float32Array([]);
    const ch1 = new Float32Array([]);
    const planar = [ch0, ch1];

    const interleaved = DspUtils.interleave(planar);

    assert.strictEqual(interleaved.length, 0);
  });

  test("should handle large channel counts", () => {
    const numChannels = 16;
    const samplesPerChannel = 100;
    const planar: Float32Array[] = [];

    for (let ch = 0; ch < numChannels; ch++) {
      const channel = new Float32Array(samplesPerChannel);
      for (let i = 0; i < samplesPerChannel; i++) {
        channel[i] = ch + i * 0.1;
      }
      planar.push(channel);
    }

    const interleaved = DspUtils.interleave(planar);
    const restored = DspUtils.deinterleave(interleaved, numChannels);

    assert.strictEqual(interleaved.length, numChannels * samplesPerChannel);
    assert.strictEqual(restored.length, numChannels);

    for (let ch = 0; ch < numChannels; ch++) {
      for (let i = 0; i < samplesPerChannel; i++) {
        assert.ok(
          Math.abs(restored[ch][i] - planar[ch][i]) < 1e-6,
          `Channel ${ch}, sample ${i} mismatch`
        );
      }
    }
  });

  test("should handle empty planar array", () => {
    const result = DspUtils.interleave([]);
    assert.strictEqual(result.length, 0);
  });

  test("should throw error for mismatched channel lengths", () => {
    const ch0 = new Float32Array([1, 2, 3]);
    const ch1 = new Float32Array([4, 5]); // Different length!

    assert.throws(() => {
      DspUtils.interleave([ch0, ch1]);
    }, /same length/i);
  });

  test("should throw error for invalid channel count in deinterleave", () => {
    const interleaved = new Float32Array([1, 2, 3, 4, 5, 6]);

    assert.throws(() => {
      DspUtils.deinterleave(interleaved, 0);
    }, /positive number/i);
  });

  test("should throw error for non-divisible length in deinterleave", () => {
    const interleaved = new Float32Array([1, 2, 3, 4, 5]); // 5 samples, not divisible by 2

    assert.throws(() => {
      DspUtils.deinterleave(interleaved, 2);
    }, /divisible/i);
  });

  test("should preserve floating point precision", () => {
    const ch0 = new Float32Array([Math.PI, Math.E, Math.SQRT2]);
    const ch1 = new Float32Array([1.23456789, 9.87654321, 0.123456789]);
    const planar = [ch0, ch1];

    const interleaved = DspUtils.interleave(planar);
    const restored = DspUtils.deinterleave(interleaved, 2);

    for (let ch = 0; ch < planar.length; ch++) {
      for (let i = 0; i < planar[ch].length; i++) {
        assert.strictEqual(
          restored[ch][i],
          planar[ch][i],
          `Precision lost at channel ${ch}, sample ${i}`
        );
      }
    }
  });

  test("should handle negative values correctly", () => {
    const ch0 = new Float32Array([-1, -2, -3]);
    const ch1 = new Float32Array([-4, -5, -6]);
    const planar = [ch0, ch1];

    const interleaved = DspUtils.interleave(planar);
    const restored = DspUtils.deinterleave(interleaved, 2);

    assert.deepStrictEqual(Array.from(restored[0]), [-1, -2, -3]);
    assert.deepStrictEqual(Array.from(restored[1]), [-4, -5, -6]);
  });

  test("should handle mixed positive and negative values", () => {
    const ch0 = new Float32Array([1, -2, 3, -4]);
    const ch1 = new Float32Array([-5, 6, -7, 8]);
    const planar = [ch0, ch1];

    const interleaved = DspUtils.interleave(planar);

    assert.deepStrictEqual(
      Array.from(interleaved),
      [1, -5, -2, 6, 3, -7, -4, 8]
    );
  });
});
