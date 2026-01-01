import { describe, test, beforeEach, afterEach } from "node:test";
import assert from "node:assert/strict";
import {
  createDspPipeline,
  DspProcessor,
  FilterBankDesign,
} from "../bindings.js";

const DEFAULT_OPTIONS = { channels: 1, sampleRate: 16000 };

function assertCloseTo(actual: number, expected: number, precision = 4) {
  const tolerance = Math.pow(10, -precision);
  assert.ok(
    Math.abs(actual - expected) < tolerance,
    `Expected ${actual} to be close to ${expected} (tolerance: ${tolerance})`
  );
}

describe("Filter Bank Stage", () => {
  let processor: DspProcessor;

  beforeEach(() => {
    processor = createDspPipeline();
  });

  afterEach(async () => {
    if (processor) {
      processor.dispose();
      processor = null as any;
    }

    // Brief async delay for N-API cleanup
    await new Promise((resolve) => setImmediate(resolve));
  });

  process.on("exit", (code) => {
    if (code !== 0) {
      console.error(
        `Process exiting with code: ${code} (0x${code
          .toString(16)
          .toUpperCase()})`
      );
    }
  });

  process.on("uncaughtException", (err: any) => {
    console.error("Uncaught exception:", err);
    if (err.code) {
      console.error(`Error code: ${err.code}`);
    }
    process.exit(1);
  });

  describe("Basic Functionality", () => {
    test("should split mono signal into multiple bands", async () => {
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
    });

    test("should produce correct channel-major output layout", async () => {
      const melBank = FilterBankDesign.createMel(3, 16000, [100, 4000]);

      processor.FilterBank({
        definitions: melBank,
        inputChannels: 1,
      });

      const signal = new Float32Array(100);
      for (let i = 0; i < signal.length; i++) {
        signal[i] = i; // Simple ramp for pattern verification
      }

      const output = await processor.process(signal, DEFAULT_OPTIONS);

      // Output layout: [Band1_Sample1, Band2_Sample1, Band3_Sample1, Band1_Sample2, ...]
      // 3 bands × 100 samples = 300 total samples
      assert.equal(output.length, 300);
    });

    test("should process stereo input correctly", async () => {
      const melBank = FilterBankDesign.createMel(3, 16000, [100, 4000]);

      processor.FilterBank({
        definitions: melBank,
        inputChannels: 2,
      });

      // Stereo signal: 100 samples per channel = 200 interleaved samples
      const signal = new Float32Array(200);
      for (let i = 0; i < 100; i++) {
        signal[i * 2 + 0] = Math.sin((2 * Math.PI * 500 * i) / 16000); // Left
        signal[i * 2 + 1] = Math.sin((2 * Math.PI * 1500 * i) / 16000); // Right
      }

      const output = await processor.process(signal, {
        sampleRate: 16000,
        channels: 2,
      });

      // Output: 2 input channels × 3 bands = 6 output channels
      // 6 channels × 100 samples = 600 total samples
      assert.equal(output.length, 600);
    });

    test("should handle different filter bank scales", async () => {
      const scales = ["mel", "bark", "linear", "log"] as const;
      const createMethods = {
        mel: FilterBankDesign.createMel.bind(FilterBankDesign),
        bark: FilterBankDesign.createBark.bind(FilterBankDesign),
        linear: FilterBankDesign.createLinear.bind(FilterBankDesign),
        log: FilterBankDesign.createLog.bind(FilterBankDesign),
      } as const;

      for (const scale of scales) {
        const proc = createDspPipeline();
        const filterBank = createMethods[scale](4, 16000, [100, 4000]);

        proc.FilterBank({
          definitions: filterBank,
          inputChannels: 1,
        });

        const signal = new Float32Array(500);
        for (let i = 0; i < signal.length; i++) {
          signal[i] = Math.sin((2 * Math.PI * 1000 * i) / 16000);
        }

        const output = await proc.process(signal, DEFAULT_OPTIONS);
        assert.equal(output.length, signal.length * 4);

        proc.dispose();
        await new Promise((resolve) => setImmediate(resolve));
      }
    });
  });

  describe("Frequency Decomposition", () => {
    test("should attenuate frequencies outside band range", async () => {
      // Create a 2-band filter bank: [100-1000 Hz] and [1000-4000 Hz]
      const filterBank = FilterBankDesign.createMel(2, 16000, [100, 4000]);

      processor.FilterBank({
        definitions: filterBank,
        inputChannels: 1,
      });

      // Test with 500 Hz signal (should pass through band 0)
      const lowFreq = new Float32Array(1000);
      for (let i = 0; i < lowFreq.length; i++) {
        lowFreq[i] = Math.sin((2 * Math.PI * 500 * i) / 16000);
      }

      const lowOutput = await processor.process(lowFreq, DEFAULT_OPTIONS);

      // Band 0 should have significant energy, band 1 should be attenuated
      let band0Energy = 0;
      let band1Energy = 0;
      for (let i = 0; i < 1000; i++) {
        band0Energy += Math.abs(lowOutput[i * 2 + 0]);
        band1Energy += Math.abs(lowOutput[i * 2 + 1]);
      }

      // Band 0 should have more energy than band 1 for 500 Hz
      assert.ok(band0Energy > band1Energy * 2);
    });

    test("should separate multi-frequency signals", async () => {
      // Create 4-band filter bank
      const filterBank = FilterBankDesign.createMel(4, 16000, [100, 4000]);

      processor.FilterBank({
        definitions: filterBank,
        inputChannels: 1,
      });

      // Signal with 200 Hz + 800 Hz + 1500 Hz + 3000 Hz
      const signal = new Float32Array(2000);
      for (let i = 0; i < signal.length; i++) {
        signal[i] =
          Math.sin((2 * Math.PI * 200 * i) / 16000) +
          Math.sin((2 * Math.PI * 800 * i) / 16000) +
          Math.sin((2 * Math.PI * 1500 * i) / 16000) +
          Math.sin((2 * Math.PI * 3000 * i) / 16000);
      }

      const output = await processor.process(signal, DEFAULT_OPTIONS);

      // Calculate energy per band
      const bandEnergies = [0, 0, 0, 0];
      for (let i = 0; i < 2000; i++) {
        for (let band = 0; band < 4; band++) {
          bandEnergies[band] += output[i * 4 + band] ** 2;
        }
      }

      // All bands should have captured some energy
      for (let band = 0; band < 4; band++) {
        assert.ok(bandEnergies[band] > 0, `Band ${band} should have energy`);
      }

      // Energy should be relatively distributed (no single band dominates)
      const totalEnergy = bandEnergies.reduce((a, b) => a + b, 0);
      const maxBandEnergy = Math.max(...bandEnergies);
      assert.ok(
        maxBandEnergy < totalEnergy * 0.6,
        "Energy should be distributed across bands"
      );
    });
  });

  describe("Multi-Channel Processing", () => {
    test("should apply same filter bank to all input channels", async () => {
      const filterBank = FilterBankDesign.createMel(4, 16000, [100, 4000]);

      processor.FilterBank({
        definitions: filterBank,
        inputChannels: 2,
      });

      // Stereo with identical signals
      const signal = new Float32Array(200);
      for (let i = 0; i < 100; i++) {
        const val = Math.sin((2 * Math.PI * 1000 * i) / 16000);
        signal[i * 2 + 0] = val; // Left
        signal[i * 2 + 1] = val; // Right (same)
      }

      const output = await processor.process(signal, {
        sampleRate: 16000,
        channels: 2,
      });

      // Output layout: [L_B1, L_B2, L_B3, L_B4, R_B1, R_B2, R_B3, R_B4, ...]
      // First 4 values (left bands) should match next 4 values (right bands)
      for (let sample = 0; sample < 10; sample++) {
        for (let band = 0; band < 4; band++) {
          const leftBand = output[sample * 8 + band];
          const rightBand = output[sample * 8 + 4 + band];
          assertCloseTo(leftBand, rightBand, 3);
        }
      }
    });

    test("should handle different signals per channel", async () => {
      const filterBank = FilterBankDesign.createMel(3, 16000, [100, 4000]);

      processor.FilterBank({
        definitions: filterBank,
        inputChannels: 2,
      });

      // Left: 500 Hz, Right: 2000 Hz
      const signal = new Float32Array(200);
      for (let i = 0; i < 100; i++) {
        signal[i * 2 + 0] = Math.sin((2 * Math.PI * 500 * i) / 16000);
        signal[i * 2 + 1] = Math.sin((2 * Math.PI * 2000 * i) / 16000);
      }

      const output = await processor.process(signal, {
        sampleRate: 16000,
        channels: 2,
      });

      // Calculate energy for left and right channel bands
      const leftBandEnergies = [0, 0, 0];
      const rightBandEnergies = [0, 0, 0];

      for (let i = 0; i < 100; i++) {
        for (let band = 0; band < 3; band++) {
          leftBandEnergies[band] += output[i * 6 + band] ** 2;
          rightBandEnergies[band] += output[i * 6 + 3 + band] ** 2;
        }
      }

      // Energy distributions should be different
      const leftMax = Math.max(...leftBandEnergies);
      const rightMax = Math.max(...rightBandEnergies);
      const leftMaxBand = leftBandEnergies.indexOf(leftMax);
      const rightMaxBand = rightBandEnergies.indexOf(rightMax);

      // 500 Hz and 2000 Hz should peak in different bands
      assert.notEqual(leftMaxBand, rightMaxBand);
    });
  });

  describe("State Management", () => {
    test("should maintain IIR filter state across process calls", async () => {
      const filterBank = FilterBankDesign.createMel(4, 16000, [100, 4000]);

      processor.FilterBank({
        definitions: filterBank,
        inputChannels: 1,
      });

      // First batch
      const signal1 = new Float32Array(500);
      for (let i = 0; i < signal1.length; i++) {
        signal1[i] = Math.sin((2 * Math.PI * 1000 * i) / 16000);
      }
      await processor.process(signal1, DEFAULT_OPTIONS);

      // Second batch - should continue from previous state
      const signal2 = new Float32Array(500);
      for (let i = 0; i < signal2.length; i++) {
        signal2[i] = Math.sin((2 * Math.PI * 1000 * (i + 500)) / 16000);
      }
      const output2 = await processor.process(signal2, DEFAULT_OPTIONS);

      // Output should be continuous (no discontinuities)
      assert.ok(output2.length > 0);
    });

    test("should clear state when clearState is called", async () => {
      const filterBank = FilterBankDesign.createMel(4, 16000, [100, 4000]);

      processor.FilterBank({
        definitions: filterBank,
        inputChannels: 1,
      });

      // Build state
      const signal1 = new Float32Array(500);
      for (let i = 0; i < signal1.length; i++) {
        signal1[i] = Math.sin((2 * Math.PI * 1000 * i) / 16000);
      }
      const output1 = await processor.process(signal1, DEFAULT_OPTIONS);

      // Clear state
      processor.clearState();

      // Process same signal again - should produce same output as first time
      const output2 = await processor.process(signal1, DEFAULT_OPTIONS);

      // First few samples should match (transient response)
      for (let i = 0; i < 20; i++) {
        assertCloseTo(output1[i], output2[i], 2);
      }
    });
  });

  describe("Pipeline Chaining", () => {
    test("should chain with RMS to get band envelopes", async () => {
      const filterBank = FilterBankDesign.createMel(4, 16000, [100, 4000]);

      processor
        .FilterBank({
          definitions: filterBank,
          inputChannels: 1,
        })
        .Rms({ mode: "moving", windowSize: 128 });

      const signal = new Float32Array(1000);
      for (let i = 0; i < signal.length; i++) {
        signal[i] = Math.sin((2 * Math.PI * 1000 * i) / 16000);
      }

      const output = await processor.process(signal, DEFAULT_OPTIONS);

      // RMS should smooth the band outputs
      assert.equal(output.length, signal.length * 4);

      // All values should be non-negative (RMS property)
      for (let i = 0; i < output.length; i++) {
        assert.ok(output[i] >= 0);
      }
    });

    test("should work with channel manipulation stages", async () => {
      const filterBank = FilterBankDesign.createMel(3, 16000, [100, 4000]);

      // Filter bank: 1 input → 3 bands
      // Then select only band 2 (channel index 1)
      processor
        .FilterBank({
          definitions: filterBank,
          inputChannels: 1,
        })
        .ChannelSelect({
          channels: [1],
          numInputChannels: 3,
        });

      const signal = new Float32Array(500);
      for (let i = 0; i < signal.length; i++) {
        signal[i] = Math.sin((2 * Math.PI * 1000 * i) / 16000);
      }

      const output = await processor.process(signal, DEFAULT_OPTIONS);

      // Should output only 1 channel (the selected band)
      assert.equal(output.length, signal.length);
    });

    test("should work before and after other stages", async () => {
      const filterBank = FilterBankDesign.createMel(2, 16000, [100, 4000]);

      processor
        .Rectify({ mode: "full" })
        .FilterBank({
          definitions: filterBank,
          inputChannels: 1,
        })
        .MovingAverage({ mode: "moving", windowSize: 32 });

      const signal = new Float32Array(500);
      for (let i = 0; i < signal.length; i++) {
        signal[i] = Math.sin((2 * Math.PI * 1000 * i) / 16000);
      }

      const output = await processor.process(signal, DEFAULT_OPTIONS);

      // Rectify → FilterBank (2 bands) → MovingAverage
      assert.equal(output.length, signal.length * 2);
    });
  });

  describe("Error Handling", () => {
    test("should throw error for empty definitions array", () => {
      assert.throws(() => {
        processor.FilterBank({
          definitions: [],
          inputChannels: 1,
        });
      }, /definitions must be a non-empty array/);
    });

    test("should throw error for invalid inputChannels", () => {
      const filterBank = FilterBankDesign.createMel(4, 16000, [100, 4000]);

      assert.throws(() => {
        processor.FilterBank({
          definitions: filterBank,
          inputChannels: 0,
        });
      }, /inputChannels must be a positive integer/);

      assert.throws(() => {
        processor.FilterBank({
          definitions: filterBank,
          inputChannels: -1,
        });
      }, /inputChannels must be a positive integer/);
    });

    test("should throw error for invalid filter definition", () => {
      assert.throws(() => {
        processor.FilterBank({
          definitions: [{ b: [], a: [1] }],
          inputChannels: 1,
        });
      }, /definition\[0\]\.b must be a non-empty array/);

      assert.throws(() => {
        processor.FilterBank({
          definitions: [{ b: [1], a: [] }],
          inputChannels: 1,
        });
      }, /definition\[0\]\.a must be a non-empty array/);
    });

    test("should throw error for mismatched channel count in process", async () => {
      const filterBank = FilterBankDesign.createMel(4, 16000, [100, 4000]);

      processor.FilterBank({
        definitions: filterBank,
        inputChannels: 2, // Expects stereo
      });

      // But provide mono signal
      const signal = new Float32Array(1000);

      await assert.rejects(async () => {
        await processor.process(signal, { sampleRate: 16000, channels: 1 });
      }, /channel/i);
    });
  });

  describe("Edge Cases", () => {
    test("should handle single band filter bank", async () => {
      const filterBank = FilterBankDesign.createMel(1, 16000, [100, 4000]);

      processor.FilterBank({
        definitions: filterBank,
        inputChannels: 1,
      });

      const signal = new Float32Array(500);
      for (let i = 0; i < signal.length; i++) {
        signal[i] = Math.sin((2 * Math.PI * 1000 * i) / 16000);
      }

      const output = await processor.process(signal, DEFAULT_OPTIONS);

      // 1 band × 1 channel = 1 output channel
      assert.equal(output.length, signal.length);
    });

    test("should handle large number of bands", async () => {
      const filterBank = FilterBankDesign.createMel(32, 16000, [100, 8000]);

      processor.FilterBank({
        definitions: filterBank,
        inputChannels: 1,
      });

      const signal = new Float32Array(1000);
      for (let i = 0; i < signal.length; i++) {
        signal[i] = Math.sin((2 * Math.PI * 1000 * i) / 16000);
      }

      const output = await processor.process(signal, DEFAULT_OPTIONS);

      // 32 bands × 1 channel = 32 output channels
      assert.equal(output.length, signal.length * 32);

      // Check that output is finite
      for (let i = 0; i < output.length; i++) {
        assert.ok(Number.isFinite(output[i]));
      }
    });

    test("should handle very short signals", async () => {
      const filterBank = FilterBankDesign.createMel(4, 16000, [100, 4000]);

      processor.FilterBank({
        definitions: filterBank,
        inputChannels: 1,
      });

      const signal = new Float32Array(10);
      for (let i = 0; i < signal.length; i++) {
        signal[i] = Math.sin((2 * Math.PI * 1000 * i) / 16000);
      }

      const output = await processor.process(signal, DEFAULT_OPTIONS);

      assert.equal(output.length, signal.length * 4);
    });

    test("should handle zero signal", async () => {
      const filterBank = FilterBankDesign.createMel(4, 16000, [100, 4000]);

      processor.FilterBank({
        definitions: filterBank,
        inputChannels: 1,
      });

      const signal = new Float32Array(500);
      // All zeros

      const output = await processor.process(signal, DEFAULT_OPTIONS);

      // Output should also be near zero (filter initial transient might be non-zero)
      assert.equal(output.length, signal.length * 4);
    });
  });

  describe("Performance", () => {
    test("should handle large signals efficiently", async () => {
      const filterBank = FilterBankDesign.createMel(16, 16000, [100, 8000]);

      processor.FilterBank({
        definitions: filterBank,
        inputChannels: 2,
      });

      // Large stereo signal: 8000 samples per channel
      const signal = new Float32Array(16000);
      for (let i = 0; i < 8000; i++) {
        signal[i * 2 + 0] = Math.sin((2 * Math.PI * 1000 * i) / 16000);
        signal[i * 2 + 1] = Math.sin((2 * Math.PI * 2000 * i) / 16000);
      }

      const startTime = Date.now();
      const output = await processor.process(signal, {
        sampleRate: 16000,
        channels: 2,
      });
      const elapsedTime = Date.now() - startTime;

      // Should complete in reasonable time (< 1 second)
      assert.ok(elapsedTime < 1000, `Processing took ${elapsedTime}ms`);

      // Output: 2 channels × 16 bands × 8000 samples = 256000 samples
      assert.equal(output.length, 8000 * 2 * 16);
    });

    test("should process multiple batches efficiently", async () => {
      const filterBank = FilterBankDesign.createMel(8, 16000, [100, 4000]);

      processor.FilterBank({
        definitions: filterBank,
        inputChannels: 1,
      });

      const batchSize = 1000;
      const numBatches = 10;

      for (let batch = 0; batch < numBatches; batch++) {
        const signal = new Float32Array(batchSize);
        for (let i = 0; i < batchSize; i++) {
          const t = batch * batchSize + i;
          signal[i] = Math.sin((2 * Math.PI * 1000 * t) / 16000);
        }

        const output = await processor.process(signal, DEFAULT_OPTIONS);
        assert.equal(output.length, batchSize * 8);
      }
    });
  });

  describe("Integration with FilterBankDesign", () => {
    test("should work with all scale types from FilterBankDesign", async () => {
      const scales = ["mel", "bark", "linear", "log"] as const;
      const createMethods = {
        mel: FilterBankDesign.createMel.bind(FilterBankDesign),
        bark: FilterBankDesign.createBark.bind(FilterBankDesign),
        linear: FilterBankDesign.createLinear.bind(FilterBankDesign),
        log: FilterBankDesign.createLog.bind(FilterBankDesign),
      } as const;

      for (const scale of scales) {
        const proc = createDspPipeline();
        const filterBank = createMethods[scale](8, 16000, [100, 4000]);

        proc.FilterBank({
          definitions: filterBank,
          inputChannels: 1,
        });

        const signal = new Float32Array(500);
        for (let i = 0; i < signal.length; i++) {
          signal[i] = Math.sin((2 * Math.PI * 1000 * i) / 16000);
        }

        const output = await proc.process(signal, DEFAULT_OPTIONS);
        assert.equal(output.length, signal.length * 8);

        proc.dispose();
        await new Promise((resolve) => setImmediate(resolve));
      }
    });

    test("should handle different band counts", async () => {
      const bandCounts = [2, 4, 8, 16, 24];

      for (const numBands of bandCounts) {
        const proc = createDspPipeline();
        const filterBank = FilterBankDesign.createMel(
          numBands,
          16000,
          [100, 4000]
        );

        proc.FilterBank({
          definitions: filterBank,
          inputChannels: 1,
        });

        const signal = new Float32Array(500);
        for (let i = 0; i < signal.length; i++) {
          signal[i] = Math.sin((2 * Math.PI * 1000 * i) / 16000);
        }

        const output = await proc.process(signal, DEFAULT_OPTIONS);
        assert.equal(output.length, signal.length * numBands);

        proc.dispose();
        await new Promise((resolve) => setImmediate(resolve));
      }
    });

    test("should handle different frequency ranges", async () => {
      const ranges: Array<[number, number]> = [
        [100, 8000],
        [200, 4000],
        [500, 2000],
        [50, 7000],
      ];

      for (const [fMin, fMax] of ranges) {
        const proc = createDspPipeline();
        const filterBank = FilterBankDesign.createMel(8, 16000, [fMin, fMax]);

        proc.FilterBank({
          definitions: filterBank,
          inputChannels: 1,
        });

        const signal = new Float32Array(500);
        for (let i = 0; i < signal.length; i++) {
          signal[i] = Math.sin((2 * Math.PI * 1000 * i) / 16000);
        }

        const output = await proc.process(signal, DEFAULT_OPTIONS);
        assert.equal(output.length, signal.length * 8);

        proc.dispose();
        await new Promise((resolve) => setImmediate(resolve));
      }
    });
  });
});
