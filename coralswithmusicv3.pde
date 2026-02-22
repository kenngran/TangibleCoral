import themidibus.*;
import processing.serial.*;

MidiBus midi;
MidiBus midi2;
Serial serialPort;

// --------- SETTINGS ---------
boolean USE_MOUSE = false;   // <<< SWITCH HERE

int SERIAL_INDEX = 2;
int MIDI_INDEX   = 4;
int MIDI_INDEX_2 = 5;
int MIDI_CHANNEL = 0;
int CC_NUMBER    = 1;
int CC_2         = 2;

int THRESHOLD = 100;
float smoothing = 0.6;
// ----------------------------

float smoothedValue = 0.5;
int serialValue = 0;

void setup() {
  size(400, 200);

  println("==== MIDI DEVICES ====");
  MidiBus.list();

  if (!USE_MOUSE) {
    println("==== SERIAL PORTS ====");
    String[] ports = Serial.list();
    printArray(ports);

    serialPort = new Serial(this, ports[SERIAL_INDEX], 115200);
  }

  midi  = new MidiBus(new java.lang.Object(), -1, MIDI_INDEX);
  midi2 = new MidiBus(new java.lang.Object(), -1, MIDI_INDEX_2);

  println("\nREADY.");
}

void draw() {
  background(30);
  fill(255);

  if (USE_MOUSE) {
    text("MODE: MOUSE", 20, 30);

    // Mouse X controls main CC
    float value = map(mouseX, 0, width, 0, 130);
    serialValue = int(map(mouseY, 0, height, 0, 130));

    processValue(value);
  } else {
    text("MODE: SERIAL", 20, 30);
  }

  text("Smoothed: " + nf(smoothedValue, 1, 2), 20, 60);
}

void serialEvent(Serial port) {
  if (USE_MOUSE) return;

  String incoming = port.readStringUntil('\n');

  if (incoming != null) {
    incoming = trim(incoming);
    println("RAW: " + incoming);

    float value = float(incoming);
    serialValue = int(value);

    processValue(value);
  }
}

// ------------------------------------------------
// Core logic (shared by mouse + serial)
// ------------------------------------------------
void processValue(float value) {

  // Smooth input
  smoothedValue = lerp(smoothedValue, value, smoothing);

  // Map to MIDI range
  int midiValue = int(map(smoothedValue, 0, 200, 0, 127));
  midiValue = constrain(midiValue, 0, 127);

  // ---- Threshold logic ----
  if (serialValue <= THRESHOLD) {

    int ccValue2 = int(map(serialValue, THRESHOLD, 0, 0, 127));
    ccValue2 = constrain(ccValue2, 0, 127);

    midi.sendControllerChange(MIDI_CHANNEL, CC_2, ccValue2);
    println("CC2:", ccValue2);

  } else {
    midi.sendControllerChange(MIDI_CHANNEL, CC_2, 0);
  }

  midi.sendControllerChange(MIDI_CHANNEL, CC_NUMBER, midiValue);
  println("MIDI:", midiValue);
}
