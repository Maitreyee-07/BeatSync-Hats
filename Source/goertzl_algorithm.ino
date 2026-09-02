#include <Arduino.h>
#include <math.h>

#define MIC_PIN A1

#define N   128          // samples per frame
#define FS  8000         // sampling frequency (Hz)

int samples[N];

// ---------- Goertzel Algorithm ----------
float goertzel(int *data, float targetFreq) {
  float s_prev = 0.0;
  float s_prev2 = 0.0;

  float k = round((N * targetFreq) / FS);          // nearest DFT bin
  float omega = (2.0 * PI * k) / N;
  float coeff = 2.0 * cos(omega);

  for (int i = 0; i < N; i++) {
    float s = data[i] + coeff * s_prev - s_prev2;
    s_prev2 = s_prev;
    s_prev  = s;
  }

  return (s_prev2 * s_prev2 +
          s_prev  * s_prev  -
          coeff * s_prev * s_prev2);
}

// ---------- Audio Sampling ----------
void captureSamples() {
  unsigned long period = 1000000UL / FS;

  for (int i = 0; i < N; i++) {
    unsigned long t0 = micros();
    samples[i] = analogRead(MIC_PIN);
    while (micros() - t0 < period);
  }
}

// ---------- DC Offset Removal ----------
void removeDC() {
  long sum = 0;
  for (int i = 0; i < N; i++) sum += samples[i];
  int mean = sum / N;

  for (int i = 0; i < N; i++) samples[i] -= mean;
}

// ---------- Hamming Window ----------
void applyWindow() {
  for (int i = 0; i < N; i++) {
    float w = 0.54 - 0.46 * cos(2 * PI * i / (N - 1));
    samples[i] = samples[i] * w;
  }
}

void setup() {
  Serial.begin(9600);
}

void loop() {

  // 1. Capture audio
  captureSamples();

  // 2. Remove DC bias
  removeDC();

  // 3. Apply window to reduce leakage
  applyWindow();

  // 4. Goertzel frequency bands (VALID for FS=8kHz)
  float bass   = goertzel(samples, 125);   // low bass
  float mid    = goertzel(samples, 1000);  // speech / music mid
  float treble = goertzel(samples, 3000);  // high freq (below Nyquist)

  // 5. Output
  Serial.print("Bass: ");
  Serial.print(bass, 1);
  Serial.print(" | Mid: ");
  Serial.print(mid, 1);
  Serial.print(" | Treble: ");
  Serial.println(treble, 1);

  delay(40);  // ~25 FPS
}