import { describe, test } from "node:test";
import * as assert from "node:assert";
import { createDspPipeline } from "../bindings.js";

describe("MelSpectrogram Stage", () => {
  test("should apply mel spectrogram with simple filterbank", async () => {
    // Create a simple 3x5 filterbank (3 mel bands, 5 frequency bins)
    const filterbank = new Float32Array([
      // Band 0: weights for bins 0-4
      0.5, 1.0, 0.5, 0.0, 0.0,
      // Band 1: weights for bins 0-4
      0.0, 0.5, 1.0, 0.5, 0.0,
      // Band 2: weights for bins 0-4
      0.0, 0.0, 0.5, 1.0, 0.5,
    ]);

    const pipeline = createDspPipeline().melSpectrogram({
      filterbankMatrix: filterbank,
      numBins: 5,
      numMelBands: 3,
    });

    // Create input: 10 samples (2 frames of 5 bins each)
    const input = new Float32Array([1, 2, 3, 4, 5, 5, 4, 3, 2, 1]);

    const result = await pipeline.process(input, {
      sampleRate: 1000,
      channels: 1,
    });

    // Should output 2 frames * 3 mel bands = 6 samples
    assert.strictEqual(result.length, 6);

    // First frame [1,2,3,4,5] with filterbank:
    // Band 0: 0.5*1 + 1.0*2 + 0.5*3 = 0.5 + 2 + 1.5 = 4.0
    // Band 1: 0.5*2 + 1.0*3 + 0.5*4 = 1 + 3 + 2 = 6.0
    // Band 2: 0.5*3 + 1.0*4 + 0.5*5 = 1.5 + 4 + 2.5 = 8.0
    assert.ok(
      Math.abs(result[0] - 4.0) < 0.01,
      `Frame 1 Band 0: expected 4.0, got ${result[0]}`
    );
    assert.ok(
      Math.abs(result[1] - 6.0) < 0.01,
      `Frame 1 Band 1: expected 6.0, got ${result[1]}`
    );
    assert.ok(
      Math.abs(result[2] - 8.0) < 0.01,
      `Frame 1 Band 2: expected 8.0, got ${result[2]}`
    );

    // Second frame [5,4,3,2,1] with filterbank:
    // Band 0: 0.5*5 + 1.0*4 + 0.5*3 = 2.5 + 4 + 1.5 = 8.0
    // Band 1: 0.5*4 + 1.0*3 + 0.5*2 = 2 + 3 + 1 = 6.0
    // Band 2: 0.5*3 + 1.0*2 + 0.5*1 = 1.5 + 2 + 0.5 = 4.0
    assert.ok(
      Math.abs(result[3] - 8.0) < 0.01,
      `Frame 2 Band 0: expected 8.0, got ${result[3]}`
    );
    assert.ok(
      Math.abs(result[4] - 6.0) < 0.01,
      `Frame 2 Band 1: expected 6.0, got ${result[4]}`
    );
    assert.ok(
      Math.abs(result[5] - 4.0) < 0.01,
      `Frame 2 Band 2: expected 4.0, got ${result[5]}`
    );
  });

  test("should handle multi-channel input", async () => {
    // Simple 2x3 filterbank (2 mel bands, 3 frequency bins)
    const filterbank = new Float32Array([1, 0, 0, 0, 0, 1]);

    const pipeline = createDspPipeline().melSpectrogram({
      filterbankMatrix: filterbank,
      numBins: 3,
      numMelBands: 2,
    });

    // 2 channels, 6 samples total (1 frame of 3 bins per channel)
    const input = new Float32Array([1, 10, 2, 20, 3, 30]);

    const result = await pipeline.process(input, {
      sampleRate: 1000,
      channels: 2,
    });

    // Should output 1 frame * 2 mel bands * 2 channels = 4 samples
    assert.strictEqual(result.length, 4);
  });

  test("should validate filterbank dimensions", () => {
    const filterbank = new Float32Array([1, 2, 3, 4]);

    assert.throws(
      () => {
        createDspPipeline().melSpectrogram({
          filterbankMatrix: filterbank,
          numBins: 3,
          numMelBands: 2,
        });
      },
      {
        message: /filterbankMatrix length.*must equal numMelBands.*numBins/i,
      }
    );
  });

  test("should validate numBins is positive", () => {
    const filterbank = new Float32Array([1, 2, 3, 4]);

    assert.throws(
      () => {
        createDspPipeline().melSpectrogram({
          filterbankMatrix: filterbank,
          numBins: 0,
          numMelBands: 2,
        });
      },
      {
        message: /numBins must be.*positive/i,
      }
    );
  });
});

