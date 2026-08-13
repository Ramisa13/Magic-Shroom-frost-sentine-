#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include <ESP32Servo.h>
#include "config.h"
#include <GardenSpine.h>

// --- Pins ---
const int DHT_PIN = 4;
const int SERVO_PIN = 13;
const int BUZZER_PIN = 25;   // active buzzer — drive-on-power, no tone()

// ----- Joystick -----
const int JOY_Y_PIN = 35;
const int JOY_SW_PIN = 27;

int joyCenter = 2048;
const int JOY_DEADZONE = 800;

unsigned long ignoreAxisUntil = 0;
const unsigned long AXIS_IGNORE_MS = 400;

// --- Button press tracking (down + up, for long-press exit) ---
volatile bool joyBtnDown = false;
volatile bool joyBtnUp = false;
volatile unsigned long lastIsrMs = 0;
const unsigned long DEBOUNCE_MS = 50;

unsigned long buttonDownAt = 0;
const unsigned long LONG_PRESS_MS = 600;

enum MenuState {
  MENU_OFF,
  MENU_SELECT,
  MENU_EDIT_UPPER,
  MENU_EDIT_LOWER
};

MenuState menuState = MENU_OFF;
int selectedMenu = 0;

// Defaults — recalibrate against real paired sensor data before treating
// these as final frost thresholds. Editable live via the on-device menu.
float upperTempLimit = 30.0;
float lowerTempLimit = 1.0;

unsigned long lastMenuActivity = 0;
const unsigned long MENU_TIMEOUT_MS = 120000;

unsigned long lastJoyMove = 0;
const unsigned long JOY_REPEAT_MS = 250;

Adafruit_SSD1306 display(128, 64, &Wire, -1);
DHT dht(DHT_PIN, DHT11);
Servo cap;
GardenSpine spine;

const unsigned char PROGMEM tempIcon[] = {
  0x07, 0x00, 0x08, 0x80, 0x08, 0x80, 0x08, 0x80, 0x0a, 0x80, 0x0a, 0x80, 0x0a, 0x80, 0x1d, 0xc0,
  0x22, 0x40, 0x27, 0x40, 0x27, 0x40, 0x22, 0x40, 0x1c, 0x80, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00
};

const unsigned char PROGMEM humIcon[] = {
  0x01, 0x00, 0x03, 0x80, 0x07, 0xc0, 0x07, 0xc0, 0x0f, 0xe0, 0x1f, 0xf0, 0x1f, 0xf0, 0x3f, 0xf8,
  0x3f, 0xf8, 0x3f, 0xf8, 0x3f, 0xf8, 0x1f, 0xf0, 0x0f, 0xe0, 0x03, 0x80, 0x00, 0x00, 0x00, 0x00
};

const unsigned char PROGMEM flakeIcon[] = {
  0x04, 0x20, 0x14, 0x50, 0x0c, 0x60, 0x47, 0xc4, 0x24, 0x48, 0x17, 0xd0, 0x6b, 0xd6, 0x07, 0xc0,
  0x6b, 0xd6, 0x17, 0xd0, 0x24, 0x48, 0x47, 0xc4, 0x0c, 0x60, 0x14, 0x50, 0x04, 0x20, 0x00, 0x00
};

// Servo stays within 20-160 degrees per the course's safe-range rule.
const int SERVO_CLOSED_ANGLE = 20;
const int SERVO_OPEN_ANGLE = 110;

// Optional: the neighboring greenhouse team's temperature. Currently
// received and stored but not yet shown anywhere — wire it into drawScreen()
// if you want it displayed as a cross-reference on the OLED.
const char* REMOTE_TEMP_TOPIC =
  "garden/greenhouse-1/climate-node/gm-01/temperature";
const unsigned long REMOTE_MESSAGE_TIMEOUT_MS = 120000;

