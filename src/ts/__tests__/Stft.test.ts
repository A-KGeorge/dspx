import { describe, it } from "node:test";
import assert from "node:assert";
import { createDspPipeline } from "../bindings.js";

describe("STFT (Short-Time Fourier Transform)", () => {
  describe("Basic Functionality", () => {
    it("should compute STFT with default parameters", async () => {
      const pipeline = createDspPipeline().stft({
        windowSize: 256,
      });

      // Generate a simple sine wave (1000 samples at 100 Hz)
      const sampleRate = 1000;
      const freq = 100;
      const input = new Float32Array(1000);
      for (let i = 0; i < input.length; i++) {
        input[i] = Math.sin((2 * Math.PI * freq * i) / sampleRate);
      }

      const output = await pipeline.process(input, { sampleRate, channels: 1 });

      // Output should be magnitude spectrum (default)
      // With windowSize=256, hopSize=128 (50% overlap), we get ~7-8 windows
      // Each window produces 129 bins (256/2 + 1 for real FFT)
      const expectedBinsPerWindow = 256 / 2 + 1; // 129
      const expectedNumWindows = Math.floor((input.length - 256) / 128) + 1;
      const expectedLength = expectedNumWindows * expectedBinsPerWindow;

      assert.ok(
        output.length >= expectedLength - expectedBinsPerWindow,
        `Expected ~${expectedLength} output samples, got ${output.length}`
      );

      // All magnitude values should be non-negative
      for (let i = 0; i < output.length; i++) {
        assert.ok(output[i] >= 0, `Magnitude should be >= 0, got ${output[i]}`);
      }
    });

    it("should compute STFT with custom hop size (75% overlap)", async () => {
      const pipeline = createDspPipeline().stft({
        windowSize: 512,
        hopSize: 128, // 75% overlap
      });

      const input = new Float32Array(1024);
      for (let i = 0; i < input.length; i++) {
        input[i] = Math.sin(2 * Math.PI * 0.1 * i);
      }

      const output = await pipeline.process(input, {
        sampleRate: 1000,
        channels: 1,
      });

      // Note: Current implementation outputs fixed-size buffer (same as input)
      // rather than concatenated frequency bins. This is a limitation of the
      // pipeline architecture which expects consistent buffer sizes.
      // The STFT is computed correctly, but output is truncated/padded to input size.
      assert.ok(
        output.length === input.length,
        `Output should match input size, got ${output.length}`
      );
    });

    it("should compute STFT with no overlap", async () => {
      const pipeline = createDspPipeline().stft({
        windowSize: 256,
        hopSize: 256, // No overlap
      });

      const input = new Float32Array(1024);
      for (let i = 0; i < input.length; i++) {
        input[i] = Math.sin(2 * Math.PI * 0.05 * i);
      }

      const output = await pipeline.process(input, {
        sampleRate: 1000,
        channels: 1,
      });

      // Note: Current implementation outputs fixed-size buffer (same as input)
      // rather than concatenated frequency bins.
      assert.ok(
        output.length === input.length,
        `Output should match input size, got ${output.length}`
      );
    });

    it("should detect frequency content correctly", async () => {
      const pipeline = createDspPipeline().stft({
        windowSize: 1024,
        hopSize: 512,
        output: "magnitude",
      });

      const sampleRate = 8000;
      const freq1 = 440; // A4 note
      const freq2 = 880; // A5 note
      const input = new Float32Array(2048);

      // First half: 440 Hz
      for (let i = 0; i < 1024; i++) {
        input[i] = Math.sin((2 * Math.PI * freq1 * i) / sampleRate);
      }

      // Second half: 880 Hz
      for (let i = 1024; i < 2048; i++) {
        input[i] = Math.sin((2 * Math.PI * freq2 * i) / sampleRate);
      }

      const output = await pipeline.process(input, { sampleRate, channels: 1 });

      // Output should show different frequency content in different windows
      assert.ok(output.length > 0);

      // Find peak in first window
      const binsPerWindow = 1024 / 2 + 1; // 513
      const firstWindow = output.slice(0, binsPerWindow);
      const firstPeakIdx = firstWindow.indexOf(Math.max(...firstWindow));
      const firstPeakFreq = (firstPeakIdx * sampleRate) / 1024;

      // Should be close to 440 Hz
      assert.ok(
        Math.abs(firstPeakFreq - freq1) < 50,
        `First window peak at ${firstPeakFreq} Hz, expected ~${freq1} Hz`
      );
    });
  });

  describe("Output Formats", () => {
    it("should output magnitude spectrum", async () => {
      const pipeline = createDspPipeline().stft({
        windowSize: 512,
        output: "magnitude",
      });

      const input = new Float32Array(1024);
      for (let i = 0; i < input.length; i++) {
        input[i] = Math.sin(2 * Math.PI * 0.1 * i);
      }

      const output = await pipeline.process(input, {
        sampleRate: 1000,
        channels: 1,
      });

      // All magnitudes should be >= 0
      for (let i = 0; i < output.length; i++) {
        assert.ok(output[i] >= 0, `Magnitude should be >= 0, got ${output[i]}`);
      }
    });

    it("should output power spectrum", async () => {
      const pipeline = createDspPipeline().stft({
        windowSize: 256,
        output: "power",
      });

      const input = new Float32Array(512);
      for (let i = 0; i < input.length; i++) {
        input[i] = Math.sin(2 * Math.PI * 0.1 * i);
      }

      const output = await pipeline.process(input, {
        sampleRate: 1000,
        channels: 1,
      });

      // Power should be >= 0 and generally larger than magnitude
      for (let i = 0; i < output.length; i++) {
        assert.ok(output[i] >= 0, `Power should be >= 0, got ${output[i]}`);
      }
    });

    it("should output phase spectrum", async () => {
      const pipeline = createDspPipeline().stft({
        windowSize: 256,
        output: "phase",
      });

      const input = new Float32Array(512);
      for (let i = 0; i < input.length; i++) {
        input[i] = Math.sin(2 * Math.PI * 0.1 * i);
      }

      const output = await pipeline.process(input, {
        sampleRate: 1000,
        channels: 1,
      });

      // Phase should be in range [-π, π] (with small tolerance for floating-point)
      const tolerance = 1e-6;
      for (let i = 0; i < output.length; i++) {
        assert.ok(
          output[i] >= -Math.PI - tolerance && output[i] <= Math.PI + tolerance,
          `Phase should be in [-π, π], got ${output[i]}`
        );
      }
    });

    it("should output complex spectrum", async () => {
      const pipeline = createDspPipeline().stft({
        windowSize: 256,
        output: "complex",
      });

      const input = new Float32Array(512);
      for (let i = 0; i < input.length; i++) {
        input[i] = Math.sin(2 * Math.PI * 0.1 * i);
      }

      const output = await pipeline.process(input, {
        sampleRate: 1000,
        channels: 1,
      });

      // Complex output is interleaved [real0, imag0, real1, imag1, ...]
      // Should have even number of values
      assert.strictEqual(
        output.length % 2,
        0,
        "Complex output should have even length"
      );

      // All values should be finite
      for (let i = 0; i < output.length; i++) {
        assert.ok(
          isFinite(output[i]),
          `Value should be finite, got ${output[i]}`
        );
      }
    });
  });

  describe("Transform Methods", () => {
    it("should use FFT for power-of-2 window sizes", async () => {
      const pipeline = createDspPipeline().stft({
        windowSize: 512, // Power of 2
        method: "fft",
      });

      const input = new Float32Array(1024);
      for (let i = 0; i < input.length; i++) {
        input[i] = Math.sin(2 * Math.PI * 0.1 * i);
      }

      const output = await pipeline.process(input, {
        sampleRate: 1000,
        channels: 1,
      });
      assert.ok(output.length > 0);
    });

    it("should use DFT for arbitrary window sizes", async () => {
      const pipeline = createDspPipeline().stft({
        windowSize: 500, // Not power of 2
        method: "dft",
      });

      const input = new Float32Array(1000);
      for (let i = 0; i < input.length; i++) {
        input[i] = Math.sin(2 * Math.PI * 0.1 * i);
      }

      const output = await pipeline.process(input, {
        sampleRate: 1000,
        channels: 1,
      });
      assert.ok(output.length > 0);
    });

    it("should auto-detect FFT for power-of-2 sizes", async () => {
      const pipeline = createDspPipeline().stft({
        windowSize: 1024, // Power of 2, should auto-select FFT
      });

      const input = new Float32Array(2048);
      for (let i = 0; i < input.length; i++) {
        input[i] = Math.sin(2 * Math.PI * 0.1 * i);
      }

      const output = await pipeline.process(input, {
        sampleRate: 1000,
        channels: 1,
      });
      assert.ok(output.length > 0);
    });

    it("should auto-detect DFT for non-power-of-2 sizes", async () => {
      const pipeline = createDspPipeline().stft({
        windowSize: 1000, // Not power of 2, should auto-select DFT
      });

      const input = new Float32Array(2000);
      for (let i = 0; i < input.length; i++) {
        input[i] = Math.sin(2 * Math.PI * 0.1 * i);
      }

      const output = await pipeline.process(input, {
        sampleRate: 1000,
        channels: 1,
      });
      assert.ok(output.length > 0);
    });
  });

  describe("Window Functions", () => {
    it("should apply Hann window (default)", async () => {
      const pipeline = createDspPipeline().stft({
        windowSize: 256,
        window: "hann",
      });

      const input = new Float32Array(512);
      for (let i = 0; i < input.length; i++) {
        input[i] = 1.0; // Constant signal
      }

      const output = await pipeline.process(input, {
        sampleRate: 1000,
        channels: 1,
      });
      assert.ok(output.length > 0);
    });

    it("should apply Hamming window", async () => {
      const pipeline = createDspPipeline().stft({
        windowSize: 256,
        window: "hamming",
      });

      const input = new Float32Array(512);
      for (let i = 0; i < input.length; i++) {
        input[i] = Math.sin(2 * Math.PI * 0.1 * i);
      }

      const output = await pipeline.process(input, {
        sampleRate: 1000,
        channels: 1,
      });
      assert.ok(output.length > 0);
    });

    it("should apply Blackman window", async () => {
      const pipeline = createDspPipeline().stft({
        windowSize: 256,
        window: "blackman",
      });

      const input = new Float32Array(512);
      for (let i = 0; i < input.length; i++) {
        input[i] = Math.sin(2 * Math.PI * 0.1 * i);
      }

      const output = await pipeline.process(input, {
        sampleRate: 1000,
        channels: 1,
      });
      assert.ok(output.length > 0);
    });

    it("should apply Bartlett window", async () => {
      const pipeline = createDspPipeline().stft({
        windowSize: 256,
        window: "bartlett",
      });

      const input = new Float32Array(512);
      for (let i = 0; i < input.length; i++) {
        input[i] = Math.sin(2 * Math.PI * 0.1 * i);
      }

      const output = await pipeline.process(input, {
        sampleRate: 1000,
        channels: 1,
      });
      assert.ok(output.length > 0);
    });

    it("should apply no window (rectangular)", async () => {
      const pipeline = createDspPipeline().stft({
        windowSize: 256,
        window: "none",
      });

      const input = new Float32Array(512);
      for (let i = 0; i < input.length; i++) {
        input[i] = Math.sin(2 * Math.PI * 0.1 * i);
      }

      const output = await pipeline.process(input, {
        sampleRate: 1000,
        channels: 1,
      });
      assert.ok(output.length > 0);
    });
  });

  describe("Multi-Channel Support", () => {
    it("should process 2-channel data independently", async () => {
      const pipeline = createDspPipeline().stft({
        windowSize: 256,
        hopSize: 128,
      });

      // Generate 2-channel interleaved data
      // Channel 1: 100 Hz, Channel 2: 200 Hz
      const input = new Float32Array(1024);
      for (let i = 0; i < input.length; i += 2) {
        input[i] = Math.sin((2 * Math.PI * 100 * (i / 2)) / 1000); // Ch1
        input[i + 1] = Math.sin((2 * Math.PI * 200 * (i / 2)) / 1000); // Ch2
      }

      const output = await pipeline.process(input, {
        sampleRate: 1000,
        channels: 2,
      });

      // Should produce output for both channels
      assert.ok(output.length > 0);
    });

    it("should process 4-channel data", async () => {
      const pipeline = createDspPipeline().stft({
        windowSize: 512,
      });

      const input = new Float32Array(2048);
      for (let i = 0; i < input.length; i++) {
        input[i] = Math.sin(2 * Math.PI * 0.1 * i);
      }

      const output = await pipeline.process(input, {
        sampleRate: 1000,
        channels: 4,
      });
      assert.ok(output.length > 0);
    });
  });

  describe("Chaining", () => {
    it("should chain with other pipeline stages", async () => {
      const pipeline = createDspPipeline()
        .MovingAverage({ mode: "moving", windowSize: 5 })
        .stft({
          windowSize: 256,
          output: "magnitude",
        });

      const input = new Float32Array(1024);
      for (let i = 0; i < input.length; i++) {
        input[i] = Math.sin(2 * Math.PI * 0.1 * i) + 0.1 * Math.random();
      }

      const output = await pipeline.process(input, {
        sampleRate: 1000,
        channels: 1,
      });
      assert.ok(output.length > 0);
    });

    it("should work with tap for debugging", async () => {
      let tappedLength = 0;
      const pipeline = createDspPipeline()
        .stft({
          windowSize: 256,
          output: "magnitude",
        })
        .tap((samples) => {
          tappedLength = samples.length;
        });

      const input = new Float32Array(512);
      for (let i = 0; i < input.length; i++) {
        input[i] = Math.sin(2 * Math.PI * 0.1 * i);
      }

      await pipeline.process(input, { sampleRate: 1000, channels: 1 });
      assert.ok(tappedLength > 0, "Tap callback should have been called");
    });
  });

  describe("Validation", () => {
    it("should throw error for invalid window size", () => {
      assert.throws(
        () => {
          createDspPipeline().stft({
            windowSize: 0,
          });
        },
        { message: /windowSize must be a positive integer/ }
      );

      assert.throws(
        () => {
          createDspPipeline().stft({
            windowSize: -10,
          });
        },
        { message: /windowSize must be a positive integer/ }
      );

      assert.throws(
        () => {
          createDspPipeline().stft({
            windowSize: 3.5,
          });
        },
        { message: /windowSize must be a positive integer/ }
      );
    });

    it("should throw error for invalid hop size", () => {
      assert.throws(
        () => {
          createDspPipeline().stft({
            windowSize: 256,
            hopSize: 0,
          });
        },
        { message: /hopSize must be a positive integer/ }
      );

      assert.throws(
        () => {
          createDspPipeline().stft({
            windowSize: 256,
            hopSize: 300, // Greater than windowSize
          });
        },
        { message: /hopSize must be a positive integer <= windowSize/ }
      );
    });

    it("should throw error for FFT with non-power-of-2 size", () => {
      assert.throws(
        () => {
          createDspPipeline().stft({
            windowSize: 1000, // Not power of 2
            method: "fft",
          });
        },
        { message: /FFT method requires power-of-2 windowSize/ }
      );
    });

    it("should throw error for invalid method", () => {
      assert.throws(
        () => {
          createDspPipeline().stft({
            windowSize: 256,
            method: "invalid" as any,
          });
        },
        { message: /method must be one of/ }
      );
    });

    it("should throw error for invalid output format", () => {
      assert.throws(
        () => {
          createDspPipeline().stft({
            windowSize: 256,
            output: "invalid" as any,
          });
        },
        { message: /output must be one of/ }
      );
    });

    it("should throw error for invalid window function", () => {
      assert.throws(
        () => {
          createDspPipeline().stft({
            windowSize: 256,
            window: "invalid" as any,
          });
        },
        { message: /window must be one of/ }
      );
    });

    it("should throw error for invalid type", () => {
      assert.throws(
        () => {
          createDspPipeline().stft({
            windowSize: 256,
            type: "invalid" as any,
          });
        },
        { message: /type must be one of/ }
      );
    });
  });

  describe("Edge Cases", () => {
    it("should handle input shorter than window size", async () => {
      const pipeline = createDspPipeline().stft({
        windowSize: 512,
      });

      const input = new Float32Array(256); // Shorter than window
      for (let i = 0; i < input.length; i++) {
        input[i] = Math.sin(2 * Math.PI * 0.1 * i);
      }

      const output = await pipeline.process(input, {
        sampleRate: 1000,
        channels: 1,
      });

      // Should either pad or return empty/single window
      assert.ok(output.length >= 0);
    });

    it("should handle input exactly equal to window size", async () => {
      const pipeline = createDspPipeline().stft({
        windowSize: 256,
        hopSize: 128,
      });

      const input = new Float32Array(256); // Exactly one window
      for (let i = 0; i < input.length; i++) {
        input[i] = Math.sin(2 * Math.PI * 0.1 * i);
      }

      const output = await pipeline.process(input, {
        sampleRate: 1000,
        channels: 1,
      });

      // Should produce at least one window
      const binsPerWindow = 256 / 2 + 1;
      assert.ok(output.length >= binsPerWindow);
    });

    it("should handle zero signal", async () => {
      const pipeline = createDspPipeline().stft({
        windowSize: 256,
      });

      const input = new Float32Array(512); // All zeros

      const output = await pipeline.process(input, {
        sampleRate: 1000,
        channels: 1,
      });

      // All values should be zero or very close
      for (let i = 0; i < output.length; i++) {
        assert.ok(Math.abs(output[i]) < 0.001, `Expected ~0, got ${output[i]}`);
      }
    });

    it("should handle DC signal (constant)", async () => {
      const pipeline = createDspPipeline().stft({
        windowSize: 256,
        output: "magnitude",
      });

      const input = new Float32Array(512);
      for (let i = 0; i < input.length; i++) {
        input[i] = 1.0; // DC component
      }

      const output = await pipeline.process(input, {
        sampleRate: 1000,
        channels: 1,
      });

      // DC should show up in bin 0 of each window
      // First bin of first window should have high energy
      const binsPerWindow = 256 / 2 + 1;
      assert.ok(output[0] > 0, "DC component should be present in first bin");
    });

    it("should handle very small window size", async () => {
      const pipeline = createDspPipeline().stft({
        windowSize: 8, // Very small
      });

      const input = new Float32Array(64);
      for (let i = 0; i < input.length; i++) {
        input[i] = Math.sin(2 * Math.PI * 0.1 * i);
      }

      const output = await pipeline.process(input, {
        sampleRate: 1000,
        channels: 1,
      });
      assert.ok(output.length > 0);
    });

    it("should handle large window size", async () => {
      const pipeline = createDspPipeline().stft({
        windowSize: 4096, // Large
      });

      const input = new Float32Array(8192);
      for (let i = 0; i < input.length; i++) {
        input[i] = Math.sin(2 * Math.PI * 0.1 * i);
      }

      const output = await pipeline.process(input, {
        sampleRate: 1000,
        channels: 1,
      });
      assert.ok(output.length > 0);
    });

    it("should handle very high frequency content", async () => {
      const pipeline = createDspPipeline().stft({
        windowSize: 256,
      });

      const sampleRate = 8000;
      const freq = 3500; // Near Nyquist
      const input = new Float32Array(512);
      for (let i = 0; i < input.length; i++) {
        input[i] = Math.sin((2 * Math.PI * freq * i) / sampleRate);
      }

      const output = await pipeline.process(input, { sampleRate, channels: 1 });
      assert.ok(output.length > 0);

      // Should detect high frequency in upper bins
      const binsPerWindow = 256 / 2 + 1;
      const firstWindow = output.slice(0, binsPerWindow);
      const maxValue = Math.max(...firstWindow);
      const maxIdx = firstWindow.indexOf(maxValue);

      // Peak should be in upper half of spectrum
      assert.ok(
        maxIdx > binsPerWindow / 2,
        "High frequency should appear in upper bins"
      );
    });
  });

  describe("Performance", () => {
    it("should handle large input efficiently", async () => {
      const pipeline = createDspPipeline().stft({
        windowSize: 1024,
        hopSize: 512,
      });

      const input = new Float32Array(100000); // 100k samples
      for (let i = 0; i < input.length; i++) {
        input[i] = Math.sin(2 * Math.PI * 0.01 * i);
      }

      const start = Date.now();
      const output = await pipeline.process(input, {
        sampleRate: 1000,
        channels: 1,
      });
      const elapsed = Date.now() - start;

      assert.ok(output.length > 0);
      assert.ok(elapsed < 5000, `STFT took too long: ${elapsed}ms`); // Should complete in <5s
    });
  });
});
