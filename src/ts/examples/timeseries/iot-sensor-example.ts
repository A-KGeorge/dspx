/**
 * IoT Sensor Processing with Irregular Timestamps
 *
 * This example demonstrates processing sensor data with network jitter,
 * where samples arrive at irregular intervals.
 */

import { createDspPipeline } from "../../bindings";

interface SensorReading {
  value: number;
  timestamp: number; // Unix timestamp in milliseconds
  sensorId: string;
}

/**
 * Simulates IoT sensor data with realistic network jitter
 */
function generateIrregularSensorData(
  count: number,
  baseInterval: number = 100
): SensorReading[] {
  const readings: SensorReading[] = [];
  let currentTime = 0; // Use relative time starting from 0

  for (let i = 0; i < count; i++) {
    // Add random jitter: ±50% of base interval
    const jitter = (Math.random() - 0.5) * baseInterval;
    currentTime += baseInterval + jitter;

    // Simulate temperature sensor with noise
    const baseTemp = 20 + Math.sin(i * 0.05) * 5; // Varying baseline
    const noise = (Math.random() - 0.5) * 2; // ±1 degree noise

    readings.push({
      value: baseTemp + noise,
      timestamp: currentTime,
      sensorId: "TEMP-001",
    });
  }

  return readings;
}

/**
 * Process sensor data with time-based moving average
 */
async function processSensorData() {
  console.log("=== IoT Sensor Processing Example ===\n");

  // Generate 50 readings with ~100ms intervals (but irregular)
  const readings = generateIrregularSensorData(50, 100);

  console.log("Raw sensor readings (first 5):");
  readings.slice(0, 5).forEach((r, i) => {
    const interval = i > 0 ? r.timestamp - readings[i - 1].timestamp : 0;
    console.log(
      `  [${i}] ${r.value.toFixed(2)}°C at t=${r.timestamp.toFixed(
        0
      )}ms (Δ=${interval.toFixed(0)}ms)`
    );
  });

  // Create pipeline with time-based window (5 second moving average)
  const pipeline = createDspPipeline();
  pipeline.MovingAverage({
    mode: "moving",
    windowDuration: 5000, // 5 seconds
  });

  // Convert to Float32Arrays
  const samples = new Float32Array(readings.map((r) => r.value));
  const timestamps = new Float32Array(readings.map((r) => r.timestamp));

  // Process with timestamps
  const smoothed = await pipeline.process(samples, timestamps, {
    channels: 1,
  });

  console.log("\nSmoothed data (5-second moving average, first 5):");
  for (let i = 0; i < 5; i++) {
    console.log(
      `  [${i}] Raw: ${samples[i].toFixed(2)}°C → Smoothed: ${smoothed[
        i
      ].toFixed(2)}°C`
    );
  }

  // Calculate statistics
  const rawStdDev = calculateStdDev(Array.from(samples));
  const smoothedStdDev = calculateStdDev(Array.from(smoothed));
  const noiseReduction = ((rawStdDev - smoothedStdDev) / rawStdDev) * 100;

  console.log("\nNoise reduction statistics:");
  console.log(`  Raw data std dev: ${rawStdDev.toFixed(3)}°C`);
  console.log(`  Smoothed std dev: ${smoothedStdDev.toFixed(3)}°C`);
  console.log(`  Noise reduction: ${noiseReduction.toFixed(1)}%`);

  // Demonstrate state persistence
  const state = await pipeline.saveState();
  console.log(`\nSaved pipeline state (${state.length} bytes)`);
  console.log(
    "✓ State can be stored in Redis or other persistence layer for recovery"
  );

  // Demonstrate continuing with new data
  console.log("\nProcessing additional sensor readings:");
  const newReadings = generateIrregularSensorData(5, 100);
  const newSamples = new Float32Array(newReadings.map((r) => r.value));
  // Adjust new timestamps to continue from where we left off
  const lastTimestamp = readings[readings.length - 1].timestamp;
  const timeDelta = newReadings[0].timestamp;
  const newTimestamps = new Float32Array(
    newReadings.map((r) => lastTimestamp + (r.timestamp - timeDelta))
  );
  const continued = await pipeline.process(newSamples, newTimestamps, {
    channels: 1,
  });

  console.log("Additional smoothed readings:");
  for (let i = 0; i < continued.length; i++) {
    console.log(
      `  [${i}] ${newSamples[i].toFixed(2)}°C → ${continued[i].toFixed(2)}°C`
    );
  }
}

/**
 * Calculate standard deviation
 */
function calculateStdDev(values: number[]): number {
  const mean = values.reduce((sum, v) => sum + v, 0) / values.length;
  const variance =
    values.reduce((sum, v) => sum + Math.pow(v - mean, 2), 0) / values.length;
  return Math.sqrt(variance);
}

// Run example
processSensorData().catch(console.error);
