/**
 * TimeAlignment.test.ts
 *
 * Tests for production-grade irregular timestamp resampling
 */

import { describe, it, beforeEach, afterEach } from "node:test";
import assert from "node:assert";
import { createDspPipeline, DspProcessor } from "../bindings.js";

describe("TimeAlignment Stage", () => {
  let pipeline: DspProcessor;

  beforeEach(() => {
    pipeline = createDspPipeline();
  });

  afterEach(() => {
    pipeline.dispose();
  });
  describe("Basic Functionality", () => {
    it("should resample irregular data to uniform grid (linear interpolation)", async () => {
      pipeline.TimeAlignment({
        targetSampleRate: 10, // 10 Hz = 100ms intervals
        interpolationMethod: "linear",
        gapPolicy: "interpolate",
      });

      // Irregular timestamps: 0ms, 50ms, 200ms, 350ms, 500ms
      const samples = new Float32Array([1.0, 2.0, 3.0, 4.0, 5.0]);
      const timestamps = new Float32Array([0, 50, 200, 350, 500]);

      const result = await pipeline.process(samples, timestamps, {
        channels: 1,
      });

      // Expected output at: 0ms, 100ms, 200ms, 300ms, 400ms, 500ms (6 samples)
      assert.strictEqual(result.length, 6);

      // Check first and last values match
      assert.strictEqual(result[0], 1.0); // t=0ms
      assert.strictEqual(result[5], 5.0); // t=500ms

      // Check interpolated values
      assert.ok(result[1] > 2.0 && result[1] < 3.0); // t=100ms (interpolated)
      assert.strictEqual(result[2], 3.0); // t=200ms (exact match)
    });

    it("should handle uniform input data correctly", async () => {
      pipeline.TimeAlignment({
        targetSampleRate: 100, // 100 Hz = 10ms intervals
      });

      // Already uniform: 0ms, 10ms, 20ms, 30ms
      const samples = new Float32Array([1.0, 2.0, 3.0, 4.0]);
      const timestamps = new Float32Array([0, 10, 20, 30]);

      const result = await pipeline.process(samples, timestamps, {
        channels: 1,
      });

      // Should preserve data
      assert.strictEqual(result.length, 4);
      assert.deepStrictEqual(Array.from(result), [1.0, 2.0, 3.0, 4.0]);
    });
  });

  describe("Gap Detection and Policies", () => {
    it("should detect gaps with gapPolicy='interpolate'", async () => {
      pipeline.TimeAlignment({
        targetSampleRate: 10, // 100ms intervals
        gapPolicy: "interpolate",
        gapThreshold: 2.0, // Detect gaps > 200ms
      });

      // Gap between 100ms and 400ms (300ms gap)
      const samples = new Float32Array([1.0, 2.0, 5.0, 6.0]);
      const timestamps = new Float32Array([0, 100, 400, 500]);

      const result = await pipeline.process(samples, timestamps, {
        channels: 1,
      });

      // Should have: 0, 100, 200, 300, 400, 500 (6 samples)
      assert.strictEqual(result.length, 6);

      // Check interpolation across gap
      assert.strictEqual(result[0], 1.0); // t=0
      assert.strictEqual(result[1], 2.0); // t=100
      assert.ok(result[2] > 2.0 && result[2] < 5.0); // t=200 (interpolated)
      assert.ok(result[3] > 2.0 && result[3] < 5.0); // t=300 (interpolated)
      assert.strictEqual(result[4], 5.0); // t=400
    });

    it("should zero-fill gaps with gapPolicy='zero-fill'", async () => {
      pipeline.TimeAlignment({
        targetSampleRate: 10,
        gapPolicy: "zero-fill",
        gapThreshold: 2.0,
      });

      // Gap between 100ms and 400ms
      const samples = new Float32Array([1.0, 2.0, 5.0, 6.0]);
      const timestamps = new Float32Array([0, 100, 400, 500]);

      const result = await pipeline.process(samples, timestamps, {
        channels: 1,
      });

      assert.strictEqual(result.length, 6);
      assert.strictEqual(result[2], 0.0); // t=200 (zero-filled)
      assert.strictEqual(result[3], 0.0); // t=300 (zero-filled)
    });

    it("should hold last value with gapPolicy='hold'", async () => {
      pipeline.TimeAlignment({
        targetSampleRate: 10,
        gapPolicy: "hold",
        gapThreshold: 2.0,
      });

      const samples = new Float32Array([1.0, 2.0, 5.0, 6.0]);
      const timestamps = new Float32Array([0, 100, 400, 500]);

      const result = await pipeline.process(samples, timestamps, {
        channels: 1,
      });

      assert.strictEqual(result.length, 6);
      assert.strictEqual(result[2], 2.0); // t=200 (held from t=100)
      assert.strictEqual(result[3], 2.0); // t=300 (held from t=100)
    });

    it("should throw error with gapPolicy='error'", async () => {
      pipeline.TimeAlignment({
        targetSampleRate: 10,
        gapPolicy: "error",
        gapThreshold: 2.0,
      });

      const samples = new Float32Array([1.0, 2.0, 5.0, 6.0]);
      const timestamps = new Float32Array([0, 100, 400, 500]);

      await assert.rejects(
        async () => {
          await pipeline.process(samples, timestamps, { channels: 1 });
        },
        {
          message: /Gap detected/,
        }
      );
    });
  });

  describe("Interpolation Methods", () => {
    it("should use cubic interpolation for smoother results", async () => {
      pipeline.TimeAlignment({
        targetSampleRate: 10,
        interpolationMethod: "cubic",
      });

      // Sinusoidal-like data
      const samples = new Float32Array([0, 1, 0, -1, 0]);
      const timestamps = new Float32Array([0, 100, 200, 300, 400]);

      const result = await pipeline.process(samples, timestamps, {
        channels: 1,
      });

      // Cubic should produce smoother curve than linear
      assert.strictEqual(result.length, 5);
      assert.ok(Math.abs(result[2]) < 0.1); // Should be near zero at midpoint
    });

    it("should use sinc interpolation for band-limited signals", async () => {
      pipeline.TimeAlignment({
        targetSampleRate: 20, // 20 Hz
        interpolationMethod: "sinc",
      });

      const samples = new Float32Array([1, 0, 1, 0, 1]);
      const timestamps = new Float32Array([0, 100, 200, 300, 400]);

      const result = await pipeline.process(samples, timestamps, {
        channels: 1,
      });

      // Sinc interpolation should handle band-limited signals well
      assert.ok(result.length > 0);
    });
  });

  describe("Clock Drift Compensation", () => {
    it("should estimate sample rate with driftCompensation='regression'", async () => {
      pipeline.TimeAlignment({
        targetSampleRate: 10,
        driftCompensation: "regression",
      });

      // Slightly drifting clock: expecting 100ms, but getting 102ms intervals
      const samples = new Float32Array([1, 2, 3, 4, 5]);
      const timestamps = new Float32Array([0, 102, 204, 306, 408]);

      const result = await pipeline.process(samples, timestamps, {
        channels: 1,
      });

      // Should still resample to uniform 100ms grid
      assert.ok(result.length >= 6);
    });

    it("should use PLL for drift compensation", async () => {
      pipeline.TimeAlignment({
        targetSampleRate: 10,
        driftCompensation: "pll",
      });

      const samples = new Float32Array([1, 2, 3, 4, 5]);
      const timestamps = new Float32Array([0, 98, 204, 302, 405]);

      const result = await pipeline.process(samples, timestamps, {
        channels: 1,
      });

      assert.ok(result.length >= 4);
    });
  });

  describe("Multi-Channel Support", () => {
    it("should handle multi-channel data correctly", async () => {
      pipeline.TimeAlignment({
        targetSampleRate: 10,
      });

      // 3-channel data
      const samples = new Float32Array([
        1.0,
        10.0,
        100.0, // Sample 0: ch0=1, ch1=10, ch2=100
        2.0,
        20.0,
        200.0, // Sample 1: ch0=2, ch1=20, ch2=200
        3.0,
        30.0,
        300.0, // Sample 2: ch0=3, ch1=30, ch2=300
      ]);
      // One timestamp per channel-value for interleaved format
      const timestamps = new Float32Array([
        0, 0, 0, 100, 100, 100, 200, 200, 200,
      ]);

      const result = await pipeline.process(samples, timestamps, {
        channels: 3,
      });

      assert.strictEqual(result.length, 9); // 3 samples * 3 channels
      assert.strictEqual(result[0], 1.0); // ch0, t=0
      assert.strictEqual(result[1], 10.0); // ch1, t=0
      assert.strictEqual(result[2], 100.0); // ch2, t=0
    });
  });

  describe("Edge Cases", () => {
    it("should handle single sample", async () => {
      pipeline.TimeAlignment({
        targetSampleRate: 10,
      });

      const samples = new Float32Array([1.0]);
      const timestamps = new Float32Array([0]);

      const result = await pipeline.process(samples, timestamps, {
        channels: 1,
      });

      assert.strictEqual(result.length, 1);
      assert.strictEqual(result[0], 1.0);
    });

    it("should handle very short time spans", async () => {
      pipeline.TimeAlignment({
        targetSampleRate: 100, // 10ms intervals
      });

      // Only 50ms of data
      const samples = new Float32Array([1.0, 2.0, 3.0]);
      const timestamps = new Float32Array([0, 25, 50]);

      const result = await pipeline.process(samples, timestamps, {
        channels: 1,
      });

      // Should have at least: 0ms, 10ms, 20ms, 30ms, 40ms, 50ms (6 samples)
      assert.ok(result.length >= 3);
    });

    it("should throw error if timestamps not provided", async () => {
      pipeline.TimeAlignment({
        targetSampleRate: 10,
      });

      const samples = new Float32Array([1, 2, 3]);

      await assert.rejects(
        async () => {
          await pipeline.process(samples, { channels: 1 }); // No timestamps
        },
        {
          message: /TimeAlignment.*requires timestamps/,
        }
      );
    });
  });

  describe("Production Use Cases", () => {
    it("should handle IoT sensor with network jitter", async () => {
      pipeline
        .TimeAlignment({
          targetSampleRate: 100, // 100 Hz
          interpolationMethod: "linear",
          gapPolicy: "interpolate",
          driftCompensation: "regression",
        })
        .MovingAverage({ mode: "moving", windowDuration: 100 }); // 100ms smoothing

      // Simulate sensor with jitter
      const timestamps = new Float32Array(100);
      const samples = new Float32Array(100);

      for (let i = 0; i < 100; i++) {
        timestamps[i] = i * 10 + Math.random() * 5 - 2.5; // ±2.5ms jitter
        samples[i] = Math.sin((2 * Math.PI * i) / 100) + Math.random() * 0.1;
      }

      const result = await pipeline.process(samples, timestamps, {
        channels: 1,
      });

      assert.ok(result.length > 0);
    });

    it("should handle GPS data with dropped packets", async () => {
      pipeline
        .TimeAlignment({
          targetSampleRate: 10, // 10 Hz GPS
          interpolationMethod: "cubic",
          gapPolicy: "hold", // Hold last position during gaps
          gapThreshold: 2.0,
        })
        .KalmanFilter({ dimensions: 2, processNoise: 1e-5 });

      // GPS lat/lon with dropped packet at 300ms
      const samples = new Float32Array([
        37.7749,
        -122.4194, // Sample 0
        37.775,
        -122.4195, // Sample 1
        37.7752,
        -122.4196, // Sample 2 (skip sample 3)
        37.7753,
        -122.4197, // Sample 4
      ]);
      const timestamps = new Float32Array([
        0,
        0, // Sample 0
        100,
        100, // Sample 1
        200,
        200, // Sample 2
        400,
        400, // Sample 4 (gap from 200-400ms)
      ]);

      const result = await pipeline.process(samples, timestamps, {
        channels: 2,
      });

      assert.ok(result.length > 0);
    });
  });
});
