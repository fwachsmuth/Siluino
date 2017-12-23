// Hifi-Anlage abschalten, nachdem es eine Weile still geblieben ist

// TO DO:
// Dokumentieren
// 
// Possible codeTypes (per enum):
//
// UNKNOWN            -1
// UNUSED              0
// RC5                 1
// RC6                 2
// NEC                 3
// SONY                4
// PANASONIC           5
// JVC                 6
// SAMSUNG             7
// WHYNTER             8
// AIWA_RC_T501        9
// LG                 10
// SANYO              11
// MITSUBISHI         12
// DISH               13
// SHARP              14 
// DENON              15
// PRONTO             16



#define PERMANENT_SILENCE   1
#define WAKEUP              2
#define FALLASLEEP          3
#define PERMANENT_SOUND     4

#define BUTTON_IDLE        10
#define BUTTON_DOWN        11
#define BUTTON_DEBOUNCE1   12
#define BUTTON_WAIT        13
#define BUTTON_UP          14
#define BUTTON_DEBOUNCE2   15
#define BUTTON_IGNOREDOWN  16

#define VU_METER           20
#define SETTING_1          21
#define SETTING_2          22
#define SETTING_3          23
#define SETTINGS_EXIT      24

#define LEARN_BASENOISE    30
#define LEARN_IR           31
#define TIMER_SHORT        32
#define TIMER_MID          33
#define TIMER_LONG         34

#define OFF                40
#define ON                 41
#define FASTBLINK          42
#define BLINK              43
#define ONCE               44 
#define TWICE              45
#define THRICE             46

#define BASENOISE_WAIT     50
#define BASENOISE_SAMPLE   51
#define BASENOISE_SAVE     52

#include <EEPROM.h>
#include <IRremote.h>

// IR Kram initialisieren
int RECV_PIN = 10;
IRrecv irrecv(RECV_PIN);
IRsend irsend;
decode_results results;

// Audio In initialisieren
const int micPin = 0; // Port A0
unsigned int currentAudioLevel = 0;  // variable to store the value coming from the sensor
unsigned int sampleCount = 0;
unsigned int sampleSum = 0;

unsigned int noiseThreshold = 0;

const byte ledPins[] = { 18, 17, 16, 15 };       // an array of pin numbers to0 which LEDs are attached
const byte ledPinCount = 4;       
byte ledMode[] = { OFF, ON, OFF, BLINK };

const byte settingsButtonPin = 9;

unsigned long lastNoiseMillis = 0;
unsigned long lastSilenceMillis = 0;
unsigned long baseNoiseLearnStartMillis = 0;
unsigned long previousButtonMillis = 0;
unsigned long permanentSilenceMinLength;
unsigned long startLearnBaseNoiseMillis = 0;
unsigned long buttonDownMillis = 0;
unsigned long buttonUpMillis = 0;


// LED variables
unsigned long fastblinkPrevMillis[] = { 0, 0, 0, 0 };        // will store last time LED was updated
unsigned long blinkPrevMillis[] = { 0, 0, 0, 0 };        // will store last time LED was updated
unsigned long currentMillis = 0;
int ledState[] = { LOW, LOW, LOW, LOW };             // ledState used to set the LED

const unsigned int ledSlowBlinkInterval = 200;
const unsigned int ledFastBlinkInterval = 80;
const unsigned int sampleBaseNoisePeriod = 3000;
const unsigned int learnBaseNoiseInitialDelay = 3000;
const unsigned int permanentNoiseMinLength = 5000;
const unsigned int longPressLength = 1000;
const unsigned int buttonDebounceInterval = 50;

byte ledBurstPatternCell = 0;
byte prevLedBurstPatternCell[] = { 0, 0, 0, 0 };

// Button handling variables
int buttonPressLength;

boolean noCodeYetReceived = true;

// byte settingsButtonState = 0;
byte myState;
byte prevState;
byte buttonState;
byte prevButtonState;
byte learnState;

int toggle = 0; // The RC5/6 toggle state

struct irCommand {
  int codeType = -1;
  int codeLen;
  unsigned long codeValue;
  unsigned int rawCodes[RAWBUF];
};

struct irCommand powerOffCode;

// ****************************************************************************************************************

