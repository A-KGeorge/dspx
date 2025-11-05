import { describe, test } from "node:test";
import assert from "node:assert";
import { DspUtils } from "../utils.js";

describe("Detrend - basic functionality", () => {
  test("should remove linear trend from signal with positive slope", () => {
    // Signal: y = 0.5x + 1 (slope=0.5, intercept=1)
    const signal = new Float32Array([1.0, 1.5, 2.0, 2.5, 3.0]);
    const detrended = DspUtils.detrend(signal, { type: "linear" });

    // After detrending, mean should be ~0
    const mean =
      detrended.reduce((sum, val) => sum + val, 0) / detrended.length;
    assert.ok(
      Math.abs(mean) < 1e-6,
      `Mean should be ~0 after linear detrend, got ${mean}`
    );

    // Variance should be very small (straight line becomes flat)
    const variance =
      detrended.reduce((sum, val) => sum + val * val, 0) / detrended.length;
    assert.ok(
      variance < 1e-10,
      `Variance should be near zero for perfect line, got ${variance}`
    );
  });

  test("should remove linear trend from signal with negative slope", () => {
    // Signal: y = -0.2x + 5 (negative slope)
    const signal = new Float32Array([5.0, 4.8, 4.6, 4.4, 4.2]);
    const detrended = DspUtils.detrend(signal, { type: "linear" });

    const mean =
      detrended.reduce((sum, val) => sum + val, 0) / detrended.length;
    assert.ok(Math.abs(mean) < 1e-6, "Mean should be ~0 after detrending");

    const variance =
      detrended.reduce((sum, val) => sum + val * val, 0) / detrended.length;
    assert.ok(
      variance < 1e-10,
      `Straight line should have near-zero variance, got ${variance}`
    );
  });

  test("should handle constant signal (zero slope)", () => {
    const signal = new Float32Array([5.0, 5.0, 5.0, 5.0, 5.0]);
    const detrended = DspUtils.detrend(signal, { type: "linear" });

    // Constant signal should become all zeros
    for (let i = 0; i < detrended.length; i++) {
      assert.ok(
        Math.abs(detrended[i]) < 1e-6,
        `Detrended constant should be ~0, got ${detrended[i]}`
      );
    }
  });

  test("should preserve oscillations around trend", () => {
    // Signal with trend + oscillation: y = x + sin(x)
    const signal = new Float32Array(10);
    for (let i = 0; i < signal.length; i++) {
      signal[i] = i + Math.sin(i * 0.5);
    }

    const detrended = DspUtils.detrend(signal, { type: "linear" });

    // The oscillation component should be preserved
    // Check that detrended signal still has variation
    const variance =
      detrended.reduce((sum, val) => sum + val * val, 0) / detrended.length;
    assert.ok(
      variance > 0.01,
      "Oscillations should be preserved after detrending"
    );
  });

  test("should default to linear detrending when no type specified", () => {
    const signal = new Float32Array([1.0, 2.0, 3.0, 4.0, 5.0]);
    const detrended = DspUtils.detrend(signal); // No type specified

    const mean =
      detrended.reduce((sum, val) => sum + val, 0) / detrended.length;
    assert.ok(Math.abs(mean) < 1e-6, "Should use linear detrending by default");
  });
});

