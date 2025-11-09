# Prebuilds Guide

This document explains how to generate and use prebuilt native binaries for dspx.

## Installation Strategy

dspx uses an intelligent installation strategy:

- **ARM architectures** (arm, arm64, aarch64): Always compile locally for optimal performance
- **x64/ia32 architectures**: Use prebuilt binaries (no C++ toolchain required)

This ensures:

1. Most users get fast installs without build tools
2. ARM users get hardware-optimized builds
3. Developers can easily test on any platform

## Generating Prebuilds

### Prerequisites

You need to build prebuilds on each target platform:

- **Windows x64**: Windows machine with Visual Studio Build Tools
- **Linux x64**: Linux machine with build-essential
- **macOS x64**: Intel Mac with Xcode Command Line Tools
- **macOS arm64**: Apple Silicon Mac with Xcode Command Line Tools

### Build Process

On each platform, run:

```bash
# Install dependencies
npm install

# Generate prebuilds for Node.js 18, 20, and 22
npm run prebuildify
```

This creates prebuilds in the `prebuilds/` directory with the following structure:

```
prebuilds/
├── darwin-arm64/
│   ├── node.napi.node      # Node.js 18+
├── darwin-x64/
│   ├── node.napi.node
├── linux-x64/
│   ├── node.napi.node
├── win32-x64/
│   ├── node.napi.node
```

### Node.js Version Compatibility

The `prebuildify` command targets:

- Node.js 18 (LTS)
- Node.js 20 (LTS)
- Node.js 22 (Current)

Since we use N-API version 8, a single binary works across all these versions.

## Using Prebuilds

### For Package Consumers

When users install dspx:

```bash
npm install dspx
```

The installation process:

1. **On ARM machines**: Automatically compiles from source

   - Requires C++ build tools
   - Optimized for specific ARM CPU features

2. **On x64 machines**: Uses prebuilt binary
   - No C++ build tools needed
   - Fast installation (~5 seconds)

### For Package Maintainers

Before publishing a new version:

1. Build prebuilds on all platforms:

   ```bash
   # On each platform:
   npm run prebuildify
   ```

2. Commit the `prebuilds/` directory:

   ```bash
   git add prebuilds/
   git commit -m "chore: update prebuilds for vX.Y.Z"
   ```

3. Publish the package:
   ```bash
   npm run publish-packages
   ```

## Testing Prebuilds

To test that prebuilds work correctly:

```bash
# 1. Remove build artifacts
rm -rf build/

# 2. Simulate fresh install
npm install

# 3. Run tests
npm test
```

On x64 systems, this should:

- Skip compilation
- Load the prebuilt binary
- Pass all tests

## Troubleshooting

### "No prebuilt binary found"

If you see this error:

1. Check that `prebuilds/` directory exists
2. Verify your platform/architecture matches available prebuilds
3. For ARM: Install C++ build tools and let it compile

### "Compilation failed"

On ARM systems:

- **macOS**: Install Xcode Command Line Tools

  ```bash
  xcode-select --install
  ```

- **Linux**: Install build-essential
  ```bash
  sudo apt-get update
  sudo apt-get install build-essential
  ```

### "Version mismatch"

If prebuilds don't work:

1. Check Node.js version: `node --version`
2. Ensure it's 18, 20, or 22
3. Clear npm cache: `npm cache clean --force`
4. Reinstall: `npm install`

## CI/CD Integration

For GitHub Actions, add a workflow to generate prebuilds:

```yaml
name: Prebuilds

on:
  release:
    types: [published]

jobs:
  build:
    strategy:
      matrix:
        os: [ubuntu-latest, windows-latest, macos-13, macos-14]
    runs-on: ${{ matrix.os }}

    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-node@v4
        with:
          node-version: 20

      - run: npm install
      - run: npm run prebuildify

      - uses: actions/upload-artifact@v4
        with:
          name: prebuilds-${{ matrix.os }}
          path: prebuilds/
```

## Platform-Specific Notes

### Windows

- Uses MSVC compiler
- AVX2 optimizations enabled
- Requires Visual Studio Build Tools (for ARM compilation only)

### macOS

- Uses Clang compiler
- Apple Silicon (M1/M2/M3): Compiles with ARMv8-a+fp+simd
- Intel (x64): Uses AVX/AVX2 optimizations

### Linux

- Uses GCC compiler
- SIMD optimizations (SSE3, AVX, AVX2 for x64)
- NEON optimizations (for ARM)

### ARM Devices

- **Raspberry Pi**: arm/arm64, compiles with NEON
- **Apple Silicon**: arm64, optimized for Apple M-series
- **Android**: arm/arm64, uses NEON
- **AWS Graviton**: arm64, ARMv8-a baseline

## Performance Notes

**Why compile ARM locally?**

ARM CPUs have diverse capabilities:

- Raspberry Pi 4: ARMv8-a baseline
- Apple M2: ARMv8.5-a+ with advanced SIMD
- AWS Graviton3: ARMv8.2-a with crypto extensions

Compiling locally allows:

1. CPU-specific optimizations (e.g., `-march=native`)
2. Better instruction scheduling
3. Optimal cache usage
4. Hardware-specific features

**x64 prebuilds are safe** because:

- More homogeneous architecture
- AVX2 widely supported (2013+)
- Predictable performance characteristics
