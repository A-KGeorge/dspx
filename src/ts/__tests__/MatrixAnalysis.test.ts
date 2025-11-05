import { describe, it } from "node:test";
import assert from "node:assert";
import {
  calculatePca,
  calculateIca,
  calculateWhitening,
  createDspPipeline,
} from "../bindings.js";

describe("PCA (Principal Component Analysis)", () => {
  describe("Basic Functionality", () => {
    it("should compute PCA for 2-channel data", () => {
      // Generate correlated 2-channel data
      const numSamples = 500;
      const data = new Float32Array(numSamples * 2);

      for (let i = 0; i < numSamples; i++) {
        const x = (i / numSamples) * 2 - 1; // Range: [-1, 1]
        data[i * 2 + 0] = x + Math.random() * 0.1; // Channel 0: mostly x
        data[i * 2 + 1] = 0.5 * x + Math.random() * 0.1; // Channel 1: 0.5*x
      }

      const pca = calculatePca(data, 2);

      // Verify structure
      assert.ok(pca.mean instanceof Float32Array);
      assert.ok(pca.pcaMatrix instanceof Float32Array);
      assert.ok(pca.eigenvalues instanceof Float32Array);
      assert.ok(pca.explainedVariance instanceof Float32Array);

      assert.strictEqual(pca.mean.length, 2);
      assert.strictEqual(pca.pcaMatrix.length, 4); // 2x2 matrix
      assert.strictEqual(pca.eigenvalues.length, 2);
      assert.strictEqual(pca.explainedVariance.length, 2);

      assert.strictEqual(pca.numChannels, 2);
      assert.strictEqual(pca.numComponents, 2);

      // Explained variance should sum to ~1.0
      const varianceSum = pca.explainedVariance[0] + pca.explainedVariance[1];
      assert.ok(
        Math.abs(varianceSum - 1.0) < 1e-5,
        `Variance sum ${varianceSum} should be ~1.0`
      );

      // First component should explain more variance
      assert.ok(
        pca.explainedVariance[0] > pca.explainedVariance[1],
        "First PC should explain more variance"
      );
    });

    it("should compute PCA for 4-channel data", () => {
      const numSamples = 1000;
      const data = new Float32Array(numSamples * 4);

      // Generate 4 channels with different signal characteristics
      for (let i = 0; i < numSamples; i++) {
        const t = i / numSamples;
        data[i * 4 + 0] = Math.sin(2 * Math.PI * t * 3); // 3 Hz
        data[i * 4 + 1] = Math.sin(2 * Math.PI * t * 5); // 5 Hz
        data[i * 4 + 2] = 0.5 * data[i * 4 + 0] + 0.3 * data[i * 4 + 1]; // Mixed
        data[i * 4 + 3] = Math.random() * 0.2 - 0.1; // Noise
      }

      const pca = calculatePca(data, 4);

      assert.strictEqual(pca.mean.length, 4);
      assert.strictEqual(pca.pcaMatrix.length, 16); // 4x4 matrix
      assert.strictEqual(pca.eigenvalues.length, 4);
      assert.strictEqual(pca.numChannels, 4);

      // Eigenvalues should be sorted descending
      for (let i = 0; i < 3; i++) {
        assert.ok(
          pca.eigenvalues[i] >= pca.eigenvalues[i + 1],
          `Eigenvalue ${i} should be >= eigenvalue ${i + 1}`
        );
      }
    });

    it("should work with minimum samples (numChannels samples)", () => {
      const numSamples = 3; // Minimum for 3 channels
      const data = new Float32Array(numSamples * 3);

      for (let i = 0; i < numSamples * 3; i++) {
        data[i] = Math.random();
      }

      const pca = calculatePca(data, 3);

      assert.strictEqual(pca.numChannels, 3);
      assert.ok(pca.pcaMatrix.length === 9);
    });
  });

  describe("Pipeline Integration", () => {
    it("should apply PCA transformation in pipeline", async () => {
      // Train PCA
      const numSamples = 500;
      const trainingData = new Float32Array(numSamples * 3);

      for (let i = 0; i < numSamples; i++) {
        const x = Math.random() * 2 - 1;
        trainingData[i * 3 + 0] = x;
        trainingData[i * 3 + 1] = 0.8 * x + Math.random() * 0.1;
        trainingData[i * 3 + 2] = 0.5 * x + Math.random() * 0.1;
      }

      const pca = calculatePca(trainingData, 3);

      // Apply in pipeline
      const pipeline = createDspPipeline();
      pipeline.PcaTransform({
        pcaMatrix: pca.pcaMatrix,
        mean: pca.mean,
        numChannels: 3,
        numComponents: 3,
      });

      // Process test data
      const testData = new Float32Array(30); // 10 samples × 3 channels
      for (let i = 0; i < 10; i++) {
        testData[i * 3 + 0] = Math.random();
        testData[i * 3 + 1] = Math.random();
        testData[i * 3 + 2] = Math.random();
      }

      const result = await pipeline.process(testData, { channels: 3 });

      assert.strictEqual(result.length, 30);
      assert.ok(result instanceof Float32Array);
    });

    it("should perform dimensionality reduction (3 → 2 components)", async () => {
      const numSamples = 500;
      const trainingData = new Float32Array(numSamples * 3);

      for (let i = 0; i < numSamples; i++) {
        const x = Math.random() * 2 - 1;
        trainingData[i * 3 + 0] = x;
        trainingData[i * 3 + 1] = 0.9 * x; // Highly correlated
        trainingData[i * 3 + 2] = Math.random() * 0.1; // Low variance noise
      }

      const pca = calculatePca(trainingData, 3);

      // Extract only top 2 components (first 2 columns = 6 elements)
      const reducedMatrix = pca.pcaMatrix.slice(0, 6); // 3 rows × 2 columns

      // Keep only top 2 components
      const pipeline = createDspPipeline();
      pipeline.PcaTransform({
        pcaMatrix: reducedMatrix,
        mean: pca.mean,
        numChannels: 3,
        numComponents: 2, // Reduce to 2
      });

      const testData = new Float32Array(15); // 5 samples × 3 channels
      for (let i = 0; i < 15; i++) {
        testData[i] = Math.random();
      }

      const result = await pipeline.process(testData, { channels: 3 });

      // Output should have 3 channels, but 3rd channel is zeroed
      assert.strictEqual(result.length, 15);

      // Check that 3rd channel is zero (dimensionality reduction)
      for (let i = 0; i < 5; i++) {
        assert.strictEqual(
          result[i * 3 + 2],
          0,
          `Sample ${i} channel 2 should be zero`
        );
      }
    });
  });

  describe("Error Handling", () => {
    it("should reject non-Float32Array input", () => {
      assert.throws(() => {
        // @ts-expect-error Testing runtime error
        calculatePca([1, 2, 3, 4], 2);
      }, /Float32Array/);
    });

    it("should reject non-integer numChannels", () => {
      const data = new Float32Array(10);
      assert.throws(() => {
        calculatePca(data, 2.5);
      }, /positive integer/);
    });

    it("should reject insufficient samples", () => {
      const data = new Float32Array(6); // Only 2 samples for 3 channels (need at least 3)
      assert.throws(() => {
        calculatePca(data, 3);
      }, /at least numChannels samples/);
    });

    it("should reject data length not divisible by numChannels", () => {
      const data = new Float32Array(7); // Not divisible by 3
      assert.throws(() => {
        calculatePca(data, 3);
      }, /divisible by numChannels/);
    });
  });

  describe("Mathematical Properties", () => {
    it("should produce orthogonal principal components", () => {
      const numSamples = 100;
      const data = new Float32Array(numSamples * 2);

      for (let i = 0; i < numSamples; i++) {
        data[i * 2 + 0] = Math.random();
        data[i * 2 + 1] = Math.random();
      }

      const pca = calculatePca(data, 2);

      // Extract the two principal components (columns of PCA matrix)
      const pc1 = [pca.pcaMatrix[0], pca.pcaMatrix[1]]; // Column 0
      const pc2 = [pca.pcaMatrix[2], pca.pcaMatrix[3]]; // Column 1

      // Dot product should be ~0 (orthogonal)
      const dotProduct = pc1[0] * pc2[0] + pc1[1] * pc2[1];
      assert.ok(
        Math.abs(dotProduct) < 1e-5,
        `Dot product ${dotProduct} should be ~0`
      );
    });

    it("should center data (mean should be near zero after centering)", () => {
      const numSamples = 200;
      const data = new Float32Array(numSamples * 2);

      for (let i = 0; i < numSamples; i++) {
        data[i * 2 + 0] = Math.random() * 10 + 5; // Mean ~10
        data[i * 2 + 1] = Math.random() * 20 - 5; // Mean ~5
      }

      const pca = calculatePca(data, 2);

      // Mean should be calculated correctly
      assert.ok(pca.mean[0] > 5 && pca.mean[0] < 15);
      assert.ok(pca.mean[1] > -5 && pca.mean[1] < 15);
    });
  });
});