void setup()
{
  analogReference(EXTERNAL);
  for (int thisLed = 0; thisLed < ledPinCount; thisLed++) pinMode(ledPins[thisLed], OUTPUT);

  irrecv.enableIRIn(); // Start the IR receiver
  
  pinMode(micPin, INPUT);
  pinMode(settingsButtonPin, INPUT_PULLUP);
  
  Serial.begin(9600);
  
  myState = PERMANENT_SILENCE;
  buttonState = BUTTON_IDLE;
  learnState = BASENOISE_WAIT;
  
  // setLedModes(OFF, THRICE, TWICE, ONCE);

  EEPROM.get(0, noiseThreshold);
  if (noiseThreshold > 1024) noiseThreshold = 50;
  Serial.print("noiseThreshold: ");
  Serial.println(noiseThreshold);

  EEPROM.get(2, permanentSilenceMinLength);
  if (permanentSilenceMinLength > 3600000) permanentSilenceMinLength = 10000;
  Serial.print("permanentSilenceMinLength: ");
  Serial.println(permanentSilenceMinLength);

  EEPROM.get(6, powerOffCode);
  Serial.println("Power-Off Code stored in EEPROM:");
  Serial.print("Code Type: ");
  Serial.println(powerOffCode.codeType);
  Serial.print("Code Length: ");
  Serial.println(powerOffCode.codeLen);
  Serial.print("Code Value (Hex): ");
  Serial.println(powerOffCode.codeValue, HEX);

}

void loop() {
//  if (myState != prevState) {
//    Serial.print("Mode: ");
//    Serial.println(myState);
//    prevState = myState;
//  }
  
  currentMillis = millis();
  updateLeds();
  checkButton();
  checkAudio();

  switch(myState) {
    case WAKEUP:
      if (currentAudioLevel <= noiseThreshold) {
        myState = PERMANENT_SILENCE;
      }
      if (currentMillis - lastNoiseMillis >= permanentNoiseMinLength) {
        setLedModes(OFF, ON, OFF, OFF);
        myState = PERMANENT_SOUND;
      }
    break;
    case PERMANENT_SOUND:
      if (currentAudioLevel <= noiseThreshold) {
        lastSilenceMillis = currentMillis;
        setLedModes(OFF, OFF, ON, OFF);
        myState = FALLASLEEP;
      }
    break;
    case FALLASLEEP:
      if (currentAudioLevel > noiseThreshold) {
        setLedModes(OFF, ON, OFF, OFF);
        myState = PERMANENT_SOUND;
      }
      if (currentMillis - lastSilenceMillis >= permanentSilenceMinLength) {
        setLedModes(ON, ON, ON, ON);
        irPowerOff();
        myState = PERMANENT_SILENCE;
      }
    break;
    case PERMANENT_SILENCE:
      setLedModes(OFF, OFF, OFF, ON);
      if (currentAudioLevel > noiseThreshold) {
        lastNoiseMillis = currentMillis;
        setLedModes(OFF, OFF, ON, OFF);
        myState = WAKEUP;
      }
    break;
    case LEARN_IR:
      learnIR();
    break;    
    case LEARN_BASENOISE:
      learnBaseNoise();
    break;
  }

}

// ****************************************************************************************************************
void buttonShortPress() {
  switch(myState) {
    case PERMANENT_SILENCE:
    case PERMANENT_SOUND:
    case WAKEUP:
    case FALLASLEEP:
      setLedModes(ON, BLINK, OFF, OFF);
      myState = SETTING_1;
    break;
    case SETTING_1:
      setLedModes(ON, OFF, BLINK, OFF);
      myState = SETTING_2;
    break;
    case SETTING_2:
      setLedModes(ON, OFF, OFF, BLINK);
      myState = SETTING_3;
    break;
    case SETTING_3:
      setLedModes(OFF, OFF, OFF, OFF);
      myState = PERMANENT_SILENCE;
    break;
    case TIMER_SHORT:
      setLedModes(ON, OFF, TWICE, OFF);
      myState = TIMER_MID;
    break;
    case TIMER_MID:
      setLedModes(ON, OFF, THRICE, OFF);
      myState = TIMER_LONG;
    break;
    case TIMER_LONG:
      setLedModes(ON, OFF, ONCE, OFF);
      myState = TIMER_SHORT;
    break;
    default:
    break;
  }
}

