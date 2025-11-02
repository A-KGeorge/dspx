/**
 * Phase 5: Drift Detection Example
 *
 * Demonstrates how to monitor timing drift in real-time data streams.
 * Essential for production EMG/biosignal applications where timing accuracy matters.
 */

import {
  createDspPipeline,
  DriftDetector,
  detectGaps,
  validateMonotonicity,
  estimateSampleRate,
  type DriftStatistics,
} from "../../index.js";

console.log("=== Phase 5: Drift Detection Example ===\n");

// Simulate realistic EMG data with timing issues
function generateEMGDataWithDrift() {
  const samples: number[] = [];
  const timestamps: number[] = [];

  const nominalRate = 1000; // 1000 Hz target
  const nominalInterval = 1000 / nominalRate; // 1ms

  let currentTime = 0; // Start from 0 instead of Date.now()

  for (let i = 0; i < 1000; i++) {
    // Simulate EMG signal (muscle activation)
    const signal = Math.sin(i * 0.05) * 100 + Math.random() * 20;
    samples.push(signal);
    timestamps.push(currentTime);

    // Simulate various timing issues
    if (i % 100 === 0) {
      // Every 100 samples: larger drift (BLE packet delay)
      currentTime += nominalInterval + Math.random() * 5; // +0-5ms drift
    } else if (i % 50 === 0) {
      // Every 50 samples: minor jitter
      currentTime += nominalInterval + (Math.random() - 0.5) * 2; // ±1ms jitter
    } else {
      // Normal sampling
      currentTime += nominalInterval;
    }
  }

  return {
    samples: new Float32Array(samples),
    timestamps: new Float32Array(timestamps),
  };
}

// Example 1: Basic Drift Detection
async function example1_BasicDriftDetection() {
  console.log("\n--- Example 1: Basic Drift Detection ---\n");

  const { samples, timestamps } = generateEMGDataWithDrift();

  let driftCount = 0;

  const pipeline = createDspPipeline();
  pipeline.MovingAverage({ mode: "moving", windowDuration: 100 }); // 100ms window

  await pipeline.process(samples, timestamps, {
    channels: 1,
    sampleRate: 1000,
    enableDriftDetection: true,
    driftThreshold: 5, // 5% threshold
    onDriftDetected: (stats) => {
      driftCount++;
      if (driftCount <= 5) {
        // Only show first 5
        console.log(`⚠️  Drift detected at sample ${stats.sampleIndex}:`);
        console.log(`   Expected interval: ${stats.expectedMs.toFixed(3)}ms`);
        console.log(`   Actual interval:   ${stats.deltaMs.toFixed(3)}ms`);
        console.log(
          `   Drift:             ${stats.absoluteDrift.toFixed(
            3
          )}ms (${stats.relativeDrift.toFixed(1)}%)\n`
        );
      }
    },
  });

  console.log(`Total drift events detected: ${driftCount}\n`);
}

// Example 2: Comprehensive Timing Analysis
async function example2_ComprehensiveAnalysis() {
  console.log("\n--- Example 2: Comprehensive Timing Analysis ---\n");

  const { samples, timestamps } = generateEMGDataWithDrift();

  // 1. Estimate sample rate
  const rateEstimate = estimateSampleRate(timestamps);
  console.log("📊 Sample Rate Estimation:");
  console.log(
    `   Estimated rate:     ${rateEstimate.estimatedRate.toFixed(2)} Hz`
  );
  console.log(
    `   Average interval:   ${rateEstimate.averageInterval.toFixed(3)} ms`
  );
  console.log(
    `   Std deviation:      ${rateEstimate.stdDevInterval.toFixed(3)} ms`
  );
  console.log(
    `   Coefficient of var: ${(
      rateEstimate.coefficientOfVariation * 100
    ).toFixed(2)}%`
  );
  console.log(`   Regularity:         ${rateEstimate.regularity}\n`);

  // 2. Detect gaps (missing samples)
  const gaps = detectGaps(timestamps, 1000, 2.0); // 2x expected interval
  console.log(`🕳️  Gap Detection:`);
  console.log(`   Gaps found:         ${gaps.length}`);
  if (gaps.length > 0) {
    gaps.slice(0, 3).forEach((gap, idx) => {
      console.log(
        `   Gap ${idx + 1}: ${gap.durationMs.toFixed(1)}ms (${
          gap.expectedSamples
        } samples missing)`
      );
    });
  }
  console.log();

  // 3. Validate monotonicity
  const violations = validateMonotonicity(timestamps);
  console.log(`✓  Monotonicity Check:`);
  if (violations.length === 0) {
    console.log(`   ✅ All timestamps are monotonically increasing`);
  } else {
    console.log(`   ❌ ${violations.length} violations found:`);
    violations.slice(0, 3).forEach((v) => {
      console.log(
        `      - Sample ${v.index}: ${v.violation} (${v.currentTimestamp} vs ${v.previousTimestamp})`
      );
    });
  }
  console.log();

  // 4. Track metrics over time
  const detector = new DriftDetector({
    expectedSampleRate: 1000,
    driftThreshold: 5,
  });

  detector.processBatch(timestamps);
  const metrics = detector.getMetrics();

  console.log(`📈 Timing Metrics (${metrics.samplesProcessed} samples):`);
  console.log(`   Min interval:       ${metrics.minDelta.toFixed(3)} ms`);
  console.log(`   Max interval:       ${metrics.maxDelta.toFixed(3)} ms`);
  console.log(`   Average interval:   ${metrics.averageDelta.toFixed(3)} ms`);
  console.log(`   Std deviation:      ${metrics.stdDevDelta.toFixed(3)} ms`);
  console.log(`   Drift events:       ${metrics.driftEventsCount}`);
  console.log(
    `   Max drift observed: ${metrics.maxDriftObserved.toFixed(3)} ms\n`
  );
}

