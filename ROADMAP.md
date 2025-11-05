# 🧭 dspx Roadmap

This roadmap outlines the planned evolution of **dspx** — a native **C++ + TypeScript DSP** framework featuring **Redis-based state persistence** and **low-overhead logging**.

---

## 🚀 Immediate Next Steps

**Resampling Operations** (Expected in next few days):

- `Decimate`: Downsample by integer factor M with anti-aliasing filter
- `Interpolate`: Upsample by integer factor L with anti-imaging filter
- `Resample`: Rational resampling (L/M) for arbitrary sample rate conversion

All three will use efficient polyphase FIR filtering implemented in C++ for maximum performance, with full TypeScript wrappers and comprehensive test coverage.

---

## ✅ Current Progress

- [x] **Redis Integration (Serialization / Deserialization)**
- [x] **Advanced Logging (Circular Buffer, Topic Routing, Concurrency)**
- [x] **Core DSP Filters:** `movingAverage`, `rms`, `rectify`, `variance`, `zScoreNormalize`, `mav`, `waveformLength`, `willisonAmplitude`, `slopeSignChange`
- [x] **FFT Implementation:** Forward/inverse FFT, RFFT, windowing, magnitude/phase extraction
- [x] **Filter Design:** FIR (low/high/band-pass/band-stop), IIR (Butterworth, Chebyshev), Biquad EQ (peaking, low-shelf, high-shelf)
- [x] **Advanced Signal Analysis:** Hjorth parameters, spectral features (centroid, rolloff, flux), entropy measures (Shannon, SampEn, ApEn)
- [x] **Signal Analysis Utilities:** `integrator`, `differentiator`, `snr`, `detrend`, `autocorrelation`, `crossCorrelation` (FFT-based implementations)
- [x] **Pipeline Integration:** FIR/IIR filters can be added to DSP pipelines with proper coefficient handling
- [x] **Utility:** `listState`, `clearState`, `getState`, `saveState`
- [x] **Bug Fixes:** Cutoff frequency validation, coefficient copying, state management, pipeline filter chaining

---

## 🧩 1. Consolidated Feature Table

