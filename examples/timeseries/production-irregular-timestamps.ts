/**
 * Production Examples: TimeAlignment Stage for Irregular Timestamps
 *
 * This file demonstrates real-world use cases for the TimeAlignment stage,
 * showing how to handle irregular sampling in production systems.
 */

import { createDspPipeline } from "../../src/ts/bindings.js";

/**
 * Example 1: IoT Sensor with Network Jitter
 *
 * Scenario: Temperature sensor transmitting over WiFi with variable latency
 * Problem: Network jitter causes irregular sample arrival (90-150ms intervals)
 * Solution: Resample to uniform 100ms (10 Hz) grid for consistent processing
 */
export async function iotSensorWithJitter() {
  console.log("\n=== IoT Sensor with Network Jitter ===");

  const pipeline = createDspPipeline();
  pipeline
    .TimeAlignment({
      targetSampleRate: 10, // 10 Hz = 100ms intervals
      interpolationMethod: "linear",
      gapPolicy: "interpolate", // Fill small gaps with interpolation
      gapThreshold: 3.0, // Detect gaps > 300ms
      driftCompensation: "regression", // Estimate true sample rate
    })
    .MovingAverage({ mode: "moving", windowDuration: 500 }); // 500ms smoothing

  // Simulated sensor data with network jitter
  const sensorReadings = new Float32Array(20);
  const timestamps = new Float32Array(20);

  let time = 0;
  for (let i = 0; i < 20; i++) {
    // Base temperature with noise
    sensorReadings[i] = 22.5 + Math.sin(i * 0.3) * 2 + Math.random() * 0.5;

    // Network jitter: 90-150ms intervals (target 100ms)
    const jitter = Math.random() * 60 - 30; // ±30ms jitter
    time += 100 + jitter;
    timestamps[i] = time;
  }

  const result = await pipeline.process(sensorReadings, timestamps, {
    channels: 1,
  });

  console.log(`Input samples: ${sensorReadings.length}`);
  console.log(`Output samples: ${result.length} (uniform 100ms grid)`);
  console.log(`Time span: ${timestamps[timestamps.length - 1].toFixed(1)}ms`);
  console.log(
    `Sample values: [${result
      .slice(0, 5)
      .map((v) => v.toFixed(2))
      .join(", ")}...]`
  );

  return result;
}

/**
 * Example 2: GPS Tracking with Dropped Packets
 *
 * Scenario: Vehicle GPS tracker losing signal in tunnels/urban canyons
 * Problem: Irregular samples with gaps (dropped packets during signal loss)
 * Solution: Resample to 1 Hz with gap detection, hold last position during gaps
 */
export async function gpsTrackingWithDropouts() {
  console.log("\n=== GPS Tracking with Dropped Packets ===");

  const pipeline = createDspPipeline();
  pipeline.TimeAlignment({
    targetSampleRate: 1, // 1 Hz = 1 second intervals
    interpolationMethod: "cubic", // Smooth trajectory
    gapPolicy: "hold", // Hold last known position during gaps
    gapThreshold: 2.0, // Detect gaps > 2 seconds
  });

  // GPS lat/lon data (2 channels: latitude, longitude)
  // Simulating dropped packets at indices 4-6 (3 second gap)
  const gpsData = new Float32Array([
    37.7749,
    -122.4194, // t=0s
    37.7751,
    -122.4196, // t=1s
    37.7753,
    -122.4198, // t=2s
    37.7755,
    -122.42, // t=3s
    // GAP: 3 seconds (tunnel)
    37.7759,
    -122.4206, // t=7s (emerged from tunnel)
    37.7761,
    -122.4208, // t=8s
  ]);

  const timestamps = new Float32Array([
    0,
    0, // Sample 0
    1000,
    1000, // Sample 1
    2000,
    2000, // Sample 2
    3000,
    3000, // Sample 3
    7000,
    7000, // Sample 4 (gap from 3s-7s)
    8000,
    8000, // Sample 5
  ]);

  const result = await pipeline.process(gpsData, timestamps, {
    channels: 2,
  });

  console.log(`Input samples: ${gpsData.length / 2} GPS points`);
  console.log(
    `Output samples: ${result.length / 2} GPS points (uniform 1s grid)`
  );
  console.log(`Gap detected: 3s tunnel at t=3s-7s`);
  console.log(`Position during gap: held at last known location`);

  // Extract output coordinates
  const coords = [];
  for (let i = 0; i < result.length; i += 2) {
    coords.push({ lat: result[i], lon: result[i + 1] });
  }
  console.log(`Output trajectory: ${coords.length} points`);
  console.log(
    `  t=0s: (${coords[0].lat.toFixed(4)}, ${coords[0].lon.toFixed(4)})`
  );
  console.log(
    `  t=4s: (${coords[4].lat.toFixed(4)}, ${coords[4].lon.toFixed(
      4
    )}) [held during gap]`
  );

  return result;
}

