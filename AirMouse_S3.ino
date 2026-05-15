#include <HijelHID_BLEMouse.h> 
#include <Wire.h>
#include <MPU6050_tockn.h>
#include <Preferences.h>

// --- HARDWARE CONFIG ---
// SDA = GPIO 1, SCL = GPIO 2
// Left Click = Touch Pin 4
// Right Click = Touch Pin 5

MPU6050 mpu6050(Wire);
HijelBLEMouse bleMouse("Adivinayak S3 Mouse", "TCET-ECE");
Preferences prefs;

// --- 1. SETTINGS BASED ON YOUR CALIBRATION ---
// Trigger at 60k (well above the 25k baseline)
const long FOIL_THRESHOLD = 60000; 
// Release at 40k (ensures it definitely un-clicks)
const long FOIL_RELEASE = 40000;   
const int ACTION_TIME = 2000;      

// --- 2. STATE VARIABLES ---
enum SpeedMode { SLOW, NORMAL, FAST };
SpeedMode currentMode = NORMAL;
float modeSens = -0.38; 

unsigned long leftTouchStart = 0;
unsigned long rightTouchStart = 0;
unsigned long lastLeftRelease = 0;
int leftClickCount = 0;
bool leftHeld = false; 

float alpha = 0.15;        
float smoothX = 0, smoothY = 0, remainderX = 0, remainderY = 0; 
const float tremorThreshold = 1.5; 
const float acceleration = 1.2;

bool isScrolling = false;
bool isLatched = false;
int latchedSpeed = 0;
unsigned long lastScrollTick = 0;

void saveCalibration() {
  prefs.begin("airmouse", false);
  prefs.putFloat("xOff", mpu6050.getGyroXoffset());
  prefs.putFloat("yOff", mpu6050.getGyroYoffset());
  prefs.putFloat("zOff", mpu6050.getGyroZoffset());
  prefs.end();
  Serial.println(">> Calibration Saved to Flash");
}

void loadCalibration() {
  prefs.begin("airmouse", true);
  mpu6050.setGyroOffsets(prefs.getFloat("xOff", 0), prefs.getFloat("yOff", 0), prefs.getFloat("zOff", 0));
  prefs.end();
  Serial.println(">> Calibration Loaded from Flash");
}

void setup() {
  Serial.begin(115200);
  delay(2000); 
  Serial.println("\n--- [ SYSTEM STARTUP ] ---");

  Wire.begin(1, 2); 
  mpu6050.begin();

  prefs.begin("airmouse", true);
  if (!prefs.isKey("xOff")) {
    Serial.println("3. First Time Run: Calibrating Gyro. KEEP STEADY!");
    delay(2000); 
    mpu6050.calcGyroOffsets(true);
    saveCalibration();
  } else {
    loadCalibration();
  }
  
  bleMouse.begin();
  Serial.println("5. READY! Waiting for Bluetooth...");
}

void loop() {
  if (bleMouse.isConnected()) {
    mpu6050.update();

    smoothX = (alpha * mpu6050.getGyroZ()) + ((1.0 - alpha) * smoothX);
    smoothY = (alpha * mpu6050.getGyroX()) + ((1.0 - alpha) * smoothY);

    long curL = touchRead(4);
    long curR = touchRead(5);

    // --- MOVEMENT LOGIC ---
    if (!isScrolling) {
      float mX = (abs(smoothX) < tremorThreshold) ? 0 : smoothX;
      float mY = (abs(smoothY) < tremorThreshold) ? 0 : smoothY;
      
      if (mX != 0 || mY != 0) {
        float fX = (pow(abs(mX), acceleration) * ((mX > 0) ? 1 : -1) * modeSens) + remainderX;
        float fY = (pow(abs(mY), acceleration) * ((mY > 0) ? 1 : -1) * modeSens) + remainderY;
        int oX = (int)fX; int oY = (int)fY;
        remainderX = fX - oX; remainderY = fY - oY;
        if (oX != 0 || oY != 0) bleMouse.move(oX, oY);
      }

      // --- LEFT CLICK & DRAG ---
      if (curL > FOIL_THRESHOLD) {
        if (leftTouchStart == 0) leftTouchStart = millis();
        // Drag starts if held for 400ms
        if (millis() - leftTouchStart > 400 && !leftHeld) {
          bleMouse.press((MouseButton)1);
          leftHeld = true;
          Serial.println(">> Drag Started");
        }
      } 
      else if (curL < FOIL_RELEASE && leftTouchStart > 0) {
        if (leftHeld) {
          bleMouse.release((MouseButton)1);
          leftHeld = false;
          Serial.println(">> Drag Released");
        } else {
          bleMouse.click((MouseButton)1);
          Serial.println(">> Left Click");
          
          leftClickCount++;
          if (leftClickCount >= 3) {
            if (currentMode == SLOW) { currentMode = NORMAL; modeSens = -0.38; Serial.println("MODE: NORMAL"); }
            else if (currentMode == NORMAL) { currentMode = FAST; modeSens = -0.65; Serial.println("MODE: FAST"); }
            else { currentMode = SLOW; modeSens = -0.20; Serial.println("MODE: SLOW"); }
            leftClickCount = 0;
          }
          lastLeftRelease = millis();
        }
        leftTouchStart = 0;
      }
      if (millis() - lastLeftRelease > 500) leftClickCount = 0;
    }

    // --- RIGHT CLICK & SCROLL ---
    if (curR > FOIL_THRESHOLD) {
      if (rightTouchStart == 0) rightTouchStart = millis();
      unsigned long dur = millis() - rightTouchStart;
      if (dur > 300) {
        isScrolling = true;
        if (!isLatched) {
          float absT = abs(smoothY);
          if (absT > 14.0) {
            latchedSpeed = map(constrain(absT, 14, 55), 14, 55, 1, 4);
            if (smoothY < 0) latchedSpeed = -latchedSpeed;
            if (dur > 2000) { isLatched = true; Serial.println(">> Scroll Latched!"); }
          } else { latchedSpeed = 0; }
        }
        if (latchedSpeed != 0) {
          int interval = map(abs(latchedSpeed), 1, 4, 120, 50);
          if (millis() - lastScrollTick > interval) {
            bleMouse.scroll(latchedSpeed);
            lastScrollTick = millis();
          }
        }
      }
    } 
    else if (curR < FOIL_RELEASE && rightTouchStart > 0) {
      if (!isScrolling) {
        bleMouse.click((MouseButton)2);
        Serial.println(">> Right Click");
      }
      rightTouchStart = 0; 
      isScrolling = false; 
      isLatched = false; 
      latchedSpeed = 0;
    }
  }
  delay(10); 
}
