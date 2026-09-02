#include <Adafruit_NeoPixel.h>

// ── Hardware ──────────────────────────────────────────────────────────────────
#define BASS_PIN    A0
#define TREBLE_PIN  A1
#define LED_PIN     6
#define LED_COUNT   90

// ── Sampling ──────────────────────────────────────────────────────────────────
#define N_SAMPLES  64
#define SAMPLE_US  16

// ── IIR smoother ──────────────────────────────────────────────────────────────
// FIX: Was 0.945+0.0549=0.9999 and too heavy — killed beat spikes.
// Lighter filter so transients (beats) actually pass through.
#define IIR_A  0.60f
#define IIR_B  0.40f

// ── Calibration ───────────────────────────────────────────────────────────────
#define CAL_FRAMES  150
// FIX: Margin reduced — mic near phone gives weak signal, old value (2.5)
// pushed noise floor too high and gated out real audio.
#define CAL_MARGIN  1.6f

// ── Peak tracker ──────────────────────────────────────────────────────────────
// FIX: Faster decay so peak doesn't get stuck high, keeping normBass near 0.
#define PEAK_DECAY  0.990f

// ── Beat detection ────────────────────────────────────────────────────────────
// FIX: Ratio lowered — mic signal isn't loud enough to hit 1.40x reliably.
#define BEAT_RATIO     1.20f
// FIX: Threshold lowered to match realistic mic levels.
#define BEAT_MIN_NORM  0.08f
#define ROLLING_LEN    20
#define MIN_BEAT_MS    280

// ── Pattern switching ─────────────────────────────────────────────────────────
#define BEATS_PER_TOGGLE  8

// ── Silence gate ──────────────────────────────────────────────────────────────
#define SILENCE_FRAMES  35

// ── Brightness ───────────────────────────────────────────────────────────────
#define BRIGHTNESS_MIN  40

// ── Chase ─────────────────────────────────────────────────────────────────────
#define CHASE_WINDOW    5
#define CHASE_SLOW_MS   70
#define CHASE_FAST_MS   12

// ── Fill curve ────────────────────────────────────────────────────────────────
#define FILL_POWER  1.5f

// ─────────────────────────────────────────────────────────────────────────────
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

float noiseFloorBass   = 0.0f;
float noiseFloorTreble = 0.0f;

float filteredBass   = 0.0f;
float filteredTreble = 0.0f;

float bassPeak   = 0.001f;
float treblePeak = 0.001f;

float   rollingBuf[ROLLING_LEN];
uint8_t rollingIdx  = 0;
float   rollingSum  = 0.0f;
bool    rollingFull = false;

unsigned long lastBeatMs    = 0;
uint8_t       beatCount     = 0;
uint8_t       activePattern = 0;

uint8_t silenceFrames = 0;

uint8_t       chaseHead   = 0;
int8_t        chaseDir    = 1;
unsigned long lastChaseMs = 0;

