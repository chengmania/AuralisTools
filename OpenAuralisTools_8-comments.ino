/* 
MIT License

Copyright (c) 2025 Gregory Cheng INC, Open Auralis Tools

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

// Open Auralis Tools Tuning Engine
// 2025 Gregory Cheng, RPT


  
=== OpenAuralisTools Pin Map (Seeed XIAO ESP32-S3) ===

  OLED Display (SSD1306 / I2C):
    SDA  → D4 (GPIO5)
    SCL  → D5 (GPIO6)

  AHT20 Temperature & Humidity Sensor:
    SDA  → D4 (GPIO5)
    SCL  → D5 (GPIO6)

  Rotary Encoder:
    CLK (A) → D1 (GPIO2)
    DT  (B) → D0 (GPIO1)
    SW (Btn)→ D2 (GPIO3)

  Speaker (STEMMA):
    A+ (PWM Audio In) → D9 (GPIO8) via RC filter
    VIN               → 3V3
    GND               → GND

  GPIO8 (D9)
   |
   R = 1kΩ
   |
   |-------> To STEMMA Speaker +
   |
   C = 0.1µF
   |
  GND -------> To STEMMA Speaker -

  Connect the resistor in series between GPIO8 and the speaker input.
  Connect the capacitor from the speaker input to GND (i.e., across the speaker's + and – terminals).

  Notes:
    - SDA/SCL are shared across OLED and AHT20 (I2C bus).
    - Use INPUT_PULLUP for the encoder button.
    - Optional RC Filter: 1kΩ resistor + 0.1µF cap from D9 to GND to smooth audio.
*/

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>
#include <RotaryEncoder.h>  // Matthias Hertel's encoder lib
#include <Adafruit_AHTX0.h> // Temp & humidity sensor
#include "driver/ledc.h"
#include <Arduino.h>
#include <Preferences.h>  //Saves settings
Preferences prefs;

// === OLED Display Settings ===
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Adafruit_AHTX0 aht;

RotaryEncoder encoder(2, 3);     // CLK = D1 (GPIO3), DT = D0 (GPIO2)
const int encoderButtonPin = 1;  // SW = D2 (GPIO1)

bool toneStarted = false;   // Tracks if a tone is currently playing
int lastPos = 0;             // Last known encoder position


// === Application State Enums ===
enum MenuState {
  MAIN_MENU,
  TONE_MENU,
  BEAT_MENU,
  ENV_MENU,
  TONE_PLAY_A440,
  TONE_PLAY_CHROMATIC,
  TONE_ASSIST
};

MenuState currentState = MAIN_MENU;
int selectedItem = 0;

int assistToneIndex = 0;     // Default F3
float octaveStretch = 0.0f;  // Cents added per octave

enum AssistState {
  ASSIST_TOP,  // NEW: Main assist menu
  ASSIST_MENU,
  ASSIST_PLAYING,
  ASSIST_STRETCH
};

enum A440State {
  A440_MENU,
  A440_OFFSET
};

A440State a440State = A440_MENU;

const char* assistTones[] = { "F3", "A3", "C#4", "F4", "A4", "Back" };
const float baseFrequencies[] = { 174.61f, 220.00f, 277.18f, 349.23f, 440.00f };  // Immutable reference
float assistFrequencies[5];                                                       // Mutable version used during playback
const char* assistTopMenu[] = { "Play Tones", "Octave Stretch", "Back" };
int assistTopSelectedItem = 0;


AssistState assistState = ASSIST_MENU;

// === Menu Options ===
const char* mainMenu[] = { "Tone Generator", "Beat Rates", "Environment" };
const char* toneMenu[] = { "A440", "Tuning Assist" };
const char* beatMenu[] = { "1 BPS", "3 BPS", "5 BPS", "7 BPS", "9 BPS", "11 BPS" };
// === A440 Submenu ===
const char* a440Menu[] = { "Play", "Set Offset", "Back" };
int a440SelectedItem = 0;
float a440OffsetCents = 0.0f;  // Default offset in cents


//unsigned long lastEncoderUpdate = 0;
bool inSubMenu = false;

