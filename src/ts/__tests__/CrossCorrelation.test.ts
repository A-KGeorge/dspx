import { describe, test } from "node:test";
import assert from "node:assert";
import { DspUtils } from "../utils.js";

describe("CrossCorrelation", () => {
  describe("Basic Functionality", () => {
    test("should compute cross-correlation of identical signals", () => {
      const signal = new Float32Array([1.0, 2.0, 3.0, 4.0, 5.0]);
      const xcorr = DspUtils.crossCorrelation(signal, signal);

      // Cross-correlation of identical signals should equal autocorrelation
      const autocorr = DspUtils.autocorrelation(signal);

      assert.strictEqual(xcorr.length, signal.length);
      for (let i = 0; i < xcorr.length; i++) {
        assert.ok(
          Math.abs(xcorr[i] - autocorr[i]) < 0.01,
          `Lag ${i}: xcorr=${xcorr[i]}, autocorr=${autocorr[i]}`
        );
      }
    });

    test("should detect time delay between shifted signals", () => {
      // Create a signal with a clear peak
      const n = 64;
      const signal = new Float32Array(n);
      for (let i = 0; i < n; i++) {
        signal[i] = Math.exp(-((i - 20) ** 2) / 50); // Gaussian at position 20
      }

      // Shift signal by 10 samples
      const delay = 10;
      const shifted = new Float32Array(n);
      for (let i = delay; i < n; i++) {
        shifted[i] = signal[i - delay];
      }

      const xcorr = DspUtils.crossCorrelation(signal, shifted);

      // Find peak in cross-correlation
      let maxCorr = -Infinity;
      let peakLag = 0;
      for (let i = 0; i < xcorr.length; i++) {
        if (xcorr[i] > maxCorr) {
          maxCorr = xcorr[i];
          peakLag = i;
        }
      }

      // Peak should occur at the delay
      assert.ok(
        Math.abs(peakLag - delay) <= 1,
        `Expected peak at lag ${delay}, got ${peakLag}`
      );
    });

    test("should compute cross-correlation of orthogonal signals", () => {
      // Create two orthogonal signals (sin and cos)
      const n = 100;
      const sin = new Float32Array(n);
      const cos = new Float32Array(n);

      for (let i = 0; i < n; i++) {
        sin[i] = Math.sin((2 * Math.PI * i) / 20);
        cos[i] = Math.cos((2 * Math.PI * i) / 20);
      }

      const xcorr = DspUtils.crossCorrelation(sin, cos);

      // Orthogonal signals should have low correlation at lag 0
      // Normalize by signal energies
      const energySin = DspUtils.dotProduct(sin, sin);
      const energyCos = DspUtils.dotProduct(cos, cos);
      const normalized = Math.abs(xcorr[0]) / Math.sqrt(energySin * energyCos);
      assert.ok(
        normalized < 0.5,
        "Orthogonal signals should have low correlation"
      );
    });

    test("should handle constant signals", () => {
      const x = new Float32Array([3.0, 3.0, 3.0, 3.0]);
      const y = new Float32Array([2.0, 2.0, 2.0, 2.0]);
      const xcorr = DspUtils.crossCorrelation(x, y);

      // For constant signals, xcorr[k] = (n-k) * x_val * y_val
      const n = x.length;
      for (let k = 0; k < n; k++) {
        const expected = (n - k) * 3.0 * 2.0;
        assert.ok(
          Math.abs(xcorr[k] - expected) < 0.1,
          `Lag ${k}: expected ${expected}, got ${xcorr[k]}`
        );
      }
    });

    test("zero-lag value should equal dot product", () => {
      const x = new Float32Array([1.0, 2.0, 3.0, 4.0, 5.0]);
      const y = new Float32Array([2.0, 1.0, 3.0, 2.0, 1.0]);
      const xcorr = DspUtils.crossCorrelation(x, y);

      // xcorr[0] should equal sum(x[i] * y[i])
      const dotProd = DspUtils.dotProduct(x, y);

      assert.ok(
        Math.abs(xcorr[0] - dotProd) < 0.01,
        `xcorr[0]=${xcorr[0]} should equal dot product=${dotProd}`
      );
    });
  });

  describe("Edge Cases", () => {
    test("should handle empty signals", () => {
      const empty = new Float32Array(0);
      const xcorr = DspUtils.crossCorrelation(empty, empty);

      assert.strictEqual(xcorr.length, 0);
    });

    test("should handle single sample", () => {
      const x = new Float32Array([5.0]);
      const y = new Float32Array([3.0]);
      const xcorr = DspUtils.crossCorrelation(x, y);

      assert.strictEqual(xcorr.length, 1);
      assert.strictEqual(xcorr[0], 15.0); // 5 * 3
    });

    test("should handle two samples", () => {
      const x = new Float32Array([2.0, 3.0]);
      const y = new Float32Array([1.0, 4.0]);
      const xcorr = DspUtils.crossCorrelation(x, y);

      assert.strictEqual(xcorr.length, 2);
      // xcorr[0] = x[0]*y[0] + x[1]*y[1] = 2*1 + 3*4 = 14
      assert.ok(Math.abs(xcorr[0] - 14.0) < 0.01);
      // xcorr[1] = x[0]*y[1] = 2*4 = 8 (only one overlap)
      assert.ok(Math.abs(xcorr[1] - 8.0) < 0.01);
    });

    test("should handle all zeros", () => {
      const zeros = new Float32Array([0, 0, 0, 0, 0]);
      const xcorr = DspUtils.crossCorrelation(zeros, zeros);

      assert.strictEqual(xcorr.length, 5);
      for (let i = 0; i < xcorr.length; i++) {
        assert.strictEqual(xcorr[i], 0);
      }
    });

    test("should handle very small values", () => {
      const x = new Float32Array([1e-10, 2e-10, 3e-10]);
      const y = new Float32Array([2e-10, 1e-10, 4e-10]);
      const xcorr = DspUtils.crossCorrelation(x, y);

      assert.strictEqual(xcorr.length, 3);
      assert.ok(xcorr[0] < 1e-18); // Very small but not necessarily zero
    });

    test("should handle very large values", () => {
      const x = new Float32Array([1e6, 2e6, 3e6]);
      const y = new Float32Array([2e6, 1e6, 2e6]);
      const xcorr = DspUtils.crossCorrelation(x, y);

      assert.strictEqual(xcorr.length, 3);
      assert.ok(xcorr[0] > 1e12); // Should produce large values
    });

    test("should handle large arrays", () => {
      const n = 10000;
      const x = new Float32Array(n);
      const y = new Float32Array(n);

      for (let i = 0; i < n; i++) {
        x[i] = Math.sin((2 * Math.PI * i) / 100);
        y[i] = Math.cos((2 * Math.PI * i) / 100);
      }

      const xcorr = DspUtils.crossCorrelation(x, y);
      assert.strictEqual(xcorr.length, n);
    });

    test("should handle negative values", () => {
      const x = new Float32Array([-1.0, -2.0, -3.0]);
      const y = new Float32Array([2.0, -1.0, 3.0]);
      const xcorr = DspUtils.crossCorrelation(x, y);

      assert.strictEqual(xcorr.length, 3);
      // xcorr[0] = (-1)*2 + (-2)*(-1) + (-3)*3 = -2 + 2 - 9 = -9
      assert.ok(Math.abs(xcorr[0] - -9.0) < 0.01);
    });
  });

  describe("Validation", () => {
    test("should reject non-Float32Array first argument", () => {
      const validArray = new Float32Array([1, 2, 3]);
      const invalidArray = [1, 2, 3] as any;

      assert.throws(
        () => DspUtils.crossCorrelation(invalidArray, validArray),
        TypeError,
        "Should reject regular array as first signal"
      );

      const float64 = new Float64Array([1, 2, 3]) as any;
      assert.throws(
        () => DspUtils.crossCorrelation(float64, validArray),
        TypeError,
        "Should reject Float64Array as first signal"
      );
    });

    test("should reject non-Float32Array second argument", () => {
      const validArray = new Float32Array([1, 2, 3]);
      const invalidArray = [1, 2, 3] as any;

      assert.throws(
        () => DspUtils.crossCorrelation(validArray, invalidArray),
        TypeError,
        "Should reject regular array as second signal"
      );

      const float64 = new Float64Array([1, 2, 3]) as any;
      assert.throws(
        () => DspUtils.crossCorrelation(validArray, float64),
        TypeError,
        "Should reject Float64Array as second signal"
      );
    });

    test("should reject signals of different lengths", () => {
      const x = new Float32Array([1, 2, 3]);
      const y = new Float32Array([1, 2, 3, 4]);

      assert.throws(
        () => DspUtils.crossCorrelation(x, y),
        RangeError,
        "Should reject signals with different lengths"
      );
    });

    test("should reject null/undefined", () => {
      const valid = new Float32Array([1, 2, 3]);

      assert.throws(
        () => DspUtils.crossCorrelation(null as any, valid),
        TypeError
      );

      assert.throws(
        () => DspUtils.crossCorrelation(valid, null as any),
        TypeError
      );

      assert.throws(
        () => DspUtils.crossCorrelation(undefined as any, valid),
        TypeError
      );

      assert.throws(
        () => DspUtils.crossCorrelation(valid, undefined as any),
        TypeError
      );
    });
  });

  describe("Real-World Scenarios", () => {
    test("should detect echo delay in audio", () => {
      // Simulate audio with echo: original + delayed copy
      const n = 500;
      const sampleRate = 16000; // Hz
      const echoDelay = 50; // samples (~3.1 ms at 16kHz)

      const original = new Float32Array(n);
      const withEcho = new Float32Array(n);

      // Generate impulse at position 100
      for (let i = 0; i < n; i++) {
        original[i] = i === 100 ? 1.0 : 0.0;
        withEcho[i] = original[i];
      }

      // Add echo at delay
      for (let i = echoDelay; i < n; i++) {
        withEcho[i] += 0.5 * original[i - echoDelay];
      }

      const xcorr = DspUtils.crossCorrelation(original, withEcho);

      // Find secondary peak (first peak is at lag 0)
      let peaks: { lag: number; value: number }[] = [];
      for (let i = 10; i < Math.min(100, xcorr.length); i++) {
        if (
          xcorr[i] > xcorr[i - 1] &&
          xcorr[i] > xcorr[i + 1] &&
          xcorr[i] > 0.1
        ) {
          peaks.push({ lag: i, value: xcorr[i] });
        }
      }

      // Should detect echo at the delay
      const detected = peaks.some((p) => Math.abs(p.lag - echoDelay) <= 2);
      assert.ok(detected, `Should detect echo near lag ${echoDelay}`);
    });

    test("should align two sensor measurements", () => {
      // Simulate two sensors measuring the same event with different delays
      const n = 200;
      const signal = new Float32Array(n);

      // Generate a signal with a clear feature
      for (let i = 0; i < n; i++) {
        signal[i] = Math.exp(-((i - 50) ** 2) / 100);
      }

      // Sensor 1: no delay
      const sensor1 = new Float32Array(signal);

      // Sensor 2: delayed by 15 samples with noise
      const delay = 15;
      const sensor2 = new Float32Array(n);
      for (let i = delay; i < n; i++) {
        sensor2[i] = signal[i - delay] + 0.05 * (Math.random() - 0.5);
      }

      const xcorr = DspUtils.crossCorrelation(sensor1, sensor2);

      // Find peak
      let maxCorr = -Infinity;
      let detectedDelay = 0;
      for (let i = 0; i < xcorr.length; i++) {
        if (xcorr[i] > maxCorr) {
          maxCorr = xcorr[i];
          detectedDelay = i;
        }
      }

      assert.ok(
        Math.abs(detectedDelay - delay) <= 2,
        `Expected delay ${delay}, detected ${detectedDelay}`
      );
    });

    test("should perform template matching", () => {
      // Create a long signal with a template embedded at position 150
      const signalLength = 500;
      const templateLength = 20;
      const templatePosition = 150;

      const signal = new Float32Array(signalLength);
      const template = new Float32Array(signalLength); // Padded template

      // Generate template: simple triangle wave
      for (let i = 0; i < templateLength; i++) {
        const val = i < templateLength / 2 ? i : templateLength - i;
        template[i] = val / (templateLength / 2);
      }

      // Embed template in signal with noise
      for (let i = 0; i < signalLength; i++) {
        signal[i] = 0.1 * (Math.random() - 0.5); // Background noise
      }
      for (let i = 0; i < templateLength; i++) {
        signal[templatePosition + i] += template[i];
      }

      const xcorr = DspUtils.crossCorrelation(template, signal);

      // Find peak (should be near template position)
      let maxCorr = -Infinity;
      let detectedPos = 0;
      for (let i = 0; i < xcorr.length; i++) {
        if (xcorr[i] > maxCorr) {
          maxCorr = xcorr[i];
          detectedPos = i;
        }
      }

      assert.ok(
        Math.abs(detectedPos - templatePosition) <= 5,
        `Expected template at ${templatePosition}, detected at ${detectedPos}`
      );
    });

    test("should measure signal similarity with different phases", () => {
      // Two sine waves with same frequency but different phases
      const n = 128;
      const freq = 5; // cycles in the window

      const signal1 = new Float32Array(n);
      const signal2 = new Float32Array(n);

      for (let i = 0; i < n; i++) {
        signal1[i] = Math.sin((2 * Math.PI * freq * i) / n);
        signal2[i] = Math.sin((2 * Math.PI * freq * i) / n + Math.PI / 4); // 45° phase shift
      }

      const xcorr = DspUtils.crossCorrelation(signal1, signal2);

      // Normalize
      const energy1 = DspUtils.dotProduct(signal1, signal1);
      const energy2 = DspUtils.dotProduct(signal2, signal2);
      const normalized = xcorr.map((v) => v / Math.sqrt(energy1 * energy2));

      // Peak should be high (same frequency) but not at lag 0 (different phase)
      const maxCorr = Math.max(...Array.from(normalized));
      assert.ok(
        maxCorr > 0.8,
        "Should have high correlation for same frequency signals"
      );
    });
  });

  describe("Symmetry and Properties", () => {
    test("should satisfy xcorr(x,y)[0] = dot(x,y)", () => {
      const x = new Float32Array([1, 2, 3, 4, 5]);
      const y = new Float32Array([5, 4, 3, 2, 1]);

      const xcorr = DspUtils.crossCorrelation(x, y);
      const dot = DspUtils.dotProduct(x, y);

      assert.ok(
        Math.abs(xcorr[0] - dot) < 0.01,
        `xcorr[0]=${xcorr[0]} should equal dot=${dot}`
      );
    });

    test("should satisfy xcorr(x,x) = autocorr(x)", () => {
      const signal = new Float32Array([2, 3, 1, 4, 2, 5]);
      const xcorr = DspUtils.crossCorrelation(signal, signal);
      const autocorr = DspUtils.autocorrelation(signal);

      for (let i = 0; i < signal.length; i++) {
        assert.ok(
          Math.abs(xcorr[i] - autocorr[i]) < 0.01,
          `Lag ${i}: xcorr=${xcorr[i]}, autocorr=${autocorr[i]}`
        );
      }
    });

    test("should have maximum at lag 0 for identical signals", () => {
      const signal = new Float32Array([1, 2, 3, 4, 5, 4, 3, 2, 1]);
      const xcorr = DspUtils.crossCorrelation(signal, signal);

      const maxValue = Math.max(...Array.from(xcorr));
      assert.ok(
        Math.abs(xcorr[0] - maxValue) < 0.01,
        "Max should occur at lag 0 for identical signals"
      );
    });

    test("normalized cross-correlation should be bounded by [-1, 1]", () => {
      const x = new Float32Array(50);
      const y = new Float32Array(50);

      for (let i = 0; i < 50; i++) {
        x[i] = Math.random() - 0.5;
        y[i] = Math.random() - 0.5;
      }

      const xcorr = DspUtils.crossCorrelation(x, y);
      const energyX = DspUtils.dotProduct(x, x);
      const energyY = DspUtils.dotProduct(y, y);
      const normalized = xcorr.map((v) => v / Math.sqrt(energyX * energyY));

      for (let i = 0; i < normalized.length; i++) {
        assert.ok(
          normalized[i] >= -1.01 && normalized[i] <= 1.01,
          `Normalized value at lag ${i} should be in [-1,1], got ${normalized[i]}`
        );
      }
    });
  });

  describe("Comparison with Naive Implementation", () => {
    test("should match naive computation for small signals", () => {
      const x = new Float32Array([1, 2, 3, 4]);
      const y = new Float32Array([4, 3, 2, 1]);

      const xcorrFFT = DspUtils.crossCorrelation(x, y);

      // Naive cross-correlation
      const xcorrNaive = new Float32Array(x.length);
      for (let lag = 0; lag < x.length; lag++) {
        let sum = 0;
        for (let i = 0; i < x.length - lag; i++) {
          sum += x[i] * y[i + lag];
        }
        xcorrNaive[lag] = sum;
      }

      for (let i = 0; i < x.length; i++) {
        assert.ok(
          Math.abs(xcorrFFT[i] - xcorrNaive[i]) < 0.01,
          `Lag ${i}: FFT=${xcorrFFT[i]}, Naive=${xcorrNaive[i]}`
        );
      }
    });
  });
});
