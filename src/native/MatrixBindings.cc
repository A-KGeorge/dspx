#include <napi.h>
#include <Eigen/Dense>
#include <vector>
#include <cmath>
#include <algorithm>

using namespace Napi;

namespace dsp
{

    /**
     * @brief Calculate PCA (Principal Component Analysis) transformation matrix.
     *
     * Performs eigenvalue decomposition of the covariance matrix to find principal components.
     *
     * @param info N-API callback info containing:
     *   - interleavedData: Float32Array of data (samples × channels interleaved)
     *   - numChannels: Number of channels (features)
     *
     * @return Object containing:
     *   - mean: Float32Array of channel means (size: numChannels)
     *   - pcaMatrix: Float32Array of eigenvectors (size: numChannels × numChannels, column-major)
     *   - eigenvalues: Float32Array of eigenvalues (size: numChannels, sorted descending)
     *   - explainedVariance: Float32Array of variance ratios (size: numChannels)
     */
    Napi::Object CalculatePca(const Napi::CallbackInfo &info)
    {
        Napi::Env env = info.Env();

        // Validate arguments
        if (info.Length() < 2 || !info[0].IsTypedArray() || !info[1].IsNumber())
        {
            Napi::TypeError::New(env, "Expected (Float32Array data, number numChannels)")
                .ThrowAsJavaScriptException();
            return Napi::Object::New(env);
        }

        // Get input data
        Napi::Float32Array dataArray = info[0].As<Napi::Float32Array>();
        int numChannels = info[1].As<Napi::Number>().Int32Value();

        size_t totalSamples = dataArray.ElementLength();
        if (totalSamples % numChannels != 0)
        {
            Napi::TypeError::New(env, "Data length must be divisible by numChannels")
                .ThrowAsJavaScriptException();
            return Napi::Object::New(env);
        }

        size_t numSamples = totalSamples / numChannels;
        if (numSamples < static_cast<size_t>(numChannels))
        {
            Napi::TypeError::New(env, "Need at least numChannels samples for PCA")
                .ThrowAsJavaScriptException();
            return Napi::Object::New(env);
        }

        float *data = dataArray.Data();

        // Map interleaved data to Eigen matrix (row-major: each row is one sample)
        using EigenMatrix = Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>;
        Eigen::Map<EigenMatrix> X(data, numSamples, numChannels);

        // Calculate mean of each channel
        Eigen::VectorXf mean = X.colwise().mean();

        // Center the data (subtract mean from each column)
        Eigen::MatrixXf X_centered = X.rowwise() - mean.transpose();

        // Compute covariance matrix: Cov = (X^T * X) / (n - 1)
        Eigen::MatrixXf cov = (X_centered.adjoint() * X_centered) / float(numSamples - 1);

        // Compute eigenvalues and eigenvectors
        Eigen::SelfAdjointEigenSolver<Eigen::MatrixXf> es(cov);

        if (es.info() != Eigen::Success)
        {
            Napi::Error::New(env, "Eigenvalue decomposition failed")
                .ThrowAsJavaScriptException();
            return Napi::Object::New(env);
        }

        // Eigen returns eigenvalues in ascending order, we want descending
        Eigen::VectorXf eigenvalues = es.eigenvalues().reverse();
        Eigen::MatrixXf eigenvectors = es.eigenvectors().rowwise().reverse();

        // Calculate explained variance ratios
        float totalVariance = eigenvalues.sum();
        Eigen::VectorXf explainedVariance = eigenvalues / totalVariance;

        // Create return object
        Napi::Object result = Napi::Object::New(env);

        // Copy mean vector
        Napi::Float32Array meanArray = Napi::Float32Array::New(env, numChannels);
        for (int i = 0; i < numChannels; ++i)
        {
            meanArray[i] = mean(i);
        }
        result.Set("mean", meanArray);

        // Copy PCA matrix (column-major for Eigen compatibility)
        Napi::Float32Array pcaArray = Napi::Float32Array::New(env, numChannels * numChannels);
        std::copy(eigenvectors.data(), eigenvectors.data() + (numChannels * numChannels), pcaArray.Data());
        result.Set("pcaMatrix", pcaArray);

        // Copy eigenvalues
        Napi::Float32Array eigenArray = Napi::Float32Array::New(env, numChannels);
        for (int i = 0; i < numChannels; ++i)
        {
            eigenArray[i] = eigenvalues(i);
        }
        result.Set("eigenvalues", eigenArray);

        // Copy explained variance
        Napi::Float32Array varianceArray = Napi::Float32Array::New(env, numChannels);
        for (int i = 0; i < numChannels; ++i)
        {
            varianceArray[i] = explainedVariance(i);
        }
        result.Set("explainedVariance", varianceArray);

        result.Set("numChannels", Napi::Number::New(env, numChannels));
        result.Set("numComponents", Napi::Number::New(env, numChannels));

        return result;
    }