//Beat rates
int beatRates[] = { 1, 3, 5, 7, 9, 11 };  // In ticks per second
int selectedBeatRateIndex = 0;            // Default to 1 BPS
unsigned long lastBeatMillis = 0;
bool beatActive = false;

// === Forward Declarations ===
void handleSelect();
int getMenuLength();
void renderMenu();
void drawCenteredItem(const char* label);
void handleBeatRate();
void playTone(int freq);
void stopTone();
void playAssistTone();
void playBeatTick();

// === Setup Function ===
// Initializes peripherals, restores settings, and starts FreeRTOS tasks
void setup() {
  //save and restore stretch settings
  prefs.begin("auralis", true);                     // Open read-only
  octaveStretch = prefs.getFloat("stretch", 0.0f);  // Default to 0.0 if not found
  prefs.end();

  Serial.begin(115200);
  pinMode(encoderButtonPin, INPUT_PULLUP);
  Wire.begin(5, 6);     // SDA = GPIO5, SCL = GPIO6
  Wire.setClock(7000);  // Set I2C to 100kHz (default is 400kHz)


  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("OLED init failed"));
    while (true)
      ;
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(10, 15);
  display.print("Open Auralis Tools");
  display.display();
  delay(1000);
  
  // Reset encoder state
  encoder.setPosition(0);
   // Initialize temp/humidity sensor
  if (!aht.begin()) {
    Serial.println("AHT20 not found");
  }

  for (int i = 0; i < 5; i++) {
    assistFrequencies[i] = baseFrequencies[i];
  }
  // Start concurrent tasks
  xTaskCreatePinnedToCore(uiTask, "UI Task", 4096, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(audioTask, "Audio Task", 2048, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(envTask, "Env Task", 2048, NULL, 1, NULL, 1);
}

// === Loop Function ===
// Empty loop; everything runs inside FreeRTOS tasks
void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));  // Idle; handled in tasks
}