describe("Detrend - constant detrending", () => {
  test("should remove mean (constant detrending)", () => {
    const signal = new Float32Array([10.1, 10.2, 9.9, 10.0, 10.3]);
    const detrended = DspUtils.detrend(signal, { type: "constant" });

    // Mean should be zero
    const mean =
      detrended.reduce((sum, val) => sum + val, 0) / detrended.length;
    assert.ok(
      Math.abs(mean) < 1e-6,
      `Mean should be ~0 after constant detrend, got ${mean}`
    );

    // Original mean was 10.1
    const originalMean =
      signal.reduce((sum, val) => sum + val, 0) / signal.length;
    assert.ok(
      Math.abs(originalMean - 10.1) < 0.1,
      "Original mean should be ~10.1"
    );
  });

  test("should preserve variance with constant detrending", () => {
    const signal = new Float32Array([5.5, 6.0, 4.5, 5.0, 6.5]);

    // Calculate original variance (around mean)
    const originalMean =
      signal.reduce((sum, val) => sum + val, 0) / signal.length;
    const originalVariance =
      signal.reduce((sum, val) => sum + (val - originalMean) ** 2, 0) /
      signal.length;

    const detrended = DspUtils.detrend(signal, { type: "constant" });

    // Calculate detrended variance (around 0)
    const detrendedVariance =
      detrended.reduce((sum, val) => sum + val ** 2, 0) / detrended.length;

    // Variances should be equal (only mean removed)
    assert.ok(
      Math.abs(originalVariance - detrendedVariance) < 1e-6,
      "Constant detrending should preserve variance"
    );
  });

  test("constant detrending should be faster for no-trend signals", () => {
    // For signals with no trend, constant detrending should work fine
    const signal = new Float32Array([5.1, 5.3, 4.9, 5.0, 5.2]);
    const detrendedConstant = DspUtils.detrend(signal, { type: "constant" });
    const detrendedLinear = DspUtils.detrend(signal, { type: "linear" });

    // Both should give similar results for no-trend signal
    const diffSum = detrendedConstant.reduce(
      (sum, val, i) => sum + Math.abs(val - detrendedLinear[i]),
      0
    );
    const avgDiff = diffSum / signal.length;

    assert.ok(
      avgDiff < 0.1,
      "Constant and linear should give similar results for trendless signal"
    );
  });
});

describe("Detrend - edge cases", () => {
  test("should handle single sample", () => {
    const signal = new Float32Array([42.5]);
    const detrended = DspUtils.detrend(signal, { type: "linear" });

    // Single sample should become 0 (signal - mean)
    assert.ok(Math.abs(detrended[0]) < 1e-6, "Single sample should be ~0");
    assert.strictEqual(detrended.length, 1, "Output length should match input");
  });

  test("should handle two samples", () => {
    const signal = new Float32Array([1.0, 3.0]);
    const detrended = DspUtils.detrend(signal, { type: "linear" });

    // For two samples, the fitted line passes exactly through both points
    // So detrending produces [0, 0] (all residuals are zero)
    assert.strictEqual(detrended.length, 2);
    assert.ok(Math.abs(detrended[0]) < 1e-10, "First residual should be ~0");
    assert.ok(Math.abs(detrended[1]) < 1e-10, "Second residual should be ~0");
  });

  test("should handle empty array", () => {
    const signal = new Float32Array(0);
    const detrended = DspUtils.detrend(signal, { type: "linear" });

    assert.strictEqual(
      detrended.length,
      0,
      "Empty input should give empty output"
    );
  });

  test("should handle large arrays", () => {
    const size = 10000;
    const signal = new Float32Array(size);
    for (let i = 0; i < size; i++) {
      signal[i] = 0.001 * i + Math.random() * 0.1; // Small trend + noise
    }

    const detrended = DspUtils.detrend(signal, { type: "linear" });

    assert.strictEqual(
      detrended.length,
      size,
      "Output length should match input"
    );

    // Mean should be ~0
    const mean = detrended.reduce((sum, val) => sum + val, 0) / size;
    assert.ok(Math.abs(mean) < 1e-3, "Mean should be near zero");
  });

  test("should handle negative values", () => {
    const signal = new Float32Array([-5.0, -3.0, -1.0, 1.0, 3.0]);
    const detrended = DspUtils.detrend(signal, { type: "linear" });

    const mean =
      detrended.reduce((sum, val) => sum + val, 0) / detrended.length;
    assert.ok(Math.abs(mean) < 1e-6, "Should handle negative values correctly");
  });

  test("should handle very small values", () => {
    const signal = new Float32Array([1e-6, 2e-6, 3e-6, 4e-6, 5e-6]);
    const detrended = DspUtils.detrend(signal, { type: "linear" });

    // Should work with small values
    assert.strictEqual(detrended.length, 5, "Should process small values");
    const mean =
      detrended.reduce((sum, val) => sum + val, 0) / detrended.length;
    assert.ok(Math.abs(mean) < 1e-10, "Mean should be near zero");
  });

  test("should handle very large values", () => {
    const signal = new Float32Array([1e6, 2e6, 3e6, 4e6, 5e6]);
    const detrended = DspUtils.detrend(signal, { type: "linear" });

    const mean =
      detrended.reduce((sum, val) => sum + val, 0) / detrended.length;
    assert.ok(Math.abs(mean) < 1e-3, "Should handle large values correctly");
  });
});

