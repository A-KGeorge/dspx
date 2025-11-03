import { describe, it } from "node:test";
import assert from "node:assert/strict";
import { dotProduct, DspUtils } from "../utils.js";

describe("dotProduct", () => {
  describe("Basic Functionality", () => {
    it("should compute dot product of simple vectors", () => {
      const a = new Float32Array([1, 2, 3, 4]);
      const b = new Float32Array([5, 6, 7, 8]);
      const result = dotProduct(a, b);
      // 1*5 + 2*6 + 3*7 + 4*8 = 5 + 12 + 21 + 32 = 70
      assert.ok(Math.abs(result - 70) < 1e-5);
    });

    it("should compute dot product of all zeros", () => {
      const a = new Float32Array([0, 0, 0, 0]);
      const b = new Float32Array([1, 2, 3, 4]);
      const result = dotProduct(a, b);
      assert.strictEqual(result, 0);
    });

    it("should compute dot product of all ones", () => {
      const a = new Float32Array([1, 1, 1, 1]);
      const b = new Float32Array([1, 1, 1, 1]);
      const result = dotProduct(a, b);
      assert.strictEqual(result, 4);
    });

    it("should compute dot product with negative values", () => {
      const a = new Float32Array([1, -2, 3, -4]);
      const b = new Float32Array([5, 6, 7, 8]);
      const result = dotProduct(a, b);
      // 1*5 + (-2)*6 + 3*7 + (-4)*8 = 5 - 12 + 21 - 32 = -18
      assert.ok(Math.abs(result - -18) < 1e-5);
    });

    it("should compute dot product of orthogonal vectors", () => {
      const a = new Float32Array([1, 0, 0]);
      const b = new Float32Array([0, 1, 0]);
      const result = dotProduct(a, b);
      assert.strictEqual(result, 0);
    });
  });

  describe("SIMD Performance Validation", () => {
    it("should handle small vectors (< 4 elements)", () => {
      const a = new Float32Array([1, 2]);
      const b = new Float32Array([3, 4]);
      const result = dotProduct(a, b);
      // 1*3 + 2*4 = 3 + 8 = 11
      assert.ok(Math.abs(result - 11) < 1e-5);
    });

    it("should handle vectors aligned to SIMD width (4 elements)", () => {
      const a = new Float32Array([1, 2, 3, 4]);
      const b = new Float32Array([2, 3, 4, 5]);
      const result = dotProduct(a, b);
      // 1*2 + 2*3 + 3*4 + 4*5 = 2 + 6 + 12 + 20 = 40
      assert.ok(Math.abs(result - 40) < 1e-5);
    });

    it("should handle vectors aligned to AVX width (8 elements)", () => {
      const a = new Float32Array([1, 2, 3, 4, 5, 6, 7, 8]);
      const b = new Float32Array([8, 7, 6, 5, 4, 3, 2, 1]);
      const result = dotProduct(a, b);
      // 1*8 + 2*7 + 3*6 + 4*5 + 5*4 + 6*3 + 7*2 + 8*1
      // = 8 + 14 + 18 + 20 + 20 + 18 + 14 + 8 = 120
      assert.ok(Math.abs(result - 120) < 1e-5);
    });

    it("should handle vectors with odd length (misaligned)", () => {
      const a = new Float32Array([1, 2, 3, 4, 5, 6, 7]);
      const b = new Float32Array([7, 6, 5, 4, 3, 2, 1]);
      const result = dotProduct(a, b);
      // 1*7 + 2*6 + 3*5 + 4*4 + 5*3 + 6*2 + 7*1
      // = 7 + 12 + 15 + 16 + 15 + 12 + 7 = 84
      assert.ok(Math.abs(result - 84) < 1e-5);
    });

    it("should handle large vectors (stress test SIMD)", () => {
      const size = 1024;
      const a = new Float32Array(size);
      const b = new Float32Array(size);

      for (let i = 0; i < size; i++) {
        a[i] = i;
        b[i] = size - i;
      }

      const result = dotProduct(a, b);

      // Compute expected result using naive JS
      let expected = 0;
      for (let i = 0; i < size; i++) {
        expected += a[i] * b[i];
      }

      // Allow larger tolerance for large vectors due to floating-point accumulation
      assert.ok(Math.abs(result - expected) < Math.abs(expected) * 1e-5);
    });
  });

  describe("Edge Cases", () => {
    it("should handle empty vectors", () => {
      const a = new Float32Array([]);
      const b = new Float32Array([]);
      const result = dotProduct(a, b);
      assert.strictEqual(result, 0);
    });

    it("should handle single element vectors", () => {
      const a = new Float32Array([5]);
      const b = new Float32Array([3]);
      const result = dotProduct(a, b);
      assert.strictEqual(result, 15);
    });

    it("should handle very small values (precision test)", () => {
      const a = new Float32Array([1e-6, 2e-6, 3e-6]);
      const b = new Float32Array([4e-6, 5e-6, 6e-6]);
      const result = dotProduct(a, b);
      // (1e-6)*(4e-6) + (2e-6)*(5e-6) + (3e-6)*(6e-6)
      // = 4e-12 + 10e-12 + 18e-12 = 32e-12
      assert.ok(Math.abs(result - 32e-12) < 1e-15);
    });

    it("should handle very large values", () => {
      const a = new Float32Array([1e6, 2e6, 3e6]);
      const b = new Float32Array([4e6, 5e6, 6e6]);
      const result = dotProduct(a, b);
      // (1e6)*(4e6) + (2e6)*(5e6) + (3e6)*(6e6)
      // = 4e12 + 10e12 + 18e12 = 32e12
      assert.ok(Math.abs(result - 32e12) / 32e12 < 1e-5);
    });
  });

  describe("Error Handling", () => {
    it("should throw TypeError for non-Float32Array first argument", () => {
      assert.throws(() => {
        // @ts-expect-error Testing invalid input
        dotProduct([1, 2, 3], new Float32Array([4, 5, 6]));
      }, TypeError);
    });

    it("should throw TypeError for non-Float32Array second argument", () => {
      assert.throws(() => {
        // @ts-expect-error Testing invalid input
        dotProduct(new Float32Array([1, 2, 3]), [4, 5, 6]);
      }, TypeError);
    });

    it("should throw RangeError for vectors of different lengths", () => {
      const a = new Float32Array([1, 2, 3]);
      const b = new Float32Array([4, 5, 6, 7]);
      assert.throws(() => {
        dotProduct(a, b);
      }, RangeError);
    });

    it("should throw TypeError for null arguments", () => {
      assert.throws(() => {
        // @ts-expect-error Testing invalid input
        dotProduct(null, new Float32Array([1, 2]));
      }, TypeError);

      assert.throws(() => {
        // @ts-expect-error Testing invalid input
        dotProduct(new Float32Array([1, 2]), null);
      }, TypeError);
    });

    it("should throw TypeError for undefined arguments", () => {
      assert.throws(() => {
        // @ts-expect-error Testing invalid input
        dotProduct(undefined, new Float32Array([1, 2]));
      }, TypeError);

      assert.throws(() => {
        // @ts-expect-error Testing invalid input
        dotProduct(new Float32Array([1, 2]), undefined);
      }, TypeError);
    });
  });

  describe("Real-World Use Cases", () => {
    it("should compute signal energy (sum of squares)", () => {
      const signal = new Float32Array([0.5, 0.8, -0.3, 0.2]);
      const energy = dotProduct(signal, signal);
      // 0.5^2 + 0.8^2 + (-0.3)^2 + 0.2^2
      // = 0.25 + 0.64 + 0.09 + 0.04 = 1.02
      assert.ok(Math.abs(energy - 1.02) < 1e-5);
    });

    it("should compute cosine similarity components", () => {
      const v1 = new Float32Array([1, 2, 3]);
      const v2 = new Float32Array([4, 5, 6]);

      const dot = dotProduct(v1, v2); // 32
      const norm1 = Math.sqrt(dotProduct(v1, v1)); // sqrt(14)
      const norm2 = Math.sqrt(dotProduct(v2, v2)); // sqrt(77)
      const cosineSimilarity = dot / (norm1 * norm2);

      assert.ok(Math.abs(cosineSimilarity - 0.9746) < 1e-4);
    });

    it("should compute weighted sum", () => {
      const weights = new Float32Array([0.25, 0.25, 0.25, 0.25]);
      const values = new Float32Array([10, 20, 30, 40]);
      const weightedSum = dotProduct(weights, values);
      // 0.25*10 + 0.25*20 + 0.25*30 + 0.25*40
      // = 2.5 + 5 + 7.5 + 10 = 25 (average)
      assert.ok(Math.abs(weightedSum - 25) < 1e-5);
    });

    it("should compute projection magnitude", () => {
      // Project vector [3, 4] onto unit vector [1, 0]
      const vector = new Float32Array([3, 4]);
      const unitVector = new Float32Array([1, 0]);
      const projectionMagnitude = dotProduct(vector, unitVector);
      assert.strictEqual(projectionMagnitude, 3);
    });

    it("should compute correlation coefficient components", () => {
      const x = new Float32Array([1, 2, 3, 4, 5]);
      const y = new Float32Array([2, 4, 5, 4, 5]);

      // Mean-centered vectors
      const xMean = x.reduce((sum, val) => sum + val, 0) / x.length;
      const yMean = y.reduce((sum, val) => sum + val, 0) / y.length;

      const xCentered = new Float32Array(x.length);
      const yCentered = new Float32Array(y.length);
      for (let i = 0; i < x.length; i++) {
        xCentered[i] = x[i] - xMean;
        yCentered[i] = y[i] - yMean;
      }

      const covariance = dotProduct(xCentered, yCentered) / x.length;
      assert.ok(covariance > 0); // Positive correlation
    });
  });

  describe("DspUtils namespace", () => {
    it("should expose dotProduct through DspUtils", () => {
      const a = new Float32Array([1, 2, 3]);
      const b = new Float32Array([4, 5, 6]);
      const result = DspUtils.dotProduct(a, b);
      assert.ok(Math.abs(result - 32) < 1e-5);
    });

    it("should have the same function reference", () => {
      assert.strictEqual(DspUtils.dotProduct, dotProduct);
    });
  });

  describe("Performance Comparison", () => {
    it("should match naive JavaScript implementation (correctness)", () => {
      const size = 256;
      const a = new Float32Array(size);
      const b = new Float32Array(size);

      // Fill with random values
      for (let i = 0; i < size; i++) {
        a[i] = Math.random() * 100 - 50;
        b[i] = Math.random() * 100 - 50;
      }

      // Native SIMD implementation
      const nativeResult = dotProduct(a, b);

      // Naive JavaScript implementation
      let jsResult = 0;
      for (let i = 0; i < size; i++) {
        jsResult += a[i] * b[i];
      }

      // Should be very close (within floating point error)
      // Use relative tolerance to handle large values
      const relativeTolerance = 1e-4; // 0.01% relative error
      const absoluteTolerance = Math.max(
        1e-3,
        Math.abs(jsResult) * relativeTolerance
      );
      assert.ok(Math.abs(nativeResult - jsResult) < absoluteTolerance);
    });
  });
});
