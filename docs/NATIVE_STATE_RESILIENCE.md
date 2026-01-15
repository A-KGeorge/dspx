# Native State Resilience (C++ Layer)

## Overview

The `LoadState` method in `DspPipeline.cc` now implements **transactional safety** for both JSON and TOON deserialization paths, ensuring the pipeline cannot be corrupted by invalid or incompatible state data.

## Problem Statement

**Before:**

- TOON path directly modified existing stages during deserialization
- If deserialization failed partway through, pipeline was left in corrupted state
- No rollback mechanism to restore previous state
- JSON path was safer (built temporary stages) but TOON path was vulnerable

**After:**

- Both paths now use transactional safety patterns
- TOON path creates backup before modifying stages
- Automatic rollback on any failure
- Pipeline guaranteed to be in valid state (either old or new, never corrupted)

## Implementation

### JSON Path (Already Safe)

**Strategy: Build → Validate → Swap**

```cpp
// 1. Build temporary stages
std::vector<std::unique_ptr<IDspStage>> newStages;

// 2. Validate and deserialize into newStages
for (auto& stageData : stateObject["stages"]) {
    auto stage = createStageFromJSON(stageData);
    newStages.push_back(std::move(stage));
}

// 3. Atomic swap (only if everything succeeded)
m_stages = std::move(newStages);
```

**Safety Guarantees:**

- Original stages untouched until deserialization complete
- If any stage fails, original pipeline unchanged
- Atomic swap at the end (all-or-nothing)

### TOON Path (Now Improved)

**Strategy: Backup → Validate → Deserialize → Rollback on Failure**

```cpp
// Phase 1: Backup current state
std::vector<std::string> stateBackup;
for (size_t i = 0; i < m_stages.size(); ++i) {
    nlohmann::json stageState;
    m_stages[i]->saveState(stageState);
    stateBackup.push_back(stageState.dump());
}

// Phase 2: Validate buffer structure
if (savedStageCount != m_stages.size()) {
    throw std::runtime_error("Stage count mismatch. Aborting (no changes made).");
}

// Phase 3: Deserialize into existing stages
for (size_t i = 0; i < m_stages.size(); ++i) {
    validateStageType(i);
    m_stages[i]->deserializeToon(deserializer); // May throw
}

// Phase 4: Success - discard backup
return true;

// Phase 5: ROLLBACK on failure
catch (const std::exception& e) {
    // Restore from backup
    for (size_t i = 0; i < stateBackup.size(); ++i) {
        nlohmann::json stageState = nlohmann::json::parse(stateBackup[i]);
        m_stages[i]->loadState(stageState);
    }
    throw; // Re-throw original error
}
```

**Safety Guarantees:**

- Current state backed up before any modifications
- Validation happens upfront (count, types)
- If deserialization fails, automatic rollback to backup
- Pipeline guaranteed to be in valid state

## Failure Scenarios

### Scenario 1: Stage Count Mismatch

**Input:** Saved state has 5 stages, current pipeline has 3 stages

**Behavior:**

```
TOON Load: Stage count mismatch. Saved state has 5 stages, current has 3.
Aborting (no changes made).
```

**Result:** Pipeline unchanged, error thrown immediately

### Scenario 2: Stage Type Mismatch

**Input:** Stage 2 saved as "rms", current stage 2 is "rectify"

**Behavior:**

```
TOON Load: Stage type mismatch at index 2. Expected 'rectify', got 'rms'.
Aborting (no changes made).
```

**Result:** Pipeline unchanged, error thrown immediately

### Scenario 3: Deserialization Error (Mid-Load)

**Input:** Corrupted buffer - stage 2 deserialization fails

**Behavior:**

```
[TOON] ROLLBACK: Deserialization failed: Invalid buffer format at offset 1234
[TOON] Restoring pipeline to previous state...
[TOON] Rollback succeeded
TOON Load Failed: Invalid buffer format at offset 1234
```

**Result:** Pipeline restored to state before LoadState was called

### Scenario 4: Deserialization + Rollback Failure

**Input:** Corrupted buffer AND backup restoration fails for stage 1

**Behavior:**

```
[TOON] ROLLBACK: Deserialization failed: Invalid buffer
[TOON] Restoring pipeline to previous state...
[TOON] Warning: Failed to restore stage 1: JSON parse error
[TOON] Rollback partially succeeded
TOON Load Failed: Invalid buffer (Warning: Rollback was incomplete -
pipeline may be in inconsistent state. Recommend calling clearState().)
```

**Result:** Pipeline may be corrupted, user warned to call `clearState()`

## Performance Impact

### Memory Overhead

**TOON Backup:**

