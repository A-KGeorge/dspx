#pragma once

#include "../IDspStage.h"
#include "../utils/SimdOps.h"
#include <Eigen/Dense>
#include <vector>
#include <stdexcept>
#include <cmath>
#include <string>
#include <algorithm>

namespace dsp::adapters
{
    /**
     * @brief Kalman Filter Stage for tracking interleaved multi-dimensional data
     *
     * Implements a discrete Kalman filter for position/velocity tracking with support
     * for interleaved data streams (e.g., [x, y, x, y] or [lat, lon, alt, lat, lon, alt]).
     *
     * Perfect for:
     * - GPS tracking (lat/lon pairs)
     * - 3D position tracking (x/y/z coordinates)
     * - Sensor fusion (accelerometer, gyroscope)
     * - Any multi-dimensional time series with noise
     *
     * State vector: [position, velocity] for each dimension
     * Measurement: position only
     *
     * Uses existing SIMD operations for efficient vector math on interleaved data.
     */
    class KalmanFilterStage : public IDspStage
    {
    public:
        /**
         * @brief Constructs a Kalman Filter Stage
         * @param dimensions Number of dimensions (2 for lat/lon, 3 for x/y/z, etc.)
         * @param process_noise Process noise covariance (Q) - models uncertainty in motion
         * @param measurement_noise Measurement noise covariance (R) - models sensor noise
         * @param initial_error Initial state estimation error covariance (P)
         */
        explicit KalmanFilterStage(
            int dimensions = 2,
            float process_noise = 1e-5f,
            float measurement_noise = 1e-2f,
            float initial_error = 1.0f)
            : m_dimensions(dimensions),
              m_process_noise(process_noise),
              m_measurement_noise(measurement_noise),
              m_initial_error(initial_error),
              m_initialized(false),
              m_first_measurement_received(false)
        {
            if (dimensions < 1 || dimensions > 10)
            {
                throw std::invalid_argument("KalmanFilter: dimensions must be between 1 and 10");
            }

            // State size: 2 * dimensions (position + velocity for each)
            m_state_size = 2 * dimensions;
        }

        const char *getType() const override
        {
            return "kalmanFilter";
        }

        void process(float *buffer, size_t numSamples, int numChannels, const float *timestamps = nullptr) override
        {
            if (numChannels != m_dimensions)
            {
                throw std::invalid_argument("KalmanFilter: number of channels must match dimensions");
            }

            // Initialize filter on first call
            if (!m_initialized)
            {
                initialize();
            }

            // Process interleaved samples: [x0, y0, x1, y1, x2, y2, ...]
            // Each group of m_dimensions values is one measurement vector
            const size_t samplesPerChannel = numSamples / numChannels;

            for (size_t i = 0; i < samplesPerChannel; i++)
            {
                // Extract measurement vector from interleaved buffer
                Eigen::VectorXf measurement(m_dimensions);
                for (int d = 0; d < m_dimensions; d++)
                {
                    measurement(d) = buffer[i * m_dimensions + d];
                }

                // Initialize state from first measurement (cold start) - only once ever
                if (!m_first_measurement_received)
                {
                    for (int d = 0; d < m_dimensions; d++)
                    {
                        m_state(d) = measurement(d); // Initialize position from first measurement
                    }
                    m_first_measurement_received = true;
                }

                // Predict step
                predict(timestamps ? timestamps[i] : 1.0f / 360.0f); // Default 360 Hz

                // Update step with measurement
                update(measurement);

                // Write filtered position back to buffer
                for (int d = 0; d < m_dimensions; d++)
                {
                    buffer[i * m_dimensions + d] = m_state(d); // Position component
                }
            }
        }

