#include <Arduino.h>
#include "MAX30105.h"
#include "heartRate.h"
#include <U8g2lib.h>
#include <Preferences.h>

#ifdef U8X8_HAVE_HW_SPI
#include <SPI.h>
#endif
#ifdef U8X8_HAVE_HW_I2C
#include <Wire.h>
#endif

#define OLED_CS 25
#define OLED_DC 32
#define OLED_RST 33
#define ENCODER_S1 4
#define ENCODER_S2 16
#define ENCODER_KEY 17

#define BUZZ 19
#define TRIG 26
#define ECHO 27

U8G2_SSD1309_128X64_NONAME0_F_4W_HW_SPI u8g2(U8G2_R0, OLED_CS, OLED_DC, OLED_RST);
MAX30105 particleSensor;
Preferences preferences;

unsigned long lastUltraTime = 0;
int dist = 999;
unsigned long lastDetected = 0;
const unsigned long screenTime = 60000;

int menuPage = 0;
int selected = 0; 
bool inMenu = false; 
bool systemAwake = false;
bool editMode = false;

unsigned long measurementStart = 0;
int finalBPM = 0;
int finalSPO2 = 0;
long minRed = 100000, maxRed = 0;
long minIR = 100000, maxIR = 0;
unsigned long sampleTime = 0;
int beatCount = 0;
bool peakDetected = false;

int historyBPM[3] = {0, 0, 0};
int historySPO2[3] = {0, 0, 0};
bool savedToHistory = false;

int spo2Limit = 95; 
int bpmLow = 50;
int bpmHigh = 100;

int get_distance() {
  digitalWrite(TRIG, LOW);
  delayMicroseconds(5);
  digitalWrite(TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG, LOW);
  
  unsigned long duration = pulseIn(ECHO, HIGH, 30000);
  if (duration == 0) {
    Serial.println("US Error: No Signal!");
    return 999;
  }

  float distance = (duration * 0.0343) / 2;
  Serial.print("Distance: "); Serial.println(distance);
  return (int)distance;
}

int measureBPM(long irValue, unsigned long elapsed) {
  long threshold = (maxIR + minIR) / 2;
  
  if (irValue > (threshold + 15) && !peakDetected) {
    beatCount++;
    peakDetected = true;
    tone(BUZZ, 3000, 10);
  }
  
  if (irValue < threshold && peakDetected) {
    peakDetected = false;
  }

  if (elapsed > 3 && elapsed < 60) {
    return (beatCount * 60) / elapsed;
  }
  return finalBPM;
}

int measureSpO2(long irValue, long redValue) {
  if (redValue > maxRed) maxRed = redValue;
  if (redValue < minRed) minRed = redValue;
  if (irValue > maxIR) maxIR = irValue;
  if (irValue < minIR) minIR = irValue;

  if (millis() - sampleTime > 2500) {
    float irAC = maxIR - minIR;
    float redAC = maxRed - minRed;

    if (irAC > 10) {
      float R = (redAC / (float)redValue) / (irAC / (float)irValue);
      float calculation = 104 - 17 * R;

      finalSPO2 = (int)calculation;
    }

    maxRed = redValue; minRed = redValue;
    maxIR = irValue;   minIR = irValue; 
    
    sampleTime = millis();
  }
  return finalSPO2;
}

