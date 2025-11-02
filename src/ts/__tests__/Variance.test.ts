import { describe, it } from "node:test";
import assert from "node:assert";
import { createDspPipeline } from "../bindings.js";

describe("Variance Filter", () => {
  describe("Batch Mode (Stateless)", () => {
    it("should compute batch variance correctly", async () => {
      const pipeline = createDspPipeline().Variance({ mode: "batch" });

      // Test data: [1, 2, 3, 4, 5]
      // Mean = 3
      // Variance = ((1-3)² + (2-3)² + (3-3)² + (4-3)² + (5-3)²) / 5 = (4 + 1 + 0 + 1 + 4) / 5 = 2
      const input = new Float32Array([1, 2, 3, 4, 5]);
      const output = await pipeline.process(input, {
        sampleRate: 1000,
        channels: 1,
      });

      // All values should be the same (the variance)
      const expectedVariance = 2.0;
      for (let i = 0; i < output.length; i++) {
        assert.ok(
          Math.abs(output[i] - expectedVariance) < 0.001,
          `Expected ${expectedVariance}, got ${output[i]}`
        );
      }
    });

    it("should compute variance of zero signal", async () => {
      const pipeline = createDspPipeline().Variance({ mode: "batch" });

      const input = new Float32Array([0, 0, 0, 0, 0]);
      const output = await pipeline.process(input, {
        sampleRate: 1000,
        channels: 1,
      });

      // Variance of constant signal = 0
      for (let i = 0; i < output.length; i++) {
        assert.strictEqual(output[i], 0);
      }
    });

    it("should compute variance of constant signal", async () => {
      const pipeline = createDspPipeline().Variance({ mode: "batch" });

      const input = new Float32Array([5, 5, 5, 5, 5]);
      const output = await pipeline.process(input, {
        sampleRate: 1000,
        channels: 1,
      });

      // Variance of constant signal = 0
      for (let i = 0; i < output.length; i++) {
        assert.ok(Math.abs(output[i]) < 0.001, `Expected ~0, got ${output[i]}`);
      }
    });

    it("should handle negative values correctly", async () => {
      const pipeline = createDspPipeline().Variance({ mode: "batch" });

      // Test data: [-2, -1, 0, 1, 2]
      // Mean = 0
      // Variance = (4 + 1 + 0 + 1 + 4) / 5 = 2
      const input = new Float32Array([-2, -1, 0, 1, 2]);
      const output = await pipeline.process(input, {
        sampleRate: 1000,
        channels: 1,
      });

      const expectedVariance = 2.0;
      for (let i = 0; i < output.length; i++) {
        assert.ok(
          Math.abs(output[i] - expectedVariance) < 0.001,
          `Expected ${expectedVariance}, got ${output[i]}`
        );
      }
    });

    it("should be stateless between calls", async () => {
      const pipeline = createDspPipeline().Variance({ mode: "batch" });

      const input1 = new Float32Array([1, 2, 3, 4, 5]);
      const output1 = await pipeline.process(input1, {
        sampleRate: 1000,
        channels: 1,
      });

      const input2 = new Float32Array([10, 20, 30, 40, 50]);
      const output2 = await pipeline.process(input2, {
        sampleRate: 1000,
        channels: 1,
      });

      // First batch variance = 2.0
      assert.ok(Math.abs(output1[0] - 2.0) < 0.001);

      // Second batch variance = 200 (independent of first)
      // Mean = 30, Variance = ((10-30)² + (20-30)² + ... + (50-30)²) / 5 = 200
      assert.ok(Math.abs(output2[0] - 200.0) < 0.01);
    });

    it("should handle multi-channel data independently", async () => {
      const pipeline = createDspPipeline().Variance({ mode: "batch" });

      // 2-channel interleaved: [ch1, ch2, ch1, ch2, ch1, ch2]
      // Channel 1: [1, 3, 5] -> mean=3, variance=2.666...
      // Channel 2: [2, 4, 6] -> mean=4, variance=2.666...
      const input = new Float32Array([1, 2, 3, 4, 5, 6]);
      const output = await pipeline.process(input, {
        sampleRate: 1000,
        channels: 2,
      });

      // Check channel 1 values (indices 0, 2, 4)
      const expectedVar = 8 / 3; // ≈ 2.666...
      for (let i = 0; i < output.length; i += 2) {
        assert.ok(
          Math.abs(output[i] - expectedVar) < 0.01,
          `Channel 1: Expected ${expectedVar}, got ${output[i]}`
        );
      }

      // Check channel 2 values (indices 1, 3, 5)
      for (let i = 1; i < output.length; i += 2) {
        assert.ok(
          Math.abs(output[i] - expectedVar) < 0.01,
          `Channel 2: Expected ${expectedVar}, got ${output[i]}`
        );
      }
    });
  });

  describe("Moving Mode (Stateful)", () => {
    it("should compute moving variance with window size 3", async () => {
      const pipeline = createDspPipeline().Variance({
        mode: "moving",
        windowSize: 3,
      });

      // First 3 values fill the window
      const input = new Float32Array([1, 2, 3, 4, 5]);
      const output = await pipeline.process(input, {
        sampleRate: 1000,
        channels: 1,
      });

      // Sample 0: [1] -> variance = 0
      // Sample 1: [1,2] -> mean=1.5, var=0.25
      // Sample 2: [1,2,3] -> mean=2, var=0.666...
      // Sample 3: [2,3,4] -> mean=3, var=0.666...
      // Sample 4: [3,4,5] -> mean=4, var=0.666...

      assert.ok(Math.abs(output[0] - 0) < 0.001, `Sample 0: got ${output[0]}`);
      assert.ok(
        Math.abs(output[1] - 0.25) < 0.01,
        `Sample 1: got ${output[1]}`
      );
      assert.ok(
        Math.abs(output[2] - 2 / 3) < 0.01,
        `Sample 2: got ${output[2]}`
      );
      assert.ok(
        Math.abs(output[3] - 2 / 3) < 0.01,
        `Sample 3: got ${output[3]}`
      );
      assert.ok(
        Math.abs(output[4] - 2 / 3) < 0.01,
        `Sample 4: got ${output[4]}`
      );
    });

    it("should maintain state across multiple process calls", async () => {
      const pipeline = createDspPipeline().Variance({
        mode: "moving",
        windowSize: 3,
      });

      // First batch
      const input1 = new Float32Array([1, 2, 3]);
      const output1 = await pipeline.process(input1, {
        sampleRate: 1000,
        channels: 1,
      });

      // After first batch, window contains [1, 2, 3]
      // Last variance ≈ 0.666...
      assert.ok(Math.abs(output1[2] - 2 / 3) < 0.01);

      // Second batch
      const input2 = new Float32Array([4, 5]);
      const output2 = await pipeline.process(input2, {
        sampleRate: 1000,
        channels: 1,
      });

      // Window: [2,3,4] -> var=0.666..., then [3,4,5] -> var=0.666...
      assert.ok(Math.abs(output2[0] - 2 / 3) < 0.01);
      assert.ok(Math.abs(output2[1] - 2 / 3) < 0.01);
    });

    it("should handle window size of 1", async () => {
      const pipeline = createDspPipeline().Variance({
        mode: "moving",
        windowSize: 1,
      });

      const input = new Float32Array([5, 10, 15, 20]);
      const output = await pipeline.process(input, {
        sampleRate: 1000,
        channels: 1,
      });

      // With window=1, variance is always 0 (single value)
      for (let i = 0; i < output.length; i++) {
        assert.ok(Math.abs(output[i]) < 0.001, `Expected ~0, got ${output[i]}`);
      }
    });

    it("should throw error for moving mode without window size", () => {
      assert.throws(
        () => {
          createDspPipeline().Variance({
            mode: "moving",
            windowSize: undefined as any,
          });
        },
        { message: /either windowSize or windowDuration must be specified/ }
      );
    });

    it("should throw error for invalid window size", () => {
      assert.throws(
        () => {
          createDspPipeline().Variance({ mode: "moving", windowSize: 0 });
        },
        { message: /windowSize must be a positive integer/ }
      );

      assert.throws(
        () => {
          createDspPipeline().Variance({ mode: "moving", windowSize: -5 });
        },
        { message: /windowSize must be a positive integer/ }
      );

      assert.throws(
        () => {
          createDspPipeline().Variance({ mode: "moving", windowSize: 3.5 });
        },
        { message: /windowSize must be a positive integer/ }
      );
    });

    it("should process multi-channel data with independent state", async () => {
      const pipeline = createDspPipeline().Variance({
        mode: "moving",
        windowSize: 2,
      });

      // 2-channel interleaved data
      const input = new Float32Array([1, 10, 2, 20, 3, 30, 4, 40]);
      const output = await pipeline.process(input, {
        sampleRate: 1000,
        channels: 2,
      });

      // Channel 1 sequence: 1, 2, 3, 4
      // Channel 2 sequence: 10, 20, 30, 40

      // Each channel should maintain independent variance
      // All values should be non-negative
      for (let i = 0; i < output.length; i++) {
        assert.ok(
          output[i] >= 0,
          `Variance should be non-negative: ${output[i]}`
        );
      }
    });
  });

  describe("State Management", () => {
    it("should serialize and deserialize state correctly for moving mode", async () => {
      const pipeline = createDspPipeline().Variance({
        mode: "moving",
        windowSize: 5,
      });

      // Process some data to build state
      const input1 = new Float32Array([1, 2, 3, 4, 5, 6, 7, 8, 9, 10]);
      await pipeline.process(input1, { sampleRate: 1000, channels: 1 });

      // Save state
      const stateJson = await pipeline.saveState();
      const state = JSON.parse(stateJson);

      // Verify state structure
      assert.strictEqual(state.stages.length, 1);
      assert.strictEqual(state.stages[0].type, "variance");
      assert.strictEqual(state.stages[0].state.mode, "moving");
      assert.strictEqual(state.stages[0].state.windowSize, 5);
      assert.strictEqual(state.stages[0].state.channels.length, 1);

      // Create new pipeline with same structure and restore state
      const pipeline2 = createDspPipeline();
      pipeline2.Variance({ mode: "moving", windowSize: 5 });
      await pipeline2.loadState(stateJson);

      // Continue processing from saved state
      // NOTE: Create separate arrays since process() modifies in-place
      const input2a = new Float32Array([11, 12, 13]);
      const input2b = new Float32Array([11, 12, 13]);

      const output1 = await pipeline.process(input2a, {
        sampleRate: 1000,
        channels: 1,
      });
      const output2 = await pipeline2.process(input2b, {
        sampleRate: 1000,
        channels: 1,
      });

      // Both should produce identical results
      for (let i = 0; i < output1.length; i++) {
        assert.ok(
          Math.abs(output1[i] - output2[i]) < 0.001,
          `Mismatch at index ${i}: ${output1[i]} vs ${output2[i]}`
        );
      }
    });

    it("should reset state correctly", async () => {
      const pipeline = createDspPipeline().Variance({
        mode: "moving",
        windowSize: 3,
      });

      // Process data
      const input1 = new Float32Array([1, 2, 3, 4, 5]);
      await pipeline.process(input1, { sampleRate: 1000, channels: 1 });

      // Reset
      pipeline.clearState();

      // Process same data again
      const input2 = new Float32Array([1, 2, 3, 4, 5]);
      const output = await pipeline.process(input2, {
        sampleRate: 1000,
        channels: 1,
      });

      // Should produce same results as first time
      assert.ok(Math.abs(output[0] - 0) < 0.001);
      assert.ok(Math.abs(output[1] - 0.25) < 0.01);
      assert.ok(Math.abs(output[2] - 2 / 3) < 0.01);
    });

    it("should handle empty input", async () => {
      const pipeline = createDspPipeline().Variance({ mode: "batch" });

      const input = new Float32Array([]);
      const output = await pipeline.process(input, {
        sampleRate: 1000,
        channels: 1,
      });

      assert.strictEqual(output.length, 0);
    });

    it("should validate running sums on state load", async () => {
      const pipeline = createDspPipeline().Variance({
        mode: "moving",
        windowSize: 5,
      });

      const input = new Float32Array([1, 2, 3, 4, 5, 6, 7, 8]);
      await pipeline.process(input, { sampleRate: 1000, channels: 1 });

      const stateJson = await pipeline.saveState();
      const state = JSON.parse(stateJson);

      // Corrupt the running sum
      state.stages[0].state.channels[0].runningSum = 9999;

      const corruptedStateJson = JSON.stringify(state);

      const pipeline2 = createDspPipeline().Variance({
        mode: "moving",
        windowSize: 5,
      });

      await assert.rejects(
        async () => {
          await pipeline2.loadState(corruptedStateJson);
        },
        {
          message: /Running sum validation failed/,
        }
      );
    });

    it("should validate window size on state load", async () => {
      const pipeline = createDspPipeline().Variance({
        mode: "moving",
        windowSize: 5,
      });

      const input = new Float32Array([1, 2, 3, 4, 5]);
      await pipeline.process(input, { sampleRate: 1000, channels: 1 });

      const stateJson = await pipeline.saveState();
      const state = JSON.parse(stateJson);

      // Change window size
      state.stages[0].state.windowSize = 10;

      const modifiedStateJson = JSON.stringify(state);

      const pipeline2 = createDspPipeline().Variance({
        mode: "moving",
        windowSize: 5,
      });

      await assert.rejects(
        async () => {
          await pipeline2.loadState(modifiedStateJson);
        },
        {
          message: /Window size mismatch/,
        }
      );
    });
  });

  describe("Edge Cases", () => {
    it("should handle single sample", async () => {
      const pipeline = createDspPipeline().Variance({ mode: "batch" });

      const input = new Float32Array([42]);
      const output = await pipeline.process(input, {
        sampleRate: 1000,
        channels: 1,
      });

      // Variance of single value = 0
      assert.ok(Math.abs(output[0]) < 0.001);
    });

    it("should handle very small values", async () => {
      const pipeline = createDspPipeline().Variance({
        mode: "moving",
        windowSize: 3,
      });

      const input = new Float32Array([0.001, 0.002, 0.003, 0.004, 0.005]);
      const output = await pipeline.process(input, {
        sampleRate: 1000,
        channels: 1,
      });

      // Should complete without errors and produce non-negative values
      for (let i = 0; i < output.length; i++) {
        assert.ok(
          output[i] >= 0,
          `Variance should be non-negative: ${output[i]}`
        );
      }
    });

    it("should handle very large values", async () => {
      const pipeline = createDspPipeline().Variance({ mode: "batch" });

      const input = new Float32Array([
        1000000, 2000000, 3000000, 4000000, 5000000,
      ]);
      const output = await pipeline.process(input, {
        sampleRate: 1000,
        channels: 1,
      });

      // Should handle large numbers without overflow
      assert.ok(output[0] > 0);
      assert.ok(isFinite(output[0]));
    });

    it("should produce non-negative variance", async () => {
      const pipeline = createDspPipeline().Variance({
        mode: "moving",
        windowSize: 10,
      });

      // Random-ish data
      const input = new Float32Array(100).map(
        (_, i) => Math.sin(i * 0.1) * 100
      );
      const output = await pipeline.process(input, {
        sampleRate: 1000,
        channels: 1,
      });

      // Variance must always be non-negative
      for (let i = 0; i < output.length; i++) {
        assert.ok(
          output[i] >= 0,
          `Variance at index ${i} should be >= 0, got ${output[i]}`
        );
      }
    });
  });
});