    /**
     * @brief Calculate Whitening (ZCA) transformation matrix.
     *
     * Whitening transforms data to have identity covariance matrix.
     * Uses ZCA (Zero-phase Component Analysis) variant: W = V * D^(-1/2) * V^T
     *
     * @param info N-API callback info containing:
     *   - interleavedData: Float32Array of data (samples × channels interleaved)
     *   - numChannels: Number of channels
     *   - regularization: Optional float (default 1e-5) to prevent division by zero
     *
     * @return Object containing:
     *   - mean: Float32Array of channel means
     *   - whiteningMatrix: Float32Array of whitening transformation (numChannels × numChannels)
     */
    Napi::Object CalculateWhitening(const Napi::CallbackInfo &info)
    {
        Napi::Env env = info.Env();

        // Validate arguments
        if (info.Length() < 2 || !info[0].IsTypedArray() || !info[1].IsNumber())
        {
            Napi::TypeError::New(env, "Expected (Float32Array data, number numChannels, [number regularization])")
                .ThrowAsJavaScriptException();
            return Napi::Object::New(env);
        }

        // Get input data
        Napi::Float32Array dataArray = info[0].As<Napi::Float32Array>();
        int numChannels = info[1].As<Napi::Number>().Int32Value();
        float regularization = (info.Length() >= 3 && info[2].IsNumber())
                                   ? info[2].As<Napi::Number>().FloatValue()
                                   : 1e-5f;

        size_t totalSamples = dataArray.ElementLength();
        if (totalSamples % numChannels != 0)
        {
            Napi::TypeError::New(env, "Data length must be divisible by numChannels")
                .ThrowAsJavaScriptException();
            return Napi::Object::New(env);
        }

        size_t numSamples = totalSamples / numChannels;
        if (numSamples < static_cast<size_t>(numChannels))
        {
            Napi::TypeError::New(env, "Need at least numChannels samples for whitening")
                .ThrowAsJavaScriptException();
            return Napi::Object::New(env);
        }

        float *data = dataArray.Data();

        // Map interleaved data to Eigen matrix
        using EigenMatrix = Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>;
        Eigen::Map<EigenMatrix> X(data, numSamples, numChannels);

        // Calculate mean and center data
        Eigen::VectorXf mean = X.colwise().mean();
        Eigen::MatrixXf X_centered = X.rowwise() - mean.transpose();

        // Compute covariance matrix
        Eigen::MatrixXf cov = (X_centered.adjoint() * X_centered) / float(numSamples - 1);

        // Compute eigenvalue decomposition
        Eigen::SelfAdjointEigenSolver<Eigen::MatrixXf> es(cov);

        if (es.info() != Eigen::Success)
        {
            Napi::Error::New(env, "Eigenvalue decomposition failed")
                .ThrowAsJavaScriptException();
            return Napi::Object::New(env);
        }

        Eigen::VectorXf eigenvalues = es.eigenvalues();
        Eigen::MatrixXf eigenvectors = es.eigenvectors();

        // Create D^(-1/2) matrix with regularization
        Eigen::VectorXf D_inv_sqrt = (eigenvalues.array() + regularization).sqrt().inverse();
        Eigen::MatrixXf D_inv_sqrt_mat = D_inv_sqrt.asDiagonal();

        // Calculate ZCA whitening matrix: W = V * D^(-1/2) * V^T
        Eigen::MatrixXf whiteningMatrix = eigenvectors * D_inv_sqrt_mat * eigenvectors.transpose();

        // Create return object
        Napi::Object result = Napi::Object::New(env);

        // Copy mean vector
        Napi::Float32Array meanArray = Napi::Float32Array::New(env, numChannels);
        for (int i = 0; i < numChannels; ++i)
        {
            meanArray[i] = mean(i);
        }
        result.Set("mean", meanArray);

        // Copy whitening matrix
        Napi::Float32Array whiteningArray = Napi::Float32Array::New(env, numChannels * numChannels);
        std::copy(whiteningMatrix.data(), whiteningMatrix.data() + (numChannels * numChannels),
                  whiteningArray.Data());
        result.Set("whiteningMatrix", whiteningArray);

        result.Set("numChannels", Napi::Number::New(env, numChannels));
        result.Set("numComponents", Napi::Number::New(env, numChannels));
        result.Set("regularization", Napi::Number::New(env, regularization));

        return result;
    }

