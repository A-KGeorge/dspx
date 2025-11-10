import { test } from "node:test";
import assert from "node:assert";
import { createDspPipeline } from "../bindings.js";

test("Convolution - Basic Moving Mode (Direct)", async () => {
  const pipeline = createDspPipeline();

  // Simple 3-tap averaging kernel
  const kernel = new Float32Array([1 / 3, 1 / 3, 1 / 3]);
  pipeline.convolution({ kernel, mode: "moving", method: "direct" });

  const input = new Float32Array([1, 2, 3, 4, 5, 6]);
  const result = await pipeline.process(input, { channels: 1 });

  // Moving convolution should compute local averages
  assert(result.length === input.length, "Output size should match input");
  assert(result[2] !== undefined, "Should have result at position 2");
});

test("Convolution - Batch Mode with Small Kernel (Direct)", async () => {
  const pipeline = createDspPipeline();

  // Simple difference kernel
  const kernel = new Float32Array([1, -1]);
  pipeline.convolution({ kernel, mode: "batch", method: "direct" });

  const input = new Float32Array([1, 3, 2, 5, 4]);
  const result = await pipeline.process(input, { channels: 1 });

  // Batch mode computes VALID convolution: output length = N - M + 1 = 5 - 2 + 1 = 4
  assert(
    result.length === 4,
    "Batch mode should return valid convolution (N-M+1)"
  );
  // Valid convolution with kernel [1, -1]:
  // result[0] = 1*1 + (-1)*3 = 1 - 3 = -2
  // result[1] = 1*3 + (-1)*2 = 3 - 2 = 1
  // result[2] = 1*2 + (-1)*5 = 2 - 5 = -3
  // result[3] = 1*5 + (-1)*4 = 5 - 4 = 1
  assert(Math.abs(result[0] - -2) < 0.01, "First difference should be -2");
  assert(Math.abs(result[1] - 1) < 0.01, "Second difference should be 1");
});

test("Convolution - Gaussian Smoothing Kernel", async () => {
  const pipeline = createDspPipeline();

  // 5-tap Gaussian kernel (approximation)
  const kernel = new Float32Array([0.06, 0.24, 0.4, 0.24, 0.06]);
  pipeline.convolution({ kernel, mode: "moving" });

  const input = new Float32Array([0, 0, 10, 0, 0, 0, 0]);
  const result = await pipeline.process(input, { channels: 1 });

  // Causal convolution: peak appears at result[4] (kernel.length-1 + input_peak_index)
  // result[4] = 0.4*10 = 4.0 (center tap on the spike)
  // result[5] = 0.24*10 = 2.4 (spike has moved to second position)
  // result[6] = 0.06*10 = 0.6 (spike has moved to last position)

  assert(result.length === input.length, "Output size should match input");
  assert(
    Math.abs(result[4] - 4.0) < 0.01,
    "Peak should be at result[4] with value ~4.0"
  );
  assert(
    Math.abs(result[5] - 2.4) < 0.01,
    "Trailing value should be at result[5] with value ~2.4"
  );
  assert(result[6] > 0, "Should have spread to result[6]");
  assert(
    result[0] === 0 && result[1] === 0 && result[2] === 0 && result[3] === 0,
    "First 4 values should be 0 (waiting for buffer to fill)"
  );
});

test("Convolution - Large Kernel with FFT", async () => {
  const pipeline = createDspPipeline();

  // Large kernel (>64 elements) should trigger FFT method
  const kernelSize = 128;
  const kernel = new Float32Array(kernelSize);
  // Simple boxcar window (averaging filter)
  for (let i = 0; i < kernelSize; i++) {
    kernel[i] = 1 / kernelSize;
  }

  pipeline.convolution({ kernel, mode: "batch", method: "auto" });

  const inputSize = 512;
  const input = new Float32Array(inputSize);
  for (let i = 0; i < inputSize; i++) {
    input[i] = Math.sin((2 * Math.PI * i) / 50);
  }

  const result = await pipeline.process(input, { channels: 1 });

  // Batch mode: valid convolution output length = N - M + 1 = 512 - 128 + 1 = 385
  const expectedLength = inputSize - kernelSize + 1;
  assert(
    result.length === expectedLength,
    `Batch mode should return valid convolution (got ${result.length}, expected ${expectedLength})`
  );

  // A boxcar average of a sine wave should produce much smaller amplitudes
  // Check that output amplitude is reduced (averaging 128 samples of a 50-sample period wave)
  const testIdx = 128; // Well past the kernel length (use valid index for smaller output)
  // The boxcar will average over ~2.5 periods, so amplitude should be much less than 1.0
  assert(
    Math.abs(result[testIdx]) < 0.5,
    `Result should be smoothed (got ${result[testIdx]}, expected < 0.5)`
  );
});

