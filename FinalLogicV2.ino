#include <Adafruit_NeoPixel.h>

#define LED_PIN 11
#define NUM_LEDS 400
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

// Ultrasonic
const int trig1 = 3, echo1 = 4;
const int trig2 = 12, echo2 = 13;

// Motor driver
const int AIN1 = 5, AIN2 = 6, BIN1 = 9, BIN2 = 10, SLP = 8;

////////// 🎛 ARTIST CONTROLS 🎛 //////////

// Distance behaviour
const int minDistance = 5;
const int maxDistance = 100;

// Death thresholds
const int deathDistance  = 12;
const int reviveDistance = 18;

// Motor feel
const int minMotorSpeed = 30;
const int maxMotorSpeed = 180;
const float motorSmooth = 0.8;   // higher = smoother/heavier

// Interaction sensitivity
const float distanceSmooth = 0.7;  // higher = calmer system

// Breathing feel
const float breathMultiplier = 0.08;  // global breathing speed
const int breathMin = 1;
const int breathMax = 8;

// Death feel
const float deathFadeSpeed = 0.9;  // closer to 1 = slower fade
const int deathBrightness = 40;    // gloomy white brightness

///////////////////////////////////////////

// State
float smoothedDistance = 100;
float smoothedSpeed = 0;
float breathPhase = 0;
float deathFade = 0;
bool coralDead = false;

void setup() {

  Serial.begin(115200);

  pinMode(trig1, OUTPUT); pinMode(echo1, INPUT);
  pinMode(trig2, OUTPUT); pinMode(echo2, INPUT);

  pinMode(AIN1, OUTPUT); pinMode(AIN2, OUTPUT);
  pinMode(BIN1, OUTPUT); pinMode(BIN2, OUTPUT);
  pinMode(SLP, OUTPUT);

  digitalWrite(SLP, HIGH);

  strip.begin();
  strip.show();
}

void loop() {

  long d1 = readUltrasonic(trig1, echo1);
  delay(40);
  long d2 = readUltrasonic(trig2, echo2);

  long active = getValidDistance(d1, d2);

  smoothedDistance = smoothedDistance * distanceSmooth + active * (1 - distanceSmooth);

  if (!coralDead && smoothedDistance < deathDistance) coralDead = true;
  if (coralDead && smoothedDistance > reviveDistance) coralDead = false;

  int targetSpeed = 0;

  if (!coralDead && smoothedDistance < maxDistance) {
    float d = constrain(smoothedDistance, minDistance, maxDistance);
    targetSpeed = map(d, maxDistance, minDistance, minMotorSpeed, maxMotorSpeed);
  }

  smoothedSpeed = smoothedSpeed * motorSmooth + targetSpeed * (1 - motorSmooth);

  driveMotors(coralDead ? 0 : (int)smoothedSpeed);

  updateCoralLight();

  // -------- SERIAL OUTPUT --------
Serial.print("S1:");
Serial.print(d1);
Serial.print("  S2:");
Serial.print(d2);

Serial.print("  Active:");
Serial.print(active);

Serial.print("  SmoothDist:");
Serial.print(smoothedDistance);

Serial.print("  MotorSpeed:");
Serial.print(smoothedSpeed);

Serial.print("  BreathPhase:");
Serial.print(breathPhase);

Serial.print("  DeathFade:");
Serial.print(deathFade);

Serial.print("  CoralDead:");
Serial.println(coralDead);


  delay(20);
}

long readUltrasonic(int trig, int echo) {
  digitalWrite(trig, LOW); delayMicroseconds(2);
  digitalWrite(trig, HIGH); delayMicroseconds(10);
  digitalWrite(trig, LOW);

  long duration = pulseIn(echo, HIGH, 30000);
  return (duration == 0) ? -1 : duration * 0.034 / 2;
}

long getValidDistance(long d1, long d2) {
  if (d1 <= 0 && d2 <= 0) return maxDistance;
  if (d1 <= 0) return d2;
  if (d2 <= 0) return d1;
  return min(d1, d2);
}

void driveMotors(int speed) {
  speed = (speed < 10) ? 0 : speed;
  analogWrite(AIN1, speed);
  analogWrite(AIN2, 0);
  analogWrite(BIN1, speed);
  analogWrite(BIN2, 0);
}

void updateCoralLight() {

  deathFade = coralDead
              ? (deathFade * deathFadeSpeed + (1 - deathFadeSpeed))
              : (deathFade * deathFadeSpeed);

  float d = constrain(smoothedDistance, minDistance, maxDistance);
  float breathSpeed = map(d, maxDistance, minDistance, breathMin, breathMax);

  breathPhase += breathSpeed * breathMultiplier;

  float breath = (sin(breathPhase) + 1.0) / 2.0;

  float baseBrightness = map(smoothedSpeed, 0, maxMotorSpeed, 10, 180);
  int aliveBrightness = baseBrightness * breath * (1.0 - deathFade);

  int gloomy = deathBrightness * deathFade;

  int r = aliveBrightness + gloomy;
  int g = aliveBrightness * 0.2 + gloomy;
  int b = aliveBrightness * 0.6 + gloomy;

  for (int i = 0; i < NUM_LEDS; i++)
    strip.setPixelColor(i, r, g, b);

  strip.show();
}
