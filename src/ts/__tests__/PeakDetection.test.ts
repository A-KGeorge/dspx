import { test } from "node:test";
import assert from "node:assert";
import { createDspPipeline } from "../bindings.js";

// ============================================================================
// TIME DOMAIN TESTS - MOVING MODE
// ============================================================================

test("PeakDetection - time domain, moving mode (default)", async () => {
  const pipeline = createDspPipeline();
  pipeline.PeakDetection({ threshold: 0.5, mode: "moving", domain: "time" });

  // Signal with clear peaks at indices 2 and 5
  const input = new Float32Array([0.2, 0.5, 0.8, 0.6, 0.7, 0.9, 0.4]);

  const result = await pipeline.process(input, {
    channels: 1,
    sampleRate: 1000,
  });

  // Peak at index 2: 0.5 < 0.8 > 0.6
  assert.equal(result[2], 1.0, "Peak at 0.8");

  // Peak at index 5: 0.7 < 0.9 > 0.4
  assert.equal(result[5], 1.0, "Peak at 0.9");

  // Non-peaks
  assert.equal(result[0], 0.0);
  assert.equal(result[1], 0.0);
  assert.equal(result[3], 0.0);
  assert.equal(result[4], 0.0);
  assert.equal(result[6], 0.0); // Last sample never a peak
});

test("PeakDetection - time domain, batch mode", async () => {
  const pipeline = createDspPipeline();
  pipeline.PeakDetection({ threshold: 0.5, mode: "batch", domain: "time" });

  const input = new Float32Array([0.3, 0.6, 0.4, 0.5, 0.8, 0.3]);

  const result = await pipeline.process(input, {
    channels: 1,
    sampleRate: 1000,
  });

  assert.equal(result[1], 1.0); // Peak: 0.3 < 0.6 > 0.4
  assert.equal(result[4], 1.0); // Peak: 0.5 < 0.8 > 0.3
  assert.equal(result[0], 0.0); // First sample never peak
  assert.equal(result[5], 0.0); // Last sample never peak
});

test("PeakDetection - time domain, moving mode with state continuity", async () => {
  const pipeline = createDspPipeline();
  pipeline.PeakDetection({ threshold: 0.5, mode: "moving", domain: "time" });

  // First batch ends with rising edge
  const input1 = new Float32Array([0.3, 0.6]);
  await pipeline.process(input1, { channels: 1, sampleRate: 1000 });

  // Second batch: peak at 0.8, then falling
  const input2 = new Float32Array([0.8, 0.4]);
  const result2 = await pipeline.process(input2, {
    channels: 1,
    sampleRate: 1000,
  });

  assert.equal(result2[0], 1.0); // 0.6 < 0.8 > 0.4 (uses history)
  assert.equal(result2[1], 0.0);
});

// ============================================================================
// FREQUENCY DOMAIN TESTS
// ============================================================================

test("PeakDetection - frequency domain, batch mode", async () => {
  const pipeline = createDspPipeline();
  pipeline.PeakDetection({
    threshold: 0.3,
    mode: "batch",
    domain: "frequency",
  });

  // Simulated magnitude spectrum with peaks at bins 2, 5, 8
  const spectrum = new Float32Array([
    0.1, // bin 0
    0.2, // bin 1
    0.5, // bin 2 - PEAK
    0.3, // bin 3
    0.4, // bin 4
    0.6, // bin 5 - PEAK
    0.2, // bin 6
    0.3, // bin 7
    0.7, // bin 8 - PEAK
    0.4, // bin 9
  ]);

  const result = await pipeline.process(spectrum, {
    channels: 1,
    sampleRate: 1000,
  });

  assert.equal(result[2], 1.0); // Peak at 0.5
  assert.equal(result[5], 1.0); // Peak at 0.6
  assert.equal(result[8], 1.0); // Peak at 0.7
  assert.equal(result[0], 0.0);
  assert.equal(result[1], 0.0);
  assert.equal(result[3], 0.0);
  assert.equal(result[4], 0.0);
});

