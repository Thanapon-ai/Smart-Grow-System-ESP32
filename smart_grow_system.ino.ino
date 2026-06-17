#define BLYNK_TEMPLATE_ID "YOUR_BLYNK_TEMPLATE"
#define BLYNK_TEMPLATE_NAME "BlynkSystem"
#define BLYNK_AUTH_TOKEN "YOUR_BLYNK_AUTH_TOKEN"

#define BLYNK_PRINT Serial

#include <WiFi.h>
#include <WiFiClientSecure.h> 
#include <BlynkSimpleEsp32.h>
#include <Wire.h>
#include <BH1750.h>
#include <Preferences.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <DHT.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// --- สีหน้าจอ ---
#define C_SKYBLUE   0x7DFF  
#define C_HOTPINK   0xF81F  
#define C_LIME      0x7E0   

// ==========================================
// --- SECTION 1: CONFIGURATION ---
// ==========================================

// WiFi
char ssid[] = ;
char pass[] = ;
char auth[] = BLYNK_AUTH_TOKEN;

unsigned long previousMillisWiFi = 0;
const long intervalWiFi = 30000;

// Google Sheets Script ID
String GAS_ID = "YOUR_Script ID"; 

// Pins
#define DHTPIN 25        
#define DHTTYPE DHT22
#define ONE_WIRE_BUS 26  
const int relayPins[] = {32, 33, 27, 14, 5, 17, 16, 13};
const int numRelays = 8; 

// TFT Pins
#define TFT_DC    4
#define TFT_CS    15
#define TFT_RST   2
#define TFT_MISO  19
#define TFT_MOSI  23
#define TFT_CLK   18

const int relayLuxValues[] = {
  2000, 6450, 4100, 6500, 6500, 3500, 6400, 3600
};

// ==========================================
// --- SECTION 2: OBJECTS & VARIABLES ---
// ==========================================
BH1750 lightMeter;
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_MOSI, TFT_CLK, TFT_RST, TFT_MISO);

Preferences preferences;
BlynkTimer timer;
DHT dht(DHTPIN, DHTTYPE);
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// *** ลบ WiFiClientSecure ออกจากตรงนี้ เพื่อไปใช้แบบ Local คืน RAM ***

enum SystemMode { MODE_IDLE, MODE_PRESET_1, MODE_PRESET_2, MODE_CUSTOM };
int currentMode = MODE_IDLE;
int currentDay = 1;
unsigned long dayStartTime = 0;

// *** ตั้งเวลา: เวลาจริง 24 ชั่วโมง = 1 วัน ***
const unsigned long DAY_DURATION_MS = 86400000; 

int targetLux = 0;
float currentLux = 0;
int activeRelayCount = 0; 
int currentRelayMask = 0; 
int customLuxWeek1 = 2000;
int customLuxWeek2 = 20000; 
float airTemp = 0.0;
float humidity = 0.0;
float waterTemp = 0.0;

// ==========================================
// --- SECTION 3: FUNCTIONS ---
// ==========================================

void sendToGoogleSheet() {
  if (WiFi.status() == WL_CONNECTED) {
    // *** ใช้ Local Client คืน Memory ป้องกันบอร์ดค้าง ***
    WiFiClientSecure client; 
    client.setInsecure();
    client.setTimeout(5000); // Timeout 5 วินาที
    Serial.println("Sending to Sheet...");
    
    String url = "/macros/s/" + GAS_ID + "/exec?";
    url += "air=" + String(airTemp) + "&water=" + String(waterTemp) + "&hum=" + String(humidity) + "&lux=" + String(currentLux);

    if (client.connect("script.google.com", 443)) {
      client.print(String("GET ") + url + " HTTP/1.1\r\nHost: script.google.com\r\nConnection: close\r\n\r\n");
      Serial.println("Data Sent!");
      client.stop(); // ปิดการเชื่อมต่อทันที
    } else {
      Serial.println("Sheet Connect Failed!");
      client.stop();
    }
  } else {
    Serial.println("WiFi offline. Skip sending data.");
  }
}

void readSensors() {
  float h = dht.readHumidity(); 
  float t = dht.readTemperature();
  if (!isnan(h) && !isnan(t)) { humidity = h; airTemp = t; }
  
  sensors.requestTemperatures(); 
  float w = sensors.getTempCByIndex(0);
  if (w > -100 && w < 100) waterTemp = w;
  
  if (WiFi.status() == WL_CONNECTED) {
    Blynk.virtualWrite(V10, airTemp); 
    Blynk.virtualWrite(V11, humidity); 
    Blynk.virtualWrite(V12, waterTemp);
  }
}

