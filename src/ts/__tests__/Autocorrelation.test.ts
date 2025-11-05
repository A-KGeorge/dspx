import { describe, test } from "node:test";
import assert from "node:assert";
import { DspUtils } from "../utils.js";

describe("Autocorrelation - basic functionality", () => {
  test("should compute autocorrelation of simple sine wave", () => {
    // 10 Hz sine at 100 Hz sampling -> period = 10 samples
    const numSamples = 100;
    const signal = new Float32Array(numSamples);
    const freq = 10;
    const sampleRate = 100;

    for (let i = 0; i < numSamples; i++) {
      signal[i] = Math.sin((2 * Math.PI * freq * i) / sampleRate);
    }

    const autocorr = DspUtils.autocorrelation(signal);

    // Zero-lag should be maximum (signal energy)
    assert.ok(
      autocorr[0] > 0,
      "Zero-lag autocorrelation should be positive (energy)"
    );

    // Should have peak at lag = 10 (one period)
    const lag10 = autocorr[10];
    assert.ok(
      Math.abs(lag10 - autocorr[0]) < autocorr[0] * 0.2,
      "Should have strong correlation at period lag"
    );
  });

  test("should compute autocorrelation of constant signal", () => {
    const signal = new Float32Array([5.0, 5.0, 5.0, 5.0, 5.0, 5.0]);
    const autocorr = DspUtils.autocorrelation(signal);

    // For constant signal, autocorr[k] = (n-k) * c^2
    // This is linear autocorrelation, not circular
    const n = signal.length;
    const c = 5.0;
    const energy = autocorr[0];
    assert.ok(energy > 0, "Energy should be positive");

    // Check first few lags match expected linear autocorrelation
    for (let k = 0; k < Math.min(4, autocorr.length); k++) {
      const expected = (n - k) * c * c;
      assert.ok(
        Math.abs(autocorr[k] - expected) < 0.1,
        `Lag ${k}: expected ${expected}, got ${autocorr[k]}`
      );
    }
  });

  test("should handle white noise (low correlation)", () => {
    const numSamples = 200;
    const signal = new Float32Array(numSamples);

    // Generate white noise
    for (let i = 0; i < numSamples; i++) {
      signal[i] = Math.random() - 0.5;
    }

    const autocorr = DspUtils.autocorrelation(signal);

    // Zero-lag should be large
    const energy = autocorr[0];
    assert.ok(energy > 0, "Energy should be positive");

    // Normalize
    const normalized = autocorr.map((v) => v / energy);

    // Non-zero lags should be small for white noise
    let avgNonZeroLag = 0;
    for (let i = 1; i < Math.min(20, autocorr.length); i++) {
      avgNonZeroLag += Math.abs(normalized[i]);
    }
    avgNonZeroLag /= 19;

    assert.ok(
      avgNonZeroLag < 0.3,
      "White noise should have low autocorrelation at non-zero lags"
    );
  });

  test("should detect periodicity in square wave", () => {
    // Square wave: 8 samples high, 8 samples low
    const period = 16;
    const numPeriods = 4;
    const signal = new Float32Array(period * numPeriods);

    for (let i = 0; i < signal.length; i++) {
      signal[i] = i % period < period / 2 ? 1.0 : -1.0;
    }

    const autocorr = DspUtils.autocorrelation(signal);

    // Normalize
    const normalized = autocorr.map((v) => v / autocorr[0]);

    // Linear autocorrelation naturally decays with lag
    // For n=64 samples, correlation at lag k uses (n-k) terms
    // At period=16: (64-16)/64 = 0.75 of the original energy
    // At 2*period=32: (64-32)/64 = 0.5 of the original energy
    assert.ok(
      normalized[period] > 0.7,
      `Should have strong correlation at period (got ${normalized[
        period
      ].toFixed(3)})`
    );
    assert.ok(
      normalized[period * 2] > 0.45,
      `Should have moderate correlation at 2× period (got ${normalized[
        period * 2
      ].toFixed(3)})`
    );

    // At half period, correlation should be negative (out of phase)
    assert.ok(
      normalized[period / 2] < -0.8,
      `Should have negative correlation at half period (got ${normalized[
        period / 2
      ].toFixed(3)})`
    );
  });

  test("zero-lag value should equal sum of squares", () => {
    const signal = new Float32Array([1.0, 2.0, 3.0, 4.0, 5.0]);
    const autocorr = DspUtils.autocorrelation(signal);

    const sumOfSquares = signal.reduce((sum, val) => sum + val * val, 0);

    assert.ok(
      Math.abs(autocorr[0] - sumOfSquares) < 1e-3,
      `Zero-lag should equal sum of squares: expected ${sumOfSquares}, got ${autocorr[0]}`
    );
  });
});

