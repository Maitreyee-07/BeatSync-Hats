#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

#define MIC_PIN A0
#define LED_PIN 6
#define LED_COUNT 100

#define N_SAMPLES 64
#define SAMPLE_US 250

#define LOCAL_AVG_LEN 20
#define BEAT_RATIO 1.4f
#define MIN_BEAT_MS 300

#define BPM_HIST 6

#define SILENCE_RMS_THRESH 10.0f   // FIX 1: raw RMS threshold, tune to your mic floor
#define BEAT_TIMEOUT_MS    3000    // FIX 3: stop blinking if no beat for this long

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

float localBuf[LOCAL_AVG_LEN];
uint8_t localIdx = 0;
float localSum = 0;
bool localFull = false;

unsigned long beatTimes[BPM_HIST];
uint8_t beatIdx = 0;
bool bpmReady = false;

unsigned long lastBeatMs = 0;
float currentBPM = 0;

float rmsMax = 10.0f;
float prevRms = 0;

float silenceCounter = 0;

float sampleRMS() {
  long sum = 0;
  int readings[N_SAMPLES];

  for (uint8_t i = 0; i < N_SAMPLES; i++) {
    readings[i] = analogRead(MIC_PIN);
    delayMicroseconds(SAMPLE_US);
  }

  long mean = 0;
  for (uint8_t i = 0; i < N_SAMPLES; i++) mean += readings[i];
  mean /= N_SAMPLES;

  for (uint8_t i = 0; i < N_SAMPLES; i++) {
    long d = readings[i] - mean;
    sum += d * d;
  }

  return sqrt((float)sum / N_SAMPLES);
}

float updateLocalAvg(float rms) {
  localSum -= localBuf[localIdx];
  localBuf[localIdx] = rms;
  localSum += rms;

  localIdx++;
  if (localIdx >= LOCAL_AVG_LEN) {
    localIdx = 0;
    localFull = true;
  }

  uint8_t cnt = localFull ? LOCAL_AVG_LEN : localIdx;
  if (cnt == 0) cnt = 1;

  return localSum / cnt;
}

void recordBeat(unsigned long now) {
  if (lastBeatMs > 0) {
    unsigned long interval = now - lastBeatMs;
    beatTimes[beatIdx] = interval;

    beatIdx = (beatIdx + 1) % BPM_HIST;
    if (beatIdx == 0) bpmReady = true;

    uint8_t cnt = bpmReady ? BPM_HIST : beatIdx;

    if (cnt > 0) {
      unsigned long tot = 0;
      for (uint8_t i = 0; i < cnt; i++) tot += beatTimes[i];
      currentBPM = 60000.0f / (float)(tot / cnt);
    }
  }
  lastBeatMs = now;
}

void setup() {
  Serial.begin(115200);
  strip.begin();
  strip.show();

  for (uint8_t i = 0; i < LOCAL_AVG_LEN; i++) {
    float r = sampleRMS();
    updateLocalAvg(r);
    if (r > rmsMax) rmsMax = r;
  }
}

void loop() {
  float rms = sampleRMS();

  rmsMax = rmsMax * 0.995f + rms * 0.005f;
  if (rms > rmsMax) rmsMax = rms;

  float normRms = rms / max(rmsMax, 1.0f);
  float localAvg = updateLocalAvg(rms);

  // FIX 1: use raw RMS for silence detection instead of normRms.
  // normRms stays falsely high after music stops because rmsMax decays slowly,
  // which was preventing silenceCounter from ever reaching 30.
  if (rms < SILENCE_RMS_THRESH)
    silenceCounter += 1;
  else
    silenceCounter = 0;

  bool isSilent = (silenceCounter > 30);

  unsigned long now = millis();

  // FIX 3: beat timeout — if no beat arrives for BEAT_TIMEOUT_MS, kill BPM state.
  // Guards against the blink pattern persisting when silence detection is slow.
  if (lastBeatMs > 0 && (now - lastBeatMs) > BEAT_TIMEOUT_MS) {
    bpmReady   = false;
    currentBPM = 0;
    lastBeatMs = 0;
  }

  bool beat = false;

  if (rms > (localAvg * BEAT_RATIO) &&
      normRms > 0.15f &&
      rms > prevRms &&
      (now - lastBeatMs) > MIN_BEAT_MS) {

    beat = true;
    recordBeat(now);
  }

  prevRms = rms;

  static unsigned long ledOnTime = 0;
  static bool ledState = false;

  if (beat) {
    ledState = true;
    ledOnTime = now;
  }

  if (ledState && (now - ledOnTime > 80)) {
    ledState = false;
  }

  if (isSilent) {
    ledState   = false;
    bpmReady   = false;
    currentBPM = 0;
    lastBeatMs = 0;   // FIX 2: clear stale timestamp so the next beat after silence
  }                   // doesn't compute a huge bogus interval and corrupt BPM

  if (!ledState &&
      bpmReady &&
      !isSilent &&
      currentBPM > 40 && currentBPM < 240) {

    unsigned long halfPeriod = (unsigned long)(30000.0f / currentBPM);
    unsigned long elapsed = now - lastBeatMs;
    ledState = (elapsed % (halfPeriod * 2)) < halfPeriod;
  }

  if (ledState) {
    for (int i = 0; i < LED_COUNT; i++) {
      strip.setPixelColor(i, strip.Color(0, 150, 255));
    }
  } else {
    for (int i = 0; i < LED_COUNT; i++) {
      strip.setPixelColor(i, 0);
    }
  }

  strip.show();

  static unsigned long lastLog = 0;
  if (now - lastLog > 500) {
    lastLog = now;
    Serial.print("RMS="); Serial.print(rms, 1);
    Serial.print(" norm="); Serial.print(normRms, 2);
    Serial.print(" avg="); Serial.print(localAvg, 1);
    Serial.print(" BPM="); Serial.print(currentBPM, 0);
    Serial.print(" beat="); Serial.println(beat ? "YES" : "-");
  }
}
