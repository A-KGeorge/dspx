import { test } from "node:test";
import * as assert from "node:assert/strict";
import { createDspPipeline } from "../bindings.js";

test("ChannelMerge - mono to stereo (duplicate channel)", async () => {
  const pipeline = createDspPipeline();
  try {
    // Duplicate channel 0 to create stereo from mono
    pipeline.ChannelMerge({
      mapping: [0, 0],
      numInputChannels: 1,
    });

    const input = new Float32Array([1, 2, 3, 4, 5]);

    const result = await pipeline.process(input, {
      channels: 1,
      sampleRate: 1000,
    });

    // Each mono sample should be duplicated
    assert.deepEqual(Array.from(result), [1, 1, 2, 2, 3, 3, 4, 4, 5, 5]);
  } finally {
    pipeline.dispose();
  }
});

test("ChannelMerge - duplicate each channel", async () => {
  const pipeline = createDspPipeline();

  // 3-channel to 6-channel by duplicating each
  pipeline.ChannelMerge({
    mapping: [0, 0, 1, 1, 2, 2],
    numInputChannels: 3,
  });

  const input = new Float32Array([
    1,
    2,
    3, // Sample 0: [A, B, C]
    4,
    5,
    6, // Sample 1
  ]);

  const result = await pipeline.process(input, {
    channels: 3,
    sampleRate: 1000,
  });

  assert.equal(result.length, 12); // 2 samples × 6 channels
  assert.deepEqual(Array.from(result), [
    1,
    1,
    2,
    2,
    3,
    3, // Sample 0: [A, A, B, B, C, C]
    4,
    4,
    5,
    5,
    6,
    6, // Sample 1
  ]);
});

test("ChannelMerge - custom routing [A,B,C] -> [A,C,B,A]", async () => {
  const pipeline = createDspPipeline();

  // Reorder and duplicate: [0, 2, 1, 0]
  pipeline.ChannelMerge({
    mapping: [0, 2, 1, 0],
    numInputChannels: 3,
  });

  const input = new Float32Array([
    1,
    2,
    3, // Sample 0: [A, B, C]
    4,
    5,
    6, // Sample 1
  ]);

  const result = await pipeline.process(input, {
    channels: 3,
    sampleRate: 1000,
  });

  assert.equal(result.length, 8); // 2 samples × 4 channels
  assert.deepEqual(Array.from(result), [
    1,
    3,
    2,
    1, // Sample 0: [A, C, B, A]
    4,
    6,
    5,
    4, // Sample 1
  ]);
});

test("ChannelMerge - identity mapping (pass through)", async () => {
  const pipeline = createDspPipeline();

  // Identity mapping: [0, 1, 2, 3]
  pipeline.ChannelMerge({
    mapping: [0, 1, 2, 3],
    numInputChannels: 4,
  });

  const input = new Float32Array([
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
  ]);

  const result = await pipeline.process(input, {
    channels: 4,
    sampleRate: 1000,
  });

  // Should pass through unchanged
  assert.deepEqual(result, input);
});

test("ChannelMerge - swap stereo channels", async () => {
  const pipeline = createDspPipeline();

  // Swap channels: [1, 0]
  pipeline.ChannelMerge({
    mapping: [1, 0],
    numInputChannels: 2,
  });

  const input = new Float32Array([
    1,
    2, // Sample 0: [L, R]
    3,
    4, // Sample 1
    5,
    6, // Sample 2
  ]);

  const result = await pipeline.process(input, {
    channels: 2,
    sampleRate: 1000,
  });

  // Should swap channels
  assert.deepEqual(Array.from(result), [
    2,
    1, // Sample 0: [R, L]
    4,
    3, // Sample 1
    6,
    5, // Sample 2
  ]);
});

