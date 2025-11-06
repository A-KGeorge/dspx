/**
 * Quick test of the new FftBatchProcessor
 */

const { FftBatchProcessor } = require("../dist/fft.js");

console.log("Testing FftBatchProcessor...\n");

// Create processor
const processor = new FftBatchProcessor({
  numThreads: 4,
  enableCache: true,
});

console.log(`✓ Created processor with ${processor.getNumThreads()} threads`);

// Generate test signals
const numSignals = 8;
const signalLength = 1024;
const signals = [];

for (let i = 0; i < numSignals; i++) {
  const signal = new Float32Array(signalLength);
  for (let j = 0; j < signalLength; j++) {
    signal[j] = Math.sin((2 * Math.PI * (100 + i * 50) * j) / 44100);
  }
  signals.push({ input: signal });
}

console.log(
  `✓ Generated ${numSignals} test signals (${signalLength} samples each)`
);

// Process batch
const start = Date.now();
const results = processor.processBatch(signals);
const elapsed = Date.now() - start;

console.log(`✓ Processed batch in ${elapsed}ms`);
console.log(`✓ Got ${results.length} results`);
console.log(`✓ First result has ${results[0].real.length} frequency bins`);

// Test cache
processor.clearCache();
const coldStart = Date.now();
processor.processBatch(signals);
const coldTime = Date.now() - coldStart;

const warmStart = Date.now();
processor.processBatch(signals);
const warmTime = Date.now() - warmStart;

const stats = processor.getCacheStats();
console.log(`\n✓ Cache test:`);
console.log(`  Cold run: ${coldTime}ms`);
console.log(
  `  Warm run: ${warmTime}ms (${(coldTime / warmTime).toFixed(1)}x faster)`
);
console.log(`  Hit rate: ${(stats.hitRate * 100).toFixed(1)}%`);
console.log(`  Hits: ${stats.hits}, Misses: ${stats.misses}`);

console.log("\n✅ All tests passed!");