describe("MFCC Stage", () => {
  test("should apply DCT to mel energies", async () => {
    const pipeline = createDspPipeline().mfcc({
      numMelBands: 4,
      numCoefficients: 2,
      useLogEnergy: false, // Disable log for simpler testing
      lifterCoefficient: 0,
    });

    // Input: 4 mel band energies per frame, 2 frames
    const input = new Float32Array([
      // Frame 1
      1, 2, 3, 4,
      // Frame 2
      4, 3, 2, 1,
    ]);

    const result = await pipeline.process(input, {
      sampleRate: 1000,
      channels: 1,
    });

    // Should output 2 frames * 2 coefficients = 4 samples
    assert.strictEqual(result.length, 4);

    // DCT output should be deterministic (though exact values depend on DCT implementation)
    assert.ok(isFinite(result[0]));
    assert.ok(isFinite(result[1]));
    assert.ok(isFinite(result[2]));
    assert.ok(isFinite(result[3]));
  });

  test("should apply log energy by default", async () => {
    const pipeline = createDspPipeline().mfcc({
      numMelBands: 4,
      numCoefficients: 2,
      // useLogEnergy defaults to true
    });

    // Input with positive energies
    const input = new Float32Array([1, 2, 3, 4]);

    const result = await pipeline.process(input, {
      sampleRate: 1000,
      channels: 1,
    });

    assert.strictEqual(result.length, 2);
    assert.ok(isFinite(result[0]));
    assert.ok(isFinite(result[1]));
  });

  test("should apply liftering when specified", async () => {
    const pipeline = createDspPipeline().mfcc({
      numMelBands: 4,
      numCoefficients: 3,
      useLogEnergy: false,
      lifterCoefficient: 22,
    });

    const input = new Float32Array([1, 2, 3, 4]);

    const result = await pipeline.process(input, {
      sampleRate: 1000,
      channels: 1,
    });

    assert.strictEqual(result.length, 3);
    assert.ok(isFinite(result[0]));
    assert.ok(isFinite(result[1]));
    assert.ok(isFinite(result[2]));
  });

  test("should use default numCoefficients = 13", async () => {
    const pipeline = createDspPipeline().mfcc({
      numMelBands: 20,
      // numCoefficients defaults to 13
    });

    const input = new Float32Array(20); // 20 mel bands, 1 frame
    input.fill(1);

    const result = await pipeline.process(input, {
      sampleRate: 1000,
      channels: 1,
    });

    assert.strictEqual(result.length, 13);
  });

  test("should validate numCoefficients <= numMelBands", () => {
    assert.throws(
      () => {
        createDspPipeline().mfcc({
          numMelBands: 10,
          numCoefficients: 15,
        });
      },
      {
        message: /numCoefficients.*numMelBands/i,
      }
    );
  });

  test("should handle multi-channel input", async () => {
    const pipeline = createDspPipeline().mfcc({
      numMelBands: 4,
      numCoefficients: 2,
      useLogEnergy: false,
    });

    // 2 channels, 8 samples total (1 frame of 4 mel bands per channel)
    const input = new Float32Array([1, 10, 2, 20, 3, 30, 4, 40]);

    const result = await pipeline.process(input, {
      sampleRate: 1000,
      channels: 2,
    });

    // Should output 1 frame * 2 coefficients * 2 channels = 4 samples
    assert.strictEqual(result.length, 4);
  });
});

describe("Full Pipeline: STFT -> MelSpectrogram -> MFCC", () => {
  test("should process audio through full feature extraction pipeline", async () => {
    // Create a simple 3x5 filterbank (3 mel bands, 5 frequency bins)
    // For windowSize=8, real FFT outputs 5 bins (8/2+1)
    const filterbank = new Float32Array([
      0.5, 1.0, 0.5, 0.0, 0.0, 0.0, 0.5, 1.0, 0.5, 0.0, 0.0, 0.0, 0.5, 1.0, 0.5,
    ]);

    const pipeline = createDspPipeline()
      .stft({
        windowSize: 8,
        hopSize: 4,
        output: "power",
        window: "hamming",
      })
      .melSpectrogram({
        filterbankMatrix: filterbank,
        numBins: 5,
        numMelBands: 3,
      })
      .mfcc({
        numMelBands: 3,
        numCoefficients: 2,
      });

    // Create 16 samples of audio (will produce multiple STFT frames)
    const audio = new Float32Array(16);
    for (let i = 0; i < audio.length; i++) {
      audio[i] = Math.sin((2 * Math.PI * 440 * i) / 8000);
    }

    const result = await pipeline.process(audio, {
      sampleRate: 8000,
      channels: 1,
    });

    // Should output some MFCC coefficients
    assert.ok(result.length > 0);
    assert.ok(result.length % 2 === 0); // Should be multiples of numCoefficients (2)

    // All values should be finite
    for (let i = 0; i < result.length; i++) {
      assert.ok(
        isFinite(result[i]),
        `Result[${i}] should be finite, got ${result[i]}`
      );
    }
  });

  test("should serialize and deserialize pipeline state", async () => {
    const filterbank = new Float32Array([
      0.5, 1.0, 0.5, 0.0, 0.0, 0.0, 0.5, 1.0, 0.5, 0.0, 0.0, 0.0, 0.5, 1.0, 0.5,
    ]);

    const pipeline = createDspPipeline()
      .stft({
        windowSize: 8,
        hopSize: 4,
        output: "power",
      })
      .melSpectrogram({
        filterbankMatrix: filterbank,
        numBins: 5,
        numMelBands: 3,
      })
      .mfcc({
        numMelBands: 3,
        numCoefficients: 2,
      });

    // Process some audio first to establish state
    const audio1 = new Float32Array(16).fill(1);
    const result1 = await pipeline.process(audio1, {
      sampleRate: 8000,
      channels: 1,
    });

    // Serialize state
    const state = await pipeline.saveState();
    assert.ok(state.length > 0, "State should have data");

    // Create new pipeline and restore state
    const newPipeline = createDspPipeline()
      .stft({
        windowSize: 8,
        hopSize: 4,
        output: "power",
      })
      .melSpectrogram({
        filterbankMatrix: filterbank,
        numBins: 5,
        numMelBands: 3,
      })
      .mfcc({
        numMelBands: 3,
        numCoefficients: 2,
      });

    await newPipeline.loadState(state);

    // Process same audio again - should produce same results
    const audio2 = new Float32Array(16).fill(1);
    const result2 = await newPipeline.process(audio2, {
      sampleRate: 8000,
      channels: 1,
    });

    // Results should match since state was restored
    assert.strictEqual(result1.length, result2.length);
    for (let i = 0; i < result1.length; i++) {
      assert.ok(
        Math.abs(result1[i] - result2[i]) < 0.001,
        `Mismatch at index ${i}: ${result1[i]} vs ${result2[i]}`
      );
    }
  });
});