describe("Detrend - validation", () => {
  test("should reject non-Float32Array input", () => {
    assert.throws(
      () => {
        // @ts-expect-error Testing invalid input
        DspUtils.detrend([1, 2, 3, 4, 5]);
      },
      TypeError,
      "Should reject regular array"
    );

    assert.throws(
      () => {
        // @ts-expect-error Testing invalid input
        DspUtils.detrend(new Float64Array([1, 2, 3]));
      },
      TypeError,
      "Should reject Float64Array"
    );
  });

  test("should reject invalid detrend type", () => {
    const signal = new Float32Array([1, 2, 3, 4, 5]);

    assert.throws(
      () => {
        // @ts-expect-error Testing invalid input
        DspUtils.detrend(signal, { type: "quadratic" });
      },
      TypeError,
      "Should reject invalid type"
    );

    assert.throws(
      () => {
        // @ts-expect-error Testing invalid input
        DspUtils.detrend(signal, { type: "polynomial" });
      },
      TypeError,
      "Should reject invalid type"
    );
  });

  test("should handle null/undefined inputs gracefully", () => {
    assert.throws(
      () => {
        // @ts-expect-error Testing invalid input
        DspUtils.detrend(null);
      },
      TypeError,
      "Should reject null"
    );

    assert.throws(
      () => {
        // @ts-expect-error Testing invalid input
        DspUtils.detrend(undefined);
      },
      TypeError,
      "Should reject undefined"
    );
  });
});