        Napi::Object serializeState(Napi::Env env) const override
        {
            Napi::Object state = Napi::Object::New(env);

            state.Set("dimensions", Napi::Number::New(env, m_dimensions));
            state.Set("processNoise", Napi::Number::New(env, m_process_noise));
            state.Set("measurementNoise", Napi::Number::New(env, m_measurement_noise));
            state.Set("initialized", Napi::Boolean::New(env, m_initialized));
            state.Set("firstMeasurementReceived", Napi::Boolean::New(env, m_first_measurement_received));

            if (m_initialized)
            {
                // Serialize state vector [pos0, pos1, ..., vel0, vel1, ...]
                Napi::Array stateArray = Napi::Array::New(env, m_state.size());
                for (int i = 0; i < m_state.size(); i++)
                {
                    stateArray.Set(static_cast<uint32_t>(i), Napi::Number::New(env, m_state(i)));
                }
                state.Set("state", stateArray);

                // Serialize covariance matrix P (flattened row-major)
                Napi::Array covArray = Napi::Array::New(env, m_P.size());
                int idx = 0;
                for (int i = 0; i < m_P.rows(); i++)
                {
                    for (int j = 0; j < m_P.cols(); j++)
                    {
                        covArray.Set(static_cast<uint32_t>(idx++), Napi::Number::New(env, m_P(i, j)));
                    }
                }
                state.Set("covariance", covArray);
            }

            return state;
        }

        void deserializeState(const Napi::Object &state) override
        {
            if (state.Has("dimensions"))
            {
                m_dimensions = state.Get("dimensions").As<Napi::Number>().Int32Value();
                m_state_size = 2 * m_dimensions;
            }

            if (state.Has("processNoise"))
            {
                m_process_noise = state.Get("processNoise").As<Napi::Number>().FloatValue();
            }

            if (state.Has("measurementNoise"))
            {
                m_measurement_noise = state.Get("measurementNoise").As<Napi::Number>().FloatValue();
            }

            if (state.Has("initialized"))
            {
                m_initialized = state.Get("initialized").As<Napi::Boolean>().Value();
            }
            if (state.Has("firstMeasurementReceived"))
            {
                m_first_measurement_received = state.Get("firstMeasurementReceived").As<Napi::Boolean>().Value();
            }

            if (m_initialized && state.Has("state"))
            {
                Napi::Array stateArray = state.Get("state").As<Napi::Array>();
                m_state.resize(stateArray.Length());
                for (uint32_t i = 0; i < stateArray.Length(); i++)
                {
                    m_state(i) = stateArray.Get(i).As<Napi::Number>().FloatValue();
                }

                if (state.Has("covariance"))
                {
                    Napi::Array covArray = state.Get("covariance").As<Napi::Array>();
                    m_P.resize(m_state_size, m_state_size);
                    int idx = 0;
                    for (int i = 0; i < m_state_size; i++)
                    {
                        for (int j = 0; j < m_state_size; j++)
                        {
                            m_P(i, j) = covArray.Get(idx++).As<Napi::Number>().FloatValue();
                        }
                    }
                }

                // Reinitialize Q and R matrices (not serialized, derived from parameters)
                m_Q = Eigen::MatrixXf::Identity(m_state_size, m_state_size) * m_process_noise;
                m_R = Eigen::MatrixXf::Identity(m_dimensions, m_dimensions) * m_measurement_noise;
            }
        }

        void reset() override
        {
            // Reset to uninitialized state - will reinitialize on next process() call
            m_initialized = false;
            m_first_measurement_received = false;
            m_state = Eigen::VectorXf();
            m_P = Eigen::MatrixXf();
            m_Q = Eigen::MatrixXf();
            m_R = Eigen::MatrixXf();
        }

        void serializeToon(toon::Serializer &serializer) const override
        {
            // Serialize configuration
            serializer.writeInt32(m_dimensions);
            serializer.writeFloat(m_process_noise);
            serializer.writeFloat(m_measurement_noise);
            serializer.writeFloat(m_initial_error);
            serializer.writeBool(m_initialized);
            serializer.writeBool(m_first_measurement_received);

            if (m_initialized)
            {
                // Serialize state vector
                serializer.writeInt32(m_state.size());
                for (int i = 0; i < m_state.size(); i++)
                {
                    serializer.writeFloat(m_state(i));
                }

                // Serialize covariance matrix P
                serializer.writeInt32(m_P.rows());
                serializer.writeInt32(m_P.cols());
                for (int i = 0; i < m_P.rows(); i++)
                {
                    for (int j = 0; j < m_P.cols(); j++)
                    {
                        serializer.writeFloat(m_P(i, j));
                    }
                }
            }
        }

