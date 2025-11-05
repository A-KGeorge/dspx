import { test } from "node:test";
import assert from "node:assert";
import { createDspPipeline } from "../bindings.js";

test("Integrator - step response", async () => {
  const pipeline = createDspPipeline();
  pipeline.Integrator({ alpha: 0.9 });

  // Unit step input: [1, 1, 1, 1, 1]
  const input = new Float32Array([1, 1, 1, 1, 1]);
  const result = await pipeline.process(input, {
    channels: 1,
    sampleRate: 1000,
  });

  // Expected output: y[n] = x[n] + 0.9 * y[n-1]
  // y[0] = 1 + 0.9*0 = 1.0
  // y[1] = 1 + 0.9*1.0 = 1.9
  // y[2] = 1 + 0.9*1.9 = 2.71
  // y[3] = 1 + 0.9*2.71 = 3.439
  // y[4] = 1 + 0.9*3.439 = 4.0951
  assert(Math.abs(result[0] - 1.0) < 0.01, `Expected ~1.0, got ${result[0]}`);
  assert(Math.abs(result[1] - 1.9) < 0.01, `Expected ~1.9, got ${result[1]}`);
  assert(Math.abs(result[2] - 2.71) < 0.01, `Expected ~2.71, got ${result[2]}`);
  assert(
    Math.abs(result[3] - 3.439) < 0.01,
    `Expected ~3.439, got ${result[3]}`
  );
  assert(
    Math.abs(result[4] - 4.0951) < 0.01,
    `Expected ~4.0951, got ${result[4]}`
  );
});

test("Integrator - DC gain validation", async () => {
  const pipeline = createDspPipeline();
  pipeline.Integrator({ alpha: 0.9 });

  // Feed constant input long enough to reach steady state
  const input = new Float32Array(100).fill(1.0);
  const result = await pipeline.process(input, {
    channels: 1,
    sampleRate: 1000,
  });

  // DC gain = 1/(1-α) = 1/(1-0.9) = 10
  // After many samples, output should converge to ~10
  const steadyState = result[result.length - 1];
  assert(
    Math.abs(steadyState - 10.0) < 0.1,
    `DC gain should be ~10.0, got ${steadyState}`
  );
});

test("Integrator - perfect integration (alpha=1.0)", async () => {
  const pipeline = createDspPipeline();
  pipeline.Integrator({ alpha: 1.0 });

  // Constant input: [2, 2, 2, 2, 2]
  const input = new Float32Array([2, 2, 2, 2, 2]);
  const result = await pipeline.process(input, {
    channels: 1,
    sampleRate: 1000,
  });

  // Perfect integration (no leakage): y[n] = y[n-1] + x[n]
  // Expected: [2, 4, 6, 8, 10]
  assert.equal(result[0], 2);
  assert.equal(result[1], 4);
  assert.equal(result[2], 6);
  assert.equal(result[3], 8);
  assert.equal(result[4], 10);
});

test("Integrator - impulse response", async () => {
  const pipeline = createDspPipeline();
  pipeline.Integrator({ alpha: 0.8 });

  // Impulse at n=0: [1, 0, 0, 0, 0]
  const input = new Float32Array([1, 0, 0, 0, 0]);
  const result = await pipeline.process(input, {
    channels: 1,
    sampleRate: 1000,
  });

  // Impulse response: y[n] = α^n for n > 0
  // y[0] = 1, y[1] = 0.8, y[2] = 0.64, y[3] = 0.512, y[4] = 0.4096
  assert(Math.abs(result[0] - 1.0) < 0.01);
  assert(Math.abs(result[1] - 0.8) < 0.01);
  assert(Math.abs(result[2] - 0.64) < 0.01);
  assert(Math.abs(result[3] - 0.512) < 0.01);
  assert(Math.abs(result[4] - 0.4096) < 0.01);
});

test("Integrator - negative values", async () => {
  const pipeline = createDspPipeline();
  pipeline.Integrator({ alpha: 0.9 });

  const input = new Float32Array([-1, -1, -1, -1, -1]);
  const result = await pipeline.process(input, {
    channels: 1,
    sampleRate: 1000,
  });

  // Should integrate negative values correctly
  // y[0] = -1, y[1] = -1 + 0.9*(-1) = -1.9, etc.
  assert(result[0] < 0, "First output should be negative");
  assert(
    result[1] < result[0],
    "Output should decrease (become more negative)"
  );
  assert(
    result[result.length - 1] < -4,
    `Final value should approach -10, got ${result[result.length - 1]}`
  );
});

test("Integrator - multi-channel", async () => {
  const pipeline = createDspPipeline();
  pipeline.Integrator({ alpha: 0.9 });

  // 2 channels, 3 samples each: [ch0, ch1, ch0, ch1, ch0, ch1]
  const input = new Float32Array([1, 2, 1, 2, 1, 2]);
  const result = await pipeline.process(input, {
    channels: 2,
    sampleRate: 1000,
  });

  // Channel 0: [1, 1, 1] → [1.0, 1.9, 2.71]
  // Channel 1: [2, 2, 2] → [2.0, 3.8, 5.42]
  assert(Math.abs(result[0] - 1.0) < 0.01, `Ch0 sample 0: expected ~1.0`);
  assert(Math.abs(result[1] - 2.0) < 0.01, `Ch1 sample 0: expected ~2.0`);
  assert(Math.abs(result[2] - 1.9) < 0.01, `Ch0 sample 1: expected ~1.9`);
  assert(Math.abs(result[3] - 3.8) < 0.01, `Ch1 sample 1: expected ~3.8`);
});