void measure_update() {
  long irValue = particleSensor.getIR();
  long redValue = particleSensor.getRed();

  if (irValue < 23500) {
    measurementStart = 0;
    savedToHistory = false;
    finalBPM = 0;
    finalSPO2 = 0;
    noTone(BUZZ);
    u8g2.setFont(u8g2_font_6x12_tr);
    u8g2.drawStr(0, 35, "STATUS: NO FINGER");
    u8g2.drawStr(0, 50, "Place finger...");
    return; 
  }

  if (measurementStart == 0) {
    measurementStart = millis();
    beatCount = 0;
    sampleTime = millis();
  }

  unsigned long elapsed = (millis() - measurementStart) / 1000;

  u8g2.setFont(u8g2_font_5x7_tr);
  if (elapsed < 60) {
        
    finalBPM  = measureBPM(irValue, elapsed);
    finalSPO2 = measureSpO2(irValue, redValue);

    char progStr[30];
    sprintf(progStr, "MEASURING... %lu/60 s", elapsed);
    u8g2.drawStr(0, 25, progStr);
  } 
  else {
    u8g2.drawStr(0, 25, "STATUS: FINISHED");

    // HISTORY
    if (!savedToHistory) {
      for(int i=2; i>0; i--) { 
        historyBPM[i]=historyBPM[i-1];
        historySPO2[i]=historySPO2[i-1]; 
      }
      historyBPM[0] = finalBPM; 
      historySPO2[0] = finalSPO2;

      preferences.begin("health-data", false);
      
      preferences.putInt("bpm0", historyBPM[0]);
      preferences.putInt("bpm1", historyBPM[1]);
      preferences.putInt("bpm2", historyBPM[2]);
      
      preferences.putInt("spo20", historySPO2[0]);
      preferences.putInt("spo21", historySPO2[1]);
      preferences.putInt("spo22", historySPO2[2]);
      
      preferences.end();

      savedToHistory = true;
      tone(BUZZ, 2000, 200);
    }
    // ALARM
    if (finalSPO2 < spo2Limit) {
      if ((millis() / 500) % 2 == 0) tone(BUZZ, 2000); else noTone(BUZZ);
    } 
    else if(finalBPM < bpmLow || finalBPM > bpmHigh) {
      if ((millis() / 500) % 2 == 0) tone(BUZZ, 100); else noTone(BUZZ);
    }
  }

  char bStr[20], sStr[20];
  sprintf(bStr, "Pulse: %d BPM", finalBPM);
  sprintf(sStr, "Oxygen: %d %% SpO2", finalSPO2);
  u8g2.drawStr(0, 40, bStr);
  u8g2.drawStr(0, 55, sStr);
}

void setup() {
  pinMode(ENCODER_S1, INPUT_PULLUP);
  pinMode(ENCODER_S2, INPUT_PULLUP);
  pinMode(ENCODER_KEY, INPUT_PULLUP);
  pinMode(BUZZ, OUTPUT);
  pinMode(TRIG, OUTPUT);
  pinMode(ECHO, INPUT);

  tone(BUZZ, 2000, 200);

  u8g2.begin();
  Wire.begin(21, 22);
  if (particleSensor.begin(Wire, I2C_SPEED_STANDARD)) {
    particleSensor.setup();
    particleSensor.setPulseAmplitudeRed(0x0A);
    particleSensor.setPulseAmplitudeIR(0x0A);
  }

  preferences.begin("health-data", false); 
  
  historyBPM[0] = preferences.getInt("bpm0", 0);
  historyBPM[1] = preferences.getInt("bpm1", 0);
  historyBPM[2] = preferences.getInt("bpm2", 0);
  
  historySPO2[0] = preferences.getInt("spo20", 0);
  historySPO2[1] = preferences.getInt("spo21", 0);
  historySPO2[2] = preferences.getInt("spo22", 0);

  preferences.end();

  systemAwake = true;     
  lastDetected = millis();  
  u8g2.setPowerSave(0);
}

