import { test } from "node:test";
import assert from "node:assert";
import { createDspPipeline } from "../bindings.js";

test("Differentiator - basic differentiation", async () => {
  const pipeline = createDspPipeline();
  pipeline.Differentiator();

  const input = new Float32Array([1.0, 3.0, 6.0, 10.0, 15.0]);

  const result = await pipeline.process(input, {
    channels: 1,
    sampleRate: 1000,
  });

  // Differences: 3-1=2, 6-3=3, 10-6=4, 15-10=5
  // First sample uses previous state (0), so 1-0=1
  assert.equal(result[0], 1.0); // 1.0 - 0.0 (initial state)
  assert.equal(result[1], 2.0); // 3.0 - 1.0
  assert.equal(result[2], 3.0); // 6.0 - 3.0
  assert.equal(result[3], 4.0); // 10.0 - 6.0
  assert.equal(result[4], 5.0); // 15.0 - 10.0
});

test("Differentiator - constant signal", async () => {
  const pipeline = createDspPipeline();
  pipeline.Differentiator();

  const input = new Float32Array([5.0, 5.0, 5.0, 5.0]);

  const result = await pipeline.process(input, {
    channels: 1,
    sampleRate: 1000,
  });

  // Constant signal -> all differences = 0 (except first)
  assert.equal(result[0], 5.0); // 5.0 - 0.0 (initial)
  assert.equal(result[1], 0.0); // 5.0 - 5.0
  assert.equal(result[2], 0.0); // 5.0 - 5.0
  assert.equal(result[3], 0.0); // 5.0 - 5.0
});

test("Differentiator - negative differences", async () => {
  const pipeline = createDspPipeline();
  pipeline.Differentiator();

  const input = new Float32Array([10.0, 8.0, 5.0, 1.0]);

  const result = await pipeline.process(input, {
    channels: 1,
    sampleRate: 1000,
  });

  // Decreasing signal -> negative differences
  assert.equal(result[0], 10.0); // 10.0 - 0.0
  assert.equal(result[1], -2.0); // 8.0 - 10.0
  assert.equal(result[2], -3.0); // 5.0 - 8.0
  assert.equal(result[3], -4.0); // 1.0 - 5.0
});

test("Differentiator - velocity from position", async () => {
  const pipeline = createDspPipeline();
  pipeline.Differentiator();

  // Position data: object moving with constant velocity
  // x(t) = 2*t (velocity = 2 m/s)
  const position = new Float32Array([0.0, 2.0, 4.0, 6.0, 8.0]);

  const velocity = await pipeline.process(position, {
    channels: 1,
    sampleRate: 1,
  }); // 1 Hz sample rate

  // Velocity should be ~2 m/s (except first sample)
  for (let i = 1; i < velocity.length; i++) {
    assert.ok(Math.abs(velocity[i] - 2.0) < 0.001);
  }
});

test("Differentiator - edge detection in signal", async () => {
  const pipeline = createDspPipeline();
  pipeline.Differentiator();

  // Step function
  const input = new Float32Array([0.0, 0.0, 0.0, 1.0, 1.0, 1.0]);

  const result = await pipeline.process(input, {
    channels: 1,
    sampleRate: 1000,
  });

  // Large spike at edge (index 3)
  assert.equal(result[0], 0.0);
  assert.equal(result[1], 0.0);
  assert.equal(result[2], 0.0);
  assert.equal(result[3], 1.0); // Edge detected!
  assert.equal(result[4], 0.0);
  assert.equal(result[5], 0.0);
});

test("Differentiator - multi-channel", async () => {
  const pipeline = createDspPipeline();
  pipeline.Differentiator();

  // 2 channels, 3 samples
  const input = new Float32Array([
    1.0,
    2.0, // Sample 0
    3.0,
    5.0, // Sample 1
    6.0,
    9.0, // Sample 2
  ]);

  const result = await pipeline.process(input, {
    channels: 2,
    sampleRate: 1000,
  });

  // Ch0: 1, 3, 6 -> diff: 1, 2, 3
  // Ch1: 2, 5, 9 -> diff: 2, 3, 4
  assert.equal(result[0], 1.0); // ch0: 1-0
  assert.equal(result[1], 2.0); // ch1: 2-0
  assert.equal(result[2], 2.0); // ch0: 3-1
  assert.equal(result[3], 3.0); // ch1: 5-2
  assert.equal(result[4], 3.0); // ch0: 6-3
  assert.equal(result[5], 4.0); // ch1: 9-5
});

