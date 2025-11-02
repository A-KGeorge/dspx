/**
 * Generates a sine wave.
 * @param freq The frequency of the sine wave in Hz.
 * @param sampleRate The sample rate in Hz.
 * @param duration The duration of the wave in seconds.
 * @returns An array of numbers representing the sine wave.
 */
export function generateSineWave(
  freq: number,
  sampleRate: number,
  numSamples: number
): number[] {
  const wave = new Array(numSamples);
  for (let i = 0; i < numSamples; i++) {
    wave[i] = Math.sin(2 * Math.PI * freq * (i / sampleRate));
  }
  return wave;
}

/**
 * Calculates the Root Mean Square (RMS) of a signal.
 * @param data The input signal (an array of numbers).
 * @returns The RMS value.
 */
export function getRms(data: number[] | Float32Array): number {
  if (data.length === 0) {
    return 0;
  }
  let sumOfSquares = 0;
  for (let i = 0; i < data.length; i++) {
    sumOfSquares += data[i] * data[i];
  }
  return Math.sqrt(sumOfSquares / data.length);
}