void loop() {
  if (millis() - lastUltraTime > 250) {
    dist = get_distance();
    lastUltraTime = millis();

    if (dist < 60) {
      lastDetected = millis(); 
      
      if (!systemAwake) {
        systemAwake = true;
        u8g2.setPowerSave(0); 
        tone(BUZZ, 2000, 50); 
      }
    }
  }

  if (systemAwake && (millis() - lastDetected > screenTime)) {
    systemAwake = false; 
    u8g2.setPowerSave(1);   
    inMenu = false;         
    tone(BUZZ, 1000, 100);  
  }

  // ROTARY OKRETANJE
  if (systemAwake) {
    static int lastS1State = HIGH;
    int currentS1 = digitalRead(ENCODER_S1);

    if (currentS1 != lastS1State && currentS1 == LOW) {
      bool right = (digitalRead(ENCODER_S2) != currentS1);
      
      if (!inMenu) {
        if (right) selected--; else selected++;
        if (selected > 2) selected = 0; // vraca sa zadnje opcije na prvu opciju
        if (selected < 0) selected = 2;
        tone(BUZZ, 2500, 15);
      } 
      else if (menuPage == 2) { // za opciju postavke
        if (!editMode) {
          if (right) selected--; else selected++;
          if (selected > 3) selected = 0; // za kretanje po min/mab bpm i SpO2
          if (selected < 0) selected = 3;
        } 
        else { // za promijenu vrijedsnoti npr bpm sa 60 na 80 mijenjam
          if (selected == 0) {
            if (right) bpmLow--;
            else bpmLow++; 
          } 
          else if (selected == 1) {
            if (right) bpmHigh--;
            else bpmHigh++;
          }
          else if (selected == 2) { 
            if (right) spo2Limit--;
            else spo2Limit++;
          } 
          
          spo2Limit = constrain(spo2Limit, 80, 100); // granice za okretanje
          bpmLow = constrain(bpmLow, 30, 60);
          bpmHigh = constrain(bpmHigh, 60, 120);
        }
        tone(BUZZ, 2000, 10);
      }
      lastS1State = currentS1;
    } 
    else { 
      lastS1State = currentS1; 
    }

    // ROTARY BUTTON
    static int lastKeyState = HIGH;
    int currentKey = digitalRead(ENCODER_KEY);

    if (lastKeyState == HIGH && currentKey == LOW) {
      if (!inMenu) { 
        menuPage = selected; 
        inMenu = true; 
        selected = 0; 
      } 
      else if (menuPage == 2) {
        if (selected == 3) { // ako je selectana opcija BACK vraca e na MAIN MENU
          inMenu = false; 
          editMode = false; 
          selected = 0; 
        }
        else editMode = !editMode;
      } 
      else { 
        inMenu = false;
        noTone(BUZZ); 
      }
      tone(BUZZ, 1500, 100);
      delay(200);
    }

    lastKeyState = currentKey;

    // PRIKAZ NA EKRANU
    u8g2.clearBuffer();
    if (!inMenu) {
      u8g2.setFont(u8g2_font_6x12_tr);
      u8g2.drawStr(37, 12, "MAIN MENU");
      u8g2.drawHLine(0, 15, 128);
      u8g2.drawStr(15, 30, "1. MEASURE");
      u8g2.drawStr(15, 45, "2. HISTORY");
      u8g2.drawStr(15, 60, "3. SETTINGS");
      u8g2.drawStr(2, 30 + (selected * 15), ">");
    } else {

      if (menuPage == 0) {
         u8g2.drawStr(0, 15, "[ MEASURING ]"); 
         measure_update(); 
      }

      else if (menuPage == 1) {
        u8g2.drawStr(0, 15, "[ HISTORY ]");
        for(int i=0; i<3; i++) {
          char h[30]; sprintf(h, "%d. %d BPM | %d%% SpO2", i+1, historyBPM[i], historySPO2[i]);
          u8g2.drawStr(0, 32 + (i*12), h);
        }
      } 

      else if (menuPage == 2) {
        u8g2.drawStr(0, 12, "[ SETTINGS ]");
        char s1[25], s2[25], s3[25];
        sprintf(s1, "Min BPM:  %d", bpmLow);
        sprintf(s2, "Max BPM:  %d", bpmHigh);
        sprintf(s3, "Min SpO2: %d%%", spo2Limit);

        u8g2.drawStr(15, 28, s1);
        u8g2.drawStr(15, 40, s2);
        u8g2.drawStr(15, 52, s3);
        u8g2.drawStr(80, 63, "[ BACK ]");
        if (selected == 3) u8g2.drawStr(70, 63, ">");
        else u8g2.drawStr(2, 28 + (selected * 12), editMode ? "*" : ">");
      }
    }
    static unsigned long lastRefresh = 0;
    if (millis() - lastRefresh > 100) { 
      u8g2.sendBuffer();
      lastRefresh = millis();
    } 
  }
}