describe("Autocorrelation - edge cases", () => {
  test("should handle empty array", () => {
    const signal = new Float32Array(0);
    const autocorr = DspUtils.autocorrelation(signal);

    assert.strictEqual(
      autocorr.length,
      0,
      "Empty input should give empty output"
    );
  });

  test("should handle single sample", () => {
    const signal = new Float32Array([3.5]);
    const autocorr = DspUtils.autocorrelation(signal);

    assert.strictEqual(autocorr.length, 1, "Output length should be 1");
    assert.ok(
      Math.abs(autocorr[0] - 3.5 * 3.5) < 1e-6,
      "Single sample autocorr should be value squared"
    );
  });

  test("should handle two samples", () => {
    const signal = new Float32Array([2.0, 3.0]);
    const autocorr = DspUtils.autocorrelation(signal);

    assert.strictEqual(autocorr.length, 2, "Output length should match input");

    // Zero lag: 2² + 3² = 13
    const expectedZeroLag = 2 * 2 + 3 * 3;
    assert.ok(
      Math.abs(autocorr[0] - expectedZeroLag) < 1e-5,
      `Zero-lag should be ${expectedZeroLag}`
    );
  });

  test("should handle all zeros", () => {
    const signal = new Float32Array([0.0, 0.0, 0.0, 0.0]);
    const autocorr = DspUtils.autocorrelation(signal);

    assert.strictEqual(autocorr.length, 4, "Output length should match input");

    // All values should be zero
    for (let i = 0; i < autocorr.length; i++) {
      assert.ok(
        Math.abs(autocorr[i]) < 1e-10,
        `All autocorr values should be ~0 for zero signal, got ${autocorr[i]} at lag ${i}`
      );
    }
  });

  test("should handle very small values", () => {
    const signal = new Float32Array([1e-5, 2e-5, 3e-5, 4e-5]);
    const autocorr = DspUtils.autocorrelation(signal);

    assert.strictEqual(autocorr.length, 4, "Should process small values");
    assert.ok(
      autocorr[0] > 0,
      "Energy should be positive even for small values"
    );
  });

  test("should handle very large values", () => {
    const signal = new Float32Array([1e6, 2e6, 3e6, 4e6]);
    const autocorr = DspUtils.autocorrelation(signal);

    assert.strictEqual(autocorr.length, 4, "Should process large values");
    assert.ok(
      autocorr[0] > 0 && isFinite(autocorr[0]),
      "Energy should be finite and positive"
    );
  });

  test("should handle large arrays", () => {
    const numSamples = 10000;
    const signal = new Float32Array(numSamples);

    for (let i = 0; i < numSamples; i++) {
      signal[i] = Math.sin((2 * Math.PI * i) / 100);
    }

    const autocorr = DspUtils.autocorrelation(signal);

    assert.strictEqual(
      autocorr.length,
      numSamples,
      "Output length should match input"
    );
    assert.ok(autocorr[0] > 0, "Energy should be positive");
  });

  test("should handle negative values", () => {
    const signal = new Float32Array([-5.0, -3.0, -1.0, 1.0, 3.0, 5.0]);
    const autocorr = DspUtils.autocorrelation(signal);

    assert.strictEqual(autocorr.length, 6, "Should handle negative values");
    assert.ok(autocorr[0] > 0, "Energy should be positive");
  });
});

describe("Autocorrelation - validation", () => {
  test("should reject non-Float32Array input", () => {
    assert.throws(
      () => {
        // @ts-expect-error Testing invalid input
        DspUtils.autocorrelation([1, 2, 3, 4, 5]);
      },
      TypeError,
      "Should reject regular array"
    );

    assert.throws(
      () => {
        // @ts-expect-error Testing invalid input
        DspUtils.autocorrelation(new Float64Array([1, 2, 3]));
      },
      TypeError,
      "Should reject Float64Array"
    );
  });

  test("should reject null/undefined inputs", () => {
    assert.throws(
      () => {
        // @ts-expect-error Testing invalid input
        DspUtils.autocorrelation(null);
      },
      TypeError,
      "Should reject null"
    );

    assert.throws(
      () => {
        // @ts-expect-error Testing invalid input
        DspUtils.autocorrelation(undefined);
      },
      TypeError,
      "Should reject undefined"
    );
  });
});