void buttonLongPress() {
  switch(myState) {
    case PERMANENT_SILENCE:
    case PERMANENT_SOUND:
    case WAKEUP:
    case FALLASLEEP:
    break;
    case SETTING_1:
      setLedModes(ON, FASTBLINK, OFF, OFF);
      startLearnBaseNoiseMillis = currentMillis;
      myState = LEARN_BASENOISE;
    break;
    case SETTING_2:
      setLedModes(ON, OFF, ONCE, OFF);
      myState = TIMER_SHORT;
    break;
    case SETTING_3:
      setLedModes(ON, OFF, OFF, FASTBLINK);
      noCodeYetReceived = true;
      myState = LEARN_IR;
    break;
    case TIMER_SHORT:
      setTimer(10);
      myState = PERMANENT_SILENCE;
    break;
    case TIMER_MID:
      setTimer(15 * 60);
      myState = PERMANENT_SILENCE;
    break;
    case TIMER_LONG:
      setTimer(60 * 60);
      myState = PERMANENT_SILENCE;
    break;
    default:
    break;
  }
}

void checkAudio() {
  if (currentMillis % 50 == 0) {
    currentAudioLevel = analogRead(micPin);    
  }
}

void learnBaseNoise() {
  switch(learnState) {
    case BASENOISE_WAIT:
      if (currentMillis - startLearnBaseNoiseMillis >= learnBaseNoiseInitialDelay - 500) {
        setLedModes(ON, ON, OFF, OFF);
      }
      if (currentMillis - startLearnBaseNoiseMillis >= learnBaseNoiseInitialDelay) {
        noiseThreshold = 0;
        sampleCount = 0;
        sampleSum = 0;
        baseNoiseLearnStartMillis = currentMillis;
        learnState = BASENOISE_SAMPLE;
      }
    break;
    case BASENOISE_SAMPLE:
      if (currentMillis - baseNoiseLearnStartMillis <= sampleBaseNoisePeriod) {
        if (currentMillis % 200 == 0) {
          checkAudio();
          sampleCount++;
          sampleSum += currentAudioLevel;
          Serial.print(currentAudioLevel);
          Serial.print("..");
        }
      } else {
        learnState = BASENOISE_SAVE;
      }
    break;
    case BASENOISE_SAVE:
      noiseThreshold = (sampleSum / sampleCount) + 5;
      EEPROM.put(0, noiseThreshold);
      Serial.print("\nAverage Noise Level ");
      Serial.println(sampleSum / sampleCount);
      learnState = BASENOISE_WAIT;
      myState = PERMANENT_SILENCE;
    break;
  }
}

void checkButton() {
//  if (buttonState != prevButtonState) {
//    Serial.print("Button: ");
//    Serial.println(buttonState);
//    prevButtonState = buttonState;
//  }
  switch(buttonState) {
    case BUTTON_IDLE:
      if (digitalRead(settingsButtonPin) == LOW) {
        buttonState = BUTTON_DOWN;
      }
      break;
    case BUTTON_DOWN:
      buttonDownMillis = currentMillis;
      buttonState = BUTTON_DEBOUNCE1;
      break;
    case BUTTON_DEBOUNCE1:
      if (currentMillis - buttonDownMillis >= buttonDebounceInterval) buttonState = BUTTON_WAIT;
      break;
    case BUTTON_WAIT:
      if (digitalRead(settingsButtonPin) == HIGH) {
        buttonState = BUTTON_UP;
      } else if (currentMillis - longPressLength >= buttonDownMillis) {
        buttonLongPress();
        buttonState = BUTTON_IGNOREDOWN;
      }
      break;
    case BUTTON_IGNOREDOWN:
      if (digitalRead(settingsButtonPin) == HIGH) buttonState = BUTTON_DEBOUNCE2;
      break;
    case BUTTON_UP:
      buttonUpMillis = currentMillis;
      buttonPressLength = buttonUpMillis - buttonDownMillis;
      buttonState = BUTTON_DEBOUNCE2;
      if (buttonPressLength < longPressLength) {
        buttonShortPress();
      } else {
        buttonLongPress();
      }
      break;
    case BUTTON_DEBOUNCE2:
      if (currentMillis - buttonUpMillis >= buttonDebounceInterval) buttonState = BUTTON_IDLE;
      break;
  }
}

