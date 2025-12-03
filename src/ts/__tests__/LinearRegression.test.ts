import { describe, it, beforeEach, afterEach } from "node:test";
import assert from "node:assert/strict";
import { createDspPipeline, DspProcessor } from "../bindings.js";

const DEFAULT_OPTIONS = { channels: 1, sampleRate: 8000 };

function assertCloseTo(actual: number, expected: number, precision = 1) {
  const tolerance = Math.pow(10, -precision);
  assert.ok(
    Math.abs(actual - expected) < tolerance,
    `Expected ${actual} to be close to ${expected} (tolerance: ${tolerance})`
  );
}

describe("Linear Regression Stage", () => {
  let pipeline: DspProcessor;

  beforeEach(() => {
    pipeline = createDspPipeline();
  });

  afterEach(() => {
    pipeline.dispose();
  });

  describe("Slope Output", () => {
    it("should extract positive trend (slope) from linearly increasing signal", async () => {
      pipeline.LinearRegression({ windowSize: 5, output: "slope" });

      // Linear signal: y = 2x (slope = 2)
      const input = new Float32Array([0, 2, 4, 6, 8, 10, 12, 14, 16, 18]);
      const result = await pipeline.process(input, DEFAULT_OPTIONS);

      // Once window fills, slope should be ~2
      // Early samples may differ as window fills
      for (let i = 5; i < result.length; i++) {
        assert.ok(
          Math.abs(result[i] - 2.0) < 0.1,
          `Slope at index ${i} should be ~2, got ${result[i]}`
        );
      }
    });

    it("should extract negative trend (slope) from linearly decreasing signal", async () => {
      pipeline.LinearRegression({ windowSize: 5, output: "slope" });

      // Linear signal: y = -1.5x (slope = -1.5)
      const input = new Float32Array([
        0, -1.5, -3, -4.5, -6, -7.5, -9, -10.5, -12,
      ]);
      const result = await pipeline.process(input, DEFAULT_OPTIONS);

      // Once window fills, slope should be ~-1.5
      for (let i = 5; i < result.length; i++) {
        assert.ok(
          Math.abs(result[i] + 1.5) < 0.1,
          `Slope at index ${i} should be ~-1.5, got ${result[i]}`
        );
      }
    });

    it("should detect zero slope for constant signal", async () => {
      pipeline.LinearRegression({ windowSize: 5, output: "slope" });

      const input = new Float32Array([5, 5, 5, 5, 5, 5, 5, 5]);
      const result = await pipeline.process(input, DEFAULT_OPTIONS);

      // Slope should be ~0 for constant signal
      for (let i = 4; i < result.length; i++) {
        assert.ok(
          Math.abs(result[i]) < 1e-3,
          `Slope should be ~0 for constant signal, got ${result[i]}`
        );
      }
    });

    it("should track changing slope over time", async () => {
      pipeline.LinearRegression({ windowSize: 10, output: "slope" });

      // First increasing, then flat, then decreasing
      const input = new Float32Array([
        0,
        1,
        2,
        3,
        4,
        5,
        6,
        7,
        8,
        9, // Slope ~1
        10,
        10,
        10,
        10,
        10,
        10,
        10,
        10, // Slope ~0
        10,
        9,
        8,
        7,
        6,
        5,
        4,
        3, // Slope ~-1
      ]);
      const result = await pipeline.process(input, DEFAULT_OPTIONS);

      // Check initial increasing region (after window fills)
      assert.ok(
        result[9] > 0.5,
        "Should detect positive slope in increasing region"
      );

      // Check flat region
      const flatIdx = 17; // In the flat region
      assert.ok(
        Math.abs(result[flatIdx]) < 0.5,
        "Should detect near-zero slope in flat region"
      );

      // Check decreasing region
      const decIdx = result.length - 1;
      assert.ok(
        result[decIdx] < -0.5,
        "Should detect negative slope in decreasing region"
      );
    });
  });

  describe("Intercept Output", () => {
    it("should extract intercept (baseline) from signal", async () => {
      pipeline.LinearRegression({ windowSize: 5, output: "intercept" });

      // y = 2x + 10 (intercept = 10)
      const input = new Float32Array([10, 12, 14, 16, 18, 20, 22, 24]);
      const result = await pipeline.process(input, DEFAULT_OPTIONS);

      // Once window fills, intercept should be ~10
      // (adjusted for window position)
      for (let i = 5; i < result.length; i++) {
        assert.ok(
          result[i] > 8 && result[i] < 22,
          `Intercept should be reasonable, got ${result[i]}`
        );
      }
    });

    it("should track baseline shift", async () => {
      pipeline.LinearRegression({ windowSize: 8, output: "intercept" });

      // Signal with baseline shift: first around 0, then around 100
      const input = new Float32Array([
        0,
        1,
        2,
        3,
        4,
        5,
        6,
        7,
        8,
        9, // Baseline ~0
        100,
        101,
        102,
        103,
        104,
        105,
        106,
        107,
        108,
        109, // Baseline ~100
      ]);
      const result = await pipeline.process(input, DEFAULT_OPTIONS);

      // Early samples should have low intercept
      assert.ok(result[7] < 50, "Initial intercept should be low");

      // Later samples should have high intercept
      assert.ok(
        result[result.length - 1] > 50,
        "Later intercept should be high"
      );
    });
  });

  describe("Residuals Output", () => {
    it("should detrend signal (remove linear trend)", async () => {
      pipeline.LinearRegression({ windowSize: 6, output: "residuals" });

      // Signal = trend + noise: y = 2x + sin(x)
      const input = new Float32Array(20);
      for (let i = 0; i < input.length; i++) {
        input[i] = 2 * i + Math.sin(i);
      }

      const result = await pipeline.process(input, DEFAULT_OPTIONS);

      // Residuals should have much smaller mean than original (trend removed)
      let sum = 0;
      for (let i = 6; i < result.length; i++) {
        sum += Math.abs(result[i]);
      }
      const meanResidual = sum / (result.length - 6);

      // Mean of absolute residuals should be small (< 2)
      assert.ok(
        meanResidual < 2,
        `Detrended signal should have small residuals, got mean ${meanResidual}`
      );
    });

    it("should output near-zero residuals for perfect linear signal", async () => {
      pipeline.LinearRegression({ windowSize: 5, output: "residuals" });

      // Perfect line: y = 3x + 5
      const input = new Float32Array([5, 8, 11, 14, 17, 20, 23, 26]);
      const result = await pipeline.process(input, DEFAULT_OPTIONS);

      // Residuals should be ~0 for perfect fit
      for (let i = 5; i < result.length; i++) {
        assert.ok(
          Math.abs(result[i]) < 1e-3,
          `Residual should be ~0 for perfect fit, got ${result[i]}`
        );
      }
    });

    it("should preserve noise after removing trend", async () => {
      pipeline.LinearRegression({ windowSize: 10, output: "residuals" });

      // Signal = trend + periodic component
      const input = new Float32Array(30);
      for (let i = 0; i < input.length; i++) {
        input[i] = i * 0.5 + 2 * Math.sin(i * 0.5); // Trend + oscillation
      }

      const result = await pipeline.process(input, DEFAULT_OPTIONS);

      // Residuals should oscillate (preserve the sine wave)
      // Check for sign changes indicating oscillation
      let signChanges = 0;
      for (let i = 11; i < result.length; i++) {
        if (result[i] > 0 !== result[i - 1] > 0) {
          signChanges++;
        }
      }

      // Relaxed threshold: linear regression removes trend, some oscillation should remain
      assert.ok(
        signChanges >= 3,
        `Residuals should oscillate after trend removal (found ${signChanges} sign changes)`
      );
    });
  });

  describe("Predictions Output", () => {
    it("should output fitted values from regression", async () => {
      pipeline.LinearRegression({ windowSize: 5, output: "predictions" });

      // Signal with some noise: y = 2x + noise
      const input = new Float32Array(10);
      for (let i = 0; i < input.length; i++) {
        input[i] = 2 * i + (Math.random() - 0.5) * 0.5;
      }

      const result = await pipeline.process(input, DEFAULT_OPTIONS);

      // Predictions should follow ~2x pattern
      // Check that predictions increase roughly linearly
      const slope = (result[9] - result[5]) / 4;
      assert.ok(
        Math.abs(slope - 2) < 0.5,
        `Predictions should increase with slope ~2, got ${slope}`
      );
    });

    it("should produce smooth predictions for noisy signal", async () => {
      pipeline.LinearRegression({ windowSize: 8, output: "predictions" });

      // Noisy linear signal
      const input = new Float32Array(20);
      for (let i = 0; i < input.length; i++) {
        input[i] = i * 1.5 + (Math.random() - 0.5) * 3; // Significant noise
      }

      const result = await pipeline.process(input, DEFAULT_OPTIONS);

      // Predictions should follow the trend reasonably well
      // Check that predictions are not wildly different from inputs
      let meanDiff = 0;
      for (let i = 8; i < input.length; i++) {
        meanDiff += Math.abs(result[i] - input[i]);
      }
      meanDiff /= input.length - 8;

      // Mean difference should be reasonable (not too large)
      assert.ok(
        meanDiff < 5,
        `Predictions should be reasonably close to input (mean diff: ${meanDiff.toFixed(
          2
        )})`
      );
    });
  });

  describe("Multi-channel Processing", () => {
    it("should process multiple channels independently", async () => {
      pipeline.LinearRegression({ windowSize: 5, output: "slope" });

      // 2 channels, 8 samples per channel, INTERLEAVED
      // Channel 0: [0, 1, 2, 3, 4, 5, 6, 7] (slope = 1)
      // Channel 1: [0, -2, -4, -6, -8, -10, -12, -14] (slope = -2)
      const input = new Float32Array([
        0,
        0, // Sample 0: Ch0=0,  Ch1=0
        1,
        -2, // Sample 1: Ch0=1,  Ch1=-2
        2,
        -4, // Sample 2: Ch0=2,  Ch1=-4
        3,
        -6, // Sample 3: Ch0=3,  Ch1=-6
        4,
        -8, // Sample 4: Ch0=4,  Ch1=-8
        5,
        -10, // Sample 5: Ch0=5,  Ch1=-10
        6,
        -12, // Sample 6: Ch0=6,  Ch1=-12
        7,
        -14, // Sample 7: Ch0=7,  Ch1=-14
      ]);

      const result = await pipeline.process(input, {
        channels: 2,
        sampleRate: 8000,
      });

      // Check channel 0 (positive slope) - indices 10, 12, 14 (samples 5, 6, 7)
      for (let i = 10; i < 16; i += 2) {
        assert.ok(
          Math.abs(result[i] - 1.0) < 0.2,
          `Ch0 slope should be ~1, got ${result[i]} at index ${i}`
        );
      }

      // Check channel 1 (negative slope) - indices 11, 13, 15 (samples 5, 6, 7)
      for (let i = 11; i < 16; i += 2) {
        assert.ok(
          Math.abs(result[i] + 2.0) < 0.2,
          `Ch1 slope should be ~-2, got ${result[i]} at index ${i}`
        );
      }
    });

    it("should detrend multiple channels independently", async () => {
      pipeline.LinearRegression({ windowSize: 6, output: "residuals" });

      // 2 channels, 10 samples per channel, INTERLEAVED
      // Channel 0: [0, 2, 4, 6, 8, 10, 12, 14, 16, 18] (slope = 2)
      // Channel 1: [0, -1, -2, -3, -4, -5, -6, -7, -8, -9] (slope = -1)
      const input = new Float32Array(20); // 10 samples × 2 channels
      for (let i = 0; i < 10; i++) {
        input[i * 2] = i * 2; // Channel 0 at even indices
        input[i * 2 + 1] = i * -1; // Channel 1 at odd indices
      }

      const result = await pipeline.process(input, {
        channels: 2,
        sampleRate: 8000,
      });

      // Both channels should have residuals ~0 (perfect lines)
      // Check after window fills (sample index >= 6)
      for (let i = 12; i < 20; i += 2) {
        assert.ok(
          Math.abs(result[i]) < 0.1,
          `Ch0 residuals should be ~0 at index ${i}, got ${result[i]}`
        );
        assert.ok(
          Math.abs(result[i + 1]) < 0.1,
          `Ch1 residuals should be ~0 at index ${i + 1}, got ${result[i + 1]}`
        );
      }
    });
  });

  describe("State Management", () => {
    it("should maintain state across multiple process calls", async () => {
      pipeline.LinearRegression({ windowSize: 5, output: "slope" });

      // Process in batches
      const batch1 = new Float32Array([0, 1, 2, 3]);
      const batch2 = new Float32Array([4, 5, 6, 7, 8]);

      await pipeline.process(batch1, DEFAULT_OPTIONS);
      const result2 = await pipeline.process(batch2, DEFAULT_OPTIONS);

      // After second batch, window is full and slope should be ~1
      for (let i = 1; i < result2.length; i++) {
        assert.ok(
          Math.abs(result2[i] - 1.0) < 0.2,
          `Slope should be ~1 after state continuity, got ${result2[i]}`
        );
      }
    });

    it("should save and restore state correctly", async () => {
      const pipeline1 = createDspPipeline();
      pipeline1.LinearRegression({ windowSize: 6, output: "slope" });

      // Process some data
      const input1 = new Float32Array([0, 2, 4, 6, 8]);
      await pipeline1.process(input1, DEFAULT_OPTIONS);

      // Save state (await since it's async)
      const state = await pipeline1.saveState();

      // Create new pipeline and restore state
      const pipeline2 = createDspPipeline();
      pipeline2.LinearRegression({ windowSize: 6, output: "slope" });
      await pipeline2.loadState(state);

      // Continue processing
      const input2 = new Float32Array([10, 12, 14, 16]);
      const result = await pipeline2.process(input2, DEFAULT_OPTIONS);

      // Should maintain slope of ~2
      for (let i = 1; i < result.length; i++) {
        assert.ok(
          Math.abs(result[i] - 2.0) < 0.3,
          `Restored state should maintain slope ~2, got ${result[i]}`
        );
      }
    });

    it("should clear state correctly", async () => {
      pipeline.LinearRegression({ windowSize: 5, output: "slope" });

      // Process data
      await pipeline.process(
        new Float32Array([0, 1, 2, 3, 4, 5]),
        DEFAULT_OPTIONS
      );

      // Clear state
      pipeline.clearState();

      // Process new data with different slope
      const input = new Float32Array([0, 3, 6, 9, 12, 15]);
      const result = await pipeline.process(input, DEFAULT_OPTIONS);

      // Should compute slope based only on new data (~3)
      assert.ok(
        Math.abs(result[result.length - 1] - 3.0) < 0.5,
        "After clearState, should compute new slope"
      );
    });
  });

  describe("Validation", () => {
    it("should reject missing windowSize", () => {
      assert.throws(
        () => {
          // @ts-expect-error Testing validation
          pipeline.LinearRegression({ output: "slope" });
        },
        TypeError,
        "Should reject missing windowSize"
      );
    });

    it("should reject invalid windowSize", () => {
      assert.throws(
        () => {
          pipeline.LinearRegression({ windowSize: 0, output: "slope" });
        },
        TypeError,
        "Should reject windowSize = 0"
      );

      assert.throws(
        () => {
          pipeline.LinearRegression({ windowSize: -5, output: "slope" });
        },
        TypeError,
        "Should reject negative windowSize"
      );

      assert.throws(
        () => {
          pipeline.LinearRegression({ windowSize: 3.5, output: "slope" });
        },
        TypeError,
        "Should reject non-integer windowSize"
      );
    });

    it("should reject invalid output mode", () => {
      assert.throws(
        () => {
          // @ts-expect-error Testing validation
          pipeline.LinearRegression({ windowSize: 10, output: "invalid" });
        },
        TypeError,
        "Should reject invalid output mode"
      );
    });

    it("should accept all valid output modes", () => {
      const outputs: Array<
        "slope" | "intercept" | "residuals" | "predictions"
      > = ["slope", "intercept", "residuals", "predictions"];

      for (const output of outputs) {
        const p = createDspPipeline();
        assert.doesNotThrow(() => {
          p.LinearRegression({ windowSize: 10, output });
        }, `Should accept output mode: ${output}`);
      }
    });
  });

  describe("Edge Cases", () => {
    it("should handle window size larger than input", async () => {
      pipeline.LinearRegression({ windowSize: 100, output: "slope" });

      const input = new Float32Array([0, 1, 2, 3, 4]);
      const result = await pipeline.process(input, DEFAULT_OPTIONS);

      // Should process without error, partial window results
      assert.strictEqual(result.length, input.length);
    });

    it("should handle single sample input", async () => {
      pipeline.LinearRegression({ windowSize: 5, output: "slope" });

      const input = new Float32Array([42]);
      const result = await pipeline.process(input, DEFAULT_OPTIONS);

      assert.strictEqual(result.length, 1);
    });

    it("should handle all-zero signal", async () => {
      pipeline.LinearRegression({ windowSize: 5, output: "slope" });

      const input = new Float32Array([0, 0, 0, 0, 0, 0, 0]);
      const result = await pipeline.process(input, DEFAULT_OPTIONS);

      // Slope should be ~0
      for (let i = 4; i < result.length; i++) {
        assert.ok(Math.abs(result[i]) < 1e-6, "Slope of zeros should be 0");
      }
    });

    it("should handle very small window size", async () => {
      pipeline.LinearRegression({ windowSize: 2, output: "slope" });

      const input = new Float32Array([0, 1, 2, 3, 4, 5]);
      const result = await pipeline.process(input, DEFAULT_OPTIONS);

      // Should compute regression with just 2 points
      for (let i = 1; i < result.length; i++) {
        assert.ok(
          Math.abs(result[i] - 1.0) < 0.1,
          "Should compute slope with small window"
        );
      }
    });
  });

  describe("Chaining with Other Stages", () => {
    it("should chain with moving average for smoothed trend", async () => {
      pipeline
        .LinearRegression({ windowSize: 10, output: "slope" })
        .MovingAverage({ mode: "moving", windowSize: 5 });

      // Noisy signal with trend
      const input = new Float32Array(30);
      for (let i = 0; i < input.length; i++) {
        input[i] = i * 0.8 + (Math.random() - 0.5) * 2;
      }

      const result = await pipeline.process(input, DEFAULT_OPTIONS);

      // Should produce smoothed slope values
      assert.strictEqual(result.length, input.length);
    });

    it("should detrend then compute RMS", async () => {
      pipeline
        .LinearRegression({ windowSize: 20, output: "residuals" })
        .Rms({ mode: "batch" });

      // Signal with trend + noise
      const input = new Float32Array(30);
      for (let i = 0; i < input.length; i++) {
        input[i] = i * 2 + (Math.random() - 0.5) * 4;
      }

      const result = await pipeline.process(input, DEFAULT_OPTIONS);

      // RMS of detrended signal should be small
      for (let i = 0; i < result.length; i++) {
        assert.ok(
          result[i] < 10,
          "RMS of detrended signal should be relatively small"
        );
      }
    });
  });
});