test("Integrator - state continuity", async () => {
  const pipeline = createDspPipeline();
  pipeline.Integrator({ alpha: 0.9 });

  // First batch: [1, 1]
  const input1 = new Float32Array([1, 1]);
  const result1 = await pipeline.process(input1, {
    channels: 1,
    sampleRate: 1000,
  });

  // result1 should be [1.0, 1.9]
  assert(Math.abs(result1[1] - 1.9) < 0.01);

  // Second batch: [1] (should continue from y[-1] = 1.9)
  const input2 = new Float32Array([1]);
  const result2 = await pipeline.process(input2, {
    channels: 1,
    sampleRate: 1000,
  });

  // y[2] = 1 + 0.9*1.9 = 2.71
  assert(
    Math.abs(result2[0] - 2.71) < 0.01,
    `Expected ~2.71, got ${result2[0]}`
  );
});

test("Integrator - state persistence", async () => {
  const pipeline = createDspPipeline();
  pipeline.Integrator({ alpha: 0.9 });

  // Process [1, 1] → state = y[-1] = 1.9
  await pipeline.process(new Float32Array([1, 1]), {
    channels: 1,
    sampleRate: 1000,
  });

  const state = await pipeline.saveState();

  // New pipeline with restored state
  const pipeline2 = createDspPipeline();
  pipeline2.Integrator({ alpha: 0.9 });
  await pipeline2.loadState(state);

  // Process [1] should continue from y[-1] = 1.9
  const result = await pipeline2.process(new Float32Array([1]), {
    channels: 1,
    sampleRate: 1000,
  });

  assert(Math.abs(result[0] - 2.71) < 0.01, `Expected ~2.71, got ${result[0]}`);
});

test("Integrator - reset clears state", async () => {
  const pipeline = createDspPipeline();
  pipeline.Integrator({ alpha: 0.9 });

  // Build up state
  await pipeline.process(new Float32Array([1, 1, 1, 1, 1]), {
    channels: 1,
    sampleRate: 1000,
  });

  // Reset
  await pipeline.clearState();

  // Process new input - should start from 0
  const result = await pipeline.process(new Float32Array([1, 1]), {
    channels: 1,
    sampleRate: 1000,
  });

  assert(
    Math.abs(result[0] - 1.0) < 0.01,
    "First output after reset should be ~1.0"
  );
  assert(
    Math.abs(result[1] - 1.9) < 0.01,
    "Second output after reset should be ~1.9"
  );
});

test("Integrator - validation errors", async () => {
  const pipeline = createDspPipeline();

  // Alpha too small
  assert.throws(
    () => pipeline.Integrator({ alpha: 0.0 }),
    /alpha must be in range \(0, 1\]/,
    "Should reject alpha = 0"
  );

  // Alpha negative
  assert.throws(
    () => pipeline.Integrator({ alpha: -0.5 }),
    /alpha must be in range \(0, 1\]/,
    "Should reject negative alpha"
  );

  // Alpha too large
  assert.throws(
    () => pipeline.Integrator({ alpha: 1.1 }),
    /alpha must be in range \(0, 1\]/,
    "Should reject alpha > 1"
  );
});

test("Integrator - chain with rectify for envelope", async () => {
  const pipeline = createDspPipeline();
  pipeline.Rectify({ mode: "full" }).Integrator({ alpha: 0.95 });

  // Oscillating signal: [-1, 1, -1, 1, -1, 1]
  const input = new Float32Array([-1, 1, -1, 1, -1, 1]);
  const result = await pipeline.process(input, {
    channels: 1,
    sampleRate: 1000,
  });

  // After rectification: [1, 1, 1, 1, 1, 1]
  // All outputs should be positive and increasing
  for (let i = 0; i < result.length; i++) {
    assert(result[i] > 0, `Output[${i}] should be positive`);
    if (i > 0) {
      assert(
        result[i] > result[i - 1],
        `Output should be monotonically increasing`
      );
    }
  }
});

test("Integrator - accelerometer to velocity simulation", async () => {
  const pipeline = createDspPipeline();
  pipeline.Integrator({ alpha: 0.99 }); // Slight leakage prevents drift

  // Constant acceleration of 1 m/s² for 10 samples
  const acceleration = new Float32Array(10).fill(1.0);
  const velocity = await pipeline.process(acceleration, {
    channels: 1,
    sampleRate: 100, // 100 Hz
  });

  // Velocity should increase approximately linearly
  // (not perfectly linear due to 0.99 leakage)
  assert(velocity[0] > 0);
  assert(velocity[9] > velocity[0]);
  assert(velocity[5] > velocity[2], "Velocity should increase over time");
});

test("Integrator - default alpha", async () => {
  const pipeline = createDspPipeline();
  pipeline.Integrator(); // Should default to alpha = 0.99

  const input = new Float32Array([1, 1]);
  const result = await pipeline.process(input, {
    channels: 1,
    sampleRate: 1000,
  });

  // y[0] = 1, y[1] = 1 + 0.99*1 = 1.99
  assert(Math.abs(result[0] - 1.0) < 0.01);
  assert(Math.abs(result[1] - 1.99) < 0.01);
});

test("Integrator - time constant relationship", async () => {
  // Time constant τ = 1/(1-α)
  // For α = 0.9, τ = 10 samples

  const pipeline = createDspPipeline();
  pipeline.Integrator({ alpha: 0.9 });

  // Feed unit step for many samples
  const input = new Float32Array(50).fill(1.0);
  const result = await pipeline.process(input, {
    channels: 1,
    sampleRate: 1000,
  });

  // After ~5τ (50 samples), should be ~99% of steady state (10.0)
  const finalValue = result[result.length - 1];
  assert(
    finalValue > 9.9,
    `After 5τ, output should be ~99% of DC gain (10.0), got ${finalValue}`
  );
});
