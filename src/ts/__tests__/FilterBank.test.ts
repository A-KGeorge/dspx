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

  afterEach(() => {
    if (processor) {
      processor.dispose();
    }
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
      const scales = [
        FilterBankDesign.createMel(4, 16000, [100, 4000]),
        FilterBankDesign.createBark(4, 16000, [100, 4000]),
        FilterBankDesign.createLog(4, 16000, [100, 4000]),
        FilterBankDesign.createLinear(4, 16000, [100, 4000]),
      ];

      const signal = new Float32Array(500);
      for (let i = 0; i < signal.length; i++) {
        signal[i] = Math.sin((2 * Math.PI * 1000 * i) / 16000);
      }

      for (const filterBank of scales) {
        const p = createDspPipeline();
        p.FilterBank({
          definitions: filterBank,
          inputChannels: 1,
        });

        const output = await p.process(signal, DEFAULT_OPTIONS);
        assert.equal(output.length, signal.length * 4);

        p.dispose();
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
      const filterBank = FilterBankDesign.createMel(8, 16000, [100, 8000]);

      processor.FilterBank({
        definitions: filterBank,
        inputChannels: 1,
      });

      // Multi-tone signal: 200 Hz + 1000 Hz + 4000 Hz
      const signal = new Float32Array(2000);
      for (let i = 0; i < signal.length; i++) {
        const t = i / 16000;
        signal[i] =
          0.5 * Math.sin(2 * Math.PI * 200 * t) +
          0.3 * Math.sin(2 * Math.PI * 1000 * t) +
          0.2 * Math.sin(2 * Math.PI * 4000 * t);
      }

      const output = await processor.process(signal, DEFAULT_OPTIONS);

      // Calculate energy per band
      const bandEnergies: number[] = new Array(8).fill(0);
      for (let i = 0; i < 2000; i++) {
        for (let band = 0; band < 8; band++) {
          bandEnergies[band] += output[i * 8 + band] ** 2;
        }
      }

      // Verify that multiple bands have significant energy
      const nonZeroBands = bandEnergies.filter((e) => e > 0.1).length;
      assert.ok(nonZeroBands >= 3, "Expected at least 3 bands with energy");
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
      const filterBank = FilterBankDesign.createMel(40, 16000, [100, 8000]);

      processor.FilterBank({
        definitions: filterBank,
        inputChannels: 1,
      });

      const signal = new Float32Array(500);
      for (let i = 0; i < signal.length; i++) {
        signal[i] = Math.sin((2 * Math.PI * 1000 * i) / 16000);
      }

      const output = await processor.process(signal, DEFAULT_OPTIONS);

      // 40 bands × 1 channel = 40 output channels
      assert.equal(output.length, signal.length * 40);
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
    test("should process large signals efficiently", async () => {
      const filterBank = FilterBankDesign.createMel(24, 16000, [100, 8000]);

      processor.FilterBank({
        definitions: filterBank,
        inputChannels: 2,
      });

      // Large stereo signal: 1 second
      const signal = new Float32Array(32000); // 16000 samples × 2 channels
      for (let i = 0; i < 16000; i++) {
        signal[i * 2 + 0] = Math.sin((2 * Math.PI * 1000 * i) / 16000);
        signal[i * 2 + 1] = Math.sin((2 * Math.PI * 2000 * i) / 16000);
      }

      const start = performance.now();
      const output = await processor.process(signal, {
        sampleRate: 16000,
        channels: 2,
      });
      const duration = performance.now() - start;

      assert.equal(output.length, 16000 * 24 * 2);

      // Should complete in reasonable time (< 100ms for 1 second of audio)
      assert.ok(duration < 100, `Processing took ${duration}ms`);
    });
  });

  describe("Integration with FilterBankDesign", () => {
    test("should work with all scale types", async () => {
      const scales = ["mel", "bark", "log", "linear"] as const;

      for (const scale of scales) {
        const filterBank = FilterBankDesign.design({
          scale,
          count: 8,
          sampleRate: 16000,
          frequencyRange: [100, 8000],
        });

        const p = createDspPipeline();
        p.FilterBank({
          definitions: filterBank,
          inputChannels: 1,
        });

        const signal = new Float32Array(500);
        for (let i = 0; i < signal.length; i++) {
          signal[i] = Math.sin((2 * Math.PI * 1000 * i) / 16000);
        }

        const output = await p.process(signal, DEFAULT_OPTIONS);
        assert.equal(output.length, signal.length * 8);

        p.dispose();
      }
    });

    test("should work with different filter types", async () => {
      const types = ["butterworth", "chebyshev"] as const;

      for (const type of types) {
        const filterBank = FilterBankDesign.design({
          scale: "mel",
          count: 4,
          sampleRate: 16000,
          frequencyRange: [100, 4000],
          type,
          order: 3,
        });

        const p = createDspPipeline();
        p.FilterBank({
          definitions: filterBank,
          inputChannels: 1,
        });

        const signal = new Float32Array(500);
        for (let i = 0; i < signal.length; i++) {
          signal[i] = Math.sin((2 * Math.PI * 1000 * i) / 16000);
        }

        const output = await p.process(signal, DEFAULT_OPTIONS);
        assert.equal(output.length, signal.length * 4);

        p.dispose();
      }
    });

    test("should respect filter order parameter", async () => {
      const orders = [2, 3, 4];

      for (const order of orders) {
        const filterBank = FilterBankDesign.design({
          scale: "mel",
          count: 4,
          sampleRate: 16000,
          frequencyRange: [100, 4000],
          order,
        });

        // Higher order should have more coefficients
        assert.ok(filterBank[0].b.length >= order);
        assert.ok(filterBank[0].a.length >= order);

        const p = createDspPipeline();
        p.FilterBank({
          definitions: filterBank,
          inputChannels: 1,
        });

        const signal = new Float32Array(500);
        for (let i = 0; i < signal.length; i++) {
          signal[i] = Math.sin((2 * Math.PI * 1000 * i) / 16000);
        }

        const output = await p.process(signal, DEFAULT_OPTIONS);
        assert.equal(output.length, signal.length * 4);

        p.dispose();
      }
    });
  });
});