bool inWarning = false;
int currentAngle = SERVO_OPEN_ANGLE;
unsigned long lastStepMs = 0;
const unsigned long STEP_INTERVAL_MS = 30;

float remoteTempC = 0;
bool remoteMessageReceived = false;
unsigned long lastRemoteMessageMs = 0;

unsigned long lastReading = 0;
const unsigned long SAMPLE_MS = 1000;
unsigned long lastPublishMs = 0;
const unsigned long PUBLISH_MS = 60000;

float lastTempC = 0, lastHumidity = 0;
bool sensorOk = false;

unsigned long lastBlinkMs = 0;
bool blinking = false;
unsigned long blinkStart = 0;
const unsigned long BLINK_INTERVAL_MS = 4000;
const unsigned long BLINK_DURATION_MS = 150;

unsigned long lastDrawMs = 0;
const unsigned long DRAW_INTERVAL_MS = 50;

// Active buzzer — drive-on-power only, no pitch control.
void setBuzzer(bool enabled) {
  static bool buzzerOn = false;
  if (enabled == buzzerOn) return;
  buzzerOn = enabled;
  digitalWrite(BUZZER_PIN, enabled ? HIGH : LOW);
}

void IRAM_ATTR joystickISR() {
  unsigned long now = millis();
  if (now - lastIsrMs > DEBOUNCE_MS) {
    if (digitalRead(JOY_SW_PIN) == LOW) {
      joyBtnDown = true;
    } else {
      joyBtnUp = true;
    }
    lastIsrMs = now;
  }
}

void calibrateJoystick() {
  long sum = 0;
  for (int i = 0; i < 20; i++) {
    sum += analogRead(JOY_Y_PIN);
    delay(10);
  }
  joyCenter = sum / 20;
  Serial.print("Joystick calibrated center: ");
  Serial.println(joyCenter);
}

int readJoyYSmoothed() {
  long sum = 0;
  for (int i = 0; i < 4; i++) {
    sum += analogRead(JOY_Y_PIN);
  }
  return sum / 4;
}

void drawMenu() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(20, 0);
  display.println("SETTINGS");
  display.drawFastHLine(0, 12, 128, SSD1306_WHITE);
  display.setCursor(0, 25);
  display.print(selectedMenu == 0 ? ">" : " ");
  display.print(" Set Upper Temp");
  display.setCursor(0, 45);
  display.print(selectedMenu == 1 ? ">" : " ");
  display.print(" Set Lower Temp");
  display.setCursor(0, 57);
  display.print("Hold: exit");
  display.display();
}

void drawEditScreen(const char* title, float value) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(10, 0);
  display.println(title);
  display.drawFastHLine(0, 12, 128, SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(25, 30);
  display.print(value, 1);
  display.setTextSize(1);
  display.setCursor(0, 57);
  display.print("Hold: exit");
  display.display();
}