describe("Autocorrelation - real-world scenarios", () => {
  test("should detect pitch in synthetic voice signal", () => {
    // Simulate vocal cords at 150 Hz (typical male voice)
    const sampleRate = 8000; // 8 kHz sampling
    const pitch = 150; // Hz
    const duration = 0.2; // 200ms
    const numSamples = Math.floor(sampleRate * duration);

    const signal = new Float32Array(numSamples);
    for (let i = 0; i < numSamples; i++) {
      // Fundamental + harmonics
      signal[i] =
        Math.sin((2 * Math.PI * pitch * i) / sampleRate) +
        0.5 * Math.sin((2 * Math.PI * pitch * 2 * i) / sampleRate) +
        0.3 * Math.sin((2 * Math.PI * pitch * 3 * i) / sampleRate);
    }

    const autocorr = DspUtils.autocorrelation(signal);
    const normalized = autocorr.map((v) => v / autocorr[0]);

    // Expected period in samples
    const expectedPeriod = Math.round(sampleRate / pitch); // ~53 samples

    // Find peak near expected period (allow ±10% tolerance)
    const minLag = Math.floor(expectedPeriod * 0.9);
    const maxLag = Math.ceil(expectedPeriod * 1.1);

    let peakLag = minLag;
    let peakVal = normalized[minLag];

    for (let lag = minLag; lag <= maxLag; lag++) {
      if (normalized[lag] > peakVal) {
        peakVal = normalized[lag];
        peakLag = lag;
      }
    }

    const detectedPitch = sampleRate / peakLag;

    assert.ok(
      Math.abs(detectedPitch - pitch) < pitch * 0.15,
      `Detected pitch ${detectedPitch} Hz should be close to ${pitch} Hz`
    );
    assert.ok(
      peakVal > 0.6,
      "Peak correlation should be strong for periodic signal"
    );
  });

  test("should detect heartbeat period in ECG-like signal", () => {
    // Simulate ECG at 75 bpm (1.25 Hz)
    const sampleRate = 250; // 250 Hz sampling (common for ECG)
    const bpm = 75;
    const heartRate = bpm / 60; // 1.25 Hz
    const numBeats = 5;
    const duration = numBeats / heartRate;
    const numSamples = Math.floor(sampleRate * duration);

    const signal = new Float32Array(numSamples);
    const period = sampleRate / heartRate; // ~200 samples

    for (let i = 0; i < numSamples; i++) {
      // Simplified QRS complex pattern
      const phase = (i % period) / period;
      if (phase < 0.1) {
        // QRS spike
        signal[i] = Math.sin(phase * 10 * Math.PI);
      } else {
        // Baseline
        signal[i] = 0.1 * Math.sin((2 * Math.PI * i) / period);
      }
    }

    const autocorr = DspUtils.autocorrelation(signal);
    const normalized = autocorr.map((v) => v / autocorr[0]);

    // Find peak in physiological range (40-200 bpm)
    const minPeriod = Math.floor(sampleRate / (200 / 60)); // 200 bpm
    const maxPeriod = Math.floor(sampleRate / (40 / 60)); // 40 bpm

    let peakLag = minPeriod;
    let peakVal = normalized[minPeriod];

    for (let lag = minPeriod; lag <= maxPeriod; lag++) {
      if (normalized[lag] > peakVal) {
        peakVal = normalized[lag];
        peakLag = lag;
      }
    }

    const detectedBPM = (sampleRate / peakLag) * 60;

    assert.ok(
      Math.abs(detectedBPM - bpm) < 10,
      `Detected BPM ${detectedBPM} should be close to ${bpm}`
    );
  });

  test("should identify echo delay in reflected signal", () => {
    // Original pulse + echo
    const sampleRate = 1000; // 1 kHz
    const pulseWidth = 10; // samples
    const echoDelay = 50; // samples (50 ms at 1 kHz)
    const numSamples = 200;

    const signal = new Float32Array(numSamples);

    // Main pulse at t=20
    for (let i = 20; i < 20 + pulseWidth; i++) {
      signal[i] = 1.0;
    }

    // Echo at t=20+50=70 with 50% amplitude
    for (let i = 70; i < 70 + pulseWidth; i++) {
      signal[i] = 0.5;
    }

    const autocorr = DspUtils.autocorrelation(signal);

    // Should have correlation peak at echo delay
    // Look for secondary peak around delay=50
    const searchStart = 40;
    const searchEnd = 60;

    let peakLag = searchStart;
    let peakVal = autocorr[searchStart];

    for (let lag = searchStart; lag <= searchEnd; lag++) {
      if (autocorr[lag] > peakVal) {
        peakVal = autocorr[lag];
        peakLag = lag;
      }
    }

    assert.ok(
      Math.abs(peakLag - echoDelay) <= 2,
      `Detected echo delay ${peakLag} should be close to ${echoDelay} samples`
    );
  });

  test("should work with AM radio-like signal", () => {
    // Amplitude modulated carrier
    const sampleRate = 44100; // 44.1 kHz
    const carrierFreq = 1000; // 1 kHz carrier
    const modulationFreq = 10; // 10 Hz modulation
    const duration = 0.5; // 500 ms
    const numSamples = Math.floor(sampleRate * duration);

    const signal = new Float32Array(numSamples);

    for (let i = 0; i < numSamples; i++) {
      const carrier = Math.sin((2 * Math.PI * carrierFreq * i) / sampleRate);
      const envelope =
        0.5 + 0.5 * Math.sin((2 * Math.PI * modulationFreq * i) / sampleRate);
      signal[i] = carrier * envelope;
    }

    const autocorr = DspUtils.autocorrelation(signal);

    // Should detect carrier periodicity
    const expectedCarrierPeriod = Math.round(sampleRate / carrierFreq); // ~44 samples
    const normalized = autocorr.map((v) => v / autocorr[0]);

    assert.ok(
      normalized[expectedCarrierPeriod] > 0.5,
      "Should detect carrier frequency correlation"
    );
  });
});

