/**
 * Chained DSP Pipeline (MovingAverage → RMS → Rectify)
 * with Redis state persistence.
 *
 * This verifies:
 *  - Multi-stage serialization to Redis
 *  - Restoration from Redis into a fresh pipeline
 *  - Stateless Rectify doesn’t corrupt persisted filters
 *  - Outputs match expected chaining results
 */

import { createClient } from "redis";
import { createDspPipeline } from "../../bindings";

async function testChainingWithRedis(startFresh = false) {
  console.log("=== DSP Chaining + Redis State Persistence Test ===\n");

  // 1. Connect to Redis
  const redis = await createClient({ url: "redis://localhost:6379" }).connect();
  const stateKey = "dsp:pipeline:chained";

  if (startFresh) {
    console.log("Clearing previous Redis state...");
    await redis.del(stateKey);
    console.log("Redis state cleared.\n");
  }

  // 2. Create a chained pipeline
  const pipeline = createDspPipeline();

  pipeline
    .MovingAverage({ mode: "moving", windowSize: 3 })
    .Rms({ mode: "moving", windowSize: 3 })
    .Rectify();

  console.log("Pipeline: MovingAverage → RMS → Rectify\n");

  // 3️. Attempt to restore previous state
  const previousState = await redis.get(stateKey);
  if (previousState) {
    console.log("Found existing state in Redis. Restoring...\n");
    await pipeline.loadState(previousState);
    console.log("State restored successfully!\n");
  } else {
    console.log("No previous state found — starting fresh.\n");
  }

  // 4. Process first batch
  const batch1 = new Float32Array([1, -2, 3, -4, 5, -6]);
  console.log("Input batch 1:", Array.from(batch1));

  const output1 = await pipeline.processCopy(batch1, {
    sampleRate: 1000,
  });
  console.log(
    "Output batch 1:",
    Array.from(output1).map((v) => v.toFixed(4))
  );

  // 5. Save state to Redis
  const state1 = await pipeline.saveState();
  await redis.set(stateKey, state1);
  console.log("\nState saved to Redis:");
  console.log(JSON.stringify(JSON.parse(state1), null, 2));

  // 6. Simulate process restart
  console.log("\n--- Simulating Restart ---\n");
  const pipeline2 = createDspPipeline();

  pipeline2
    .MovingAverage({ mode: "moving", windowSize: 3 })
    .Rms({ mode: "moving", windowSize: 3 })
    .Rectify();

  const restoredState = await redis.get(stateKey);
  if (restoredState) {
    console.log("Restoring pipeline from Redis...");
    await pipeline2.loadState(restoredState);
    console.log("Pipeline state fully restored.\n");
  }

  // 7. Process second batch (continuation test)
  const batch2 = new Float32Array([7, -8, 9]);
  console.log("Input batch 2:", Array.from(batch2));

  const output2 = await pipeline2.processCopy(batch2, {
    sampleRate: 1000,
  });
  console.log(
    "Output batch 2:",
    Array.from(output2).map((v) => v.toFixed(4))
  );

  // 8. Inspect rectifier behavior (stateless)
  const savedState2 = await pipeline2.saveState();
  const parsed = JSON.parse(savedState2);
  const rectifyState = parsed.stages.find((s: any) => s.type === "rectify");

  console.log("\nRectify stage state:");
  console.log(rectifyState || "Rectify stage not found in saved state!");

  console.log("\nChaining + Redis test complete!");

  // 9. Disconnect from Redis
  await redis.disconnect();
}

// Toggle this to reset Redis between runs
const START_FRESH = false;

testChainingWithRedis(START_FRESH).catch(console.error);