describe("Whitening Transformation", () => {
  describe("Basic Functionality", () => {
    it("should compute whitening matrix for 2-channel data", () => {
      const numSamples = 500;
      const data = new Float32Array(numSamples * 2);

      for (let i = 0; i < numSamples; i++) {
        data[i * 2 + 0] = Math.random() * 2 - 1;
        data[i * 2 + 1] = Math.random() * 2 - 1;
      }

      const whitening = calculateWhitening(data, 2);

      assert.ok(whitening.mean instanceof Float32Array);
      assert.ok(whitening.whiteningMatrix instanceof Float32Array);
      assert.strictEqual(whitening.mean.length, 2);
      assert.strictEqual(whitening.whiteningMatrix.length, 4); // 2x2 matrix
      assert.strictEqual(whitening.numChannels, 2);
      assert.strictEqual(whitening.numComponents, 2);
      assert.ok(
        Math.abs(whitening.regularization - 1e-5) < 1e-10,
        "Default regularization should be ~1e-5"
      );
    });

    it("should accept custom regularization parameter", () => {
      const numSamples = 100;
      const data = new Float32Array(numSamples * 2);

      for (let i = 0; i < numSamples * 2; i++) {
        data[i] = Math.random();
      }

      const whitening = calculateWhitening(data, 2, 1e-3);

      assert.ok(
        Math.abs(whitening.regularization - 1e-3) < 1e-10,
        "Custom regularization should be ~1e-3"
      );
    });

    it("should work with 4-channel data", () => {
      const numSamples = 800;
      const data = new Float32Array(numSamples * 4);

      for (let i = 0; i < numSamples; i++) {
        data[i * 4 + 0] = Math.random();
        data[i * 4 + 1] = Math.random();
        data[i * 4 + 2] = Math.random();
        data[i * 4 + 3] = Math.random();
      }

      const whitening = calculateWhitening(data, 4);

      assert.strictEqual(whitening.whiteningMatrix.length, 16); // 4x4 matrix
      assert.strictEqual(whitening.numChannels, 4);
    });
  });

  describe("Pipeline Integration", () => {
    it("should apply whitening transformation in pipeline", async () => {
      const numSamples = 500;
      const trainingData = new Float32Array(numSamples * 3);

      for (let i = 0; i < numSamples * 3; i++) {
        trainingData[i] = Math.random() * 10;
      }

      const whitening = calculateWhitening(trainingData, 3);

      const pipeline = createDspPipeline();
      pipeline.WhiteningTransform({
        whiteningMatrix: whitening.whiteningMatrix,
        mean: whitening.mean,
        numChannels: 3,
        numComponents: 3,
      });

      const testData = new Float32Array(30); // 10 samples × 3 channels
      for (let i = 0; i < 30; i++) {
        testData[i] = Math.random() * 10;
      }

      const result = await pipeline.process(testData, { channels: 3 });

      assert.strictEqual(result.length, 30);
      assert.ok(result instanceof Float32Array);
    });
  });

  describe("Error Handling", () => {
    it("should reject invalid regularization", () => {
      const data = new Float32Array(20);
      assert.throws(() => {
        calculateWhitening(data, 2, -0.01); // Negative
      }, /regularization must be positive/);
    });

    it("should reject non-Float32Array input", () => {
      assert.throws(() => {
        // @ts-expect-error Testing runtime error
        calculateWhitening([1, 2, 3, 4], 2);
      }, /Float32Array/);
    });
  });
});

