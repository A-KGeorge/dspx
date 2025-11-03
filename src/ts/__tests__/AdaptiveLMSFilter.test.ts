import { describe, it } from "node:test";
import assert from "node:assert/strict";
import { AdaptiveLMSFilter } from "../filters.js";

describe("AdaptiveLMSFilter", () => {
  describe("Constructor", () => {
    it("should create filter with default parameters", () => {
      const filter = new AdaptiveLMSFilter(32);
      assert.strictEqual(filter.getNumTaps(), 32);
      assert.ok(
        Math.abs(filter.getLearningRate() - 0.01) < 1e-6,
        "Learning rate should be approximately 0.01"
      );
    });

    it("should create filter with custom mu", () => {
      const filter = new AdaptiveLMSFilter(16, { mu: 0.05 });
      assert.strictEqual(filter.getNumTaps(), 16);
      assert.ok(
        Math.abs(filter.getLearningRate() - 0.05) < 1e-6,
        "Learning rate should be approximately 0.05"
      );
    });

    it("should create filter with normalized LMS", () => {
      const filter = new AdaptiveLMSFilter(32, { normalized: true });
      assert.strictEqual(filter.getNumTaps(), 32);
    });

    it("should create filter with regularization", () => {
      const filter = new AdaptiveLMSFilter(32, { lambda: 0.001 });
      assert.strictEqual(filter.getNumTaps(), 32);
    });

    it("should throw error for numTaps = 0", () => {
      assert.throws(() => {
        new AdaptiveLMSFilter(0);
      }, /numTaps must be greater than 0/);
    });

    it("should throw error for mu <= 0", () => {
      assert.throws(() => {
        new AdaptiveLMSFilter(32, { mu: 0 });
      }, /Learning rate mu must be in range/);
    });

    it("should throw error for mu > 1", () => {
      assert.throws(() => {
        new AdaptiveLMSFilter(32, { mu: 1.5 });
      }, /Learning rate mu must be in range/);
    });

    it("should throw error for lambda < 0", () => {
      assert.throws(() => {
        new AdaptiveLMSFilter(32, { lambda: -0.1 });
      }, /Regularization lambda must be in range/);
    });

    it("should throw error for lambda >= 1", () => {
      assert.throws(() => {
        new AdaptiveLMSFilter(32, { lambda: 1.0 });
      }, /Regularization lambda must be in range/);
    });
  });

  describe("Initialization", () => {
    it("should initialize for single channel", () => {
      const filter = new AdaptiveLMSFilter(8);
      filter.init(1);

      const weights = filter.getWeights(0);
      assert.strictEqual(weights.length, 8);
      // Weights should be initialized to zero
      for (let i = 0; i < weights.length; i++) {
        assert.strictEqual(weights[i], 0);
      }
    });

    it("should initialize for multiple channels", () => {
      const filter = new AdaptiveLMSFilter(8);
      filter.init(2);

      const weights0 = filter.getWeights(0);
      const weights1 = filter.getWeights(1);
      assert.strictEqual(weights0.length, 8);
      assert.strictEqual(weights1.length, 8);
    });

    it("should throw error for invalid channel index", () => {
      const filter = new AdaptiveLMSFilter(8);
      filter.init(1);

      assert.throws(() => {
        filter.getWeights(1); // Only channel 0 exists
      });
    });
  });

  describe("System Identification - Simple Delay", () => {
    it("should learn a simple 1-sample delay", () => {
      const numTaps = 8;
      const filter = new AdaptiveLMSFilter(numTaps, { mu: 0.1 });
      filter.init(1);

      // Generate test signal: random noise
      const numSamples = 200;
      const input = new Float32Array(numSamples);
      const desired = new Float32Array(numSamples);
      const output = new Float32Array(numSamples);
      const error = new Float32Array(numSamples);

      for (let i = 0; i < numSamples; i++) {
        input[i] = Math.random() * 2 - 1; // Random in [-1, 1]
      }

      // Desired output is input delayed by 1 sample
      desired[0] = 0;
      for (let i = 1; i < numSamples; i++) {
        desired[i] = input[i - 1];
      }

      // Train the filter
      filter.process(input, desired, output, error, true);

      // Check weights: should have a peak at tap 1
      const weights = filter.getWeights(0);
      console.log("Learned weights for 1-sample delay:", weights);

      // Weight at index 1 should be close to 1.0
      assert.ok(
        Math.abs(weights[1] - 1.0) < 0.3,
        `Expected weight[1] ≈ 1.0, got ${weights[1]}`
      );

      // Other weights should be close to 0
      assert.ok(
        Math.abs(weights[0]) < 0.3,
        `Expected weight[0] ≈ 0, got ${weights[0]}`
      );
      for (let i = 2; i < numTaps; i++) {
        assert.ok(
          Math.abs(weights[i]) < 0.3,
          `Expected weight[${i}] ≈ 0, got ${weights[i]}`
        );
      }
    });

    it("should learn a 3-sample delay", () => {
      const numTaps = 8;
      const filter = new AdaptiveLMSFilter(numTaps, { mu: 0.1 });
      filter.init(1);

      const numSamples = 300;
      const input = new Float32Array(numSamples);
      const desired = new Float32Array(numSamples);
      const output = new Float32Array(numSamples);
      const error = new Float32Array(numSamples);

      for (let i = 0; i < numSamples; i++) {
        input[i] = Math.random() * 2 - 1;
      }

      // Desired: input delayed by 3 samples
      for (let i = 0; i < 3; i++) {
        desired[i] = 0;
      }
      for (let i = 3; i < numSamples; i++) {
        desired[i] = input[i - 3];
      }

      filter.process(input, desired, output, error, true);

      const weights = filter.getWeights(0);
      console.log("Learned weights for 3-sample delay:", weights);

      // Weight at index 3 should be close to 1.0
      assert.ok(
        Math.abs(weights[3] - 1.0) < 0.3,
        `Expected weight[3] ≈ 1.0, got ${weights[3]}`
      );
    });
  });

  describe("System Identification - FIR Filter", () => {
    it("should identify a simple 3-tap FIR filter", () => {
      const numTaps = 8;
      const filter = new AdaptiveLMSFilter(numTaps, { mu: 0.05 });
      filter.init(1);

      // Unknown system: simple 3-tap FIR [0.5, 1.0, 0.5]
      const unknownSystem = new Float32Array([0.5, 1.0, 0.5]);

      const numSamples = 500;
      const input = new Float32Array(numSamples);
      const desired = new Float32Array(numSamples);
      const output = new Float32Array(numSamples);
      const error = new Float32Array(numSamples);

      // Generate white noise input
      for (let i = 0; i < numSamples; i++) {
        input[i] = Math.random() * 2 - 1;
      }

      // Convolve input with unknown system to get desired output
      for (let i = 0; i < numSamples; i++) {
        desired[i] = 0;
        for (let j = 0; j < unknownSystem.length; j++) {
          if (i - j >= 0) {
            desired[i] += unknownSystem[j] * input[i - j];
          }
        }
      }

      // Train adaptive filter
      filter.process(input, desired, output, error, true);

      const weights = filter.getWeights(0);
      console.log("Learned FIR coefficients:", weights.slice(0, 5));
      console.log("Target FIR coefficients:", unknownSystem);

      // Check first 3 taps match the unknown system
      for (let i = 0; i < unknownSystem.length; i++) {
        assert.ok(
          Math.abs(weights[i] - unknownSystem[i]) < 0.15,
          `Weight[${i}]: expected ${unknownSystem[i]}, got ${weights[i]}`
        );
      }
    });
  });

  describe("Noise Cancellation", () => {
    it("should cancel sinusoidal interference", () => {
      const numTaps = 16;
      const filter = new AdaptiveLMSFilter(numTaps, { mu: 0.01 });
      filter.init(1);

      const numSamples = 400;
      const input = new Float32Array(numSamples);
      const desired = new Float32Array(numSamples);
      const output = new Float32Array(numSamples);
      const error = new Float32Array(numSamples);

      const frequency = 0.1; // Normalized frequency

      // Input: reference noise (correlated with interference)
      for (let i = 0; i < numSamples; i++) {
        input[i] = Math.sin(2 * Math.PI * frequency * i);
      }

      // Desired: clean signal + interference
      // Clean signal is DC offset = 1.0
      // Interference is same sinusoid (correlated with input)
      for (let i = 0; i < numSamples; i++) {
        const cleanSignal = 1.0;
        const interference = Math.sin(2 * Math.PI * frequency * i);
        desired[i] = cleanSignal + interference;
      }

      // Train filter to cancel interference
      filter.process(input, desired, output, error, true);

      // After training, error should be close to clean signal
      // Check last 50 samples (after convergence)
      const startIdx = numSamples - 50;
      let avgError = 0;
      for (let i = startIdx; i < numSamples; i++) {
        avgError += error[i];
      }
      avgError /= 50;

      console.log(
        `Average error (should be ~1.0 for DC clean signal): ${avgError}`
      );

      // Error should be close to 1.0 (the DC offset)
      assert.ok(
        Math.abs(avgError - 1.0) < 0.3,
        `Expected avg error ≈ 1.0, got ${avgError}`
      );
    });
  });

  describe("Normalized LMS (NLMS)", () => {
    it("should converge for varying input power", () => {
      const numTaps = 16;
      const lms = new AdaptiveLMSFilter(numTaps, {
        mu: 0.01,
        normalized: false,
      });
      const nlms = new AdaptiveLMSFilter(numTaps, {
        mu: 0.5,
        normalized: true,
      }); // Higher mu for NLMS

      lms.init(1);
      nlms.init(1);

      // Unknown system: 2-tap delay + gain
      const unknownSystem = new Float32Array([0, 0.8, 0]);

      const numSamples = 500;
      const input = new Float32Array(numSamples);
      const desiredLMS = new Float32Array(numSamples);
      const desiredNLMS = new Float32Array(numSamples);
      const outputLMS = new Float32Array(numSamples);
      const outputNLMS = new Float32Array(numSamples);
      const errorLMS = new Float32Array(numSamples);
      const errorNLMS = new Float32Array(numSamples);

      // Input with varying power (first half low, second half high)
      for (let i = 0; i < numSamples / 2; i++) {
        input[i] = (Math.random() * 2 - 1) * 0.2; // Low power
      }
      for (let i = numSamples / 2; i < numSamples; i++) {
        input[i] = (Math.random() * 2 - 1) * 2.0; // High power
      }

      // Convolve with unknown system
      for (let i = 0; i < numSamples; i++) {
        desiredLMS[i] = 0;
        desiredNLMS[i] = 0;
        for (let j = 0; j < unknownSystem.length; j++) {
          if (i - j >= 0) {
            desiredLMS[i] += unknownSystem[j] * input[i - j];
            desiredNLMS[i] += unknownSystem[j] * input[i - j];
          }
        }
      }

      // Train both filters
      lms.process(input, desiredLMS, outputLMS, errorLMS, true);
      nlms.process(input, desiredNLMS, outputNLMS, errorNLMS, true);

      // Compare final weights
      const weightsLMS = lms.getWeights(0);
      const weightsNLMS = nlms.getWeights(0);

      console.log("LMS weights:", weightsLMS.slice(0, 5));
      console.log("NLMS weights:", weightsNLMS.slice(0, 5));

      // Both should converge to target (0, 0.8, 0)
      const errorLMSweight = Math.abs(weightsLMS[1] - 0.8);
      const errorNLMSweight = Math.abs(weightsNLMS[1] - 0.8);

      console.log(
        `LMS error: ${errorLMSweight}, NLMS error: ${errorNLMSweight}`
      );

      // Both should converge (relaxed tolerance due to varying power)
      assert.ok(
        errorLMSweight < 0.4,
        `LMS should converge, error: ${errorLMSweight}`
      );
      assert.ok(
        errorNLMSweight < 0.4,
        `NLMS should converge, error: ${errorNLMSweight}`
      );
    });
  });

  describe("Filter Mode (Inference)", () => {
    it("should filter without adapting weights", () => {
      const numTaps = 8;
      const filter = new AdaptiveLMSFilter(numTaps, { mu: 0.1 });
      filter.init(1);

      // Train on simple delay
      const trainSamples = 200;
      const trainInput = new Float32Array(trainSamples);
      const trainDesired = new Float32Array(trainSamples);
      const trainOutput = new Float32Array(trainSamples);
      const trainError = new Float32Array(trainSamples);

      for (let i = 0; i < trainSamples; i++) {
        trainInput[i] = Math.random() * 2 - 1;
      }
      trainDesired[0] = 0;
      for (let i = 1; i < trainSamples; i++) {
        trainDesired[i] = trainInput[i - 1];
      }

      filter.process(trainInput, trainDesired, trainOutput, trainError, true);

      // Get weights after training
      const trainedWeights = new Float32Array(filter.getWeights(0));

      // Now use filter mode (inference)
      const testSamples = 50;
      const testInput = new Float32Array(testSamples);
      const testOutput = new Float32Array(testSamples);

      for (let i = 0; i < testSamples; i++) {
        testInput[i] = Math.random() * 2 - 1;
      }

      filter.filter(testInput, testOutput);

      // Weights should not have changed
      const finalWeights = filter.getWeights(0);
      for (let i = 0; i < numTaps; i++) {
        assert.strictEqual(
          finalWeights[i],
          trainedWeights[i],
          `Weight[${i}] changed during filter mode`
        );
      }
    });

    it("should process with adapt=false (same as filter)", () => {
      const numTaps = 4;
      const filter1 = new AdaptiveLMSFilter(numTaps, { mu: 0.1 });
      const filter2 = new AdaptiveLMSFilter(numTaps, { mu: 0.1 });

      filter1.init(1);
      filter2.init(1);

      // Set same initial weights
      const weights = new Float32Array([0.25, 0.5, 0.25, 0]);
      filter1.setWeights(0, weights);
      filter2.setWeights(0, weights);

      const input = new Float32Array([1, 2, 3, 4, 5]);
      const desired = new Float32Array(5);
      const output1 = new Float32Array(5);
      const output2 = new Float32Array(5);
      const error = new Float32Array(5);

      // filter1: use filter() method
      filter1.filter(input, output1);

      // filter2: use process() with adapt=false
      filter2.process(input, desired, output2, error, false);

      // Outputs should be identical
      for (let i = 0; i < 5; i++) {
        assert.strictEqual(
          output1[i],
          output2[i],
          `Output mismatch at index ${i}`
        );
      }
    });
  });

  describe("Weight Management", () => {
    it("should get and set weights correctly", () => {
      const numTaps = 4;
      const filter = new AdaptiveLMSFilter(numTaps);
      filter.init(1);

      const newWeights = new Float32Array([0.1, 0.2, 0.3, 0.4]);
      filter.setWeights(0, newWeights);

      const retrievedWeights = filter.getWeights(0);
      for (let i = 0; i < numTaps; i++) {
        assert.strictEqual(retrievedWeights[i], newWeights[i]);
      }
    });

    it("should throw error for wrong weight vector size", () => {
      const filter = new AdaptiveLMSFilter(4);
      filter.init(1);

      const wrongSizeWeights = new Float32Array([0.1, 0.2, 0.3]); // Too short

      assert.throws(() => {
        filter.setWeights(0, wrongSizeWeights);
      });
    });

    it("should support transfer learning (pre-trained weights)", () => {
      const numTaps = 8;

      // Pre-trained filter (simulated)
      const pretrainedWeights = new Float32Array(numTaps);
      pretrainedWeights[2] = 0.7; // Pre-learned delay

      // New filter initialized with pre-trained weights
      const filter = new AdaptiveLMSFilter(numTaps, { mu: 0.05 });
      filter.init(1);
      filter.setWeights(0, pretrainedWeights);

      // Fine-tune on new data
      const numSamples = 100;
      const input = new Float32Array(numSamples);
      const desired = new Float32Array(numSamples);
      const output = new Float32Array(numSamples);
      const error = new Float32Array(numSamples);

      for (let i = 0; i < numSamples; i++) {
        input[i] = Math.random() * 2 - 1;
      }
      for (let i = 0; i < 2; i++) {
        desired[i] = 0;
      }
      for (let i = 2; i < numSamples; i++) {
        desired[i] = input[i - 2];
      }

      filter.process(input, desired, output, error, true);

      const finalWeights = filter.getWeights(0);

      // Weight at index 2 should still be dominant (started at 0.7)
      assert.ok(
        finalWeights[2] > 0.5,
        `Expected dominant weight at index 2, got ${finalWeights[2]}`
      );
    });
  });

  describe("Learning Rate Adjustment", () => {
    it("should update learning rate dynamically", () => {
      const filter = new AdaptiveLMSFilter(8, { mu: 0.01 });
      assert.ok(Math.abs(filter.getLearningRate() - 0.01) < 1e-6);

      filter.setLearningRate(0.05);
      assert.ok(Math.abs(filter.getLearningRate() - 0.05) < 1e-6);
    });

    it("should throw error for invalid learning rate", () => {
      const filter = new AdaptiveLMSFilter(8);

      assert.throws(() => {
        filter.setLearningRate(0);
      }, /Learning rate mu must be/);

      assert.throws(() => {
        filter.setLearningRate(1.5);
      }, /Learning rate mu must be/);
    });

    it("should implement annealing schedule", () => {
      const numTaps = 8;
      const filter = new AdaptiveLMSFilter(numTaps, { mu: 0.1 });
      filter.init(1);

      const numEpochs = 5;
      const samplesPerEpoch = 100;

      // Generate training data
      const input = new Float32Array(samplesPerEpoch);
      const desired = new Float32Array(samplesPerEpoch);
      const output = new Float32Array(samplesPerEpoch);
      const error = new Float32Array(samplesPerEpoch);

      for (let i = 0; i < samplesPerEpoch; i++) {
        input[i] = Math.random() * 2 - 1;
      }
      desired[0] = 0;
      for (let i = 1; i < samplesPerEpoch; i++) {
        desired[i] = input[i - 1];
      }

      // Train with decreasing learning rate
      for (let epoch = 0; epoch < numEpochs; epoch++) {
        const mu = 0.1 * Math.pow(0.5, epoch); // Halve each epoch
        filter.setLearningRate(mu);

        filter.process(input, desired, output, error, true);

        console.log(
          `Epoch ${epoch + 1}: mu=${mu.toFixed(4)}, final_error=${error[
            samplesPerEpoch - 1
          ].toFixed(4)}`
        );
      }

      const weights = filter.getWeights(0);
      // Should have converged to delay filter
      assert.ok(
        Math.abs(weights[1] - 1.0) < 0.2,
        `Expected weight[1] ≈ 1.0, got ${weights[1]}`
      );
    });
  });

  describe("Multi-Channel Processing", () => {
    it("should process 2 channels independently (planar layout)", () => {
      const numTaps = 8;
      const filter = new AdaptiveLMSFilter(numTaps, { mu: 0.1 });
      filter.init(2);

      const samplesPerChannel = 100;
      const totalSamples = samplesPerChannel * 2; // Planar format

      // Planar layout: [all Ch0 samples, then all Ch1 samples]
      const input = new Float32Array(totalSamples);
      const desired = new Float32Array(totalSamples);
      const output = new Float32Array(totalSamples);
      const error = new Float32Array(totalSamples);

      // Channel 0: first samplesPerChannel elements (delay by 1 sample)
      // Channel 1: next samplesPerChannel elements (delay by 2 samples)
      for (let i = 0; i < samplesPerChannel; i++) {
        input[i] = Math.random() * 2 - 1; // Ch0
        input[samplesPerChannel + i] = Math.random() * 2 - 1; // Ch1
      }

      // Desired outputs (planar layout)
      // Channel 0: 1-sample delay
      desired[0] = 0;
      for (let i = 1; i < samplesPerChannel; i++) {
        desired[i] = input[i - 1];
      }

      // Channel 1: 2-sample delay
      desired[samplesPerChannel] = 0;
      desired[samplesPerChannel + 1] = 0;
      for (let i = 2; i < samplesPerChannel; i++) {
        desired[samplesPerChannel + i] = input[samplesPerChannel + i - 2];
      }

      filter.process(input, desired, output, error, true);

      const weights0 = filter.getWeights(0);
      const weights1 = filter.getWeights(1);

      console.log("Channel 0 weights (1-sample delay):", weights0.slice(0, 5));
      console.log("Channel 1 weights (2-sample delay):", weights1.slice(0, 5));

      // Channel 0 should have peak at index 1
      assert.ok(
        Math.abs(weights0[1] - 1.0) < 0.3,
        `Ch0: Expected weight[1] ≈ 1.0, got ${weights0[1]}`
      );

      // Channel 1 should have peak at index 2
      assert.ok(
        Math.abs(weights1[2] - 1.0) < 0.3,
        `Ch1: Expected weight[2] ≈ 1.0, got ${weights1[2]}`
      );
    });
  });

  describe("Reset Functionality", () => {
    it("should reset all state", () => {
      const filter = new AdaptiveLMSFilter(4, { mu: 0.1 });
      filter.init(1);

      // Train the filter
      const numSamples = 50;
      const input = new Float32Array(numSamples);
      const desired = new Float32Array(numSamples);
      const output = new Float32Array(numSamples);
      const error = new Float32Array(numSamples);

      for (let i = 0; i < numSamples; i++) {
        input[i] = Math.random();
        desired[i] = Math.random();
      }

      filter.process(input, desired, output, error, true);

      // Weights should be non-zero after training
      const weightsBeforeReset = filter.getWeights(0);
      let hasNonZero = false;
      for (let i = 0; i < 4; i++) {
        if (Math.abs(weightsBeforeReset[i]) > 0.01) {
          hasNonZero = true;
          break;
        }
      }
      assert.ok(hasNonZero, "Weights should be non-zero after training");

      // Reset the filter
      filter.reset();

      // Weights should be zero after reset
      const weightsAfterReset = filter.getWeights(0);
      for (let i = 0; i < 4; i++) {
        assert.strictEqual(
          weightsAfterReset[i],
          0,
          `Weight[${i}] should be 0 after reset`
        );
      }
    });
  });

  describe("Edge Cases", () => {
    it("should handle all-zero input", () => {
      const filter = new AdaptiveLMSFilter(4, { mu: 0.1 });
      filter.init(1);

      const input = new Float32Array(10); // All zeros
      const desired = new Float32Array(10);
      const output = new Float32Array(10);
      const error = new Float32Array(10);

      for (let i = 0; i < 10; i++) {
        desired[i] = 1.0;
      }

      filter.process(input, desired, output, error, true);

      // Weights should remain near zero (no gradient to update)
      const weights = filter.getWeights(0);
      for (let i = 0; i < 4; i++) {
        assert.ok(
          Math.abs(weights[i]) < 0.1,
          `Weight[${i}] should stay near 0 with zero input`
        );
      }
    });

    it("should handle single sample", () => {
      const filter = new AdaptiveLMSFilter(4, { mu: 0.1 });
      filter.init(1);

      const input = new Float32Array([0.5]);
      const desired = new Float32Array([0.8]);
      const output = new Float32Array(1);
      const error = new Float32Array(1);

      filter.process(input, desired, output, error, true);

      assert.ok(output[0] !== undefined);
      assert.ok(error[0] !== undefined);
    });

    it("should handle large number of samples", () => {
      const filter = new AdaptiveLMSFilter(32, { mu: 0.01 });
      filter.init(1);

      const numSamples = 10000;
      const input = new Float32Array(numSamples);
      const desired = new Float32Array(numSamples);
      const output = new Float32Array(numSamples);
      const error = new Float32Array(numSamples);

      for (let i = 0; i < numSamples; i++) {
        input[i] = Math.random() * 2 - 1;
        desired[i] = Math.random() * 2 - 1;
      }

      // Should not crash or hang
      filter.process(input, desired, output, error, true);

      const weights = filter.getWeights(0);
      assert.strictEqual(weights.length, 32);
    });
  });

  describe("Regularization (Leaky LMS)", () => {
    it("should apply regularization without causing instability", () => {
      const numTaps = 8;
      const filterNoReg = new AdaptiveLMSFilter(numTaps, {
        mu: 0.1,
        lambda: 0.0,
      });
      const filterWithReg = new AdaptiveLMSFilter(numTaps, {
        mu: 0.1,
        lambda: 0.001,
      });

      filterNoReg.init(1);
      filterWithReg.init(1);

      // Simple training scenario
      const numSamples = 300;
      const input = new Float32Array(numSamples);
      const desired = new Float32Array(numSamples);
      const outputNoReg = new Float32Array(numSamples);
      const outputWithReg = new Float32Array(numSamples);
      const errorNoReg = new Float32Array(numSamples);
      const errorWithReg = new Float32Array(numSamples);

      for (let i = 0; i < numSamples; i++) {
        input[i] = Math.random() * 2 - 1;
      }

      // Simple delay system
      desired[0] = 0;
      for (let i = 1; i < numSamples; i++) {
        desired[i] = input[i - 1];
      }

      filterNoReg.process(input, desired, outputNoReg, errorNoReg, true);
      filterWithReg.process(input, desired, outputWithReg, errorWithReg, true);

      const weightsNoReg = filterNoReg.getWeights(0);
      const weightsWithReg = filterWithReg.getWeights(0);

      // Compute L2 norm of weights
      let normNoReg = 0;
      let normWithReg = 0;
      for (let i = 0; i < numTaps; i++) {
        normNoReg += weightsNoReg[i] * weightsNoReg[i];
        normWithReg += weightsWithReg[i] * weightsWithReg[i];
      }
      normNoReg = Math.sqrt(normNoReg);
      normWithReg = Math.sqrt(normWithReg);

      console.log(
        `Weight norm without regularization: ${normNoReg.toFixed(4)}`
      );
      console.log(`Weight norm with regularization: ${normWithReg.toFixed(4)}`);

      // Both should converge (weights should be finite and reasonable)
      assert.ok(
        isFinite(normNoReg) && normNoReg < 10,
        "Weights without regularization should be finite"
      );
      assert.ok(
        isFinite(normWithReg) && normWithReg < 10,
        "Weights with regularization should be finite"
      );

      // Regularization typically produces slightly smaller norms
      // (but not guaranteed due to stochastic nature, so we just check both converged)
    });
  });
});
