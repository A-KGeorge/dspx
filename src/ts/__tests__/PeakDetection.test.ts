import { test } from "node:test";
import assert from "node:assert/strict";
import { createDspPipeline } from "../index"; // Adjust path as needed
// Note: 'interleave' is not used in these tests, so it's removed.

// Helper function to find indices of peaks
const findPeakIndices = (data: Float32Array): number[] => {
  const indices: number[] = [];
  data.forEach((val, i) => {
    if (val === 1.0) {
      indices.push(i);
    }
  });
  return indices;
};

// --- 1. Parameter Validation Tests ---

test("PeakDetection - Parameter Validation - should throw if threshold is missing", () => {
  const pipeline = createDspPipeline();
  assert.throws(
    // @ts-expect-error
    () => pipeline.PeakDetection({}),
    (err: Error) => {
      assert.strictEqual(err.name, "RangeError");
      assert.match(err.message, /threshold must be >= 0/);
      return true;
    }
  );
});

test("PeakDetection - Parameter Validation - should throw if threshold is negative", () => {
  const pipeline = createDspPipeline();
  assert.throws(
    () => pipeline.PeakDetection({ threshold: -1 }),
    (err: Error) => {
      assert.strictEqual(err.name, "RangeError");
      assert.match(err.message, /threshold must be >= 0/);
      return true;
    }
  );
});

test("PeakDetection - Parameter Validation - should throw if mode is invalid", () => {
  const pipeline = createDspPipeline();
  assert.throws(
    // @ts-expect-error
    () => pipeline.PeakDetection({ threshold: 0.5, mode: "invalid" }),
    (err: Error) => {
      assert.strictEqual(err.name, "TypeError");
      assert.match(err.message, /mode must be 'moving' or 'batch'/);
      return true;
    }
  );
});

test("PeakDetection - Parameter Validation - should throw if domain is invalid", () => {
  const pipeline = createDspPipeline();
  assert.throws(
    // @ts-expect-error
    () => pipeline.PeakDetection({ threshold: 0.5, domain: "invalid" }),
    (err: Error) => {
      assert.strictEqual(err.name, "TypeError");
      assert.match(err.message, /domain must be 'time' or 'frequency'/);
      return true;
    }
  );
});

test("PeakDetection - Parameter Validation - should throw if windowSize is even", () => {
  const pipeline = createDspPipeline();
  assert.throws(
    () => pipeline.PeakDetection({ threshold: 0.5, windowSize: 4 }),
    (err: Error) => {
      assert.strictEqual(err.name, "RangeError");
      assert.match(err.message, /windowSize must be an odd integer >= 3/);
      return true;
    }
  );
});

test("PeakDetection - Parameter Validation - should throw if windowSize is less than 3", () => {
  const pipeline = createDspPipeline();
  assert.throws(
    () => pipeline.PeakDetection({ threshold: 0.5, windowSize: 1 }),
    (err: Error) => {
      assert.strictEqual(err.name, "RangeError");
      assert.match(err.message, /windowSize must be an odd integer >= 3/);
      return true;
    }
  );
});

test("PeakDetection - Parameter Validation - should throw if minPeakDistance is less than 1", () => {
  const pipeline = createDspPipeline();
  assert.throws(
    () => pipeline.PeakDetection({ threshold: 0.5, minPeakDistance: 0 }),
    (err: Error) => {
      assert.strictEqual(err.name, "RangeError");
      assert.match(err.message, /minPeakDistance must be an integer >= 1/);
      return true;
    }
  );
});

test("PeakDetection - Parameter Validation - should warn if using moving mode and windowSize != 3", (t) => {
  const pipeline = createDspPipeline();
  const consoleWarnMock = t.mock.method(console, "warn", () => {});

  pipeline.PeakDetection({
    threshold: 0.5,
    mode: "moving",
    windowSize: 5,
  });

  assert.strictEqual(consoleWarnMock.mock.calls.length, 1);
  assert.match(
    consoleWarnMock.mock.calls[0].arguments[0],
    /"moving' mode only supports windowSize = 3/
  );
});

// --- 2. Mode: 'batch' (Stateless) Tests ---

const batchData = new Float32Array([
  0,
  1,
  5,
  2,
  3, // Peak at 2
  6,
  1,
  0,
  4,
  9, // Peak at 5, Peak at 9
  8,
  2,
  7,
  3,
  0, // Peak at 12
]);
const batchExpectedPeaks = [2, 5, 9, 12];
const batchExpectedOutput = new Float32Array([
  0, 0, 1, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 0,
]);

test("PeakDetection - Mode: 'batch' - should find all peaks with default settings (windowSize=3, minPeakDistance=1)", async () => {
  const pipeline = createDspPipeline();
  pipeline.PeakDetection({
    threshold: 0.5,
    mode: "batch",
  });
  const result = await pipeline.process(batchData.slice(), { channels: 1 });
  assert.deepStrictEqual(findPeakIndices(result), batchExpectedPeaks);
  assert.deepStrictEqual(result, batchExpectedOutput);
});

test("PeakDetection - Mode: 'batch' - should respect threshold", async () => {
  const pipeline = createDspPipeline();
  pipeline.PeakDetection({
    threshold: 7, // Only peaks at 9 and 7 should be found
    mode: "batch",
  });
  const result = await pipeline.process(batchData.slice(), { channels: 1 });
  assert.deepStrictEqual(findPeakIndices(result), [9, 12]);
});