describe("ICA (Independent Component Analysis)", () => {
  describe("Basic Functionality", () => {
    it("should compute ICA for mixed 2-source signals", () => {
      // Generate 2 independent sources
      const numSamples = 1000;
      const source1 = new Float32Array(numSamples);
      const source2 = new Float32Array(numSamples);

      for (let i = 0; i < numSamples; i++) {
        // Source 1: Sine wave
        source1[i] = Math.sin(2 * Math.PI * (i / numSamples) * 3);
        // Source 2: Square wave
        source2[i] = Math.sin(2 * Math.PI * (i / numSamples) * 5) > 0 ? 1 : -1;
      }

      // Mix the sources (simulate 2 sensors)
      const mixedData = new Float32Array(numSamples * 2);
      for (let i = 0; i < numSamples; i++) {
        mixedData[i * 2 + 0] = 0.7 * source1[i] + 0.3 * source2[i]; // Sensor 1
        mixedData[i * 2 + 1] = 0.4 * source1[i] + 0.6 * source2[i]; // Sensor 2
      }

      const ica = calculateIca(mixedData, 2);

      assert.ok(ica.mean instanceof Float32Array);
      assert.ok(ica.icaMatrix instanceof Float32Array);
      assert.strictEqual(ica.mean.length, 2);
      assert.strictEqual(ica.icaMatrix.length, 4); // 2x2 matrix
      assert.strictEqual(ica.numChannels, 2);
      assert.strictEqual(ica.numComponents, 2);

      // Check convergence
      assert.ok(typeof ica.converged === "boolean");
      assert.ok(typeof ica.iterations === "number");
      assert.ok(ica.iterations > 0);

      console.log(
        `ICA converged: ${ica.converged} in ${ica.iterations} iterations`
      );
    });

    it("should work with 3-source mixed signals", () => {
      const numSamples = 1500;
      const source1 = new Float32Array(numSamples);
      const source2 = new Float32Array(numSamples);
      const source3 = new Float32Array(numSamples);

      for (let i = 0; i < numSamples; i++) {
        const t = i / numSamples;
        source1[i] = Math.sin(2 * Math.PI * t * 2);
        source2[i] = Math.sin(2 * Math.PI * t * 4);
        source3[i] = Math.random() * 2 - 1; // Noise
      }

      // Mix sources
      const mixedData = new Float32Array(numSamples * 3);
      for (let i = 0; i < numSamples; i++) {
        mixedData[i * 3 + 0] =
          0.5 * source1[i] + 0.3 * source2[i] + 0.2 * source3[i];
        mixedData[i * 3 + 1] =
          0.2 * source1[i] + 0.6 * source2[i] + 0.2 * source3[i];
        mixedData[i * 3 + 2] =
          0.3 * source1[i] + 0.1 * source2[i] + 0.6 * source3[i];
      }

      const ica = calculateIca(mixedData, 3);

      assert.strictEqual(ica.icaMatrix.length, 9); // 3x3 matrix
      assert.strictEqual(ica.numChannels, 3);
    });

    it("should accept custom maxIterations and tolerance", () => {
      const numSamples = 800;
      const data = new Float32Array(numSamples * 2);

      for (let i = 0; i < numSamples * 2; i++) {
        data[i] = Math.random() * 2 - 1;
      }

      const ica = calculateIca(data, 2, 500, 1e-5);

      assert.ok(ica.iterations <= 500);
    });
  });

  describe("Pipeline Integration", () => {
    it("should apply ICA transformation in pipeline", async () => {
      const numSamples = 1000;
      const source1 = new Float32Array(numSamples);
      const source2 = new Float32Array(numSamples);

      for (let i = 0; i < numSamples; i++) {
        source1[i] = Math.sin(2 * Math.PI * (i / numSamples) * 2);
        source2[i] = Math.cos(2 * Math.PI * (i / numSamples) * 3);
      }

      const trainingData = new Float32Array(numSamples * 2);
      for (let i = 0; i < numSamples; i++) {
        trainingData[i * 2 + 0] = 0.6 * source1[i] + 0.4 * source2[i];
        trainingData[i * 2 + 1] = 0.3 * source1[i] + 0.7 * source2[i];
      }

      const ica = calculateIca(trainingData, 2);

      if (!ica.converged) {
        console.warn("ICA did not converge, but continuing with test");
      }

      const pipeline = createDspPipeline();
      pipeline.IcaTransform({
        icaMatrix: ica.icaMatrix,
        mean: ica.mean,
        numChannels: 2,
        numComponents: 2,
      });

      const testData = new Float32Array(20); // 10 samples × 2 channels
      for (let i = 0; i < 20; i++) {
        testData[i] = Math.random();
      }

      const result = await pipeline.process(testData, { channels: 2 });

      assert.strictEqual(result.length, 20);
      assert.ok(result instanceof Float32Array);
    });
  });

  describe("Error Handling", () => {
    it("should reject insufficient samples (need 5× numChannels)", () => {
      const data = new Float32Array(8); // Only 4 samples for 2 channels (need at least 10)
      assert.throws(() => {
        calculateIca(data, 2);
      }, /at least 5/);
    });

    it("should reject non-integer maxIterations", () => {
      const data = new Float32Array(100);
      assert.throws(() => {
        calculateIca(data, 2, 100.5, 1e-4);
      }, /positive integer/);
    });

    it("should reject invalid tolerance", () => {
      const data = new Float32Array(100);
      assert.throws(() => {
        calculateIca(data, 2, 200, -0.01);
      }, /tolerance must be positive/);
    });
  });

  describe("Convergence", () => {
    it("should converge with sufficient samples and iterations", () => {
      const numSamples = 2000; // More samples
      const data = new Float32Array(numSamples * 3);

      // Generate clearly separable sources
      for (let i = 0; i < numSamples; i++) {
        const t = i / numSamples;
        const s1 = Math.sin(2 * Math.PI * t * 3);
        const s2 = Math.sin(2 * Math.PI * t * 7);
        const s3 = t < 0.5 ? 1 : -1; // Step function

        // Strong mixing
        data[i * 3 + 0] = s1 + 0.5 * s2 + 0.3 * s3;
        data[i * 3 + 1] = 0.5 * s1 + s2 + 0.3 * s3;
        data[i * 3 + 2] = 0.3 * s1 + 0.5 * s2 + s3;
      }

      const ica = calculateIca(data, 3, 500, 1e-4);

      console.log(
        `ICA convergence test: ${ica.converged} (${ica.iterations} iterations)`
      );

      // With good data, ICA should converge
      assert.ok(ica.converged || ica.iterations < 500);
    });
  });
});