test("PeakDetection - frequency domain with minPeakDistance", async () => {
  const pipeline = createDspPipeline();
  pipeline.PeakDetection({
    threshold: 0.3,
    domain: "frequency",
    minPeakDistance: 3, // Peaks must be >= 3 bins apart
  });

  // Two close peaks at bins 2 and 4
  const spectrum = new Float32Array([
    0.1, // bin 0
    0.2, // bin 1
    0.5, // bin 2 - PEAK (lower)
    0.3, // bin 3
    0.6, // bin 4 - PEAK (higher, should win)
    0.2, // bin 5
  ]);

  const result = await pipeline.process(spectrum, {
    channels: 1,
    sampleRate: 1000,
  });

  // Only the higher peak (bin 4) should be detected
  assert.equal(result[2], 0.0); // Suppressed (too close to bin 4)
  assert.equal(result[4], 1.0); // Kept (higher peak)
});

test("PeakDetection - frequency domain, multi-channel", async () => {
  const pipeline = createDspPipeline();
  pipeline.PeakDetection({ threshold: 0.4, domain: "frequency" });

  // 2 channels, 5 bins each
  const spectrum = new Float32Array([
    // bin 0: ch0=0.2, ch1=0.3
    0.2, 0.3,
    // bin 1: ch0=0.5, ch1=0.3
    0.5, 0.3,
    // bin 2: ch0=0.3, ch1=0.5
    0.3, 0.5,
    // bin 3: ch0=0.6, ch1=0.3
    0.6, 0.3,
    // bin 4: ch0=0.2, ch1=0.2
    0.2, 0.2,
  ]);

  const result = await pipeline.process(spectrum, {
    channels: 2,
    sampleRate: 1000,
  });

  // Ch0 peaks
  assert.equal(result[1 * 2 + 0], 1.0); // bin 1, ch0
  assert.equal(result[3 * 2 + 0], 1.0); // bin 3, ch0

  // Ch1 peak
  assert.equal(result[2 * 2 + 1], 1.0); // bin 2, ch1

  // Non-peaks
  assert.equal(result[0 * 2 + 0], 0.0);
  assert.equal(result[2 * 2 + 0], 0.0);
});

// ============================================================================
// PIPELINE INTEGRATION TESTS
// ============================================================================

test("PeakDetection - after FFT (spectral peaks)", async () => {
  const pipeline = createDspPipeline();
  pipeline
    .fft({ size: 16, type: "rfft", output: "magnitude" })
    .PeakDetection({ threshold: 0.1, domain: "frequency" });

  // 50 Hz sine wave at 1 kHz sample rate
  const sampleRate = 1000;
  const freq = 50;
  const signal = new Float32Array(16);
  for (let i = 0; i < 16; i++) {
    signal[i] = Math.sin((2 * Math.PI * freq * i) / sampleRate);
  }

  const result = await pipeline.process(signal, {
    channels: 1,
    sampleRate: 1000,
  });

  // Should detect peak near 50 Hz bin
  const peakCount = result.filter((v) => v === 1.0).length;
  assert.ok(peakCount > 0, "Should detect at least one spectral peak");
  assert.ok(peakCount <= 3, "Should not detect too many false peaks");
});

test("PeakDetection - ECG R-peak detection pipeline", async () => {
  const pipeline = createDspPipeline();
  pipeline
    // .filter({ //TODO: enable to simulate realistic ECG preprocessing
    //   type: "butterworth",
    //   mode: "bandpass",
    //   lowCutoffFrequency: 5,
    //   highCutoffFrequency: 15,
    //   sampleRate: 360,
    //   order: 2,
    // })
    .Rectify({ mode: "full" })
    .PeakDetection({ threshold: 0.6, mode: "moving", domain: "time" });

  // Simulated ECG with 2 R-peaks
  const ecg = new Float32Array([
    // First R-peak
    0.1, 0.3, 0.9, 0.5, 0.2, 0.1,
    // Second R-peak
    0.1, 0.3, 0.85, 0.4, 0.1, 0.0,
  ]);

  const result = await pipeline.process(ecg, {
    channels: 1,
    sampleRate: 360,
  });

  console.log(result);

  // Count detected R-peaks
  const peakCount = result.filter((v) => v === 1.0).length;
  assert.equal(peakCount, 2, "Should detect 2 R-peaks");
});

