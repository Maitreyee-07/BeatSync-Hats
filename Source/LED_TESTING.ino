#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

// ── Hardware ──────────────────────────────────────────────────────────────────
#define BASS_PIN    A0
#define TREBLE_PIN  A1
#define LED_PIN     6
#define LED_COUNT   100

// ── Sampling ──────────────────────────────────────────────────────────────────
#define N_SAMPLES  64
#define SAMPLE_US  250

// ── Beat detection ────────────────────────────────────────────────────────────
#define LOCAL_AVG_LEN    20
#define BEAT_RATIO       1.25f   // FIXED: was 1.4f — lower = fires on quieter beats
#define MIN_BEAT_MS      300
#define SILENCE_THRESH   2.0f    // FIXED: was 10.0f — much lower for sensitive mics
#define SILENCE_FRAMES   30
#define BEAT_TIMEOUT_MS  3000

// ── BPM history ───────────────────────────────────────────────────────────────
#define BPM_HIST  6

// ── Pattern switching ─────────────────────────────────────────────────────────
#define CHASE_TREBLE_THRESH  0.30f   // FIXED: was 0.35f — easier to trigger chase
#define BPM_MIN              40.0f
#define BPM_MAX              240.0f

// ── Pattern 0: Beat Flash ─────────────────────────────────────────────────────
#define FLASH_HOLD_MS  80
#define FLASH_FADE_MS  200

// ── Pattern 2: Chase ──────────────────────────────────────────────────────────
#define CHASE_WINDOW    5
#define CHASE_BASE_MS  40

// ── Envelope decay / attack ───────────────────────────────────────────────────
// FIXED: was bassMax * 0.995 + bassRms * 0.005 — attack (0.005) was far too slow.
// Raising attack to 0.05 lets the envelope track rising signals 10× faster,
// which means normBass/normTreble actually reach meaningful values instead of
// sitting near 0 while bassMax drags far above the signal.
#define ENV_DECAY  0.990f   // was 0.995f — slightly faster envelope release
#define ENV_ATTACK 0.050f   // was 0.005f — much faster envelope attack

// ── Beat normalisation gate ───────────────────────────────────────────────────
// FIXED: was 0.15f — on a weak mic normBass rarely exceeds this. Lowered to 0.08f.
#define BEAT_NORM_GATE 0.08f

// ─────────────────────────────────────────────────────────────────────────────
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

// ── Rolling average buffer (bass channel) ────────────────────────────────────
float   localBuf[LOCAL_AVG_LEN];
uint8_t localIdx  = 0;
float   localSum  = 0.0f;
bool    localFull = false;

float updateLocalAvg(float rms) {
  localSum -= localBuf[localIdx];
  localBuf[localIdx] = rms;
  localSum += rms;
  localIdx++;
  if (localIdx >= LOCAL_AVG_LEN) { localIdx = 0; localFull = true; }
  uint8_t cnt = localFull ? LOCAL_AVG_LEN : (localIdx == 0 ? 1 : localIdx);
  return localSum / cnt;
}

// ── BPM tracker ───────────────────────────────────────────────────────────────
unsigned long beatIntervals[BPM_HIST];
uint8_t       beatHistIdx = 0;
bool          bpmReady    = false;
float         currentBPM  = 0.0f;
unsigned long lastBeatMs  = 0;

void resetBpmState() {
  bpmReady    = false;
  currentBPM  = 0.0f;
  lastBeatMs  = 0;
  beatHistIdx = 0;
  for (uint8_t i = 0; i < BPM_HIST; i++) beatIntervals[i] = 0;
}

void recordBeat(unsigned long now) {
  if (lastBeatMs > 0) {
    unsigned long interval = now - lastBeatMs;
    beatIntervals[beatHistIdx] = interval;
    beatHistIdx = (beatHistIdx + 1) % BPM_HIST;
    if (beatHistIdx == 0) bpmReady = true;

    uint8_t cnt = bpmReady ? BPM_HIST : beatHistIdx;
    if (cnt > 0) {
      unsigned long tot = 0;
      for (uint8_t i = 0; i < cnt; i++) tot += beatIntervals[i];
      currentBPM = 60000.0f / ((float)tot / (float)cnt);
    }
  }
  lastBeatMs = now;
}

// ── Signal globals ────────────────────────────────────────────────────────────
// FIXED: was 10.0f — starting envelope at 10 means the first ~100 frames of
// normBass/normTreble are suppressed below 0.1 even if the mic is loud.
// Starting at 1.0f lets the envelope find the true peak immediately.
float   bassMax      = 1.0f;   // was 10.0f
float   trebleMax    = 1.0f;   // was 10.0f
float   prevBassRms  = 0.0f;
uint8_t silenceCount = 0;

