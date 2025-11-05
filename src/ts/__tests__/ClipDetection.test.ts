import { test } from "node:test";
import assert from "node:assert";
import { createDspPipeline } from "../bindings.js";

test("ClipDetection - basic clipping detection", async () => {
  const pipeline = createDspPipeline();
  pipeline.ClipDetection({ threshold: 0.8 });

  const input = new Float32Array([0.5, 0.9, -0.95, 0.7, 1.0, -0.5, 0.85]);

  const result = await pipeline.process(input, {
    channels: 1,
    sampleRate: 1000,
  });

  // Expected: 0.0, 1.0, 1.0, 0.0, 1.0, 0.0, 1.0
  assert.equal(result[0], 0.0); // 0.5 < 0.8
  assert.equal(result[1], 1.0); // 0.9 >= 0.8
  assert.equal(result[2], 1.0); // |-0.95| >= 0.8
  assert.equal(result[3], 0.0); // 0.7 < 0.8
  assert.equal(result[4], 1.0); // 1.0 >= 0.8
  assert.equal(result[5], 0.0); // |-0.5| < 0.8
  assert.equal(result[6], 1.0); // 0.85 >= 0.8
});

test("ClipDetection - no clipping", async () => {
  const pipeline = createDspPipeline();
  pipeline.ClipDetection({ threshold: 1.0 });

  const input = new Float32Array([0.5, 0.8, -0.9, 0.3, 0.7]);

  const result = await pipeline.process(input, {
    channels: 1,
    sampleRate: 1000,
  });

  // All values below threshold
  for (let i = 0; i < result.length; i++) {
    assert.equal(result[i], 0.0);
  }
});

test("ClipDetection - all clipping", async () => {
  const pipeline = createDspPipeline();
  pipeline.ClipDetection({ threshold: 0.5 });

  const input = new Float32Array([1.0, 0.9, -0.8, -1.0, 0.6]);

  const result = await pipeline.process(input, {
    channels: 1,
    sampleRate: 1000,
  });

  // All values above threshold
  for (let i = 0; i < result.length; i++) {
    assert.equal(result[i], 1.0);
  }
});

test("ClipDetection - multi-channel", async () => {
  const pipeline = createDspPipeline();
  pipeline.ClipDetection({ threshold: 0.7 });

  // 2 channels, 3 samples: [ch0, ch1] repeated
  const input = new Float32Array([0.5, 0.9, 0.8, 0.6, 0.4, 1.0]);

  const result = await pipeline.process(input, {
    channels: 2,
    sampleRate: 1000,
  });

  // Ch0: 0.5, 0.8, 0.4 -> 0.0, 1.0, 0.0
  // Ch1: 0.9, 0.6, 1.0 -> 1.0, 0.0, 1.0
  assert.equal(result[0], 0.0); // ch0: 0.5
  assert.equal(result[1], 1.0); // ch1: 0.9
  assert.equal(result[2], 1.0); // ch0: 0.8
  assert.equal(result[3], 0.0); // ch1: 0.6
  assert.equal(result[4], 0.0); // ch0: 0.4
  assert.equal(result[5], 1.0); // ch1: 1.0
});

test("ClipDetection - validation errors", () => {
  const pipeline = createDspPipeline();

  // Missing threshold
  assert.throws(() => {
    pipeline.ClipDetection({ threshold: undefined as any });
  }, /threshold must be > 0/);

  // Zero threshold
  assert.throws(() => {
    pipeline.ClipDetection({ threshold: 0 });
  }, /threshold must be > 0/);

  // Negative threshold
  assert.throws(() => {
    pipeline.ClipDetection({ threshold: -0.5 });
  }, /threshold must be > 0/);
});

test("ClipDetection - chain with other stages", async () => {
  const pipeline = createDspPipeline();

  // Generate signal, detect clipping, compute RMS of clipped regions
  pipeline.ClipDetection({ threshold: 0.7 }).Rms({ mode: "batch" });

  const input = new Float32Array([0.5, 0.9, -0.8, 0.6, 1.0]);

  const result = await pipeline.process(input, {
    channels: 1,
    sampleRate: 1000,
  });

  // After ClipDetection: [0.0, 1.0, 1.0, 0.0, 1.0]
  // RMS of [0, 1, 1, 0, 1] = sqrt((0 + 1 + 1 + 0 + 1) / 5) = sqrt(0.6) ≈ 0.7746
  assert.ok(Math.abs(result[0] - 0.7746) < 0.01);
});

test("ClipDetection - state persistence", async () => {
  const pipeline = createDspPipeline();
  pipeline.ClipDetection({ threshold: 0.8 });

  const input1 = new Float32Array([0.5, 0.9]);
  const result1 = await pipeline.process(input1, {
    channels: 1,
    sampleRate: 1000,
  });

  // Save state
  const state = await pipeline.saveState();

  // Create new pipeline and restore
  const pipeline2 = createDspPipeline();
  pipeline2.ClipDetection({ threshold: 0.5 }); // Different threshold initially
  await pipeline2.loadState(state);

  // Process with restored pipeline (should use threshold 0.8)
  const input2 = new Float32Array([0.75, 0.85]);
  const result2 = await pipeline2.process(input2, {
    channels: 1,
    sampleRate: 1000,
  });

  assert.equal(result2[0], 0.0); // 0.75 < 0.8
  assert.equal(result2[1], 1.0); // 0.85 >= 0.8
});
