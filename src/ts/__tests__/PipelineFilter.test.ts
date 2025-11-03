import { describe, test } from "node:test";
import assert from "node:assert";
import { createDspPipeline } from "../bindings.js";
import { IirFilter, FirFilter } from "../filters.js";
import { generateSineWave } from "./test-utils.js";

describe("Pipeline Filter Stage", () => {
  test("should produce the same output as a standalone filter", async () => {
    const sampleRate = 8000;
    const input = generateSineWave(100, sampleRate, 512);

    // Standalone filter
    const standaloneFilter = IirFilter.createButterworthLowPass({
      cutoffFrequency: 500,
      sampleRate,
      order: 4,
    });
    const standaloneOutput = await standaloneFilter.process(
      new Float32Array(input)
    );

    // Pipeline filter
    const pipeline = createDspPipeline();
    pipeline.filter({
      type: "butterworth",
      mode: "lowpass",
      cutoffFrequency: 500,
      sampleRate,
      order: 4,
    });
    const pipelineOutput = await pipeline.process(new Float32Array(input), {
      sampleRate,
    });

    // Skip the initial transient period where filters might differ
    const skipSamples = 50;
    const standaloneSliced = Array.from(standaloneOutput.slice(skipSamples));
    const pipelineSliced = Array.from(pipelineOutput.slice(skipSamples));

    for (let i = 0; i < standaloneSliced.length; i++) {
      assert.strictEqual(
        pipelineSliced[i].toPrecision(6),
        standaloneSliced[i].toPrecision(6),
        `Mismatch at index ${i + skipSamples}`
      );
    }
  });

  test("should produce the same output as a standalone FIR filter", async () => {
    const sampleRate = 1000;
    const cutoffFrequency = 100;

    const processor = createDspPipeline();
    processor.filter({
      type: "fir",
      mode: "lowpass",
      order: 51,
      cutoffFrequency,
      sampleRate,
    });

    const standaloneFilter = FirFilter.createLowPass({
      cutoffFrequency,
      sampleRate,
      order: 51,
    });

    const input = new Float32Array(200).map((_, i) =>
      Math.sin((i / 50) * Math.PI * 2)
    );
    const inputCopy = new Float32Array(input); // Create a copy for standalone filter
    // Pass empty options to avoid auto-generating timestamps
    const pipelineOutput = await processor.process(input, {});
    const standaloneOutput = await standaloneFilter.process(inputCopy);

    // FIR filters have a transient period equal to the filter order
    const skipSamples = 51;
    const standaloneSliced = Array.from(standaloneOutput.slice(skipSamples));
    const pipelineSliced = Array.from(pipelineOutput.slice(skipSamples));

    assert.strictEqual(
      pipelineSliced.length,
      standaloneSliced.length,
      "Output lengths should match after slicing"
    );

    for (let i = 0; i < standaloneSliced.length; i++) {
      // Use relative tolerance to account for float32 vs float64 precision
      const relative_error = Math.abs(
        (pipelineSliced[i] - standaloneSliced[i]) / standaloneSliced[i]
      );
      assert.ok(
        relative_error < 1e-4,
        `Mismatch at index ${i + skipSamples}: expected ${
          standaloneSliced[i]
        }, got ${
          pipelineSliced[i]
        } (relative error: ${relative_error.toExponential(2)})`
      );
    }
  });

  test("should chain two filters correctly", async () => {
    const processor = createDspPipeline();
    processor.filter({
      type: "butterworth",
      mode: "lowpass",
      order: 4,
      sampleRate: 1000,
      cutoffFrequency: 100,
    });
    processor.filter({
      type: "fir",
      mode: "highpass",
      order: 3,
      sampleRate: 1000,
      cutoffFrequency: 20,
    });

    const input = new Float32Array(200).map((_, i) =>
      Math.sin((i / 50) * Math.PI * 4)
    );
    const output = await processor.process(input, { sampleRate: 1000 });

    // The output should not be all zeros, indicating processing happened
    const sum = output.reduce((a, b) => a + Math.abs(b), 0);
    assert.ok(sum > 0, "Output should not be all zeros");
    assert.strictEqual(
      output.length,
      input.length,
      "Output length should match input length"
    );
  });

  test("should save and restore filter state", async () => {
    const filterConfig = {
      type: "butterworth" as const,
      mode: "lowpass" as const,
      order: 4,
      sampleRate: 1000,
      cutoffFrequency: 100,
    };

    const processor = createDspPipeline();
    processor.filter(filterConfig);

    const input1 = new Float32Array(50).fill(1);
    await processor.process(input1, { sampleRate: 1000 });

    const state = await processor.saveState();

    // Create a new processor with same pipeline structure and load state
    const processor2 = createDspPipeline();
    processor2.filter(filterConfig);
    await processor2.loadState(state);

    // Create separate input arrays to avoid buffer reuse (process() modifies in-place)
    const input2a = new Float32Array(50).fill(1);
    const input2b = new Float32Array(50).fill(1);
    const output1 = await processor.process(input2a, { sampleRate: 1000 });
    const output2 = await processor2.process(input2b, { sampleRate: 1000 });

    assert.deepStrictEqual(
      output1,
      output2,
      "Outputs should be identical after restoring state"
    );
  });

  test("should handle multi-channel processing", async () => {
    const processor = createDspPipeline();
    const filterConfig = {
      type: "butterworth" as const,
      mode: "highpass" as const,
      order: 2,
      sampleRate: 1000,
      cutoffFrequency: 200,
    };
    processor.filter(filterConfig);

    const input = [
      new Float32Array(100).map((_, i) => Math.sin((i / 50) * Math.PI)), // Channel 1
      new Float32Array(100).map((_, i) => Math.cos((i / 50) * Math.PI)), // Channel 2
    ];

    const interleavedInput = new Float32Array(200);
    for (let i = 0; i < 100; i++) {
      interleavedInput[i * 2] = input[0][i];
      interleavedInput[i * 2 + 1] = input[1][i];
    }

    const pipelineOutputInterleaved = await processor.process(
      interleavedInput,
      { channels: 2, sampleRate: 1000 }
    );

    const output = [new Float32Array(100), new Float32Array(100)];
    for (let i = 0; i < 100; i++) {
      output[0][i] = pipelineOutputInterleaved[i * 2];
      output[1][i] = pipelineOutputInterleaved[i * 2 + 1];
    }

    assert.strictEqual(output.length, 2, "Should output two channels");
    assert.strictEqual(
      output[0].length,
      100,
      "Channel 1 length should be correct"
    );
    assert.strictEqual(
      output[1].length,
      100,
      "Channel 2 length should be correct"
    );

    // Compare channel 1 against a standalone filter
    const standaloneFilter = IirFilter.createButterworthHighPass(filterConfig);
    const standaloneOutput = await standaloneFilter.process(input[0]);

    const skipSamples = 50;
    const pipelineSli = Array.from(output[0].slice(skipSamples));
    const standaloneSli = Array.from(standaloneOutput.slice(skipSamples));

    for (let i = 0; i < pipelineSli.length; i++) {
      assert.strictEqual(
        pipelineSli[i].toPrecision(6),
        standaloneSli[i].toPrecision(6),
        `Channel 1 mismatch at index ${i + skipSamples}`
      );
    }
  });
});
