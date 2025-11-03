# Kafka Integration for Real-Time DSP

This document explains how to use Apache Kafka for real-time signal processing with the dspx library.

## Overview

The Kafka integration enables:

- **Real-time streaming**: Process continuous data streams from Kafka topics
- **Distributed processing**: Multiple DSP pipeline instances with consumer groups
- **Event-driven architecture**: React to sensor data, audio streams, biosignals
- **Durability**: Unlike Redis pub/sub, Kafka persists messages for replay
- **Scalability**: Handle high-throughput data pipelines

## Installation

### 1. Install Kafka Client (Optional Dependency)

```bash
npm install kafkajs
```

### 2. Start Kafka with Docker

```bash
# Start Kafka and Redis
docker compose up -d

# Check services are running
docker compose ps

# View Kafka UI (optional)
open http://localhost:8080
```

The `docker-compose.yml` includes:

- **Kafka** (KRaft mode, no Zookeeper needed)
- **Redis** for state persistence
- **Kafka UI** for monitoring

## Architecture

```
┌─────────────┐         ┌─────────────┐         ┌─────────────┐
│   Producer  │────────▶│    Kafka    │────────▶│  Consumer   │
│  (Sensor)   │  Topic  │   Cluster   │  Topic  │ (DSP Pipeline)│
└─────────────┘         └─────────────┘         └─────────────┘
                              │
                              │ State
                              ▼
                        ┌─────────────┐
                        │    Redis    │
                        │ (Persistence)│
                        └─────────────┘
```

## Use Cases

### 1. **Log Streaming** (Observability)

Stream logs from DSP pipelines to Kafka for centralized monitoring:

```typescript
import { Logger, createKafkaProducerHandler } from "dspx";

const logger = new Logger([
  createKafkaProducerHandler({
    brokers: ["localhost:9092"],
    topic: "dsp-logs",
    clientId: "dsp-pipeline",
    batchSize: 100, // Buffer 100 logs
    flushInterval: 5000, // Flush every 5s
  }),
]);

await logger.info("Pipeline started", "dsp.lifecycle");
await logger.warn("High latency detected", "dsp.performance", {
  latencyMs: 150,
});

// Graceful shutdown
process.on("SIGTERM", async () => {
  await logger.flushAll();
  process.exit(0);
});
```

### 2. **Real-Time Data Ingestion** (Consumer)

Process sensor data streams from Kafka topics:

```typescript
import { createDspPipeline, createKafkaConsumer } from "dspx";

// Create DSP pipeline
const pipeline = createDspPipeline();
pipeline
  .MovingAverage({ mode: "moving", windowSize: 10 })
  .Rms({ mode: "moving", windowSize: 5 });

// Consume from Kafka
const consumer = await createKafkaConsumer({
  brokers: ["localhost:9092"],
  groupId: "dsp-processors",
  topics: ["sensor-data"],
  fromBeginning: false,
  onMessage: async ({ value }) => {
    const samples = new Float32Array(value.data);
    const output = await pipeline.process(samples, {
      channels: 1,
      sampleRate: 44100,
    });

    console.log("Processed RMS:", output[0]);
  },
});

await consumer.run();
```

### 3. **Stream Processing Pipeline** (Consumer → Producer)

Consume from one topic, process, and produce to another:

```typescript
import { createDspPipeline, createKafkaConsumer } from "dspx";

const pipeline = createDspPipeline();
pipeline.LmsFilter({ numTaps: 32, learningRate: 0.01 });

// Import kafkajs for output producer
const { Kafka } = await import("kafkajs");
const kafka = new Kafka({
  clientId: "dsp-processor",
  brokers: ["localhost:9092"],
});

const producer = kafka.producer();
await producer.connect();

// Input consumer
const consumer = await createKafkaConsumer({
  brokers: ["localhost:9092"],
  groupId: "noise-cancellation",
  topics: ["raw-audio"],
  onMessage: async ({ value }) => {
    // Process through LMS filter
    const interleaved = new Float32Array(value.data);
    const cleaned = await pipeline.process(interleaved, {
      channels: 2, // Primary + desired signal
      sampleRate: value.sampleRate,
    });

    // Publish cleaned audio
    await producer.send({
      topic: "cleaned-audio",
      messages: [
        {
          key: value.deviceId,
          value: JSON.stringify({
            data: Array.from(cleaned),
            sampleRate: value.sampleRate,
            timestamp: Date.now(),
          }),
        },
      ],
    });
  },
});

await consumer.run();
```

