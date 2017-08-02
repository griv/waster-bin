/**

   pvi waster bin
   v1.0
   Steve Berrick
   2016

   - triggers crisis audio file to play when a bin is opened - proximity sensor
   - pwms out to led strip
   - randomly plays audio file after no interaction (demo)
   - uses 3 volume pots - crisis internal (L), crisis external (R) and demo (R)

**/

// REMEMBER TO SET THIS FOR EACH BIN!!!!!!!!!
#define CRISIS   "IDENTITY"


#include <Audio.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <SerialFlash.h>
#include <FadeLed.h>

// GUItool: begin automatically generated code
AudioPlaySdWav           wavPlayer;
AudioEffectFade          fadeR;
AudioEffectFade          fadeL;
AudioMixer4              mixerR;
AudioMixer4              mixerL;
AudioOutputI2S           audioOutput;
AudioConnection          patchCord1(wavPlayer, 0, fadeL, 0);
AudioConnection          patchCord2(wavPlayer, 1, fadeR, 0);
AudioConnection          patchCord3(fadeR, 0, mixerR, 0);
AudioConnection          patchCord4(fadeL, 0, mixerL, 0);
AudioConnection          patchCord5(mixerR, 0, audioOutput, 1);
AudioConnection          patchCord6(mixerL, 0, audioOutput, 0);
AudioControlSGTL5000     sgtl5000;
// GUItool: end automatically generated code

// Use these with the audio adaptor board
#define SDCARD_CS_PIN    10
#define SDCARD_MOSI_PIN  7
#define SDCARD_SCK_PIN   14

// audio amp i2c
#define MAX9744_I2CADDR 0x4B

File effectsDir;
File crisisDir;
int numberOfEffects = 0;
int currentFile = 0;
int prevFile = 0;
int nextFile;
const char *filename;
String crisisAudioDir = "/CRISIS/";
String effectAudioDir = "/EFFECTS/";
String crisisTrackName = "";
String effectTrackName = "";

bool crisisPlaying = false;
bool demoPlaying = false;

unsigned long lastClosed = 0;

unsigned long nextEffect = 0;

const unsigned long effectDelay = 120000; // 300000 = 5 minutes
float effectDelayMultipler = 1.37; // = 5 minutes to 6.85 minutes

// proximity sensor
const int proximityPin = 2;
const int numReadings = 3;
int readings[numReadings];
int readIndex = 0;
float avgReading = 0.0;

// led strip 12v via mosfet
FadeLed lights(5);

// left vol pot crisis
const int crisisLPin = A2;

// right vol pot crisis
const int crisisRPin = A3;

// right vol pot demo
const int demoRPin = A6;

// inbuilt led
const int led = 13;

int8_t vol = 31;

elapsedMillis msec = 0;

void setup() {

  pinMode(led, OUTPUT);
  digitalWrite(led, HIGH);

  Serial.begin(9600);

  Serial.println(" ");
  Serial.println("pvi collective: waster");
  Serial.println("______________________________");
  Serial.println(" ");

  // lights
  analogWriteFrequency(5, 375000);
  FadeLed::setInterval(1);
  lights.begin(0);

  // audio board
  AudioMemory(16);

  sgtl5000.enable();
  sgtl5000.volume(0.5);

  SPI.setMOSI(SDCARD_MOSI_PIN);
  SPI.setSCK(SDCARD_SCK_PIN);
  if (!(SD.begin(SDCARD_CS_PIN))) {
    // stop here, but print a message repetitively
    bool blink = true;
    while (1) {
      Serial.println("Unable to access the SD card");
      if (blink) {
        digitalWrite(led, LOW);
        blink = false;
      } else {
        digitalWrite(led, HIGH);
        blink = true;
      }
      delay(500);
    }
  }

  // i2c to amp
  if (!setVolume(0)) {
    Serial.println("hmm.. the amp isn't responding");
    while (1);
  }

  // proximity sensor
  pinMode(proximityPin, INPUT_PULLUP);
  // initialize all the readings to 0:
  for (int i = 0; i < numReadings; i++) {
    readings[i] = 0;
  }

  // random the random
  randomSeed(analogRead(0));

  // effect timer
  nextEffect = random(effectDelay, effectDelay * effectDelayMultipler);

  // read file names from sd card
  getCrisisTrackName();
  getNumberOfEffectTracks();

}

// set volume to amp over i2c
boolean setVolume(int8_t v) {
  // cant be higher than 63 or lower than 0
  if (v > 63) v = 63;
  if (v < 0) v = 0;

  Wire.beginTransmission(MAX9744_I2CADDR);
  Wire.write(v);
  if (Wire.endTransmission() == 0) {
    return true;
  } else {
    return false;
  }

}