test("ChannelMerge - expand single channel to 4 channels", async () => {
  const pipeline = createDspPipeline();

  // One channel to four: [0, 0, 0, 0]
  pipeline.ChannelMerge({
    mapping: [0, 0, 0, 0],
    numInputChannels: 1,
  });

  const input = new Float32Array([1, 2, 3]);

  const result = await pipeline.process(input, {
    channels: 1,
    sampleRate: 1000,
  });

  assert.equal(result.length, 12); // 3 samples × 4 channels
  assert.deepEqual(Array.from(result), [
    1,
    1,
    1,
    1, // Sample 0
    2,
    2,
    2,
    2, // Sample 1
    3,
    3,
    3,
    3, // Sample 2
  ]);
});

test("ChannelMerge - chain with other stages", async () => {
  const pipeline = createDspPipeline();

  // Duplicate channel 0, then compute RMS
  pipeline
    .ChannelMerge({
      mapping: [0, 0],
      numInputChannels: 1,
    })
    .Rms({
      mode: "batch",
    });

  const input = new Float32Array([1, 2, 3, 4, 5]);

  const result = await pipeline.process(input, {
    channels: 1,
    sampleRate: 1000,
  });

  // ChannelMerge outputs 5 samples × 2 channels = 10 values
  // RMS batch mode fills buffer with RMS value (doesn't resize)
  assert.equal(result.length, 10); // 5 samples × 2 merged channels

  // Expected RMS: sqrt((1^2 + 2^2 + 3^2 + 4^2 + 5^2) / 5) ≈ 3.316
  // Both channels should have same RMS (duplicated from channel 0)
  for (let i = 0; i < result.length; i++) {
    assert.ok(Math.abs(result[i] - 3.316) < 0.01);
  }
});

test("ChannelMerge - validation", async () => {
  const pipeline = createDspPipeline();

  // Empty mapping array
  assert.throws(() => {
    pipeline.ChannelMerge({
      mapping: [],
      numInputChannels: 2,
    });
  }, /mapping must be a non-empty array/);

  // Invalid numInputChannels
  assert.throws(() => {
    pipeline.ChannelMerge({
      mapping: [0],
      numInputChannels: 0,
    });
  }, /numInputChannels must be a positive integer/);

  // Mapping index out of range
  assert.throws(() => {
    pipeline.ChannelMerge({
      mapping: [0, 1, 5],
      numInputChannels: 4,
    });
  }, /mapping index 5 out of range/);

  // Negative mapping index
  assert.throws(() => {
    pipeline.ChannelMerge({
      mapping: [0, -1],
      numInputChannels: 2,
    });
  }, /mapping index -1 out of range/);
});

test("ChannelMerge - mono to stereo with processing", async () => {
  const pipeline = createDspPipeline();

  // Convert mono to stereo, then apply rectification
  pipeline
    .ChannelMerge({
      mapping: [0, 0],
      numInputChannels: 1,
    })
    .Rectify({
      mode: "full",
    });

  const input = new Float32Array([-1, -2, 3, -4, 5]);

  const result = await pipeline.process(input, {
    channels: 1,
    sampleRate: 1000,
  });

  // Should have 2 channels with absolute values
  assert.deepEqual(Array.from(result), [1, 1, 2, 2, 3, 3, 4, 4, 5, 5]);
});

test("ChannelMerge - complex multi-stage pipeline", async () => {
  const pipeline = createDspPipeline();

  // 2-channel input -> merge to 3 channels [0, 1, 0] -> select [0, 2] -> back to 2 channels
  pipeline
    .ChannelMerge({
      mapping: [0, 1, 0],
      numInputChannels: 2,
    })
    .ChannelSelect({
      channels: [0, 2],
      numInputChannels: 3,
    });

  const input = new Float32Array([
    1,
    2, // Sample 0: [A, B]
    3,
    4, // Sample 1
  ]);

  const result = await pipeline.process(input, {
    channels: 2,
    sampleRate: 1000,
  });

  // After merge: [A, B, A], after select: [A, A]
  assert.deepEqual(Array.from(result), [
    1,
    1, // Sample 0: [A, A]
    3,
    3, // Sample 1
  ]);
});
