#include <Adafruit_NeoPixel.h>

#define LED_PIN 6
#define LED_COUNT 16

Adafruit_NeoPixel ring(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

void setup() {
  ring.begin();
  ring.show();
}

void loop() {
  for (int brightness = 0; brightness <= 255; brightness++) {
    ring.setBrightness(brightness);
    ring.fill(ring.Color(0, 0, 255)); // Blue
    ring.show();
    delay(5);
  }

  for (int brightness = 255; brightness >= 0; brightness--) {
    ring.setBrightness(brightness);
    ring.fill(ring.Color(0, 0, 255)); // Blue
    ring.show();
    delay(5);
  }
}