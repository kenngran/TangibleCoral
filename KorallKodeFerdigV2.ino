#include <FastLED.h>

// =========================================================
// FASTLED SETUP
// =========================================================
#define LED_PIN 11
#define NUM_LEDS 240
#define LED_TYPE WS2812B
#define COLOR_ORDER GRB
CRGB leds[NUM_LEDS];

// =========================================================
// MOTOR DRIVER PINS (from your motor sketch)
// =========================================================
const int AIN1 = 6, AIN2 = 7, BIN1 = 9, BIN2 = 10, SLP = 8;

// =========================================================
// HC-SR04 PINS
// =========================================================
const uint8_t trigPin = 2;
const uint8_t echoPin = 3;

const uint8_t trigPin2 = 4;
const uint8_t echoPin2 = 5;
// =========================================================
// DISTANCE / MAPPING (LED breathing values)
// =========================================================
const int DIST_DEAD_CM = 10;  // <10cm => dead mode: white 255
const int DIST_NEAR_CM = 0;  // <=20cm => max brightness/speed
const int DIST_FAR_CM = 150;  // >=150cm => min brightness/speed (200+ same)

const uint8_t MIN_BRIGHT = 100;
const uint8_t MAX_BRIGHT = 255;

const float MIN_BPM = 1.0f; //8
const float MAX_BPM = 36.0f; //24

//Linje 270 - Lys sensitivitet

// =========================================================
// MOTOR BEHAVIOR (USE THESE EXACT LEVELS from your motor sketch)
// =========================================================
const int minDistance = 50;     // near for motors
const int maxDistance = 150;    // far for motors
const int deathDistance = 10;   // dead threshold
const int reviveDistance = 15;  // hysteresis for revive

const int minMotorSpeed = 60;
const int maxMotorSpeed = 250;
const float motorSmooth = 0.8f;  // smoothing (0..1), higher = smoother/slower

// Motor startup boost
const int startBoost = 255;  // full power kick
const int boostTime = 500;   // milliseconds
bool motorWasStopped = true;
unsigned long motorStartTime = 0;

// Smoothed motor speed
float smoothedSpeed = (float)minMotorSpeed;

// Dead state with hysteresis
bool coralDead = false;

// =========================================================
// LED OUTPUT THROTTLE
// =========================================================
const uint16_t SHOW_INTERVAL_MS = 40;  // ~40 FPS max output

// =========================================================
// ULTRASONIC TIMING
// =========================================================
const uint32_t PING_PERIOD_US = 100000;                                     // 10 Hz
const uint32_t ECHO_WINDOW_US = (uint32_t)(DIST_FAR_CM * 58.3f + 2500.0f);  // cap wait

// =========================================================
// COLORS
// =========================================================
CRGB colorPurple = CRGB(128, 0, 255);
CRGB colorPink = CRGB(255, 0, 180);

// =========================================================
// HELPERS
// =========================================================
static inline float echoUsToCm(uint32_t echoUs) {
  return (echoUs * 0.0343f) * 0.5f;
}
static inline float clampf(float x, float lo, float hi) {
  if (x < lo) return lo;
  if (x > hi) return hi;
  return x;
}

// Map distance to LED targets (brightness + bpm)
void computeTargetsFromDistance(float dCm, uint8_t &targetBright, float &targetBpm) {
  float d = clampf(dCm, (float)DIST_NEAR_CM, (float)DIST_FAR_CM);
  float t = ((float)DIST_FAR_CM - d) / (float)(DIST_FAR_CM - DIST_NEAR_CM);  // 0 far -> 1 near
  t = clampf(t, 0.0f, 1.0f);

  targetBright = (uint8_t)(MIN_BRIGHT + t * (MAX_BRIGHT - MIN_BRIGHT) + 0.5f);
  targetBpm = MIN_BPM + t * (MAX_BPM - MIN_BPM);
}

// =========================================================
// ULTRASONIC via attachInterrupt on echoPin
// =========================================================
volatile uint32_t echoRiseUs = 0;
volatile uint32_t echoFallUs = 0;
volatile bool echoSeenRise = false;
volatile bool echoPulseDone = false;

bool pingActive = false;
uint32_t pingStartUs = 0;

float distanceCm = (float)DIST_FAR_CM;
bool hasNewDistance = false;

bool ultrasonicBusy() {
  return pingActive;
}

// ISR: track rising and falling edges on echoPin
void echoIsrChange() {
  bool high = (digitalRead(echoPin) == HIGH);
  uint32_t nowUs = micros();

  if (!pingActive) return;

  if (high) {
    echoRiseUs = nowUs;
    echoSeenRise = true;
  } else {
    if (echoSeenRise) {
      echoFallUs = nowUs;
      echoPulseDone = true;
    }
  }
}