test("Convolution - Force FFT Method", async () => {
  const pipeline = createDspPipeline();

  const kernel = new Float32Array([0.25, 0.5, 0.25]);
  pipeline.convolution({ kernel, mode: "batch", method: "fft" });

  const input = new Float32Array([1, 2, 3, 4, 5]);
  const result = await pipeline.process(input, { channels: 1 });

  // Batch mode: valid convolution output length = N - M + 1 = 5 - 3 + 1 = 3
  assert(
    result.length === 3,
    `FFT batch mode should return valid convolution (got ${result.length}, expected 3)`
  );
});

test("Convolution - Force Direct Method", async () => {
  const pipeline = createDspPipeline();

  const kernel = new Float32Array([0.25, 0.5, 0.25]);
  pipeline.convolution({ kernel, mode: "batch", method: "direct" });

  const input = new Float32Array([1, 2, 3, 4, 5]);
  const result = await pipeline.process(input, { channels: 1 });

  // Batch mode: valid convolution output length = N - M + 1 = 5 - 3 + 1 = 3
  assert(
    result.length === 3,
    `Direct batch mode should return valid convolution (got ${result.length}, expected 3)`
  );
});

test("Convolution - Multi-Channel Processing", async () => {
  const pipeline = createDspPipeline();

  // Simple smoothing kernel
  const kernel = new Float32Array([0.33, 0.34, 0.33]);
  pipeline.convolution({ kernel, mode: "moving" });

  // 2 channels, 6 samples per channel
  const input = new Float32Array([
    1,
    10, // Sample 0: ch0=1, ch1=10
    2,
    20, // Sample 1: ch0=2, ch1=20
    3,
    30, // Sample 2: ch0=3, ch1=30
    4,
    40, // Sample 3: ch0=4, ch1=40
    5,
    50, // Sample 4: ch0=5, ch1=50
    6,
    60, // Sample 5: ch0=6, ch1=60
  ]);

  const result = await pipeline.process(input, { channels: 2 });

  // Check interleaved output
  assert(
    result.length === input.length,
    "Multi-channel output size should match"
  );

  // With a 3-tap kernel, first 2 outputs (samples 0 and 1) will be 0 for both channels
  // Sample 2 onwards will have convolution results
  // result[4] is ch0 sample 2: should be 0.33*3 + 0.34*2 + 0.33*1 ≈ 2.0
  // result[5] is ch1 sample 2: should be 0.33*30 + 0.34*20 + 0.33*10 ≈ 20.0
  assert(
    result[0] === 0 && result[1] === 0,
    "First sample should be 0 (buffer not full)"
  );
  assert(
    result[2] === 0 && result[3] === 0,
    "Second sample should be 0 (buffer not full)"
  );
  assert(Math.abs(result[4] - 2.0) < 0.01, "Ch0 sample 2 should be ~2.0");
  assert(Math.abs(result[5] - 20.0) < 0.1, "Ch1 sample 2 should be ~20.0");
});

test("Convolution - Auto Method Selection", async () => {
  const pipeline = createDspPipeline();

  // Small kernel - should use direct method
  const smallKernel = new Float32Array([1, 1, 1]);
  pipeline.convolution({ kernel: smallKernel, mode: "batch", method: "auto" });

  const input1 = new Float32Array([1, 2, 3, 4, 5]);
  const result1 = await pipeline.process(input1, { channels: 1 });

  // Batch mode: valid convolution output length = N - M + 1 = 5 - 3 + 1 = 3
  assert(
    result1.length === 3,
    `Auto selection batch mode should return valid convolution (got ${result1.length}, expected 3)`
  );

  // Create new pipeline for large kernel
  const pipeline2 = createDspPipeline();

  // Large kernel - should use FFT method
  const largeKernel = new Float32Array(100);
  for (let i = 0; i < 100; i++) {
    largeKernel[i] = 1 / 100;
  }
  pipeline2.convolution({ kernel: largeKernel, mode: "batch", method: "auto" });

  const input2 = new Float32Array(200);
  for (let i = 0; i < 200; i++) {
    input2[i] = Math.random();
  }

  const result2 = await pipeline2.process(input2, { channels: 1 });

  // Batch mode: valid convolution output length = N - M + 1 = 200 - 100 + 1 = 101
  assert(
    result2.length === 101,
    `Auto selection with large kernel should return valid convolution (got ${result2.length}, expected 101)`
  );
});

