#include <Adafruit_NeoPixel.h>

#define MIC_PIN     A1
#define LED_PIN     6
#define LED_COUNT   80

#define N              64
#define FS             9600

#define LOCAL_BEAT_RATIO   1.5
#define MIN_BEAT_INTERVAL  200
#define TRANSIENT_RISE     1.15
#define LOCAL_AVG_LEN      43
#define MIN_ABS_RMS        5.0

#define BPM_HISTORY        8
#define SLOW_BPM_THRESH    80
#define FAST_BPM_THRESH    140

#define SLOW_SUSTAIN_MS    800
#define MEDIUM_SUSTAIN_MS  350
#define FAST_SUSTAIN_MS    150

#define BEAT_MODE_WINDOW    3000
#define BEAT_MODE_MIN_COUNT 3
#define BEAT_DROPOUT_MS     2500

#define MUSIC_SILENCE_RMS  4.0
#define MUSIC_SMOOTH       0.15

#define MASTER_BRIGHTNESS  55


#define MAX_LIT_LEDS       20

Adafruit_NeoPixel ring(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

int16_t samples[N];
float   localEnergyBuf[LOCAL_AVG_LEN];
int     localEnergyIdx  = 0;
float   localEnergySum  = 0;
bool    localEnergyFull = false;


float         prevRMS             = 0;
unsigned long lastBeatTime        = 0;
float         beatIntervals[BPM_HISTORY];
int           bpmIdx              = 0;
bool          bpmReady            = false;
float         currentBPM          = 0;


int           currentMode         = 0;
unsigned long lastBeatDetectedTime = 0;
unsigned long beatTimestamps[8];
int           beatTsIdx           = 0;

unsigned long beatOnTime   = 0;
int           sustainMs    = 300;
int           chasePos     = 0;
int           colorPhase   = 0;
float         musicBrightness = 0;
float         hueShift     = 0;

uint32_t hsv(float h, float s, float v) {
  h = fmod(h, 360.0);
  if (h < 0) h += 360;
  float c = v * s;
  float x = c * (1.0 - fabs(fmod(h / 60.0, 2.0) - 1.0));
  float m = v - c;
  float r = 0, g = 0, b = 0;
  if      (h < 60)  { r=c; g=x; b=0; }
  else if (h < 120) { r=x; g=c; b=0; }
  else if (h < 180) { r=0; g=c; b=x; }
  else if (h < 240) { r=0; g=x; b=c; }
  else if (h < 300) { r=x; g=0; b=c; }
  else              { r=c; g=0; b=x; }
  return ring.Color(
    (uint8_t)((r + m) * 255),
    (uint8_t)((g + m) * 255),
    (uint8_t)((b + m) * 255)
  );
}


void captureSamples() {
  for (int i = 0; i < N; i++) samples[i] = analogRead(MIC_PIN);
}

float getRMS() {
  long sum = 0;
  for (int i = 0; i < N; i++) sum += samples[i];
  int16_t mean = sum / N;
  float sumSq = 0;
  for (int i = 0; i < N; i++) {
    float d = samples[i] - mean;
    sumSq += d * d;
  }
  return sqrt(sumSq / N);
}

float updateLocalAvg(float rms) {
  localEnergySum -= localEnergyBuf[localEnergyIdx];
  localEnergyBuf[localEnergyIdx] = rms;
  localEnergySum += rms;
  localEnergyIdx = (localEnergyIdx + 1) % LOCAL_AVG_LEN;
  if (localEnergyIdx == 0) localEnergyFull = true;
  int count = localEnergyFull ? LOCAL_AVG_LEN : (localEnergyIdx > 0 ? localEnergyIdx : LOCAL_AVG_LEN);
  return (count > 0) ? (localEnergySum / count) : rms;
}


void updateBPM(unsigned long intervalMs) {
  beatIntervals[bpmIdx] = (float)intervalMs;
  bpmIdx = (bpmIdx + 1) % BPM_HISTORY;
  if (bpmIdx == 0) bpmReady = true;
  int count = bpmReady ? BPM_HISTORY : bpmIdx;
  if (count == 0) return;
  float total = 0;
  for (int i = 0; i < count; i++) total += beatIntervals[i];
  currentBPM = 60000.0 / (total / count);
}

bool detectBeat(float rms, float localAvg) {
  if (rms < MIN_ABS_RMS)                             return false;
  if (rms < localAvg * LOCAL_BEAT_RATIO)              return false;
  if (prevRMS > 0 && rms < prevRMS * TRANSIENT_RISE) return false;
  unsigned long now = millis();
  if ((now - lastBeatTime) < MIN_BEAT_INTERVAL)       return false;
  if (lastBeatTime > 0) updateBPM(now - lastBeatTime);
  lastBeatTime = now;
  return true;
}


int determineMode(float rms, bool beat) {
  unsigned long now = millis();
  if (beat) {
    lastBeatDetectedTime = now;
    beatTimestamps[beatTsIdx % 8] = now;
    beatTsIdx++;
  }
  int recentBeats = 0;
  for (int i = 0; i < 8; i++) {
    if (beatTimestamps[i] > 0 &&
        (now - beatTimestamps[i]) < (unsigned long)BEAT_MODE_WINDOW)
      recentBeats++;
  }
  if (rms < MUSIC_SILENCE_RMS)                                        return 0;
  if (recentBeats >= BEAT_MODE_MIN_COUNT &&
      (now - lastBeatDetectedTime) < (unsigned long)BEAT_DROPOUT_MS)  return 2;
  return 1;
}


void renderSilence() {
  musicBrightness = 0;
  ring.clear();
  ring.show();
}


void renderMusicOnly(float rms) {
  float target = constrain((rms - MUSIC_SILENCE_RMS) * 8.0, 0, 255);
  musicBrightness += (target - musicBrightness) * MUSIC_SMOOTH;

  if (musicBrightness < 4) {
    ring.clear();
    ring.show();
    return;
  }


  hueShift += 0.3;
  if (hueShift >= 360) hueShift -= 360;

  float v = musicBrightness / 255.0;

  ring.clear();

  int offset = (millis() / 3000) % 4;

  // Light every 4th LED = 20 LEDs max on 80-LED ring
  for (int i = offset; i < LED_COUNT; i += 4) {
    float ledHue = hueShift + (i * 4.5);
    // Slight brightness wave so adjacent dots pulse differently
    float wave   = 0.7 + 0.3 * sin((i / (float)LED_COUNT) * 2.0 * PI + hueShift * 0.03);
    ring.setPixelColor(i, hsv(fmod(ledHue, 360), 1.0, v * wave));
  }

  ring.show();
}


void triggerPatternSlow() {
  ring.clear();

  int origin = chasePos;
  int reach  = MAX_LIT_LEDS / 2; // 10 LEDs each direction

  for (int d = 0; d <= reach; d++) {
    float fade = 1.0 - ((float)d / reach);
    // Quadratic falloff for nicer glow shape
    uint8_t v  = (uint8_t)(255.0 * fade * fade);

    // Pure magenta: R + B, no green (saves green channel power)
    uint8_t r = v;
    uint8_t g = 0;
    uint8_t b = (uint8_t)(v * 0.75);

    ring.setPixelColor((origin + d) % LED_COUNT,             ring.Color(r, g, b));
    ring.setPixelColor((origin - d + LED_COUNT) % LED_COUNT, ring.Color(r, g, b));
  }

  // Hot white-ish core (just this 1 pixel, acceptable cost)
  ring.setPixelColor(origin, ring.Color(255, 30, 200));

  ring.show();

  // Advance origin slowly so each beat ripples from a new spot
  chasePos = (chasePos + 8) % LED_COUNT;
  sustainMs = SLOW_SUSTAIN_MS;
}


void triggerPatternMedium() {
  // Aggressive trail decay (0.35 = trail gone in ~3 frames)
  for (int i = 0; i < LED_COUNT; i++) {
    uint32_t c = ring.getPixelColor(i);
    ring.setPixelColor(i, ring.Color(
      ((c >> 16) & 0xFF) * 0.35,
      ((c >>  8) & 0xFF) * 0.35,
      ( c        & 0xFF) * 0.35
    ));
  }

  int p1 = chasePos;
  int p2 = (chasePos + LED_COUNT / 2) % LED_COUNT;

  // Comet 1 head: Cyan (G+B only — no red draw)
  ring.setPixelColor(p1, ring.Color(0, 200, 255));
  ring.setPixelColor((p1 - 1 + LED_COUNT) % LED_COUNT, ring.Color(0, 80, 130));
  ring.setPixelColor((p1 - 2 + LED_COUNT) % LED_COUNT, ring.Color(0, 25, 50));

  // Comet 2 head: Deep orange (R dominant, half G, no B)
  ring.setPixelColor(p2, ring.Color(255, 110, 0));
  ring.setPixelColor((p2 - 1 + LED_COUNT) % LED_COUNT, ring.Color(120, 45, 0));
  ring.setPixelColor((p2 - 2 + LED_COUNT) % LED_COUNT, ring.Color(40, 14, 0));

  ring.show();

  int step = (bpmReady && currentBPM > 0)
    ? constrain((int)(currentBPM / 35.0), 1, 5) : 2;
  chasePos = (chasePos + step) % LED_COUNT;
  sustainMs = MEDIUM_SUSTAIN_MS;
}


void triggerPatternFast() {
  ring.clear();

  // Hue cycles through spectrum: one full cycle every ~9 beats
  float h = fmod(colorPhase * 40.0, 360.0);

  // Offset shifts by 2 each beat so pattern appears to spin
  int offset = (colorPhase * 2) % 4;

  // Light every 4th LED starting from offset = 20 LEDs
  for (int i = offset; i < LED_COUNT; i += 4) {
    ring.setPixelColor(i, hsv(h, 1.0, 1.0));
  }

  ring.show();
  colorPhase++;
  sustainMs = FAST_SUSTAIN_MS;
}


void beatFadeOut() {
  unsigned long elapsed = millis() - beatOnTime;
  if (elapsed < (unsigned long)sustainMs) return;

  unsigned long fadeElapsed = elapsed - sustainMs;
  if (fadeElapsed > 120) {
    ring.clear();
    ring.show();
    return;
  }

  float fadeRatio = 1.0 - (fadeElapsed / 120.0);
  for (int i = 0; i < LED_COUNT; i++) {
    uint32_t c = ring.getPixelColor(i);
    ring.setPixelColor(i, ring.Color(
      ((c >> 16) & 0xFF) * fadeRatio,
      ((c >>  8) & 0xFF) * fadeRatio,
      ( c        & 0xFF) * fadeRatio
    ));
  }
  ring.show();
}

void setup() {
  Serial.begin(9600);

  ring.begin();
  ring.setBrightness(MASTER_BRIGHTNESS); // ~21% — key power saving
  ring.clear();
  ring.show();

  
  for (int i = 0; i < LED_COUNT; i += 4) {
    ring.setPixelColor(i, ring.Color(180, 0, 80));
    ring.show();
    delay(18);
  }
  delay(200);
  ring.clear(); ring.show();
  for (int i = 0; i < LED_COUNT; i += 4) {
    ring.setPixelColor(i, ring.Color(0, 160, 180));
    ring.show();
    delay(18);
  }
  delay(200);
  ring.clear(); ring.show();

  memset(localEnergyBuf, 0, sizeof(localEnergyBuf));
  memset(beatIntervals,  0, sizeof(beatIntervals));
  memset(beatTimestamps, 0, sizeof(beatTimestamps));

  Serial.println(">>> Warming up (keep quiet)...");
  for (int i = 0; i < LOCAL_AVG_LEN; i++) {
    captureSamples();
    updateLocalAvg(getRMS());
    delay(10);
  }


  Serial.println("Time(ms)  | Mode         | BPM");
  Serial.println("----------|--------------|----");
}

void loop() {
  captureSamples();
  float rms      = getRMS();
  float localAvg = updateLocalAvg(rms);

  bool beat = detectBeat(rms, localAvg);
  prevRMS = rms;

  int mode = determineMode(rms, beat);
  currentMode = mode;

  if (mode == 0) {
    renderSilence();

  } else if (mode == 1) {
    renderMusicOnly(rms);

  } else {
    // Beat mode
    if (beat) {
      beatOnTime = millis();
      if (!bpmReady || currentBPM <= 0) {
        triggerPatternMedium();
      } else if (currentBPM < SLOW_BPM_THRESH) {
        triggerPatternSlow();
      } else if (currentBPM < FAST_BPM_THRESH) {
        triggerPatternMedium();
      } else {
        triggerPatternFast();
      }
    } else {
      beatFadeOut();
    }
  }

  // Serial log every 300ms
  static unsigned long lastLog = 0;
  if (millis() - lastLog > 300) {
    Serial.print(millis());
    Serial.print("ms | ");
    if (mode == 0) {
      Serial.print("SILENCE       | -");
    } else if (mode == 1) {
      Serial.print("MUSIC-ONLY    | -");
    } else {
      if      (!bpmReady || currentBPM <= 0)   Serial.print("BEAT (init)   | ?");
      else if (currentBPM < SLOW_BPM_THRESH) { Serial.print("BEAT SLOW     | "); Serial.print(currentBPM, 0); }
      else if (currentBPM < FAST_BPM_THRESH) { Serial.print("BEAT MEDIUM   | "); Serial.print(currentBPM, 0); }
      else                                   { Serial.print("BEAT FAST     | "); Serial.print(currentBPM, 0); }
    }
    Serial.println();
    lastLog = millis();
  }
}
