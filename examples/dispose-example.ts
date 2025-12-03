/**
 * Example demonstrating proper pipeline disposal
 *
 * The dispose() method provides explicit resource cleanup for DspPipeline.
 * This is important for:
 * - Long-running applications with pipeline reuse
 * - Preventing memory leaks when creating many pipelines
 * - Ensuring clean shutdown of audio processing systems
 * - Avoiding race conditions between GC and async processing
 */

import { DspPipeline } from "../src/index";

async function basicDisposalExample() {
  console.log("=== Basic Disposal Example ===\n");

  const pipeline = new DspPipeline(48000);
  pipeline.addStage("lowpass", { cutoff: 1000 });

  const buffer = new Float32Array(1024);
  for (let i = 0; i < buffer.length; i++) {
    buffer[i] = Math.sin((2 * Math.PI * 440 * i) / 48000);
  }

  try {
    await pipeline.process(buffer, { channels: 1 });
    console.log("Processing complete");
  } finally {
    // Always dispose in finally block to ensure cleanup
    pipeline.dispose();
    console.log("Pipeline disposed\n");
  }

  // Attempting to use disposed pipeline will throw error
  try {
    await pipeline.process(buffer, { channels: 1 });
  } catch (error) {
    console.log("Expected error:", (error as Error).message);
  }
}

async function poolingExample() {
  console.log("\n=== Pipeline Pooling Example ===\n");

  class PipelinePool {
    private pipelines: DspPipeline[] = [];

    constructor(private size: number, private sampleRate: number) {}

    acquire(): DspPipeline {
      if (this.pipelines.length > 0) {
        return this.pipelines.pop()!;
      }
      return new DspPipeline(this.sampleRate);
    }

    release(pipeline: DspPipeline) {
      if (this.pipelines.length < this.size) {
        this.pipelines.push(pipeline);
      } else {
        // Dispose excess pipelines
        pipeline.dispose();
      }
    }

    dispose() {
      // Clean up all pooled pipelines
      for (const pipeline of this.pipelines) {
        pipeline.dispose();
      }
      this.pipelines = [];
      console.log("Pipeline pool disposed");
    }
  }

  const pool = new PipelinePool(3, 48000);

  // Use multiple pipelines from pool
  for (let i = 0; i < 5; i++) {
    const pipeline = pool.acquire();
    pipeline.addStage("movingAverage", { windowSize: 10 });

    const buffer = new Float32Array(100).fill(1.0);
    await pipeline.process(buffer, { channels: 1 });

    pool.release(pipeline);
  }

  pool.dispose();
}

async function errorHandlingExample() {
  console.log("\n=== Error Handling Example ===\n");

  const pipeline = new DspPipeline(48000);
  pipeline.addStage("lowpass", { cutoff: 1000 });

  const buffer = new Float32Array(1024);

  // Start async processing
  const processPromise = pipeline.process(buffer, { channels: 1 });

  // Attempt to dispose while processing
  try {
    pipeline.dispose();
  } catch (error) {
    console.log("Cannot dispose during processing:", (error as Error).message);
  }

  // Wait for processing to complete
  await processPromise;

  // Now disposal will succeed
  pipeline.dispose();
  console.log("Pipeline disposed after processing completed");
}

async function resourceCleanupExample() {
  console.log("\n=== Resource Cleanup Example ===\n");

  // Create pipeline with heavy stages
  const pipeline = new DspPipeline(48000);
  pipeline.addStage("fft", { fftSize: 8192 });
  pipeline.addStage("stft", {
    fftSize: 2048,
    hopSize: 512,
    windowType: "hann",
  });
  pipeline.addStage("melSpectrogram", {
    numMelBands: 128,
    minFrequency: 0,
    maxFrequency: 8000,
  });

  console.log("Pipeline created with heavy stages");

  // Process some data
  const buffer = new Float32Array(16384);
  await pipeline.process(buffer, { channels: 1 });

  console.log("Processing complete");

  // Explicit disposal frees all internal buffers immediately
  // - FFT working buffers
  // - STFT overlap buffers
  // - Mel filterbank matrices
  // - All stage internal state
  pipeline.dispose();

  console.log("All resources freed deterministically");
}

async function idempotentDisposalExample() {
  console.log("\n=== Idempotent Disposal Example ===\n");

  const pipeline = new DspPipeline(48000);

  // Dispose is safe to call multiple times
  pipeline.dispose();
  console.log("First dispose: OK");

  pipeline.dispose();
  console.log("Second dispose: OK (no-op)");

  pipeline.dispose();
  console.log("Third dispose: OK (no-op)\n");
}

// Run all examples
async function main() {
  await basicDisposalExample();
  await poolingExample();
  await errorHandlingExample();
  await resourceCleanupExample();
  await idempotentDisposalExample();

  console.log("\n=== All examples completed ===");
}

main().catch(console.error);