// === UI Task ===
// Handles rotary encoder movement and button logic in its own threa
void uiTask(void* pvParameters) {
  encoder.setPosition(0);

  static bool lastButtonState = HIGH;
  static bool buttonHeld = false;
  static unsigned long buttonPressTime = 0;
  unsigned long lastUpdate = 0;

  for (;;) {
    encoder.tick();

    // === Button Handling ===
    bool currentButtonState = digitalRead(encoderButtonPin);
    unsigned long now = millis();

    // Button Press Detected
    if (!buttonHeld && lastButtonState == HIGH && currentButtonState == LOW) {
      buttonPressTime = now;
      buttonHeld = true;
      Serial.println("BUTTON DOWN");
    }

    // Button Released
    if (buttonHeld && lastButtonState == LOW && currentButtonState == HIGH) {
      buttonHeld = false;
      unsigned long heldTime = now - buttonPressTime;
      Serial.print("BUTTON UP after ");
      Serial.print(heldTime);
      Serial.println(" ms");

      if (currentState == TONE_PLAY_A440 && heldTime >= 800) {
        Serial.println("LONG PRESS: EXIT");
        stopTone();
        toneStarted = false;
        currentState = TONE_MENU;
        selectedItem = 0;
        a440SelectedItem = 0;
        encoder.setPosition(0);
        renderMenu();
      } else if (currentState == TONE_PLAY_A440) {
        float freq = 440 * pow(2.0, a440OffsetCents / 1200.0);
        toneStarted = !toneStarted;
        if (toneStarted) playTone(freq);
        else stopTone();
        Serial.println("SHORT PRESS: TOGGLE");
      } else {
        handleSelect();
      }
    }

    lastButtonState = currentButtonState;

    // === Rotary Handling ===
    int currentPos = encoder.getPosition();
    int delta = currentPos - lastPos;

    if (millis() - lastUpdate >= 150 && abs(delta) >= 1) {
      lastPos = currentPos;

      if (currentState == TONE_ASSIST) {
        if (assistState == ASSIST_TOP) {
          int menuLength = 3;
          currentPos = constrain(currentPos, 0, menuLength - 1);
          encoder.setPosition(currentPos);
          assistTopSelectedItem = currentPos;

        } else if (assistState == ASSIST_MENU) {
          currentPos = constrain(currentPos, 0, (sizeof(assistTones) / sizeof(assistTones[0])) - 1);
          encoder.setPosition(currentPos);
          assistToneIndex = currentPos;

        } else if (assistState == ASSIST_PLAYING) {
          currentPos = constrain(currentPos, 0, (sizeof(assistTones) / sizeof(assistTones[0])) - 1);
          encoder.setPosition(currentPos);

          if (currentPos != assistToneIndex) {
            assistToneIndex = currentPos;

            if (assistToneIndex == 5) {
              stopTone();
              assistState = ASSIST_TOP;
              encoder.setPosition(assistTopSelectedItem);
            } else {
              playAssistTone();
            }
          }

        } else if (assistState == ASSIST_STRETCH) {
          octaveStretch = constrain(currentPos * 0.1f, -3.0f, 3.0f);
        }
      }

      // === A440 Offset Adjustment ===
      else if (currentState == TONE_PLAY_A440) {
        float step = 0.1f * delta;
        a440OffsetCents += step;
        a440OffsetCents = constrain(a440OffsetCents, -50.0f, 50.0f);

        if (toneStarted) {
          float freq = 440 * pow(2.0, a440OffsetCents / 1200.0);
          tone(8, freq);
        }
      }

      // === Other Menus ===
      else {
        int menuLength = getMenuLength();
        currentPos = constrain(currentPos, 0, menuLength - 1);
        encoder.setPosition(currentPos);
        selectedItem = currentPos;

        if (currentState == BEAT_MENU)
          selectedBeatRateIndex = selectedItem;
      }

      renderMenu();
      lastUpdate = millis();
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// === Audio Task ===
// Runs beat timing and tick output independently
void audioTask(void* pvParameters) {
  for (;;) {
    if (currentState == BEAT_MENU) {
      handleBeatRate();
    }

    // Could expand this to include tone decay, fadeouts, etc.
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

// === Env Task ===
// Periodically polls AHT20 and updates environment scree
void envTask(void* pvParameters) {
  for (;;) {
    if (currentState == ENV_MENU) {
      renderMenu();  // Renders updated temp/humidity
    }
    vTaskDelay(pdMS_TO_TICKS(2000));  // Refresh every 2 seconds
  }
}

// Save Stretch Settings
void saveStretchSetting() {
  prefs.begin("auralis", false);  // Open for writing
  prefs.putFloat("stretch", octaveStretch);
  prefs.end();
}

// === Menu Lengths ===
int getMenuLength() {
  switch (currentState) {
    case MAIN_MENU: return sizeof(mainMenu) / sizeof(mainMenu[0]);
    case TONE_MENU: return sizeof(toneMenu) / sizeof(toneMenu[0]);
    case BEAT_MENU: return sizeof(beatMenu) / sizeof(beatMenu[0]);
    default: return 0;
  }
}


// === State Transition Logic ===
void handleSelect() {
  stopTone();  // Always stop tones on selection

  switch (currentState) {
    case MAIN_MENU: handleMainMenuSelect(); break;
    case TONE_MENU: handleToneMenuSelect(); break;
    case TONE_PLAY_A440: handleA440Select(); break;
    case TONE_ASSIST: handleToneAssistSelect(); break;
    case BEAT_MENU:
    case ENV_MENU:
    case TONE_PLAY_CHROMATIC:
      currentState = MAIN_MENU;
      break;
    default:
      currentState = MAIN_MENU;
      break;
  }

  selectedItem = 0;
  renderMenu();
}

void handleMainMenuSelect() {
  switch (selectedItem) {
    case 0: currentState = TONE_MENU; break;
    case 1: currentState = BEAT_MENU; break;
    case 2: currentState = ENV_MENU; break;
  }
}

void handleToneMenuSelect() {
  switch (selectedItem) {
    case 0:
      currentState = TONE_PLAY_A440;
      encoder.setPosition(a440OffsetCents * 10);  // Only once here
      break;
    case 1:
      enterToneAssist();
      break;
  }
}

void handleA440Select() {
  switch (a440SelectedItem) {
    case 0:
      playTone(440 * pow(2.0, a440OffsetCents / 1200.0));
      toneStarted = true;
      break;
    case 1:
      a440State = A440_OFFSET;
      encoder.setPosition(a440OffsetCents * 10);  // Each unit = 0.1 cent
      break;
    case 2:
      stopTone();
      toneStarted = false;
      currentState = TONE_MENU;
      encoder.setPosition(selectedItem);
      break;
  }
}

void enterToneAssist() {
  currentState = TONE_ASSIST;
  assistState = ASSIST_TOP;
  assistTopSelectedItem = 0;
  encoder.setPosition(0);
}

void handleToneAssistSelect() {
  switch (assistState) {
    case ASSIST_TOP:
      if (assistTopSelectedItem == 0) {
        assistState = ASSIST_MENU;
        encoder.setPosition(assistToneIndex);
      } else if (assistTopSelectedItem == 1) {
        assistState = ASSIST_STRETCH;

        // 🛠 Sync encoder to saved stretch value (0.1 BPS steps → int position)
        int stretchPos = round(octaveStretch * 10.0f);  // -30 to +30
        encoder.setPosition(stretchPos);

        // 🛠 Also sync lastPos if you're using it in uiTask() to prevent immediate overwrite
        lastPos = stretchPos;
      } else if (assistTopSelectedItem == 2) {
        currentState = MAIN_MENU;
        assistState = ASSIST_TOP;
      }
      break;

    case ASSIST_MENU:
      if (assistToneIndex == (sizeof(assistTones) / sizeof(assistTones[0])) - 1) {
        assistState = ASSIST_TOP;
        encoder.setPosition(assistTopSelectedItem);
      } else {
        assistState = ASSIST_PLAYING;
        playAssistTone();
      }
      break;

    case ASSIST_PLAYING:
      stopTone();
      assistState = ASSIST_MENU;
      break;

    case ASSIST_STRETCH:
      saveStretchSetting();
      assistState = ASSIST_TOP;
      encoder.setPosition(assistTopSelectedItem);
      break;
  }
}


// === Display Dispatcher ===
void renderMenu() {
  switch (currentState) {
    case MAIN_MENU: renderMainMenu(); break;
    case TONE_MENU: renderToneMenu(); break;
    case BEAT_MENU: renderBeatMenu(); break;
    case ENV_MENU: renderEnvMenu(); break;
    case TONE_PLAY_A440: renderA440Screen(); break;
    case TONE_PLAY_CHROMATIC: renderChromaticScreen(); break;
    case TONE_ASSIST: renderAssistMenu(); break;
  }
}

// === Menu Renderers ===
void renderMainMenu() {
  drawCenteredItem(mainMenu[selectedItem]);
}

void renderToneMenu() {
  drawCenteredItem(toneMenu[selectedItem]);
}

void renderBeatMenu() {
  drawCenteredItem(beatMenu[selectedItem]);
}

void renderEnvMenu() {
  sensors_event_t humidity, temp;
  aht.getEvent(&humidity, &temp);
  float tempF = temp.temperature * 9.0 / 5.0 + 32.0;

  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(25, 0);
  //display.print("Temp: ");
  display.print(tempF, 1);
  display.print(" F");
  display.setCursor(25, 17);
  //display.print("Humidity: ");
  display.print(humidity.relative_humidity);
  display.println(" %");
  display.display();
}

void renderA440Screen() {
  display.clearDisplay();
  display.setCursor(0, 0);

  float freq = 440 * pow(2.0, a440OffsetCents / 1200.0);
  display.println("A440 Generator");
  display.print("Freq: ");
  display.print(freq, 2);
  display.println(" Hz");
  display.print("Offset: ");
  display.print(a440OffsetCents, 1);
  display.println("c");
  display.println(toneStarted ? "Playing..." : "Stopped");
  display.println("Click to toggle");

  display.display();
}


void renderChromaticScreen() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Chromatic Scale...");
  display.display();
}

void renderAssistMenu() {
  display.clearDisplay();
  display.setCursor(0, 0);

  switch (assistState) {
    case ASSIST_TOP:
      display.println("Tune Assist");
      for (int i = 0; i < sizeof(assistTopMenu) / sizeof(assistTopMenu[0]); i++) {
        if (i == assistTopSelectedItem) display.print("> ");
        else display.print("  ");
        display.println(assistTopMenu[i]);
      }
      break;

    case ASSIST_MENU:
      display.setCursor(25, 0);
      display.print("Select Tone:");
      display.setCursor(45, 15);
      //display.setTextSize(2);
      display.println(assistTones[assistToneIndex]);
      break;

    case ASSIST_PLAYING:
      display.setCursor(25, 0);
      display.print("Playing:");
      display.setCursor(45, 15);
      //display.setTextSize(2);
      display.println(assistTones[assistToneIndex]);
      break;

    case ASSIST_STRETCH:
      display.setCursor(25, 0);
      display.print("Octave Stretch:");
      display.setCursor(45, 12);
      display.print(octaveStretch, 2);
      display.println(" BPS");

      if (octaveStretch > 0.01f) {
        display.setCursor(45, 23);
        display.println("Widening");
      } else if (octaveStretch < -0.01f) {
        display.setCursor(45, 23);
        display.println("Narrowing");
      } else {
        display.setCursor(45, 23);
        display.println("Unstretched");
      }

      break;
  }

  display.display();
}



// === Helper to Draw Menus ===
void drawCenteredItem(const char* label) {

  if (label == nullptr || strlen(label) == 0) return;  // 🔐 extra protection

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  String line = "< ";
  line += label;
  line += " >";

  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(line, 0, 0, &x1, &y1, &w, &h);

  int x = (SCREEN_WIDTH - w) / 2;
  int y = (SCREEN_HEIGHT - h) / 2;

  display.setCursor(x, y);
  display.print(line);
  display.display();
}

// === Tone Functions ===
void playTone(int freq) {
  tone(8, freq);
}

void stopTone() {
  noTone(8);
}

void handleBeatRate() {
  unsigned long currentMillis = millis();
  int interval = 1000 / beatRates[selectedBeatRateIndex];  // milliseconds per tick

  if (currentMillis - lastBeatMillis >= interval) {
    lastBeatMillis = currentMillis;
    playBeatTick();
  }
}

void playBeatTick() {
  tone(8, 1000, 30);  // Short tick at 1kHz for 30ms
}

void playAssistTone() {
  if (assistToneIndex == 5) return;  // Back
  updateAssistFreqsFromStretch();    // Make sure the stretch is applied every time
  tone(8, assistFrequencies[assistToneIndex]);
  Serial.print("Playing ");
  Serial.print(assistTones[assistToneIndex]);
  Serial.print(" @ ");
  Serial.println(assistFrequencies[assistToneIndex], 2);
}



// === Assist Frequency Update Function ===
// This updates assistFrequencies[] based on octaveStretch (in BPS)
// A4 is fixed at 440 Hz
void updateAssistFreqsFromStretch() {
  const float BPS_TO_CENTS = 3.91f;
  float stretchCents = octaveStretch * BPS_TO_CENTS;

  // Fixed A4
  assistFrequencies[4] = 440.0f;

  // A3 — 4:2 octave stretch (A3 up to A4)
  float A4_2nd = 2.0f * assistFrequencies[4];
  float targetA3_4th = A4_2nd / pow(2.0, stretchCents / 1200.0);  // 4:2
  assistFrequencies[1] = targetA3_4th / 4.0f;

  // F4 — 5 semitones below A4 in equal temperament
  assistFrequencies[3] = assistFrequencies[4] * pow(2.0f, -4.0f / 12.0f);

  // F3 — 6:3 octave stretch (F3 up to F4)
  float F4_3rd = 3.0f * assistFrequencies[3];
  float targetF3_6th = F4_3rd / pow(2.0, stretchCents / 1200.0);  // 6:3
  assistFrequencies[0] = targetF3_6th / 6.0f;

  // C#4 — 8 semitones below A4 (or 4 semitones above A3)
  assistFrequencies[2] = assistFrequencies[4] * pow(2.0f, -8.0f / 12.0f);
}
