#include<Adafruit_NeoPixel.h>

#define LED_PIN 6
#define LED_COUNT 16

Adafruit_NeoPixel ring(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

void setup(){
  ring.begin();
  ring.setBrightness(20);
  ring.show();
  randomSeed(analogRead(0));
}

void loop(){
  chasingOrange(2);
  fireflicker(40);
  dualColorRotate(20);
  sparkleWhite(30);
  breathingYellow(2);
}

void chasingOrange(int rounds){
  for(int r=0; r<rounds;r++){
    for(int pos=0;pos<LED_COUNT;pos++){
      ring.clear();
      ring.setPixelColor(pos, ring.Color(255, 191, 0));
      ring.show();
      delay(120);
    }
  }
}

void fireflicker(int frames){
  for(int f=0; f<frames;f++){
    for(int i=0;i< LED_COUNT;i++){
      int intensity = random(150,255);
      ring.setPixelColor(i,ring.Color(intensity, random(40,80), 0));
    }
    ring.show();
    delay(80);
  }
}

void dualColorRotate(int steps){
  int pos1=0;
  int pos2= LED_COUNT/2;

  for(int i=0; i<steps; i++){
    ring.clear();
    ring.setPixelColor(pos1,ring.Color(255,0,0));
    ring.setPixelColor(pos2, ring.Color(0,0,255));
    ring.show();

    pos1 = (pos1 +1)% LED_COUNT;
    pos2 = (pos2 -1 +LED_COUNT)% LED_COUNT;

    delay(150);
  }
}

void sparkleWhite(int flashes){
  for(int i=0; i<flashes; i++){
    ring.clear();
    int randomLED = random(0,LED_COUNT);
    ring.setPixelColor(randomLED,ring.Color(255,255,255));
    ring.show();
    delay(80);
  }
}

void breathingYellow(int cycles){
  for(int c=0; c< cycles; c++){
    for(int b=0; b<128; b++){
      ring.setBrightness(b);
      ring.fill(ring.Color(255,255,0));
      ring.show();
      delay(5);
    }

    for(int b=128; b>=0; b--){
      ring.setBrightness(b);
      ring.fill(ring.Color(255,255,0));
      ring.show();
      delay(5);
    }
  }
  ring.setBrightness(20);
}