void startPing() {
  echoSeenRise = false;
  echoPulseDone = false;
  echoRiseUs = 0;
  echoFallUs = 0;

  pingActive = true;
  pingStartUs = micros();

  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
}

void updateUltrasonic() {
  static uint32_t lastPingUs = 0;
  uint32_t nowUs = micros();

  if (!pingActive) {
    if (nowUs - lastPingUs >= PING_PERIOD_US) {
      lastPingUs = nowUs;
      startPing();
    }
    return;
  }

  if (echoPulseDone) {
    pingActive = false;

    uint32_t widthUs = (echoFallUs >= echoRiseUs) ? (echoFallUs - echoRiseUs) : 0;
    float cm = echoUsToCm(widthUs);

    // clamp far; 200+ behaves exactly like far
    cm = clampf(cm, 0.0f, (float)DIST_FAR_CM);

    distanceCm = cm;
    hasNewDistance = true;
    return;
  }

  // Timeout -> treat as far
  if (nowUs - pingStartUs >= ECHO_WINDOW_US) {
    pingActive = false;
    distanceCm = (float)DIST_FAR_CM;
    hasNewDistance = true;
  }
}

// =========================================================
// MOTOR CONTROL
// =========================================================
void driveMotors(int speed) {
  // speed expected 0..255
  speed = (speed < 0) ? 0 : (speed > 255 ? 255 : speed);

  if (speed == 0) {
    motorWasStopped = true;

    analogWrite(AIN1, 0);
    analogWrite(AIN2, 0);
    analogWrite(BIN1, 0);
    analogWrite(BIN2, 0);
    return;
  }

  // transition from stopped -> moving
  if (motorWasStopped) {
    motorStartTime = millis();
    motorWasStopped = false;
  }

  // startup boost
  if (millis() - motorStartTime < (unsigned long)boostTime) {
    analogWrite(AIN1, startBoost);
    analogWrite(AIN2, 0);
    analogWrite(BIN1, startBoost);
    analogWrite(BIN2, 0);
  } else {
    analogWrite(AIN1, speed);
    analogWrite(AIN2, 0);
    analogWrite(BIN1, speed);
    analogWrite(BIN2, 0);
  }
}

int computeMotorTargetSpeed(float dCm) {
  // Dead handled outside, but safe anyway:
  if (dCm < deathDistance) return 0;

  // Clamp distance for motor mapping to your min/maxDistance
  float d = clampf(dCm, (float)minDistance, (float)maxDistance);

  // Map: far -> slow, near -> fast
  // when d=maxDistance => minMotorSpeed
  // when d=minDistance => maxMotorSpeed
  float t = ((float)maxDistance - d) / (float)(maxDistance - minDistance);  // 0 far -> 1 near
  t = clampf(t, 0.0f, 1.0f);

  float speed = (float)minMotorSpeed + t * (float)(maxMotorSpeed - minMotorSpeed);
  return (int)(speed + 0.5f);
}

// =========================================================
// BREATH CONTROLLER (unchanged)
// =========================================================
struct BreathController {
  float pendingMaxBright = (float)MIN_BRIGHT;
  float pendingBpm = MIN_BPM;

  float cycleMaxBright = (float)MIN_BRIGHT;
  float cycleBpm = MIN_BPM;

  float brightEasePerCycle = 0.22f;
  float bpmEasePerCycle = 0.18f;

  uint16_t phase = 0;

  uint8_t lastOut = 255;
  uint32_t lastShowMs = 0;
  uint32_t lastMs = 0;
} breath;