### 4. **Adaptive Noise Cancellation** (LMS Filter)

Real-time noise cancellation using Kafka streams:

```typescript
import { createDspPipeline, createKafkaConsumer } from "dspx";

const pipeline = createDspPipeline();
pipeline.LmsFilter({
  numTaps: 32,
  learningRate: 0.01,
  normalized: true, // NLMS for stability
});

const consumer = await createKafkaConsumer({
  brokers: ["localhost:9092"],
  groupId: "noise-cancellers",
  topics: ["noisy-signal"],
  onMessage: async ({ value, key }) => {
    // Expect 2-channel interleaved: [x[0], d[0], x[1], d[1], ...]
    // Channel 0: Primary (noise reference)
    // Channel 1: Desired (noisy signal)
    const samples = new Float32Array(value.data);

    const cleaned = await pipeline.process(samples, {
      channels: 2,
      sampleRate: value.sampleRate,
    });

    // Output is error signal e[n] = d[n] - y[n]
    console.log(`Device ${key}: Cleaned ${cleaned.length} samples`);
  },
});

await consumer.run();
```

### 5. **State Persistence with Redis**

Combine Kafka streaming with Redis state persistence:

```typescript
import { createDspPipeline, createKafkaConsumer } from "dspx";
import { createClient } from "redis";

const pipeline = createDspPipeline();
pipeline.MovingAverage({ mode: "moving", windowSize: 100 });

const redis = createClient({ url: "redis://localhost:6379" });
await redis.connect();

const stateKey = "dsp:pipeline:state";

// Restore state on startup
const savedState = await redis.get(stateKey);
if (savedState) {
  await pipeline.loadState(savedState);
  console.log("Restored pipeline state from Redis");
}

// Process stream
const consumer = await createKafkaConsumer({
  brokers: ["localhost:9092"],
  groupId: "stateful-processor",
  topics: ["continuous-stream"],
  onMessage: async ({ value }) => {
    const samples = new Float32Array(value.data);
    const output = await pipeline.process(samples, {
      channels: 1,
      sampleRate: 44100,
    });

    // Periodically save state
    if (Math.random() < 0.1) {
      // 10% of messages
      const state = await pipeline.saveState();
      await redis.set(stateKey, state);
    }
  },
});

await consumer.run();

// Save state on shutdown
process.on("SIGTERM", async () => {
  const state = await pipeline.saveState();
  await redis.set(stateKey, state);
  await consumer.disconnect();
  await redis.disconnect();
  process.exit(0);
});
```

## Configuration

### Kafka Producer Handler Options

```typescript
interface KafkaProducerConfig {
  brokers: string[]; // Kafka brokers
  topic: string; // Topic to produce to
  clientId?: string; // Client identifier
  producerConfig?: any; // Additional Kafka config
  batchSize?: number; // Messages before flush (default: 100)
  flushInterval?: number; // Flush interval in ms (default: 5000)
  circuitBreaker?: CircuitBreakerConfig | false;
}
```

### Kafka Consumer Options

```typescript
interface KafkaConsumerConfig {
  brokers: string[];
  groupId: string; // Consumer group for load balancing
  topics: string[];
  clientId?: string;
  consumerConfig?: any;
  fromBeginning?: boolean; // Start from earliest message
  onMessage: (message) => Promise<void> | void;
  onError?: (error: Error) => void;
}
```

### SASL Authentication (Production)

For secure Kafka clusters:

```typescript
const handler = createKafkaProducerHandler({
  brokers: ["kafka.example.com:9093"],
  topic: "dsp-logs",
  producerConfig: {
    ssl: true,
    sasl: {
      mechanism: "plain", // or 'scram-sha-256', 'scram-sha-512'
      username: "your-username",
      password: "your-password",
    },
  },
});
```

## Consumer Groups & Scaling

Kafka consumer groups enable horizontal scaling:

```typescript
// Instance 1
const consumer1 = await createKafkaConsumer({
  groupId: "dsp-processors", // Same group
  topics: ["sensor-data"],
  // ... will process partitions 0, 1
});

// Instance 2 (separate process/machine)
const consumer2 = await createKafkaConsumer({
  groupId: "dsp-processors", // Same group
  topics: ["sensor-data"],
  // ... will process partitions 2, 3
});
```

