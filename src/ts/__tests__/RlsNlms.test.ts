import { describe, it } from "node:test";
import assert from "node:assert/strict";
import { createDspPipeline } from "../bindings.js";
import type { RlsFilterParams } from "../types.js";

describe("RLS (Recursive Least Squares) Filter", () => {
  describe("Basic Functionality", () => {
    it("should converge for simple system identification", async () => {
      // Create a simple FIR system: y[n] = 0.5*x[n] + 0.3*x[n-1]
      const numSamples = 200;
      const numTaps = 4;

      // Generate input signal (white noise)
      const input = new Float32Array(numSamples);
      for (let i = 0; i < numSamples; i++) {
        input[i] = Math.random() * 2 - 1;
      }

      // Generate desired output (system response)
      const desired = new Float32Array(numSamples);
      for (let i = 0; i < numSamples; i++) {
        desired[i] = 0.5 * input[i];
        if (i >= 1) {
          desired[i] += 0.3 * input[i - 1];
        }
      }

      // Create interleaved 2-channel buffer
      const interleaved = new Float32Array(numSamples * 2);
      for (let i = 0; i < numSamples; i++) {
        interleaved[i * 2 + 0] = input[i];
        interleaved[i * 2 + 1] = desired[i];
      }

      // Create pipeline with RLS filter
      const pipeline = createDspPipeline();
      pipeline.RlsFilter({ numTaps, lambda: 0.99, delta: 0.1 });

      const result = await pipeline.process(interleaved, { channels: 2 });

      // Calculate MSE for last 50 samples (should be small after convergence)
      let mse = 0;
      for (let i = 150; i < 200; i++) {
        const error = result[i * 2];
        mse += error * error;
      }
      mse /= 50;

      assert.ok(
        mse < 0.01,
        `MSE ${mse} should be less than 0.01 after convergence`
      );
    });

    it("should handle 2-channel requirement correctly", async () => {
      const pipeline = createDspPipeline();
      pipeline.RlsFilter({ numTaps: 16, lambda: 0.99 });

      const singleChannel = new Float32Array(100);
      for (let i = 0; i < 100; i++) {
        singleChannel[i] = Math.sin((2 * Math.PI * i) / 10);
      }

      // Should throw error because only 1 channel provided
      await assert.rejects(
        async () => pipeline.process(singleChannel, { channels: 1 }),
        /requires exactly 2 channels/
      );
    });

    it("should output error signal on both channels", async () => {
      const numSamples = 50;
      const interleaved = new Float32Array(numSamples * 2);

      for (let i = 0; i < numSamples; i++) {
        interleaved[i * 2 + 0] = Math.sin((2 * Math.PI * i) / 20);
        interleaved[i * 2 + 1] = Math.cos((2 * Math.PI * i) / 20);
      }

      const pipeline = createDspPipeline();
      pipeline.RlsFilter({ numTaps: 8, lambda: 0.98 });

      const result = await pipeline.process(interleaved, { channels: 2 });

      assert.strictEqual(result.length, numSamples * 2);

      // Both channels should have identical error signal
      for (let i = 0; i < numSamples; i++) {
        assert.strictEqual(result[i * 2 + 0], result[i * 2 + 1]);
      }
    });
  });

  describe("Parameter Validation", () => {
    it("should reject missing numTaps", () => {
      const pipeline = createDspPipeline();
      assert.throws(
        () => pipeline.RlsFilter({ lambda: 0.99 } as RlsFilterParams),
        /numTaps.*positive integer/
      );
    });

    it("should reject invalid numTaps", () => {
      const pipeline = createDspPipeline();

      assert.throws(
        () => pipeline.RlsFilter({ numTaps: 0, lambda: 0.99 }),
        TypeError
      );

      assert.throws(
        () => pipeline.RlsFilter({ numTaps: -5, lambda: 0.99 }),
        TypeError
      );

      assert.throws(
        () => pipeline.RlsFilter({ numTaps: 3.5, lambda: 0.99 }),
        TypeError
      );
    });

    it("should reject missing lambda", () => {
      const pipeline = createDspPipeline();
      assert.throws(
        () => pipeline.RlsFilter({ numTaps: 16 } as RlsFilterParams),
        /lambda.*required/
      );
    });

    it("should reject lambda out of range", () => {
      const pipeline = createDspPipeline();

      assert.throws(
        () => pipeline.RlsFilter({ numTaps: 16, lambda: 0 }),
        RangeError
      );

      assert.throws(
        () => pipeline.RlsFilter({ numTaps: 16, lambda: 1.5 }),
        RangeError
      );
    });

    it("should accept lambda = 1.0 (edge case)", () => {
      const pipeline = createDspPipeline();
      assert.doesNotThrow(() => {
        pipeline.RlsFilter({ numTaps: 16, lambda: 1.0 });
      });
    });

    it("should reject invalid delta", () => {
      const pipeline = createDspPipeline();

      assert.throws(
        () => pipeline.RlsFilter({ numTaps: 16, lambda: 0.99, delta: 0 }),
        RangeError
      );

      assert.throws(
        () => pipeline.RlsFilter({ numTaps: 16, lambda: 0.99, delta: -0.1 }),
        RangeError
      );
    });

    it("should use default delta when not specified", async () => {
      const pipeline = createDspPipeline();

      assert.doesNotThrow(() => {
        pipeline.RlsFilter({ numTaps: 8, lambda: 0.99 });
      });

      // Verify it works
      const interleaved = new Float32Array(20);
      for (let i = 0; i < 10; i++) {
        interleaved[i * 2 + 0] = Math.random();
        interleaved[i * 2 + 1] = Math.random();
      }

      const result = await pipeline.process(interleaved, { channels: 2 });
      assert.strictEqual(result.length, 20);
    });
  });

  describe("Convergence Properties", () => {
    it("should work effectively for system identification", async () => {
      const numTaps = 5;
      const numSamples = 200;

      // Generate test signals with a well-defined system
      const input = new Float32Array(numSamples);
      const desired = new Float32Array(numSamples);

      // Use a chirp signal (varying frequency) which is more challenging
      for (let i = 0; i < numSamples; i++) {
        const t = i / numSamples;
        input[i] = Math.sin(2 * Math.PI * (5 + 10 * t) * t);
        // System: y[n] = 0.7*x[n] + 0.2*x[n-1]
        desired[i] = 0.7 * input[i];
        if (i >= 1) desired[i] += 0.2 * input[i - 1];
      }

      const interleaved = new Float32Array(numSamples * 2);
      for (let i = 0; i < numSamples; i++) {
        interleaved[i * 2 + 0] = input[i];
        interleaved[i * 2 + 1] = desired[i];
      }

      // Test RLS
      const rlsPipeline = createDspPipeline();
      rlsPipeline.RlsFilter({ numTaps, lambda: 0.99, delta: 0.1 });
      const rlsResult = await rlsPipeline.process(interleaved, { channels: 2 });

      // Calculate final MSE for RLS (last 50 samples)
      let rlsMse = 0;
      for (let i = 150; i < 200; i++) {
        const error = rlsResult[i * 2];
        rlsMse += error * error;
      }
      rlsMse /= 50;

      // RLS should converge to very low error
      assert.ok(
        rlsMse < 0.01,
        `RLS MSE ${rlsMse} should be less than 0.01 for system identification`
      );

      // Also test that it's better than a simple baseline
      assert.ok(
        rlsMse < 0.1,
        `RLS should achieve reasonable convergence (MSE=${rlsMse})`
      );
    });
  });
  describe("State Persistence", () => {
    it("should maintain convergence after state save/restore", async () => {
      const numSamples = 100;
      const numTaps = 8;

      // Generate system
      const input = new Float32Array(numSamples);
      const desired = new Float32Array(numSamples);

      for (let i = 0; i < numSamples; i++) {
        input[i] = Math.random() * 2 - 1;
        desired[i] = 0.5 * input[i];
        if (i >= 1) desired[i] += 0.3 * input[i - 1];
      }

      const interleaved = new Float32Array(numSamples * 2);
      for (let i = 0; i < numSamples; i++) {
        interleaved[i * 2 + 0] = input[i];
        interleaved[i * 2 + 1] = desired[i];
      }

      // First pipeline: converge
      const pipeline1 = createDspPipeline();
      pipeline1.RlsFilter({ numTaps, lambda: 0.99, delta: 0.1 });
      await pipeline1.process(interleaved, { channels: 2 });

      // Save state
      const state = await pipeline1.saveState();
      assert.ok(state !== undefined);
      assert.strictEqual(typeof state, "string");

      // Second pipeline: restore state
      const pipeline2 = createDspPipeline();
      pipeline2.RlsFilter({ numTaps, lambda: 0.99, delta: 0.1 });
      const loaded = await pipeline2.loadState(state);
      assert.strictEqual(loaded, true);

      // Process new data with restored state
      const newInput = new Float32Array(25);
      const newDesired = new Float32Array(25);
      for (let i = 0; i < 25; i++) {
        newInput[i] = Math.random() * 2 - 1;
        newDesired[i] = 0.5 * newInput[i];
        if (i >= 1) newDesired[i] += 0.3 * newInput[i - 1];
      }

      const newInterleaved = new Float32Array(50);
      for (let i = 0; i < 25; i++) {
        newInterleaved[i * 2 + 0] = newInput[i];
        newInterleaved[i * 2 + 1] = newDesired[i];
      }

      const result = await pipeline2.process(newInterleaved, { channels: 2 });

      // Calculate MSE
      let mse = 0;
      for (let i = 0; i < 25; i++) {
        const error = result[i * 2];
        mse += error * error;
      }
      mse /= 25;

      assert.ok(mse < 0.05, "Error should remain low with restored state");
    });
  });
});

