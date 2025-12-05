import { describe, test } from "node:test";
import assert from "node:assert/strict";
import { createDspPipeline, FilterBankDesign } from "../bindings.js";

const DEFAULT_OPTIONS = { channels: 1, sampleRate: 16000 };

describe("Filter Bank Stage - Single Test", () => {
  test("should split mono signal into multiple bands", async () => {
    const processor = createDspPipeline();

    const melBank = FilterBankDesign.createMel(4, 16000, [100, 4000]);

    processor.FilterBank({
      definitions: melBank,
      inputChannels: 1,
    });

    const signal = new Float32Array(1000);
    for (let i = 0; i < signal.length; i++) {
      signal[i] = Math.sin((2 * Math.PI * 500 * i) / 16000);
    }

    const output = await processor.process(signal, DEFAULT_OPTIONS);

    // Output should have 4 bands × 1 channel = 4 channels
    assert.equal(output.length, signal.length * 4);

    console.log("Test passed!");
  });
});
