import {
  describe,
  test,
  before,
  after,
  beforeEach,
  afterEach,
} from "node:test";
import assert from "node:assert/strict";
import { createDspPipeline, DspProcessor } from "../bindings.js";
import { createClient } from "redis";
import type { RedisClientType } from "redis";

const DEFAULT_OPTIONS = { channels: 1, sampleRate: 44100 };

function assertCloseTo(actual: number, expected: number, precision = 5) {
  const tolerance = Math.pow(10, -precision);
  assert.ok(
    Math.abs(actual - expected) < tolerance,
    `Expected ${actual} to be close to ${expected} (tolerance: ${tolerance})`
  );
}

// Helper to check if Redis is available
async function isRedisAvailable(): Promise<boolean> {
  try {
    const client = createClient({
      url: "redis://localhost:6379",
      socket: { connectTimeout: 1000 },
    });
    await client.connect();
    await client.disconnect();
    return true;
  } catch {
    return false;
  }
}

describe("Redis State Persistence", () => {
  let redis: RedisClientType;
  let redisAvailable: boolean;

  before(async () => {
    redisAvailable = await isRedisAvailable();
    if (!redisAvailable) {
      console.log(
        "\n⚠️  Redis not available - skipping Redis integration tests"
      );
      console.log(
        "   To run these tests, start Redis: docker run -p 6379:6379 redis\n"
      );
    }
  });

  beforeEach(async () => {
    if (!redisAvailable) return;

    redis = createClient({ url: "redis://localhost:6379" });
    await redis.connect();

    // Clean up test keys
    const keys = await redis.keys("test:dsp:*");
    if (keys.length > 0) {
      await redis.del(keys);
    }
  });

  afterEach(async () => {
    if (redis && redis.isOpen) {
      await redis.disconnect();
    }
  });

  describe("Basic State Persistence", () => {
    test("should save and restore state from Redis", async () => {
      if (!redisAvailable) return;

      const stateKey = "test:dsp:state:1";
      const processor = createDspPipeline();
      processor.MovingAverage({ mode: "moving", windowSize: 3 });

      // Build state
      await processor.process(
        new Float32Array([1, 2, 3, 4, 5]),
        DEFAULT_OPTIONS
      );

      // Save to Redis
      const stateJson = await processor.saveState();
      await redis.set(stateKey, stateJson);

      // Verify state was saved
      const savedState = await redis.get(stateKey);
      assert.ok(savedState);
      assert.equal(savedState, stateJson);

      // Create new processor and restore
      const processor2 = createDspPipeline();
      processor2.MovingAverage({ mode: "moving", windowSize: 3 });

      const restoredState = await redis.get(stateKey);
      assert.ok(restoredState);
      await processor2.loadState(restoredState);

      // Verify continuity
      const output1 = await processor.process(
        new Float32Array([6]),
        DEFAULT_OPTIONS
      );
      const output2 = await processor2.process(
        new Float32Array([6]),
        DEFAULT_OPTIONS
      );

      assertCloseTo(output1[0], output2[0]);
    });

    test("should handle missing state key gracefully", async () => {
      if (!redisAvailable) return;

      const stateKey = "test:dsp:state:nonexistent";
      const state = await redis.get(stateKey);

      assert.equal(state, null);
    });

    test("should update state in Redis after processing", async () => {
      if (!redisAvailable) return;

      const stateKey = "test:dsp:state:2";
      const processor = createDspPipeline();
      processor.MovingAverage({ mode: "moving", windowSize: 2 });

      // Initial state
      await processor.process(new Float32Array([1, 2]), DEFAULT_OPTIONS);
      const state1 = await processor.saveState();
      await redis.set(stateKey, state1);

      // Process more data
      await processor.process(new Float32Array([3, 4]), DEFAULT_OPTIONS);
      const state2 = await processor.saveState();
      await redis.set(stateKey, state2);

      // States should be different
      assert.notEqual(state1, state2);

      const savedState = await redis.get(stateKey);
      assert.equal(savedState, state2);
    });
  });

  describe("Multi-Stage Pipeline Persistence", () => {
    test("should persist and restore complex pipeline", async () => {
      if (!redisAvailable) return;

      const stateKey = "test:dsp:state:complex";
      const processor = createDspPipeline();
      processor
        .MovingAverage({ mode: "moving", windowSize: 3 })
        .Rms({ mode: "moving", windowSize: 2 })
        .Rectify({ mode: "full" });

      // Build state
      await processor.process(
        new Float32Array([1, -2, 3, -4, 5]),
        DEFAULT_OPTIONS
      );

      // Save to Redis
      const stateJson = await processor.saveState();
      await redis.set(stateKey, stateJson);

      const state = JSON.parse(stateJson);
      assert.equal(state.stages.length, 3);

      // Restore in new processor
      const processor2 = createDspPipeline();
      processor2
        .MovingAverage({ mode: "moving", windowSize: 3 })
        .Rms({ mode: "moving", windowSize: 2 })
        .Rectify({ mode: "full" });

      const restoredState = await redis.get(stateKey);
      assert.ok(restoredState);
      await processor2.loadState(restoredState);

      // Verify continuity
      const output1 = await processor.process(
        new Float32Array([6]),
        DEFAULT_OPTIONS
      );
      const output2 = await processor2.process(
        new Float32Array([6]),
        DEFAULT_OPTIONS
      );

      assertCloseTo(output1[0], output2[0]);
    });
  });

  describe("Streaming Scenario", () => {
    test("should maintain state across simulated stream chunks", async () => {
      if (!redisAvailable) return;

      const stateKey = "test:dsp:state:stream";
      const processor = createDspPipeline();
      processor.MovingAverage({ mode: "moving", windowSize: 5 });

      const chunks = [
        new Float32Array([1, 2, 3]),
        new Float32Array([4, 5, 6]),
        new Float32Array([7, 8, 9]),
      ];

      const outputs: Float32Array[] = [];

      for (const chunk of chunks) {
        const output = await processor.process(
          new Float32Array(chunk),
          DEFAULT_OPTIONS
        );
        outputs.push(output);

        // Save state after each chunk
        const state = await processor.saveState();
        await redis.set(stateKey, state);
      }

      // Verify all chunks processed
      assert.equal(outputs.length, 3);

      // Simulate restart and continue processing
      const processor2 = createDspPipeline();
      processor2.MovingAverage({ mode: "moving", windowSize: 5 });

      const savedState = await redis.get(stateKey);
      assert.ok(savedState);
      await processor2.loadState(savedState);

      // Continue with new chunk
      const output1 = await processor.process(
        new Float32Array([10]),
        DEFAULT_OPTIONS
      );
      const output2 = await processor2.process(
        new Float32Array([10]),
        DEFAULT_OPTIONS
      );

      assertCloseTo(output1[0], output2[0]);
    });

    test("should handle rapid save/restore cycles", async () => {
      if (!redisAvailable) return;

      const stateKey = "test:dsp:state:rapid";
      const processor = createDspPipeline();
      processor.Rms({ mode: "moving", windowSize: 3 });

      // Rapidly process and save
      for (let i = 0; i < 10; i++) {
        await processor.process(new Float32Array([i]), DEFAULT_OPTIONS);
        const state = await processor.saveState();
        await redis.set(stateKey, state);
      }

      // Verify final state
      const finalState = await redis.get(stateKey);
      assert.ok(finalState);

      const processor2 = createDspPipeline();
      processor2.Rms({ mode: "moving", windowSize: 3 });
      await processor2.loadState(finalState);

      // Both should be in sync
      const output1 = await processor.process(
        new Float32Array([100]),
        DEFAULT_OPTIONS
      );
      const output2 = await processor2.process(
        new Float32Array([100]),
        DEFAULT_OPTIONS
      );

      assertCloseTo(output1[0], output2[0]);
    });
  });

  describe("Multi-Channel Scenarios", () => {
    test("should persist state for multiple channels", async () => {
      if (!redisAvailable) return;

      const channel1Key = "test:dsp:state:ch1";
      const channel2Key = "test:dsp:state:ch2";

      // Channel 1
      const processor1 = createDspPipeline();
      processor1.MovingAverage({ mode: "moving", windowSize: 2 });
      await processor1.process(new Float32Array([1, 2]), DEFAULT_OPTIONS);
      await redis.set(channel1Key, await processor1.saveState());

      // Channel 2
      const processor2 = createDspPipeline();
      processor2.MovingAverage({ mode: "moving", windowSize: 3 });
      await processor2.process(new Float32Array([3, 4, 5]), DEFAULT_OPTIONS);
      await redis.set(channel2Key, await processor2.saveState());

      // Verify both states exist and are different
      const state1 = await redis.get(channel1Key);
      const state2 = await redis.get(channel2Key);

      assert.ok(state1);
      assert.ok(state2);
      assert.notEqual(state1, state2);

      // Verify different window sizes
      const parsed1 = JSON.parse(state1);
      const parsed2 = JSON.parse(state2);

      assert.equal(parsed1.stages[0].state.windowSize, 2);
      assert.equal(parsed2.stages[0].state.windowSize, 3);
    });
  });

  describe("State Expiration", () => {
    test("should set TTL on state keys for automatic cleanup", async () => {
      if (!redisAvailable) return;

      const stateKey = "test:dsp:state:ttl";
      const processor = createDspPipeline();
      processor.MovingAverage({ mode: "moving", windowSize: 2 });

      await processor.process(new Float32Array([1, 2]), DEFAULT_OPTIONS);
      const state = await processor.saveState();

      // Save with 60 second TTL
      await redis.setEx(stateKey, 60, state);

      // Verify TTL is set
      const ttl = await redis.ttl(stateKey);
      assert.ok(ttl > 0 && ttl <= 60);

      // Verify state can be retrieved
      const retrieved = await redis.get(stateKey);
      assert.equal(retrieved, state);
    });
  });

  describe("Error Handling", () => {
    test("should handle corrupted state in Redis", async () => {
      if (!redisAvailable) return;

      const stateKey = "test:dsp:state:corrupted";
      const processor = createDspPipeline();
      processor.MovingAverage({ mode: "moving", windowSize: 3 });

      // Save corrupted JSON
      await redis.set(stateKey, "{ invalid json }");

      // Should throw when trying to load
      await assert.rejects(async () => {
        const state = await redis.get(stateKey);
        if (state) {
          JSON.parse(state); // This should throw
        }
      });
    });

    test("should handle state validation failure", async () => {
      if (!redisAvailable) return;

      const stateKey = "test:dsp:state:invalid";
      const processor = createDspPipeline();
      processor.MovingAverage({ mode: "moving", windowSize: 3 });

      await processor.process(new Float32Array([1, 2, 3]), DEFAULT_OPTIONS);
      const stateJson = await processor.saveState();
      const state = JSON.parse(stateJson);

      // Corrupt the state
      if (state.stages[0].state.channels && state.stages[0].state.channels[0]) {
        state.stages[0].state.channels[0].runningSum = 9999;
      }

      await redis.set(stateKey, JSON.stringify(state));

      // Should throw validation error
      const processor2 = createDspPipeline({
        fallbackOnLoadFailure: false,
        maxRetries: 0,
      });
      processor2.MovingAverage({ mode: "moving", windowSize: 3 });

      const corruptedState = await redis.get(stateKey);
      assert.ok(corruptedState);

      await assert.rejects(
        async () => await processor2.loadState(corruptedState),
        /Running sum validation failed/
      );
    });
  });

  describe("State Metadata", () => {
    test("should include timestamp in saved state", async () => {
      if (!redisAvailable) return;

      const stateKey = "test:dsp:state:timestamp";
      const processor = createDspPipeline();
      processor.MovingAverage({ mode: "moving", windowSize: 2 });

      await processor.process(new Float32Array([1, 2]), DEFAULT_OPTIONS);
      const stateJson = await processor.saveState();
      await redis.set(stateKey, stateJson);

      const state = JSON.parse(stateJson);
      assert.ok(state.timestamp);
      assert.ok(new Date(state.timestamp).getTime() > 0);
    });

    test("should track state version across updates", async () => {
      if (!redisAvailable) return;

      const stateKey = "test:dsp:state:version";
      const metadataKey = "test:dsp:state:version:meta";
      const processor = createDspPipeline();
      processor.MovingAverage({ mode: "moving", windowSize: 2 });

      // Save multiple versions
      const versions: string[] = [];
      const states: string[] = [];

      for (let i = 0; i < 3; i++) {
        await processor.process(new Float32Array([i]), DEFAULT_OPTIONS);
        const state = await processor.saveState();
        await redis.set(stateKey, state);
        versions.push(JSON.parse(state).timestamp);
        states.push(state);

        // Track version count
        await redis.incr(metadataKey);

        // Small delay to ensure different timestamps
        await new Promise((resolve) => setTimeout(resolve, 10));
      }

      const versionCount = await redis.get(metadataKey);
      assert.equal(versionCount, "3");

      // States should be different (different buffer contents)
      assert.notEqual(states[0], states[1]);
      assert.notEqual(states[1], states[2]);

      // At least some timestamps should be different
      const uniqueTimestamps = new Set(versions);
      assert.ok(uniqueTimestamps.size >= 1, "Should have valid timestamps");
    });
  });
});
