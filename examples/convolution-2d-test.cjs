/**
 * 2D Convolution Test (Multi-Channel)
 *
 * Demonstrates how to perform 2D convolution using the multi-channel API.
 *
 * In this example:
 * - We have a 3x3 spatial grid (e.g., 3 rows, 3 columns of sensors)
 * - Each sensor has 5 time samples
 * - Total channels: 3 * 3 = 9 channels
 *
 * The data is flattened into an interleaved 1D array:
 * [r0c0_t0, r0c1_t0, r0c2_t0, r1c0_t0, r1c1_t0, r1c2_t0, r2c0_t0, r2c1_t0, r2c2_t0,
 *  r0c0_t1, r0c1_t1, r0c2_t1, r1c0_t1, r1c1_t1, r1c2_t1, r2c0_t1, r2c1_t1, r2c2_t1,
 *  ...]
 *
 * We apply a 1D time-domain kernel [0.5, 0.5] to smooth each sensor independently.
 */

const { createDspPipeline } = require("../dist/index.js");

(async () => {
  console.log("=== 2D Convolution Test (Multi-Channel) ===\n");

  // Grid dimensions
  const rows = 3;
  const cols = 3;
  const timeSamples = 5;
  const totalChannels = rows * cols; // 9 channels

  console.log(`Grid: ${rows}x${cols} (${totalChannels} sensors)`);
  console.log(`Time samples per sensor: ${timeSamples}`);
  console.log(`Total data points: ${totalChannels * timeSamples}\n`);

  // Create 2D grid data (simplified for testing)
  // Each sensor has values: [2, 4, 6, 8, 10]
  const grid2D = [];
  for (let r = 0; r < rows; r++) {
    grid2D[r] = [];
    for (let c = 0; c < cols; c++) {
      // Each sensor has the same pattern for easy verification
      grid2D[r][c] = [2, 4, 6, 8, 10];
    }
  }

  console.log("Input 2D Grid (each sensor shows time samples):");
  for (let r = 0; r < rows; r++) {
    let rowStr = `Row ${r}: `;
    for (let c = 0; c < cols; c++) {
      rowStr += `[${grid2D[r][c].join(", ")}] `;
    }
    console.log(rowStr);
  }
  console.log("");

  // Flatten to interleaved 1D array
  // Format: [r0c0_t0, r0c1_t0, r0c2_t0, r1c0_t0, ..., r2c2_t0, r0c0_t1, r0c1_t1, ...]
  const flattenedInput = new Float32Array(totalChannels * timeSamples);
  let idx = 0;
  for (let t = 0; t < timeSamples; t++) {
    for (let r = 0; r < rows; r++) {
      for (let c = 0; c < cols; c++) {
        flattenedInput[idx++] = grid2D[r][c][t];
      }
    }
  }

  console.log("Flattened interleaved input (first 18 values):");
  console.log(Array.from(flattenedInput.slice(0, 18)));
  console.log("");

  // Create 1D time-domain kernel (smoothing)
  const kernel = new Float32Array([0.5, 0.5]);
  console.log("1D Time Kernel:", Array.from(kernel));
  console.log("");

  // ============================================================================
  // BATCH MODE: Process all channels with valid convolution
  // ============================================================================
  console.log("--- BATCH MODE ---");

  const batchPipeline = createDspPipeline();
  batchPipeline.convolution({
    kernel: kernel,
    mode: "batch",
  });

  const batchResult = await batchPipeline.process(flattenedInput, {
    channels: totalChannels,
  });

  // Expected: Each channel gets valid convolution
  // For input [2, 4, 6, 8, 10] with kernel [0.5, 0.5]:
  // output = [3, 5, 7, 9] (length = 5 - 2 + 1 = 4)
  const expectedOutputPerChannel = [3, 5, 7, 9];
  const expectedOutputLength = 4 * totalChannels; // 36

  console.log(
    `Output length: ${batchResult.length} (expected: ${expectedOutputLength})`
  );
  console.log(
    `Output samples per channel: ${batchResult.length / totalChannels}`
  );
  console.log("");

  // Unflatten and display results
  console.log("Output 2D Grid (convolved):");
  const outputSamplesPerChannel = batchResult.length / totalChannels;
  idx = 0;
  let allCorrect = true;

  for (let r = 0; r < rows; r++) {
    let rowStr = `Row ${r}: `;
    for (let c = 0; c < cols; c++) {
      const channelOutput = [];
      for (let t = 0; t < outputSamplesPerChannel; t++) {
        const value = batchResult[t * totalChannels + (r * cols + c)];
        channelOutput.push(value.toFixed(1));

        // Verify correctness
        if (Math.abs(value - expectedOutputPerChannel[t]) > 0.001) {
          allCorrect = false;
        }
      }
      rowStr += `[${channelOutput.join(", ")}] `;
    }
    console.log(rowStr);
  }
  console.log("");

  console.log("Expected per channel:", expectedOutputPerChannel);
  console.log("");

  console.log("Mathematical verification for one channel:");
  console.log("  Input: [2, 4, 6, 8, 10]");
  console.log("  Kernel: [0.5, 0.5]");
  console.log("  Output[0] = 0.5*2 + 0.5*4 = 1 + 2 = 3 ✓");
  console.log("  Output[1] = 0.5*4 + 0.5*6 = 2 + 3 = 5 ✓");
  console.log("  Output[2] = 0.5*6 + 0.5*8 = 3 + 4 = 7 ✓");
  console.log("  Output[3] = 0.5*8 + 0.5*10 = 4 + 5 = 9 ✓");
  console.log("");

  console.log(
    allCorrect ? "✅ ALL CHANNELS CORRECT" : "❌ SOME CHANNELS INCORRECT"
  );
  console.log("");

  // ============================================================================
  // MOVING MODE: Stateful processing
  // ============================================================================
  console.log("--- MOVING MODE ---");

  const movingPipeline = createDspPipeline();
  movingPipeline.convolution({
    kernel: kernel,
    mode: "moving",
  });

  const movingResult = await movingPipeline.process(flattenedInput, {
    channels: totalChannels,
  });

  console.log(
    `Output length: ${movingResult.length} (same as input: ${flattenedInput.length})`
  );
  console.log("");

  // Display first few values per channel
  console.log("First time sample output (all channels):");
  const firstSampleOutput = [];
  for (let i = 0; i < totalChannels; i++) {
    firstSampleOutput.push(movingResult[i].toFixed(1));
  }
  console.log(firstSampleOutput.join(", "));
  console.log("(First sample is 0 for all channels - buffer not full yet)");
  console.log("");

  console.log("Second time sample output (all channels):");
  const secondSampleOutput = [];
  for (let i = 0; i < totalChannels; i++) {
    secondSampleOutput.push(movingResult[totalChannels + i].toFixed(1));
  }
  console.log(secondSampleOutput.join(", "));
  console.log("(All channels should be 3.0 = 0.5*2 + 0.5*4)");
  console.log("");

  // Verify moving mode correctness
  const movingCorrect =
    movingResult.length === flattenedInput.length &&
    Math.abs(movingResult[0]) < 0.001 && // First sample is 0 (buffer filling)
    Math.abs(movingResult[totalChannels] - 3.0) < 0.001; // Second sample is 3.0

  console.log(
    movingCorrect ? "✅ MOVING MODE CORRECT" : "❌ MOVING MODE INCORRECT"
  );
  console.log("");

  // ============================================================================
  // SUMMARY
  // ============================================================================
  console.log("=".repeat(70));
  console.log("SUMMARY");
  console.log("=".repeat(70));
  console.log("");
  console.log("2D Convolution via Multi-Channel API:");
  console.log("  • Data: 3x3 grid (9 channels), 5 time samples each");
  console.log(
    "  • Flattened: Interleaved format [ch0_t0, ch1_t0, ..., ch8_t0, ch0_t1, ...]"
  );
  console.log("  • Kernel: 1D time-domain [0.5, 0.5]");
  console.log("  • Each channel convolved independently");
  console.log("");
  console.log("Batch Mode:");
  console.log(
    "  • Valid convolution: Output length = N - M + 1 = 5 - 2 + 1 = 4"
  );
  console.log("  • All 9 channels produce [3, 5, 7, 9]");
  console.log("");
  console.log("Moving Mode:");
  console.log("  • Stateful: Output length = N = 5 (same as input)");
  console.log("  • First sample is 0 (buffer filling)");
  console.log("  • Subsequent samples are convolved values");
  console.log("");
})();
