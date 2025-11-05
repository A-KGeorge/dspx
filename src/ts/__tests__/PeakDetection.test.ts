import { test } from "node:test";
import assert from "node:assert";
import { createDspPipeline } from "../bindings.js";

test("PeakDetection - basic peak detection", async () => {
  const pipeline = createDspPipeline();
  pipeline.PeakDetection({ threshold: 0.5 });

  // Signal with clear peaks at indices 2 and 5
  const input = new Float32Array([0.2, 0.5, 0.8, 0.6, 0.7, 0.9, 0.4]);

  const result = await pipeline.process(input, {
    channels: 1,
    sampleRate: 1000,
  });

  // Expected peaks: 0.8 at index 2, 0.9 at index 5
  assert.equal(result[0], 0.0);
  assert.equal(result[1], 0.0);
  assert.equal(result[2], 1.0); // Peak: 0.5 < 0.8 > 0.6
  assert.equal(result[3], 0.0);
  assert.equal(result[4], 0.0);
  assert.equal(result[5], 1.0); // Peak: 0.7 < 0.9 > 0.4
  assert.equal(result[6], 0.0);
});

test("PeakDetection - no peaks", async () => {
  const pipeline = createDspPipeline();
  pipeline.PeakDetection({ threshold: 0.5 });

  // Monotonic increasing signal (no local maxima)
  const input = new Float32Array([0.1, 0.2, 0.3, 0.4, 0.5]);

  const result = await pipeline.process(input, {
    channels: 1,
    sampleRate: 1000,
  });

  // No peaks
  for (let i = 0; i < result.length; i++) {
    assert.equal(result[i], 0.0);
  }
});

test("PeakDetection - peaks below threshold ignored", async () => {
  const pipeline = createDspPipeline();
  pipeline.PeakDetection({ threshold: 0.8 });

  // Peak at 0.6 is below threshold
  const input = new Float32Array([0.2, 0.4, 0.6, 0.5, 0.3]);

  const result = await pipeline.process(input, {
    channels: 1,
    sampleRate: 1000,
  });

  // Peak at 0.6 ignored because 0.6 < 0.8
  for (let i = 0; i < result.length; i++) {
    assert.equal(result[i], 0.0);
  }
});

test("PeakDetection - zero threshold (all peaks)", async () => {
  const pipeline = createDspPipeline();
  pipeline.PeakDetection({ threshold: 0.0 });

  const input = new Float32Array([0.1, 0.3, 0.2, 0.4, 0.1]);

  const result = await pipeline.process(input, {
    channels: 1,
    sampleRate: 1000,
  });

  // Peaks at 0.3 (index 1) and 0.4 (index 3)
  assert.equal(result[0], 0.0);
  assert.equal(result[1], 1.0); // 0.1 < 0.3 > 0.2
  assert.equal(result[2], 0.0);
  assert.equal(result[3], 1.0); // 0.2 < 0.4 > 0.1
  assert.equal(result[4], 0.0);
});

test("PeakDetection - multi-channel", async () => {
  const pipeline = createDspPipeline();
  pipeline.PeakDetection({ threshold: 0.5 });

  // 2 channels, 4 samples
  const input = new Float32Array([
    0.3,
    0.2, // Sample 0
    0.6,
    0.5, // Sample 1
    0.4,
    0.8, // Sample 2
    0.2,
    0.6, // Sample 3
  ]);

  const result = await pipeline.process(input, {
    channels: 2,
    sampleRate: 1000,
  });

  // Ch0: 0.3, 0.6, 0.4, 0.2 -> peak at index 1 (0.6)
  // Ch1: 0.2, 0.5, 0.8, 0.6 -> peak at index 2 (0.8)
  assert.equal(result[0], 0.0); // ch0: 0.3
  assert.equal(result[1], 0.0); // ch1: 0.2
  assert.equal(result[2], 1.0); // ch0: 0.6 (peak)
  assert.equal(result[3], 0.0); // ch1: 0.5
  assert.equal(result[4], 0.0); // ch0: 0.4
  assert.equal(result[5], 1.0); // ch1: 0.8 (peak)
  assert.equal(result[6], 0.0); // ch0: 0.2
  assert.equal(result[7], 0.0); // ch1: 0.6
});