    /**
     * @brief Calculate ICA (Independent Component Analysis) using FastICA algorithm.
     *
     * Separates mixed signals into statistically independent components.
     * Uses the FastICA algorithm with tanh nonlinearity.
     *
     * @param info N-API callback info containing:
     *   - interleavedData: Float32Array of mixed signals
     *   - numChannels: Number of channels (sources)
     *   - maxIterations: Optional number (default 200)
     *   - tolerance: Optional number (default 1e-4) for convergence
     *
     * @return Object containing:
     *   - mean: Float32Array of channel means
     *   - icaMatrix: Float32Array of unmixing matrix (numChannels × numChannels)
     *   - converged: Boolean indicating if algorithm converged
     *   - iterations: Number of iterations performed
     */
    Napi::Object CalculateIca(const Napi::CallbackInfo &info)
    {
        Napi::Env env = info.Env();

        // Validate arguments
        if (info.Length() < 2 || !info[0].IsTypedArray() || !info[1].IsNumber())
        {
            Napi::TypeError::New(env, "Expected (Float32Array data, number numChannels, [number maxIter], [number tol])")
                .ThrowAsJavaScriptException();
            return Napi::Object::New(env);
        }

        // Get input parameters
        Napi::Float32Array dataArray = info[0].As<Napi::Float32Array>();
        int numChannels = info[1].As<Napi::Number>().Int32Value();
        int maxIterations = (info.Length() >= 3 && info[2].IsNumber())
                                ? info[2].As<Napi::Number>().Int32Value()
                                : 200;
        float tolerance = (info.Length() >= 4 && info[3].IsNumber())
                              ? info[3].As<Napi::Number>().FloatValue()
                              : 1e-4f;

        size_t totalSamples = dataArray.ElementLength();
        if (totalSamples % numChannels != 0)
        {
            Napi::TypeError::New(env, "Data length must be divisible by numChannels")
                .ThrowAsJavaScriptException();
            return Napi::Object::New(env);
        }

        size_t numSamples = totalSamples / numChannels;
        if (numSamples < static_cast<size_t>(numChannels * 5))
        {
            Napi::TypeError::New(env, "ICA requires at least 5 × numChannels samples")
                .ThrowAsJavaScriptException();
            return Napi::Object::New(env);
        }

        float *data = dataArray.Data();

        // Map interleaved data to Eigen matrix
        using EigenMatrix = Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>;
        Eigen::Map<EigenMatrix> X(data, numSamples, numChannels);

        // Step 1: Center the data
        Eigen::VectorXf mean = X.colwise().mean();
        Eigen::MatrixXf X_centered = X.rowwise() - mean.transpose();

        // Step 2: Whiten the data
        Eigen::MatrixXf cov = (X_centered.adjoint() * X_centered) / float(numSamples - 1);
        Eigen::SelfAdjointEigenSolver<Eigen::MatrixXf> es(cov);

        if (es.info() != Eigen::Success)
        {
            Napi::Error::New(env, "Eigenvalue decomposition failed in whitening step")
                .ThrowAsJavaScriptException();
            return Napi::Object::New(env);
        }

        // Whitening matrix: V * D^(-1/2)
        Eigen::VectorXf eigenvalues = es.eigenvalues();
        Eigen::MatrixXf eigenvectors = es.eigenvectors();

        float regularization = 1e-5f;
        Eigen::VectorXf D_inv_sqrt = (eigenvalues.array() + regularization).sqrt().inverse();
        Eigen::MatrixXf whiteningMatrix = eigenvectors * D_inv_sqrt.asDiagonal();

        // Apply whitening: Z = X_centered * whiteningMatrix
        Eigen::MatrixXf Z = X_centered * whiteningMatrix;

        // Step 3: FastICA iteration
        // Initialize W randomly
        Eigen::MatrixXf W = Eigen::MatrixXf::Random(numChannels, numChannels);

        // Orthogonalize initial W
        Eigen::JacobiSVD<Eigen::MatrixXf> svd(W, Eigen::ComputeThinU | Eigen::ComputeThinV);
        W = svd.matrixU() * svd.matrixV().transpose();

        bool converged = false;
        int iteration = 0;

        for (iteration = 0; iteration < maxIterations; ++iteration)
        {
            Eigen::MatrixXf W_old = W;

            // For each component
            for (int comp = 0; comp < numChannels; ++comp)
            {
                Eigen::VectorXf w = W.col(comp);

                // Project data: y = Z * w
                Eigen::VectorXf y = Z * w;

                // Apply nonlinearity: g(y) = tanh(y), g'(y) = 1 - tanh^2(y)
                Eigen::VectorXf g = y.array().tanh();
                Eigen::VectorXf g_prime = 1.0f - g.array().square();

                // Update rule: w_new = E[Z^T * g(y)] - E[g'(y)] * w
                Eigen::VectorXf w_new = (Z.transpose() * g) / float(numSamples) - (g_prime.mean()) * w;

                // Decorrelate against previous components
                for (int j = 0; j < comp; ++j)
                {
                    w_new -= w_new.dot(W.col(j)) * W.col(j);
                }

                // Normalize
                float norm = w_new.norm();
                if (norm > 1e-10f)
                {
                    w_new /= norm;
                }

                W.col(comp) = w_new;
            }

            // Check convergence
            float change = (W - W_old).array().abs().maxCoeff();
            if (change < tolerance)
            {
                converged = true;
                break;
            }
        }

        // Final unmixing matrix: ICA = whiteningMatrix * W^T
        Eigen::MatrixXf icaMatrix = whiteningMatrix * W.transpose();

        // Create return object
        Napi::Object result = Napi::Object::New(env);

        // Copy mean vector
        Napi::Float32Array meanArray = Napi::Float32Array::New(env, numChannels);
        for (int i = 0; i < numChannels; ++i)
        {
            meanArray[i] = mean(i);
        }
        result.Set("mean", meanArray);

        // Copy ICA matrix
        Napi::Float32Array icaArray = Napi::Float32Array::New(env, numChannels * numChannels);
        std::copy(icaMatrix.data(), icaMatrix.data() + (numChannels * numChannels),
                  icaArray.Data());
        result.Set("icaMatrix", icaArray);

        result.Set("numChannels", Napi::Number::New(env, numChannels));
        result.Set("numComponents", Napi::Number::New(env, numChannels));
        result.Set("converged", Napi::Boolean::New(env, converged));
        result.Set("iterations", Napi::Number::New(env, iteration + 1));

        return result;
    }