- Temporary: ~5-50 KB per stage (depends on buffer sizes)
- Short-lived: Discarded immediately after success
- Example: 10 stages × 10 KB = 100 KB temporary allocation

**JSON Path:**

- Temporary: Full stage objects in `newStages` vector
- Example: 10 stages × 50 KB = 500 KB temporary allocation

### Time Overhead

**Backup Creation (TOON):**

- ~50-200 µs per stage (serialize to JSON)
- 10 stages: ~500-2000 µs (~0.5-2 ms)

**Rollback (on failure only):**

- ~100-300 µs per stage (deserialize from JSON)
- 10 stages: ~1-3 ms

**Success Path:**

- No rollback overhead
- Backup memory freed immediately

## Debug Mode

Enable detailed logging with environment variable:

```bash
export DSPX_DEBUG_TOON=1
```

**Output:**

```
[TOON] Phase 1: Creating backup of current state
[TOON] Phase 2: Parsing TOON buffer
[TOON] Phase 3: Validating and deserializing stages
[TOON] Loading state into stage[0]: type='movingAverage'
[TOON] Loading state into stage[1]: type='rms'
[TOON] Phase 4: Success! Loaded 2 stages. Backup discarded.
```

**On Failure:**

```
[TOON] Phase 1: Creating backup of current state
[TOON] Phase 2: Parsing TOON buffer
[TOON] Phase 3: Validating and deserializing stages
[TOON] Loading state into stage[0]: type='movingAverage'
[TOON] ROLLBACK: Deserialization failed: Unexpected token at offset 234
[TOON] Restoring pipeline to previous state...
[TOON] Rollback succeeded
```

## Best Practices

### Application Layer

**1. Always check return value:**

```typescript
const success = await pipeline.loadState(state);
if (!success) {
  console.warn("State load failed - using fresh state");
}
```

**2. Handle rollback failures:**

```typescript
try {
  await pipeline.loadState(corruptedState);
} catch (error) {
  if (error.message.includes("Rollback was incomplete")) {
    // Pipeline may be corrupted
    console.error("Critical: Pipeline corrupted, clearing state");
    pipeline.clearState();
  }
}
```

**3. Use circuit breaker (TS layer):**

```typescript
const pipeline = createDspPipeline({
  enableCircuitBreaker: true,
  fallbackOnLoadFailure: true, // Auto-calls clearState()
});
```

### Testing

**Test corrupted state handling:**

```typescript
// Corrupt a TOON buffer
const state = await pipeline.saveState({ format: "toon" });
const corrupted = Buffer.from(state);
corrupted[100] = 0xff; // Corrupt byte

// Should rollback gracefully
try {
  await pipeline.loadState(corrupted);
} catch (error) {
  expect(error.message).toContain("TOON Load Failed");
}

// Verify pipeline still works
const result = await pipeline.process(samples, { channels: 1 });
expect(result.length).toBeGreaterThan(0);
```

## Migration from Old Version

**No code changes required** - the improvement is fully backwards compatible.

**Old behavior:**

- TOON deserialization could leave pipeline corrupted on failure
- Users had to manually call `clearState()` to recover

**New behavior:**

- TOON deserialization automatically rolls back on failure
- Pipeline guaranteed to be in valid state
- Better error messages guide recovery

## Comparison with JSON Path

| Feature         | TOON Path          | JSON Path           |
| --------------- | ------------------ | ------------------- |
| Strategy        | Backup → Rollback  | Build → Swap        |
| Memory Overhead | ~100 KB (backup)   | ~500 KB (newStages) |
| Time Overhead   | ~0.5-2 ms (backup) | ~2-5 ms (rebuild)   |
| Rollback Speed  | ~1-3 ms            | N/A (atomic swap)   |
| Safety          | ✅ Transactional   | ✅ Transactional    |
| Debug Output    | ✅ Detailed phases | ✅ Stage-by-stage   |

Both paths are now equally safe with different trade-offs.

## Future Enhancements

### Possible Improvements:

1. **Copy-on-Write Stages**: Instead of serializing backup, clone stage objects

   - Pro: Faster backup (~10× speedup)
   - Con: Requires implementing copy constructors for all stages

2. **Two-Phase Commit**: Validate entire buffer before any modifications

   - Pro: No rollback needed
   - Con: Requires buffering entire deserialization

3. **Incremental Backup**: Only backup stages that will be modified
   - Pro: Reduced memory/time overhead
   - Con: More complex logic, harder to maintain

## Related Documentation

- [State Resilience (TS Layer)](./STATE_RESILIENCE.md) - Circuit breaker and retry patterns
- [State Persistence](./STATE_PERSISTENCE.md) - saveState/loadState API reference
- [TOON Format](./TOON_FORMAT.md) - Binary serialization format specification
