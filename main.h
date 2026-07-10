#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "FluxGarage_RoboEyes.h"

// OLED Display Settings
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

// Pin Definitions
#define RED_TOUCH_PIN 4     // TTP223 (top/forehead)
#define LEFT_TOUCH_PIN 5    // TTP223B (left ear)
#define RIGHT_TOUCH_PIN 6   // TTP223B (right ear)

// I2C Addresses for OLEDs
#define OLED_LEFT_ADDR 0x3C
#define OLED_RIGHT_ADDR 0x3D

// Touch Detection Settings
#define DEBOUNCE_DELAY 50
#define LONG_PRESS_TIME 2000
#define DOUBLE_TAP_TIME 400
#define COMBINED_GESTURE_TIME 300

// Initialize OLED displays
Adafruit_SSD1306 display_left(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Adafruit_SSD1306 display_right(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Initialize RoboEyes for both displays
RoboEyes<Adafruit_SSD1306> eyesLeft(display_left);
RoboEyes<Adafruit_SSD1306> eyesRight(display_right);

// Touch sensor states
struct TouchState {
  bool lastState = false;
  bool currentState = false;
  unsigned long lastChangeTime = 0;
  unsigned long pressStartTime = 0;
  bool isPressed = false;
  int tapCount = 0;
  unsigned long lastTapTime = 0;
  bool isLongPress = false;
};

TouchState redTouch, leftTouch, rightTouch;

// Emotion states (matches library exactly)
enum Emotion {
  EMOTION_DEFAULT,
  EMOTION_HAPPY,
  EMOTION_ANGRY,
  EMOTION_TIRED,
  EMOTION_CONFUSED,
  EMOTION_LAUGH,
  EMOTION_SWEAT,
  EMOTION_CURIOUS,
  EMOTION_CYCLOPS
};

Emotion currentEmotion = EMOTION_DEFAULT;
bool emotionChanged = false;
unsigned long emotionStartTime = 0;
unsigned long currentTime = 0;

// Combined gesture tracking
bool leftEarTouched = false;
bool rightEarTouched = false;
bool redTouched = false;
unsigned long combinedGestureStart = 0;

void setup() {
  Serial.begin(115200);
  
  // Initialize touch pins
  pinMode(RED_TOUCH_PIN, INPUT_PULLUP);
  pinMode(LEFT_TOUCH_PIN, INPUT_PULLUP);
  pinMode(RIGHT_TOUCH_PIN, INPUT_PULLUP);
  
  // Initialize left OLED
  if(!display_left.begin(SSD1306_SWITCHCAPVCC, OLED_LEFT_ADDR)) {
    Serial.println(F("SSD1306 left allocation failed"));
    for(;;);
  }
  
  // Initialize right OLED
  if(!display_right.begin(SSD1306_SWITCHCAPVCC, OLED_RIGHT_ADDR)) {
    Serial.println(F("SSD1306 right allocation failed"));
    for(;;);
  }
  
  // Setup displays
  display_left.clearDisplay();
  display_left.display();
  display_right.clearDisplay();
  display_right.display();
  
  // Initialize RoboEyes
  eyesLeft.begin(SCREEN_WIDTH, SCREEN_HEIGHT, 50);
  eyesRight.begin(SCREEN_WIDTH, SCREEN_HEIGHT, 50);
  
  // Set initial colors (monochrome)
  eyesLeft.setDisplayColors(0, 1);
  eyesRight.setDisplayColors(0, 1);
  
  // Open eyes initially
  eyesLeft.open();
  eyesRight.open();
  
  // Set default position
  eyesLeft.setPosition(DEFAULT);
  eyesRight.setPosition(DEFAULT);
  
  // Enable auto blink
  eyesLeft.setAutoblinker(true, 2, 3);
  eyesRight.setAutoblinker(true, 2, 3);
  
  // Enable idle mode (eyes looking around)
  eyesLeft.setIdleMode(true, 2, 4);
  eyesRight.setIdleMode(true, 2, 4);
  
  Serial.println("RoboEyes initialized!");
  printControls();
}

void loop() {
  currentTime = millis();
  
  // Read all touch sensors
  readTouchSensors();
  
  // Process touch events
  processTouchEvents();
  
  // Update eyes
  eyesLeft.update();
  eyesRight.update();
  
  // Handle emotion timeouts
  handleEmotionTimeout();
}

void readTouchSensors() {
  // TTP223 outputs HIGH when touched (active HIGH)
  updateTouchState(redTouch, RED_TOUCH_PIN);
  updateTouchState(leftTouch, LEFT_TOUCH_PIN);
  updateTouchState(rightTouch, RIGHT_TOUCH_PIN);
}

void updateTouchState(TouchState &state, int pin) {
  bool reading = digitalRead(pin);
  
  if (reading != state.lastState) {
    state.lastChangeTime = currentTime;
  }
  
  if ((currentTime - state.lastChangeTime) > DEBOUNCE_DELAY) {
    if (reading != state.currentState) {
      state.currentState = reading;
      
      if (state.currentState) {
        // Touch started
        state.isPressed = true;
        state.pressStartTime = currentTime;
        state.isLongPress = false;
        state.tapCount++;
        
        // Check for double tap
        if (state.tapCount >= 2 && 
            (currentTime - state.lastTapTime) <= DOUBLE_TAP_TIME) {
          // Double tap detected
          handleDoubleTap(pin);
          state.tapCount = 0;
        } else if (state.tapCount >= 2) {
          // Too slow for double tap
          state.tapCount = 1;
        }
        
        state.lastTapTime = currentTime;
      } else {
        // Touch ended
        state.isPressed = false;
        
        // Check for single tap (not double tap)
        if (state.tapCount == 1 && 
            !state.isLongPress &&
            (currentTime - state.lastTapTime) < DOUBLE_TAP_TIME) {
          // Single tap detected
          handleSingleTap(pin);
          state.tapCount = 0;
        }
        
        state.isLongPress = false;
        
        // Check for combined gestures when touch ends
        checkCombinedGestures();
      }
    }
  }
  
  state.lastState = reading;
  
  // Check for long press
  if (state.isPressed && !state.isLongPress) {
    if ((currentTime - state.pressStartTime) >= LONG_PRESS_TIME) {
      state.isLongPress = true;
      handleLongPress(pin);
    }
  }
}

void processTouchEvents() {
  // Track which touches are active for combined gestures
  if (leftTouch.isPressed) {
    leftEarTouched = true;
    combinedGestureStart = currentTime;
  }
  
  if (rightTouch.isPressed) {
    rightEarTouched = true;
    combinedGestureStart = currentTime;
  }
  
  if (redTouch.isPressed) {
    redTouched = true;
    combinedGestureStart = currentTime;
  }
  
  // Check for hold + touch combinations (anger)
  if (leftTouch.isPressed && rightTouch.isPressed) {
    // Left + Right = CURIOSITY
    if (!emotionChanged || currentEmotion != EMOTION_CURIOUS) {
      setEmotion(EMOTION_CURIOUS);
      Serial.println("Gesture: Left + Right = CURIOSITY");
    }
  }
  
  // Check for all three touches
  if (redTouch.isPressed && leftTouch.isPressed && rightTouch.isPressed) {
    // All three = CYCLOPS
    if (!emotionChanged || currentEmotion != EMOTION_CYCLOPS) {
      setEmotion(EMOTION_CYCLOPS);
      Serial.println("Gesture: All Three = CYCLOPS");
    }
  }
  
  // Reset combined gesture flags if no touches
  if (!leftTouch.isPressed && !rightTouch.isPressed && !redTouch.isPressed) {
    if ((currentTime - combinedGestureStart) > COMBINED_GESTURE_TIME) {
      leftEarTouched = false;
      rightEarTouched = false;
      redTouched = false;
    }
  }
}

void checkCombinedGestures() {
  // This is called when a touch ends
  // The actual combined gesture handling is in processTouchEvents()
}

void handleSingleTap(int pin) {
  switch(pin) {
    case RED_TOUCH_PIN:
      setEmotion(EMOTION_TIRED);
      Serial.println("Red Single Tap: TIRED");
      break;
      
    case LEFT_TOUCH_PIN:
      setEmotion(EMOTION_HAPPY);
      Serial.println("Left Single Tap: HAPPY");
      break;
      
    case RIGHT_TOUCH_PIN:
      setEmotion(EMOTION_ANGRY);
      Serial.println("Right Single Tap: ANGRY");
      break;
  }
}

void handleDoubleTap(int pin) {
  switch(pin) {
    case RED_TOUCH_PIN:
      setEmotion(EMOTION_SWEAT);
      Serial.println("Red Double Tap: SWEAT");
      break;
      
    case LEFT_TOUCH_PIN:
      setEmotion(EMOTION_LAUGH);
      Serial.println("Left Double Tap: LAUGH");
      break;
      
    case RIGHT_TOUCH_PIN:
      setEmotion(EMOTION_CONFUSED);
      Serial.println("Right Double Tap: CONFUSED");
      break;
  }
}

void handleLongPress(int pin) {
  if (pin == RED_TOUCH_PIN) {
    setEmotion(EMOTION_DEFAULT);
    Serial.println("Red Long Press: RESET to DEFAULT");
  } else if (pin == LEFT_TOUCH_PIN) {
    // Left hold + Right touch = ANGRY (handled in processTouchEvents)
    if (rightTouch.isPressed) {
      setEmotion(EMOTION_ANGRY);
      Serial.println("Left Hold + Right Touch = ANGRY");
    }
  } else if (pin == RIGHT_TOUCH_PIN) {
    // Right hold + Left touch = ANGRY (handled in processTouchEvents)
    if (leftTouch.isPressed) {
      setEmotion(EMOTION_ANGRY);
      Serial.println("Right Hold + Left Touch = ANGRY");
    }
  }
}

void setEmotion(Emotion emotion) {
  currentEmotion = emotion;
  emotionChanged = true;
  emotionStartTime = currentTime;
  
  // Apply emotion to both eyes
  applyEmotionToEyes(eyesLeft, emotion);
  applyEmotionToEyes(eyesRight, emotion);
}

void applyEmotionToEyes(RoboEyes<Adafruit_SSD1306> &eyes, Emotion emotion) {
  // Reset all features first
  eyes.setHFlicker(false);
  eyes.setVFlicker(false);
  eyes.setSweat(false);
  eyes.setCyclops(false);
  eyes.setCuriosity(false);
  eyes.setAutoblinker(true, 2, 3);
  eyes.setIdleMode(true, 2, 4);
  
  switch(emotion) {
    case EMOTION_DEFAULT:
      eyes.setMood(DEFAULT);
      eyes.open();
      break;
      
    case EMOTION_HAPPY:
      eyes.setMood(HAPPY);
      eyes.open();
      break;
      
    case EMOTION_ANGRY:
      eyes.setMood(ANGRY);
      eyes.setHFlicker(true, 2); // Add angry shake
      eyes.open();
      break;
      
    case EMOTION_TIRED:
      eyes.setMood(TIRED);
      eyes.setAutoblinker(false); // No blinking when tired
      eyes.setIdleMode(false);
      eyes.open();
      break;
      
    case EMOTION_CONFUSED:
      eyes.setMood(DEFAULT);
      eyes.anim_confused(); // Trigger confusion animation
      eyes.open();
      break;
      
    case EMOTION_LAUGH:
      eyes.setMood(HAPPY);
      eyes.anim_laugh(); // Trigger laugh animation
      eyes.open();
      break;
      
    case EMOTION_SWEAT:
      eyes.setMood(DEFAULT);
      eyes.setSweat(true);
      eyes.open();
      break;
      
    case EMOTION_CURIOUS:
      eyes.setMood(DEFAULT);
      eyes.setCuriosity(true);
      eyes.setIdleMode(true, 1, 3);
      eyes.open();
      break;
      
    case EMOTION_CYCLOPS:
      eyes.setMood(DEFAULT);
      eyes.setCyclops(true);
      eyes.setIdleMode(true, 2, 3);
      eyes.open();
      break;
  }
}

void handleEmotionTimeout() {
  // Return to default after 5 seconds for temporary emotions
  if (emotionChanged) {
    if ((currentTime - emotionStartTime) > 5000) {
      // Reset to default for temporary emotions
      if (currentEmotion == EMOTION_CONFUSED || 
          currentEmotion == EMOTION_LAUGH || 
          currentEmotion == EMOTION_SWEAT ||
          currentEmotion == EMOTION_CURIOUS) {
        setEmotion(EMOTION_DEFAULT);
        Serial.println("Returning to DEFAULT after animation");
      }
    }
  }
}

void printControls() {
  Serial.println("\n=== RoboEyes Touch Controls ===");
  Serial.println("\nRED Touch (Forehead):");
  Serial.println("  • Single Tap: TIRED 😴");
  Serial.println("  • Double Tap: SWEAT 😰");
  Serial.println("  • Long Press: RESET to DEFAULT");
  Serial.println("\nLEFT Touch (Left Ear):");
  Serial.println("  • Single Tap: HAPPY 😊");
  Serial.println("  • Double Tap: LAUGH 😂");
  Serial.println("  • Hold + Right Touch: ANGRY 😡");
  Serial.println("\nRIGHT Touch (Right Ear):");
  Serial.println("  • Single Tap: ANGRY 😠");
  Serial.println("  • Double Tap: CONFUSED 🤔");
  Serial.println("  • Hold + Left Touch: ANGRY 😡");
  Serial.println("\nCombined Gestures:");
  Serial.println("  • Left + Right: CURIOSITY 👀");
  Serial.println("  • All Three: CYCLOPS 👁️");
  Serial.println("===============================\n");
}