void handleJoystickMenu() {
  if (menuState != MENU_OFF && millis() - lastMenuActivity > MENU_TIMEOUT_MS) {
    menuState = MENU_OFF;
  }

  if (joyBtnDown) {
    joyBtnDown = false;
    buttonDownAt = millis();
  }

  if (joyBtnUp) {
    joyBtnUp = false;
    unsigned long heldFor = millis() - buttonDownAt;
    lastMenuActivity = millis();
    ignoreAxisUntil = millis() + AXIS_IGNORE_MS;

    if (heldFor >= LONG_PRESS_MS) {
      menuState = MENU_OFF;   // long press, from anywhere: back to the face
    } else {
      switch (menuState) {
        case MENU_OFF:
          menuState = MENU_SELECT;
          break;
        case MENU_SELECT:
          if (selectedMenu == 0) menuState = MENU_EDIT_UPPER;
          else menuState = MENU_EDIT_LOWER;
          break;
        case MENU_EDIT_UPPER:
        case MENU_EDIT_LOWER:
          menuState = MENU_SELECT;
          break;
      }
    }
  }

  if (millis() < ignoreAxisUntil) return;
  if (millis() - lastJoyMove < JOY_REPEAT_MS) return;

  int joyY = readJoyYSmoothed();
  bool joyUp = joyY < (joyCenter - JOY_DEADZONE);
  bool joyDown = joyY > (joyCenter + JOY_DEADZONE);

  if (menuState == MENU_SELECT) {
    if (joyUp) {
      selectedMenu--;
      if (selectedMenu < 0) selectedMenu = 1;
      lastJoyMove = millis();
      lastMenuActivity = millis();
    }
    if (joyDown) {
      selectedMenu++;
      if (selectedMenu > 1) selectedMenu = 0;
      lastJoyMove = millis();
      lastMenuActivity = millis();
    }
  }

  if (menuState == MENU_EDIT_UPPER) {
    if (joyUp) { upperTempLimit += 0.5; lastJoyMove = millis(); lastMenuActivity = millis(); }
    if (joyDown) { upperTempLimit -= 0.5; lastJoyMove = millis(); lastMenuActivity = millis(); }
  }

  if (menuState == MENU_EDIT_LOWER) {
    if (joyUp) { lowerTempLimit += 0.5; lastJoyMove = millis(); lastMenuActivity = millis(); }
    if (joyDown) { lowerTempLimit -= 0.5; lastJoyMove = millis(); lastMenuActivity = millis(); }
  }
}

void stepServoToward(int target) {
  if (millis() - lastStepMs < STEP_INTERVAL_MS) return;
  lastStepMs = millis();
  if (currentAngle == target) return;
  currentAngle += (target > currentAngle) ? 1 : -1;
  cap.write(currentAngle);
}

// GardenSpine calls this whenever gm-01 publishes on REMOTE_TEMP_TOPIC.
// A GardenSpine payload includes a JSON field such as: "value":22.8
void handleRemoteTemperature(const char* topic, const char* message) {
  if (strcmp(topic, REMOTE_TEMP_TOPIC) != 0) return;

  const char* valueStart = strstr(message, "\"value\":");
  if (valueStart == nullptr) return;
  valueStart += 8;  // Skip the characters in "\"value\":".

  char* valueEnd;
  float receivedTemperature = strtof(valueStart, &valueEnd);
  if (valueEnd == valueStart) return;  // Reject missing or non-numeric values.

  remoteTempC = receivedTemperature;
  remoteMessageReceived = true;
  lastRemoteMessageMs = millis();

  Serial.print("Remote temperature: ");
  Serial.print(remoteTempC, 1);
  Serial.println(" C");
}

bool remoteTemperatureIsFresh() {
  return remoteMessageReceived &&
         (millis() - lastRemoteMessageMs <= REMOTE_MESSAGE_TIMEOUT_MS);
}

void drawMushroomFace(int centerY) {
  const int leftX = 40, rightX = 88, eyeR = 8;
  if (blinking) {
    display.drawFastHLine(leftX - eyeR, centerY, eyeR * 2, SSD1306_WHITE);
    display.drawFastHLine(rightX - eyeR, centerY, eyeR * 2, SSD1306_WHITE);
  } else {
    display.drawCircle(leftX, centerY, eyeR, SSD1306_WHITE);
    display.drawCircle(rightX, centerY, eyeR, SSD1306_WHITE);
    int pupilR = inWarning ? 2 : 4;
    int pupilYOffset = inWarning ? -2 : 0;
    display.fillCircle(leftX, centerY + pupilYOffset, pupilR, SSD1306_WHITE);
    display.fillCircle(rightX, centerY + pupilYOffset, pupilR, SSD1306_WHITE);
  }
  if (inWarning) {
    display.drawLine(leftX - 8, centerY - 11, leftX + 2, centerY - 14, SSD1306_WHITE);
    display.drawLine(rightX - 2, centerY - 14, rightX + 8, centerY - 11, SSD1306_WHITE);
  }
}