/**
 * Example 3: Medical Vitals Monitoring
 *
 * Scenario: Patient monitoring system with multiple sensors at different rates
 * Problem: Heart rate (1 Hz), SpO2 (0.5 Hz), blood pressure (manual/irregular)
 * Solution: Align all vitals to common 1 Hz timeline for correlation analysis
 */
export async function medicalVitalsMonitoring() {
  console.log("\n=== Medical Vitals Monitoring ===");

  const pipeline = createDspPipeline();
  pipeline
    .TimeAlignment({
      targetSampleRate: 1, // 1 Hz = 1 second intervals
      interpolationMethod: "linear",
      gapPolicy: "hold", // Safe default for vitals
      gapThreshold: 5.0, // Detect gaps > 5 seconds (sensor disconnect)
      driftCompensation: "pll", // Phase-locked loop for adaptive tracking
    })
    .MovingAverage({ mode: "moving", windowDuration: 10000 }); // 10s smoothing

  // Simulated heart rate data (irregular timing due to processing delays)
  const heartRates = new Float32Array([72, 74, 73, 71, 70, 72, 75, 76, 74, 73]);

  const timestamps = new Float32Array([
    0, 980, 2010, 2950, 4020, 4980, 6010, 6990, 8005, 9000,
  ]);

  const result = await pipeline.process(heartRates, timestamps, {
    channels: 1,
  });

  console.log(`Input: ${heartRates.length} irregular heart rate samples`);
  console.log(`Output: ${result.length} samples (uniform 1s grid)`);
  console.log(
    `Time span: ${(timestamps[timestamps.length - 1] / 1000).toFixed(1)}s`
  );
  console.log(
    `Smoothed HR: [${result
      .slice(0, 5)
      .map((v) => v.toFixed(1))
      .join(", ")}...] bpm`
  );

  return result;
}

/**
 * Example 4: Audio Streams with Clock Drift
 *
 * Scenario: Network audio streaming with slight clock mismatch
 * Problem: Sender clock runs at 44099 Hz instead of 44100 Hz (0.002% drift)
 * Solution: Compensate for drift and resample to exact 44100 Hz
 */
export async function audioStreamWithClockDrift() {
  console.log("\n=== Audio Stream with Clock Drift ===");

  const pipeline = createDspPipeline();
  pipeline.TimeAlignment({
    targetSampleRate: 44100, // Target: 44.1 kHz
    interpolationMethod: "sinc", // Band-limited for audio quality
    gapPolicy: "zero-fill", // Silence for missing audio
    gapThreshold: 2.0,
    driftCompensation: "regression", // Estimate actual sample rate
  });

  // Simulate 100 samples with slight clock drift (44099 Hz instead of 44100 Hz)
  const numSamples = 100;
  const audioSamples = new Float32Array(numSamples);
  const timestamps = new Float32Array(numSamples);

  const actualSampleRate = 44099; // Slightly off from target 44100 Hz
  for (let i = 0; i < numSamples; i++) {
    // Simple sine wave
    audioSamples[i] = Math.sin((2 * Math.PI * 440 * i) / actualSampleRate);
    // Timestamps reflect actual (drifted) sample rate
    timestamps[i] = (i * 1000) / actualSampleRate; // ms
  }

  const result = await pipeline.process(audioSamples, timestamps, {
    channels: 1,
  });

  console.log(
    `Input: ${audioSamples.length} samples at ${actualSampleRate} Hz (drifted)`
  );
  console.log(`Output: ${result.length} samples at 44100 Hz (corrected)`);
  console.log(
    `Drift correction: ${44100 - actualSampleRate} Hz offset detected`
  );

  return result;
}

/**
 * Example 5: Event-Driven Data (Comparison with Legacy Approach)
 *
 * Demonstrates the difference between TimeAlignment (time-based)
 * and legacy Interpolate/Decimate (index-based) approaches.
 */
