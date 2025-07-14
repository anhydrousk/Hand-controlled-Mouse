#include <Arduino.h>
#include <Wire.h>
#include <MPU6050.h>
#include <BleMouse.h>

MPU6050 mpu;
BleMouse bleMouse("Precision Air Mouse", "Maker", 100);

// IMU Configuration
int16_t ax, ay, az, gx, gy, gz;
const float GYRO_SENSITIVITY = 0.0022f;
const int GYRO_DEADZONE = 400;
float rawX = 0, rawY = 0;
float filteredX = 0, filteredY = 0;
const float SMOOTHING = 0.15f;
const int8_t X_AXIS_CORRECTION = -1;
const int8_t Y_AXIS_CORRECTION = 1;

// Scroll Configuration
const float SCROLL_SENSITIVITY = 0.15f;
const float SCROLL_SMOOTHING = 0.4f;
const float SCROLL_THRESHOLD = 0.2f;
float scrollX = 0, scrollY = 0;

// Button Configuration
const int LEFT_BUTTON_PIN = 26;
const int RIGHT_BUTTON_PIN = 27;
const int DEBOUNCE_DELAY = 50;
const int MULTI_CLICK_TIMEOUT = 400; // ms

// System State
struct {
  // Left button
  bool leftPressed = false;
  unsigned long lastLeftPress = 0;
  unsigned long lastLeftRelease = 0;
  unsigned long leftFirstClickTime = 0;
  int leftClickCount = 0;
  
  // Right button
  bool rightPressed = false;
  unsigned long lastRightPress = 0;
  unsigned long lastRightRelease = 0;
  unsigned long rightFirstClickTime = 0;
  int rightClickCount = 0;
  
  bool scrollMode = false;
  bool cursorFrozen = false;
} systemState;

void setup() {
  Serial.begin(115200);
  Wire.begin();
  Wire.setClock(400000);

  mpu.initialize();
  if (!mpu.testConnection()) {
    Serial.println("MPU6050 connection failed");
    while(1);
  }
  
  Serial.println("Calibrating... Keep device flat and still");
  delay(2000);
  mpu.CalibrateAccel(8);
  mpu.CalibrateGyro(8);
  Serial.println("Calibration complete!");

  pinMode(LEFT_BUTTON_PIN, INPUT_PULLUP);
  pinMode(RIGHT_BUTTON_PIN, INPUT_PULLUP);
  bleMouse.begin();
}

void handleLeftSingleClick() {
  if (!bleMouse.isConnected()) return;
  bleMouse.click(MOUSE_LEFT);
  Serial.println("Left single click");
}

void handleLeftDoubleClick() {
  if (!bleMouse.isConnected()) return;
  bleMouse.click(MOUSE_LEFT);
  delay(100);
  bleMouse.click(MOUSE_LEFT);
  Serial.println("Left double click");
}

void toggleScrollMode() {
  systemState.scrollMode = !systemState.scrollMode;
  scrollX = 0;
  scrollY = 0;
  Serial.println(systemState.scrollMode ? "Scroll mode ON (Triple-click)" : "Scroll mode OFF");
}

void toggleCursorFreeze() {
  systemState.cursorFrozen = !systemState.cursorFrozen;
  Serial.println(systemState.cursorFrozen ? "Cursor frozen (Double right-click)" : "Cursor unfrozen");
}

void processIMU() {
  if (systemState.cursorFrozen) return;
  
  mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);
  
  if (abs(gx) < GYRO_DEADZONE) gx = 0;
  if (abs(gy) < GYRO_DEADZONE) gy = 0;

  rawX = gx * GYRO_SENSITIVITY * X_AXIS_CORRECTION;
  rawY = gy * GYRO_SENSITIVITY * Y_AXIS_CORRECTION;
  
  if (systemState.scrollMode) {
    // In scroll mode, only vertical scrolling
    scrollY = (SCROLL_SMOOTHING * rawY * SCROLL_SENSITIVITY) + ((1-SCROLL_SMOOTHING) * scrollY);
    
    // Only send scroll commands if there's meaningful vertical movement
    if (abs(scrollY) > SCROLL_THRESHOLD) {
      bleMouse.move(0, 0, -scrollY, 0); // Vertical scrolling only (negative for natural direction)
    }
  } else {
    // Normal cursor movement
    filteredX = (SMOOTHING * rawX) + ((1-SMOOTHING) * filteredX);
    filteredY = (SMOOTHING * rawY) + ((1-SMOOTHING) * filteredY);
    bleMouse.move(filteredX, filteredY, 0);
  }
}

void processLeftButton() {
  bool currentLeftState = (digitalRead(LEFT_BUTTON_PIN) == LOW);
  
  if (currentLeftState && !systemState.leftPressed) {
    if (millis() - systemState.lastLeftPress > DEBOUNCE_DELAY) {
      systemState.leftPressed = true;
      systemState.lastLeftPress = millis();
      
      if (millis() - systemState.leftFirstClickTime > MULTI_CLICK_TIMEOUT) {
        systemState.leftClickCount = 0;
        systemState.leftFirstClickTime = millis();
      }
      systemState.leftClickCount++;
      
      Serial.print("Left button press. Count: ");
      Serial.println(systemState.leftClickCount);
    }
  }
  else if (!currentLeftState && systemState.leftPressed) {
    systemState.leftPressed = false;
    systemState.lastLeftRelease = millis();
  }
}

void processRightButton() {
  bool currentRightState = (digitalRead(RIGHT_BUTTON_PIN) == LOW);
  
  if (currentRightState && !systemState.rightPressed) {
    if (millis() - systemState.lastRightPress > DEBOUNCE_DELAY) {
      systemState.rightPressed = true;
      systemState.lastRightPress = millis();
      
      if (millis() - systemState.rightFirstClickTime > MULTI_CLICK_TIMEOUT) {
        systemState.rightClickCount = 0;
        systemState.rightFirstClickTime = millis();
      }
      systemState.rightClickCount++;
      
      Serial.print("Right button press. Count: ");
      Serial.println(systemState.rightClickCount);
    }
  }
  else if (!currentRightState && systemState.rightPressed) {
    systemState.rightPressed = false;
    systemState.lastRightRelease = millis();
  }
}

void handleLeftClicks() {
  if (!systemState.leftPressed && 
      millis() - systemState.leftFirstClickTime > MULTI_CLICK_TIMEOUT && 
      systemState.leftClickCount > 0) {
        
    switch(systemState.leftClickCount) {
      case 1:
        handleLeftSingleClick();
        break;
      case 2:
        handleLeftDoubleClick();
        break;
      case 3:
        toggleScrollMode(); // Triple-click toggles scroll mode
        break;
    }
    
    systemState.leftClickCount = 0;
    systemState.leftFirstClickTime = 0;
  }
}

void handleRightClicks() {
  if (!systemState.rightPressed && 
      millis() - systemState.rightFirstClickTime > MULTI_CLICK_TIMEOUT && 
      systemState.rightClickCount > 0) {
        
    switch(systemState.rightClickCount) {
      case 1:
        bleMouse.click(MOUSE_RIGHT);
        Serial.println("Right single click");
        break;
      case 2:
        toggleCursorFreeze(); // Double-click toggles freeze
        break;
    }
    
    systemState.rightClickCount = 0;
    systemState.rightFirstClickTime = 0;
  }
}

void loop() {
  if (!bleMouse.isConnected()) {
    delay(100);
    return;
  }

  processIMU();
  processLeftButton();
  processRightButton();
  handleLeftClicks();
  handleRightClicks();
  
  delay(8); // ~125Hz update rate
}