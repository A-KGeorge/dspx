/**
 * Filter Bank Stage Example
 *
 * Demonstrates frequency decomposition using filter banks:
 * - Mel-scale filter bank for speech analysis
 * - Octave filter bank for musical analysis
 * - Custom filter bank with arbitrary coefficients
 * - Multi-channel processing with filter banks
 */

import { createDspPipeline, FilterBankDesign } from "../dist/index.js";

async function main() {
  console.log("=== Filter Bank Stage Examples ===\n");

// ============================================================
// Example 1: Mel-Scale Filter Bank for Speech Analysis
// ============================================================

console.log("--- Example 1: Mel-Scale Filter Bank (Speech) ---");

const sampleRate = 16000;

// Design a 24-band Mel-scale filter bank (common for speech recognition)
const melBank = FilterBankDesign.createMel(24, sampleRate, [100, 8000]);

console.log(`✅ Designed 24-band Mel-scale filter bank`);
console.log(`   Frequency range: 100-8000 Hz`);
console.log(`   Sample rate: ${sampleRate} Hz`);
console.log(
  `   Example band 0: ${melBank[0].b.length} b coeffs, ${melBank[0].a.length} a coeffs`
);

// Create pipeline with Mel filter bank
const melPipeline = createDspPipeline().FilterBank({
  definitions: melBank,
  inputChannels: 1, // Mono input
});

// Generate test signal: speech-like formants at 200, 800, 2500 Hz
const speechSignal = new Float32Array(8000); // 0.5 seconds
for (let i = 0; i < speechSignal.length; i++) {
  const t = i / sampleRate;
  speechSignal[i] =
    0.5 * Math.sin(2 * Math.PI * 200 * t) + // F1 (fundamental)
    0.3 * Math.sin(2 * Math.PI * 800 * t) + // F2
    0.2 * Math.sin(2 * Math.PI * 2500 * t); // F3
}

console.log(`\n📊 Processing speech signal through Mel filter bank...`);
const melOutput = await melPipeline.process(speechSignal, {
  sampleRate,
  channels: 1,
});

console.log(`   Input: 1 channel × ${speechSignal.length} samples`);
console.log(
  `   Output: 24 channels × ${melOutput.length / 24} samples (interleaved)`
);
console.log(`   Layout: [Ch1_B1, Ch1_B2, ..., Ch1_B24, ...]\n`);

// ============================================================
// Example 2: Octave Filter Bank for Musical Analysis
// ============================================================

console.log("--- Example 2: Octave Filter Bank (Musical) ---");

const audioRate = 44100;

// Design a 10-band logarithmic (octave) filter bank
const octaveBank = FilterBankDesign.createLog(10, audioRate, [20, 20000]);

console.log(`✅ Designed 10-band octave filter bank`);
console.log(`   Frequency range: 20-20000 Hz (full audio spectrum)`);
console.log(`   Sample rate: ${audioRate} Hz`);

// Get frequency boundaries for visualization
const boundaries = FilterBankDesign.getBoundaries({
  scale: "log",
  count: 10,
  sampleRate: audioRate,
  frequencyRange: [20, 20000],
});

console.log(`\n🎵 Octave band edges (Hz):`);
for (let i = 0; i < boundaries.length - 1; i++) {
  console.log(
    `   Band ${i + 1}: ${boundaries[i].toFixed(1)} - ${boundaries[
      i + 1
    ].toFixed(1)} Hz`
  );
}

// Create pipeline with octave filter bank
const octavePipeline = createDspPipeline().FilterBank({
  definitions: octaveBank,
  inputChannels: 1,
});

// Generate test signal: musical chord (A major: A3=220Hz, C#4=277Hz, E4=330Hz)
const chordSignal = new Float32Array(44100); // 1 second
for (let i = 0; i < chordSignal.length; i++) {
  const t = i / audioRate;
  chordSignal[i] =
    0.4 * Math.sin(2 * Math.PI * 220 * t) + // A3
    0.3 * Math.sin(2 * Math.PI * 277 * t) + // C#4
    0.3 * Math.sin(2 * Math.PI * 330 * t); // E4
}

console.log(`\n📊 Processing musical chord through octave filter bank...`);
const octaveOutput = await octavePipeline.process(chordSignal, {
  sampleRate: audioRate,
  channels: 1,
});

console.log(`   Input: 1 channel × ${chordSignal.length} samples`);
console.log(
  `   Output: 10 channels × ${octaveOutput.length / 10} samples (interleaved)`
);
console.log(`   Each band captures a different frequency range\n`);

// ============================================================
// Example 3: Stereo Input with Filter Bank
// ============================================================

console.log("--- Example 3: Stereo Filter Bank ---");

// Use a smaller Bark-scale filter bank for stereo demo
const barkBank = FilterBankDesign.createBark(12, sampleRate, [100, 8000]);

console.log(`✅ Designed 12-band Bark-scale filter bank`);
console.log(`   Input: 2 channels (stereo)`);
console.log(`   Output: 24 channels (12 bands × 2 input channels)`);

const stereoPipeline = createDspPipeline().FilterBank({
  definitions: barkBank,
  inputChannels: 2, // Stereo input
});

// Generate stereo test signal (left=500Hz, right=1500Hz)
const stereoSignal = new Float32Array(32000); // 2 channels × 8000 samples = 16000 samples/ch
for (let i = 0; i < 16000; i++) {
  const t = i / sampleRate;
  stereoSignal[i * 2 + 0] = Math.sin(2 * Math.PI * 500 * t); // Left: 500 Hz
  stereoSignal[i * 2 + 1] = Math.sin(2 * Math.PI * 1500 * t); // Right: 1500 Hz
}

console.log(`\n📊 Processing stereo signal through Bark filter bank...`);
const stereoOutput = await stereoPipeline.process(stereoSignal, {
  sampleRate,
  channels: 2,
});

console.log(`   Input: 2 channels × 16000 samples (interleaved)`);
console.log(
  `   Output: 24 channels × ${stereoOutput.length / 24} samples (interleaved)`
);
console.log(`   Layout: [L_B1, L_B2, ..., L_B12, R_B1, R_B2, ..., R_B12, ...]`);
console.log(`   Left channel energy concentrated in low bands (500 Hz)`);
console.log(`   Right channel energy concentrated in mid bands (1500 Hz)\n`);

// ============================================================
// Example 4: Custom Filter Bank Design
// ============================================================

console.log("--- Example 4: Custom Filter Bank Design ---");

// Design with explicit parameters
const customBank = FilterBankDesign.design({
  scale: "mel",
  count: 16,
  sampleRate: 8000,
  frequencyRange: [200, 3400], // Telephone bandwidth
  type: "butterworth",
  order: 3,
  rippleDb: 0.5,
});

console.log(`✅ Custom 16-band Mel filter bank (telephone bandwidth)`);
console.log(`   Frequency range: 200-3400 Hz`);
console.log(`   Filter type: Butterworth, order 3`);
console.log(`   Sample rate: 8000 Hz`);

const customPipeline = createDspPipeline().FilterBank({
  definitions: customBank,
  inputChannels: 1,
});

// Generate test signal: multi-tone
const multiTone = new Float32Array(8000);
const tones = [300, 600, 1200, 2400]; // Hz
for (let i = 0; i < multiTone.length; i++) {
  const t = i / 8000;
  multiTone[i] = tones.reduce(
    (sum, freq) => sum + 0.25 * Math.sin(2 * Math.PI * freq * t),
    0
  );
}

console.log(`\n📊 Processing multi-tone signal...`);
const customOutput = await customPipeline.process(multiTone, {
  sampleRate: 8000,
  channels: 1,
});

console.log(`   Input: 1 channel × ${multiTone.length} samples`);
console.log(
  `   Output: 16 channels × ${customOutput.length / 16} samples (interleaved)`
);
console.log(`   Each band captures one or more tones\n`);

// ============================================================
// Example 5: Energy Analysis Across Bands
// ============================================================

console.log("--- Example 5: Energy Analysis ---");

// Calculate RMS energy per band for the Mel filter bank output
const numBands = 24;
const samplesPerBand = melOutput.length / numBands;
const bandEnergies: number[] = [];

for (let band = 0; band < numBands; band++) {
  let sumSquares = 0;
  for (let i = 0; i < samplesPerBand; i++) {
    const sample = melOutput[band + i * numBands];
    sumSquares += sample * sample;
  }
  const rms = Math.sqrt(sumSquares / samplesPerBand);
  bandEnergies.push(rms);
}

console.log(`📊 Energy distribution across 24 Mel bands:`);
const maxEnergy = Math.max(...bandEnergies);
for (let i = 0; i < numBands; i++) {
  const normalized = bandEnergies[i] / maxEnergy;
  const bars = "█".repeat(Math.round(normalized * 20));
  console.log(`   Band ${i + 1}: ${bars} (${(normalized * 100).toFixed(1)}%)`);
}

console.log(
  `\n✅ Bands with most energy correspond to formant frequencies (200, 800, 2500 Hz)\n`
);

// ============================================================
// Example 6: Chaining with Other Stages
// ============================================================

console.log("--- Example 6: Filter Bank + Envelope Detection ---");

// Combine filter bank with RMS for envelope detection per band
const envelopePipeline = createDspPipeline()
  .FilterBank({
    definitions: FilterBankDesign.createMel(8, sampleRate, [100, 4000]),
    inputChannels: 1,
  })
  .Rms({ mode: "moving", windowSize: 256 }); // RMS on all 8 band outputs

console.log(`✅ Pipeline: Mel Filter Bank (8 bands) → RMS Envelope`);

const envelopeOutput = await envelopePipeline.process(speechSignal, {
  sampleRate,
  channels: 1,
});

console.log(`   Input: 1 channel × ${speechSignal.length} samples`);
console.log(`   After FilterBank: 8 channels`);
console.log(`   After RMS: 8 channels (smoothed envelopes)`);
console.log(
  `   Output: ${envelopeOutput.length / 8} samples per band (interleaved)\n`
);

console.log("=== All Examples Complete ===");
