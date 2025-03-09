#include <Wire.h>
#include <INA219_WE.h>

#include <ArduinoOTA.h>
#include <WiFi.h>
#include <FastLED.h>
#include <BlynkSimpleEsp32.h>
#include "time.h"

#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define DATA_PIN 1
CRGB leds[1];
// ----- OLED Setup -----
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
#define i2c_Address 0x3c  ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
Adafruit_SH1106G display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ----- INA219 Sensor Setup -----
INA219_WE INA1 = INA219_WE(0x40);
INA219_WE INA2 = INA219_WE(0x44);
//Adafruit_INA219 INA1(0x40);  // Measures discharge current/voltage
//Adafruit_INA219 INA2(0x44);  // Measures charge current/voltage
bool connected = false;
// ----- 5-Way Switch Pin Definitions -----
const int btnUp    = 5;
const int btnDown  = 10;     // Was 6, changed to 10
const int btnLeft  = 6;      // Was 7, changed to 6 
const int btnRight = 7;      // Was 10, changed to 7
const int btnCenter= 20;
const int FET1= 21;
const int FET2= 0;
const int ledpin= 1;
const int chargerpin= 3;
const char* ssid = "mikesnet";
const char* password = "springchicken";
bool toggleState = true;     
// Add to enum MenuState
enum MenuState {
  MAIN_SCREEN,
  MENU_SCREEN,
  EDITING_CHARGE,
  EDITING_CYCLES,
  BATT_TEST
};

// Add new global variables
int brFactor = 1;
double dischargeMwh = 0.0;
double dischargeMah = 0.0;
unsigned long testStartTime = 0;
bool isDischarging = false;
bool isCharging = false;
int currentCycle = 1;
unsigned long testEndTime = 0;  // To store completion time
MenuState currentState = MAIN_SCREEN;
int selectedMenuItem = 0;
bool chargeEnabled = true;
int numCycles = 1;
float vcutoff = 2.9;
double totalEnergy_mWh = 0.0;
double totalCharge_mAh = 0.0;
unsigned long lastSampleTime = 0;
float voltage2;
float current2;
float power2;
float voltage1;
float current1;     
float power1; 
struct tm timeinfo;
char auth[] = "ozogc-FyTEeTsd_1wsgPs5rkFazy6L79";

const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -18000;  //Replace with your GMT offset (secs)
const int daylightOffset_sec = 3600;   //Replace with your daylight offset (secs)

WidgetTerminal terminal(V10);