// ── RMS sampler ───────────────────────────────────────────────────────────────
float sampleRMS(uint8_t pin) {
  int  readings[N_SAMPLES];
  long mean = 0;
  for (uint8_t i = 0; i < N_SAMPLES; i++) {
    readings[i] = analogRead(pin);
    delayMicroseconds(SAMPLE_US);
  }
  for (uint8_t i = 0; i < N_SAMPLES; i++) mean += readings[i];
  mean /= N_SAMPLES;
  long sum = 0;
  for (uint8_t i = 0; i < N_SAMPLES; i++) {
    long d = readings[i] - mean;
    sum += d * d;
  }
  return sqrt((float)sum / N_SAMPLES);
}

// ── Pattern 0 state ───────────────────────────────────────────────────────────
unsigned long flashStartMs = 0;
bool          flashActive  = false;

// ── Pattern 2 state ───────────────────────────────────────────────────────────
uint8_t chaseHead   = 0;
int8_t  chaseDir    = 1;
unsigned long lastChaseMs = 0;

uint16_t lcgState = 42;
uint8_t lcgNext() {
  lcgState = lcgState * 25173u + 13849u;
  return (uint8_t)(lcgState >> 8);
}

// ─────────────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  for (uint8_t i = 0; i < LOCAL_AVG_LEN; i++) localBuf[i] = 0.0f;
  resetBpmState();

  strip.begin();
  strip.show();

  // Prime rolling average — now uses ENV_ATTACK/DECAY so priming is consistent
  for (uint8_t i = 0; i < LOCAL_AVG_LEN; i++) {
    float r = sampleRMS(BASS_PIN);
    updateLocalAvg(r);
    // FIXED: use same envelope formula as loop() so priming doesn't diverge
    bassMax = bassMax * ENV_DECAY + r * ENV_ATTACK;
    if (r > bassMax) bassMax = r;
  }
}

