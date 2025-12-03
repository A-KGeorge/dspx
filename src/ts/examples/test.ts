import { createDspPipeline } from "../index.js";

const pipeline = createDspPipeline().Rms({
  mode: "moving",
  windowDuration: 2, // 2 ms window
});

const input = new Float32Array([1, 2, 3, 4, 5]);

const timestamps = new Float32Array([
  0.0, // sample 0
  1.1, // sample 1 arrives a bit late
  2.9, // bigger gap
  3.0, // almost same as prev → creates duplicate-like scenario
  6.7, // long delay
]);

const output = await pipeline.process(input, timestamps, {
  channels: 1,
  // sampleRate: 44100,
});

console.log(
  "Output: ",
  Array.from(output).map((i) => i.toFixed(4))
);