| **Category**                          | **Methods**                                                                                                                                                                                           | **Description / Use Case**                          | **Redis Usage**                  | **Implementation Difficulty** |
| ------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------- | -------------------------------- | ----------------------------- |
| 🧩 **Core Time-Domain Filters**       | ✅ `movingAverage`, ✅ `rms`, ✅ `rectify`, ✅ `variance`, ✅ `zScoreNormalize`, ✅ `mav`, ✅ `waveformLength`, ✅ `willisonAmplitude`, ✅ `slopeSignChange`                                          | Core smoothing and EMG amplitude estimation         | Buffer persistence (per channel) | 🟢 Easy                       |
| 🧠 **Statistical / Entropy Features** | ✅ `hjorthParameters`, ✅ `entropy`, ✅ `sampleEntropy`, ✅ `approximateEntropy`, ☐ `kurtosis`, ☐ `skewness`                                                                                          | Shape and complexity features                       | Aggregates per window            | 🟡 Medium                     |
| 🔉 **Spectral / Transform Domain**    | ✅ `fft`, ✅ `rfft`, ✅ `ifft`, ✅ `irfft`, ✅ `spectralCentroid`, ✅ `spectralRolloff`, ✅ `spectralFlux`, ✅ `hilbertTransform`, ✅ `waveletTransform`, ☐ `stft`, ☐ `melSpectrogram`, ☐ `mfcc`      | Frequency and time-frequency analysis               | Optional (RedisJSON possible)    | 🔴 Hard                       |
| 🎛 **Filtering (Classic + Modern)**    | ✅ `firFilter`, ✅ `iirFilter`, ✅ `butterworthLowpass/Highpass/Bandpass`, ✅ `chebyshevLowpass/Highpass/Bandpass`, ✅ `peakingEQ`, ✅ `lowShelf`, ✅ `highShelf`, ☐ `kalmanFilter`, ☐ `wienerFilter` | Filtering for sensor / audio data                   | Coefficients / state storage     | 🔴 Hard                       |
| ⏱ **Resampling / Rate Control**       | ✅ `polyphaseDecimate`, ✅ `interpolate`, ✅ `resample`                                                                                                                                               | Resampling and alias mitigation                     | Redis phase/delay tracking       | 🟡 Medium                     |
| 🔊 **Fundamental Frequency**          | ☐ `yin`, ☐ `cepstrumPitch`                                                                                                                                                                            | Pitch / F₀ estimation for audio or tremor detection | Difference function buffers      | 🔴 Hard                       |
| 🪞 **Feature Extraction (Spectral)**  | ✅ `spectralCentroid`, ✅ `spectralRolloff`, ✅ `spectralFlux`, ☐ `spectralFlatness`, ☐ `mfcc`                                                                                                        | Audio / signal features for ML                      | Aggregates + filterbank storage  | 🟡 Medium                     |
| 🧬 **Adaptive Filters**               | ✅ `lmsFilter`, ✅ `nlmsFilter`, ✅ `rls`, ☐ `wienerFilter`, ✅ `pca`, ✅ `ica`, ✅ `whiten`                                                                                                          | Adaptive denoising + decorrelation                  | Redis holds coefficients         | 🔴 Hard                       |
| ⚡ **Signal Analysis Utilities**      | ✅ `autocorrelation`, ✅ `crossCorrelation`, ✅ `detrend`, ✅ `integrator`, ✅ `differentiator`, ✅ `snr`, ✅ `clipDetection`, ✅ `peakDetection`                                                      | Pre/post-processing utilities                       | Minimal (buffer only)            | 🟢 Easy                       |
| 🧍‍♂️ **EMG / Biosignal Specific**       | ☐ `muscleActivationThreshold`, ☐ `fatigue`, ☐ `autoregression`, ☐ `arCoefficients`                                                                                                                    | Biomedical signal interpretation                    | Redis calibration + baseline     | 🟡 Medium                     |
| 📡 **Amplitude / Modulation**         | ☐ `amDemod`, ☐ `amMod`, ☐ `envelopeDetect`, ☐ `instantaneousPhase`                                                                                                                                    | Modulation and envelope features                    | Low-pass filter state            | 🟡 Medium                     |
| 🧠 **Multi-Channel Spatial Ops**      | ☐ `channelSelect`, ☐ `channelMerge`, ✅ `spatialFilter`, ✅ `beamformer`                                                                                                                              | Multi-channel EEG/EMG processing                    | Multi-channel buffers            | 🔴 Hard                       |
| 🔧 **Utilities**                      | ✅ `clearState`, ✅ `getState`, ✅ `listState`                                                                                                                                                        | Redis state management + debugging                  | Full Redis integration           | 🟢 Easy                       |
| 🌀 **Wavelet Filters (Daubechies)**   | ✅ `haar`, ✅ `db2`–`db10`                                                                                                                                                                            | Multi-resolution analysis                           | Redis stores transform levels    | 🟡 Medium                     |

---

## 🧱 2. Implementation Phases

### 🟩 **Stage 1 — MVP / Easy**

| Priority | Category                                                 | Status | Notes                                 |
| -------- | -------------------------------------------------------- | ------ | ------------------------------------- |
| 1️⃣       | `movingAverage`, `rms`, `rectify`, `variance`            | [X]    | Baseline DSP primitives (C++ + N-API) |
| 2️⃣       | `waveformLength`, `willisonAmplitude`, `slopeSignChange` | [X]    | Next EMG feature set                  |
| 3️⃣       | `clearState`, `getState`, `listState`, `saveState`       | [X]    | Complete Redis debug utilities        |

---

### 🟩 **Stage 1.5 — Signal Analysis Utilities** ✅ **COMPLETE**

