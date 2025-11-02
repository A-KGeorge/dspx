import { describe, test } from "node:test";
import assert from "node:assert";
import { createDspPipeline } from "../bindings.js";

describe("Resampling Operations", () => {
  describe("Interpolate (Upsample)", () => {
    test("should validate interpolation factor", () => {
      const pipeline = createDspPipeline();

      assert.throws(
        () => {
          pipeline.Interpolate({ factor: 1, sampleRate: 1000 });
        },
        /factor must be an integer >= 2/,
        "Should reject factor < 2"
      );

      assert.throws(
        () => {
          pipeline.Interpolate({ factor: 2.5, sampleRate: 1000 });
        },
        /factor must be an integer >= 2/,
        "Should reject non-integer factor"
      );
    });

    test("should validate sample rate", () => {
      const pipeline = createDspPipeline();

      assert.throws(
        () => {
          pipeline.Interpolate({ factor: 2, sampleRate: 0 });
        },
        /sampleRate must be positive/,
        "Should reject zero sample rate"
      );

      assert.throws(
        () => {
          pipeline.Interpolate({ factor: 2, sampleRate: -100 });
        },
        /sampleRate must be positive/,
        "Should reject negative sample rate"
      );
    });

    test("should validate filter order", () => {
      const pipeline = createDspPipeline();

      assert.throws(
        () => {
          pipeline.Interpolate({ factor: 2, sampleRate: 1000, order: 2 });
        },
        /order must be odd and >= 3/,
        "Should reject order < 3"
      );

      assert.throws(
        () => {
          pipeline.Interpolate({ factor: 2, sampleRate: 1000, order: 50 });
        },
        /order must be odd and >= 3/,
        "Should reject even order"
      );
    });

    test("should interpolate signal by factor of 2", async () => {
      const pipeline = createDspPipeline();
      pipeline.Interpolate({ factor: 2, sampleRate: 1000 });

      // Create test signal: 100 samples of sine wave
      const input = new Float32Array(100);
      for (let i = 0; i < input.length; i++) {
        input[i] = Math.sin((2 * Math.PI * 5 * i) / 1000);
      }

      const output = await pipeline.process(input, { sampleRate: 1000 });

      // Output should be 2x input length (200 samples)
      assert.ok(
        output.length >= 195 && output.length <= 205,
        `Output length ${output.length} should be close to ${input.length * 2}`
      );

      // Verify signal amplitude is preserved
      const maxOutput = Math.max(...Array.from(output).map(Math.abs));
      assert.ok(
        maxOutput > 0.9 && maxOutput < 1.1,
        "Signal amplitude should be preserved"
      );
    });
  });

  describe("Decimate (Downsample)", () => {
    test("should validate decimation factor", () => {
      const pipeline = createDspPipeline();

      assert.throws(
        () => {
          pipeline.Decimate({ factor: 1, sampleRate: 1000 });
        },
        /factor must be an integer >= 2/,
        "Should reject factor < 2"
      );

      assert.throws(
        () => {
          pipeline.Decimate({ factor: 3.5, sampleRate: 1000 });
        },
        /factor must be an integer >= 2/,
        "Should reject non-integer factor"
      );
    });

    test("should validate sample rate", () => {
      const pipeline = createDspPipeline();

      assert.throws(
        () => {
          pipeline.Decimate({ factor: 2, sampleRate: 0 });
        },
        /sampleRate must be positive/,
        "Should reject zero sample rate"
      );
    });

    test("should validate filter order", () => {
      const pipeline = createDspPipeline();

      assert.throws(
        () => {
          pipeline.Decimate({ factor: 2, sampleRate: 1000, order: 4 });
        },
        /order must be odd and >= 3/,
        "Should reject even order"
      );
    });

    test("should decimate signal by factor of 2", async () => {
      const pipeline = createDspPipeline();
      pipeline.Decimate({ factor: 2, sampleRate: 1000 });

      const input = new Float32Array(100).map((_, i) =>
        Math.sin((2 * Math.PI * 5 * i) / 1000)
      );

      const output = await pipeline.process(input, { sampleRate: 1000 });

      // Expected output: ~50 samples (100 / 2)
      assert.ok(
        output.length >= 45 && output.length <= 55,
        `Output length ${output.length} should be close to ${input.length / 2}`
      );

      // Verify we got actual signal data
      const maxOutput = Math.max(...Array.from(output).map(Math.abs));
      assert.ok(maxOutput > 0, "Output should contain signal data");
    });
  });

  describe("Resample (Rational Rate Conversion)", () => {
    test("should validate up/down factors", () => {
      const pipeline = createDspPipeline();

      assert.throws(
        () => {
          pipeline.Resample({ upFactor: 0, downFactor: 2, sampleRate: 1000 });
        },
        /upFactor must be a positive integer/,
        "Should reject upFactor = 0"
      );

      assert.throws(
        () => {
          pipeline.Resample({ upFactor: 2, downFactor: 0, sampleRate: 1000 });
        },
        /downFactor must be a positive integer/,
        "Should reject downFactor = 0"
      );

      assert.throws(
        () => {
          pipeline.Resample({
            upFactor: 2.5,
            downFactor: 3,
            sampleRate: 1000,
          });
        },
        /upFactor must be a positive integer/,
        "Should reject non-integer upFactor"
      );
    });

    test("should validate sample rate", () => {
      const pipeline = createDspPipeline();

      assert.throws(
        () => {
          pipeline.Resample({ upFactor: 3, downFactor: 2, sampleRate: -100 });
        },
        /sampleRate must be positive/,
        "Should reject negative sample rate"
      );
    });

    test("should validate filter order", () => {
      const pipeline = createDspPipeline();

      assert.throws(
        () => {
          pipeline.Resample({
            upFactor: 3,
            downFactor: 2,
            sampleRate: 1000,
            order: 6,
          });
        },
        /order must be odd and >= 3/,
        "Should reject even order"
      );
    });

    test("should resample from 8kHz to 12kHz", async () => {
      const pipeline = createDspPipeline();
      // 12000/8000 = 3/2, so upFactor=3, downFactor=2
      pipeline.Resample({ upFactor: 3, downFactor: 2, sampleRate: 8000 });

      const input = new Float32Array(80); // 80 samples at 8kHz = 10ms
      for (let i = 0; i < input.length; i++) {
        input[i] = Math.sin((i / 8000) * 2 * Math.PI * 440); // 440 Hz tone
      }

      const output = await pipeline.process(input, { sampleRate: 8000 });

      // Expected: 80 * (3/2) = 120 samples
      const expectedLength = Math.floor((input.length * 3) / 2);
      assert.ok(
        output.length >= expectedLength - 5 &&
          output.length <= expectedLength + 5,
        `Output length ${output.length} should be close to ${expectedLength}`
      );

      // Verify signal is present
      const maxOutput = Math.max(...Array.from(output).map(Math.abs));
      assert.ok(maxOutput > 0, "Output should contain signal data");
    });

    test("should handle 44.1kHz to 48kHz conversion", async () => {
      const pipeline = createDspPipeline();
      // 48000/44100 = 160/147 (GCD reduced)
      pipeline.Resample({
        upFactor: 160,
        downFactor: 147,
        sampleRate: 44100,
      });

      const inputSamples = 441; // 10ms at 44.1kHz
      const input = new Float32Array(inputSamples);
      for (let i = 0; i < inputSamples; i++) {
        input[i] = Math.sin((i / 44100) * 2 * Math.PI * 1000); // 1kHz tone
      }

      const output = await pipeline.process(input, { sampleRate: 44100 });

      // Expected: 441 * (160/147) = 480 samples (10ms at 48kHz)
      const expectedLength = Math.floor((inputSamples * 160) / 147);
      assert.ok(
        output.length >= expectedLength - 5 &&
          output.length <= expectedLength + 5,
        `Output length ${output.length} should be close to ${expectedLength}`
      );

      // Verify signal is present
      const maxOutput = Math.max(...Array.from(output).map(Math.abs));
      assert.ok(maxOutput > 0, "Output should contain signal data");
    });
  });

  describe("Documentation Tests", () => {
    test("should support buffer resizing in pipeline", () => {
      // Dynamic buffer resizing is now supported!
      // The pipeline architecture was enhanced to handle stages that change buffer size

      const pipeline = createDspPipeline();

      // These methods exist and validate parameters
      assert.doesNotThrow(() => {
        pipeline.Interpolate({ factor: 2, sampleRate: 1000 });
      });

      assert.doesNotThrow(() => {
        pipeline.Decimate({ factor: 2, sampleRate: 1000 });
      });

      assert.doesNotThrow(() => {
        pipeline.Resample({ upFactor: 3, downFactor: 2, sampleRate: 1000 });
      });
    });
  });

  describe("Multi-channel Resampling", () => {
    test("should interpolate multi-channel signal", async () => {
      const pipeline = createDspPipeline();
      pipeline.Interpolate({ factor: 2, sampleRate: 1000 });

      // Create 2-channel interleaved signal: 50 samples per channel = 100 total
      const samplesPerChannel = 50;
      const input = new Float32Array(samplesPerChannel * 2);
      for (let i = 0; i < samplesPerChannel; i++) {
        input[i * 2] = Math.sin((2 * Math.PI * 5 * i) / 1000); // Ch 0
        input[i * 2 + 1] = Math.cos((2 * Math.PI * 5 * i) / 1000); // Ch 1
      }

      const output = await pipeline.process(input, {
        sampleRate: 1000,
        channels: 2,
      });

      // Expected: 50 samples/ch * 2 factor * 2 channels = ~200 total
      const expectedLength = samplesPerChannel * 2 * 2;
      assert.ok(
        output.length >= expectedLength - 10 &&
          output.length <= expectedLength + 10,
        `Output length ${output.length} should be close to ${expectedLength}`
      );
    });

    test("should decimate multi-channel signal", async () => {
      const pipeline = createDspPipeline();
      pipeline.Decimate({ factor: 2, sampleRate: 1000 });

      // Create 2-channel interleaved signal: 100 samples per channel = 200 total
      const samplesPerChannel = 100;
      const input = new Float32Array(samplesPerChannel * 2);
      for (let i = 0; i < samplesPerChannel; i++) {
        input[i * 2] = Math.sin((2 * Math.PI * 5 * i) / 1000);
        input[i * 2 + 1] = Math.cos((2 * Math.PI * 5 * i) / 1000);
      }

      const output = await pipeline.process(input, {
        sampleRate: 1000,
        channels: 2,
      });

      // Expected: 100 samples/ch / 2 factor * 2 channels = ~100 total
      const expectedLength = (samplesPerChannel / 2) * 2;
      assert.ok(
        output.length >= expectedLength - 10 &&
          output.length <= expectedLength + 10,
        `Output length ${output.length} should be close to ${expectedLength}`
      );
    });
  });

  describe("Pipeline Chaining with Resampling", () => {
    test("should chain multiple resampling operations", async () => {
      const pipeline = createDspPipeline();
      pipeline.Interpolate({ factor: 4, sampleRate: 1000 });
      pipeline.Decimate({ factor: 2, sampleRate: 4000 });

      const input = new Float32Array(100);
      for (let i = 0; i < input.length; i++) {
        input[i] = Math.sin((2 * Math.PI * 5 * i) / 1000);
      }

      const output = await pipeline.process(input, { sampleRate: 1000 });

      // Expected: 100 * 4 / 2 = 200 samples
      const expectedLength = (input.length * 4) / 2;
      assert.ok(
        output.length >= expectedLength - 10 &&
          output.length <= expectedLength + 10,
        `Output length ${output.length} should be close to ${expectedLength}`
      );
    });

    test("should chain resampling with other DSP operations", async () => {
      const pipeline = createDspPipeline();
      pipeline.MovingAverage({ mode: "moving", windowSize: 5 });
      pipeline.Decimate({ factor: 2, sampleRate: 1000 });
      pipeline.Rectify();

      const input = new Float32Array(100);
      for (let i = 0; i < input.length; i++) {
        input[i] = Math.sin((2 * Math.PI * 5 * i) / 1000);
      }

      const output = await pipeline.process(input, { sampleRate: 1000 });

      // After decimation, should be ~50 samples
      assert.ok(
        output.length >= 45 && output.length <= 55,
        `Output length ${output.length} should be close to 50`
      );

      // After rectify, all values should be non-negative
      const hasNegative = Array.from(output).some((v) => v < 0);
      assert.ok(
        !hasNegative,
        "All values should be non-negative after Rectify"
      );
    });

    test("should handle round-trip resampling", async () => {
      const pipeline = createDspPipeline();
      pipeline.Interpolate({ factor: 2, sampleRate: 1000 });
      pipeline.Decimate({ factor: 2, sampleRate: 2000 });

      const input = new Float32Array(100);
      for (let i = 0; i < input.length; i++) {
        input[i] = Math.sin((2 * Math.PI * 5 * i) / 1000);
      }

      const output = await pipeline.process(input, { sampleRate: 1000 });

      // Should be approximately the same size as input
      assert.ok(
        output.length >= input.length - 10 &&
          output.length <= input.length + 10,
        `Output length ${output.length} should be close to input length ${input.length}`
      );

      // Signal should be reasonably preserved (allowing for filtering effects)
      const compareLength = Math.min(input.length, output.length);
      let sumSquaredDiff = 0;
      for (let i = 0; i < compareLength; i++) {
        sumSquaredDiff += (input[i] - output[i]) ** 2;
      }
      const rmse = Math.sqrt(sumSquaredDiff / compareLength);

      // Note: Some signal distortion is expected due to anti-aliasing and anti-imaging filters
      // The filters remove high-frequency content to prevent aliasing/imaging artifacts
      assert.ok(
        rmse < 0.5,
        `RMSE ${rmse} should be reasonably small (< 0.5, some distortion expected from filtering)`
      );
    });
  });

  describe("Edge Cases", () => {
    test("should handle small input buffers", async () => {
      const pipeline = createDspPipeline();
      pipeline.Interpolate({ factor: 2, sampleRate: 1000 });

      const input = new Float32Array([1, 2, 3, 4, 5]);
      const output = await pipeline.process(input, { sampleRate: 1000 });

      // Should produce ~10 samples (5 * 2)
      assert.ok(
        output.length >= 8 && output.length <= 12,
        `Small input should be handled: got ${output.length} samples`
      );
    });

    test("should handle large decimation factors", async () => {
      const pipeline = createDspPipeline();
      pipeline.Decimate({ factor: 10, sampleRate: 10000 });

      const input = new Float32Array(1000);
      for (let i = 0; i < input.length; i++) {
        input[i] = Math.sin((2 * Math.PI * 50 * i) / 10000);
      }

      const output = await pipeline.process(input, { sampleRate: 10000 });

      // Expected: ~100 samples (1000 / 10)
      const expectedLength = input.length / 10;
      assert.ok(
        output.length >= expectedLength - 5 &&
          output.length <= expectedLength + 5,
        `Output length ${output.length} should be close to ${expectedLength}`
      );
    });

    test("should handle rational resampling with large factors", async () => {
      const pipeline = createDspPipeline();
      // Resample 44.1kHz to 48kHz (common audio conversion)
      pipeline.Resample({
        upFactor: 160,
        downFactor: 147,
        sampleRate: 44100,
      });

      const input = new Float32Array(882); // 20ms at 44.1kHz
      for (let i = 0; i < input.length; i++) {
        input[i] = Math.sin((2 * Math.PI * 440 * i) / 44100); // A4 note
      }

      const output = await pipeline.process(input, { sampleRate: 44100 });

      // Expected: ~960 samples (20ms at 48kHz)
      const expectedLength = Math.floor((input.length * 160) / 147);
      assert.ok(
        output.length >= expectedLength - 10 &&
          output.length <= expectedLength + 10,
        `Output length ${output.length} should be close to ${expectedLength}`
      );
    });
  });
});
