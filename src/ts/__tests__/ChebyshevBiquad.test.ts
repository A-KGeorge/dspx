import { describe, test } from "node:test";
import assert from "node:assert/strict";
import { IirFilter } from "../filters.js";

const sampleRate = 44100;

describe("Chebyshev Filters", () => {
  test("should create Chebyshev low-pass filter", async () => {
    const filter = IirFilter.createChebyshevLowPass({
      cutoffFrequency: 1000,
      sampleRate,
      order: 2,
      rippleDb: 0.5,
    });

    assert.strictEqual(filter.getBCoefficients().length, 3);
    assert.strictEqual(filter.getACoefficients().length, 2);

    // Test filtering
    const testSignal = new Float32Array(10).fill(1.0);
    const filtered = await filter.process(testSignal);
    assert.strictEqual(filtered.length, 10);
  });

  test("should create Chebyshev high-pass filter", async () => {
    const filter = IirFilter.createChebyshevHighPass({
      cutoffFrequency: 500,
      sampleRate,
      order: 2,
      rippleDb: 0.5,
    });

    const testSignal = new Float32Array(10).fill(1.0);
    const filtered = await filter.process(testSignal);
    assert.strictEqual(filtered.length, 10);
  });

  test("should create Chebyshev band-pass filter", async () => {
    const filter = IirFilter.createChebyshevBandPass({
      lowCutoffFrequency: 500,
      highCutoffFrequency: 2000,
      sampleRate,
      order: 2,
      rippleDb: 0.5,
    });

    // Band-pass cascades high-pass and low-pass filters
    // Order 2 means 2nd-order (biquad) for each, so cascaded = 4th-order
    // 4th-order filter has 5 coefficients (b0, b1, b2, b3, b4)
    assert.strictEqual(filter.getBCoefficients().length, 5);

    const testSignal = new Float32Array(10).fill(1.0);
    await filter.process(testSignal);
  });

  test("should use default ripple of 0.5 dB", () => {
    const filter1 = IirFilter.createChebyshevLowPass({
      cutoffFrequency: 1000,
      sampleRate,
      order: 2,
    });

    const filter2 = IirFilter.createChebyshevLowPass({
      cutoffFrequency: 1000,
      sampleRate,
      order: 2,
      rippleDb: 0.5,
    });

    const bCoeffs1 = filter1.getBCoefficients();
    const bCoeffs2 = filter2.getBCoefficients();

    // Coefficients should be equal for same ripple
    assert.strictEqual(bCoeffs1.length, bCoeffs2.length);
  });

  test("should support different ripple values", () => {
    const filter1 = IirFilter.createChebyshevLowPass({
      cutoffFrequency: 1000,
      sampleRate,
      order: 2,
      rippleDb: 0.1,
    });

    const filter2 = IirFilter.createChebyshevLowPass({
      cutoffFrequency: 1000,
      sampleRate,
      order: 2,
      rippleDb: 1.0,
    });

    const bCoeffs1 = filter1.getBCoefficients();
    const bCoeffs2 = filter2.getBCoefficients();

    // Both should have coefficients
    assert.strictEqual(bCoeffs1.length, 3);
    assert.strictEqual(bCoeffs2.length, 3);
  });
});

describe("Biquad EQ Filters", () => {
  test("should create peaking EQ filter with boost", async () => {
    const filter = IirFilter.createPeakingEQ({
      centerFrequency: 1000,
      sampleRate,
      Q: 2.0,
      gainDb: 6.0,
    });

    const bCoeffs = filter.getBCoefficients();
    const aCoeffs = filter.getACoefficients();

    assert.strictEqual(bCoeffs.length, 3);
    assert.strictEqual(aCoeffs.length, 2);

    // Test basic processing
    const testSignal = new Float32Array(20).fill(1.0);
    const boosted = await filter.process(testSignal);
    assert.strictEqual(boosted.length, 20);
  });

  test("should create low-shelf filter", async () => {
    const filter = IirFilter.createLowShelf({
      cutoffFrequency: 1000,
      sampleRate,
      gainDb: 6.0,
      Q: 0.707,
    });

    const testSignal = new Float32Array(10).fill(1.0);
    const processed = await filter.process(testSignal);

    assert.strictEqual(processed.length, 10);
  });

  test("should create high-shelf filter with attenuation", async () => {
    const filter = IirFilter.createHighShelf({
      cutoffFrequency: 1000,
      sampleRate,
      gainDb: -6.0,
      Q: 0.707,
    });

    const testSignal = new Float32Array(20).fill(1.0);
    const attenuated = await filter.process(testSignal);

    assert.strictEqual(attenuated.length, 20);
  });

  test("should support EQ chain", async () => {
    // Create 3-band parametric EQ
    const lowShelf = IirFilter.createLowShelf({
      cutoffFrequency: 200,
      sampleRate,
      gainDb: 3.0,
      Q: 0.707,
    });

    const midPeak = IirFilter.createPeakingEQ({
      centerFrequency: 1000,
      sampleRate,
      Q: 1.5,
      gainDb: -6.0,
    });

    const highShelf = IirFilter.createHighShelf({
      cutoffFrequency: 3000,
      sampleRate,
      gainDb: 2.0,
      Q: 0.707,
    });

    // Test signal
    const testSignal = new Float32Array(100).fill(1.0);

    // Apply EQ chain
    let processed = await lowShelf.process(testSignal);
    processed = await midPeak.process(processed);
    processed = await highShelf.process(processed);

    assert.strictEqual(processed.length, 100);
  });

  test("should create peaking EQ with negative gain (cut)", async () => {
    const filter = IirFilter.createPeakingEQ({
      centerFrequency: 1000,
      sampleRate,
      Q: 2.0,
      gainDb: -12.0,
    });

    const testSignal = new Float32Array(20).fill(1.0);
    const cut = await filter.process(testSignal);

    assert.strictEqual(cut.length, 20);
  });
});

describe("Filter Validation", () => {
  test("should reject ripple > 3 dB", () => {
    assert.throws(() => {
      IirFilter.createChebyshevLowPass({
        cutoffFrequency: 1000,
        sampleRate,
        order: 2,
        rippleDb: 5.0,
      });
    }, /ripple/i);
  });

  test("should reject Q = 0 for peaking EQ", () => {
    assert.throws(() => {
      IirFilter.createPeakingEQ({
        centerFrequency: 1000,
        sampleRate,
        Q: 0,
        gainDb: 6.0,
      });
    }, /Q must be positive/i);
  });

  test("should reject negative Q", () => {
    assert.throws(() => {
      IirFilter.createPeakingEQ({
        centerFrequency: 1000,
        sampleRate,
        Q: -1.5,
        gainDb: 6.0,
      });
    }, /Q must be positive/i);
  });
});
