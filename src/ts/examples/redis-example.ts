/**
 * Example showing how to use Redis for DSP state persistence
 *
 * This demonstrates:
 * 1. Creating a pipeline with Redis configuration
 * 2. Processing audio data
 * 3. Saving/loading state (when Redis integration is fully implemented)
 *
 * Note: This example shows the intended API. Full C++ Redis integration
 * requires implementing saveState/loadState methods in DspPipeline.cc
 */

import { createClient } from "redis";
import { createDspPipeline } from "../bindings";

async function redisExample(startFresh = false) {
  console.log("=== DSP Pipeline with Redis State Persistence ===\n");

  // 1. Create Redis client
  const redis = await createClient({ url: "redis://localhost:6379" }).connect();

  // 2. Create DSP pipeline with Redis configuration
  const stateKey = "dsp:pipeline:channel1";

  // Optional: Clear previous state to start fresh
  if (startFresh) {
    console.log("Clearing previous state to start fresh...");
    await redis.del(stateKey);
    console.log("State cleared\n");
  }

  const pipeline = createDspPipeline();

  // 3. Build the pipeline
  pipeline.MovingAverage({ mode: "moving", windowSize: 3 });

  console.log("Pipeline created with moving average filter (window=3)");

  // 4. Try to restore previous state (if exists)
  const previousState = await redis.get(stateKey);
  if (previousState) {
    console.log("Found previous state in Redis, restoring...");
    await pipeline.loadState(previousState);
    console.log("State restored successfully!");
  } else {
    console.log("No previous state found in Redis");
  }

  // 5. First batch of audio
  console.log("\n--- Processing first batch ---");
  const batch1 = new Float32Array([1, 2, 3, 4, 5]);
  console.log("Input batch 1:", Array.from(batch1));

  const output1 = await pipeline.processCopy(batch1, {
    sampleRate: 1000,
    channels: 1,
  });
  console.log("Output batch 1:", Array.from(output1));

  // 6. Save state to Redis
  console.log("\n--- Saving state to Redis ---");
  const state = await pipeline.saveState();
  await redis.set(stateKey, state);
  console.log("State saved:", state);

  // 7. Simulate stopping and restarting the process
  console.log("\n--- Simulating restart ---");

  // 8. Create new pipeline and restore state
  const pipeline2 = createDspPipeline();

  pipeline2.MovingAverage({ mode: "moving", windowSize: 3 });

  const restoredState = await redis.get(stateKey);
  if (restoredState) {
    console.log("Restored state from Redis");
    await pipeline2.loadState(restoredState);
    console.log(
      "State restoration complete - continuing from where we left off!"
    );
  }

  // 9. Continue processing with restored state
  console.log("\n--- Processing second batch (after 'restart') ---");
  const batch2 = new Float32Array([6, 7, 8]);
  console.log("Input batch 2:", Array.from(batch2));

  const output2 = await pipeline2.processCopy(batch2, {
    sampleRate: 1000,
    channels: 1,
  });
  console.log("Output batch 2:", Array.from(output2));

  console.log(
    "\nNote: With state restoration, batch2 continues the moving average from where batch1 left off!"
  );
  console.log(
    "Expected output without state: [6, 6.5, 7] - but we got continuous averaging!"
  );

  // 10. Optional: Clear state (commented out to persist between runs)
  // console.log("\n--- Clearing state ---");
  // await redis.del(stateKey);
  // pipeline2.clearState();
  // console.log("State cleared from Redis");

  // Disconnect from Redis
  await redis.disconnect();
} // Real-world use case example
async function streamingExample(startFresh = false) {
  console.log("\n\n=== Streaming Audio Processing Example ===\n");

  const redis = await createClient({ url: "redis://localhost:6379" }).connect();
  const channelId = "audio-stream-ch1";
  const stateKey = `dsp:stream:${channelId}`;

  // Optional: Clear previous state to start fresh
  if (startFresh) {
    console.log("Clearing previous streaming state to start fresh...");
    await redis.del(stateKey);
    console.log("Streaming state cleared\n");
  }

  // Create pipeline
  const pipeline = createDspPipeline();

  pipeline.MovingAverage({ mode: "moving", windowSize: 5 });

  // Restore state if processing was interrupted
  const savedState = await redis.get(stateKey);
  if (savedState) {
    console.log("Resuming from saved state...");
    await pipeline.loadState(savedState);
  }

  // Simulate streaming audio chunks
  const chunks = [
    new Float32Array([1, 2, 3, 4, 5]),
    new Float32Array([6, 7, 8, 9, 10]),
    new Float32Array([11, 12, 13, 14, 15]),
  ];

  console.log("Processing audio stream in chunks...\n");

  for (let i = 0; i < chunks.length; i++) {
    const chunk = chunks[i];
    console.log(`Chunk ${i + 1} input:`, Array.from(chunk));

    const processed = await pipeline.processCopy(chunk, {
      sampleRate: 44100,
      channels: 1,
    });

    console.log(`Chunk ${i + 1} output:`, Array.from(processed));

    // Save state after each chunk (for crash recovery)
    const state = await pipeline.saveState();
    await redis.set(stateKey, state);
    console.log(`State saved after chunk ${i + 1}\n`);
  }

  console.log("Stream processing complete. State saved for next session.");

  // Disconnect from Redis
  await redis.disconnect();
}

// Run examples
console.log("Running DSP + Redis examples...\n");

// Set to true to clear previous state and start fresh
// Set to false to demonstrate state persistence across runs
const START_FRESH = false;

redisExample(START_FRESH)
  .then(() => streamingExample(START_FRESH))
  .then(() => {
    console.log("\nAll examples completed successfully!");
    console.log(
      "\nTip: Set START_FRESH = true to clear Redis state and start fresh"
    );
    console.log(
      "Tip: Set START_FRESH = false to see state persistence across runs"
    );
    process.exit(0);
  })
  .catch((error) => {
    console.error("\nError:", error);
    process.exit(1);
  });