void updateLeds() {
  for (byte thisLed = 0; thisLed < ledPinCount; thisLed++) {
    switch(ledMode[thisLed]) {
      case OFF:
        digitalWrite(ledPins[thisLed], LOW); 
      break;
      case ON:
        digitalWrite(ledPins[thisLed], HIGH); 
      break;
      case FASTBLINK:
        if (currentMillis - fastblinkPrevMillis[thisLed] >= ledFastBlinkInterval) {
          digitalWrite(ledPins[thisLed], ledState[thisLed] = !ledState[thisLed]); 
          fastblinkPrevMillis[thisLed] = currentMillis;
        }
      break;
      case BLINK:
        if (currentMillis - blinkPrevMillis[thisLed] >= ledSlowBlinkInterval) {
          digitalWrite(ledPins[thisLed], ledState[thisLed] = !ledState[thisLed]); 
          blinkPrevMillis[thisLed] = currentMillis;
        }
      break;
      case ONCE:
        ledBurstPatternCell = (currentMillis / 50 % 20);
        if (ledBurstPatternCell != prevLedBurstPatternCell[thisLed]) {
          prevLedBurstPatternCell[thisLed] = ledBurstPatternCell;
          switch (ledBurstPatternCell) {
            case 0: digitalWrite(ledPins[thisLed], HIGH); break;
            default: digitalWrite(ledPins[thisLed], LOW); break;
          }
        }
      break;
      case TWICE:
        ledBurstPatternCell = (currentMillis / 50 % 20);
        if (ledBurstPatternCell != prevLedBurstPatternCell[thisLed]) {
          prevLedBurstPatternCell[thisLed] = ledBurstPatternCell;
          switch (ledBurstPatternCell) {
            case 0: case 4: digitalWrite(ledPins[thisLed], HIGH); break;
            default: digitalWrite(ledPins[thisLed], LOW); break;
          }
        }
      break;
      case THRICE:
        ledBurstPatternCell = (currentMillis / 50 % 20);
        if (ledBurstPatternCell != prevLedBurstPatternCell[thisLed]) {
          prevLedBurstPatternCell[thisLed] = ledBurstPatternCell;
          switch (ledBurstPatternCell) {
            case 0: case 4: case 8: digitalWrite(ledPins[thisLed], HIGH); break;
            default: digitalWrite(ledPins[thisLed], LOW); break;
          }
        }
      break;
    }
  }
}

void setLedModes(byte newSettingsLedMode, byte newRedLedMode, byte newYellowLedMode, byte newGreenLedMode) {
  ledMode[0] = newSettingsLedMode;      
  ledMode[1] = newRedLedMode;
  ledMode[2] = newYellowLedMode;
  ledMode[3] = newGreenLedMode;
}

void setTimer(unsigned long newTimer) {
  permanentSilenceMinLength = newTimer * 1000;
  Serial.print("New Shutdown Timer (ms): ");
  Serial.println(permanentSilenceMinLength);
  EEPROM.put(2, permanentSilenceMinLength);
}

void learnIR() {
  delay(300);
  while(noCodeYetReceived) {
    if (irrecv.decode(&results)) {
      storeCode(&results);
      irrecv.resume(); // resume receiver
      noCodeYetReceived = false;
    }
    currentMillis = millis();
    updateLeds();
  }
  irrecv.resume(); // Receive the next value
  myState = PERMANENT_SILENCE;
}

void irPowerOff() {
  sendCode(false);
  sendCode(false);
  delay(50);
}

