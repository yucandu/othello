#include <Wire.h>
#include <INA219_WE.h>
#include <SH1106Wire.h>
#include <ArduinoOTA.h>
#include <WiFi.h>
#include <FastLED.h>
#include <BlynkSimpleEsp32.h>
#include "time.h"
#define DATA_PIN 1
CRGB leds[1];
// ----- OLED Setup -----
SH1106Wire display(0x3c, SDA, SCL);

// ----- INA219 Sensor Setup -----
INA219_WE INA1 = INA219_WE(0x40);
INA219_WE INA2 = INA219_WE(0x44);
//Adafruit_INA219 INA1(0x40);  // Measures discharge current/voltage
//Adafruit_INA219 INA2(0x44);  // Measures charge current/voltage
bool connected = false;
// ----- 5-Way Switch Pin Definitions -----
const int btnUp    = 5;
const int btnDown  = 6; //7
const int btnLeft  = 7;
const int btnRight = 10; //6
const int btnCenter= 20;
const int FET1= 21;
const int FET2= 0;
const int ledpin= 1;
const int chargerpin= 3;
const char* ssid = "mikesnet";
const char* password = "springchicken";
bool toggleState = true;     
double totalEnergy_mWh = 0.0;
double totalCharge_mAh = 0.0;
unsigned long lastSampleTime = 0;


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
  

String getButtonPressed() {
  if (digitalRead(btnUp) == LOW)    return "UP";
  if (digitalRead(btnDown) == LOW)  return "DOWN";
  if (digitalRead(btnLeft) == LOW)  return "LEFT";
  if (digitalRead(btnRight) == LOW) return "RIGHT";
  if (digitalRead(btnCenter) == LOW)return "CENTER";
  return "None";
}


void drawWifiIcon(int x, int y) {
  if (WiFi.status() == WL_CONNECTED) {
      // Draw three arcs relative to (x, y)
      // Outer arc
      display.setPixel(x, y);
      display.setPixel(x + 1, y + 1);
      display.setPixel(x + 2, y);
      // Middle arc
      display.setPixel(x, y + 2); 
      display.setPixel(x + 1, y + 3);
      display.setPixel(x + 2, y + 2);
      // Center dot
      display.setPixel(x + 1, y + 4);
  }
}

// Add near other global variables at top:
double totalEnergy_mWh = 0.0;
double totalCharge_mAh = 0.0;
unsigned long lastSampleTime = 0;

// Replace the doDisplay() function:
void doDisplay(){
    // ----- Read 5-Way Switch -----
    String activeButton = getButtonPressed();
    String chargerStatus = String(analogRead(chargerpin));
    float voltage2 = INA2.getBusVoltage_V();
    float current2 = INA2.getCurrent_mA();
    float power2 = INA2.getBusPower();
    
    // Calculate accumulated values
    unsigned long currentTime = millis();
    double hours = (currentTime - lastSampleTime) / 3600000.0; // Convert ms to hours
    totalEnergy_mWh += power2 * hours;
    totalCharge_mAh += current2 * hours;
    lastSampleTime = currentTime;
    
    display.clear();
    // Left-aligned labels
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.drawString(0, 0, "V:");
    display.drawString(0, 12, "mA:");
    display.drawString(0, 24, "mWh:");
    display.drawString(0, 36, "mAh:");
    display.drawString(0, 48, "Status:");
    
    // Right-aligned values
    display.setTextAlignment(TEXT_ALIGN_RIGHT);
    display.drawString(128, 0, String(voltage2, 2));
    display.drawString(128, 12, String(current2, 0));
    display.drawString(128, 24, String(totalEnergy_mWh, 0));
    display.drawString(128, 36, String(totalCharge_mAh, 0));
    display.drawString(128, 48, activeButton + " " + chargerStatus);
    
    drawWifiIcon(64, 60);  // Move wifi icon to top-right
    display.display();
}


void setup() {
  Serial.begin(115200);
  Wire.begin();
  FastLED.addLeds<NEOPIXEL, DATA_PIN>(leds, 1); 
  leds[0] = CRGB(5, 0, 0);
  FastLED.show();

  // ----- Initialize INA219 Sensors -----
  if (!INA1.init()) {
    Serial.println("Failed to find INA1 at 0x40");
    //while (1);
  }
  if (!INA2.init()) {
    Serial.println("Failed to find INA2 at 0x44");
    //while (1);
  }

  INA1.setBusRange(BRNG_16);
  INA2.setBusRange(BRNG_16);

  // ----- Initialize OLED -----
  display.init();
  //display.flipScreenVertically();
  display.setFont(ArialMT_Plain_10);

  // ----- Setup 5-Way Switch Pins -----
  pinMode(btnUp, INPUT_PULLUP);
  pinMode(btnDown, INPUT_PULLUP);
  pinMode(btnLeft, INPUT_PULLUP);
  pinMode(btnRight, INPUT_PULLUP);
  pinMode(btnCenter, INPUT_PULLUP);
  pinMode(chargerpin, INPUT);
  doDisplay();
  /*pinMode(FET1, OUTPUT);
  pinMode(FET2, OUTPUT);
  digitalWrite(FET1, LOW);
  digitalWrite(FET2, LOW);*/
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
}


void loop() {
  // ----- Read INA219 Sensor Values -----

  if ((WiFi.status() == WL_CONNECTED) && (!connected)) {
    connected = true;
    ArduinoOTA.setHostname("BatTester");
    ArduinoOTA.begin();
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    Blynk.config(auth, IPAddress(192, 168, 50, 197), 8080);
    Blynk.connect();
    struct tm timeinfo;
    getLocalTime(&timeinfo);
    terminal.println("***BATTERY TESTER v1.0 STARTED***");
    terminal.print("Connected to ");
    terminal.println(ssid);
    terminal.print("IP address: ");
    terminal.println(WiFi.localIP());
    printLocalTime();
    terminal.flush();
  }
  
  if ((WiFi.status() == WL_CONNECTED) && (connected))  {
    ArduinoOTA.handle();
    Blynk.run();
  }

  every(500){
    doDisplay();
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
      Blynk.virtualWrite(V10, totalCharge_mAh);
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