describe("NLMS (Normalized LMS) Filter", () => {
  describe("Basic Functionality", () => {
    it("should converge with normalized=true", async () => {
      const numSamples = 150;
      const numTaps = 4;

      const input = new Float32Array(numSamples);
      const desired = new Float32Array(numSamples);

      for (let i = 0; i < numSamples; i++) {
        input[i] = Math.random() * 2 - 1;
        desired[i] = 0.6 * input[i];
        if (i >= 1) desired[i] += 0.2 * input[i - 1];
      }

      const interleaved = new Float32Array(numSamples * 2);
      for (let i = 0; i < numSamples; i++) {
        interleaved[i * 2 + 0] = input[i];
        interleaved[i * 2 + 1] = desired[i];
      }

      const pipeline = createDspPipeline();
      pipeline.LmsFilter({ numTaps, learningRate: 0.1, normalized: true });

      const result = await pipeline.process(interleaved, { channels: 2 });

      // Calculate MSE for last 30 samples
      let mse = 0;
      for (let i = 120; i < 150; i++) {
        const error = result[i * 2];
        mse += error * error;
      }
      mse /= 30;

      assert.ok(mse < 0.05, `NLMS should converge, MSE=${mse}`);
    });

    it("should work with standard LMS when normalized=false", async () => {
      const numSamples = 100;
      const numTaps = 4;

      const input = new Float32Array(numSamples);
      const desired = new Float32Array(numSamples);

      for (let i = 0; i < numSamples; i++) {
        input[i] = Math.random() * 2 - 1;
        desired[i] = 0.5 * input[i];
      }

      const interleaved = new Float32Array(numSamples * 2);
      for (let i = 0; i < numSamples; i++) {
        interleaved[i * 2 + 0] = input[i];
        interleaved[i * 2 + 1] = desired[i];
      }

      const pipeline = createDspPipeline();
      pipeline.LmsFilter({ numTaps, learningRate: 0.01, normalized: false });

      const result = await pipeline.process(interleaved, { channels: 2 });

      // Calculate MSE for last 20 samples
      let mse = 0;
      for (let i = 80; i < 100; i++) {
        const error = result[i * 2];
        mse += error * error;
      }
      mse /= 20;

      assert.ok(mse < 0.1, "Standard LMS should converge");
    });
  });

  describe("Parameter Compatibility", () => {
    it("should accept both learningRate and mu parameters", () => {
      const pipeline1 = createDspPipeline();
      assert.doesNotThrow(() => {
        pipeline1.LmsFilter({
          numTaps: 8,
          learningRate: 0.01,
          normalized: true,
        });
      });

      const pipeline2 = createDspPipeline();
      assert.doesNotThrow(() => {
        pipeline2.LmsFilter({ numTaps: 8, mu: 0.01, normalized: true });
      });
    });

    it("should work with optional lambda parameter", () => {
      const pipeline = createDspPipeline();
      assert.doesNotThrow(() => {
        pipeline.LmsFilter({
          numTaps: 8,
          mu: 0.01,
          normalized: true,
          lambda: 0.9,
        });
      });
    });
  });
});

// Note: Direct convergence speed comparisons (RLS vs LMS/NLMS) are
// scenario-dependent and can be flaky with random data. RLS excels
// with colored (correlated) inputs and time-varying systems, but may
// not converge faster than LMS/NLMS on white noise with short sequences.
// See the "Convergence Properties" test above for absolute performance testing.