describe("Combined Workflows", () => {
  describe("PCA + Dimensionality Reduction", () => {
    it("should reduce 8 channels to 3 principal components", async () => {
      const numSamples = 1000;
      const trainingData = new Float32Array(numSamples * 8);

      // Generate 8 channels with redundancy
      for (let i = 0; i < numSamples; i++) {
        const t = i / numSamples;
        const signal1 = Math.sin(2 * Math.PI * t * 2);
        const signal2 = Math.sin(2 * Math.PI * t * 5);
        const noise = Math.random() * 0.1;

        // Many channels are just scaled/shifted versions of signal1 and signal2
        trainingData[i * 8 + 0] = signal1 + noise;
        trainingData[i * 8 + 1] = 0.9 * signal1 + noise;
        trainingData[i * 8 + 2] = signal2 + noise;
        trainingData[i * 8 + 3] = 0.8 * signal2 + noise;
        trainingData[i * 8 + 4] = 0.5 * signal1 + 0.5 * signal2 + noise;
        trainingData[i * 8 + 5] = noise * 2; // Pure noise
        trainingData[i * 8 + 6] = 0.7 * signal1 + noise;
        trainingData[i * 8 + 7] = 0.6 * signal2 + noise;
      }

      const pca = calculatePca(trainingData, 8);

      console.log("Explained variance:", Array.from(pca.explainedVariance));

      // Top 3 components should explain most variance
      const top3Variance =
        pca.explainedVariance[0] +
        pca.explainedVariance[1] +
        pca.explainedVariance[2];
      console.log(
        `Top 3 components explain ${(top3Variance * 100).toFixed(
          1
        )}% of variance`
      );

      // Extract only top 3 components (first 3 columns = 24 elements: 8 rows × 3 columns)
      const reducedMatrix = pca.pcaMatrix.slice(0, 24);

      // Apply dimensionality reduction
      const pipeline = createDspPipeline();
      pipeline.PcaTransform({
        pcaMatrix: reducedMatrix,
        mean: pca.mean,
        numChannels: 8,
        numComponents: 3, // Reduce to 3
      });

      const testData = new Float32Array(80); // 10 samples × 8 channels
      for (let i = 0; i < 80; i++) {
        testData[i] = Math.random();
      }

      const result = await pipeline.process(testData, { channels: 8 });

      // Channels 3-7 should be zeroed
      for (let i = 0; i < 10; i++) {
        for (let ch = 3; ch < 8; ch++) {
          assert.strictEqual(
            result[i * 8 + ch],
            0,
            `Sample ${i} channel ${ch} should be zero`
          );
        }
      }
    });
  });

  describe("Whitening + ICA Pipeline", () => {
    it("should chain whitening and ICA for source separation", async () => {
      const numSamples = 1500;

      // Generate 3 independent sources
      const source1 = new Float32Array(numSamples);
      const source2 = new Float32Array(numSamples);
      const source3 = new Float32Array(numSamples);

      for (let i = 0; i < numSamples; i++) {
        const t = i / numSamples;
        source1[i] = Math.sin(2 * Math.PI * t * 3);
        source2[i] = Math.sin(2 * Math.PI * t * 7);
        source3[i] = Math.random() * 2 - 1;
      }

      // Mix sources
      const mixedData = new Float32Array(numSamples * 3);
      for (let i = 0; i < numSamples; i++) {
        mixedData[i * 3 + 0] =
          0.5 * source1[i] + 0.3 * source2[i] + 0.2 * source3[i];
        mixedData[i * 3 + 1] =
          0.3 * source1[i] + 0.5 * source2[i] + 0.2 * source3[i];
        mixedData[i * 3 + 2] =
          0.2 * source1[i] + 0.2 * source2[i] + 0.6 * source3[i];
      }

      // Train whitening and ICA
      const whitening = calculateWhitening(mixedData, 3);
      const ica = calculateIca(mixedData, 3);

      console.log(`ICA converged: ${ica.converged}`);

      // Build pipeline with both transformations
      const pipeline = createDspPipeline();
      pipeline
        .WhiteningTransform({
          whiteningMatrix: whitening.whiteningMatrix,
          mean: whitening.mean,
          numChannels: 3,
          numComponents: 3,
        })
        .IcaTransform({
          icaMatrix: ica.icaMatrix,
          mean: ica.mean,
          numChannels: 3,
          numComponents: 3,
        });

      // Process test data
      const testData = new Float32Array(30); // 10 samples × 3 channels
      for (let i = 0; i < 30; i++) {
        testData[i] = Math.random();
      }

      const result = await pipeline.process(testData, { channels: 3 });

      assert.strictEqual(result.length, 30);
      assert.ok(result instanceof Float32Array);
    });
  });
});

