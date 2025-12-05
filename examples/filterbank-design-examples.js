/**
 * Filter Bank Design Examples
 *
 * Demonstrates creating psychoacoustic and mathematical filter banks
 * for audio and speech analysis applications.
 */
import { FilterBankDesign } from '../dist/index.js';
console.log('='.repeat(80));
console.log('FILTER BANK DESIGN EXAMPLES');
console.log('='.repeat(80));
// ============================================================================
// Example 1: Mel-Scale Filter Bank (Speech Recognition)
// ============================================================================
console.log('\n' + '='.repeat(80));
console.log('Example 1: 24-Band Mel-Scale Filter Bank for Speech Recognition');
console.log('='.repeat(80));
const melBank = FilterBankDesign.design({
    scale: 'mel',
    count: 24,
    sampleRate: 16000,
    frequencyRange: [100, 8000],
    type: 'butterworth',
    order: 2
});
console.log(`\n✓ Created ${melBank.length} Mel-spaced filters`);
console.log(`  Frequency range: 100 Hz - 8000 Hz`);
console.log(`  Sample rate: 16000 Hz`);
// Show first few filters
console.log('\nFirst 5 filters:');
const melBoundaries = FilterBankDesign.getBoundaries({
    scale: 'mel',
    count: 24,
    sampleRate: 16000,
    frequencyRange: [100, 8000]
});
for (let i = 0; i < 5; i++) {
    console.log(`  Band ${i + 1}: ${melBoundaries[i].toFixed(1)} Hz - ${melBoundaries[i + 1].toFixed(1)} Hz`);
    console.log(`    b coeffs (${melBank[i].b.length}): [${melBank[i].b.slice(0, 3).map(c => c.toFixed(6)).join(', ')}, ...]`);
    console.log(`    a coeffs (${melBank[i].a.length}): [${melBank[i].a.slice(0, 3).map(c => c.toFixed(6)).join(', ')}, ...]`);
}
// ============================================================================
// Example 2: Bark-Scale Filter Bank (Psychoacoustic Analysis)
// ============================================================================
console.log('\n' + '='.repeat(80));
console.log('Example 2: 20-Band Bark-Scale Filter Bank');
console.log('='.repeat(80));
const barkBank = FilterBankDesign.createBark(20, 44100, [20, 20000]);
console.log(`\n✓ Created ${barkBank.length} Bark-spaced filters`);
console.log(`  Frequency range: 20 Hz - 20000 Hz`);
console.log(`  Sample rate: 44100 Hz`);
const barkBoundaries = FilterBankDesign.getBoundaries({
    scale: 'bark',
    count: 20,
    sampleRate: 44100,
    frequencyRange: [20, 20000]
});
console.log('\nBoundary frequencies (Hz):');
console.log(barkBoundaries.map(f => f.toFixed(0)).join(', '));
// ============================================================================
// Example 3: Logarithmic (Octave Band) Filter Bank
// ============================================================================
console.log('\n' + '='.repeat(80));
console.log('Example 3: 10-Band Octave Filter Bank');
console.log('='.repeat(80));
const octaveBank = FilterBankDesign.createLog(10, 44100, [20, 20000]);
console.log(`\n✓ Created ${octaveBank.length} logarithmically-spaced filters`);
console.log(`  Frequency range: 20 Hz - 20000 Hz`);
console.log(`  Sample rate: 44100 Hz`);
const octaveBoundaries = FilterBankDesign.getBoundaries({
    scale: 'log',
    count: 10,
    sampleRate: 44100,
    frequencyRange: [20, 20000]
});
console.log('\nOctave band edges:');
for (let i = 0; i < octaveBoundaries.length - 1; i++) {
    const center = Math.sqrt(octaveBoundaries[i] * octaveBoundaries[i + 1]);
    console.log(`  Band ${i + 1}: ${octaveBoundaries[i].toFixed(1)} - ${octaveBoundaries[i + 1].toFixed(1)} Hz (center: ${center.toFixed(1)} Hz)`);
}
// ============================================================================
// Example 4: Linear Filter Bank (Equal Bandwidth)
// ============================================================================
console.log('\n' + '='.repeat(80));
console.log('Example 4: 16-Band Linear Filter Bank');
console.log('='.repeat(80));
const linearBank = FilterBankDesign.createLinear(16, 44100, [0, 22050]);
console.log(`\n✓ Created ${linearBank.length} linearly-spaced filters`);
console.log(`  Frequency range: 0 Hz - 22050 Hz (Nyquist)`);
console.log(`  Sample rate: 44100 Hz`);
const linearBoundaries = FilterBankDesign.getBoundaries({
    scale: 'linear',
    count: 16,
    sampleRate: 44100,
    frequencyRange: [0, 22050]
});
const bandwidth = linearBoundaries[1] - linearBoundaries[0];
console.log(`\nEqual bandwidth: ${bandwidth.toFixed(1)} Hz per band`);
console.log('Band edges:');
console.log(linearBoundaries.map(f => f.toFixed(0)).join(', '));
// ============================================================================
// Example 5: Chebyshev Filter Bank (Steeper Rolloff)
// ============================================================================
console.log('\n' + '='.repeat(80));
console.log('Example 5: 12-Band Mel Filter Bank with Chebyshev Filters');
console.log('='.repeat(80));
const chebyshevBank = FilterBankDesign.design({
    scale: 'mel',
    count: 12,
    sampleRate: 44100,
    frequencyRange: [300, 8000],
    type: 'chebyshev',
    order: 4,
    rippleDb: 0.5
});
console.log(`\n✓ Created ${chebyshevBank.length} Chebyshev bandpass filters`);
console.log(`  Type: Chebyshev Type I (0.5 dB ripple)`);
console.log(`  Order: 4 (steeper rolloff than Butterworth)`);
console.log(`  Frequency range: 300 Hz - 8000 Hz`);
// ============================================================================
// Example 6: Comparing Scales
// ============================================================================
console.log('\n' + '='.repeat(80));
console.log('Example 6: Comparing Different Scales');
console.log('='.repeat(80));
const scales = ['linear', 'log', 'mel', 'bark'];
const count = 10;
const sampleRate = 16000;
const range = [100, 8000];
console.log(`\nComparing ${count} bands from ${range[0]} - ${range[1]} Hz at ${sampleRate} Hz:\n`);
for (const scale of scales) {
    const boundaries = FilterBankDesign.getBoundaries({
        scale,
        count,
        sampleRate,
        frequencyRange: range
    });
    const bandwidths = [];
    for (let i = 0; i < boundaries.length - 1; i++) {
        bandwidths.push(boundaries[i + 1] - boundaries[i]);
    }
    const avgBandwidth = bandwidths.reduce((a, b) => a + b, 0) / bandwidths.length;
    const minBandwidth = Math.min(...bandwidths);
    const maxBandwidth = Math.max(...bandwidths);
    console.log(`${scale.toUpperCase().padEnd(8)} scale:`);
    console.log(`  Average bandwidth: ${avgBandwidth.toFixed(1)} Hz`);
    console.log(`  Bandwidth range: ${minBandwidth.toFixed(1)} - ${maxBandwidth.toFixed(1)} Hz`);
    console.log(`  First band: ${boundaries[0].toFixed(1)} - ${boundaries[1].toFixed(1)} Hz (${bandwidths[0].toFixed(1)} Hz)`);
    console.log(`  Last band: ${boundaries[count - 1].toFixed(1)} - ${boundaries[count].toFixed(1)} Hz (${bandwidths[count - 1].toFixed(1)} Hz)`);
    console.log('');
}
// ============================================================================
// Example 7: Using Filter Bank with Pipeline
// ============================================================================
console.log('='.repeat(80));
console.log('Example 7: Integration with DSP Pipeline');
console.log('='.repeat(80));
console.log('\nHow to use filter bank coefficients in a pipeline:\n');
console.log('```typescript');
console.log('import { createDspPipeline, FilterBankDesign } from "dspx";');
console.log('');
console.log('// 1. Design filter bank');
console.log('const bank = FilterBankDesign.createMel(24, 16000, [100, 8000]);');
console.log('');
console.log('// 2. Process signal through all bands');
console.log('const bandOutputs = [];');
console.log('for (const coeffs of bank) {');
console.log('  const pipeline = createDspPipeline();');
console.log('  pipeline.filter({');
console.log('    type: "iir",');
console.log('    b: coeffs.b,');
console.log('    a: coeffs.a');
console.log('  });');
console.log('  ');
console.log('  const output = await pipeline.process(signal, {');
console.log('    sampleRate: 16000,');
console.log('    channels: 1');
console.log('  });');
console.log('  ');
console.log('  bandOutputs.push(output);');
console.log('}');
console.log('```');
// ============================================================================
// Summary
// ============================================================================
console.log('\n' + '='.repeat(80));
console.log('SUMMARY');
console.log('='.repeat(80));
console.log('\nKey Features:');
console.log('  ✓ 4 frequency scales: Linear, Log, Mel, Bark');
console.log('  ✓ 2 filter types: Butterworth, Chebyshev');
console.log('  ✓ Configurable order (steepness)');
console.log('  ✓ Helper methods for common scales');
console.log('  ✓ Boundary visualization support');
console.log('\nCommon Use Cases:');
console.log('  • Speech recognition: Mel-scale (20-40 bands)');
console.log('  • Audio compression: Bark-scale (20-30 bands)');
console.log('  • Musical analysis: Log-scale (10 octave bands)');
console.log('  • Research/testing: Linear-scale');
console.log('\nPerformance:');
console.log('  • Stateless design (no runtime state)');
console.log('  • Fast coefficient generation (< 1ms for 24 bands)');
console.log('  • Optimized IIR filter designs');
console.log('  • Ready for real-time processing');
console.log('\n✅ All filter bank design examples completed!\n');