test("Convolution - Custom Auto Threshold", async () => {
  const pipeline = createDspPipeline();

  // Set custom threshold for FFT selection
  const kernel = new Float32Array(50);
  for (let i = 0; i < 50; i++) {
    kernel[i] = 1 / 50;
  }

  // With threshold=30, kernel size 50 should trigger FFT
  pipeline.convolution({
    kernel,
    mode: "batch",
    method: "auto",
    autoThreshold: 30,
  });

  const input = new Float32Array(100);
  for (let i = 0; i < 100; i++) {
    input[i] = Math.sin((2 * Math.PI * i) / 20);
  }

  const result = await pipeline.process(input, { channels: 1 });

  // Batch mode: valid convolution output length = N - M + 1 = 100 - 50 + 1 = 51
  assert(
    result.length === 51,
    `Custom threshold batch mode should return valid convolution (got ${result.length}, expected 51)`
  );
});

test("Convolution - Edge Detector Kernel", async () => {
  const pipeline = createDspPipeline();

  // Simple edge detection kernel [-1, 0, 1]
  const kernel = new Float32Array([-1, 0, 1]);
  pipeline.convolution({ kernel, mode: "batch" });

  // Step function
  const input = new Float32Array([0, 0, 0, 1, 1, 1, 0, 0, 0]);
  const result = await pipeline.process(input, { channels: 1 });

  // Batch mode: valid convolution output length = N - M + 1 = 9 - 3 + 1 = 7
  assert(
    result.length === 7,
    `Edge detection batch mode should return valid convolution (got ${result.length}, expected 7)`
  );

  // Valid convolution with kernel [-1, 0, 1]:
  // result[0] = -1*0 + 0*0 + 1*0 = 0
  // result[1] = -1*0 + 0*0 + 1*1 = 1 (rising edge detected!)
  // result[2] = -1*0 + 0*1 + 1*1 = 1
  // result[3] = -1*1 + 0*1 + 1*1 = -1 (transition)
  // result[4] = -1*1 + 0*1 + 1*0 = -1 (falling edge detected!)
  assert(result[1] > 0.5, "Should detect rising edge at index 1");
  assert(result[4] < -0.5, "Should detect falling edge at index 4");
});

test("Convolution - Stateful Moving Mode", async () => {
  const pipeline = createDspPipeline();

  const kernel = new Float32Array([0.5, 0.5]);
  pipeline.convolution({ kernel, mode: "moving", method: "direct" });

  // Process in chunks
  const chunk1 = new Float32Array([1, 2, 3]);
  const result1 = await pipeline.process(chunk1, { channels: 1 });

  const chunk2 = new Float32Array([4, 5, 6]);
  const result2 = await pipeline.process(chunk2, { channels: 1 });

  assert(result1.length === 3, "First chunk should have 3 samples");
  assert(result2.length === 3, "Second chunk should have 3 samples");
  // State should be maintained between calls
});

test("Convolution - State Serialization (Moving Mode)", async () => {
  const pipeline1 = createDspPipeline();

  const kernel = new Float32Array([0.33, 0.34, 0.33]);
  pipeline1.convolution({ kernel, mode: "moving", method: "direct" });

  // Process some data
  const input1 = new Float32Array([1, 2, 3, 4, 5]);
  await pipeline1.process(input1, { channels: 1 });

  // Save state
  const state = await pipeline1.saveState();

  // Create new pipeline and restore state
  const pipeline2 = createDspPipeline();
  pipeline2.convolution({ kernel, mode: "moving", method: "direct" });
  pipeline2.loadState(state);

  // Continue processing
  const input2 = new Float32Array([6, 7, 8]);
  const result2 = await pipeline2.process(input2, { channels: 1 });

  assert(result2.length === 3, "Restored pipeline should continue processing");
});

test("Convolution - Empty Input Handling", async () => {
  const pipeline = createDspPipeline();

  const kernel = new Float32Array([1, 1, 1]);
  pipeline.convolution({ kernel, mode: "batch" });

  const emptyInput = new Float32Array([]);
  const result = await pipeline.process(emptyInput, { channels: 1 });

  assert(result.length === 0, "Empty input should produce empty output");
});

test("Convolution - Single Sample Input", async () => {
  const pipeline = createDspPipeline();

  const kernel = new Float32Array([1]);
  pipeline.convolution({ kernel, mode: "batch" });

  const input = new Float32Array([5]);
  const result = await pipeline.process(input, { channels: 1 });

  assert(result.length === 1, "Single sample should produce single output");
  assert(
    Math.abs(result[0] - 5) < 0.01,
    "Identity kernel should preserve value"
  );
});