// ─────────────────────────────────────────────────────────────────────────────
float measureACRms(uint8_t pin) {
  long readings[N_SAMPLES];
  long mean = 0;
  for (uint8_t i = 0; i < N_SAMPLES; i++) {
    readings[i] = analogRead(pin);
    delayMicroseconds(SAMPLE_US);
  }
  for (uint8_t i = 0; i < N_SAMPLES; i++) mean += readings[i];
  mean /= N_SAMPLES;
  long sumSq = 0;
  for (uint8_t i = 0; i < N_SAMPLES; i++) {
    long d = readings[i] - mean;
    sumSq += d * d;
  }
  return sqrt((float)sumSq / (float)N_SAMPLES) * (5.0f / 1023.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
float updateRolling(float v) {
  rollingSum -= rollingBuf[rollingIdx];
  rollingBuf[rollingIdx] = v;
  rollingSum += v;
  rollingIdx++;
  if (rollingIdx >= ROLLING_LEN) { rollingIdx = 0; rollingFull = true; }
  uint8_t cnt = rollingFull ? ROLLING_LEN : (rollingIdx == 0 ? 1 : rollingIdx);
  return (cnt > 0) ? (rollingSum / cnt) : 0.001f;
}

// ─────────────────────────────────────────────────────────────────────────────
void calibrate() {
  strip.clear();
  strip.show();
  delay(300);

  float sumB = 0.0f, sumT = 0.0f;

  for (uint16_t f = 0; f < CAL_FRAMES; f++) {
    sumB += measureACRms(BASS_PIN);
    sumT += measureACRms(TREBLE_PIN);
    if (f % 10 == 0) {
      uint8_t px = (f / 10) % LED_COUNT;
      strip.setPixelColor(px, strip.Color(0, 60, 0));
      strip.show();
    }
  }

  noiseFloorBass   = (sumB / CAL_FRAMES) * CAL_MARGIN;
  noiseFloorTreble = (sumT / CAL_FRAMES) * CAL_MARGIN;

  if (noiseFloorBass   < 0.001f) noiseFloorBass   = 0.001f;
  if (noiseFloorTreble < 0.001f) noiseFloorTreble = 0.001f;

  for (uint8_t i = 0; i < LED_COUNT; i++)
    strip.setPixelColor(i, strip.Color(80, 80, 80));
  strip.show();
  delay(300);
  strip.clear();
  strip.show();

  Serial.print(F("CAL  bassFloor="));   Serial.print(noiseFloorBass,   4);
  Serial.print(F("  trebleFloor="));    Serial.println(noiseFloorTreble, 4);
}

// ─────────────────────────────────────────────────────────────────────────────
// FIX: Smooth gradient across full 0.0→1.0 range instead of 4 hard steps.
// This is why you only saw yellow or blue — normBass almost never hit
// the middle buckets (0.25–0.75). Now every value produces a unique color.
// Blue(0) → Cyan → Green → Yellow(1)
// ─────────────────────────────────────────────────────────────────────────────
void normToRGB(float n, uint8_t &r, uint8_t &g, uint8_t &b) {
  if (n < 0.0f) n = 0.0f;
  if (n > 1.0f) n = 1.0f;

  if (n < 0.33f) {
    // Blue → Cyan
    float t = n / 0.33f;
    r = 0;
    g = (uint8_t)(200 * t);
    b = 255;
  } else if (n < 0.66f) {
    // Cyan → Green
    float t = (n - 0.33f) / 0.33f;
    r = 0;
    g = (uint8_t)(200 + 55 * t);   // 200→255
    b = (uint8_t)(255 * (1.0f - t));
  } else {
    // Green → Yellow
    float t = (n - 0.66f) / 0.34f;
    r = (uint8_t)(220 * t);
    g = 255;
    b = 0;
  }
}

uint8_t scaleBy(uint8_t ch, uint8_t bri) {
  return (uint8_t)(((uint16_t)ch * bri) / 255);
}

// ─────────────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  for (uint8_t i = 0; i < ROLLING_LEN; i++) rollingBuf[i] = 0.0f;
  strip.begin();
  strip.setBrightness(255);
  strip.show();
  calibrate();
}

// ─────────────────────────────────────────────────────────────────────────────
void loop() {
  unsigned long now = millis();

  // ── 1. Measure ──────────────────────────────────────────────────────────────
  float bassRaw   = measureACRms(BASS_PIN);
  float trebleRaw = measureACRms(TREBLE_PIN);

  // ── 2. IIR smooth ───────────────────────────────────────────────────────────
  filteredBass   = (IIR_A * filteredBass)   + (IIR_B * bassRaw);
  filteredTreble = (IIR_A * filteredTreble) + (IIR_B * trebleRaw);

  // ── 3. Signal above noise floor ─────────────────────────────────────────────
  float activeBass   = filteredBass   - noiseFloorBass;
  float activeTreble = filteredTreble - noiseFloorTreble;
  if (activeBass   < 0.0f) activeBass   = 0.0f;
  if (activeTreble < 0.0f) activeTreble = 0.0f;

  // ── 4. Silence gate ─────────────────────────────────────────────────────────
  if (activeBass < 0.001f) {
    if (silenceFrames < 255) silenceFrames++;
  } else {
    silenceFrames = 0;
  }
  bool isSilent = (silenceFrames > SILENCE_FRAMES);

  // ── 5. Update peak ──────────────────────────────────────────────────────────
  if (!isSilent) {
    if (activeBass   > bassPeak)   bassPeak   = activeBass;
    else                           bassPeak  *= PEAK_DECAY;
    if (bassPeak < 0.001f)         bassPeak   = 0.001f;

    if (activeTreble > treblePeak) treblePeak = activeTreble;
    else                           treblePeak *= PEAK_DECAY;
    if (treblePeak < 0.001f)       treblePeak = 0.001f;
  }

  // ── 6. Normalise ────────────────────────────────────────────────────────────
  float normBass   = activeBass   / bassPeak;
  float normTreble = activeTreble / treblePeak;
  if (normBass   > 1.0f) normBass   = 1.0f;
  if (normTreble > 1.0f) normTreble = 1.0f;

  // ── 7. Beat detection ───────────────────────────────────────────────────────
  float rollingAvg = updateRolling(activeBass);
  bool  beat       = false;

  if (!isSilent &&
      activeBass > rollingAvg * BEAT_RATIO &&
      normBass   > BEAT_MIN_NORM           &&
      (now - lastBeatMs) > (unsigned long)MIN_BEAT_MS) {

    beat       = true;
    lastBeatMs = now;
    beatCount++;

    if (beatCount >= BEATS_PER_TOGGLE) {
      beatCount     = 0;
      activePattern = (activePattern == 0) ? 1 : 0;
    }
  }

  // ── 8. Color from bass ──────────────────────────────────────────────────────
  uint8_t r, g, b;
  normToRGB(normBass, r, g, b);

  // ── 9. Brightness from treble ───────────────────────────────────────────────
  uint8_t brightness = 0;
  if (!isSilent) {
    brightness = (uint8_t)(BRIGHTNESS_MIN +
                 normTreble * (255 - BRIGHTNESS_MIN));
  }

  // ── 10. Render ──────────────────────────────────────────────────────────────
  strip.clear();

  if (!isSilent) {

    // ─ PATTERN 0: FILL ──────────────────────────────────────────────────────
    if (activePattern == 0) {
      float fillNorm = pow(normBass, 1.0f / FILL_POWER);
      if (fillNorm > 1.0f) fillNorm = 1.0f;
      uint8_t litCount = (uint8_t)(fillNorm * LED_COUNT + 0.5f);

      for (uint8_t i = 0; i < litCount; i++) {
        strip.setPixelColor(i,
          strip.Color(scaleBy(r, brightness),
                      scaleBy(g, brightness),
                      scaleBy(b, brightness)));
      }
    }

    // ─ PATTERN 1: CHASE ─────────────────────────────────────────────────────
    else {
      unsigned long stepMs = (unsigned long)(
        CHASE_SLOW_MS - normTreble * (CHASE_SLOW_MS - CHASE_FAST_MS));
      if (stepMs < 10) stepMs = 10;

      if ((now - lastChaseMs) >= stepMs) {
        lastChaseMs = now;
        if (beat) chaseDir = -chaseDir;

        if (chaseDir > 0) {
          chaseHead = (uint8_t)((chaseHead + 1) % LED_COUNT);
        } else {
          chaseHead = (chaseHead == 0)
                        ? (uint8_t)(LED_COUNT - 1)
                        : (uint8_t)(chaseHead - 1);
        }
      }

      for (uint8_t w = 0; w < CHASE_WINDOW; w++) {
        uint8_t px = (chaseDir > 0)
          ? (uint8_t)((chaseHead + LED_COUNT - w) % LED_COUNT)
          : (uint8_t)((chaseHead + w)              % LED_COUNT);

        uint8_t trailBri = (uint8_t)(
          (uint16_t)brightness * (CHASE_WINDOW - w) / CHASE_WINDOW);

        strip.setPixelColor(px,
          strip.Color(scaleBy(r, trailBri),
                      scaleBy(g, trailBri),
                      scaleBy(b, trailBri)));
      }
    }
  }

  strip.show();

  // ── 11. Serial debug ────────────────────────────────────────────────────────
  static unsigned long lastLog = 0;
  if (now - lastLog > 300) {
    lastLog = now;
    Serial.print(F("aB="));    Serial.print(activeBass,   4);
    Serial.print(F("  pk="));  Serial.print(bassPeak,     4);
    Serial.print(F("  nm="));  Serial.print(normBass,     2);
    Serial.print(F("  avg=")); Serial.print(rollingAvg,   4);
    Serial.print(F("  bt="));  Serial.print(beat ? "Y" : "-");
    Serial.print(F("  cnt=")); Serial.print(beatCount);
    Serial.print(F("  bri=")); Serial.print(brightness);
    Serial.print(F("  pat=")); Serial.println(activePattern == 0 ? "FILL" : "CHASE");
  }
}