# dspx

## 1.1.4

### Patch Changes

- [`024f861`](https://github.com/A-KGeorge/dspx/commit/024f861d7992e90ec3cf710bc98e2239aa84f573) Thanks [@A-KGeorge](https://github.com/A-KGeorge)! - fixed fft

## 1.1.3

### Patch Changes

- [`e7a292b`](https://github.com/A-KGeorge/dspx/commit/e7a292b10d8be190a69fd9587d536159552ae72a) Thanks [@A-KGeorge](https://github.com/A-KGeorge)! - added fft native chaining.

## 1.1.1

### Patch Changes

- [`3afc644`](https://github.com/A-KGeorge/dspx/commit/3afc6449d4c144f5d2ca1ccbe570b371b28d0e8c) Thanks [@A-KGeorge](https://github.com/A-KGeorge)! - added tests, modified docs,

## 1.1.0

### Minor Changes

- [`c6ec14a`](https://github.com/A-KGeorge/dspx/commit/c6ec14add600fbe3692736286f9d7d33eca602d0) Thanks [@A-KGeorge](https://github.com/A-KGeorge)! - added mfcc, melspectrogram, and stft (all chainable)

## 1.0.1

### Patch Changes

- Fixed parks-mcclellan example file
- Updated licensing documentation (FFTPACK public domain, Eigen MPL-2.0)

## 1.0.0

### Major Changes

- **First stable release!** 🎉
- Parallel FFT batch processing with thread pool and LRU caching
- Comprehensive DSP pipeline with 40+ processing stages
- Native C++ acceleration with SIMD optimizations (AVX2, NEON)
- Redis-backed state persistence for distributed processing
- TypeScript/JavaScript API with full type safety
- Cross-platform support: Windows, macOS (x64/ARM64), Linux

### Features Included

- **Filters**: FIR, IIR, Notch, Bandpass, Highpass, Lowpass, Bessel, Butterworth, Chebyshev
- **FFT**: Forward/inverse FFT with batch processing and caching (100x speedup for repeated signals)
- **Time-Series**: Irregular timestamp support, interpolation, resampling
- **Matrix Analysis**: Covariance, eigendecomposition, whitening
- **Spatial Filters**: Beamforming, CSP (Common Spatial Patterns), channel selection
- **Advanced**: Wavelet transforms, Hilbert envelope, adaptive filters (LMS, NLMS, RLS)
- **Utilities**: Moving average, variance, rectification, peak detection, envelope detection

## 1.0.0-alpha.18

### Major Changes

- [`653a144`](https://github.com/A-KGeorge/dspx/commit/653a144fd3260d7a17150c49d7dcf26a4f7cfc01) Thanks [@A-KGeorge](https://github.com/A-KGeorge)! - added fft batch and cached it

## 0.2.0-alpha.17

### Minor Changes

- [`546583f`](https://github.com/A-KGeorge/dspx/commit/546583f45be6e4c1e907eb08d6d735a2059cd3ec) Thanks [@A-KGeorge](https://github.com/A-KGeorge)! - added core from eigen to .npmignore

## 0.2.0-alpha.16

### Minor Changes

- [`af68940`](https://github.com/A-KGeorge/dspx/commit/af68940be7378c34f3a1e60bc0814b9a61d3acc9) Thanks [@A-KGeorge](https://github.com/A-KGeorge)! - added filters, optimized fir

## 0.2.0-alpha.15

### Patch Changes

- [`d066be6`](https://github.com/A-KGeorge/dspx/commit/d066be6bf297d7ddefef8b9f2a12d76c4e253301) Thanks [@A-KGeorge](https://github.com/A-KGeorge)! - added disclaimer for arm processors

## 0.2.0-alpha.14

### Patch Changes

- [`46ea7ca`](https://github.com/A-KGeorge/dspx/commit/46ea7ca3d98eaf9cf84e67a61481c76b379fe9f3) Thanks [@A-KGeorge](https://github.com/A-KGeorge)! - pixel build 3

## 0.2.0-alpha.13

### Patch Changes

- [`be82251`](https://github.com/A-KGeorge/dspx/commit/be8225168143ae956934a86d279e4a3f3defca4f) Thanks [@A-KGeorge](https://github.com/A-KGeorge)! - optimization for neon

## 0.2.0-alpha.12

### Patch Changes

- [`e72da53`](https://github.com/A-KGeorge/dspx/commit/e72da538a230fc9f2c05806b3d55a94401896f5e) Thanks [@A-KGeorge](https://github.com/A-KGeorge)! - optimized neon

## 0.2.0-alpha.11

### Minor Changes

- [`fd0518d`](https://github.com/A-KGeorge/dspx/commit/fd0518de4a3d922d9fe8be6ba4f92a2a0fa85a31) Thanks [@A-KGeorge](https://github.com/A-KGeorge)! - Add prebuild for linux-arm64

## 0.2.0-alpha.10

### Minor Changes

- [`08a1f50`](https://github.com/A-KGeorge/dspx/commit/08a1f501dfe2db187bbe192c88f7d83e4153a853) Thanks [@A-KGeorge](https://github.com/A-KGeorge)! - fixed performance for convolution, add differential filter/linear regression/dot product for ts, optimized for arm neon as well

## 0.2.0-alpha.9

### Minor Changes

- [`63c3b35`](https://github.com/A-KGeorge/dspx/commit/63c3b35b6e8aaf121a0fc50ea2571d89946e17e9) Thanks [@A-KGeorge](https://github.com/A-KGeorge)! - optimized fft, fir, and iir

## 0.2.0-alpha.8

### Minor Changes

- [`893cb30`](https://github.com/A-KGeorge/dspx/commit/893cb304a6651193a341be9d5a5d0567b232fde4) Thanks [@A-KGeorge](https://github.com/A-KGeorge)! - Added circuit breaker for backend logging

## 0.2.0-alpha.7

### Minor Changes

- [`baf4d07`](https://github.com/A-KGeorge/dspx/commit/baf4d07d2755afb75a5a056555b448e0b1d1e254) Thanks [@A-KGeorge](https://github.com/A-KGeorge)! - Used SimdOps with FFT properly

## 0.2.0-alpha.6

### Patch Changes

- [`f475db4`](https://github.com/A-KGeorge/dspx/commit/f475db4ef755f7780343305d8db9babd9c453718) Thanks [@A-KGeorge](https://github.com/A-KGeorge)! - import path fix 1

## 0.2.0-alpha.5

### Patch Changes

- [`5213143`](https://github.com/A-KGeorge/dspx/commit/5213143cfd98739a3e756496580168e4250413b9) Thanks [@A-KGeorge](https://github.com/A-KGeorge)! - fixed imports

## 0.2.0-alpha.4

### Patch Changes

- [`99fa2c8`](https://github.com/A-KGeorge/dspx/commit/99fa2c8f7a2bab982a963b7327b297a661bc1020) Thanks [@A-KGeorge](https://github.com/A-KGeorge)! - fixed imports

## 0.2.0-alpha.3

### Patch Changes

- [`0d0fd27`](https://github.com/A-KGeorge/dspx/commit/0d0fd27b33759559b8217cb9d8388675849832ae) Thanks [@A-KGeorge](https://github.com/A-KGeorge)! - fixed import paths

## 0.2.0-alpha.2

### Patch Changes

- [`399cd77`](https://github.com/A-KGeorge/dspx/commit/399cd77a3d1f062e39b160990c9e700ed3030546) Thanks [@A-KGeorge](https://github.com/A-KGeorge)! - included extensions in index.ts

## 0.2.0-alpha.1

### Patch Changes

- [`f77670f`](https://github.com/A-KGeorge/dspx/commit/f77670fa354542a92a852b3b94d6297ec37abadb) Thanks [@A-KGeorge](https://github.com/A-KGeorge)! - Included files