// ─────────────────────────────────────────────────────────────────────────────
void loop() {
  unsigned long now = millis();

  // ── 1. Sample both channels ───────────────────────────────────────────────
  float bassRms   = sampleRMS(BASS_PIN);
  float trebleRms = sampleRMS(TREBLE_PIN);

  // ── 2. Adaptive peak envelope ─────────────────────────────────────────────
  // FIXED: ENV_ATTACK raised from 0.005 → 0.05 so the envelope rises quickly
  // when signal increases, preventing normBass from being crushed near zero.
  bassMax   = bassMax   * ENV_DECAY + bassRms   * ENV_ATTACK;
  trebleMax = trebleMax * ENV_DECAY + trebleRms * ENV_ATTACK;
  if (bassRms   > bassMax)   bassMax   = bassRms;
  if (trebleRms > trebleMax) trebleMax = trebleRms;

  // FIXED: clamp envelope floor at 1.0 (was implicit via bassMax start of 10).
  // Without a floor, bassMax can decay to near-zero during silence and then
  // normBass will spike to 1.0 on the first tiny noise after silence, causing
  // false beat detections.
  if (bassMax   < 1.0f) bassMax   = 1.0f;
  if (trebleMax < 1.0f) trebleMax = 1.0f;

  float normBass   = bassRms   / bassMax;
  float normTreble = trebleRms / trebleMax;
  float localAvg   = updateLocalAvg(bassRms);

  // ── 3. Silence detection ──────────────────────────────────────────────────
  // FIXED: threshold lowered from 10.0 → 2.0 so quiet-but-present music
  // doesn't get mistaken for silence. Adjust SILENCE_THRESH up if your mic
  // picks up a lot of ambient hiss.
  if (bassRms < SILENCE_THRESH) {
    if (silenceCount < 255) silenceCount++;
  } else {
    silenceCount = 0;
  }
  bool isSilent = (silenceCount > SILENCE_FRAMES);

  if (isSilent) {
    resetBpmState();
    flashActive = false;
  }

  // ── 4. Beat timeout ───────────────────────────────────────────────────────
  if (lastBeatMs > 0 && (now - lastBeatMs) > BEAT_TIMEOUT_MS) {
    resetBpmState();
  }

  // ── 5. Beat detection ─────────────────────────────────────────────────────
  // FIXED: BEAT_NORM_GATE lowered from 0.15 → 0.08, BEAT_RATIO from 1.4 → 1.25
  // Combined, these allow weaker mic signals to still trigger beats reliably.
  bool beat = false;
  if (!isSilent &&
      bassRms  > (localAvg * BEAT_RATIO) &&
      normBass > BEAT_NORM_GATE &&
      bassRms  > prevBassRms &&
      (now - lastBeatMs) > MIN_BEAT_MS) {

    beat         = true;
    flashActive  = true;
    flashStartMs = now;
    recordBeat(now);
  }
  prevBassRms = bassRms;

  // ── 6. Choose active pattern ──────────────────────────────────────────────
  uint8_t activePattern;
  if (!isSilent && normTreble > CHASE_TREBLE_THRESH) {
    activePattern = 2;
  } else if (!isSilent && bpmReady &&
             currentBPM > BPM_MIN && currentBPM < BPM_MAX) {
    activePattern = 1;
  } else {
    activePattern = 0;
  }

  // ── 7. Render ─────────────────────────────────────────────────────────────

  // ─ PATTERN 0: Beat Flash ──────────────────────────────────────────────────
  if (activePattern == 0) {

    uint8_t r = 0, g = 0, b = 0;

    if (flashActive) {
      unsigned long elapsed = now - flashStartMs;
      float alpha;

      if (elapsed < (unsigned long)FLASH_HOLD_MS) {
        alpha = 1.0f;
      } else if (elapsed < (unsigned long)(FLASH_HOLD_MS + FLASH_FADE_MS)) {
        alpha = 1.0f - (float)(elapsed - FLASH_HOLD_MS) / (float)FLASH_FADE_MS;
      } else {
        alpha       = 0.0f;
        flashActive = false;
      }
      r = (uint8_t)(255.0f * alpha);
      g = (uint8_t)(100.0f * alpha);
      b = 0;
    }

    for (uint8_t i = 0; i < LED_COUNT; i++) {
      strip.setPixelColor(i, strip.Color(r, g, b));
    }
  }

  // ─ PATTERN 1: BPM Breathe ─────────────────────────────────────────────────
  else if (activePattern == 1) {

    unsigned long period = (unsigned long)(60000.0f / currentBPM);
    if (period < 2) period = 2;
    unsigned long half  = period / 2;
    if (half < 1) half = 1;
    unsigned long phase = now % period;

    uint8_t bri;
    if (phase < half) {
      bri = (uint8_t)((phase * 255UL) / half);
    } else {
      bri = (uint8_t)(((period - phase) * 255UL) / half);
    }

    uint8_t r = (uint8_t)((30UL * bri) / 255UL);
    uint8_t g = 0;
    uint8_t bv = bri;

    for (uint8_t i = 0; i < LED_COUNT; i++) {
      strip.setPixelColor(i, strip.Color(r, g, bv));
    }
  }

  // ─ PATTERN 2: Chase ───────────────────────────────────────────────────────
  else {

    float divisor = normTreble * 4.0f;
    if (divisor < 1.0f) divisor = 1.0f;
    unsigned long stepMs = (unsigned long)((float)CHASE_BASE_MS / divisor);
    if (stepMs < 10) stepMs = 10;

    if ((now - lastChaseMs) >= stepMs) {
      lastChaseMs = now;

      if (beat) chaseDir = (lcgNext() & 1) ? 1 : -1;

      if (chaseDir > 0) {
        chaseHead = (chaseHead + 1) % LED_COUNT;
      } else {
        chaseHead = (chaseHead == 0) ? (uint8_t)(LED_COUNT - 1)
                                     : (uint8_t)(chaseHead - 1);
      }
    }

    for (uint8_t i = 0; i < LED_COUNT; i++) {
      strip.setPixelColor(i, 0);
    }

    for (uint8_t w = 0; w < CHASE_WINDOW; w++) {
      uint8_t px;
      if (chaseDir > 0) {
        px = (uint8_t)((chaseHead + LED_COUNT - w) % LED_COUNT);
      } else {
        px = (uint8_t)((chaseHead + w) % LED_COUNT);
      }
      uint8_t bri = (uint8_t)(255 - w * (255 / CHASE_WINDOW));
      strip.setPixelColor(px, strip.Color(0, bri, bri / 2));
    }
  }

  // ── 8. Silence override ───────────────────────────────────────────────────
  if (isSilent) {
    for (uint8_t i = 0; i < LED_COUNT; i++) strip.setPixelColor(i, 0);
  }

  strip.show();

  // ── 9. Serial debug (every 500 ms) ───────────────────────────────────────
  static unsigned long lastLog = 0;
  if (now - lastLog > 500) {
    lastLog = now;
    Serial.print("BASS=");    Serial.print(bassRms,    1);
    Serial.print(" TREB=");   Serial.print(trebleRms,  1);
    Serial.print(" nB=");     Serial.print(normBass,   2);
    Serial.print(" nT=");     Serial.print(normTreble, 2);
    Serial.print(" BPM=");    Serial.print(currentBPM, 0);
    Serial.print(" PAT=");    Serial.print(activePattern);
    Serial.print(" beat=");   Serial.print(beat     ? "Y" : "-");
    Serial.print(" silent="); Serial.println(isSilent ? "Y" : "-");
  }
}