void updateTFT() {
  tft.fillRect(0, 0, 320, 30, ILI9341_NAVY);
  tft.setTextColor(ILI9341_WHITE); tft.setTextSize(2); tft.setCursor(60, 5); tft.print("SMART GROW SYS");
  tft.fillRect(0, 31, 320, 209, ILI9341_BLACK);
  
  tft.setTextSize(2);
  tft.setCursor(10, 40); tft.setTextColor(ILI9341_YELLOW); tft.print("Mode: ");
  if (currentMode == MODE_IDLE) tft.print("IDLE");
  else if (currentMode == MODE_PRESET_1) tft.print("P1"); 
  else if (currentMode == MODE_PRESET_2) tft.print("P2"); 
  else tft.print("Custom");
  
  tft.setCursor(180, 40); tft.setTextColor(C_SKYBLUE); tft.print("Day: "); tft.print(currentDay);
  tft.print("/14");
  
  tft.setCursor(10, 70); tft.setTextColor(ILI9341_WHITE); tft.print("Light: "); tft.print((int)currentLux); tft.print(" Lx");
  tft.setCursor(10, 95); tft.setTextColor(ILI9341_GREENYELLOW); tft.print("Target: ");
  
  if (targetLux == 35800) {
     tft.print("35000"); // เนียนแสดงผลตามต้องการ
  } else {
     tft.print(targetLux);
  }
  tft.print(" Lx");
  
  tft.setCursor(10, 130); tft.setTextColor(ILI9341_ORANGE); tft.print("Air: "); tft.print(airTemp, 1); tft.print("C");
  tft.setCursor(160, 130);
  tft.setTextColor(C_SKYBLUE); tft.print("Hum: "); tft.print(humidity, 0); tft.print("%");
  tft.setCursor(10, 155); tft.setTextColor(C_HOTPINK); tft.print("Water: "); tft.print(waterTemp, 1); tft.print("C");

  tft.setCursor(240, 10); tft.setTextSize(1);
  if(WiFi.status() == WL_CONNECTED) { tft.setTextColor(C_LIME); tft.print("ONLINE"); }
  else { tft.setTextColor(ILI9341_RED); tft.print("OFFLINE"); }

  tft.drawLine(0, 185, 320, 185, ILI9341_DARKGREY);
  tft.setTextSize(2);
  tft.setCursor(10, 195); tft.setTextColor(ILI9341_ORANGE); tft.print("Active Relays (" + String(activeRelayCount) + "/" + String(numRelays) + "):");
  tft.setCursor(10, 220); tft.setTextColor(ILI9341_GREENYELLOW);
  
  if (activeRelayCount == 0) tft.print("- None -");
  else {
    for (int i = 0; i < 8; i++) { 
      if ((currentRelayMask >> i) & 1) { 
        tft.print(i + 1); tft.print(" "); 
      } 
    }
  }
}

void updateBlynkUI() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (currentMode == MODE_IDLE) { Blynk.virtualWrite(V1, 0); Blynk.virtualWrite(V2, 0); Blynk.virtualWrite(V5, 0); }
  else if (currentMode == MODE_PRESET_1) { Blynk.virtualWrite(V1, 1); Blynk.virtualWrite(V2, 0); Blynk.virtualWrite(V5, 0); }
  else if (currentMode == MODE_PRESET_2) { Blynk.virtualWrite(V1, 0); Blynk.virtualWrite(V2, 1); Blynk.virtualWrite(V5, 0); }
  else if (currentMode == MODE_CUSTOM) { Blynk.virtualWrite(V1, 0); Blynk.virtualWrite(V2, 0); Blynk.virtualWrite(V5, 1); }
}

void updateTargetLux() {
  if (currentMode == MODE_IDLE) { targetLux = 0; return; }
  if (currentMode == MODE_PRESET_1) targetLux = (currentDay <= 7) ? 3500 : 20000;
  else if (currentMode == MODE_PRESET_2) targetLux = (currentDay <= 7) ? 3500 : 35800;
  else if (currentMode == MODE_CUSTOM) targetLux = (currentDay <= 7) ? customLuxWeek1 : customLuxWeek2;
}

