{
  "targets": [
    {
      "target_name": "dspx",
      "sources": [
        "src/native/DspPipeline.cc",
        "src/native/core/MovingAbsoluteValueFilter.cc",
        "src/native/core/MovingAverageFilter.cc",
        "src/native/core/MovingVarianceFilter.cc",
        "src/native/core/MovingZScoreFilter.cc",
        "src/native/core/RmsFilter.cc",
        "src/native/core/SscFilter.cc",
        "src/native/core/WampFilter.cc",
        "src/native/core/WaveformLengthFilter.cc",
        "src/native/core/FftEngine.cc",
        "src/native/core/Fftpack.cc",
        "src/native/core/MovingFftFilter.cc",
        "src/native/core/FirFilter.cc",
        "src/native/core/IirFilter.cc",
        "src/native/FftBindings.cc",
        "src/native/FilterBindings.cc",
        "src/native/MatrixBindings.cc",
        "src/native/UtilityBindings.cc",
        "src/native/utils/CircularBufferArray.cc",
        "src/native/utils/CircularBufferVector.cc",
        "src/native/utils/NapiUtils.cc",
        "src/native/utils/SlidingWindowFilter.cc",
        "src/native/utils/TimeSeriesBuffer.cc",
        "src/native/adapters/FilterStage.cc"
      ],
      "include_dirs": [
        "<!@(node -p \"require('node-addon-api').include\")",
        "src/native",
        "src/native/core",
        "src/native/utils",
        "src/native/adapters",
        "src/native/emg",
        "src/native/vendors/eigen-3.4.0"
      ],
      "defines": [
        "NAPI_VERSION=8"
      ],
      "cflags!": [ "-fno-exceptions" ],
      "cflags_cc!": [ "-fno-exceptions" ],
      "cflags": [ "-O3", "-ffast-math" ],
      "cflags_cc": [ "-std=c++17", "-O3", "-ffast-math" ],      
      "msvs_settings": {
        "VCCLCompilerTool": {
          "ExceptionHandling": 1,
          "AdditionalOptions": [ "/std:c++17", "/O2", "/fp:fast", "/arch:AVX2" ],
          "Optimization": 3,
          "FavorSizeOrSpeed": 1,
          "InlineFunctionExpansion": 2
        }
      },
      "xcode_settings": {
        "GCC_ENABLE_CPP_EXCEPTIONS": "YES",
        "CLANG_CXX_LIBRARY": "libc++",
        "MACOSX_DEPLOYMENT_TARGET": "10.15",
        "OTHER_CPLUSPLUSFLAGS": [ "-std=c++17", "-stdlib=libc++", "-O3", "-ffast-math"],
        "GCC_OPTIMIZATION_LEVEL": "3"
      },
      "conditions": [
        # Condition for Windows
        ["OS=='win'", {
          "defines": [ "_HAS_EXCEPTIONS=1" ]
        }],
        # Condition for x64 architecture (Linux/macOS)
        ['target_arch=="x64"', {
          "cflags+": [ "-msse3", "-mavx", "-mavx2" ],
          "cflags_cc+": [ "-msse3", "-mavx", "-mavx2" ],
          'xcode_settings': {
            'OTHER_CPLUSPLUSFLAGS+': [ '-msse3', '-mavx', '-mavx2' ]
          }
        }],
        # Condition for ia32 architecture (Linux/macOS - if you support 32-bit x86)
        ['target_arch=="ia32"', {
           "cflags+": [ "-msse3", "-mavx", "-mavx2" ], # Or adjust based on 32-bit support
           "cflags_cc+": [ "-msse3", "-mavx", "-mavx2" ],
          'xcode_settings': {
            'OTHER_CPLUSPLUSFLAGS+': [ '-msse3', '-mavx', '-mavx2' ]
          }
        }],
        # Condition for arm64 architecture (Android, iOS, M1/M2 Macs, Tensor G4, etc.)
        ['target_arch=="arm64"', {
          # ARMv8-a baseline: NEON + FP support (compatible with all ARMv8 CPUs)
          "cflags+": [ "-march=armv8-a+fp+simd" ],
          "cflags_cc+": [ "-march=armv8-a+fp+simd" ],
          'xcode_settings': {
            'OTHER_CPLUSPLUSFLAGS+': [ '-march=armv8-a+fp+simd' ]
          }
          # Optional: Upgrade to ARMv8.2-a for newer CPUs (Tensor G4, Apple M2+, Graviton 3+)
          # Enables FP16 arithmetic and additional optimizations
          # Uncomment the lines below to enable ARMv8.2-a:
          # "cflags+": [ "-march=armv8.2-a+fp16" ],
          # "cflags_cc+": [ "-march=armv8.2-a+fp16" ],
          # 'xcode_settings': {
          #   'OTHER_CPLUSPLUSFLAGS+': [ '-march=armv8.2-a+fp16' ]
          # }
        }],
        # Condition for 32-bit ARM (older Android devices)
        ['target_arch=="arm"', {
          "cflags+": [ "-mfpu=neon", "-mfloat-abi=hard" ],  # 32-bit ARM NEON
          "cflags_cc+": [ "-mfpu=neon", "-mfloat-abi=hard" ],
        }]
      ]
    }
  ]
}