test("PeakDetection - ECG R-peak detection", async () => {
  const pipeline = createDspPipeline();
  pipeline.PeakDetection({ threshold: 0.7 });

  // Simulated ECG with R-peaks
  const input = new Float32Array([
    0.1, 0.2, 0.3, 0.9, 0.5, 0.2, 0.1, 0.2, 0.3, 0.85, 0.4, 0.2,
  ]);

  const result = await pipeline.process(input, {
    channels: 1,
    sampleRate: 1000,
  });

  // Count peaks
  let peakCount = 0;
  for (let i = 0; i < result.length; i++) {
    if (result[i] === 1.0) peakCount++;
  }

  assert.equal(peakCount, 2); // Two R-peaks detected
  assert.equal(result[3], 1.0); // First peak at 0.9
  assert.equal(result[9], 1.0); // Second peak at 0.85
});

test("PeakDetection - validation errors", () => {
  const pipeline = createDspPipeline();

  // Missing threshold
  assert.throws(() => {
    pipeline.PeakDetection({ threshold: undefined as any });
  }, /threshold must be >= 0/);

  // Negative threshold
  assert.throws(() => {
    pipeline.PeakDetection({ threshold: -0.5 });
  }, /threshold must be >= 0/);
});

test("PeakDetection - state continuity across batches", async () => {
  const pipeline = createDspPipeline();
  pipeline.PeakDetection({ threshold: 0.5 });

  // First batch ends with rising edge
  const input1 = new Float32Array([0.3, 0.6]);
  await pipeline.process(input1, { channels: 1, sampleRate: 1000 });

  // Second batch starts with peak and falling edge
  const input2 = new Float32Array([0.8, 0.4]);
  const result2 = await pipeline.process(input2, {
    channels: 1,
    sampleRate: 1000,
  });

  // Peak detected at 0.8 (continuing from 0.6)
  assert.equal(result2[0], 1.0); // 0.6 < 0.8 > 0.4
  assert.equal(result2[1], 0.0);
});

test("PeakDetection - chain with rectify", async () => {
  const pipeline = createDspPipeline();

  // Rectify first, then find peaks
  pipeline.Rectify({ mode: "full" }).PeakDetection({ threshold: 0.5 });

  const input = new Float32Array([0.3, -0.7, -0.2, 0.6, -0.8, -0.4]);

  const result = await pipeline.process(input, {
    channels: 1,
    sampleRate: 1000,
  });

  // After rectification: [0.3, 0.7, 0.2, 0.6, 0.8, 0.4]
  // Peaks: 0.7 (index 1), 0.8 (index 4)
  assert.equal(result[0], 0.0);
  assert.equal(result[1], 1.0); // Peak at 0.7
  assert.equal(result[2], 0.0);
  assert.equal(result[3], 0.0);
  assert.equal(result[4], 1.0); // Peak at 0.8
  assert.equal(result[5], 0.0);
});

test("PeakDetection - state persistence", async () => {
  const pipeline = createDspPipeline();
  pipeline.PeakDetection({ threshold: 0.7 });

  const input1 = new Float32Array([0.5, 0.8]);
  await pipeline.process(input1, { channels: 1, sampleRate: 1000 });

  // Save state (with history: prev=0.8, prevPrev=0.5)
  const state = await pipeline.saveState();

  // Create new pipeline and restore
  const pipeline2 = createDspPipeline();
  pipeline2.PeakDetection({ threshold: 0.5 });
  await pipeline2.loadState(state);

  // Continue processing - should detect peak at 0.8
  const input2 = new Float32Array([0.6]);
  const result2 = await pipeline2.process(input2, {
    channels: 1,
    sampleRate: 1000,
  });

  assert.equal(result2[0], 1.0); // 0.5 < 0.8 > 0.6 (peak detected with threshold 0.7)
});