test("PeakDetection - Mode: 'batch' - should enforce minPeakDistance = 3", async () => {
  const pipeline = createDspPipeline();
  const dataDist = new Float32Array([
    0,
    5,
    2, // Peak at 1
    6,
    3, // Peak at 3 (suppressed)
    8,
    2, // Peak at 5 (kept)
    4,
    1, //
    9,
    2, // Peak at 10 (kept)
  ]);
  const expected = new Float32Array([0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0]);

  pipeline.PeakDetection({
    threshold: 0.5,
    mode: "batch",
    minPeakDistance: 3,
  });

  const result = await pipeline.process(dataDist.slice(), { channels: 1 });
  assert.deepStrictEqual(findPeakIndices(result), [1, 5, 10]);
  assert.deepStrictEqual(result, expected);
});

test("PeakDetection - Mode: 'batch' - should work with flexible windowSize = 5 (scalar path)", async () => {
  const pipeline = createDspPipeline();
  // 0, 1, 5, 2, 3, 6, 1, 0, 4, 9, 8, 2, 7, 3, 0
  // See original test file for logic breakdown
  pipeline.PeakDetection({
    threshold: 0.5,
    mode: "batch",
    windowSize: 5,
  });
  const result = await pipeline.process(batchData.slice(), { channels: 1 });
  assert.deepStrictEqual(findPeakIndices(result), [5, 9]);
});

// --- 3. Mode: 'moving' (Stateful) Tests ---

test("PeakDetection - Mode: moving - should find peaks across chunk boundaries (windowSize=3)", async () => {
  const pipeline = createDspPipeline();
  pipeline.PeakDetection({
    threshold: 0.5,
    mode: "moving",
  });

  const data1 = new Float32Array([0, 1, 5, 2]);
  const data2 = new Float32Array([1, 6, 1, 0]);
  const data3 = new Float32Array([4, 9, 8, 2]);
  const data4 = new Float32Array([7, 3, 0]);

  const expected1 = new Float32Array([0, 0, 0, 0]);
  const expected2 = new Float32Array([0, 1, 0, 0]);
  const expected3 = new Float32Array([1, 0, 0, 0]);
  const expected4 = new Float32Array([1, 0, 0]);

  let result = await pipeline.process(data1.slice(), { channels: 1 });
  assert.deepStrictEqual(result, expected1);

  result = await pipeline.process(data2.slice(), { channels: 1 });
  assert.deepStrictEqual(result, expected2);

  result = await pipeline.process(data3.slice(), { channels: 1 });
  assert.deepStrictEqual(result, expected3);

  result = await pipeline.process(data4.slice(), { channels: 1 });
  assert.deepStrictEqual(result, expected4);
});

test("PeakDetection - Mode: moving - should enforce minPeakDistance = 4 across chunks", async () => {
  const pipeline = createDspPipeline();
  const d1 = new Float32Array([0, 5, 2]); // Peak at [1] (5)
  const d2 = new Float32Array([6, 3, 8]); // Peak at [3] (6) (suppressed, dist=2). Peak at [5] (8)
  const d3 = new Float32Array([2, 4, 1]); // Peak at [7] (4) (suppressed, dist=2)
  const d4 = new Float32Array([9, 2, 0]); // Peak at [9] (9) (kept, dist=4)

  pipeline.PeakDetection({
    threshold: 0.5,
    mode: "moving",
    minPeakDistance: 4,
  });

  let res1 = await pipeline.process(d1.slice(), { channels: 1 });
  assert.deepStrictEqual(res1, new Float32Array([0, 0, 0]));

  let res2 = await pipeline.process(d2.slice(), { channels: 1 });
  assert.deepStrictEqual(res2, new Float32Array([1, 0, 0]));

  let res3 = await pipeline.process(d3.slice(), { channels: 1 });
  assert.deepStrictEqual(res3, new Float32Array([1, 0, 0]));

  let res4 = await pipeline.process(d4.slice(), { channels: 1 });
  assert.deepStrictEqual(res4, new Float32Array([0, 0, 1]));

  const state = await pipeline.saveState(); // Corrected: getState -> saveState
  const stageState = JSON.parse(state).stages[0].state;
  assert.strictEqual(stageState.peakCooldown[0], 2); // Cooldown carries over
});

test("PeakDetection - Mode: moving - should reset state and cooldown", async () => {
  const pipeline = createDspPipeline();
  pipeline.PeakDetection({
    threshold: 0.5,
    mode: "moving",
    minPeakDistance: 4,
  });

  await pipeline.process(new Float32Array([0, 5, 2]), { channels: 1 });
  const res2 = await pipeline.process(new Float32Array([6, 3, 8]), {
    channels: 1,
  });
  assert.deepStrictEqual(res2, new Float32Array([1, 0, 0])); // Peak at [1] found

  let state = await pipeline.saveState(); // Corrected: getState -> saveState
  let stageState = JSON.parse(state).stages[0].state;
  assert.strictEqual(stageState.prevSample[0], 8);
  assert.strictEqual(stageState.peakCooldown[0], 3); // Cooldown from peak at [5]

  pipeline.clearState(); // Corrected: reset -> clearState

  state = await pipeline.saveState(); // Corrected: getState -> saveState
  stageState = JSON.parse(state).stages[0].state;
  assert.strictEqual(stageState.prevSample[0], 0);
  assert.strictEqual(stageState.peakCooldown[0], 0);

  // Process again, peak should not be suppressed
  await pipeline.process(new Float32Array([0, 5, 2]), { channels: 1 });
  const res4 = await pipeline.process(new Float32Array([6, 3, 8]), {
    channels: 1,
  });
  assert.deepStrictEqual(res4, new Float32Array([1, 0, 0])); // Peak at [1] found again
});