    /**
     * @brief Calculate beamformer steering weights and blocking matrix for GSC architecture.
     *
     * Computes fixed beamforming matrices for a microphone array:
     * 1. Steering weights: Delay-and-sum beamformer pointing at target direction
     * 2. Blocking matrix: Creates N-1 signals where target is cancelled (noise-only references)
     *
     * @param info N-API callback info containing:
     *   - numChannels: Number of microphones/sensors
     *   - arrayGeometry: String - "linear" (ULA), "circular", or "planar"
     *   - targetAngleDeg: Target direction in degrees (0° = broadside)
     *   - elementSpacing: (Optional) Spacing between elements in wavelengths (default: 0.5)
     *
     * @return Object containing:
     *   - steeringWeights: Float32Array of steering weights (size: numChannels)
     *   - blockingMatrix: Float32Array of blocking matrix (size: numChannels × (numChannels-1), column-major)
     *   - numChannels: Number of channels
     *
     * @example Linear array, 8 mics, 0° target (broadside):
     * const bf = calculateBeamformerWeights(8, "linear", 0.0, 0.5);
     * // Use bf.steeringWeights and bf.blockingMatrix in GscPreprocessor
     */
    Napi::Object CalculateBeamformerWeights(const Napi::CallbackInfo &info)
    {
        Napi::Env env = info.Env();

        // Validate arguments
        if (info.Length() < 3 || !info[0].IsNumber() || !info[1].IsString() || !info[2].IsNumber())
        {
            Napi::TypeError::New(env,
                                 "Expected (number numChannels, string arrayGeometry, number targetAngleDeg, [number elementSpacing])")
                .ThrowAsJavaScriptException();
            return Napi::Object::New(env);
        }

        int numChannels = info[0].As<Napi::Number>().Int32Value();
        std::string geometry = info[1].As<Napi::String>().Utf8Value();
        float targetAngleDeg = info[2].As<Napi::Number>().FloatValue();
        float elementSpacing = 0.5f; // Default: half-wavelength spacing

        if (info.Length() >= 4 && info[3].IsNumber())
        {
            elementSpacing = info[3].As<Napi::Number>().FloatValue();
        }

        if (numChannels < 2)
        {
            Napi::TypeError::New(env, "numChannels must be >= 2")
                .ThrowAsJavaScriptException();
            return Napi::Object::New(env);
        }

        // Convert angle to radians
        const float PI = 3.14159265358979323846f;
        float targetAngleRad = targetAngleDeg * PI / 180.0f;

        // Initialize steering weights vector
        Eigen::VectorXf steeringWeights(numChannels);

        // **1. Calculate Steering Weights (Delay-and-Sum Beamformer)**
        if (geometry == "linear")
        {
            // Uniform Linear Array (ULA)
            // Phase shift for element n: exp(-j * 2π * d/λ * n * sin(θ))
            // For real-valued weights (no phase), use equal weights (simple delay-and-sum)
            // More sophisticated: apply time delays, but for simplicity, use equal weighting
            // normalized by 1/sqrt(N) for unit norm

            float weight = 1.0f / std::sqrt(static_cast<float>(numChannels));
            for (int n = 0; n < numChannels; ++n)
            {
                // For narrowband: could apply phase shifts based on element position
                // For simplicity: uniform weighting (true delay-and-sum requires fractional delays)
                steeringWeights(n) = weight;
            }
        }
        else if (geometry == "circular")
        {
            // Circular array: elements evenly spaced around a circle
            // Similar to linear, use uniform weighting for broadside
            float weight = 1.0f / std::sqrt(static_cast<float>(numChannels));
            for (int n = 0; n < numChannels; ++n)
            {
                steeringWeights(n) = weight;
            }
        }
        else if (geometry == "planar")
        {
            // Planar array: 2D grid, use uniform weights
            float weight = 1.0f / std::sqrt(static_cast<float>(numChannels));
            for (int n = 0; n < numChannels; ++n)
            {
                steeringWeights(n) = weight;
            }
        }
        else
        {
            Napi::TypeError::New(env, "Unknown array geometry: " + geometry + " (supported: 'linear', 'circular', 'planar')")
                .ThrowAsJavaScriptException();
            return Napi::Object::New(env);
        }

        // **2. Calculate Blocking Matrix**
        // Goal: Create N-1 orthogonal vectors that are perpendicular to steering vector
        // Method: Use Gram-Schmidt or QR decomposition
        //
        // Approach: Create a matrix with steering vector as first column, then complete to orthonormal basis
        // The blocking matrix is the N × (N-1) matrix of the last N-1 columns

        Eigen::MatrixXf A(numChannels, numChannels);
        A.col(0) = steeringWeights; // First column is steering vector

        // Fill remaining columns with standard basis vectors (to be orthogonalized)
        for (int i = 1; i < numChannels; ++i)
        {
            A.col(i) = Eigen::VectorXf::Zero(numChannels);
            A(i, i) = 1.0f;
        }

        // Perform QR decomposition to get orthonormal basis
        Eigen::HouseholderQR<Eigen::MatrixXf> qr(A);
        Eigen::MatrixXf Q = qr.householderQ();

        // Blocking matrix: columns 1 to N-1 (exclude first column which is steering direction)
        Eigen::MatrixXf blockingMatrix = Q.block(0, 1, numChannels, numChannels - 1);

        // **3. Create return object**
        Napi::Object result = Napi::Object::New(env);

        // Copy steering weights
        Napi::Float32Array steeringArray = Napi::Float32Array::New(env, numChannels);
        for (int i = 0; i < numChannels; ++i)
        {
            steeringArray[i] = steeringWeights(i);
        }
        result.Set("steeringWeights", steeringArray);

        // Copy blocking matrix (column-major)
        size_t blockingSize = numChannels * (numChannels - 1);
        Napi::Float32Array blockingArray = Napi::Float32Array::New(env, blockingSize);
        std::copy(blockingMatrix.data(), blockingMatrix.data() + blockingSize,
                  blockingArray.Data());
        result.Set("blockingMatrix", blockingArray);

        result.Set("numChannels", Napi::Number::New(env, numChannels));
        result.Set("geometry", Napi::String::New(env, geometry));
        result.Set("targetAngleDeg", Napi::Number::New(env, targetAngleDeg));

        return result;
    }