// read effects dir on sd card for files
void getNumberOfEffectTracks() {

  effectsDir = SD.open(effectAudioDir);
  effectsDir.rewindDirectory();
  numberOfEffects = 0;
  Serial.println("reading effect audio directory: " + effectAudioDir);
  while (true) {
    File entry = effectsDir.openNextFile();
    if (!entry) {
      // no more files
      break;
    }
    if (entry.size() > 4096) {
      numberOfEffects++;
      Serial.print(numberOfEffects);
      Serial.print(": ");
      Serial.println(entry.name());
    }
    entry.close();
  }

}

// grab a random effect to play
void getEffectToPlay() {

  currentFile = random(1, numberOfEffects + 1);

  effectsDir.rewindDirectory();
  for (int i = 1; i <= numberOfEffects; i++) {
    File entry = effectsDir.openNextFile();
    if (!entry) {
      // no more files
      break;
    }
    if (i == currentFile) {
      if (entry.size() <= 4096) {
        Serial.println("invalid file, let's try again");
        currentFile = prevFile;
        getEffectToPlay();
        break;
      } else {
        effectTrackName = effectAudioDir + String(entry.name());
      }
      break;
    }
  }

}

// read crisis dir on sd card for file
void getCrisisTrackName() {

  File crisisDir;
  crisisDir = SD.open(crisisAudioDir);
  crisisDir.rewindDirectory();
  Serial.println("reading crisis audio directory: " + crisisAudioDir);
  while (true) {

    File entry = crisisDir.openNextFile();
    if (!entry) {
      // no more files
      break;
    }
    if (entry.size() > 4096) {
      Serial.println(entry.name());
      crisisTrackName = crisisAudioDir + String(entry.name());
    }
    entry.close();

  }

}

// read proximity sensor
void updateProximitySensor() {

  // check proximity sensor for open bin lid
  int thisReading = digitalRead(proximityPin);

  //  Serial.println(thisReading);
  readings[readIndex] = thisReading;

  float totalReading = 0;
  for (int i = 0; i < numReadings; i++) {
    totalReading = totalReading + readings[i];
  }
  avgReading = totalReading / numReadings;
  //  Serial.println(avgReading);

  // advance to the next position in the array:
  readIndex++;

  // if we're at the end of the array...
  if (readIndex >= numReadings) {
    // ...wrap around to the beginning:
    readIndex = 0;
  }

}

void playFile(const char *filename) {

  Serial.print("playing track: ");
  Serial.println(filename);

  setVolume(63);

  // Start playing the file.  This sketch continues to
  // run while the file plays.
  wavPlayer.play(filename);

  fadeL.fadeIn(70);
  fadeR.fadeIn(70);

  // A brief delay for the library read WAV info
  delay(5);

}

// loop
void loop() {

  // update lights
  FadeLed::update();

  // update sensor
  updateProximitySensor();

  // check if something is playing
  if (wavPlayer.isPlaying()) {

    // is a crisis playing?
    if (crisisPlaying) {

      // if bin has been closed
      if (avgReading <= 0.0) {

        Serial.println("bin closed");
        fadeL.fadeOut(700);
        fadeR.fadeOut(700);

        delay(700);
        wavPlayer.stop();

        setVolume(0);

        crisisPlaying = false;

        lastClosed = millis();
        nextEffect = random(effectDelay, effectDelay * effectDelayMultipler);

      } else {

        float volL = analogRead(crisisLPin);
        volL = volL / 1024;
        float volR = analogRead(crisisRPin);
        volR = volR / 1024;

        mixerL.gain(0, volL);
        mixerR.gain(0, volR);

        digitalWrite(led, LOW);

        // update lights
        if (CRISIS == "POWER") {

          if (msec > 34500 && lights.get() != 0) {

            lights.setTime(170);
            lights.set(0);
            
          }
          
        }

      }

    }

    // are effects playing?
    if (demoPlaying) {

      // if bin is opened
      if (avgReading >= 1.0) {

        fadeL.fadeOut(300);
        fadeR.fadeOut(300);

        delay(300);

        wavPlayer.stop();

        setVolume(0);

        demoPlaying = false;

      } else {

        float volE = analogRead(demoRPin);
        volE = volE / 1024;

        mixerL.gain(0, 0.0);
        mixerR.gain(0, volE);

        digitalWrite(led, LOW);

      }

    }

  } else {

    crisisPlaying = false;
    demoPlaying = false;

    setVolume(0);

    if (avgReading >= 1) {
      // bin lid open

      if (!crisisPlaying) {

        Serial.println("bin opened");

        // start playing if not already playing
        crisisPlaying = true;
        playFile(crisisTrackName.c_str());
        msec = 0;
        fadeOn();

      }

    } else {
      // bin lid closed

      // check for effects timer
      if (millis() > lastClosed + nextEffect) {

        demoPlaying = true;

        if (numberOfEffects > 0) {        
          getEffectToPlay();
         playFile(effectTrackName.c_str());
        }

        lastClosed = millis();
        nextEffect = random(effectDelay, effectDelay * effectDelayMultipler);

      }

      fadeOff();

    }

  }

}


void fadeOn() {
  lights.setTime(770);
  lights.set(100);
}

void fadeOff() {
  lights.setTime(370);
  lights.set(0);
}