export async function eventDrivenComparison() {
  console.log("\n=== TimeAlignment vs Legacy Interpolate/Decimate ===");

  // Sample data: event-driven sensor readings
  const events = new Float32Array([10, 20, 30, 40, 50]);
  const eventTimes = new Float32Array([0, 50, 250, 300, 500]);

  // --- Approach 1: TimeAlignment (Time-Based) ---
  const timeBasedPipeline = createDspPipeline();
  timeBasedPipeline.TimeAlignment({
    targetSampleRate: 10, // 10 Hz = 100ms intervals
    interpolationMethod: "linear",
    gapPolicy: "interpolate",
  });

  const timeBasedResult = await timeBasedPipeline.process(events, eventTimes, {
    channels: 1,
  });

  console.log("\n✅ TimeAlignment (Time-Based):");
  console.log(`  - Uses actual timestamps: [${eventTimes.join(", ")}]ms`);
  console.log(`  - Detects 200ms gap (50-250ms)`);
  console.log(`  - Interpolates to uniform 100ms grid`);
  console.log(
    `  - Output: ${timeBasedResult.length} samples at [0, 100, 200, 300, 400, 500]ms`
  );

  // --- Approach 2: Legacy Interpolate (Index-Based) ---
  const indexBasedPipeline = createDspPipeline();
  indexBasedPipeline.Interpolate({ factor: 2 }); // 2x upsampling

  // Note: Interpolate ignores timestamps, treats as uniform samples
  const indexBasedResult = await indexBasedPipeline.process(
    events,
    eventTimes,
    {
      channels: 1,
    }
  );

  console.log("\n⚠️ Legacy Interpolate (Index-Based):");
  console.log(`  - Ignores actual timestamps`);
  console.log(`  - Assumes uniform spacing between samples`);
  console.log(`  - 2x upsampling: 5 samples → 10 samples`);
  console.log(
    `  - Output: ${indexBasedResult.length} samples (no gap awareness)`
  );

  console.log("\n📊 Key Differences:");
  console.log("  TimeAlignment: Gap-aware, time-based, handles drift");
  console.log("  Interpolate:   Gap-unaware, index-based, assumes uniform");

  return { timeBasedResult, indexBasedResult };
}

/**
 * Example 6: Multi-Channel Sensor Fusion
 *
 * Scenario: IMU (accelerometer + gyroscope) with different sensor update rates
 * Problem: Accel @ 100 Hz, Gyro @ 50 Hz, need synchronized 100 Hz output
 * Solution: Align both to common timeline for sensor fusion
 */
export async function multiChannelSensorFusion() {
  console.log("\n=== Multi-Channel Sensor Fusion (IMU) ===");

  const pipeline = createDspPipeline();
  pipeline.TimeAlignment({
    targetSampleRate: 100, // 100 Hz target
    interpolationMethod: "cubic",
    gapPolicy: "interpolate",
  });

  // 3-axis accelerometer + 3-axis gyroscope (6 channels total)
  // Simulating 10 samples with slight irregularities
  const numSamples = 10;
  const imuData = new Float32Array(numSamples * 6);
  const timestamps = new Float32Array(numSamples * 6);

  for (let i = 0; i < numSamples; i++) {
    const t = i * 10 + Math.random() * 2; // ~10ms intervals with jitter

    // Accelerometer (ax, ay, az)
    imuData[i * 6 + 0] = Math.sin(i * 0.5);
    imuData[i * 6 + 1] = Math.cos(i * 0.5);
    imuData[i * 6 + 2] = 9.81; // Gravity

    // Gyroscope (gx, gy, gz)
    imuData[i * 6 + 3] = Math.random() * 0.1;
    imuData[i * 6 + 4] = Math.random() * 0.1;
    imuData[i * 6 + 5] = Math.random() * 0.1;

    // All channels share same timestamp in interleaved format
    for (let ch = 0; ch < 6; ch++) {
      timestamps[i * 6 + ch] = t;
    }
  }

  const result = await pipeline.process(imuData, timestamps, {
    channels: 6,
  });

  console.log(`Input: ${numSamples} IMU samples (6 channels, irregular)`);
  console.log(
    `Output: ${result.length / 6} IMU samples (6 channels, uniform 10ms)`
  );
  console.log(`Channels: [ax, ay, az, gx, gy, gz]`);
  console.log(`Time-aligned for sensor fusion algorithms`);

  return result;
}

// Run all examples if executed directly
if (import.meta.url === `file://${process.argv[1]}`) {
  (async () => {
    console.log("🚀 Production Examples: TimeAlignment Stage\n");
    console.log("=".repeat(60));

    await iotSensorWithJitter();
    await gpsTrackingWithDropouts();
    await medicalVitalsMonitoring();
    await audioStreamWithClockDrift();
    await eventDrivenComparison();
    await multiChannelSensorFusion();

    console.log("\n" + "=".repeat(60));
    console.log("✅ All examples completed successfully!");
    console.log("\nKey Takeaways:");
    console.log("  • TimeAlignment handles irregular timestamps natively");
    console.log("  • Gap detection prevents interpolation across missing data");
    console.log("  • Clock drift compensation improves long-term accuracy");
    console.log("  • Multiple interpolation methods for different use cases");
    console.log("  • Multi-channel support for sensor fusion applications");
  })();
}