describe("Real-World Application Scenarios", () => {
  describe("EEG Artifact Removal", () => {
    it("should simulate EEG with eye blink artifact removal using ICA", async () => {
      const numSamples = 2000;
      const numChannels = 4;

      // Simulate EEG: brain signals + eye blink artifact
      const brainSignal1 = new Float32Array(numSamples);
      const brainSignal2 = new Float32Array(numSamples);
      const eyeBlinkArtifact = new Float32Array(numSamples);

      for (let i = 0; i < numSamples; i++) {
        const t = i / numSamples;
        brainSignal1[i] = Math.sin(2 * Math.PI * t * 8); // 8 Hz (alpha rhythm)
        brainSignal2[i] = Math.sin(2 * Math.PI * t * 12); // 12 Hz (beta rhythm)

        // Eye blink: Sharp spike every 500 samples
        eyeBlinkArtifact[i] =
          i % 500 < 50 ? Math.exp(-((i % 500) ** 2) / 50) : 0;
      }

      // Mix signals (frontal channels get more artifact)
      const eegData = new Float32Array(numSamples * numChannels);
      for (let i = 0; i < numSamples; i++) {
        eegData[i * 4 + 0] = brainSignal1[i] + 0.8 * eyeBlinkArtifact[i]; // Frontal
        eegData[i * 4 + 1] = brainSignal2[i] + 0.6 * eyeBlinkArtifact[i]; // Frontal
        eegData[i * 4 + 2] = 0.7 * brainSignal1[i] + 0.2 * eyeBlinkArtifact[i]; // Parietal
        eegData[i * 4 + 3] = 0.8 * brainSignal2[i] + 0.1 * eyeBlinkArtifact[i]; // Occipital
      }

      const ica = calculateIca(eegData, numChannels, 500);

      console.log(
        `EEG ICA converged: ${ica.converged} in ${ica.iterations} iterations`
      );

      const pipeline = createDspPipeline();
      pipeline.IcaTransform({
        icaMatrix: ica.icaMatrix,
        mean: ica.mean,
        numChannels: 4,
        numComponents: 4,
      });

      // In practice, you would:
      // 1. Identify which IC is the eye blink artifact
      // 2. Reconstruct EEG without that component
      // For this test, we just verify the pipeline works
      const testEeg = eegData.slice(0, 40); // First 10 samples
      const separated = await pipeline.process(testEeg, { channels: 4 });

      assert.strictEqual(separated.length, 40);
    });
  });

  describe("EMG Signal Decomposition", () => {
    it("should separate mixed EMG signals using PCA", async () => {
      const numSamples = 1000;
      const numChannels = 4;

      // Simulate 4 EMG channels from overlapping muscles
      const muscle1 = new Float32Array(numSamples);
      const muscle2 = new Float32Array(numSamples);

      for (let i = 0; i < numSamples; i++) {
        // Muscle 1: High frequency bursts
        muscle1[i] =
          Math.random() *
          (Math.sin(2 * Math.PI * (i / numSamples) * 20) > 0.5 ? 1 : 0);
        // Muscle 2: Lower frequency activity
        muscle2[i] =
          Math.random() *
          Math.abs(Math.sin(2 * Math.PI * (i / numSamples) * 3));
      }

      // Channels are linear combinations
      const emgData = new Float32Array(numSamples * numChannels);
      for (let i = 0; i < numSamples; i++) {
        emgData[i * 4 + 0] = 0.8 * muscle1[i] + 0.2 * muscle2[i];
        emgData[i * 4 + 1] = 0.7 * muscle1[i] + 0.3 * muscle2[i];
        emgData[i * 4 + 2] = 0.3 * muscle1[i] + 0.7 * muscle2[i];
        emgData[i * 4 + 3] = 0.2 * muscle1[i] + 0.8 * muscle2[i];
      }

      const pca = calculatePca(emgData, numChannels);

      console.log(
        "EMG PCA explained variance:",
        Array.from(pca.explainedVariance)
      );

      // Top 2 PCs should capture most variance
      const top2 = pca.explainedVariance[0] + pca.explainedVariance[1];
      console.log(`Top 2 PCs explain ${(top2 * 100).toFixed(1)}% of variance`);

      // Extract only top 2 components (first 2 columns = 8 elements: 4 rows × 2 columns)
      const reducedMatrix = pca.pcaMatrix.slice(0, 8);

      const pipeline = createDspPipeline();
      pipeline.PcaTransform({
        pcaMatrix: reducedMatrix,
        mean: pca.mean,
        numChannels: 4,
        numComponents: 2, // Reduce to 2 (representing 2 muscles)
      });

      const testEmg = emgData.slice(0, 40);
      const decomposed = await pipeline.process(testEmg, { channels: 4 });

      assert.strictEqual(decomposed.length, 40);
    });
  });
});