void storeCode(decode_results *results) {
  powerOffCode.codeType = results->decode_type;
  int count = results->rawlen;
  if (powerOffCode.codeType == UNKNOWN) {
    Serial.println("Received unknown code, saving as raw");
    powerOffCode.codeLen = results->rawlen - 1;
    // To store raw codes:
    // Drop first value (gap)
    // Convert from ticks to microseconds
    // Tweak marks shorter, and spaces longer to cancel out IR receiver distortion
    for (int i = 1; i <= powerOffCode.codeLen; i++) {
      if (i % 2) {
        // Mark
        powerOffCode.rawCodes[i - 1] = results->rawbuf[i]*USECPERTICK - MARK_EXCESS;
        Serial.print(" m");
      } 
      else {
        // Space
        powerOffCode.rawCodes[i - 1] = results->rawbuf[i]*USECPERTICK + MARK_EXCESS;
        Serial.print(" s");
      }
      Serial.print(powerOffCode.rawCodes[i - 1], DEC);
    }
    Serial.println("");
  }
  else {
    if (powerOffCode.codeType == NEC) {
      Serial.print("Received NEC: ");
      if (results->value == REPEAT) {
        // Don't record a NEC repeat value as that's useless.
        Serial.println("repeat; ignoring.");
        return;
      }
    } 
    else if (powerOffCode.codeType == SONY) {
      Serial.print("Received SONY: ");
    } 
    else if (powerOffCode.codeType == RC5) {
      Serial.print("Received RC5: ");
    } 
    else if (powerOffCode.codeType == RC6) {
      Serial.print("Received RC6: ");
    } 
    else {
      Serial.print("Unexpected codeType ");
      Serial.print(powerOffCode.codeType, DEC);
      Serial.println("");
    }
    Serial.println(results->value, HEX);
    powerOffCode.codeValue = results->value;
    powerOffCode.codeLen = results->bits;
  }

  EEPROM.put(6, powerOffCode);

//  Serial.println(powerOffCode.codeType);
//  Serial.println(powerOffCode.codeLen);
//  Serial.println(powerOffCode.codeValue, HEX);

}

void sendCode(int repeat) {
  switch (powerOffCode.codeType) {
    case NEC:
      if (repeat) {
        irsend.sendNEC(REPEAT, powerOffCode.codeLen);
       } else {
        irsend.sendNEC(powerOffCode.codeValue, powerOffCode.codeLen);
       }
    break;
    case RC5: case RC6:
      if (!repeat) {
        // Flip the toggle bit for a new button press
        toggle = 1 - toggle;
      }
      // Put the toggle bit into the code to send
      powerOffCode.codeValue = powerOffCode.codeValue & ~(1 << (powerOffCode.codeLen - 1));
      powerOffCode.codeValue = powerOffCode.codeValue | (toggle << (powerOffCode.codeLen - 1));
      if (powerOffCode.codeType == RC5) {
        irsend.sendRC5(powerOffCode.codeValue, powerOffCode.codeLen);
      } else {
        irsend.sendRC6(powerOffCode.codeValue, powerOffCode.codeLen);
      }
    break;
    case SONY:
      irsend.sendSony(powerOffCode.codeValue, powerOffCode.codeLen);
    break;
    case PANASONIC:
      irsend.sendPanasonic(powerOffCode.codeValue, powerOffCode.codeLen);
    break;
    case JVC:
      irsend.sendJVC(powerOffCode.codeValue, 16, 0);
      delayMicroseconds(50);
      irsend.sendJVC(powerOffCode.codeValue, 16, 1);
    break;
    case SAMSUNG:
      irsend.sendSAMSUNG(powerOffCode.codeValue, powerOffCode.codeLen);
    break;
    case WHYNTER:
      irsend.sendWhynter(powerOffCode.codeValue, powerOffCode.codeLen);
    break;
    case AIWA_RC_T501:
      irsend.sendAiwaRCT501(powerOffCode.codeValue);
    break;
    case LG:
      irsend.sendLG(powerOffCode.codeValue, powerOffCode.codeLen);
    break;
//    case SANYO:
//      irsend.sendSanyo(powerOffCode.codeValue, powerOffCode.codeLen);
//    break;
//    case MITSUBISHI:
//      irsend.sendMitsubishi(powerOffCode.codeValue, powerOffCode.codeLen);
//    break;
//    case DISH:
//      irsend.sendDISH(powerOffCode.codeValue, powerOffCode.codeLen);
//    break;
//    case SHARP:
//      irsend.sendSharpRaw(powerOffCode.codeValue, powerOffCode.codeLen);
//    break;
    case DENON:
      irsend.sendDenon(powerOffCode.codeValue, powerOffCode.codeLen);
    break;
//    case PRONTO:
//      irsend.sendPronto(powerOffCode.codeValue, powerOffCode.codeLen);
//    break;
    case UNKNOWN:
      // Assume 38 KHz
      irsend.sendRaw(powerOffCode.rawCodes, powerOffCode.codeLen, 38);
    break;
    default:
      Serial.print("Huh? Not covered codeType: "); Serial.println(powerOffCode.codeType);
    break;
  }
}

