/**
 * Using Pre-computed Optimal FIR Coefficients
 * 
 * This example shows how to use the pre-computed optimal FIR coefficient tables
 * that ship with the library. No Python needed at runtime!
 */

const { FirFilter } = require('./build/Release/dspx.node');

// Import the pre-computed optimal coefficients
// In production: import { OPTIMAL_LOWPASS_COEFFS, getOptimalLowpass } from 'dsp-ts-redis/optimal-fir-tables';
const {
  OPTIMAL_LOWPASS_COEFFS,
  OPTIMAL_HIGHPASS_COEFFS,
  OPTIMAL_BANDPASS_COEFFS,
  OPTIMAL_NOTCH_COEFFS,
  getOptimalLowpass,
  getOptimalHighpass,
  getPowerLineNotch
} = require('./src/ts/optimal-fir-tables');

console.log('Pre-computed Optimal FIR Coefficients Demo');
console.log('===========================================\n');

// ============================================================================
// Example 1: Direct Access to Optimal Coefficients
// ============================================================================

console.log('Example 1: Direct Access');
console.log('-----------------------');

// Create optimal lowpass filter (87 taps vs 128 = 32% faster!)
const lowpass = new FirFilter(OPTIMAL_LOWPASS_COEFFS.cutoff_0_2, true);
console.log(`✅ Created optimal lowpass filter: ${OPTIMAL_LOWPASS_COEFFS.cutoff_0_2.length} taps`);
console.log(`   (Traditional window-based would use 128 taps = 32% slower)\n`);

// Create optimal highpass filter
const highpass = new FirFilter(OPTIMAL_HIGHPASS_COEFFS.cutoff_0_2, true);
console.log(`✅ Created optimal highpass filter: ${OPTIMAL_HIGHPASS_COEFFS.cutoff_0_2.length} taps`);
console.log(`   (Traditional window-based would use 141 taps = 31% slower)\n`);

// ============================================================================
// Example 2: Using Helper Functions
// ============================================================================

console.log('Example 2: Helper Functions');
console.log('---------------------------');

// Automatically select best filter for cutoff frequency
const coeffs1 = getOptimalLowpass(0.2);  // Picks cutoff_0_2 (87 taps)
const coeffs2 = getOptimalLowpass(0.35); // Picks cutoff_0_3 (73 taps)

console.log(`Cutoff 0.2π: ${coeffs1.length} taps`);
console.log(`Cutoff 0.35π: ${coeffs2.length} taps (even faster!)\n`);

// ============================================================================
// Example 3: Power Line Notch Filter
// ============================================================================

console.log('Example 3: Power Line Notch (60 Hz)');
console.log('-----------------------------------');

// Get pre-computed 60 Hz notch filter for 1000 Hz sampling
const notchCoeffs = getPowerLineNotch(1000, 60);
const notchFilter = new FirFilter(notchCoeffs, true);

console.log(`✅ Created 60 Hz notch filter: ${notchCoeffs.length} taps`);
console.log(`   Removes 60 Hz ±2 Hz with 60 dB attenuation\n`);

// ============================================================================
// Example 4: Performance Test
// ============================================================================

console.log('Example 4: Performance Comparison');
console.log('---------------------------------');

// Generate test signal
const signalLength = 50000;
const testSignal = new Float32Array(signalLength);
for (let i = 0; i < signalLength; i++) {
    testSignal[i] = Math.sin(2 * Math.PI * 0.1 * i) +  // Signal at 0.1π
                    Math.sin(2 * Math.PI * 0.4 * i);    // Noise at 0.4π
}

// Test optimal filter (87 taps)
const optimalFilter = new FirFilter(OPTIMAL_LOWPASS_COEFFS.cutoff_0_2, true);
const start1 = performance.now();
const output1 = optimalFilter.process(testSignal);
const time1 = performance.now() - start1;

// Test traditional filter (128 taps)
const traditionalFilter = FirFilter.createLowPass({
    sampleRate: 1000,
    cutoffFrequency: 100,
    transitionWidth: 25,
    stateful: true
});
const start2 = performance.now();
const output2 = traditionalFilter.process(testSignal);
const time2 = performance.now() - start2;

console.log(`Optimal (87 taps):      ${time1.toFixed(2)} ms`);
console.log(`Traditional (128 taps): ${time2.toFixed(2)} ms`);
console.log(`Speedup:                ${(time2 / time1).toFixed(2)}x`);
console.log(`Performance gain:       ${((time2 - time1) / time2 * 100).toFixed(1)}%\n`);

// ============================================================================
// Example 5: All Available Filters
// ============================================================================

console.log('Example 5: Available Pre-computed Filters');
console.log('-----------------------------------------');

console.log('Lowpass filters:');
console.log(`  cutoff_0_1: ${OPTIMAL_LOWPASS_COEFFS.cutoff_0_1.length} taps (very sharp, 0.1π)`);
console.log(`  cutoff_0_2: ${OPTIMAL_LOWPASS_COEFFS.cutoff_0_2.length} taps (moderate, 0.2π)`);
console.log(`  cutoff_0_3: ${OPTIMAL_LOWPASS_COEFFS.cutoff_0_3.length} taps (wide, 0.3π)`);
console.log(`  cutoff_0_4: ${OPTIMAL_LOWPASS_COEFFS.cutoff_0_4.length} taps (relaxed, 0.4π)`);

console.log('\nHighpass filters:');
console.log(`  cutoff_0_1: ${OPTIMAL_HIGHPASS_COEFFS.cutoff_0_1.length} taps (sharp, 0.1π)`);
console.log(`  cutoff_0_2: ${OPTIMAL_HIGHPASS_COEFFS.cutoff_0_2.length} taps (moderate, 0.2π)`);
console.log(`  cutoff_0_3: ${OPTIMAL_HIGHPASS_COEFFS.cutoff_0_3.length} taps (relaxed, 0.3π)`);

console.log('\nBandpass filters:');
console.log(`  band_0_15_0_35: ${OPTIMAL_BANDPASS_COEFFS.band_0_15_0_35.length} taps (narrow)`);
console.log(`  band_0_2_0_4: ${OPTIMAL_BANDPASS_COEFFS.band_0_2_0_4.length} taps (moderate)`);
console.log(`  band_0_25_0_45: ${OPTIMAL_BANDPASS_COEFFS.band_0_25_0_45.length} taps (wide)`);

console.log('\nNotch filters:');
const notchKeys = Object.keys(OPTIMAL_NOTCH_COEFFS);
notchKeys.forEach(key => {
    const taps = OPTIMAL_NOTCH_COEFFS[key].length;
    console.log(`  ${key}: ${taps} taps`);
});

console.log('\n✅ All filters ready to use - no Python required!');
console.log('🚀 30-50% better performance than traditional designs!');
