import nodeGypBuild from "node-gyp-build";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

// Get the directory of the current file
const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);

let DspAddon: any;
// Load the addon using node-gyp-build
try {
  // First, try the path that works when installed
  DspAddon = nodeGypBuild(join(__dirname, ".."));
} catch (e) {
  try {
    // If that fails, try the path that works locally during testing/dev
    DspAddon = nodeGypBuild(join(__dirname, "..", ".."));
  } catch (err: any) {
    // If both fail, throw a more informative error
    throw new Error(
      `Failed to load native DSP addon: ${err?.message || String(err)}`
    );
  }
}

/**
 * Computes the dot product of two vectors using SIMD-accelerated native code.
 *
 * The dot product is computed as: result = sum(a[i] * b[i]) for i = 0 to n-1
 *
 * This implementation uses SSE2/AVX2 SIMD instructions for optimal performance
 * on x86-64 architectures, processing 4-8 elements per instruction.
 *
 * @param a - First input vector (Float32Array)
 * @param b - Second input vector (Float32Array)
 * @returns The dot product as a scalar number
 * @throws {TypeError} If inputs are not Float32Arrays
 * @throws {RangeError} If vectors have different lengths
 *
 * @example
 * ```typescript
 * const a = new Float32Array([1, 2, 3, 4]);
 * const b = new Float32Array([5, 6, 7, 8]);
 * const result = dotProduct(a, b); // 70 = 1*5 + 2*6 + 3*7 + 4*8
 * ```
 *
 * @example
 * ```typescript
 * // Computing vector similarity (cosine similarity requires normalization)
 * const v1 = new Float32Array([1, 2, 3]);
 * const v2 = new Float32Array([4, 5, 6]);
 * const dot = dotProduct(v1, v2); // 32
 *
 * // For cosine similarity:
 * const norm1 = Math.sqrt(dotProduct(v1, v1)); // sqrt(14)
 * const norm2 = Math.sqrt(dotProduct(v2, v2)); // sqrt(77)
 * const cosineSimilarity = dot / (norm1 * norm2); // ~0.9746
 * ```
 *
 * @example
 * ```typescript
 * // Computing signal energy
 * const signal = new Float32Array([0.5, 0.8, -0.3, 0.2]);
 * const energy = dotProduct(signal, signal); // 1.02 = sum of squares
 * ```
 */
export function dotProduct(a: Float32Array, b: Float32Array): number {
  if (!(a instanceof Float32Array)) {
    throw new TypeError("First argument must be a Float32Array");
  }
  if (!(b instanceof Float32Array)) {
    throw new TypeError("Second argument must be a Float32Array");
  }
  if (a.length !== b.length) {
    throw new RangeError(
      `Vector lengths must match: a.length=${a.length}, b.length=${b.length}`
    );
  }

  return DspAddon.dotProduct(a, b);
}

/**
 * Utility functions for DSP operations.
 *
 * @namespace DspUtils
 */
export const DspUtils = {
  /**
   * Computes the dot product of two vectors using SIMD-accelerated native code.
   * @see {@link dotProduct} for detailed documentation
   */
  dotProduct,
};