void updateBreath() {
  uint32_t nowMs = millis();
  uint32_t dtMs = nowMs - breath.lastMs;
  if (dtMs == 0) dtMs = 1;
  breath.lastMs = nowMs;

  // 🔥 Smooth continuously instead of per cycle
  //if too twitchy reduce
  const float brightFollow = 0.08f;   // responsiveness (0.05–0.15 good)
  const float bpmFollow = 0.12f;

  breath.cycleMaxBright += (breath.pendingMaxBright - breath.cycleMaxBright) * brightFollow;
  breath.cycleBpm += (breath.pendingBpm - breath.cycleBpm) * bpmFollow;

  breath.cycleMaxBright = clampf(breath.cycleMaxBright, (float)MIN_BRIGHT, (float)MAX_BRIGHT);
  breath.cycleBpm = clampf(breath.cycleBpm, MIN_BPM, MAX_BPM);

  // Phase advance
  float cyclesPerMs = breath.cycleBpm / 60000.0f;
  uint32_t inc = (uint32_t)(65536.0f * cyclesPerMs * (float)dtMs);
  breath.phase += (uint16_t)inc;

  uint8_t s = sin8((uint8_t)(breath.phase >> 8));

  const uint8_t floorPct = 26;
  uint8_t breathLevel = lerp8by8(floorPct, 255, s);

  uint8_t outBright = (uint8_t)((breath.cycleMaxBright * (float)breathLevel) / 255.0f + 0.5f);

  CRGB c = blend(colorPurple, colorPink, s);
  c.nscale8_video(outBright);

  if ((nowMs - breath.lastShowMs) >= SHOW_INTERVAL_MS && !ultrasonicBusy()) {
    fill_solid(leds, NUM_LEDS, c);
    FastLED.show();
    breath.lastShowMs = nowMs;
  }
}


// =========================================================
// SETUP / LOOP
// =========================================================
void setup() {
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  digitalWrite(trigPin, LOW);

  // Motors
  pinMode(AIN1, OUTPUT);
  pinMode(AIN2, OUTPUT);
  pinMode(BIN1, OUTPUT);
  pinMode(BIN2, OUTPUT);
  pinMode(SLP, OUTPUT);
  digitalWrite(SLP, HIGH);

  // Stop motors initially
  driveMotors(0);

  // LEDs
  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.clear(true);
  FastLED.show();

  Serial.begin(115200);
  //Serial.println("Starting... (echo on D3 interrupt, trig on D2) + Motors enabled");

  int irq = digitalPinToInterrupt(echoPin);
  if (irq == NOT_AN_INTERRUPT) {
    //Serial.println("ERROR: echoPin does not support external interrupt on this board.");
  } else {
    attachInterrupt(irq, echoIsrChange, CHANGE);
    //Serial.print("Attached interrupt number: ");
    //Serial.println(irq);
  }
}

void loop() {
  updateUltrasonic();

  if (hasNewDistance) {
    hasNewDistance = false;

    float d = distanceCm;
    if (d > DIST_FAR_CM) d = (float)DIST_FAR_CM;

    Serial.print("Distance(cm): ");
    Serial.println(d);

    // ---------------- DEAD/REVIVE LOGIC (for motors + LED dead mode) ----------------
    if (!coralDead && d < (float)deathDistance) coralDead = true;
    if (coralDead && d > (float)reviveDistance) coralDead = false;

    //Serial.print("coralDead: ");
    //Serial.println(coralDead ? "true" : "false");

    // ---------------- MOTOR TARGETS ----------------
    int targetSpeed = coralDead ? 0 : computeMotorTargetSpeed(d);

    // Smooth motor speed
    smoothedSpeed = smoothedSpeed * motorSmooth + (float)targetSpeed * (1.0f - motorSmooth);
    int finalSpeed = coralDead ? 0 : (int)(smoothedSpeed + 0.5f);
/*
    Serial.print("Motor targetSpeed=");
    Serial.print(targetSpeed);
    Serial.print(" smoothedSpeed=");
    Serial.print(smoothedSpeed, 2);
    Serial.print(" finalSpeed=");
    Serial.println(finalSpeed);
*/
    driveMotors(finalSpeed);

    // ---------------- LED TARGETS (only when not dead) ----------------
    if (!coralDead) {
      uint8_t tBright;
      float tBpm;
      computeTargetsFromDistance(d, tBright, tBpm);

      breath.pendingMaxBright = (float)tBright;
      breath.pendingBpm = tBpm;
/*
      Serial.print("Pending LED targets -> Bright=");
      Serial.print(tBright);
      Serial.print(" BPM=");
      Serial.println(tBpm, 2);
      */
    } else {
      Serial.println("Dead mode: motors stopped + LEDs white");
    }
  }

  // DEAD MODE output (LEDs white + motors already stopped by logic)
  if (coralDead) {
    static uint32_t lastDeadShow = 0;
    uint32_t nowMs = millis();
    if ((nowMs - lastDeadShow) >= SHOW_INTERVAL_MS && !ultrasonicBusy()) {
      FastLED.setBrightness(255);
      fill_solid(leds, NUM_LEDS, CRGB::White);
      FastLED.show();
      lastDeadShow = nowMs;
      Serial.println("SHOW DEAD MODE (white 255)");
    }
    return;
  }

  // Normal breathing LEDs
  updateBreath();

  // extra ultrasonic tick
  updateUltrasonic();
}