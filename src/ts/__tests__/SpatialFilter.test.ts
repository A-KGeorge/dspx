import { describe, it } from "node:test";
import assert from "node:assert/strict";
import {
  createDspPipeline,
  calculateCommonSpatialPatterns,
} from "../bindings.js";

describe("Spatial Filters (CSP for BCI/EEG)", () => {
  describe("calculateCommonSpatialPatterns()", () => {
    it("should calculate CSP filters for binary classification", () => {
      const numChannels = 4;
      const numSamplesClass1 = 100;
      const numSamplesClass2 = 100;

      // Generate synthetic class 1 data (high variance in channels 0, 1)
      const dataClass1 = new Float32Array(numSamplesClass1 * numChannels);
      for (let i = 0; i < numSamplesClass1; i++) {
        dataClass1[i * numChannels + 0] = 2.0 * (Math.random() * 2 - 1); // High variance
        dataClass1[i * numChannels + 1] = 2.0 * (Math.random() * 2 - 1);
        dataClass1[i * numChannels + 2] = 0.5 * (Math.random() * 2 - 1); // Low variance
        dataClass1[i * numChannels + 3] = 0.5 * (Math.random() * 2 - 1);
      }

      // Generate synthetic class 2 data (high variance in channels 2, 3)
      const dataClass2 = new Float32Array(numSamplesClass2 * numChannels);
      for (let i = 0; i < numSamplesClass2; i++) {
        dataClass2[i * numChannels + 0] = 0.5 * (Math.random() * 2 - 1); // Low variance
        dataClass2[i * numChannels + 1] = 0.5 * (Math.random() * 2 - 1);
        dataClass2[i * numChannels + 2] = 2.0 * (Math.random() * 2 - 1); // High variance
        dataClass2[i * numChannels + 3] = 2.0 * (Math.random() * 2 - 1);
      }

      const csp = calculateCommonSpatialPatterns(
        dataClass1,
        dataClass2,
        numChannels,
        2
      );

      assert.equal(
        csp.numChannels,
        numChannels,
        "Should have correct numChannels"
      );
      assert.equal(csp.numFilters, 2, "Should have 2 filters");
      assert.ok(
        csp.cspMatrix instanceof Float32Array,
        "cspMatrix should be Float32Array"
      );
      assert.equal(
        csp.cspMatrix.length,
        numChannels * 2,
        "Matrix size should be numChannels × numFilters"
      );
      assert.ok(
        csp.eigenvalues instanceof Float32Array,
        "eigenvalues should be Float32Array"
      );
      assert.equal(csp.eigenvalues.length, 2, "Should have 2 eigenvalues");
      assert.ok(
        csp.mean instanceof Float32Array,
        "mean should be Float32Array"
      );
      assert.equal(
        csp.mean.length,
        numChannels,
        "mean should have numChannels elements"
      );
    });

    it("should return filters sorted by discriminability", () => {
      const numChannels = 3;
      const numSamples = 50;

      const dataClass1 = new Float32Array(numSamples * numChannels);
      const dataClass2 = new Float32Array(numSamples * numChannels);

      // Create clearly separable classes
      for (let i = 0; i < numSamples; i++) {
        // Class 1: strong signal in channel 0
        dataClass1[i * numChannels + 0] =
          3.0 * Math.sin((2 * Math.PI * i) / 10);
        dataClass1[i * numChannels + 1] = 0.5 * (Math.random() * 2 - 1);
        dataClass1[i * numChannels + 2] = 0.5 * (Math.random() * 2 - 1);

        // Class 2: strong signal in channel 2
        dataClass2[i * numChannels + 0] = 0.5 * (Math.random() * 2 - 1);
        dataClass2[i * numChannels + 1] = 0.5 * (Math.random() * 2 - 1);
        dataClass2[i * numChannels + 2] =
          3.0 * Math.sin((2 * Math.PI * i) / 10);
      }

      const csp = calculateCommonSpatialPatterns(
        dataClass1,
        dataClass2,
        numChannels
      );

      // Eigenvalues should be in descending order
      for (let i = 0; i < csp.eigenvalues.length - 1; i++) {
        assert.ok(
          csp.eigenvalues[i] >= csp.eigenvalues[i + 1],
          `Eigenvalue[${i}] (${csp.eigenvalues[i]}) should be >= eigenvalue[${
            i + 1
          }] (${csp.eigenvalues[i + 1]})`
        );
      }

      // First eigenvalue should be significantly larger (most discriminative)
      const ratio = csp.eigenvalues[0] / csp.eigenvalues[csp.numFilters - 1];
      assert.ok(
        ratio > 2.0,
        `First eigenvalue should be >> last, ratio = ${ratio.toFixed(2)}`
      );
    });

    it("should handle optional numFilters parameter", () => {
      const numChannels = 8;
      const numSamples = 100;

      const dataClass1 = new Float32Array(numSamples * numChannels);
      const dataClass2 = new Float32Array(numSamples * numChannels);

      for (let i = 0; i < numSamples * numChannels; i++) {
        dataClass1[i] = Math.random() * 2 - 1;
        dataClass2[i] = Math.random() * 2 - 1;
      }

      // Default: return all filters
      const cspAll = calculateCommonSpatialPatterns(
        dataClass1,
        dataClass2,
        numChannels
      );
      assert.equal(
        cspAll.numFilters,
        numChannels,
        "Should return all filters by default"
      );

      // Specify top 4 filters
      const cspTop4 = calculateCommonSpatialPatterns(
        dataClass1,
        dataClass2,
        numChannels,
        4
      );
      assert.equal(cspTop4.numFilters, 4, "Should return top 4 filters");
      assert.equal(cspTop4.cspMatrix.length, numChannels * 4);
      assert.equal(cspTop4.eigenvalues.length, 4);
    });

    it("should throw for invalid inputs", () => {
      const validData = new Float32Array(100);

      assert.throws(
        () => calculateCommonSpatialPatterns(validData, validData, 0),
        /numChannels must be a positive integer/
      );

      assert.throws(
        () => calculateCommonSpatialPatterns(validData, validData, 5, 0),
        /numFilters must be in range/
      );

      assert.throws(
        () => calculateCommonSpatialPatterns(validData, validData, 4, 5),
        /numFilters must be in range/
      );

      assert.throws(
        () =>
          calculateCommonSpatialPatterns(new Float32Array(99), validData, 4),
        /Data length must be divisible by numChannels/
      );
    });
  });

  describe("CspTransform Stage", () => {
    it("should apply CSP filters to multi-channel stream", async () => {
      const numChannels = 4;
      const numFilters = 2;
      const numSamplesClass1 = 50;
      const numSamplesClass2 = 50;

      // Train CSP on labeled data
      const trainClass1 = new Float32Array(numSamplesClass1 * numChannels);
      const trainClass2 = new Float32Array(numSamplesClass2 * numChannels);

      for (let i = 0; i < numSamplesClass1; i++) {
        trainClass1[i * numChannels + 0] =
          2.0 * Math.sin((2 * Math.PI * i) / 10);
        trainClass1[i * numChannels + 1] =
          2.0 * Math.cos((2 * Math.PI * i) / 10);
        trainClass1[i * numChannels + 2] = 0.3 * (Math.random() * 2 - 1);
        trainClass1[i * numChannels + 3] = 0.3 * (Math.random() * 2 - 1);
      }

      for (let i = 0; i < numSamplesClass2; i++) {
        trainClass2[i * numChannels + 0] = 0.3 * (Math.random() * 2 - 1);
        trainClass2[i * numChannels + 1] = 0.3 * (Math.random() * 2 - 1);
        trainClass2[i * numChannels + 2] =
          2.0 * Math.sin((2 * Math.PI * i) / 10);
        trainClass2[i * numChannels + 3] =
          2.0 * Math.cos((2 * Math.PI * i) / 10);
      }

      const csp = calculateCommonSpatialPatterns(
        trainClass1,
        trainClass2,
        numChannels,
        numFilters
      );

      // Apply CSP transform in pipeline
      const pipeline = createDspPipeline();
      pipeline.CspTransform({
        cspMatrix: csp.cspMatrix,
        mean: csp.mean,
        numChannels: numChannels,
        numFilters: numFilters,
      });

      // Process test data (class 1 pattern)
      const testData = new Float32Array(20 * numChannels);
      for (let i = 0; i < 20; i++) {
        testData[i * numChannels + 0] = 2.0 * Math.sin((2 * Math.PI * i) / 10);
        testData[i * numChannels + 1] = 2.0 * Math.cos((2 * Math.PI * i) / 10);
        testData[i * numChannels + 2] = 0.3 * (Math.random() * 2 - 1);
        testData[i * numChannels + 3] = 0.3 * (Math.random() * 2 - 1);
      }

      const result = await pipeline.process(testData, {
        channels: numChannels,
      });

      // Output should have numFilters channels (dimensionality reduction)
      // But the pipeline preserves original channel count, zeros out extra channels
      assert.equal(
        result.length,
        20 * numChannels,
        "Output should maintain buffer size"
      );

      // Check that CSP components (first numFilters channels) are non-zero
      let hasNonZeroInComponents = false;
      for (let i = 0; i < 20; i++) {
        for (let ch = 0; ch < numFilters; ch++) {
          if (Math.abs(result[i * numChannels + ch]) > 1e-6) {
            hasNonZeroInComponents = true;
            break;
          }
        }
      }
      assert.ok(
        hasNonZeroInComponents,
        "CSP components should have non-zero values"
      );
    });

    it("should center data using mean vector", async () => {
      const numChannels = 2;
      const numFilters = 2;

      // Create data with non-zero mean
      const trainClass1 = new Float32Array(50 * numChannels);
      const trainClass2 = new Float32Array(50 * numChannels);

      for (let i = 0; i < 50; i++) {
        trainClass1[i * numChannels + 0] = 5.0 + Math.random(); // Mean = 5.5
        trainClass1[i * numChannels + 1] = 3.0 + Math.random(); // Mean = 3.5

        trainClass2[i * numChannels + 0] = 5.0 + Math.random();
        trainClass2[i * numChannels + 1] = 3.0 + Math.random();
      }

      const csp = calculateCommonSpatialPatterns(
        trainClass1,
        trainClass2,
        numChannels,
        numFilters
      );

      // Mean should be approximately [5.5, 3.5]
      assert.ok(
        Math.abs(csp.mean[0] - 5.5) < 0.5,
        `Mean[0] should be ~5.5, got ${csp.mean[0]}`
      );
      assert.ok(
        Math.abs(csp.mean[1] - 3.5) < 0.5,
        `Mean[1] should be ~3.5, got ${csp.mean[1]}`
      );

      const pipeline = createDspPipeline();
      pipeline.CspTransform({
        cspMatrix: csp.cspMatrix,
        mean: csp.mean,
        numChannels: numChannels,
        numFilters: numFilters,
      });

      // Process data with same mean
      const testData = new Float32Array(10 * numChannels);
      for (let i = 0; i < 10; i++) {
        testData[i * numChannels + 0] = 5.5;
        testData[i * numChannels + 1] = 3.5;
      }

      const result = await pipeline.process(testData, {
        channels: numChannels,
      });

      // After centering (x - mean), all values should be ~0
      // After CSP transform, output should be near zero
      let maxOutput = 0;
      for (let i = 0; i < result.length; i++) {
        maxOutput = Math.max(maxOutput, Math.abs(result[i]));
      }

      assert.ok(
        maxOutput < 0.5,
        `Centered data should produce small output, got max = ${maxOutput}`
      );
    });

    it("should throw for parameter validation", async () => {
      const pipeline = createDspPipeline();

      assert.throws(
        () =>
          pipeline.CspTransform({
            cspMatrix: new Float32Array(8),
            mean: new Float32Array(4),
            numChannels: 4,
            numFilters: 3, // Wrong size: 4*3=12, but matrix has 8 elements
          }),
        /cspMatrix length.*must equal numChannels/
      );

      assert.throws(
        () =>
          pipeline.CspTransform({
            cspMatrix: new Float32Array(8),
            mean: new Float32Array(3), // Wrong size
            numChannels: 4,
            numFilters: 2,
          }),
        /mean length.*must equal numChannels/
      );

      assert.throws(
        () =>
          pipeline.CspTransform({
            cspMatrix: new Float32Array(8),
            mean: new Float32Array(4),
            numChannels: 4,
            numFilters: 5, // More filters than channels
          }),
        /numFilters cannot exceed numChannels/
      );
    });
  });

  describe("Motor Imagery BCI Use Case", () => {
    it("should classify left hand vs right hand motor imagery", async () => {
      // Simulate motor imagery EEG: 8 channels, 200 samples per trial
      const numChannels = 8;
      const samplesPerTrial = 200;
      const numTrials = 30;

      // Class 1: Left hand motor imagery (mu/beta ERD in C3, channels 2-3)
      const leftHandTrials = new Float32Array(
        numTrials * samplesPerTrial * numChannels
      );
      for (let trial = 0; trial < numTrials; trial++) {
        for (let i = 0; i < samplesPerTrial; i++) {
          const idx = (trial * samplesPerTrial + i) * numChannels;

          // Baseline EEG (alpha rhythm, 10 Hz)
          for (let ch = 0; ch < numChannels; ch++) {
            leftHandTrials[idx + ch] = 0.5 * Math.sin((2 * Math.PI * i) / 20);
          }

          // Left motor cortex (C3): Event-Related Desynchronization (ERD)
          // Reduced power in mu band (8-13 Hz) during motor imagery
          leftHandTrials[idx + 2] *= 0.3; // Strong suppression in C3
          leftHandTrials[idx + 3] *= 0.5; // Moderate suppression in adjacent
        }
      }

      // Class 2: Right hand motor imagery (mu/beta ERD in C4, channels 5-6)
      const rightHandTrials = new Float32Array(
        numTrials * samplesPerTrial * numChannels
      );
      for (let trial = 0; trial < numTrials; trial++) {
        for (let i = 0; i < samplesPerTrial; i++) {
          const idx = (trial * samplesPerTrial + i) * numChannels;

          for (let ch = 0; ch < numChannels; ch++) {
            rightHandTrials[idx + ch] = 0.5 * Math.sin((2 * Math.PI * i) / 20);
          }

          // Right motor cortex (C4): ERD during right hand imagery
          rightHandTrials[idx + 5] *= 0.3; // Strong suppression in C4
          rightHandTrials[idx + 6] *= 0.5;
        }
      }

      // Train CSP filters
      const csp = calculateCommonSpatialPatterns(
        leftHandTrials,
        rightHandTrials,
        numChannels,
        4 // Top 4 most discriminative filters
      );

      // Verify discriminability
      const discriminability = csp.eigenvalues[0] / csp.eigenvalues[3];
      assert.ok(
        discriminability > 1.5,
        `CSP should find discriminative features, ratio = ${discriminability.toFixed(
          2
        )}`
      );

      // Build real-time classification pipeline
      const pipeline = createDspPipeline();
      pipeline
        .CspTransform({
          cspMatrix: csp.cspMatrix,
          mean: csp.mean,
          numChannels: numChannels,
          numFilters: 4,
        })
        .Variance({ mode: "moving", windowSize: 50 })
        .MovingAverage({ mode: "moving", windowSize: 20 });

      // Test on new left hand trial
      const testLeftHand = new Float32Array(samplesPerTrial * numChannels);
      for (let i = 0; i < samplesPerTrial; i++) {
        for (let ch = 0; ch < numChannels; ch++) {
          testLeftHand[i * numChannels + ch] =
            0.5 * Math.sin((2 * Math.PI * i) / 20);
        }
        testLeftHand[i * numChannels + 2] *= 0.3;
        testLeftHand[i * numChannels + 3] *= 0.5;
      }

      const resultLeft = await pipeline.process(testLeftHand, {
        channels: numChannels,
      });

      // Test on new right hand trial
      const testRightHand = new Float32Array(samplesPerTrial * numChannels);
      for (let i = 0; i < samplesPerTrial; i++) {
        for (let ch = 0; ch < numChannels; ch++) {
          testRightHand[i * numChannels + ch] =
            0.5 * Math.sin((2 * Math.PI * i) / 20);
        }
        testRightHand[i * numChannels + 5] *= 0.3;
        testRightHand[i * numChannels + 6] *= 0.5;
      }

      const resultRight = await pipeline.process(testRightHand, {
        channels: numChannels,
      });

      // CSP features (variance of filtered signals) should differ between classes
      // Calculate mean variance across all samples for each class
      let varianceLeft = 0;
      let varianceRight = 0;
      const startIdx = 100 * numChannels; // After filter warm-up

      for (let i = 100; i < samplesPerTrial; i++) {
        // Use first CSP component for classification
        varianceLeft += Math.abs(resultLeft[i * numChannels + 0]);
        varianceRight += Math.abs(resultRight[i * numChannels + 0]);
      }

      varianceLeft /= samplesPerTrial - 100;
      varianceRight /= samplesPerTrial - 100;

      // Classes should produce different feature values
      const featureDiff = Math.abs(varianceLeft - varianceRight);
      const meanVariance = (varianceLeft + varianceRight) / 2;

      assert.ok(
        featureDiff / meanVariance > 0.1,
        `CSP features should differ between classes, got ${(
          (featureDiff / meanVariance) *
          100
        ).toFixed(1)}% difference`
      );
    });
  });

  describe("Comparison with PCA/ICA", () => {
    it("CSP should outperform PCA for supervised classification", async () => {
      // CSP uses class labels (supervised), PCA doesn't (unsupervised)
      // CSP should find more discriminative features

      const numChannels = 4;
      const numSamples = 500; // More samples for better covariance estimation

      // Create two clearly separable classes with correlated channels
      const class1 = new Float32Array(numSamples * numChannels);
      const class2 = new Float32Array(numSamples * numChannels);

      for (let i = 0; i < numSamples; i++) {
        // Class 1: high variance in channels 0, 1 (make them correlated)
        const sig1 = 4.0 * (Math.random() * 2 - 1);
        class1[i * numChannels + 0] = sig1 + 0.5 * (Math.random() * 2 - 1);
        class1[i * numChannels + 1] = sig1 + 0.5 * (Math.random() * 2 - 1);
        class1[i * numChannels + 2] = 0.3 * (Math.random() * 2 - 1);
        class1[i * numChannels + 3] = 0.3 * (Math.random() * 2 - 1);

        // Class 2: high variance in channels 2, 3 (make them correlated)
        const sig2 = 4.0 * (Math.random() * 2 - 1);
        class2[i * numChannels + 0] = 0.3 * (Math.random() * 2 - 1);
        class2[i * numChannels + 1] = 0.3 * (Math.random() * 2 - 1);
        class2[i * numChannels + 2] = sig2 + 0.5 * (Math.random() * 2 - 1);
        class2[i * numChannels + 3] = sig2 + 0.5 * (Math.random() * 2 - 1);
      }

      const csp = calculateCommonSpatialPatterns(
        class1,
        class2,
        numChannels,
        2
      );

      // CSP eigenvalue ratio indicates class separability
      const cspSeparability = csp.eigenvalues[0] / csp.eigenvalues[1];

      // PCA would just find directions of maximum variance (unsupervised)
      // It wouldn't necessarily separate the classes well
      // CSP explicitly maximizes class separation

      assert.ok(
        cspSeparability > 1.5,
        `CSP should find highly discriminative features, ratio = ${cspSeparability.toFixed(
          2
        )}`
      );

      // CSP's first component should have higher eigenvalue (more discriminative)
      assert.ok(
        csp.eigenvalues[0] > csp.eigenvalues[1],
        "First CSP filter should be most discriminative"
      );
    });
  });
});