| Operation          | Status | Implementation Details                                       | Tests | Use Cases                                     |
| ------------------ | ------ | ------------------------------------------------------------ | ----- | --------------------------------------------- |
| `integrator`       | [X]    | Cumulative sum for DC/trend extraction                       | ✓     | Velocity from acceleration, DC offset         |
| `differentiator`   | [X]    | First-order difference for rate-of-change                    | ✓     | Acceleration from velocity, high-pass         |
| `snr`              | [X]    | Windowed signal-to-noise ratio (dB)                          | ✓     | Quality metrics, noise floor analysis         |
| `detrend`          | [X]    | Linear/constant detrending                                   | ✓     | Baseline removal, drift correction            |
| `autocorrelation`  | [X]    | FFT-based auto-correlation for pitch/periodicity             | ✓     | Pitch detection, echo analysis, pattern       |
| `crossCorrelation` | [X]    | FFT-based cross-correlation for time delay/template matching | ✓     | Echo delay, sensor alignment, template search |

**Implementation Highlights:**

- **FFT-based correlation**: Uses `conj(FFT(x)) * FFT(y)` formula with zero-padding to 2n for linear correlation
- **Comprehensive testing**: 825/825 tests passing across all 6 utilities
- **Mathematical validation**: Debug scripts confirmed FFT output matches naive implementations
- **Edge case handling**: Empty signals, single samples, all zeros, large arrays, negative values
- **Real-world scenarios**: Echo detection, sensor alignment, template matching, pitch detection
- **Property verification**: Dot product equivalence, symmetry, normalization bounds

**Key Formula Corrections:**

- Autocorrelation: `IFFT(|FFT(x)|²)` for xcorr[k] = sum x[i]\*x[i+k]
- Cross-correlation: `IFFT(conj(FFT(x)) * FFT(y))` for xcorr[k] = sum x[i]\*y[i+k]
- Zero-padding: Next power-of-2 ≥ 2n prevents circular convolution artifacts

---

### 🟨 **Stage 2 — Intermediate (Math + Buffer Dependent)**

| Priority | Category                                              | Status | Notes                                |
| -------- | ----------------------------------------------------- | ------ | ------------------------------------ |
| 4️⃣       | `zScoreNormalize`, `mav`, `hjorthParameters`          | [X]    | Window math & standard deviation ops |
| 5️⃣       | `polyphaseDecimate`, `interpolate`, `resample`        | [X]    | Leverages Circular Buffers           |
| 6️⃣       | `spectralCentroid`, `spectralRolloff`, `spectralFlux` | [X]    | Derived FFT metrics                  |
| 7️⃣       | `entropy`, `sampleEntropy`, `approximateEntropy`      | [X]    | Complexity metrics per window        |

**🚀 Resampling Implementation Plan:**

- C++ polyphase FIR decimator with anti-aliasing
- C++ polyphase FIR interpolator with anti-imaging
- C++ rational resampler (L/M) combining both
- Full N-API bindings and TypeScript wrappers
- Comprehensive test coverage for correctness and edge cases

---

### 🔴 **Stage 3 — Advanced DSP / FFT / Wavelets**

| Priority | Category                                                     | Status | Notes                          |
| -------- | ------------------------------------------------------------ | ------ | ------------------------------ |
| 8️⃣       | `fft`, `hilbertTransform`, `hilbertEnvelope`                 | [X]    | Transform foundation           |
| 9️⃣       | `firFilter`, `butterworthFilter`, `notchFilter`, `iirFilter` | [X]    | Real-world filter validation   |
| 🔟       | `waveletTransform`, `haar`, `db2–db10`                       | [X]    | Decomposition + reconstruction |

---

### 🧠 **Stage 4 — Adaptive / Statistical / Multichannel**

| Category                         | Status | Notes                               |
| -------------------------------- | ------ | ----------------------------------- |
| `lmsFilter`, `nlmsFilter`, `rls` | [X]    | Adaptive learning filters           |
| `pca`, `ica`, `whiten`           | [X]    | Statistical transformations         |
| `beamformer`, `spatialFilter`    | [X]    | Vectorized multi-channel processing |

