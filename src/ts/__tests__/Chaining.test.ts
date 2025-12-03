import { describe, test, beforeEach, afterEach } from "node:test";
import assert from "node:assert/strict";
import { createDspPipeline, DspProcessor } from "../bindings.js";

const DEFAULT_OPTIONS = { channels: 1, sampleRate: 44100 };

function assertCloseTo(actual: number, expected: number, precision = 4) {
  const tolerance = Math.pow(10, -precision);
  assert.ok(
    Math.abs(actual - expected) < tolerance,
    `Expected ${actual} to be close to ${expected} (tolerance: ${tolerance})`
  );
}

describe("DSP Pipeline Chaining", () => {
  let processor: DspProcessor;

  beforeEach(() => {
    processor = createDspPipeline();
  });

  afterEach(() => {
    processor.dispose();
  });

  describe("Two-Stage Chains", () => {
    test("should chain MovingAverage → Rectify", async () => {
      processor
        .MovingAverage({ mode: "moving", windowSize: 2 })
        .Rectify({ mode: "full" });

      // Input with negative values
      const input = new Float32Array([1, -3, 2, -4]);
      const output = await processor.process(input, DEFAULT_OPTIONS);

      // Stage 1 (MovingAverage):
      // [1] → 1
      // [1, -3] → -1
      // [-3, 2] → -0.5
      // [2, -4] → -1
      //
      // Stage 2 (Rectify full):
      // 1 → 1
      // -1 → 1
      // -0.5 → 0.5
      // -1 → 1
      assertCloseTo(output[0], 1);
      assertCloseTo(output[1], 1);
      assertCloseTo(output[2], 0.5);
      assertCloseTo(output[3], 1);
    });

    test("should chain Rectify → MovingAverage", async () => {
      processor
        .Rectify({ mode: "full" })
        .MovingAverage({ mode: "moving", windowSize: 2 });

      const input = new Float32Array([1, -3, 2, -4]);
      const output = await processor.process(input, DEFAULT_OPTIONS);

      // Stage 1 (Rectify): [1, 3, 2, 4]
      // Stage 2 (MovingAverage):
      // [1] → 1
      // [1, 3] → 2
      // [3, 2] → 2.5
      // [2, 4] → 3
      assertCloseTo(output[0], 1);
      assertCloseTo(output[1], 2);
      assertCloseTo(output[2], 2.5);
      assertCloseTo(output[3], 3);
    });

    test("should chain MovingAverage → RMS", async () => {
      processor
        .MovingAverage({ mode: "moving", windowSize: 2 })
        .Rms({ mode: "moving", windowSize: 2 });

      const input = new Float32Array([4, 0, 8, 0]);
      const output = await processor.process(input, DEFAULT_OPTIONS);

      // Stage 1 (MovingAverage):
      // [4] → 4
      // [4, 0] → 2
      // [0, 8] → 4
      // [8, 0] → 4
      //
      // Stage 2 (RMS):
      // [4] → 4
      // [4, 2] → sqrt(20/2) = 3.1622...
      // [2, 4] → sqrt(20/2) = 3.1622...
      // [4, 4] → 4
      assertCloseTo(output[0], 4);
      assertCloseTo(output[1], 3.1622);
      assertCloseTo(output[2], 3.1622);
      assertCloseTo(output[3], 4);
    });

    test("should chain Rectify → RMS", async () => {
      processor
        .Rectify({ mode: "half" })
        .Rms({ mode: "moving", windowSize: 2 });

      const input = new Float32Array([3, -3, 4, -4]);
      const output = await processor.process(input, DEFAULT_OPTIONS);

      // Stage 1 (Rectify half): [3, 0, 4, 0]
      // Stage 2 (RMS):
      // [3] → 3
      // [3, 0] → sqrt(9/2) = 2.1213...
      // [0, 4] → sqrt(16/2) = 2.8284...
      // [4, 0] → sqrt(16/2) = 2.8284...
      assertCloseTo(output[0], 3);
      assertCloseTo(output[1], 2.1213);
      assertCloseTo(output[2], 2.8284);
      assertCloseTo(output[3], 2.8284);
    });
  });

  describe("Three-Stage Chains", () => {
    test("should chain MovingAverage → RMS → Rectify", async () => {
      processor
        .MovingAverage({ mode: "moving", windowSize: 2 })
        .Rms({ mode: "moving", windowSize: 2 })
        .Rectify({ mode: "full" });

      const input = new Float32Array([2, -2, 2, -2]);
      const output = await processor.process(input, DEFAULT_OPTIONS);

      // Stage 1 (MovingAverage):
      // [2] → 2
      // [2, -2] → 0
      // [-2, 2] → 0
      // [2, -2] → 0
      //
      // Stage 2 (RMS):
      // [2] → 2
      // [2, 0] → sqrt(4/2) = 1.4142...
      // [0, 0] → 0
      // [0, 0] → 0
      //
      // Stage 3 (Rectify): All already positive or zero
      assertCloseTo(output[0], 2);
      assertCloseTo(output[1], 1.4142);
      assertCloseTo(output[2], 0);
      assertCloseTo(output[3], 0);
    });

    test("should chain Rectify → MovingAverage → RMS", async () => {
      processor
        .Rectify({ mode: "full" })
        .MovingAverage({ mode: "moving", windowSize: 2 })
        .Rms({ mode: "moving", windowSize: 2 });

      const input = new Float32Array([1, -1, 1, -1]);
      const output = await processor.process(input, DEFAULT_OPTIONS);

      // Stage 1 (Rectify): [1, 1, 1, 1]
      // Stage 2 (MovingAverage): [1, 1, 1, 1]
      // Stage 3 (RMS): [1, 1, 1, 1]
      output.forEach((val) => assertCloseTo(val, 1));
    });

    test("should chain MovingAverage → Rectify → RMS", async () => {
      processor
        .MovingAverage({ mode: "moving", windowSize: 3 })
        .Rectify({ mode: "full" })
        .Rms({ mode: "moving", windowSize: 2 });

      const input = new Float32Array([3, -6, 3, 0, -3]);
      const output = await processor.process(input, DEFAULT_OPTIONS);

      assert.equal(output.length, 5);
      // All outputs should be positive (after rectify) and meaningful
      output.forEach((val) => assert.ok(val >= 0));
    });
  });

  describe("State Management with Chains", () => {
    test("should save and restore state for two-stage chain", async () => {
      processor
        .MovingAverage({ mode: "moving", windowSize: 2 })
        .Rectify({ mode: "full" });

      // Build state
      await processor.process(new Float32Array([1, -2, 3]), DEFAULT_OPTIONS);

      const stateJson = await processor.saveState();
      const state = JSON.parse(stateJson);

      // Verify state structure
      assert.equal(state.stages.length, 2);
      assert.equal(state.stages[0].type, "movingAverage");
      assert.equal(state.stages[1].type, "rectify");

      // Load into new processor
      const processor2 = createDspPipeline();
      try {
        processor2
          .MovingAverage({ mode: "moving", windowSize: 2 })
          .Rectify({ mode: "full" });
        await processor2.loadState(stateJson);

        // Both should produce same output
        const output1 = await processor.process(
          new Float32Array([4]),
          DEFAULT_OPTIONS
        );
        const output2 = await processor2.process(
          new Float32Array([4]),
          DEFAULT_OPTIONS
        );

        assertCloseTo(output2[0], output1[0]);
      } finally {
        processor2.dispose();
      }
    });

    test("should save and restore state for three-stage chain", async () => {
      processor
        .MovingAverage({ mode: "moving", windowSize: 2 })
        .Rms({ mode: "moving", windowSize: 2 })
        .Rectify({ mode: "half" });

      // Build state
      await processor.process(new Float32Array([1, 2, 3, 4]), DEFAULT_OPTIONS);

      const stateJson = await processor.saveState();
      const state = JSON.parse(stateJson);

      // Verify all three stages
      assert.equal(state.stages.length, 3);
      assert.equal(state.stages[0].type, "movingAverage");
      assert.equal(state.stages[1].type, "rms");
      assert.equal(state.stages[2].type, "rectify");

      // Load and verify continuity
      const processor2 = createDspPipeline();
      try {
        processor2
          .MovingAverage({ mode: "moving", windowSize: 2 })
          .Rms({ mode: "moving", windowSize: 2 })
          .Rectify({ mode: "half" });
        await processor2.loadState(stateJson);

        const output1 = await processor.process(
          new Float32Array([5]),
          DEFAULT_OPTIONS
        );
        const output2 = await processor2.process(
          new Float32Array([5]),
          DEFAULT_OPTIONS
        );

        assertCloseTo(output2[0], output1[0]);
      } finally {
        processor2.dispose();
      }
    });

    test("should maintain state continuity across batches in chain", async () => {
      processor
        .MovingAverage({ mode: "moving", windowSize: 3 })
        .Rms({ mode: "moving", windowSize: 2 });

      // Process multiple batches
      const output1 = await processor.process(
        new Float32Array([1, 2]),
        DEFAULT_OPTIONS
      );
      const output2 = await processor.process(
        new Float32Array([3, 4]),
        DEFAULT_OPTIONS
      );
      const output3 = await processor.process(
        new Float32Array([5]),
        DEFAULT_OPTIONS
      );

      // All outputs should be valid
      assert.ok(output1.every((v) => !isNaN(v) && v >= 0));
      assert.ok(output2.every((v) => !isNaN(v) && v >= 0));
      assert.ok(output3.every((v) => !isNaN(v) && v >= 0));
    });

    test("should reset entire chain correctly", async () => {
      processor
        .MovingAverage({ mode: "moving", windowSize: 2 })
        .Rms({ mode: "moving", windowSize: 2 })
        .Rectify();

      // Build state
      await processor.process(new Float32Array([1, 2, 3, 4]), DEFAULT_OPTIONS);

      // Reset
      processor.clearState();

      // Process fresh data
      const output = await processor.process(
        new Float32Array([10]),
        DEFAULT_OPTIONS
      );

      // Should be close to 10 (single value through the pipeline)
      assertCloseTo(output[0], 10);
    });
  });

  describe("Order Dependency", () => {
    test("should produce different results with different chain order", async () => {
      const processor1 = createDspPipeline();
      const processor2 = createDspPipeline();

      try {
        processor1
          .Rectify({ mode: "half" })
          .MovingAverage({ mode: "moving", windowSize: 2 });
        processor2
          .MovingAverage({ mode: "moving", windowSize: 2 })
          .Rectify({ mode: "half" });

        const input = new Float32Array([1, -3, 2]);

        const output1 = await processor1.process(
          new Float32Array(input),
          DEFAULT_OPTIONS
        );
        const output2 = await processor2.process(
          new Float32Array(input),
          DEFAULT_OPTIONS
        );

        // Processor1: Rectify first → [1, 0, 2] → MovingAvg → [1, 0.5, 1]
        // Processor2: MovingAvg first → [1, -1, 0.5] → Rectify → [1, 0, 0.5]
        assert.notDeepEqual(Array.from(output1), Array.from(output2));
      } finally {
        processor1.dispose();
        processor2.dispose();
      }
    });
  });

  describe("Complex Scenarios", () => {
    test("should handle empty input in chain", async () => {
      processor
        .MovingAverage({ mode: "moving", windowSize: 2 })
        .Rms({ mode: "moving", windowSize: 2 });

      const output = await processor.process(
        new Float32Array([]),
        DEFAULT_OPTIONS
      );
      assert.equal(output.length, 0);
    });

    test("should handle single sample through entire chain", async () => {
      processor
        .MovingAverage({ mode: "moving", windowSize: 3 })
        .Rectify({ mode: "full" })
        .Rms({ mode: "moving", windowSize: 2 });

      const output = await processor.process(
        new Float32Array([5]),
        DEFAULT_OPTIONS
      );

      assert.equal(output.length, 1);
      assertCloseTo(output[0], 5);
    });

    test("should process large chain efficiently", async () => {
      processor
        .MovingAverage({ mode: "moving", windowSize: 5 })
        .Rectify({ mode: "full" })
        .Rms({ mode: "moving", windowSize: 5 })
        .MovingAverage({ mode: "moving", windowSize: 3 });

      const input = new Float32Array(100).map((_, i) => Math.sin(i * 0.1));
      const output = await processor.process(input, DEFAULT_OPTIONS);

      assert.equal(output.length, 100);
      output.forEach((val) => {
        assert.ok(!isNaN(val));
        assert.ok(val >= 0); // After rectify, all should be positive
      });
    });

    test("should handle repeated same-stage chains", async () => {
      processor
        .MovingAverage({ mode: "moving", windowSize: 2 })
        .MovingAverage({ mode: "moving", windowSize: 2 })
        .MovingAverage({ mode: "moving", windowSize: 2 });

      const input = new Float32Array([1, 2, 3, 4, 5]);
      const output = await processor.process(input, DEFAULT_OPTIONS);

      assert.equal(output.length, 5);
      // Triple smoothing should produce smoother output
      output.forEach((val) => assert.ok(!isNaN(val) && val > 0));
    });
  });

  describe("Edge Cases in Chains", () => {
    test("should handle all-zero signal through chain", async () => {
      processor
        .MovingAverage({ mode: "moving", windowSize: 3 })
        .Rms({ mode: "moving", windowSize: 2 });

      const input = new Float32Array([0, 0, 0, 0, 0]);
      const output = await processor.process(input, DEFAULT_OPTIONS);

      output.forEach((val) => assertCloseTo(val, 0));
    });

    test("should handle extreme values through chain", async () => {
      processor
        .Rectify({ mode: "full" })
        .MovingAverage({ mode: "moving", windowSize: 2 });

      const input = new Float32Array([1e6, -1e6, 1e-6, -1e-6]);
      const output = await processor.process(input, DEFAULT_OPTIONS);

      assert.ok(output.every((v) => !isNaN(v)));
      assert.ok(output.some((v) => v > 1e5)); // Large values present
      assert.ok(output.some((v) => v < 1e-5)); // Small values present
    });
  });
});