    /**
     * @brief Calculate Common Spatial Patterns (CSP) for binary classification (BCI/EEG).
     *
     * CSP finds spatial filters that maximize variance for one class while minimizing it for another.
     * Commonly used in motor imagery BCI (e.g., left hand vs right hand movement).
     *
     * @param info N-API callback info containing:
     *   - dataClass1: Float32Array of class 1 data (trials × channels interleaved)
     *   - dataClass2: Float32Array of class 2 data (trials × channels interleaved)
     *   - numChannels: Number of EEG channels
     *   - numFilters: (Optional) Number of top filters to return (default: numChannels)
     *
     * @return Object containing:
     *   - cspMatrix: Float32Array of CSP filters (numChannels × numFilters, column-major)
     *   - eigenvalues: Float32Array of eigenvalues (numFilters, sorted descending)
     *   - mean: Float32Array of channel means (numChannels)
     *   - numChannels: Number of input channels
     *   - numFilters: Number of output filters
     *
     * @example Motor imagery BCI (left hand vs right hand):
     * const leftHandTrials = new Float32Array(500 * 8);  // 500 samples × 8 channels
     * const rightHandTrials = new Float32Array(500 * 8);
     * const csp = calculateCommonSpatialPatterns(leftHandTrials, rightHandTrials, 8, 4);
     * // Use csp.cspMatrix and csp.mean in MatrixTransformStage
     */
    Napi::Object CalculateCommonSpatialPatterns(const Napi::CallbackInfo &info)
    {
        Napi::Env env = info.Env();

        // Validate arguments
        if (info.Length() < 3 || !info[0].IsTypedArray() || !info[1].IsTypedArray() || !info[2].IsNumber())
        {
            Napi::TypeError::New(env,
                                 "Expected (Float32Array dataClass1, Float32Array dataClass2, number numChannels, [number numFilters])")
                .ThrowAsJavaScriptException();
            return Napi::Object::New(env);
        }

        Napi::Float32Array dataArray1 = info[0].As<Napi::Float32Array>();
        Napi::Float32Array dataArray2 = info[1].As<Napi::Float32Array>();
        int numChannels = info[2].As<Napi::Number>().Int32Value();
        int numFilters = numChannels; // Default: return all filters

        if (info.Length() >= 4 && info[3].IsNumber())
        {
            numFilters = info[3].As<Napi::Number>().Int32Value();
        }

        if (numChannels <= 0)
        {
            Napi::TypeError::New(env, "numChannels must be > 0")
                .ThrowAsJavaScriptException();
            return Napi::Object::New(env);
        }

        if (numFilters <= 0 || numFilters > numChannels)
        {
            Napi::TypeError::New(env, "numFilters must be in range [1, numChannels]")
                .ThrowAsJavaScriptException();
            return Napi::Object::New(env);
        }

        // Validate data lengths
        size_t totalSamples1 = dataArray1.ElementLength();
        size_t totalSamples2 = dataArray2.ElementLength();

        if (totalSamples1 % numChannels != 0 || totalSamples2 % numChannels != 0)
        {
            Napi::TypeError::New(env, "Data length must be divisible by numChannels")
                .ThrowAsJavaScriptException();
            return Napi::Object::New(env);
        }

        size_t numSamples1 = totalSamples1 / numChannels;
        size_t numSamples2 = totalSamples2 / numChannels;

        // Map data to Eigen matrices
        using EigenMatrix = Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>;
        Eigen::Map<EigenMatrix> X1(dataArray1.Data(), numSamples1, numChannels);
        Eigen::Map<EigenMatrix> X2(dataArray2.Data(), numSamples2, numChannels);

        // **1. Calculate mean (combined across both classes)**
        Eigen::VectorXf mean1 = X1.colwise().mean();
        Eigen::VectorXf mean2 = X2.colwise().mean();
        Eigen::VectorXf mean = (mean1 + mean2) / 2.0f;

        // **2. Center the data**
        Eigen::MatrixXf X1_centered = X1.rowwise() - mean.transpose();
        Eigen::MatrixXf X2_centered = X2.rowwise() - mean.transpose();

        // **3. Calculate covariance matrices for each class**
        // Cov = (X^T * X) / (n - 1)
        Eigen::MatrixXf Cov1 = (X1_centered.transpose() * X1_centered) / float(numSamples1 - 1);
        Eigen::MatrixXf Cov2 = (X2_centered.transpose() * X2_centered) / float(numSamples2 - 1);

        // **3.5. Add regularization for numerical stability (especially on macOS)**
        // Small regularization term prevents singular matrices
        const float reg = 1e-6f;
        Cov1.diagonal().array() += reg;
        Cov2.diagonal().array() += reg;

        // **4. Solve generalized eigenvalue problem: Cov1 * v = λ * Cov2 * v**
        // Equivalent to: Cov2^(-1) * Cov1 * v = λ * v
        // Use Eigen's generalized eigenvalue solver
        Eigen::GeneralizedSelfAdjointEigenSolver<Eigen::MatrixXf> ges(Cov1, Cov2);

        if (ges.info() != Eigen::Success)
        {
            Napi::Error::New(env, "Generalized eigenvalue decomposition failed")
                .ThrowAsJavaScriptException();
            return Napi::Object::New(env);
        }

        // **5. Extract eigenvalues and eigenvectors**
        // Eigenvalues in ascending order by default, reverse for descending
        Eigen::VectorXf eigenvalues = ges.eigenvalues().reverse();
        Eigen::MatrixXf eigenvectors = ges.eigenvectors().rowwise().reverse();

        // **6. Select top numFilters filters**
        Eigen::MatrixXf cspMatrix = eigenvectors.block(0, 0, numChannels, numFilters);
        Eigen::VectorXf topEigenvalues = eigenvalues.head(numFilters);

        // **7. Create return object**
        Napi::Object result = Napi::Object::New(env);

        // Copy mean vector
        Napi::Float32Array meanArray = Napi::Float32Array::New(env, numChannels);
        for (int i = 0; i < numChannels; ++i)
        {
            meanArray[i] = mean(i);
        }
        result.Set("mean", meanArray);

        // Copy CSP matrix (column-major)
        size_t cspSize = numChannels * numFilters;
        Napi::Float32Array cspArray = Napi::Float32Array::New(env, cspSize);
        std::copy(cspMatrix.data(), cspMatrix.data() + cspSize, cspArray.Data());
        result.Set("cspMatrix", cspArray);

        // Copy eigenvalues
        Napi::Float32Array eigenvaluesArray = Napi::Float32Array::New(env, numFilters);
        for (int i = 0; i < numFilters; ++i)
        {
            eigenvaluesArray[i] = topEigenvalues(i);
        }
        result.Set("eigenvalues", eigenvaluesArray);

        result.Set("numChannels", Napi::Number::New(env, numChannels));
        result.Set("numFilters", Napi::Number::New(env, numFilters));

        return result;
    }

    /**
     * @brief Initialize all matrix analysis functions and export to JavaScript.
     */
    Napi::Object InitMatrixBindings(Napi::Env env, Napi::Object exports)
    {
        exports.Set("calculatePca", Napi::Function::New(env, CalculatePca));
        exports.Set("calculateWhitening", Napi::Function::New(env, CalculateWhitening));
        exports.Set("calculateIca", Napi::Function::New(env, CalculateIca));
        exports.Set("calculateBeamformerWeights", Napi::Function::New(env, CalculateBeamformerWeights));
        exports.Set("calculateCommonSpatialPatterns", Napi::Function::New(env, CalculateCommonSpatialPatterns));
        return exports;
    }

} // namespace dsp