---

### 📊 **Stage 5 — Visualization & Monitoring**

| Priority | Category                                           | Status | Notes                                        |
| -------- | -------------------------------------------------- | ------ | -------------------------------------------- |
| 🎨       | **Server-Side Plotting** (Matplotlib/Seaborn-like) | [ ]    | Generate PNG/SVG plots for debugging/reports |
| 📈       | **Real-Time Dashboard** (D3.js + uWS)              | [ ]    | Live signal visualization with WebSockets    |
| 🔍       | **Signal Inspector** (Interactive Analysis)        | [ ]    | Zoom, pan, measure time-domain features      |
| 📉       | **Spectrogram Viewer**                             | [ ]    | Time-frequency visualization                 |

**Server-Side Plotting Use Cases:**

- **Debugging**: Save PNG of signal before/after filtering
- **Analysis**: Generate histograms of DriftDetector values
- **Reporting**: Nightly jobs with emailed plot attachments
- **CI/CD**: Automated test reports with visual validation

**Real-Time Dashboard Features:**

- **WebSocket Streaming**: uWebSockets for low-latency data push
- **D3.js Visualizations**: Interactive charts, spectrograms, waterfalls
- **Multi-Channel Display**: Synchronized views across channels
- **State Monitoring**: Redis state visualization (like Kafka/Redis admin panels)
- **Performance Metrics**: Latency histograms, throughput graphs

**Potential Libraries:**

- Server-Side: `node-canvas`, `sharp`, `svg.js` for plot generation
- Real-Time: `uWebSockets.js` for streaming, `D3.js` for client rendering
- Inspiration: Grafana-like dashboard for DSP pipelines

---

## 📁 3. Suggested Project Structure

```
dspx/
├── src/
│   ├── native/
│   │   ├── core/
│   │   │   ├── MovingAverage.cc
│   │   │   ├── RMS.cc
│   │   │   ├── Variance.cc
│   │   │   └── Rectify.cc
│   │   ├── filters/
│   │   │   ├── FIRFilter.cc
│   │   │   ├── Butterworth.cc
│   │   │   └── NotchFilter.cc
│   │   ├── transforms/
│   │   │   ├── FFT.cc
│   │   │   ├── Hilbert.cc
│   │   │   └── Wavelet.cc
│   │   ├── features/
│   │   │   ├── MAV.cc
│   │   │   ├── Hjorth.cc
│   │   │   └── Entropy.cc
│   │   ├── emg/
│   │   │   ├── Willison.cc
│   │   │   ├── SlopeSignChange.cc
│   │   │   └── WaveformLength.cc
│   │   ├── utils/
│   │   │   ├── DSPMath.h
│   │   │   └── CircularBuffer.h
│   │   ├── DSPSystem.cc
│   │   └── DSPSystem.h
│   ├── ts/
│   │   ├── index.ts
│   │   ├── pipeline/
│   │   │   ├── Pipeline.ts
│   │   │   ├── Stage.ts
│   │   │   └── Store.ts
│   │   └── bindings.ts
│   └── build/
├── CMakeLists.txt
├── package.json
├── tsconfig.json
└── README.md
```

---

## 🚀 **Next Goals**

- [x] Add time-domain EMG features (`waveformLength`, `willisonAmplitude`, `slopeSignChange`)
- [x] Implement true time-based filtering with sample expiration by age
- [x] Introduce FFT and Hilbert transform pipeline (partial)
- [x] Begin filter design (Butterworth + Notch)
- [ ] Add server-side plotting (matplotlib-like) for debugging and reports
- [ ] Build real-time dashboard with D3.js + uWebSockets for live visualization
- [ ] Benchmark native C++ vs pure JS performance
- [ ] Expand unit tests for new stages and Redis states