test("Differentiator - state continuity across batches", async () => {
  const pipeline = createDspPipeline();
  pipeline.Differentiator();

  // First batch
  const input1 = new Float32Array([1.0, 2.0, 3.0]);
  const result1 = await pipeline.process(input1, {
    channels: 1,
    sampleRate: 1000,
  });

  // Second batch (continues from 3.0)
  const input2 = new Float32Array([4.0, 5.0]);
  const result2 = await pipeline.process(input2, {
    channels: 1,
    sampleRate: 1000,
  });

  // First batch differences: 1-0, 2-1, 3-2
  assert.equal(result1[0], 1.0);
  assert.equal(result1[1], 1.0);
  assert.equal(result1[2], 1.0);

  // Second batch continues: 4-3, 5-4
  assert.equal(result2[0], 1.0); // 4.0 - 3.0
  assert.equal(result2[1], 1.0); // 5.0 - 4.0
});

test("Differentiator - sine wave (approximates cosine)", async () => {
  const pipeline = createDspPipeline();
  pipeline.Differentiator();

  // Generate sine wave: sin(2πft)
  const sampleRate = 100;
  const freq = 1; // 1 Hz
  const numSamples = 100;
  const input = new Float32Array(numSamples);

  for (let i = 0; i < numSamples; i++) {
    input[i] = Math.sin((2 * Math.PI * freq * i) / sampleRate);
  }

  const derivative = await pipeline.process(input, {
    channels: 1,
    sampleRate,
  });

  // d/dt sin(ωt) = ω*cos(ωt)
  // Check a few points (skip first sample due to initial state)
  const omega = (2 * Math.PI * freq) / sampleRate;

  for (let i = 10; i < 20; i++) {
    const expected = omega * Math.cos((2 * Math.PI * freq * i) / sampleRate);
    // Allow some error due to discrete approximation
    assert.ok(Math.abs(derivative[i] - expected) < 0.1);
  }
});

test("Differentiator - chain with lowpass filter", async () => {
  const pipeline = createDspPipeline();

  // Noisy position data - smooth first, then differentiate
  pipeline.MovingAverage({ mode: "moving", windowSize: 3 }).Differentiator();

  // Noisy step function
  const input = new Float32Array([0, 0.1, -0.1, 1.0, 1.1, 0.9, 1.0]);

  const result = await pipeline.process(input, {
    channels: 1,
    sampleRate: 1000,
  });

  // After smoothing, edge should be cleaner
  // Result will show smoothed derivative
  assert.ok(result.length === input.length);
});

test("Differentiator - DC removal (high-pass)", async () => {
  const pipeline = createDspPipeline();
  pipeline.Differentiator();

  // Signal with DC offset
  const input = new Float32Array([
    5.0,
    5.1,
    5.2,
    5.3,
    5.4,
    5.5, // Constant DC + linear trend
  ]);

  const result = await pipeline.process(input, {
    channels: 1,
    sampleRate: 1000,
  });

  // DC removed, only rate of change remains
  // Differences should be ~0.1 (except first)
  for (let i = 1; i < result.length; i++) {
    assert.ok(Math.abs(result[i] - 0.1) < 0.001);
  }
});

test("Differentiator - state persistence", async () => {
  const pipeline = createDspPipeline();
  pipeline.Differentiator();

  const input1 = new Float32Array([1.0, 2.0, 3.0]);
  await pipeline.process(input1, { channels: 1, sampleRate: 1000 });

  // Save state (previous sample = 3.0)
  const state = await pipeline.saveState();

  // Create new pipeline and restore
  const pipeline2 = createDspPipeline();
  pipeline2.Differentiator();
  await pipeline2.loadState(state);

  // Continue processing
  const input2 = new Float32Array([4.0, 5.0]);
  const result2 = await pipeline2.process(input2, {
    channels: 1,
    sampleRate: 1000,
  });

  // Should continue from prev=3.0
  assert.equal(result2[0], 1.0); // 4.0 - 3.0
  assert.equal(result2[1], 1.0); // 5.0 - 4.0
});

test("Differentiator - reset state", async () => {
  const pipeline = createDspPipeline();
  pipeline.Differentiator();

  const input1 = new Float32Array([5.0, 10.0]);
  await pipeline.process(input1, { channels: 1, sampleRate: 1000 });

  // Reset
  pipeline.clearState();

  // Process new data (should start from 0 again)
  const input2 = new Float32Array([3.0, 6.0]);
  const result2 = await pipeline.process(input2, {
    channels: 1,
    sampleRate: 1000,
  });

  assert.equal(result2[0], 3.0); // 3.0 - 0.0 (reset)
  assert.equal(result2[1], 3.0); // 6.0 - 3.0
});