test("Convolution - Validation: Empty Kernel", async () => {
  const pipeline = createDspPipeline();

  assert.throws(
    () => {
      pipeline.convolution({ kernel: new Float32Array([]) });
    },
    /kernel.*empty/i,
    "Should reject empty kernel"
  );
});

test("Convolution - Validation: Invalid Mode", async () => {
  const pipeline = createDspPipeline();

  const kernel = new Float32Array([1, 1, 1]);

  assert.throws(
    () => {
      // @ts-expect-error - Testing invalid mode
      pipeline.convolution({ kernel, mode: "invalid" });
    },
    /mode.*moving.*batch/i,
    "Should reject invalid mode"
  );
});

test("Convolution - Validation: Invalid Method", async () => {
  const pipeline = createDspPipeline();

  const kernel = new Float32Array([1, 1, 1]);

  assert.throws(
    () => {
      // @ts-expect-error - Testing invalid method
      pipeline.convolution({ kernel, method: "invalid" });
    },
    /method.*auto.*direct.*fft/i,
    "Should reject invalid method"
  );
});

test("Convolution - Default Parameters", async () => {
  const pipeline = createDspPipeline();

  const kernel = new Float32Array([0.5, 0.5]);
  // Test defaults: mode=moving, method=auto
  pipeline.convolution({ kernel });

  const input = new Float32Array([1, 2, 3, 4, 5]);
  const result = await pipeline.process(input, { channels: 1 });

  assert(result.length === input.length, "Default parameters should work");
});

test("Convolution - Chaining with Other Stages", async () => {
  const pipeline = createDspPipeline();

  // Chain: rectify → convolution smoothing → RMS
  pipeline.Rectify({ mode: "full" });
  pipeline.convolution({
    kernel: new Float32Array([0.33, 0.34, 0.33]),
    mode: "moving",
  });
  pipeline.Rms({ mode: "moving", windowSize: 5 });

  const input = new Float32Array([-1, 2, -3, 4, -5, 6, -7, 8]);
  const result = await pipeline.process(input, { channels: 1 });

  assert(result.length === input.length, "Chained pipeline should work");
  assert(
    result.every((x) => x >= 0),
    "RMS output should be non-negative"
  );
});

test("Convolution - Reset State", async () => {
  const pipeline = createDspPipeline();

  const kernel = new Float32Array([0.5, 0.5]);
  pipeline.convolution({ kernel, mode: "moving" });

  // Process some data
  await pipeline.process(new Float32Array([1, 2, 3]), { channels: 1 });

  // Reset
  pipeline.clearState();

  // Process again - should start fresh
  const result = await pipeline.process(new Float32Array([4, 5, 6]), {
    channels: 1,
  });

  assert(result.length === 3, "Reset should allow fresh processing");
});

test("Convolution - Array Kernel Input", async () => {
  const pipeline = createDspPipeline();

  // Test with regular array instead of Float32Array
  const kernel = [0.25, 0.5, 0.25];
  pipeline.convolution({ kernel });

  const input = new Float32Array([1, 2, 3, 4, 5]);
  const result = await pipeline.process(input, { channels: 1 });

  assert(
    result.length === input.length,
    "Array kernel should be converted to Float32Array"
  );
});

test("Convolution - Large Multi-Channel with FFT", async () => {
  const pipeline = createDspPipeline();

  // Large kernel for FFT
  const kernelSize = 100;
  const kernel = new Float32Array(kernelSize);
  for (let i = 0; i < kernelSize; i++) {
    kernel[i] = 1 / kernelSize;
  }

  pipeline.convolution({ kernel, mode: "batch", method: "fft" });

  // 4 channels, 200 samples per channel
  const numChannels = 4;
  const samplesPerChannel = 200;
  const input = new Float32Array(numChannels * samplesPerChannel);
  for (let i = 0; i < input.length; i++) {
    input[i] = Math.sin((2 * Math.PI * (i % samplesPerChannel)) / 50);
  }

  const result = await pipeline.process(input, { channels: numChannels });

  // Batch mode: valid convolution output length per channel = N - M + 1 = 200 - 100 + 1 = 101
  // Total output = 101 * 4 = 404
  const expectedLength = (samplesPerChannel - kernelSize + 1) * numChannels;
  assert(
    result.length === expectedLength,
    `Multi-channel FFT batch mode should work (got ${result.length}, expected ${expectedLength})`
  );
});
