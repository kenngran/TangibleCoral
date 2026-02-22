#include <Adafruit_NeoPixel.h>

#define LED_PIN 11
#define NUM_LEDS 400
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

// Ultrasonic
const int trig1 = 3, echo1 = 2;
const int trig2 = 4, echo2 = 5;

// Motor driver
const int AIN1 = 6, AIN2 = 7, BIN1 = 9, BIN2 = 10, SLP = 8;

////////// 🎛 ARTIST CONTROLS 🎛 //////////

// Distance behaviour
const int minDistance = 50;
const int maxDistance = 100;

// Death thresholds
const int deathDistance  = 10;
const int reviveDistance = 15;

// Motor feel
const int minMotorSpeed = 70;
const int maxMotorSpeed = 200;
const float motorSmooth = 0.8;

// Interaction sensitivity
const float distanceSmooth = 0.7;

// Breathing feel
const float breathMultiplier = 0.3;
const int breathMin = 10;
const int breathMax = 12;

///////////////////////////////////////////

// State
float smoothedDistance = 100;
float smoothedSpeed = minMotorSpeed;
float breathPhase = 0;
bool coralDead = false;

// Motor startup boost state
bool motorWasStopped = true;
unsigned long motorStartTime = 2000;

void setup() {

  Serial.begin(115200);

  pinMode(trig1, OUTPUT); pinMode(echo1, INPUT);
  pinMode(trig2, OUTPUT); pinMode(echo2, INPUT);

  pinMode(AIN1, OUTPUT); pinMode(AIN2, OUTPUT);
  pinMode(BIN1, OUTPUT); pinMode(BIN2, OUTPUT);
  pinMode(SLP, OUTPUT);

  digitalWrite(SLP, HIGH);

  strip.begin();
  strip.setBrightness(255);
  strip.show();
}

void loop() {

  long d1 = readUltrasonic(trig1, echo1);
  delay(5);
  long d2 = readUltrasonic(trig2, echo2);

  long active = getValidDistance(d1, d2);

  //Serial.println(active);

  // Smooth distance
  smoothedDistance =
      smoothedDistance * distanceSmooth
    + active * (1 - distanceSmooth);

  // Death logic
  if (!coralDead && smoothedDistance < deathDistance)
      coralDead = true;

  if (coralDead && smoothedDistance > reviveDistance)
      coralDead = false;

  // -------- MOTOR --------
  int targetSpeed = 0;

  if (!coralDead) {
    float d = constrain(smoothedDistance, minDistance, maxDistance);

    targetSpeed = map(d,
                      maxDistance, minDistance,
                      minMotorSpeed, maxMotorSpeed);
  }

  smoothedSpeed =
      smoothedSpeed * motorSmooth
    + targetSpeed * (1 - motorSmooth);

  int finalSpeed = coralDead ? 0 : (int)smoothedSpeed;

  driveMotors(finalSpeed);

  Serial.println(active);

  updateCoralLight();

  delay(5);
}

long readUltrasonic(int trig, int echo) {
  digitalWrite(trig, LOW); delayMicroseconds(2);
  digitalWrite(trig, HIGH); delayMicroseconds(10);
  digitalWrite(trig, LOW);

  long duration = pulseIn(echo, HIGH, 30000);
  return (duration == 0) ? maxDistance : duration * 0.034 / 2;
}

long getValidDistance(long d1, long d2) {
  if (d1 <= 0 && d2 <= 0) return maxDistance;
  if (d1 <= 0) return d2;
  if (d2 <= 0) return d1;
  return min(d1, d2);
}

void driveMotors(int speed) {

  const int startBoost = 255;  // full power kick
  const int boostTime = 500;    // milliseconds

  if (speed == 0) {
    motorWasStopped = true;
    analogWrite(AIN1, 0);
    analogWrite(AIN2, 0);
    analogWrite(BIN1, 0);
    analogWrite(BIN2, 0);
    return;
  }

  // Detect transition from stopped → moving
  if (motorWasStopped) {
    motorStartTime = millis();
    motorWasStopped = false;
  }

  // Apply startup boost
  if (millis() - motorStartTime < boostTime) {
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

void updateCoralLight() {

  // ===== DEAD = PURE WHITE =====
  if (coralDead) {

    uint32_t white = strip.Color(255, 255, 255);

    for (int i = 0; i < NUM_LEDS; i++)
      strip.setPixelColor(i, white);

    strip.show();
    return;
  }

  // ===== ALIVE =====
  float d = constrain(smoothedDistance, minDistance, maxDistance);
  float breathSpeed = map(d, maxDistance, minDistance, breathMin, breathMax);

  breathPhase += breathSpeed * breathMultiplier;

  float breath = (sin(breathPhase) + 1.0) / 2.0;

  float baseBrightness = map(smoothedSpeed,
                             minMotorSpeed,
                             maxMotorSpeed,
                             20, 180);

  int aliveBrightness = baseBrightness * breath;

  int r = aliveBrightness;
  int g = aliveBrightness * 0.2;
  int b = aliveBrightness * 0.6;

  for (int i = 0; i < NUM_LEDS; i++)
    strip.setPixelColor(i, strip.Color(r, g, b));

  strip.show();
}