void resetSystem() {
  currentMode = MODE_IDLE; 
  currentDay = 1; 
  targetLux = 0;
  activeRelayCount = 0; 
  currentRelayMask = 0;
  
  for(int i=0; i<numRelays; i++) digitalWrite(relayPins[i], HIGH);
  
  preferences.begin("grow-sys", false); 
  preferences.putInt("mode", MODE_IDLE); 
  preferences.putInt("day", 1); 
  preferences.end();
  
  updateTFT(); 
  updateBlynkUI(); 
  if(WiFi.status()==WL_CONNECTED) Blynk.virtualWrite(V8, "System Reset");
}

void startMode(int mode) {
  currentMode = mode; 
  currentDay = 1; 
  dayStartTime = millis();
  updateTargetLux();
  
  preferences.begin("grow-sys", false); 
  preferences.putInt("mode", currentMode);
  preferences.putInt("day", currentDay); 
  preferences.end();
  
  updateTFT(); 
  updateBlynkUI();
}

void checkDayCycle() {
  if (currentMode == MODE_IDLE) return;
  
  // เช็คว่าครบเวลาจริง 24 ชั่วโมง หรือยัง
  if (millis() - dayStartTime >= DAY_DURATION_MS) {
    currentDay++;
    dayStartTime = millis(); 
    
    if (currentDay == 8 && WiFi.status() == WL_CONNECTED) { 
      Blynk.syncVirtual(V4);
    }
    
    if (currentDay > 14) {
      resetSystem(); 
    } else {
      preferences.begin("grow-sys", false); 
      preferences.putInt("day", currentDay);
      preferences.end();
      updateTargetLux(); 
      updateTFT();
    }
  }
}

void smartControlLight() {
  float luxReading = lightMeter.readLightLevel(); 
  if (luxReading >= 0) currentLux = luxReading;
  
  if (currentMode == MODE_IDLE) { 
    for(int i=0; i<numRelays; i++) digitalWrite(relayPins[i], HIGH); 
    activeRelayCount = 0; currentRelayMask = 0; 
    return;
  }

  int bestMask = 0; 
  long minDiff = 999999; 
  bool foundOverTarget = false;
  long threshold = targetLux - 200;
  if (threshold < 0) threshold = 0;

  for (int i = 0; i < 256; i++) {
    long currentSum = 0;
    for (int bit = 0; bit < 8; bit++) { 
      if ((i >> bit) & 1) currentSum += relayLuxValues[bit];
    }
    
    if (currentSum >= threshold) {
      foundOverTarget = true;
      long diff = abs(currentSum - targetLux); 
      if (diff < minDiff) { 
        minDiff = diff; bestMask = i;
      }
    }
  }

  if (!foundOverTarget && targetLux > 0) bestMask = 255;
  if (targetLux == 0) bestMask = 0;

  if (bestMask != currentRelayMask) {
    currentRelayMask = bestMask;
    activeRelayCount = 0;
    
    // *** ระบบป้องกัน Inrush Current (ปิดทุกดวง แล้วทยอยเปิด) ***
    for(int i=0; i<8; i++) digitalWrite(relayPins[i], HIGH); 
    delay(100);
    
    for (int i = 0; i < 8; i++) { 
      if ((bestMask >> i) & 1) { 
        digitalWrite(relayPins[i], LOW); 
        activeRelayCount++;
        delay(300); // ทยอยเปิดห่างกัน 0.3 วินาที เพื่อไม่ให้ดึงกระแสแรงเกินไป
      } 
    }
  }
  if(WiFi.status() == WL_CONNECTED) Blynk.virtualWrite(V7, (int)currentLux);
}

// --- BLYNK HANDLERS ---
BLYNK_CONNECTED() { 
  Blynk.syncAll(); updateBlynkUI();
}
BLYNK_WRITE(V1) { if (param.asInt() == 1) startMode(MODE_PRESET_1); } 
BLYNK_WRITE(V2) { if (param.asInt() == 1) startMode(MODE_PRESET_2); } 
BLYNK_WRITE(V5) { if (param.asInt() == 1) startMode(MODE_CUSTOM); } 
BLYNK_WRITE(V6) { if (param.asInt() == 1) resetSystem(); } 