// ============================================================================
// ERROR HANDLING
// ============================================================================

test("PeakDetection - validation errors", () => {
  const pipeline = createDspPipeline();

  // Missing threshold
  assert.throws(
    () => {
      pipeline.PeakDetection({ threshold: undefined as any });
    },
    /threshold must be >= 0/,
    "Missing threshold"
  );

  // Negative threshold
  assert.throws(
    () => {
      pipeline.PeakDetection({ threshold: -0.5 });
    },
    /threshold must be >= 0/,
    "Negative threshold"
  );

  // Invalid mode
  assert.throws(
    () => {
      pipeline.PeakDetection({
        threshold: 0.5,
        mode: "invalid" as any,
      });
    },
    /mode must be 'batch' or 'moving'/,
    "Invalid mode"
  );

  // Invalid domain
  assert.throws(
    () => {
      pipeline.PeakDetection({
        threshold: 0.5,
        domain: "invalid" as any,
      });
    },
    /domain must be 'time' or 'frequency'/,
    "Invalid domain"
  );
});

test("PeakDetection - state persistence", async () => {
  const pipeline = createDspPipeline();
  pipeline.PeakDetection({ threshold: 0.7, mode: "moving", domain: "time" });

  const input1 = new Float32Array([0.5, 0.8]);
  await pipeline.process(input1, { channels: 1, sampleRate: 1000 });

  // Save state (with history: buffer contains [0.5, 0.8])
  const state = await pipeline.saveState();

  // Create new pipeline and restore
  const pipeline2 = createDspPipeline();
  pipeline2.PeakDetection({ threshold: 0.5, mode: "moving", domain: "time" });
  await pipeline2.loadState(state);

  // Continue processing - should detect peak at 0.8
  const input2 = new Float32Array([0.6]);
  const result2 = await pipeline2.process(input2, {
    channels: 1,
    sampleRate: 1000,
  });

  assert.equal(result2[0], 1.0); // 0.5 < 0.8 > 0.6 (peak detected with threshold 0.7)
});

test("PeakDetection - multi-channel with state continuity", async () => {
  const pipeline = createDspPipeline();
  pipeline.PeakDetection({ threshold: 0.4, mode: "moving", domain: "time" });

  // 2 channels, 5 samples per channel
  // Ch0: [0.3, 0.6, 0.4, 0.7, 0.3] - peaks at indices 1, 3
  // Ch1: [0.2, 0.3, 0.5, 0.3, 0.2] - peak at index 2
  const input = new Float32Array([
    0.3,
    0.2, // Sample 0
    0.6,
    0.3, // Sample 1
    0.4,
    0.5, // Sample 2
    0.7,
    0.3, // Sample 3
    0.3,
    0.2, // Sample 4
  ]);

  const result = await pipeline.process(input, {
    channels: 2,
    sampleRate: 1000,
  });

  // Ch0 peaks
  assert.equal(result[1 * 2 + 0], 1.0, "Ch0 peak at 0.6");
  assert.equal(result[3 * 2 + 0], 1.0, "Ch0 peak at 0.7");

  // Ch1 peak
  assert.equal(result[2 * 2 + 1], 1.0, "Ch1 peak at 0.5");

  // Non-peaks
  assert.equal(result[0 * 2 + 0], 0.0);
  assert.equal(result[2 * 2 + 0], 0.0);
  assert.equal(result[4 * 2 + 1], 0.0); // Last sample
});