#define every(interval) \
  static uint32_t __every__##interval = millis(); \
  if (millis() - __every__##interval >= interval && (__every__##interval = millis()))

BLYNK_WRITE(V10) {
  if (String("help") == param.asStr()) {
    terminal.println("==List of available commands:==");
    terminal.println("wifi");
    terminal.println("volts");
    terminal.println("==End of list.==");
  }
  if (String("wifi") == param.asStr()) {
    terminal.print("Connected to: ");
    terminal.println(ssid);
    terminal.print("IP address:");
    terminal.println(WiFi.localIP());
    terminal.print("Signal strength: ");
    terminal.println(WiFi.RSSI());
    printLocalTime();
  }
  if (String("volts") == param.asStr()) {
    float shuntvoltage = INA1.getShuntVoltage_mV();
    float busvoltage = INA1.getBusVoltage_V();
    float power = INA1.getBusPower();
    float current = INA1.getCurrent_mA();
    float volts = busvoltage + (shuntvoltage / 1000);
    terminal.print("Volts: ");
    terminal.print(volts);
    terminal.println("v");
    terminal.print("Current: ");
    terminal.print(current);
    terminal.println("mA");
    terminal.print("Power: ");
    terminal.print(power);
    terminal.println("mW");
  }
  terminal.flush();
}

void printLocalTime() {
  time_t rawtime;
  struct tm* timeinfo;
  time(&rawtime);
  timeinfo = localtime(&rawtime);
  terminal.print(asctime(timeinfo));
}

  // Global variables for debouncing
  static unsigned long lastButtonPress = 0;
  static String lastButtonState = "None";

  // Modified getRawButtonPressed to include debouncing
  String getRawButtonPressed() {
    if (digitalRead(btnUp) == LOW)    return "UP";
    if (digitalRead(btnDown) == LOW)  return "DOWN";
    if (digitalRead(btnLeft) == LOW)  return "LEFT";
    if (digitalRead(btnRight) == LOW) return "RIGHT";
    if (digitalRead(btnCenter) == LOW)return "CENTER";
    return "None";
  }

  String getDebouncedButton() {
    String currentButton = getRawButtonPressed();
    unsigned long currentTime = millis();
    
    if (currentButton != lastButtonState) {
      lastButtonState = currentButton;
      if (currentButton != "None" && currentTime - lastButtonPress > 100) {
        lastButtonPress = currentTime;
        return currentButton;
      }
    }
    return "None";
  }
  





void drawWifiIcon(int x, int y) {
  if (WiFi.status() == WL_CONNECTED) {
      // Draw three arcs relative to (x, y)
      // Outer arc
      display.drawPixel(x, y, SH110X_WHITE);
      display.drawPixel(x + 1, y + 1, SH110X_WHITE);
      display.drawPixel(x + 2, y, SH110X_WHITE);
      // Middle arc
      display.drawPixel(x, y + 2, SH110X_WHITE); 
      display.drawPixel(x + 1, y + 3, SH110X_WHITE);
      display.drawPixel(x + 2, y + 2, SH110X_WHITE);
      // Center dot
      display.drawPixel(x + 1, y + 4, SH110X_WHITE);
  }
}



// Helper function for right-aligned text
void printRightAligned(const String &text, int x, int y) {
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(text.c_str(), 0, 0, &x1, &y1, &w, &h);
  display.setCursor(x - w, y);
  display.print(text);
}

void drawBattTest() {
  String activeButton = getDebouncedButton();
  if(activeButton == "CENTER") {
    currentState = MENU_SCREEN;
    digitalWrite(FET1, LOW);
    digitalWrite(FET2, LOW);
    isDischarging = false;
    isCharging = false;
    return;
  }
  
   voltage1 = INA1.getBusVoltage_V();
   current1 = INA1.getCurrent_mA();
   power1 = INA1.getBusPower();

  if(isCharging && analogRead(chargerpin) > 2000) {
    isCharging = false;
    digitalWrite(FET2, LOW);
    if(currentCycle < numCycles) {
      currentCycle++;
      dischargeMwh = 0;
      dischargeMah = 0;
      isDischarging = true;
      lastSampleTime = millis();
      digitalWrite(FET1, HIGH);
    }else {
      testEndTime = millis(); // Set end time when all cycles are complete
    }
  }
  
  // Update measurements only during discharge
  if(isDischarging) {
    if(voltage1 < vcutoff) {
      isDischarging = false;
      digitalWrite(FET1, LOW);
      if(chargeEnabled) {
        isCharging = true;
        digitalWrite(FET2, HIGH);
      } else {
        testEndTime = millis(); // Set end time if not charging
      }
    }
  }


  
  // Only update time if test is still running
  unsigned long elapsedTime;
  if(isDischarging || isCharging) {
    elapsedTime = millis() - testStartTime;
  } else {
    elapsedTime = testEndTime - testStartTime;
  }
  
  int hours = elapsedTime / 3600000;
  int minutes = (elapsedTime % 3600000) / 60000;
  int seconds = (elapsedTime % 60000) / 1000;
  
  // Display
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  
  // Left-aligned labels
  display.setCursor(0, 0);
  display.print("V:");
  display.setCursor(0, 12);
  display.print("mA:");
  display.setCursor(0, 24);
  display.print("mWh:");
  display.setCursor(0, 36);
  display.print("mAh:");
  display.setCursor(0, 48);
  
  // Right-aligned values
  printRightAligned(String(voltage1, 2) + "V", 128, 0);
  printRightAligned(String(current1, 1) + "mA", 128, 12);
  printRightAligned(String(dischargeMwh, 0) + "mWh", 128, 24);
  printRightAligned(String(dischargeMah, 0) + "mAh", 128, 36);
  
  char timeStr[9];
  sprintf(timeStr, "%02d:%02d:%02d", hours, minutes, seconds);
  printRightAligned(timeStr, 128, 48);
  
  // Status indication
  display.setCursor(0, 56);
  if(isDischarging) {
    //leds[0] = CRGB(5, 0, 0);
    display.print("DISCHARGE C");
    display.print(currentCycle);
  } else if(isCharging) {
    //leds[0] = CRGB(0, 0, 5);
    display.print("CHARGE C");
    display.print(currentCycle);
  } else if(!isDischarging && !isCharging) {
    //leds[0] = CRGB(0, 5, 0);
    display.print("COMPLETE C");
    display.print(currentCycle);
  }
  //FastLED.show();
  display.display();
}

void drawMain() {
  String activeButton = getDebouncedButton();
  if(activeButton != "None" && currentState == MAIN_SCREEN) {  // Only change to menu if we're already in main screen
    currentState = MENU_SCREEN;
    return;
  }
  
  String chargerStatus = String(analogRead(chargerpin));
  voltage2 = INA2.getBusVoltage_V();
  current2 = INA2.getCurrent_mA();
  power2 = INA2.getBusPower();
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  
  // Left-aligned labels
  display.setCursor(0, 0);
  display.print("V:");
  display.setCursor(0, 12);
  display.print("mA:");
  display.setCursor(0, 24);
  display.print("mWh:");
  display.setCursor(0, 36);
  display.print("mAh:");
  display.setCursor(0, 48);
  display.print("Status:");
  
  // Right-aligned values
  printRightAligned(String(voltage2, 2) + "V", 128, 0);
  printRightAligned(String(current2, 1) + "mA", 128, 12);
  printRightAligned(String(totalEnergy_mWh, 0) + "mWh", 128, 24);
  printRightAligned(String(totalCharge_mAh, 0) + "mAh", 128, 36);
  printRightAligned(activeButton + " " + chargerStatus, 128, 48);
  
  drawWifiIcon(64, 60);
  display.display();
}

void drawMenu() {
  if (currentState == MAIN_SCREEN) {return;}
  display.clearDisplay();
  display.setTextSize(1);
  const char* menuItems[] = {"Batt Test", "Charge?", "Cycles", "Profiler", "Main"};
  int numItems = 5;
  
  for(int i = 0; i < numItems; i++) {
    // Set text color for menu items
    if(i == selectedMenuItem) {
      if((currentState == EDITING_CHARGE && i == 1) || 
         (currentState == EDITING_CYCLES && i == 2)) {
        display.setTextColor(SH110X_WHITE); // Normal text for menu item when editing value
      } else {
        display.setTextColor(SH110X_BLACK, SH110X_WHITE); // Highlighted menu item
      }
    } else {
      display.setTextColor(SH110X_WHITE); // Normal text for non-selected items
    }
    
    display.setCursor(0, i*12);
    display.print(menuItems[i]);
    
    // Handle right-aligned values
    if(i == 1) { // Charge setting
      display.setTextColor(SH110X_WHITE); // Reset to normal first
      if(currentState == EDITING_CHARGE && i == selectedMenuItem) {
        display.setTextColor(SH110X_BLACK, SH110X_WHITE); // Only highlight value when editing
      }
      printRightAligned(chargeEnabled ? "Y" : "N", 128, i*12);
    }
    else if(i == 2) { // Cycles setting
      display.setTextColor(SH110X_WHITE); // Reset to normal first
      if(currentState == EDITING_CYCLES && i == selectedMenuItem) {
        display.setTextColor(SH110X_BLACK, SH110X_WHITE); // Only highlight value when editing
      }
      printRightAligned(String(numCycles), 128, i*12);
    }
  }
  display.display();
}

void handleMenu() {
  String activeButton = getDebouncedButton();
  
  if(activeButton == "None") return;
  
  if(currentState == MENU_SCREEN) {
      if(activeButton == "UP" && selectedMenuItem > 0) {
          selectedMenuItem--;
      }
      else if(activeButton == "DOWN" && selectedMenuItem < 4) {
          selectedMenuItem++;
      }
      else if(activeButton == "CENTER") {
            if(selectedMenuItem == 0) { // Batt Test
              currentState = BATT_TEST;
              testStartTime = millis();
              lastSampleTime = millis();
              dischargeMwh = 0;
              dischargeMah = 0;
              currentCycle = 1;
              isDischarging = true;
              isCharging = false;
              digitalWrite(FET2, LOW);
              digitalWrite(FET1, HIGH);
          }
          if(selectedMenuItem == 1) {
              currentState = EDITING_CHARGE;
          }
          else if(selectedMenuItem == 2) {
              currentState = EDITING_CYCLES;
          }
          else if(selectedMenuItem == 4) {
              currentState = MAIN_SCREEN;
              selectedMenuItem = 0;
              return;  // Add immediate return to prevent extra menu draw
          }
      }
  }
  else if(currentState == EDITING_CHARGE) {
      if(activeButton == "UP" || activeButton == "DOWN") {
          chargeEnabled = !chargeEnabled;
      }
      else if(activeButton == "CENTER") {
          currentState = MENU_SCREEN;
      }
  }
  else if(currentState == EDITING_CYCLES) {
      if(activeButton == "UP" && numCycles < 99) {
          numCycles++;
      }
      else if(activeButton == "DOWN" && numCycles > 1) {
          numCycles--;
      }
      else if(activeButton == "CENTER") {
          currentState = MENU_SCREEN;
      }
  }
}


void setup() {
  Serial.begin(115200);
  Wire.begin();
  FastLED.addLeds<NEOPIXEL, DATA_PIN>(leds, 1); 
  leds[0] = CRGB(20, 20, 20);
  FastLED.show();

  // ----- Initialize INA219 Sensors -----
  if (!INA1.init()) {
    terminal.println("Failed to find INA1 at 0x40");
    //while (1);
  }
  if (!INA2.init()) {
    terminal.println("Failed to find INA2 at 0x44");
    //while (1);
  }

  INA1.setBusRange(BRNG_16);
  INA2.setBusRange(BRNG_16);

  // ----- Initialize OLED -----
  display.begin(i2c_Address, true);
  display.setRotation(2);
  display.clearDisplay();
  display.display();
  //display.flipScreenVertically();
  display.setFont();
  //display.setBrightness(255);

  // ----- Setup 5-Way Switch Pins -----
  pinMode(btnUp, INPUT_PULLUP);
  pinMode(btnDown, INPUT_PULLUP);
  pinMode(btnLeft, INPUT_PULLUP);
  pinMode(btnRight, INPUT_PULLUP);
  pinMode(btnCenter, INPUT_PULLUP);
  pinMode(chargerpin, INPUT);
  drawMain();
  digitalWrite(FET1, LOW);
  digitalWrite(FET2, LOW);
  pinMode(FET1, OUTPUT);
  pinMode(FET2, OUTPUT);
  digitalWrite(FET1, LOW);
  digitalWrite(FET2, LOW);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
}


void loop() {
  // ----- Read INA219 Sensor Values -----

  if ((WiFi.status() == WL_CONNECTED) && (!connected)) {
    connected = true;
    long m1, m2, m3, m4;
    m1 = millis();
    ArduinoOTA.setHostname("BatTester");
    ArduinoOTA.begin();
    m2 = millis();
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    Blynk.config(auth, IPAddress(192, 168, 50, 197), 8080);
    Blynk.connect();
    m3 = millis();
    getLocalTime(&timeinfo);
    m4 = millis();
    terminal.println("***BATTERY TESTER v1.0 STARTED***");
    terminal.print("Connected to ");
    terminal.println(ssid);
    terminal.print("IP address: ");
    terminal.println(WiFi.localIP());
    printLocalTime();
    terminal.print("Times: ");
    terminal.println(m1);
    terminal.println(m2);
    terminal.println(m3);
    terminal.println(m4);
    terminal.flush();
  }
  
  if ((WiFi.status() == WL_CONNECTED) && (connected))  {
    ArduinoOTA.handle();
    Blynk.run();
  }

  every(10){
      if(currentState == MAIN_SCREEN) {
        drawMain();
        if (current2 > 10) {
          leds[0] = CRGB(5 * brFactor, 0, 0);
        }
        else if (WiFi.status() == WL_CONNECTED) {leds[0] = CRGB(0, 5 * brFactor, 0);}
        else {leds[0] = CRGB(5 * brFactor, 20 * brFactor, 0);}
        
    } else if(currentState == BATT_TEST) {
        drawBattTest();
        int currentHour;
        currentHour = timeinfo.tm_hour;
        if (currentHour > 9) {
          brFactor = 4;
        }
        else {brFactor = 1;}
        
        if (isDischarging) {
          leds[0] = CRGB(5 * brFactor, 0, 0);
        } else if (isCharging) {
          leds[0] = CRGB(0, 0, 5 * brFactor);
        } else {
          leds[0] = CRGB(0, 5 * brFactor, 0);
        }
    } else {
      leds[0] = CRGB(5 * brFactor, 0, 5 * brFactor);
        handleMenu();
        drawMenu();
    }
    FastLED.show();
  }



  every(5000){
    // Calculate accumulated values
    unsigned long currentTime = millis();
    double hours = (currentTime - lastSampleTime) / 3600000.0; // Convert ms to hours
    totalEnergy_mWh += power2 * hours;
    totalCharge_mAh += current2 * hours;
    if (isDischarging) {
      dischargeMwh += power1 * hours;
      dischargeMah += current1 * hours;
    }
    lastSampleTime = currentTime;

  }

  every(10000){
    if (WiFi.status() == WL_CONNECTED) {
      Blynk.virtualWrite(V1, INA1.getBusVoltage_V());
      Blynk.virtualWrite(V2, INA1.getCurrent_mA());
      Blynk.virtualWrite(V3, INA1.getBusPower());
      Blynk.virtualWrite(V4, INA2.getBusVoltage_V());
      Blynk.virtualWrite(V5, INA2.getCurrent_mA());
      Blynk.virtualWrite(V6, WiFi.RSSI());
      Blynk.virtualWrite(V7, INA2.getBusPower());
      Blynk.virtualWrite(V8, analogRead(chargerpin));
      Blynk.virtualWrite(V9, totalEnergy_mWh);
      Blynk.virtualWrite(V11, totalCharge_mAh);
      Blynk.virtualWrite(V12, dischargeMwh);
      Blynk.virtualWrite(V13, dischargeMah);
    }
  }

/*every(5000){
    toggleState = !toggleState;
    
    if (toggleState) {
      digitalWrite(FET1, HIGH);
      digitalWrite(FET2, LOW);
    } else {
      digitalWrite(FET1, LOW);
      digitalWrite(FET2, HIGH);
    }
  }*/
}