describe("Detrend - real-world scenarios", () => {
  test("should remove baseline drift from EEG-like signal", () => {
    // Simulate EEG with slow baseline drift + fast oscillations
    const numSamples = 100;
    const signal = new Float32Array(numSamples);

    for (let i = 0; i < numSamples; i++) {
      const drift = 0.05 * i; // Linear drift
      const alpha = Math.sin((2 * Math.PI * i * 10) / numSamples); // 10 Hz oscillation
      signal[i] = drift + alpha * 0.5;
    }

    const detrended = DspUtils.detrend(signal, { type: "linear" });

    // After detrending, mean should be ~0
    const mean =
      detrended.reduce((sum, val) => sum + val, 0) / detrended.length;
    assert.ok(
      Math.abs(mean) < 0.01,
      `EEG detrending should center signal, got mean ${mean}`
    );

    // Oscillations should be preserved
    const maxAbs = Math.max(...detrended.map(Math.abs));
    assert.ok(
      maxAbs > 0.3,
      "Alpha oscillations should be preserved after detrending"
    );
  });

  test("should remove DC offset from sensor data", () => {
    // Simulate temperature sensor with constant offset
    const signal = new Float32Array([
      25.1, 25.3, 24.9, 25.0, 25.2, 24.8, 25.1, 25.0,
    ]);

    const detrended = DspUtils.detrend(signal, { type: "constant" });

    // After removing DC, mean should be 0
    const mean =
      detrended.reduce((sum, val) => sum + val, 0) / detrended.length;
    assert.ok(Math.abs(mean) < 1e-6, "DC offset should be removed (mean ~0)");

    // Variations around mean should be preserved
    const stdDev = Math.sqrt(
      detrended.reduce((sum, val) => sum + val ** 2, 0) / detrended.length
    );
    assert.ok(stdDev > 0.05, "Temperature variations should be preserved");
  });

  test("should improve spectral analysis by removing trend", () => {
    // Signal with strong trend can cause spectral leakage
    const numSamples = 128;
    const signal = new Float32Array(numSamples);

    for (let i = 0; i < numSamples; i++) {
      const trend = 0.1 * i; // Strong linear trend
      const sine = Math.sin((2 * Math.PI * i * 5) / numSamples); // 5 Hz signal
      signal[i] = trend + sine;
    }

    const detrended = DspUtils.detrend(signal, { type: "linear" });

    // After detrending, trend should be removed but sine preserved
    const maxAbs = Math.max(...detrended.map(Math.abs));
    assert.ok(
      maxAbs < 2.0,
      "Detrended signal should not have large trend component"
    );

    // Check that sine component is still present (amplitude ~1)
    const minVal = Math.min(...Array.from(detrended));
    const maxVal = Math.max(...Array.from(detrended));
    const peakToPeak = maxVal - minVal;
    assert.ok(
      peakToPeak > 1.5 && peakToPeak < 2.5,
      "Sine wave (amplitude 1) should be preserved"
    );
  });

  test("should handle multi-stage processing: detrend then filter", () => {
    // Realistic scenario: detrend → highpass → analyze
    const signal = new Float32Array(50);
    for (let i = 0; i < signal.length; i++) {
      signal[i] = 0.02 * i + Math.sin(i * 0.5) + Math.random() * 0.1;
    }

    // Step 1: Detrend
    const detrended = DspUtils.detrend(signal, { type: "linear" });

    // Step 2: Verify detrending worked
    const mean =
      detrended.reduce((sum, val) => sum + val, 0) / detrended.length;
    assert.ok(
      Math.abs(mean) < 0.01,
      "Detrending should center signal before filtering"
    );

    // Step 3: Further processing would follow (bandpass, feature extraction, etc.)
    // For now, just verify the detrended signal is ready for filtering
    assert.strictEqual(
      detrended.length,
      signal.length,
      "Signal length preserved"
    );
  });
});

describe("Detrend - comparison between methods", () => {
  test("linear should outperform constant for trending data", () => {
    // Strong linear trend
    const signal = new Float32Array(20);
    for (let i = 0; i < signal.length; i++) {
      signal[i] = 0.5 * i + Math.random() * 0.1; // Strong trend + small noise
    }

    const detrendedLinear = DspUtils.detrend(signal, { type: "linear" });
    const detrendedConstant = DspUtils.detrend(signal, { type: "constant" });

    // Linear detrending should produce smaller variance
    const varianceLinear =
      detrendedLinear.reduce((sum, val) => sum + val ** 2, 0) /
      detrendedLinear.length;
    const varianceConstant =
      detrendedConstant.reduce((sum, val) => sum + val ** 2, 0) /
      detrendedConstant.length;

    assert.ok(
      varianceLinear < varianceConstant,
      "Linear detrending should remove trend better than constant"
    );
  });

  test("constant and linear should be similar for trendless data", () => {
    // No trend, just oscillation around mean
    const signal = new Float32Array(20);
    for (let i = 0; i < signal.length; i++) {
      signal[i] = 5.0 + Math.sin(i * 0.5) * 0.5; // Centered at 5.0
    }

    const detrendedLinear = DspUtils.detrend(signal, { type: "linear" });
    const detrendedConstant = DspUtils.detrend(signal, { type: "constant" });

    // Results should be very similar
    const maxDiff = Math.max(
      ...detrendedLinear.map((val, i) => Math.abs(val - detrendedConstant[i]))
    );

    assert.ok(
      maxDiff < 0.1,
      "Linear and constant should give similar results for trendless data"
    );
  });
});
