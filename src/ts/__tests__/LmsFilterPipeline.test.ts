import { describe, it } from "node:test";
import assert from "node:assert/strict";
import { createDspPipeline } from "../bindings.js";

describe("LmsFilter Pipeline Stage", () => {
  describe("Constructor and Validation", () => {
    it("should create LMS filter with valid parameters", () => {
      const pipeline = createDspPipeline();
      assert.doesNotThrow(() => {
        pipeline.LmsFilter({ numTaps: 32, learningRate: 0.01 });
      });
    });

    it("should reject invalid numTaps", () => {
      const pipeline = createDspPipeline();
      assert.throws(
        () => pipeline.LmsFilter({ numTaps: 0, learningRate: 0.01 }),
        /numTaps must be a positive integer/
      );
    });

    it("should reject negative learningRate", () => {
      const pipeline = createDspPipeline();
      assert.throws(
        () => pipeline.LmsFilter({ numTaps: 32, learningRate: -0.01 }),
        /learningRate.*must be in \(0, 1\]/
      );
    });

    it("should reject invalid lambda", () => {
      const pipeline = createDspPipeline();
      assert.throws(
        () =>
          pipeline.LmsFilter({ numTaps: 32, learningRate: 0.01, lambda: 1.5 }),
        /lambda must be in \[0, 1\)/
      );
    });

    it("should accept 'mu' as alias for learningRate", () => {
      const pipeline = createDspPipeline();
      assert.doesNotThrow(() => {
        pipeline.LmsFilter({ numTaps: 32, mu: 0.01 });
      });
    });
  });

  describe("2-Channel Requirement", () => {
    it("should require exactly 2 channels", async () => {
      const pipeline = createDspPipeline();
      pipeline.LmsFilter({ numTaps: 8, learningRate: 0.1 });

      // Single channel should fail
      const singleChannel = new Float32Array(100);
      await assert.rejects(
        async () =>
          pipeline.process(singleChannel, { channels: 1, sampleRate: 1000 }),
        /requires exactly 2 channels/
      );

      // 3 channels should also fail
      const threeChannel = new Float32Array(300);
      await assert.rejects(
        async () =>
          pipeline.process(threeChannel, { channels: 3, sampleRate: 1000 }),
        /requires exactly 2 channels/
      );
    });

    it("should accept 2-channel interleaved input", async () => {
      const pipeline = createDspPipeline();
      pipeline.LmsFilter({ numTaps: 8, learningRate: 0.1 });

      const samples = 100;
      const interleaved = new Float32Array(samples * 2); // 2 channels
      for (let i = 0; i < samples; i++) {
        interleaved[i * 2 + 0] = Math.sin((2 * Math.PI * i) / 10); // Channel 0
        interleaved[i * 2 + 1] = Math.cos((2 * Math.PI * i) / 10); // Channel 1
      }

      const result = await pipeline.process(interleaved, {
        channels: 2,
        sampleRate: 1000,
      });

      assert.ok(result instanceof Float32Array);
      assert.strictEqual(result.length, samples * 2);
    });
  });

  describe("Noise Cancellation", () => {
    it("should cancel correlated noise", async () => {
      const pipeline = createDspPipeline();
      pipeline.LmsFilter({ numTaps: 32, learningRate: 0.01 });

      const samples = 1000;
      const interleaved = new Float32Array(samples * 2);

      // Generate test signal
      const cleanSignal = new Float32Array(samples);
      const noise = new Float32Array(samples);
      for (let i = 0; i < samples; i++) {
        cleanSignal[i] = Math.sin((2 * Math.PI * 3 * i) / samples); // 3Hz clean signal
        noise[i] = 0.8 * Math.sin((2 * Math.PI * 15 * i) / samples); // 15Hz noise
      }

      // Create interleaved buffer
      // For noise cancellation:
      // Channel 0 (primary x[n]): noise reference (correlated with interference)
      // Channel 1 (desired d[n]): noisy signal (clean + noise)
      // Output e[n] = d[n] - y[n]: cleaned signal (noise removed)
      for (let i = 0; i < samples; i++) {
        interleaved[i * 2 + 0] = noise[i]; // Primary: pure noise reference
        interleaved[i * 2 + 1] = cleanSignal[i] + noise[i]; // Desired: signal corrupted by noise
      }

      const result = await pipeline.process(interleaved, {
        channels: 2,
        sampleRate: 1000,
      });

      // Extract the error signal (cleaned output)
      const cleaned = new Float32Array(samples);
      for (let i = 0; i < samples; i++) {
        cleaned[i] = result[i * 2 + 0];
      }

      // After adaptation, compute RMS of error in final portion (after convergence)
      // Compare cleaned signal to original clean signal
      const convergenceStart = samples - 200; // Last 200 samples
      let errorSum = 0;
      let cleanSignalPower = 0;

      for (let i = convergenceStart; i < samples; i++) {
        const error = cleaned[i] - cleanSignal[i];
        errorSum += error * error;
        cleanSignalPower += cleanSignal[i] * cleanSignal[i];
      }

      const rmsError = Math.sqrt(errorSum / 200);
      const signalRms = Math.sqrt(cleanSignalPower / 200);

      // Relative error should be small (good noise cancellation)
      const relativeError = rmsError / signalRms;
      assert.ok(
        relativeError < 0.5,
        `Expected relative error < 0.5, got ${relativeError}`
      );
    });
  });

  describe("System Identification", () => {
    it("should learn a simple delay", async () => {
      const pipeline = createDspPipeline();
      pipeline.LmsFilter({ numTaps: 8, learningRate: 0.1 });

      const samples = 200;
      const delay = 2; // 2-sample delay
      const interleaved = new Float32Array(samples * 2);

      // Generate random input
      const input = new Float32Array(samples);
      for (let i = 0; i < samples; i++) {
        input[i] = Math.random() * 2 - 1;
      }

      // Create delayed version as "desired" signal
      for (let i = 0; i < samples; i++) {
        interleaved[i * 2 + 0] = input[i]; // Channel 0: input x[n]
        interleaved[i * 2 + 1] = i >= delay ? input[i - delay] : 0; // Channel 1: x[n-delay]
      }

      const result = await pipeline.process(interleaved, {
        channels: 2,
        sampleRate: 1000,
      });

      // The error should be small after convergence (filter learned the delay)
      const errorSamples = new Float32Array(samples);
      for (let i = 0; i < samples; i++) {
        errorSamples[i] = result[i * 2 + 0];
      }

      // Measure RMS error in the last 50 samples (after convergence)
      let rmsError = 0;
      const convergenceStart = samples - 50;
      for (let i = convergenceStart; i < samples; i++) {
        rmsError += errorSamples[i] * errorSamples[i];
      }
      rmsError = Math.sqrt(rmsError / 50);

      // Error should be very small after learning
      assert.ok(rmsError < 0.1, `Expected low RMS error, got ${rmsError}`);
    });
  });

  describe("NLMS Algorithm", () => {
    it("should use normalized LMS when enabled", async () => {
      const pipelineStandard = createDspPipeline();
      pipelineStandard.LmsFilter({
        numTaps: 16,
        learningRate: 0.01,
        normalized: false,
      });

      const pipelineNLMS = createDspPipeline();
      pipelineNLMS.LmsFilter({
        numTaps: 16,
        learningRate: 0.5,
        normalized: true,
      });

      const samples = 300;
      const interleaved = new Float32Array(samples * 2);

      // Create test signal with varying amplitude
      for (let i = 0; i < samples; i++) {
        const amplitude = 1.0 + 0.5 * Math.sin((2 * Math.PI * i) / 100); // Varying amplitude
        const signal = amplitude * Math.sin((2 * Math.PI * 5 * i) / samples);
        const noise = 0.3 * Math.cos((2 * Math.PI * 20 * i) / samples);

        interleaved[i * 2 + 0] = noise;
        interleaved[i * 2 + 1] = signal + noise;
      }

      // Both should work, but NLMS should converge faster with varying amplitudes
      const resultStandard = await pipelineStandard.process(
        interleaved.slice(),
        { channels: 2, sampleRate: 1000 }
      );
      const resultNLMS = await pipelineNLMS.process(interleaved.slice(), {
        channels: 2,
        sampleRate: 1000,
      });

      assert.ok(resultStandard instanceof Float32Array);
      assert.ok(resultNLMS instanceof Float32Array);
      assert.strictEqual(resultStandard.length, samples * 2);
      assert.strictEqual(resultNLMS.length, samples * 2);
    });
  });

  describe("State Persistence", () => {
    it("should save and restore filter state", async () => {
      const pipeline = createDspPipeline();
      pipeline.LmsFilter({ numTaps: 8, learningRate: 0.1 });

      const samples = 100;
      const interleaved = new Float32Array(samples * 2);
      for (let i = 0; i < samples; i++) {
        interleaved[i * 2 + 0] = Math.sin((2 * Math.PI * i) / 20);
        interleaved[i * 2 + 1] = Math.cos((2 * Math.PI * i) / 20);
      }

      // Process first batch
      const result1 = await pipeline.process(interleaved, {
        channels: 2,
        sampleRate: 1000,
      });

      // Save state
      const stateJson = await pipeline.saveState();
      const state = JSON.parse(stateJson);
      assert.ok(state.stages.length === 1);
      assert.strictEqual(state.stages[0].type, "lmsFilter");

      // Create new pipeline and restore state
      const pipeline2 = createDspPipeline();
      pipeline2.LmsFilter({ numTaps: 8, learningRate: 0.1 });
      await pipeline2.loadState(stateJson);

      // Process same batch again
      const result2 = await pipeline2.process(interleaved, {
        channels: 2,
        sampleRate: 1000,
      });

      // Results should be similar (filter adapted from saved state)
      assert.strictEqual(result2.length, result1.length);
    });
  });

  describe("Pipeline Chaining", () => {
    it("should chain LMS filter with other stages", async () => {
      const pipeline = createDspPipeline();
      pipeline.LmsFilter({ numTaps: 16, learningRate: 0.05 });
      pipeline.MovingAverage({ mode: "moving", windowSize: 10 }); // Smooth the output

      const samples = 200;
      const interleaved = new Float32Array(samples * 2);
      for (let i = 0; i < samples; i++) {
        const signal = Math.sin((2 * Math.PI * 5 * i) / samples);
        const noise = 0.3 * Math.sin((2 * Math.PI * 20 * i) / samples);
        interleaved[i * 2 + 0] = noise;
        interleaved[i * 2 + 1] = signal + noise;
      }

      const result = await pipeline.process(interleaved, {
        channels: 2,
        sampleRate: 1000,
      });

      assert.ok(result instanceof Float32Array);
      assert.strictEqual(result.length, samples * 2);
    });
  });

  describe("Edge Cases", () => {
    it("should handle constant signals", async () => {
      const pipeline = createDspPipeline();
      pipeline.LmsFilter({ numTaps: 8, learningRate: 0.1 });

      const samples = 100;
      const interleaved = new Float32Array(samples * 2);
      interleaved.fill(0.5); // All constant

      const result = await pipeline.process(interleaved, {
        channels: 2,
        sampleRate: 1000,
      });

      assert.ok(result instanceof Float32Array);
      assert.strictEqual(result.length, samples * 2);
    });

    it("should handle zero input", async () => {
      const pipeline = createDspPipeline();
      pipeline.LmsFilter({ numTaps: 8, learningRate: 0.1 });

      const samples = 100;
      const interleaved = new Float32Array(samples * 2); // All zeros

      const result = await pipeline.process(interleaved, {
        channels: 2,
        sampleRate: 1000,
      });

      assert.ok(result instanceof Float32Array);
      assert.strictEqual(result.length, samples * 2);

      // Output should also be near zero
      for (let i = 0; i < result.length; i++) {
        assert.ok(Math.abs(result[i]) < 1e-6);
      }
    });
  });
});
