/**
 * FFT Moving Mode Demonstration
 *
 * This example demonstrates the new `mode: 'moving'` parameter for FFT,
 * which provides sliding-window FFT (STFT) functionality with a cleaner API.
 *
 * Internally, `fft({ mode: 'moving' })` delegates to `stft()` for efficiency,
 * reusing the existing MovingFftFilter implementation with CircularBufferArray.
 *
 * Run: npm run build && node examples/fft-moving-mode-demo.cjs
 */

const { createDspPipeline } = require("../dist/index.js");
console.log("=== FFT Moving Mode Demo ===\n");

// Create test signal: 440 Hz sine wave sampled at 8 kHz
const sampleRate = 8000;
const frequency = 440; // A4 note
const duration = 1.0; // 1 second
const numSamples = Math.floor(sampleRate * duration);
const signal = new Float32Array(numSamples);

for (let i = 0; i < numSamples; i++) {
  signal[i] = Math.sin((2 * Math.PI * frequency * i) / sampleRate);
}

console.log(`Test Signal: ${frequency} Hz sine wave`);
console.log(`Sample Rate: ${sampleRate} Hz`);
console.log(`Duration: ${duration} s`);
console.log(`Samples: ${numSamples}\n`);

// ============================================================================
// BATCH MODE: Process entire buffer at once (stateless)
// ============================================================================
console.log("--- Batch Mode (Stateless) ---");

const batchPipeline = createDspPipeline(1, sampleRate);
batchPipeline.fft({
  mode: "batch",
  size: 1024,
  type: "rfft",
  output: "magnitude",
});

const batchResult = batchPipeline.process(signal);
const batchPeakBin = batchResult.indexOf(Math.max(...batchResult));
const batchPeakFreq = (batchPeakBin * sampleRate) / 1024;

console.log(`FFT Size: 1024`);
console.log(`Output Bins: ${batchResult.length}`);
console.log(`Peak Bin: ${batchPeakBin}`);
console.log(`Peak Frequency: ${batchPeakFreq.toFixed(1)} Hz`);
console.log(`Expected: ${frequency} Hz`);
console.log(`Error: ${Math.abs(batchPeakFreq - frequency).toFixed(1)} Hz\n`);

// ============================================================================
// MOVING MODE: Sliding-window FFT (stateful, delegates to STFT)
// ============================================================================
console.log("--- Moving Mode (Stateful STFT) ---");

const movingPipeline = createDspPipeline(1, sampleRate);
movingPipeline.fft({
  mode: "moving",
  size: 512,
  hopSize: 256, // 50% overlap
  type: "rfft",
  output: "magnitude",
});

const movingResult = movingPipeline.process(signal);

// Moving mode outputs: [window0_bins, window1_bins, window2_bins, ...]
// Each window has 257 bins (512/2 + 1 for real FFT)
const binsPerWindow = 512 / 2 + 1; // 257
const numWindows = movingResult.length / binsPerWindow;

console.log(`FFT Size: 512`);
console.log(`Hop Size: 256 (50% overlap)`);
console.log(`Bins per Window: ${binsPerWindow}`);
console.log(`Number of Windows: ${numWindows}`);

// Analyze first window
const firstWindow = movingResult.slice(0, binsPerWindow);
const firstPeakBin = firstWindow.indexOf(Math.max(...firstWindow));
const firstPeakFreq = (firstPeakBin * sampleRate) / 512;

console.log(`\nFirst Window:`);
console.log(`  Peak Bin: ${firstPeakBin}`);
console.log(`  Peak Frequency: ${firstPeakFreq.toFixed(1)} Hz`);
console.log(`  Expected: ${frequency} Hz`);
console.log(`  Error: ${Math.abs(firstPeakFreq - frequency).toFixed(1)} Hz`);

// Analyze last window
const lastWindow = movingResult.slice(-binsPerWindow);
const lastPeakBin = lastWindow.indexOf(Math.max(...lastWindow));
const lastPeakFreq = (lastPeakBin * sampleRate) / 512;

console.log(`\nLast Window:`);
console.log(`  Peak Bin: ${lastPeakBin}`);
console.log(`  Peak Frequency: ${lastPeakFreq.toFixed(1)} Hz`);
console.log(`  Expected: ${frequency} Hz`);
console.log(`  Error: ${Math.abs(lastPeakFreq - frequency).toFixed(1)} Hz\n`);

// ============================================================================
// COMPARISON: Show equivalence of fft({mode:'moving'}) and stft()
// ============================================================================
console.log("--- Equivalence Test: fft({mode:'moving'}) vs stft() ---");

const stftPipeline = createDspPipeline(1, sampleRate);
stftPipeline.stft({
  windowSize: 512,
  hopSize: 256,
  method: "fft",
  type: "real",
  forward: true,
  output: "magnitude",
  window: "hann",
});

const stftResult = stftPipeline.process(signal);

// Compare results
const maxDiff = movingResult.reduce(
  (max, val, i) => Math.max(max, Math.abs(val - stftResult[i])),
  0
);

const avgDiff =
  movingResult.reduce((sum, val, i) => sum + Math.abs(val - stftResult[i]), 0) /
  movingResult.length;

console.log(
  `Output Length Match: ${
    movingResult.length === stftResult.length ? "✓" : "✗"
  }`
);
console.log(`Max Difference: ${maxDiff.toExponential(2)}`);
console.log(`Avg Difference: ${avgDiff.toExponential(2)}`);
console.log(maxDiff < 1e-6 ? "✓ Results are identical!" : "✗ Results differ");

console.log("\n=== Demo Complete ===");