// Example 3: Production Monitoring Setup
async function example3_ProductionMonitoring() {
  console.log("\n--- Example 3: Production Monitoring Setup ---\n");

  const { samples, timestamps } = generateEMGDataWithDrift();

  // Simulated metrics backend (Prometheus, Datadog, etc.)
  const metrics = {
    driftEvents: 0,
    maxDrift: 0,
    avgInterval: 0,
    violations: [] as string[],
  };

  const pipeline = createDspPipeline();
  pipeline
    .MovingAverage({ mode: "moving", windowDuration: 100 })
    .Rms({ mode: "moving", windowDuration: 50 });

  await pipeline.process(samples, timestamps, {
    channels: 1,
    sampleRate: 1000,
    enableDriftDetection: true,
    driftThreshold: 10, // 10% for alerting
    onDriftDetected: (stats) => {
      metrics.driftEvents++;
      metrics.maxDrift = Math.max(metrics.maxDrift, stats.absoluteDrift);

      // Alert if drift is severe (>20%)
      if (stats.relativeDrift > 20) {
        metrics.violations.push(
          `CRITICAL: ${stats.relativeDrift.toFixed(1)}% drift at sample ${
            stats.sampleIndex
          }`
        );

        // In production, send to PagerDuty/Slack/etc.
        console.log(`🚨 ALERT: Severe timing drift detected!`);
        console.log(`   Drift: ${stats.relativeDrift.toFixed(1)}%`);
        console.log(`   Sample: ${stats.sampleIndex}`);
        console.log(`   Consider checking BLE connection or sensor battery\n`);
      }
    },
  });

  // Report metrics
  console.log("📊 Production Metrics:");
  console.log(`   Total drift events:     ${metrics.driftEvents}`);
  console.log(`   Max drift observed:     ${metrics.maxDrift.toFixed(3)} ms`);
  console.log(`   Critical violations:    ${metrics.violations.length}`);

  if (metrics.violations.length > 0) {
    console.log("\n   Critical violations:");
    metrics.violations.forEach((v) => console.log(`   - ${v}`));
  }
  console.log();
}

// Example 4: Real-Time Drift Dashboard
async function example4_RealTimeDashboard() {
  console.log("\n--- Example 4: Real-Time Drift Dashboard ---\n");

  const { samples, timestamps } = generateEMGDataWithDrift();

  // Simulate streaming data in chunks
  const chunkSize = 100;
  const detector = new DriftDetector({
    expectedSampleRate: 1000,
    driftThreshold: 5,
  });

  console.log("Streaming EMG data (simulated)...\n");
  console.log("Chunk | Samples | Drift Events | Avg Interval | Status");
  console.log("------|---------|--------------|--------------|--------");

  for (let i = 0; i < samples.length; i += chunkSize) {
    const chunkEnd = Math.min(i + chunkSize, samples.length);
    const chunkTimestamps = timestamps.slice(i, chunkEnd);

    const beforeMetrics = detector.getMetrics();
    detector.processBatch(chunkTimestamps);
    const afterMetrics = detector.getMetrics();

    const chunkDriftEvents =
      afterMetrics.driftEventsCount - beforeMetrics.driftEventsCount;
    const avgInterval = afterMetrics.averageDelta;
    const status = chunkDriftEvents > 5 ? "⚠️  HIGH" : "✅ OK";

    console.log(
      `  ${Math.floor(i / chunkSize)
        .toString()
        .padStart(3)}  | ` +
        `${chunkSize.toString().padStart(7)} | ` +
        `${chunkDriftEvents.toString().padStart(12)} | ` +
        `${avgInterval.toFixed(3).padStart(12)} ms | ` +
        `${status}`
    );
  }

  const finalMetrics = detector.getMetrics();
  console.log("\n📊 Final Statistics:");
  console.log(`   Total samples:      ${finalMetrics.samplesProcessed}`);
  console.log(`   Total drift events: ${finalMetrics.driftEventsCount}`);
  console.log(
    `   Drift rate:         ${(
      (finalMetrics.driftEventsCount / finalMetrics.samplesProcessed) *
      100
    ).toFixed(2)}%\n`
  );
}

// Run all examples
async function main() {
  await example1_BasicDriftDetection();
  await example2_ComprehensiveAnalysis();
  await example3_ProductionMonitoring();
  await example4_RealTimeDashboard();

  console.log("=== Phase 5 Complete ===\n");
  console.log("✅ Drift detection helps you:");
  console.log("   • Debug BLE/IoT timing issues");
  console.log("   • Monitor data quality in production");
  console.log("   • Detect hardware problems early");
  console.log("   • Ensure accurate EMG/biosignal processing\n");
}

main().catch(console.error);
