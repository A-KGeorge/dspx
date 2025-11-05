import { test } from "node:test";
import * as assert from "node:assert/strict";
import { createDspPipeline } from "../bindings.js";

test("ChannelSelect - basic channel selection", async () => {
  const pipeline = createDspPipeline();

  // 4-channel input, select channels 0 and 2
  pipeline.ChannelSelect({
    channels: [0, 2],
    numInputChannels: 4,
  });

  // Input: [A0, B0, C0, D0, A1, B1, C1, D1, ...] (4 channels)
  // Expected output: [A0, C0, A1, C1, ...] (2 channels)
  const input = new Float32Array([
    1,
    2,
    3,
    4, // Sample 0: channels [A, B, C, D]
    5,
    6,
    7,
    8, // Sample 1
    9,
    10,
    11,
    12, // Sample 2
  ]);

  const result = await pipeline.process(input, {
    channels: 4,
    sampleRate: 1000,
  });

  // Should extract channels 0 and 2 from each sample
  assert.equal(result.length, 6); // 3 samples × 2 channels
  assert.deepEqual(Array.from(result), [
    1,
    3, // Sample 0: A, C
    5,
    7, // Sample 1: A, C
    9,
    11, // Sample 2: A, C
  ]);
});

test("ChannelSelect - reorder channels (swap stereo)", async () => {
  const pipeline = createDspPipeline();

  // Swap left and right channels
  pipeline.ChannelSelect({
    channels: [1, 0],
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

test("ChannelSelect - duplicate channel (mono to stereo)", async () => {
  const pipeline = createDspPipeline();

  // Duplicate channel 0 to create stereo from mono
  pipeline.ChannelSelect({
    channels: [0, 0],
    numInputChannels: 1,
  });

  const input = new Float32Array([1, 2, 3, 4, 5]);

  const result = await pipeline.process(input, {
    channels: 1,
    sampleRate: 1000,
  });

  // Each mono sample should be duplicated
  assert.deepEqual(Array.from(result), [1, 1, 2, 2, 3, 3, 4, 4, 5, 5]);
});

test("ChannelSelect - select non-contiguous channels", async () => {
  const pipeline = createDspPipeline();

  // From 8-channel EEG, select channels 0, 3, 7
  pipeline.ChannelSelect({
    channels: [0, 3, 7],
    numInputChannels: 8,
  });

  const input = new Float32Array([
    1,
    2,
    3,
    4,
    5,
    6,
    7,
    8, // Sample 0
    9,
    10,
    11,
    12,
    13,
    14,
    15,
    16, // Sample 1
  ]);

  const result = await pipeline.process(input, {
    channels: 8,
    sampleRate: 1000,
  });

  // Should extract channels 0, 3, 7
  assert.deepEqual(Array.from(result), [
    1,
    4,
    8, // Sample 0: ch[0, 3, 7]
    9,
    12,
    16, // Sample 1: ch[0, 3, 7]
  ]);
});

test("ChannelSelect - complex pattern (duplicate and reorder)", async () => {
  const pipeline = createDspPipeline();

  // From 3 channels, create 5 channels with pattern [A, C, B, A, C]
  pipeline.ChannelSelect({
    channels: [0, 2, 1, 0, 2],
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

  assert.equal(result.length, 10); // 2 samples × 5 channels
  assert.deepEqual(Array.from(result), [
    1,
    3,
    2,
    1,
    3, // Sample 0: [A, C, B, A, C]
    4,
    6,
    5,
    4,
    6, // Sample 1
  ]);
});

test("ChannelSelect - chain with other stages", async () => {
  const pipeline = createDspPipeline();

  // Select channels 0 and 1 from 4-channel input, then compute RMS
  pipeline
    .ChannelSelect({
      channels: [0, 1],
      numInputChannels: 4,
    })
    .Rms({
      mode: "batch",
    });

  const input = new Float32Array([
    1,
    2,
    3,
    4, // Sample 0
    2,
    3,
    4,
    5, // Sample 1
    3,
    4,
    5,
    6, // Sample 2
  ]);

  const result = await pipeline.process(input, {
    channels: 4,
    sampleRate: 1000,
  });

  // ChannelSelect outputs 3 samples × 2 channels = 6 values
  // RMS batch mode fills buffer with RMS value (doesn't resize)
  assert.equal(result.length, 6); // 3 samples × 2 selected channels

  // Channel 0 RMS: sqrt((1^2 + 2^2 + 3^2) / 3) ≈ 2.16
  // Channel 1 RMS: sqrt((2^2 + 3^2 + 4^2) / 3) ≈ 3.11
  // All samples in each channel should have the same RMS value
  assert.ok(Math.abs(result[0] - 2.16) < 0.01);
  assert.ok(Math.abs(result[1] - 3.11) < 0.01);
  assert.ok(Math.abs(result[2] - 2.16) < 0.01);
  assert.ok(Math.abs(result[3] - 3.11) < 0.01);
  assert.ok(Math.abs(result[4] - 2.16) < 0.01);
  assert.ok(Math.abs(result[5] - 3.11) < 0.01);
});

test("ChannelSelect - validation", async () => {
  const pipeline = createDspPipeline();

  // Empty channels array
  assert.throws(() => {
    pipeline.ChannelSelect({
      channels: [],
      numInputChannels: 2,
    });
  }, /channels must be a non-empty array/);

  // Invalid numInputChannels
  assert.throws(() => {
    pipeline.ChannelSelect({
      channels: [0],
      numInputChannels: 0,
    });
  }, /numInputChannels must be a positive integer/);

  // Channel index out of range
  assert.throws(() => {
    pipeline.ChannelSelect({
      channels: [0, 1, 5],
      numInputChannels: 4,
    });
  }, /channel index 5 out of range/);

  // Negative channel index
  assert.throws(() => {
    pipeline.ChannelSelect({
      channels: [0, -1],
      numInputChannels: 2,
    });
  }, /channel index -1 out of range/);
});

test("ChannelSelect - single channel to single channel", async () => {
  const pipeline = createDspPipeline();

  pipeline.ChannelSelect({
    channels: [0],
    numInputChannels: 1,
  });

  const input = new Float32Array([1, 2, 3, 4, 5]);
  const result = await pipeline.process(input, {
    channels: 1,
    sampleRate: 1000,
  });

  // Should pass through unchanged
  assert.deepEqual(result, input);
});

test("ChannelSelect - all channels in original order", async () => {
  const pipeline = createDspPipeline();

  pipeline.ChannelSelect({
    channels: [0, 1, 2, 3],
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
