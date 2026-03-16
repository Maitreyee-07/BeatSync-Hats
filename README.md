# 🎧 Beat Sensing LED Hat

A wearable **music-reactive LED hat** that lights up in sync with surrounding music beats.  
The system uses a **microphone module to detect audio intensity**, processes the signal using an **Arduino Nano**, and drives **WS2812B addressable LEDs** to produce dynamic lighting effects.

This project demonstrates a simple implementation of **real-time audio signal processing on embedded hardware** combined with **programmable LED control**.

---

## ✨ Features

- 🎶 Beat detection using microphone input
- 💡 Dynamic LED patterns synchronized with music
- ⚡ Addressable WS2812B LED control
- 🧠 Arduino Nano based processing
- 👒 Wearable LED hat design

---

## 🧰 Hardware Components

| Component | Description |
|-----------|-------------|
| Arduino Nano | Microcontroller used to process audio signal and control LEDs |
| WS2812B LED Strip | Addressable RGB LEDs for lighting effects |
| Microphone Module | Captures ambient sound/music |
| Power Supply | Battery pack or portable power source |
| Connecting Wires | Circuit connections |

---

## ⚙️ Working Principle

1. The **microphone module** captures ambient sound.
2. The **Arduino Nano reads the analog signal** from the microphone.
3. The program monitors the **audio amplitude**.
4. When the signal exceeds a defined **threshold**, it is interpreted as a **beat**.
5. The **WS2812B LEDs respond with lighting effects** such as flashes or color transitions.

This enables the LEDs on the hat to **react to music in real time**.

---
## 🖼 System Overview


Microphone → Arduino Nano → WS2812B LEDs
(Audio) (Processing) (Lighting Effects)


---

## 🔌 Circuit Connections

| Device | Arduino Pin |
|------|------|
| Microphone OUT | A0 |
| WS2812B Data | D6 |
| VCC | 5V |
| GND | GND |

*(Pin configuration may vary depending on the implementation.)*

---

## 💻 Software Requirements

- Arduino IDE
- FastLED library or Adafruit NeoPixel library

Install required libraries using the **Arduino Library Manager**.

---

## 🚀 How to Run

1. Clone the repository
git clone https://github.com/Maitreyee-07/BeatSync-Hats.git


2. Open the Arduino sketch in **Arduino IDE**

3. Install required LED libraries

4. Upload the code to **Arduino Nano**

5. Power the system and play music near the hat 🎶

---

## 📸 Prototype
Images/Prototype.jpeg




---

## 🔧 Future Improvements

- FFT based beat detection
- Multiple LED animation modes
- Adjustable sensitivity
- Bluetooth music synchronization
- Custom wearable PCB

---

## 👩‍💻 Author

**Maitreyee Kumbhojkar**  
Electrical Engineering, IIT Dharwad  

GitHub: https://github.com/Maitreyee-07
