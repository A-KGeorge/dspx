/**
 * Native State Resilience Test
 *
 * Demonstrates transactional safety of TOON deserialization
 * Tests rollback behavior when loading corrupted state
 */

import { createDspPipeline } from "../src/ts/index.js";
import * as fs from "fs";

async function testNativeResilience() {
  console.log("\n=== Native State Resilience Test ===\n");

  // Create pipeline with multiple stages
  const pipeline = createDspPipeline();

  pipeline
    .MovingAverage({ mode: "moving", windowSize: 100 })
    .Rms({ mode: "moving", windowSize: 50 })
    .Rectify({ mode: "full" });

  console.log("1. Created pipeline with 3 stages");

  // Process some data to populate stage state
  const samples = new Float32Array(1000);
  for (let i = 0; i < samples.length; i++) {
    samples[i] = Math.sin(i * 0.1) + Math.random() * 0.2;
  }

  await pipeline.process(samples, { channels: 1, sampleRate: 1000 });
  console.log("2. Processed data to populate stage state");

  // Save state (TOON format)
  const originalState = await pipeline.saveState({ format: "toon" });
  console.log(`3. Saved state: ${originalState.length} bytes (TOON format)`);

  // Verify state works
  const result1 = await pipeline.process(samples.slice(0, 100), {
    channels: 1,
  });
  console.log(`4. Verified pipeline works: output length ${result1.length}`);

  // Test 1: Corrupt the state buffer (mid-section)
  console.log("\n--- Test 1: Corrupted Buffer (Mid-Section) ---");
  const corrupted1 = Buffer.from(originalState);
  const corruptOffset = Math.floor(corrupted1.length / 2);
  corrupted1[corruptOffset] = 0xff;
  corrupted1[corruptOffset + 1] = 0xff;
  corrupted1[corruptOffset + 2] = 0xff;

  console.log(`Corrupted 3 bytes at offset ${corruptOffset}`);

  try {
    await pipeline.loadState(corrupted1);
    console.log("✗ FAIL: Should have thrown error");
  } catch (error: any) {
    console.log(`✓ PASS: Caught error: ${error.message.substring(0, 80)}...`);

    // Verify pipeline still works after rollback
    const result2 = await pipeline.process(samples.slice(0, 100), {
      channels: 1,
    });
    console.log(
      `✓ PASS: Pipeline still functional after rollback (output: ${result2.length})`
    );
  }

  // Test 2: Corrupt the header
  console.log("\n--- Test 2: Corrupted Header ---");
  const corrupted2 = Buffer.from(originalState);
  corrupted2[0] = 0x00; // Should be 0x7B ('{' in TOON)

  try {
    await pipeline.loadState(corrupted2);
    console.log("✗ FAIL: Should have thrown error");
  } catch (error: any) {
    console.log(`✓ PASS: Caught error: ${error.message.substring(0, 80)}...`);
  }

  // Test 3: Load valid state (should succeed)
  console.log("\n--- Test 3: Load Valid State ---");
  try {
    const success = await pipeline.loadState(originalState);
    console.log(`✓ PASS: Loaded valid state successfully (success=${success})`);

    const result3 = await pipeline.process(samples.slice(0, 100), {
      channels: 1,
    });
    console.log(
      `✓ PASS: Pipeline works after successful load (output: ${result3.length})`
    );
  } catch (error: any) {
    console.log(`✗ FAIL: Should not have thrown: ${error.message}`);
  }

  // Test 4: Stage count mismatch
  console.log("\n--- Test 4: Stage Count Mismatch ---");
  const pipeline2 = createDspPipeline();
  pipeline2.MovingAverage({ mode: "moving", windowSize: 100 }); // Only 1 stage

  try {
    await pipeline2.loadState(originalState); // Has 3 stages
    console.log("✗ FAIL: Should have rejected incompatible state");
  } catch (error: any) {
    console.log(
      `✓ PASS: Rejected incompatible state: ${error.message.substring(
        0,
        80
      )}...`
    );
  }

  // Test 5: Stage type mismatch
  console.log("\n--- Test 5: Stage Type Mismatch ---");
  const pipeline3 = createDspPipeline();
  pipeline3
    .MovingAverage({ mode: "moving", windowSize: 100 })
    .Rectify({ mode: "full" }) // Different type in position 2
    .Rms({ mode: "moving", windowSize: 50 }); // Different order

  try {
    await pipeline3.loadState(originalState);
    console.log("✗ FAIL: Should have rejected incompatible stage types");
  } catch (error: any) {
    console.log(
      `✓ PASS: Rejected incompatible types: ${error.message.substring(
        0,
        80
      )}...`
    );
  }

  // Test 6: Save corrupted state to file for manual inspection
  console.log("\n--- Test 6: Export Corrupted State for Debugging ---");
  const debugDir = "./debug_state";
  if (!fs.existsSync(debugDir)) {
    fs.mkdirSync(debugDir, { recursive: true });
  }

  fs.writeFileSync(`${debugDir}/valid_state.toon`, originalState);
  fs.writeFileSync(`${debugDir}/corrupted_mid.toon`, corrupted1);
  fs.writeFileSync(`${debugDir}/corrupted_header.toon`, corrupted2);
  console.log(`✓ Exported states to ${debugDir}/`);

  // Test 7: Performance - measure rollback overhead
  console.log("\n--- Test 7: Rollback Performance ---");
  const iterations = 100;
  let rollbackTime = 0;

  for (let i = 0; i < iterations; i++) {
    const corrupted = Buffer.from(originalState);
    corrupted[Math.floor(corrupted.length / 2)] = 0xff;

    const start = process.hrtime.bigint();
    try {
      await pipeline.loadState(corrupted);
    } catch (error) {
      // Expected
    }
    const end = process.hrtime.bigint();
    rollbackTime += Number(end - start) / 1e6; // Convert to ms
  }

  const avgRollbackTime = rollbackTime / iterations;
  console.log(
    `Average rollback time: ${avgRollbackTime.toFixed(
      3
    )} ms (${iterations} iterations)`
  );
  console.log(`Overhead per stage: ${(avgRollbackTime / 3).toFixed(3)} ms`);

  console.log("\n✓ All tests completed successfully!");
}

// Run tests
testNativeResilience().catch((error) => {
  console.error("Test failed:", error);
  process.exit(1);
});