void drawScreen() {
  display.clearDisplay();
  drawMushroomFace(12);
  display.drawFastHLine(0, 25, 128, SSD1306_WHITE);

  if (inWarning) {
    display.drawBitmap(4, 30, flakeIcon, 16, 16, SSD1306_WHITE);
  } else {
    display.drawBitmap(4, 30, tempIcon, 16, 16, SSD1306_WHITE);
  }

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(26, 34);
  display.print("Temp: ");
  if (sensorOk) { display.print(lastTempC, 1); display.print(" C"); }
  else { display.print("--.- C"); }

  display.drawBitmap(4, 47, humIcon, 16, 16, SSD1306_WHITE);
  display.setCursor(26, 51);
  display.print("Hum:  ");
  if (sensorOk) { display.print(lastHumidity, 1); display.print(" %"); }
  else { display.print("-- %"); }

  display.display();
}

void setup() {
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  pinMode(JOY_SW_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(JOY_SW_PIN), joystickISR, CHANGE);
  Serial.begin(115200);

  cap.attach(SERVO_PIN);
  cap.write(SERVO_OPEN_ANGLE);  // Safe state on boot.

  dht.begin();
  delay(2000);

  spine.begin();
  bool listening = spine.subscribe(REMOTE_TEMP_TOPIC, handleRemoteTemperature);
  Serial.print("Subscribe to remote temperature: ");
  Serial.println(listening ? "ok" : "failed");

  Wire.begin(21, 22);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("SSD1306 allocation failed");
    while (true);
  }

  calibrateJoystick();
}

void loop() {
  spine.loop();
  handleJoystickMenu();

  if (!blinking && millis() - lastBlinkMs >= BLINK_INTERVAL_MS) {
    blinking = true;
    blinkStart = millis();
    lastBlinkMs = millis();
  }
  if (blinking && millis() - blinkStart >= BLINK_DURATION_MS) {
    blinking = false;
  }

  if (millis() - lastReading >= SAMPLE_MS) {
    lastReading = millis();
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    sensorOk = !(isnan(t) || isnan(h));

    if (sensorOk) {
      lastTempC = t;
      lastHumidity = h;

      // A warning is active whenever the temperature is outside the
      // user-selected range, including temperatures above the upper limit.
      inWarning = lastTempC < lowerTempLimit || lastTempC > upperTempLimit;
      setBuzzer(inWarning);
    } else {
      // Do not leave an old warning tone running after a failed sensor read.
      inWarning = false;
      setBuzzer(false);
    }
  }

  // Actuate the servo from this device's own most recent temperature.
  // The hand/vent opens outside the selected range and closes within it.
  // If the sensor has no valid current reading, stay in the safe/open state.
  bool localOutsideLimits = !sensorOk ||
                            lastTempC < lowerTempLimit ||
                            lastTempC > upperTempLimit;
  int servoTarget = localOutsideLimits
                    ? SERVO_OPEN_ANGLE
                    : SERVO_CLOSED_ANGLE;
  stepServoToward(servoTarget);

  if (spine.connected() && (millis() - lastPublishMs >= PUBLISH_MS || lastPublishMs == 0)) {
    lastPublishMs = millis();
    if (sensorOk) {
      spine.publish("temperature", lastTempC, "celsius");
      spine.publish("humidity", lastHumidity, "percent-rh");
      spine.publish("status", inWarning ? "warning" : "ok", "enum");
    }
  }

  if (millis() - lastDrawMs >= DRAW_INTERVAL_MS) {
    lastDrawMs = millis();
    if (menuState == MENU_OFF) drawScreen();
    else if (menuState == MENU_SELECT) drawMenu();
    else if (menuState == MENU_EDIT_UPPER) drawEditScreen("Upper Temp", upperTempLimit);
    else if (menuState == MENU_EDIT_LOWER) drawEditScreen("Lower Temp", lowerTempLimit);
  }
}