test("PeakDetection - batch mode with no state", async () => {
  const pipeline = createDspPipeline();
  pipeline.PeakDetection({ threshold: 0.5, mode: "batch", domain: "time" });

  // First batch
  const input1 = new Float32Array([0.3, 0.6, 0.4]);
  const result1 = await pipeline.process(input1, {
    channels: 1,
    sampleRate: 1000,
  });

  assert.equal(result1[1], 1.0, "Peak at 0.6 in first batch");

  // Second batch - should NOT use state from first batch
  const input2 = new Float32Array([0.7, 0.5]);
  const result2 = await pipeline.process(input2, {
    channels: 1,
    sampleRate: 1000,
  });

  // No peak detected (would need context from previous batch, but batch mode is stateless)
  assert.equal(result2[0], 0.0, "Batch mode: no cross-batch state");
  assert.equal(result2[1], 0.0);
});

test("PeakDetection - chain with rectify", async () => {
  const pipeline = createDspPipeline();

  // Rectify first, then find peaks
  pipeline
    .Rectify({ mode: "full" })
    .PeakDetection({ threshold: 0.5, mode: "moving", domain: "time" });

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

test("PeakDetection - edge cases", async () => {
  const pipeline = createDspPipeline();
  pipeline.PeakDetection({ threshold: 0.5, mode: "moving", domain: "time" });

  // Empty input
  const empty = new Float32Array([]);
  const result1 = await pipeline.process(empty, {
    channels: 1,
    sampleRate: 1000,
  });
  assert.equal(result1.length, 0, "Empty input produces empty output");

  // Single sample (not enough for peak detection)
  const single = new Float32Array([0.9]);
  const result2 = await pipeline.process(single, {
    channels: 1,
    sampleRate: 1000,
  });
  assert.equal(result2[0], 0.0, "Single sample is not a peak");

  // Two samples (still not enough for 3-point comparison)
  const two = new Float32Array([0.5, 0.8]);
  const result3 = await pipeline.process(two, {
    channels: 1,
    sampleRate: 1000,
  });
  assert.equal(result3[0], 0.0, "Two samples: no peak yet");
  assert.equal(result3[1], 0.0);
});

test("PeakDetection - zero threshold detects all peaks", async () => {
  const pipeline = createDspPipeline();
  pipeline.PeakDetection({ threshold: 0.0, mode: "batch", domain: "time" });

  const input = new Float32Array([0.1, 0.3, 0.2, 0.4, 0.1]);

  const result = await pipeline.process(input, {
    channels: 1,
    sampleRate: 1000,
  });

  // Peaks at 0.3 (index 1) and 0.4 (index 3)
  assert.equal(result[1], 1.0, "Peak at 0.3 with zero threshold");
  assert.equal(result[3], 1.0, "Peak at 0.4 with zero threshold");
});

test("PeakDetection - monotonic signal has no peaks", async () => {
  const pipeline = createDspPipeline();
  pipeline.PeakDetection({ threshold: 0.0, mode: "batch", domain: "time" });

  // Monotonically increasing
  const increasing = new Float32Array([0.1, 0.2, 0.3, 0.4, 0.5]);

  const result = await pipeline.process(increasing, {
    channels: 1,
    sampleRate: 1000,
  });

  // No local maxima
  for (let i = 0; i < result.length; i++) {
    assert.equal(result[i], 0.0, `No peaks in monotonic signal at ${i}`);
  }
});

test("PeakDetection - clearState resets sliding window", async () => {
  const pipeline = createDspPipeline();
  pipeline.PeakDetection({ threshold: 0.5, mode: "moving", domain: "time" });

  // Build up history: [0.3, 0.6]
  await pipeline.process(new Float32Array([0.3, 0.6]), {
    channels: 1,
    sampleRate: 1000,
  });

  // Clear state
  pipeline.clearState();

  // After clear, window is empty. Process [0.8, 0.4]
  const result = await pipeline.process(new Float32Array([0.8, 0.4]), {
    channels: 1,
    sampleRate: 1000,
  });

  // No peak detected because history was cleared
  assert.equal(result[0], 0.0, "No peak after clearState");
  assert.equal(result[1], 0.0);
});