BLYNK_WRITE(V3) { 
  int index = param.asInt();
  switch(index) {
    case 1: customLuxWeek1 = 2000; break;
    case 2: customLuxWeek1 = 4000; break;
    case 3: customLuxWeek1 = 6000; break;
    case 4: customLuxWeek1 = 7000; break;
  }
  preferences.begin("grow-sys", false); 
  preferences.putInt("lux1", customLuxWeek1); 
  preferences.end();
  if(currentMode == MODE_CUSTOM && currentDay <= 7) { 
    updateTargetLux(); smartControlLight();
  }
}
BLYNK_WRITE(V4) { 
  int index = param.asInt();
  switch(index) {
    case 1: customLuxWeek2 = 20000; break;
    case 2: customLuxWeek2 = 25000; break; 
    case 3: customLuxWeek2 = 30000; break; 
    case 4: customLuxWeek2 = 35800; break;
  }
  preferences.begin("grow-sys", false); 
  preferences.putInt("lux2", customLuxWeek2); 
  preferences.end();
  if(currentMode == MODE_CUSTOM && currentDay > 7) updateTargetLux();
}

// ==========================================
// --- SETUP & LOOP ---
// ==========================================
void setup() {
  Serial.begin(115200);
  for (int i = 0; i < numRelays; i++) { 
    pinMode(relayPins[i], OUTPUT); 
    digitalWrite(relayPins[i], HIGH); 
  }

  Wire.begin(21, 22); 
  lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE); 
  dht.begin();
  sensors.begin();
  
  tft.begin(); 
  tft.setRotation(1); 
  tft.fillScreen(ILI9341_BLACK); 
  updateTFT();

  preferences.begin("grow-sys", true);
  currentMode = preferences.getInt("mode", MODE_IDLE);
  currentDay = preferences.getInt("day", 1);
  customLuxWeek1 = preferences.getInt("lux1", 2000);
  customLuxWeek2 = preferences.getInt("lux2", 20000);
  preferences.end();

  if (currentMode != MODE_IDLE) { 
    updateTargetLux(); 
    dayStartTime = millis();
  }

  Serial.print("Connecting to WiFi: "); Serial.println(ssid);
  WiFi.begin(ssid, pass);
  int wifiTimeout = 0;
  
  while (WiFi.status() != WL_CONNECTED) { 
    delay(500); Serial.print("."); 
    wifiTimeout++;
    if (wifiTimeout > 40) { 
       Serial.println("\nWiFi Failed! Running in OFFLINE MODE.");
       break;
    } 
  }
  
  if(WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi Connected!");
    Blynk.config(auth, "blynk.cloud", 80); 
    Blynk.connect();
  }

  // *** ตั้งค่า Timer สำหรับโหมดเวลาจริง (Real-Time) ***
  timer.setInterval(2000L, smartControlLight);    // คุมไฟทุก 2 วินาที (ให้ปรับแสงได้ไว)
  timer.setInterval(60000L, checkDayCycle);       // เช็ควันทุกๆ 1 นาที (ไม่เปลืองรอบทำงาน)
  timer.setInterval(60000L, readSensors);         // อ่านเซนเซอร์ทุก 1 นาที
  timer.setInterval(60000L, updateTFT);           // อัปเดตจอทุก 1 นาที
  
  // *** สำคัญ: ส่ง Google Sheet ทุกๆ 10 นาที (600,000 มิลลิวินาที) ***
  timer.setInterval(600000L, sendToGoogleSheet);
}

void loop() {
  unsigned long currentMillis = millis();
  
  // จัดการ WiFi แบบ Non-Blocking
  if ((WiFi.status() != WL_CONNECTED) && (currentMillis - previousMillisWiFi >= intervalWiFi)) {
    Serial.println("Reconnecting WiFi...");
    WiFi.disconnect(); 
    WiFi.reconnect(); 
    previousMillisWiFi = currentMillis;
  }
  
  // *** ป้องกัน Blynk ทำให้บอร์ดค้าง ***
  // ให้พยายามรันและเชื่อมต่อ Blynk เฉพาะตอนที่ WiFi ต่อติดแล้วเท่านั้น
  if (WiFi.status() == WL_CONNECTED) { 
    if (!Blynk.connected()) {
      Blynk.connect(); 
    }
  }
  
  if (Blynk.connected()) {
    Blynk.run(); 
  }
  
  // timer รับหน้าที่สั่งคุมแสง อ่านจอ ส่งชีท ให้เดินต่อไปได้แม้ออฟไลน์
  timer.run();
}