        void deserializeToon(toon::Deserializer &deserializer) override
        {
            // Deserialize configuration
            m_dimensions = deserializer.readInt32();
            m_process_noise = deserializer.readFloat();
            m_measurement_noise = deserializer.readFloat();
            m_initial_error = deserializer.readFloat();
            m_initialized = deserializer.readBool();
            m_first_measurement_received = deserializer.readBool();

            m_state_size = 2 * m_dimensions;

            if (m_initialized)
            {
                // Deserialize state vector
                int stateSize = deserializer.readInt32();
                m_state.resize(stateSize);
                for (int i = 0; i < stateSize; i++)
                {
                    m_state(i) = deserializer.readFloat();
                }

                // Deserialize covariance matrix P
                int rows = deserializer.readInt32();
                int cols = deserializer.readInt32();
                m_P.resize(rows, cols);
                for (int i = 0; i < rows; i++)
                {
                    for (int j = 0; j < cols; j++)
                    {
                        m_P(i, j) = deserializer.readFloat();
                    }
                }

                // Reinitialize Q and R matrices based on configuration
                m_Q = Eigen::MatrixXf::Identity(m_state_size, m_state_size) * m_process_noise;
                m_R = Eigen::MatrixXf::Identity(m_dimensions, m_dimensions) * m_measurement_noise;
            }
        }

    private:
        void initialize()
        {
            // State: [pos_1, pos_2, ..., pos_n, vel_1, vel_2, ..., vel_n]
            m_state = Eigen::VectorXf::Zero(m_state_size);

            // Covariance matrix P (state_size x state_size)
            m_P = Eigen::MatrixXf::Identity(m_state_size, m_state_size) * m_initial_error;

            // Process noise covariance Q (state_size x state_size)
            m_Q = Eigen::MatrixXf::Identity(m_state_size, m_state_size) * m_process_noise;

            // Measurement noise covariance R (dimensions x dimensions)
            m_R = Eigen::MatrixXf::Identity(m_dimensions, m_dimensions) * m_measurement_noise;

            m_initialized = true;
        }

        void predict(float dt)
        {
            // State transition matrix F for constant velocity model
            // F = [I  dt*I]
            //     [0   I  ]
            Eigen::MatrixXf F = Eigen::MatrixXf::Identity(m_state_size, m_state_size);
            for (int i = 0; i < m_dimensions; i++)
            {
                F(i, m_dimensions + i) = dt;
            }

            // Predict state: x_pred = F * x
            m_state = F * m_state;

            // Predict covariance: P_pred = F * P * F^T + Q
            m_P = F * m_P * F.transpose() + m_Q;
        }

        void update(const Eigen::VectorXf &measurement)
        {
            // Measurement matrix H: [I 0] (only observe position, not velocity)
            Eigen::MatrixXf H = Eigen::MatrixXf::Zero(m_dimensions, m_state_size);
            H.block(0, 0, m_dimensions, m_dimensions) = Eigen::MatrixXf::Identity(m_dimensions, m_dimensions);

            // Innovation: y = z - H * x_pred
            Eigen::VectorXf innovation = measurement - H * m_state;

            // Innovation covariance: S = H * P * H^T + R
            Eigen::MatrixXf S = H * m_P * H.transpose() + m_R;

            // Kalman gain: K = P * H^T * S^(-1)
            // Use Eigen's inverse() which handles small matrices efficiently
            Eigen::MatrixXf K = m_P * H.transpose() * S.inverse();

            // Update state: x = x_pred + K * innovation
            m_state = m_state + K * innovation;

            // Update covariance: P = (I - K * H) * P
            Eigen::MatrixXf I = Eigen::MatrixXf::Identity(m_state_size, m_state_size);
            m_P = (I - K * H) * m_P;
        }

        int m_dimensions;          // Number of dimensions (2 for lat/lon, 3 for xyz, etc.)
        int m_state_size;          // 2 * dimensions (position + velocity)
        float m_process_noise;     // Q - process noise covariance
        float m_measurement_noise; // R - measurement noise covariance
        float m_initial_error;     // P0 - initial error covariance
        bool m_initialized;
        bool m_first_measurement_received; // Track if we've received first measurement for cold start

        Eigen::VectorXf m_state; // State vector [pos, vel]
        Eigen::MatrixXf m_P;     // Error covariance matrix
        Eigen::MatrixXf m_Q;     // Process noise covariance matrix
        Eigen::MatrixXf m_R;     // Measurement noise covariance matrix
    };

} // namespace dsp::adapters
