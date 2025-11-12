/**
 * Fixed Convolution Test
 *
 * Demonstrates the difference between batch and moving mode convolution
 */

const { createDspPipeline } = require("../dist/index.js");

(async () => {
  console.log("=== Convolution Mode Comparison ===\n");

  const input = new Float32Array([2, 4, 6, 8, 10]);
  const kernel = new Float32Array([0.5, 0.5]);

  console.log("Input:", Array.from(input));
  console.log("Kernel:", Array.from(kernel));
  console.log("");

  // ============================================================================
  // BATCH MODE: Standard convolution (stateless, output length = N - M + 1)
  // ============================================================================
  console.log("--- BATCH MODE (Standard Convolution) ---");

  const batchPipeline = createDspPipeline();
  batchPipeline.convolution({
    kernel: kernel,
    mode: "batch",
  });

  const batchResult = await batchPipeline.process(input, { channels: 1 });

  console.log("Output:", Array.from(batchResult));
  console.log("Output Length:", batchResult.length);
  console.log("Expected: [3, 5, 7, 9] (length = 5 - 2 + 1 = 4)");

  // Verify correctness
  const expectedBatch = [3, 5, 7, 9];
  const batchCorrect =
    batchResult.length === 4 &&
    Math.abs(batchResult[0] - expectedBatch[0]) < 0.001 &&
    Math.abs(batchResult[1] - expectedBatch[1]) < 0.001 &&
    Math.abs(batchResult[2] - expectedBatch[2]) < 0.001 &&
    Math.abs(batchResult[3] - expectedBatch[3]) < 0.001;

  console.log(batchCorrect ? "✅ CORRECT" : "❌ INCORRECT");
  console.log("");
  console.log("Calculation:");
  console.log("  output[0] = 0.5*2 + 0.5*4 = 1 + 2 = 3");
  console.log("  output[1] = 0.5*4 + 0.5*6 = 2 + 3 = 5");
  console.log("  output[2] = 0.5*6 + 0.5*8 = 3 + 4 = 7");
  console.log("  output[3] = 0.5*8 + 0.5*10 = 4 + 5 = 9");
  console.log("");

  // ============================================================================
  // MOVING MODE: Streaming convolution (stateful, output length = N)
  // ============================================================================
  console.log("--- MOVING MODE (Streaming Convolution) ---");

  const movingPipeline = createDspPipeline();
  movingPipeline.convolution({
    kernel: kernel,
    mode: "moving", // Stateful, maintains buffer across process() calls
  });

  const movingResult = await movingPipeline.process(input, { channels: 1 });

  console.log("Output:", Array.from(movingResult));
  console.log("Output Length:", movingResult.length);
  console.log(
    "Note: Moving mode maintains state and outputs same length as input"
  );
})();