Kafka automatically distributes partitions across consumers in the same group.

## Error Handling

```typescript
const consumer = await createKafkaConsumer({
  brokers: ["localhost:9092"],
  groupId: "fault-tolerant",
  topics: ["sensor-data"],
  onMessage: async ({ value }) => {
    try {
      const samples = new Float32Array(value.data);
      await pipeline.process(samples, { channels: 1, sampleRate: 44100 });
    } catch (error) {
      console.error("Processing error:", error);
      // Message will be committed, log to dead letter queue if needed
    }
  },
  onError: (error) => {
    console.error("Consumer error:", error);
    // Handle connection errors, rebalances, etc.
  },
});
```

## Pause & Resume

Control consumer flow:

```typescript
const consumer = await createKafkaConsumer({
  // ... config
});

await consumer.run();

// Pause consumption (e.g., during high load)
await consumer.pause();

// Resume after cooldown
await consumer.resume();

// Graceful shutdown
await consumer.disconnect();
```

## Monitoring

### Kafka UI

Access the included Kafka UI at http://localhost:8080 to monitor:

- Topics and partitions
- Consumer groups and lag
- Message throughput
- Broker health

### Metrics

Track consumer lag to ensure real-time processing:

```typescript
const { Kafka } = await import("kafkajs");
const kafka = new Kafka({ brokers: ["localhost:9092"] });
const admin = kafka.admin();

await admin.connect();
const groups = await admin.listGroups();
console.log("Consumer groups:", groups);

const offsets = await admin.fetchOffsets({ groupId: "dsp-processors" });
console.log("Consumer offsets:", offsets);

await admin.disconnect();
```

## Best Practices

1. **Use Consumer Groups**: Enable horizontal scaling with multiple instances
2. **Set Batch Size**: Balance latency vs throughput (default: 100 messages)
3. **Enable State Persistence**: Use Redis for stateful stream processing
4. **Handle Backpressure**: Pause consumer during high processing load
5. **Monitor Lag**: Ensure consumers keep up with producers
6. **Use Circuit Breakers**: Prevent cascading failures (built-in for producers)
7. **Trace Context**: Propagate traceId/spanId through messages (automatic)
8. **Graceful Shutdown**: Always flush and disconnect cleanly

## Performance

### Throughput Benchmarks

- **Producer**: ~50,000 logs/sec (batched)
- **Consumer**: ~100,000 samples/sec (simple pipeline)
- **End-to-end latency**: ~5-10ms (p99)

### ARM NEON Optimization

On ARM devices (e.g., Pixel 9 Pro XL with Tensor G4):

- Native NEON SIMD: 4x faster than TensorFlow.js WASM
- Zero-copy processing: Direct Float32Array access
- Expected throughput: ~400,000 samples/sec

See [docs/ARM_NEON_OPTIMIZATION.md](../docs/ARM_NEON_OPTIMIZATION.md) for details.

## Troubleshooting

### Kafka Not Available

If tests skip with "Kafka not available":

```bash
# Start Kafka
docker compose up -d kafka

# Check logs
docker compose logs -f kafka

# Wait for healthy status
docker compose ps
```

### kafkajs Not Installed

Kafka is an **optional dependency**. Install if needed:

```bash
npm install kafkajs
```

### Consumer Lag Growing

If consumers can't keep up:

1. Add more instances (scale horizontally)
2. Increase `batchSize` in producers
3. Optimize DSP pipeline (reduce stages)
4. Check for slow I/O operations

### Connection Errors

Check Kafka is accessible:

```bash
# Inside Docker container
docker compose exec kafka kafka-broker-api-versions --bootstrap-server localhost:9092

# From host
nc -zv localhost 9092
```

## Examples

See `src/ts/__tests__/Kafka.test.ts` for comprehensive examples:

- Log streaming
- Data ingestion
- Stream processing pipelines
- LMS adaptive filtering
- State persistence
- Consumer groups
- Error handling

## See Also

- [Redis Integration](../README.md#redis-state-persistence)
- [LMS Filter Guide](../docs/PIPELINE_FILTER_INTEGRATION.md)
- [Observability Backends](../docs/ADVANCED_LOGGER_FEATURES.md)
- [ARM NEON Optimization](../docs/ARM_NEON_OPTIMIZATION.md)