describe("Autocorrelation - symmetry and properties", () => {
  test("should be symmetric for real signals (mathematically)", () => {
    // Note: We only return positive lags, so we can't directly test symmetry
    // But we can verify that the computation is correct
    const signal = new Float32Array([1.0, 2.0, 3.0, 2.0, 1.0]);
    const autocorr = DspUtils.autocorrelation(signal);

    // At minimum, verify output length and energy
    assert.strictEqual(autocorr.length, 5, "Output length should match input");
    assert.ok(autocorr[0] > 0, "Energy should be positive");
  });

  test("normalized autocorrelation should be in [-1, 1]", () => {
    const signal = new Float32Array(50);
    for (let i = 0; i < signal.length; i++) {
      signal[i] = Math.sin((2 * Math.PI * i) / 10) + Math.random() * 0.1;
    }

    const autocorr = DspUtils.autocorrelation(signal);
    const normalized = autocorr.map((v) => v / autocorr[0]);

    // Check that all normalized values are in valid range
    for (let i = 0; i < normalized.length; i++) {
      assert.ok(
        normalized[i] >= -1.1 && normalized[i] <= 1.1,
        `Normalized autocorr at lag ${i} should be in [-1, 1], got ${normalized[i]}`
      );
    }

    // Zero-lag should be 1.0 after normalization
    assert.ok(
      Math.abs(normalized[0] - 1.0) < 1e-6,
      "Normalized zero-lag should be 1.0"
    );
  });

  test("should decay for non-periodic signals", () => {
    // Damped sinusoid
    const numSamples = 100;
    const signal = new Float32Array(numSamples);

    for (let i = 0; i < numSamples; i++) {
      const decay = Math.exp(-i / 30);
      signal[i] = decay * Math.sin((2 * Math.PI * i) / 10);
    }

    const autocorr = DspUtils.autocorrelation(signal);
    const normalized = autocorr.map((v) => v / autocorr[0]);

    // Should see decay in autocorrelation
    const lag50 = Math.abs(normalized[50]);
    const lag10 = Math.abs(normalized[10]);

    assert.ok(lag50 < lag10, "Autocorrelation should decay for damped signal");
  });
});

describe("Autocorrelation - comparison with naive method", () => {
  test("should match naive autocorrelation for small signals", () => {
    const signal = new Float32Array([1.0, 2.0, 3.0, 2.0, 1.0]);

    // FFT-based
    const autocorrFFT = DspUtils.autocorrelation(signal);

    // Naive direct computation (for first few lags)
    const naiveAutocorr = new Float32Array(signal.length);
    for (let lag = 0; lag < signal.length; lag++) {
      let sum = 0;
      for (let i = 0; i < signal.length - lag; i++) {
        sum += signal[i] * signal[i + lag];
      }
      naiveAutocorr[lag] = sum;
    }

    // Compare first few lags
    for (let lag = 0; lag < 3; lag++) {
      const diff = Math.abs(autocorrFFT[lag] - naiveAutocorr[lag]);
      const relError = diff / Math.max(Math.abs(naiveAutocorr[lag]), 1e-10);

      assert.ok(
        relError < 0.01,
        `Lag ${lag}: FFT result ${autocorrFFT[lag]} should match naive ${naiveAutocorr[lag]}`
      );
    }
  });
});
