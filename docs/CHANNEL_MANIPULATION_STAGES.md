# Channel Manipulation Stages

This document describes the `ChannelSelect` and `ChannelMerge` pipeline stages for flexible channel routing.

## Overview

Both stages are **resizing stages** (they change the output buffer size) and fully support:

- Chainable API
- State serialization/deserialization
- Pipeline introspection via `listState()`
- Multi-stage pipelines

## ChannelSelect

Selects specific channels by index from a multi-channel input.

### Features

- **Arbitrary Selection**: Select any channels in any order
- **Reordering**: Change channel order (e.g., `[1, 0]` swaps stereo channels)
- **Duplication**: Duplicate channels (e.g., `[0, 0]` creates mono from left channel)
- **Non-contiguous**: Select channels like `[0, 3, 7]` from EEG data

### TypeScript API

```typescript
import { createDspPipeline } from "dspx";

const pipeline = createDspPipeline();

pipeline
  .ChannelSelect({
    channels: [0, 2, 4], // Select channels 0, 2, and 4
    numInputChannels: 8, // Expected input channels (for validation)
  })
  .Rms({ mode: "batch" });

// Process 8-channel EEG data, compute RMS on 3 selected channels
const input = new Float32Array(80); // 10 samples × 8 channels
const result = await pipeline.process(input, {
  channels: 8,
  sampleRate: 250,
});
```

### Use Cases

**1. EEG Channel Selection**

```typescript
// Select frontal and central channels from 64-channel EEG
pipeline.ChannelSelect({
  channels: [0, 1, 2, 16, 17, 18], // Fp1, Fp2, Fz, C3, Cz, C4
  numInputChannels: 64,
});
```

**2. Stereo Channel Swap**

```typescript
// Swap left and right audio channels
pipeline.ChannelSelect({
  channels: [1, 0],
  numInputChannels: 2,
});
```

**3. Mono from Stereo**

```typescript
// Extract left channel only
pipeline.ChannelSelect({
  channels: [0],
  numInputChannels: 2,
});
```

### C++ Implementation

- **Header**: `src/native/adapters/ChannelSelectStage.h`
- **Class**: `ChannelSelectStage`
- **Method**: `processResizing()` - extracts selected channels from interleaved buffer
- **Factory**: Registered as `"channelSelect"` in `DspPipeline.cc`

### Validation

- `channels` array must not be empty
- All channel indices must be in range `[0, numInputChannels-1]`
- Negative indices are rejected
- TypeScript validates before calling native code

---

## ChannelMerge

Maps input channels to output channels via a mapping array.

### Features

- **Channel Duplication**: Duplicate channels to multiple outputs
- **Mono to Stereo**: `mapping: [0, 0]` duplicates channel 0
- **Custom Routing**: Any input→output mapping
- **Channel Expansion**: Expand from N to M channels

### TypeScript API

```typescript
import { createDspPipeline } from "dspx";

const pipeline = createDspPipeline();

pipeline
  .ChannelMerge({
    mapping: [0, 0], // Map channel 0 to both outputs
    numInputChannels: 1, // Expected input channels
  })
  .Rectify({ mode: "full" });

// Convert mono to stereo, then rectify
const input = new Float32Array(10); // 10 mono samples
const result = await pipeline.process(input, {
  channels: 1,
  sampleRate: 1000,
});
```

### Use Cases

**1. Mono to Stereo**

```typescript
// Duplicate mono signal to stereo
pipeline.ChannelMerge({
  mapping: [0, 0],
  numInputChannels: 1,
});
```

**2. Quad Upmix**

```typescript
// Stereo to quad (duplicate L→FL/RL, R→FR/RR)
pipeline.ChannelMerge({
  mapping: [0, 1, 0, 1], // FL, FR, RL, RR
  numInputChannels: 2,
});
```

**3. Channel Routing**

```typescript
// Route channel 2 to 3 outputs, channel 0 to 1 output
pipeline.ChannelMerge({
  mapping: [2, 2, 2, 0],
  numInputChannels: 4,
});
```

### C++ Implementation

- **Header**: `src/native/adapters/ChannelMergeStage.h`
- **Class**: `ChannelMergeStage`
- **Method**: `processResizing()` - maps input channels to output positions
- **Factory**: Registered as `"channelMerge"` in `DspPipeline.cc`

### Mapping Semantics

The `mapping` array is **output-centric**:

- `mapping[0]` = input channel for output channel 0
- `mapping[1]` = input channel for output channel 1
- etc.

**Example**: `mapping: [1, 0, 2]`

- Output channel 0 ← Input channel 1
- Output channel 1 ← Input channel 0
- Output channel 2 ← Input channel 2

### Validation

- `mapping` array must not be empty
- All mapping indices must be in range `[0, numInputChannels-1]`
- Negative indices are rejected
- TypeScript validates before calling native code

---

## Combined Usage

You can chain both stages for complex routing:

```typescript
// Select channels 0, 1, 2 from 8-channel input, then duplicate to 6 channels
pipeline
  .ChannelSelect({
    channels: [0, 1, 2],
    numInputChannels: 8,
  })
  .ChannelMerge({
    mapping: [0, 1, 2, 0, 1, 2], // Duplicate all 3 channels
    numInputChannels: 3,
  });
```

---

## Performance

Both stages use efficient buffer copy operations:

- Single-pass processing
- No intermediate allocations
- Zero-copy where possible
- SIMD-friendly memory access patterns

---

## Test Coverage

- **ChannelSelect**: 11 tests covering selection, reordering, duplication, validation
- **ChannelMerge**: 12 tests covering merging, duplication, routing, validation
- **Total**: 23 tests, all passing ✅

Test files:

- `src/ts/__tests__/ChannelSelect.test.ts`
- `src/ts/__tests__/ChannelMerge.test.ts`

---

## Implementation Details

### Buffer Layout

Both stages work with **interleaved** multi-channel buffers:

```
Input:  [Ch0_S0, Ch1_S0, Ch2_S0, Ch0_S1, Ch1_S1, Ch2_S1, ...]
         └────── Sample 0 ──────┘ └────── Sample 1 ──────┘

Output: [SelectedCh0_S0, SelectedCh1_S0, SelectedCh0_S1, ...]
```

### State Management

Both stages implement full state serialization:

```typescript
const state = pipeline.saveState();
// ... later
pipeline.restoreState(state);
```

Serialized state includes:

- Stage type (`"channelSelect"` or `"channelMerge"`)
- Channel array or mapping array
- Input channel count

---

## Related Stages

- **`ChannelSelector`**: Legacy stage (not chainable, deprecated in favor of `ChannelSelect`)
- **`Filter`**: Apply frequency-domain filtering to channels
- **`Rms`**: Compute RMS per channel
- **`Beamformer`**: Spatial filtering for sensor arrays

---

## Future Enhancements

Potential improvements:

1. **Channel Mixing**: Weighted sums like `[0.5*ch0 + 0.5*ch1]`
2. **Conditional Routing**: Route based on signal properties
3. **Dynamic Mapping**: Change mapping based on runtime conditions
4. **Matrix Routing**: Full M×N mixing matrices

---

## References

- C++ Headers:
  - `src/native/adapters/ChannelSelectStage.h`
  - `src/native/adapters/ChannelMergeStage.h`
- TypeScript Bindings: `src/ts/bindings.ts` (lines 1419-1534)
- Type Definitions: `src/ts/types.ts` (lines 1003-1031)
- Factory Registration: `src/native/DspPipeline.cc` (lines 669-719)
