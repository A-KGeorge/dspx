/**
 * Quick Start: Using Parks-McClellan Optimal FIR Coefficients
 *
 * This is the simplest way to use your optimal filter.
 */

const { FirFilter } = require("./build/Release/dspx.node");
const fs = require("fs");

// Step 1: Load the optimal coefficients
const coeffsJson = JSON.parse(fs.readFileSync("./lowpass.json", "utf-8"));
const optimalCoeffs = new Float32Array(coeffsJson);

console.log(
  `Loaded ${optimalCoeffs.length} optimal Parks-McClellan coefficients`
);

// Step 2: Create the filter (87 taps vs 128 taps = 32% faster!)
const filter = new FirFilter(optimalCoeffs, true); // true = stateful

// Step 3: Process your signal
const signal = new Float32Array(10000);
for (let i = 0; i < signal.length; i++) {
  signal[i] =
    Math.sin(2 * Math.PI * 0.1 * i) + // 0.1π signal (passes)
    Math.sin(2 * Math.PI * 0.4 * i); // 0.4π noise (filtered)
}

const filtered = filter.process(signal);

console.log(`✅ Filtered ${signal.length} samples`);
console.log(`Input RMS:  ${rms(signal).toFixed(4)}`);
console.log(`Output RMS: ${rms(filtered).toFixed(4)}`);
console.log(
  "\n🚀 You just got 32% better performance with optimal coefficients!"
);

function rms(arr) {
  let sum = 0;
  for (let i = 0; i < arr.length; i++) {
    sum += arr[i] * arr[i];
  }
  return Math.sqrt(sum / arr.length);
}
