import { describe, test } from "node:test";
import assert from "node:assert";
import { createDspPipeline } from "../bindings.js";

describe("Z-Score Normalize Filter", () => {
  describe("Batch Mode (Stateless)", () => {
    test("should normalize to mean 0 and stddev ~1 for batch mode", async () => {
      const pipeline = createDspPipeline();
      pipeline.ZScoreNormalize({ mode: "batch" });

      // Input: [1, 2, 3, 4, 5] with mean=3, stddev≈1.414
      const input = new Float32Array([1, 2, 3, 4, 5]);
      const output = await pipeline.process(input, {
        sampleRate: 1000,
        channels: 1,
      });

      // Calculate mean (should be ~0)
      const mean = output.reduce((sum, val) => sum + val, 0) / output.length;
      assert.ok(Math.abs(mean) < 0.0001, `Mean should be ~0, got ${mean}`);

      // Calculate variance (should be ~1)
      const variance =
        output.reduce((sum, val) => sum + val * val, 0) / output.length;
      assert.ok(
        Math.abs(variance - 1.0) < 0.01,
        `Variance should be ~1, got ${variance}`
      );

      // Expected normalized values: [-1.414, -0.707, 0, 0.707, 1.414]
      assert.ok(Math.abs(output[0] - -1.414) < 0.01);
      assert.ok(Math.abs(output[2] - 0) < 0.01); // Middle value should be ~0
      assert.ok(Math.abs(output[4] - 1.414) < 0.01);
    });

    test("should handle constant signal (zero stddev) with epsilon", async () => {
      const pipeline = createDspPipeline();
      pipeline.ZScoreNormalize({ mode: "batch", epsilon: 1e-6 });

      const input = new Float32Array([5, 5, 5, 5, 5]);
      const output = await pipeline.process(input, {
        sampleRate: 1000,
        channels: 1,
      });

      // When stddev is 0, all values should be 0 (since they all equal the mean)
      output.forEach((val, i) => {
        assert.ok(
          Math.abs(val) < 0.0001,
          `Expected 0 for constant signal, got ${val} at index ${i}`
        );
      });
    });

    test("should handle negative values correctly", async () => {
      const pipeline = createDspPipeline();
      pipeline.ZScoreNormalize({ mode: "batch" });

      const input = new Float32Array([-2, -1, 0, 1, 2]);
      const output = await pipeline.process(input, {
        sampleRate: 1000,
        channels: 1,
      });

      // Mean should be ~0
      const mean = output.reduce((sum, val) => sum + val, 0) / output.length;
      assert.ok(Math.abs(mean) < 0.0001);

      // Variance should be ~1
      const variance =
        output.reduce((sum, val) => sum + val * val, 0) / output.length;
      assert.ok(Math.abs(variance - 1.0) < 0.01);
    });

    test("should be stateless between calls", async () => {
      const pipeline = createDspPipeline();
      pipeline.ZScoreNormalize({ mode: "batch" });

      const input1 = new Float32Array([1, 2, 3, 4, 5]);
      const output1 = await pipeline.process(input1, {
        sampleRate: 1000,
        channels: 1,
      });

      const input2 = new Float32Array([1, 2, 3, 4, 5]);
      const output2 = await pipeline.process(input2, {
        sampleRate: 1000,
        channels: 1,
      });

      // Both outputs should be identical (stateless)
      for (let i = 0; i < output1.length; i++) {
        assert.strictEqual(output1[i], output2[i]);
      }
    });

    test("should handle multi-channel data independently", async () => {
      const pipeline = createDspPipeline();
      pipeline.ZScoreNormalize({ mode: "batch" });

      // 2 channels, 5 samples per channel
      // Channel 0: [1, 3, 5, 7, 9] (interleaved at indices 0,2,4,6,8)
      // Channel 1: [10, 20, 30, 40, 50] (interleaved at indices 1,3,5,7,9)
      const input = new Float32Array([1, 10, 3, 20, 5, 30, 7, 40, 9, 50]);
      const output = await pipeline.process(input, {
        sampleRate: 1000,
        channels: 2,
      });

      // Extract channels
      const ch0 = [];
      const ch1 = [];
      for (let i = 0; i < output.length; i++) {
        if (i % 2 === 0) ch0.push(output[i]);
        else ch1.push(output[i]);
      }

      // Each channel should have mean ~0 and variance ~1
      const mean0 = ch0.reduce((s, v) => s + v, 0) / ch0.length;
      const mean1 = ch1.reduce((s, v) => s + v, 0) / ch1.length;

      assert.ok(
        Math.abs(mean0) < 0.0001,
        `Ch0 mean should be ~0, got ${mean0}`
      );
      assert.ok(
        Math.abs(mean1) < 0.0001,
        `Ch1 mean should be ~0, got ${mean1}`
      );
    });
  });

  describe("Moving Mode (Stateful)", () => {
    test("should compute moving Z-Score with window size 3", async () => {
      const pipeline = createDspPipeline();
      pipeline.ZScoreNormalize({ mode: "moving", windowSize: 3 });

      const input = new Float32Array([1, 2, 3, 4, 5]);
      const output = await pipeline.process(input, {
        sampleRate: 1000,
        channels: 1,
      });

      // Window [1]: mean=1, stddev=0 → z-score=0
      assert.ok(Math.abs(output[0] - 0) < 0.01);

      // Window [1,2]: mean=1.5, stddev=0.5 → z-score for 2 = (2-1.5)/0.5 = 1.0
      assert.ok(Math.abs(output[1] - 1.0) < 0.01);

      // Window [1,2,3]: mean=2, stddev≈0.816 → z-score for 3 = (3-2)/0.816 ≈ 1.225
      assert.ok(Math.abs(output[2] - 1.225) < 0.05);

      // Window [2,3,4]: mean=3, stddev≈0.816 → z-score for 4 = (4-3)/0.816 ≈ 1.225
      assert.ok(Math.abs(output[3] - 1.225) < 0.05);

      // Window [3,4,5]: mean=4, stddev≈0.816 → z-score for 5 = (5-4)/0.816 ≈ 1.225
      assert.ok(Math.abs(output[4] - 1.225) < 0.05);
    });

    test("should maintain state across multiple process calls", async () => {
      const pipeline = createDspPipeline();
      pipeline.ZScoreNormalize({ mode: "moving", windowSize: 3 });

      // First batch: [1, 2, 3]
      const input1 = new Float32Array([1, 2, 3]);
      const output1 = await pipeline.process(input1, {
        sampleRate: 1000,
        channels: 1,
      });

      // After first batch, buffer contains [1, 2, 3]
      // Second batch: [4, 5]
      const input2 = new Float32Array([4, 5]);
      const output2 = await pipeline.process(input2, {
        sampleRate: 1000,
        channels: 1,
      });

      // For value 4: window is [2, 3, 4], mean=3, stddev≈0.816
      // Z-score for 4 = (4-3)/0.816 ≈ 1.225
      assert.ok(Math.abs(output2[0] - 1.225) < 0.05);

      // For value 5: window is [3, 4, 5], mean=4, stddev≈0.816
      // Z-score for 5 = (5-4)/0.816 ≈ 1.225
      assert.ok(Math.abs(output2[1] - 1.225) < 0.05);
    });

    test("should handle window size of 1", async () => {
      const pipeline = createDspPipeline();
      pipeline.ZScoreNormalize({ mode: "moving", windowSize: 1 });

      const input = new Float32Array([10, 20, 30]);
      const output = await pipeline.process(input, {
        sampleRate: 1000,
        channels: 1,
      });

      // With window size 1, stddev is always 0, so all values should be 0
      output.forEach((val) => {
        assert.ok(Math.abs(val) < 0.0001, `Expected 0, got ${val}`);
      });
    });

    test("should throw error for moving mode without window size", () => {
      const pipeline = createDspPipeline();

      assert.throws(() => {
        pipeline.ZScoreNormalize({ mode: "moving" } as any);
      }, /either windowSize or windowDuration must be specified/);
    });

    test("should throw error for invalid window size", () => {
      const pipeline = createDspPipeline();

      assert.throws(() => {
        pipeline.ZScoreNormalize({ mode: "moving", windowSize: 0 });
      }, /windowSize must be a positive integer/);

      assert.throws(() => {
        pipeline.ZScoreNormalize({ mode: "moving", windowSize: -5 });
      }, /windowSize must be a positive integer/);

      assert.throws(() => {
        pipeline.ZScoreNormalize({ mode: "moving", windowSize: 3.14 });
      }, /windowSize must be a positive integer/);
    });

    test("should process multi-channel data with independent state", async () => {
      const pipeline = createDspPipeline();
      pipeline.ZScoreNormalize({ mode: "moving", windowSize: 3 });

      // 2 channels, 5 samples per channel (interleaved)
      // Channel 0: [1, 2, 3, 4, 5]
      // Channel 1: [10, 20, 30, 40, 50]
      const input = new Float32Array([1, 10, 2, 20, 3, 30, 4, 40, 5, 50]);
      const output = await pipeline.process(input, {
        sampleRate: 1000,
        channels: 2,
      });

      // Each channel should have independent sliding window state
      // Values should be normalized based on their own channel's statistics
      assert.ok(output.length === 10);

      // Extract channels for verification
      const ch0 = [];
      const ch1 = [];
      for (let i = 0; i < output.length; i++) {
        if (i % 2 === 0) ch0.push(output[i]);
        else ch1.push(output[i]);
      }

      // Both channels should show similar z-score patterns (since they have similar distributions)
      // Last value in each channel (window of [3,4,5] or [30,40,50]) should be ~1.225
      assert.ok(Math.abs(ch0[4] - 1.225) < 0.05);
      assert.ok(Math.abs(ch1[4] - 1.225) < 0.05);
    });
  });

  describe("State Management", () => {
    test("should serialize and deserialize state correctly for moving mode", async () => {
      const pipeline = createDspPipeline();
      pipeline.ZScoreNormalize({ mode: "moving", windowSize: 5 });

      // Process some data to build state
      const input1 = new Float32Array([1, 2, 3, 4, 5]);
      await pipeline.process(input1, { sampleRate: 1000, channels: 1 });

      // Save state
      const stateJson = await pipeline.saveState();
      const state = JSON.parse(stateJson);

      // Verify state structure
      assert.strictEqual(state.stages.length, 1);
      assert.strictEqual(state.stages[0].type, "zScoreNormalize");
      assert.strictEqual(state.stages[0].state.mode, "moving");
      assert.strictEqual(state.stages[0].state.windowSize, 5);
      assert.strictEqual(state.stages[0].state.numChannels, 1);

      // Verify buffer state
      const channelState = state.stages[0].state.channels[0];
      assert.ok(Array.isArray(channelState.buffer));
      assert.strictEqual(channelState.buffer.length, 5);
      assert.ok(typeof channelState.runningSum === "number");
      assert.ok(typeof channelState.runningSumOfSquares === "number");

      // Restore state in a new pipeline
      const pipeline2 = createDspPipeline();
      pipeline2.ZScoreNormalize({ mode: "moving", windowSize: 5 });
      await pipeline2.loadState(stateJson);

      // Continue processing
      const input2 = new Float32Array([6, 7, 8]);
      const output1 = await pipeline.process(input2.slice(), {
        sampleRate: 1000,
        channels: 1,
      });
      const output2 = await pipeline2.process(input2.slice(), {
        sampleRate: 1000,
        channels: 1,
      });

      // Both outputs should be identical
      for (let i = 0; i < output1.length; i++) {
        assert.strictEqual(output1[i], output2[i]);
      }
    });

    test("should reset state correctly", async () => {
      const pipeline = createDspPipeline();
      pipeline.ZScoreNormalize({ mode: "moving", windowSize: 3 });

      // Process some data
      const input1 = new Float32Array([10, 20, 30, 40, 50]);
      const output1 = await pipeline.process(input1, {
        sampleRate: 1000,
        channels: 1,
      });

      // Reset state
      pipeline.clearState();

      // Process the same data again
      const input2 = new Float32Array([10, 20, 30, 40, 50]);
      const output2 = await pipeline.process(input2, {
        sampleRate: 1000,
        channels: 1,
      });

      // First few samples should match (since state was reset)
      assert.strictEqual(output1[0], output2[0]);
      assert.strictEqual(output1[1], output2[1]);
      assert.strictEqual(output1[2], output2[2]);
    });

    test("should handle empty input", async () => {
      const pipeline = createDspPipeline();
      pipeline.ZScoreNormalize({ mode: "moving", windowSize: 3 });

      const input = new Float32Array([]);
      const output = await pipeline.process(input, {
        sampleRate: 1000,
        channels: 1,
      });

      assert.strictEqual(output.length, 0);
    });

    test("should validate running sums on state load", async () => {
      const pipeline1 = createDspPipeline();
      pipeline1.ZScoreNormalize({ mode: "moving", windowSize: 3 });

      const input = new Float32Array([1, 2, 3]);
      await pipeline1.process(input, { sampleRate: 1000, channels: 1 });

      const stateJson = await pipeline1.saveState();
      const state = JSON.parse(stateJson);

      // Corrupt the running sum
      state.stages[0].state.channels[0].runningSum = 999;

      const pipeline2 = createDspPipeline({
        fallbackOnLoadFailure: false,
        maxRetries: 0,
      });
      pipeline2.ZScoreNormalize({ mode: "moving", windowSize: 3 });

      // Should throw validation error
      await assert.rejects(
        async () => await pipeline2.loadState(JSON.stringify(state)),
        /Running sum validation failed/
      );
    });

    test("should validate window size on state load", async () => {
      const pipeline1 = createDspPipeline();
      pipeline1.ZScoreNormalize({ mode: "moving", windowSize: 5 });

      const input = new Float32Array([1, 2, 3, 4, 5]);
      await pipeline1.process(input, { sampleRate: 1000, channels: 1 });

      const stateJson = await pipeline1.saveState();
      const state = JSON.parse(stateJson);

      // Corrupt the window size
      state.stages[0].state.windowSize = 10;

      const pipeline2 = createDspPipeline({
        fallbackOnLoadFailure: false,
        maxRetries: 0,
      });
      pipeline2.ZScoreNormalize({ mode: "moving", windowSize: 5 });

      // Should throw validation error
      await assert.rejects(
        async () => await pipeline2.loadState(JSON.stringify(state)),
        /Window size mismatch/
      );
    });
  });

  describe("Edge Cases", () => {
    test("should handle single sample", async () => {
      const pipeline = createDspPipeline();
      pipeline.ZScoreNormalize({ mode: "batch" });

      const input = new Float32Array([42]);
      const output = await pipeline.process(input, {
        sampleRate: 1000,
        channels: 1,
      });

      // With single value, stddev is 0, so output should be 0
      assert.ok(Math.abs(output[0]) < 0.0001);
    });

    test("should handle very small values", async () => {
      const pipeline = createDspPipeline();
      pipeline.ZScoreNormalize({ mode: "batch" });

      const input = new Float32Array([1e-10, 2e-10, 3e-10, 4e-10, 5e-10]);
      const output = await pipeline.process(input, {
        sampleRate: 1000,
        channels: 1,
      });

      // Should still normalize correctly despite small values
      const mean = output.reduce((sum, val) => sum + val, 0) / output.length;
      assert.ok(Math.abs(mean) < 0.0001);
    });

    test("should handle very large values", async () => {
      const pipeline = createDspPipeline();
      pipeline.ZScoreNormalize({ mode: "batch" });

      const input = new Float32Array([1e6, 2e6, 3e6, 4e6, 5e6]);
      const output = await pipeline.process(input, {
        sampleRate: 1000,
        channels: 1,
      });

      // Should normalize correctly despite large values
      const mean = output.reduce((sum, val) => sum + val, 0) / output.length;
      assert.ok(Math.abs(mean) < 0.0001);
    });

    test("should respect custom epsilon value", async () => {
      const pipeline = createDspPipeline();
      pipeline.ZScoreNormalize({ mode: "batch", epsilon: 0.1 });

      // Create signal with very small stddev
      const input = new Float32Array([5.0, 5.001, 4.999, 5.0, 5.001]);
      const output = await pipeline.process(input, {
        sampleRate: 1000,
        channels: 1,
      });

      // With epsilon=0.1, small stddev (~0.001) should be treated as ~0
      // So all values should be close to 0
      output.forEach((val) => {
        assert.ok(Math.abs(val) < 0.1, `Expected small value, got ${val}`);
      });
    });
  });
});
