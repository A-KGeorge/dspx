import { describe, test, beforeEach, afterEach } from "node:test";
import assert from "node:assert/strict";
import { createDspPipeline, type DspProcessor } from "../bindings.js";

describe("TOON Binary Serialization", () => {
  let processor: DspProcessor;

  afterEach(() => {
    if (processor) {
      processor.dispose();
    }
  });

  test("should serialize and deserialize ExponentialMovingAverage state in TOON format", async () => {
    // Create pipeline with EMA stage
    processor = createDspPipeline();
    processor.ExponentialMovingAverage({ mode: "moving", alpha: 0.3 });

    // Process some data to build up state
    const input = new Float32Array([1, 2, 3, 4, 5, 6, 7, 8]);
    const result1 = await processor.process(input, {
      channels: 1,
      sampleRate: 1000,
    });

    // Save state in TOON format
    const toonState = await processor.saveState({ format: "toon" });

    // Verify it's a Buffer
    assert.ok(Buffer.isBuffer(toonState), "TOON state should be a Buffer");
    assert.ok(toonState.length > 0, "TOON state should not be empty");

    // Create new processor and load TOON state
    processor.dispose();
    processor = createDspPipeline();
    processor.ExponentialMovingAverage({ mode: "moving", alpha: 0.3 });

    // Load TOON state
    const loaded = await processor.loadState(toonState);
    assert.ok(loaded, "Should successfully load TOON state");

    // Process more data - should continue from previous state
    const input2 = new Float32Array([9, 10, 11, 12]);
    const result2 = await processor.process(input2, {
      channels: 1,
      sampleRate: 1000,
    });

    // Verify state continuity
    assert.ok(
      result2.length > 0,
      "Should process successfully after TOON load"
    );
  });

  test("should serialize and deserialize CumulativeMovingAverage state in TOON format", async () => {
    processor = createDspPipeline();
    processor.CumulativeMovingAverage({ mode: "moving" });

    const input = new Float32Array([10, 20, 30, 40]);
    await processor.process(input, { channels: 1, sampleRate: 1000 });

    // Save in TOON format
    const toonState = await processor.saveState({ format: "toon" });
    assert.ok(Buffer.isBuffer(toonState));

    // Reload
    processor.dispose();
    processor = createDspPipeline();
    processor.CumulativeMovingAverage({ mode: "moving" });
    await processor.loadState(toonState);

    const input2 = new Float32Array([50, 60]);
    const result = await processor.process(input2, {
      channels: 1,
      sampleRate: 1000,
    });

    // CMA should continue: (10+20+30+40+50)/5 = 30, (10+20+30+40+50+60)/6 = 35
    assert.ok(
      Math.abs(result[0] - 30) < 0.1,
      "Should continue cumulative average"
    );
    assert.ok(
      Math.abs(result[1] - 35) < 0.1,
      "Should continue cumulative average"
    );
  });

  test("should serialize MovingAverage with large buffer efficiently in TOON format", async () => {
    // Create MovingAverage with large window (will have big state buffer)
    processor = createDspPipeline();
    processor.MovingAverage({ mode: "moving", windowSize: 1000 });

    // Fill the window
    const largeInput = new Float32Array(2000);
    for (let i = 0; i < largeInput.length; i++) {
      largeInput[i] = Math.sin(i * 0.1);
    }

    await processor.process(largeInput, { channels: 1, sampleRate: 1000 });

    // Save in both formats and compare sizes
    const jsonState = await processor.saveState();
    const toonState = await processor.saveState({ format: "toon" });

    assert.ok(typeof jsonState === "string", "JSON state should be string");
    assert.ok(Buffer.isBuffer(toonState), "TOON state should be Buffer");

    const jsonSize = Buffer.byteLength(jsonState, "utf8");
    const toonSize = toonState.length;

    console.log(`JSON size: ${jsonSize} bytes`);
    console.log(`TOON size: ${toonSize} bytes`);
    console.log(
      `Size reduction: ${((1 - toonSize / jsonSize) * 100).toFixed(1)}%`
    );

    // TOON should be significantly smaller (binary floats vs text)
    assert.ok(
      toonSize < jsonSize,
      "TOON format should be smaller than JSON for large buffers"
    );

    // Verify it deserializes correctly
    processor.dispose();
    processor = createDspPipeline();
    processor.MovingAverage({ mode: "moving", windowSize: 1000 });
    const loaded = await processor.loadState(toonState);
    assert.ok(loaded, "Should load TOON state successfully");

    // Process and verify continuity
    const testInput = new Float32Array([1, 2, 3, 4]);
    const result = await processor.process(testInput, {
      channels: 1,
      sampleRate: 1000,
    });
    assert.ok(
      result.length === 4,
      "Should continue processing after TOON load"
    );
  });

  test("should handle multi-channel state in TOON format", async () => {
    processor = createDspPipeline();
    processor.ExponentialMovingAverage({ mode: "moving", alpha: 0.5 });

    // Process 2-channel data
    const input = new Float32Array([1, 10, 2, 20, 3, 30, 4, 40]); // Interleaved
    await processor.process(input, { channels: 2, sampleRate: 1000 });

    const toonState = await processor.saveState({ format: "toon" });

    // Reload and verify each channel maintains independent state
    processor.dispose();
    processor = createDspPipeline();
    processor.ExponentialMovingAverage({ mode: "moving", alpha: 0.5 });
    await processor.loadState(toonState);

    const input2 = new Float32Array([5, 50, 6, 60]);
    const result = await processor.process(input2, {
      channels: 2,
      sampleRate: 1000,
    });

    assert.ok(result.length === 4, "Should maintain 2 channels");
    // Each channel should continue its own EMA
    assert.ok(
      result[0] !== result[1],
      "Channels should have independent state"
    );
  });

  test("should fall back to JSON format when format option is not specified", async () => {
    processor = createDspPipeline();
    processor.ExponentialMovingAverage({ mode: "moving", alpha: 0.3 });

    const input = new Float32Array([1, 2, 3, 4]);
    await processor.process(input, { channels: 1, sampleRate: 1000 });

    // Default should be JSON string
    const defaultState = await processor.saveState();
    assert.ok(
      typeof defaultState === "string",
      "Default format should be JSON string"
    );

    // Should still be loadable
    processor.dispose();
    processor = createDspPipeline();
    processor.ExponentialMovingAverage({ mode: "moving", alpha: 0.3 });
    const loaded = await processor.loadState(defaultState);
    assert.ok(loaded, "Should load JSON state");
  });

  test("should throw error when loading TOON state into mismatched pipeline", async () => {
    processor = createDspPipeline();
    processor.ExponentialMovingAverage({ mode: "moving", alpha: 0.3 });

    const input = new Float32Array([1, 2, 3, 4]);
    await processor.process(input, { channels: 1, sampleRate: 1000 });

    const toonState = await processor.saveState({ format: "toon" });

    // Try to load into different pipeline structure
    processor.dispose();
    processor = createDspPipeline({
      fallbackOnLoadFailure: false,
      maxRetries: 0,
    });
    processor.CumulativeMovingAverage({ mode: "moving" }); // Different stage type!

    await assert.rejects(
      async () => await processor.loadState(toonState),
      /TOON Load [Ff]ailed|Pipeline structure mismatch/,
      "Should throw error for mismatched pipeline"
    );
  });
});
