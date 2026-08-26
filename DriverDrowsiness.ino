#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <TinyGPS++.h>
#include <HardwareSerial.h>
#include <Adafruit_NeoPixel.h>

// ---------------- Objects ----------------
WebServer server(80);
Preferences preferences;
TinyGPSPlus gps;
HardwareSerial gpsSerial(2);
HardwareSerial simSerial(1);

extern String htmlPage;

// ---------------- WS2812 ----------------
#define RING_PIN   4
#define RING_COUNT 8
Adafruit_NeoPixel ring(RING_COUNT, RING_PIN, NEO_GRB + NEO_KHZ800);

// ---------------- Buzzer ----------------
const int buzzerPin = 33;   // buzzer negative here, positive on +5V

// ---------------- Physical Emergency Button ----------------
const int emergencyButtonPin = 25;  // button to GND

// ---------------- AP Settings ----------------
const char* apSSID = "ESP32-Setup";
const char* apPassword = "12345678";

// ---------------- WiFi Saved ----------------
String savedSSID = "";
String savedPASS = "";

// ---------------- WiFi Status ----------------
bool wifiConnected = false;
bool internetOK = false;
String wifiIP = "";
String wifiSSIDNow = "";

// ---------------- Debug ----------------
String lastDebugMessage = "System started";
String lastSmsStatus = "No SMS sent yet";
String lastCallStatus = "No call made yet";

// ---------------- GPS Data ----------------
String gpsFix = "No Fix";
String gpsLat = "-";
String gpsLng = "-";
String gpsAlt = "-";
String gpsSpeed = "-";
String gpsSats = "0";
String gpsTime = "-";
String gpsDate = "-";
String gpsRawStatus = "No GPS data";

// ---------------- SIM Data ----------------
bool simReady = false;
bool simRegistered = false;
int simSignal = -1;
String simSignalText = "Unknown";
String simNetworkStatus = "Unknown";
String simOperator = "-";

// ---------------- Emergency ----------------
bool emergencyActive = false;
unsigned long emergencyStartTime = 0;
const unsigned long emergencyDuration = 60000;
bool smsSentForCurrentEmergency = false;
bool callDoneForCurrentEmergency = false;

// ---------------- Settings ----------------
int callDurationSeconds = 20;
int emergencyCallRepeat = 1;

// ---------------- LED/Buzzer Settings ----------------
bool ledEnabled = true;
int ledRunSeconds = 10;
int ledPattern = 1;
unsigned long redStartDuration = 5000; // 3 sec RED start
bool redStartDone = false;

bool buzzerEnabled = true;
int buzzerRunSeconds = 10;
int buzzerPattern = 1;

// ---------------- LED/Buzzer Runtime ----------------
bool ledEffectActive = false;
bool buzzerEffectActive = false;
unsigned long ledEffectStart = 0;
unsigned long buzzerEffectStart = 0;

// ---------------- Physical Button Debounce ----------------
bool lastButtonReading = HIGH;
bool stableButtonState = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;

// ---------------- Contacts ----------------
struct Contact {
  String name;
  String number;
  bool smsEnabled;
  bool callEnabled;
};

Contact contacts[5];

// ---------------- Timers ----------------
unsigned long lastWiFiCheck = 0;
unsigned long lastGPSCheck = 0;
unsigned long lastSimSignalCheck = 0;
unsigned long lastSimRegisterCheck = 0;
unsigned long lastInternetCheck = 0;
unsigned long lastLedAnim = 0;
unsigned long lastBuzzAnim = 0;

const unsigned long wifiCheckInterval = 3000;
const unsigned long gpsCheckInterval = 500;
const unsigned long simSignalCheckInterval = 8000;
const unsigned long simRegisterCheckInterval = 30000;
const unsigned long internetCheckInterval = 5000;

// ---------------- Utility ----------------
void setDebug(String msg) {
  lastDebugMessage = msg;
  Serial.println(msg);
}

String normalizePhoneNumber(String num) {
  num.trim();
  num.replace(" ", "");
  num.replace("-", "");

  if (num.startsWith("+")) return num;
  if (num.length() == 10) return "+91" + num;
  if (num.length() == 11 && num.startsWith("0")) return "+91" + num.substring(1);
  return num;
}

String buildGoogleMapsLink() {
  if (gps.location.isValid()) {
    return "https://maps.google.com/?q=" + gpsLat + "," + gpsLng;
  }
  return "Location not fixed";
}

bool checkInternet() {
  if (WiFi.status() != WL_CONNECTED) return false;
  IPAddress resolvedIP;
  return WiFi.hostByName("google.com", resolvedIP);
}

// ---------------- Preferences ----------------
void loadCredentials() {
  preferences.begin("wifiCreds", true);
  savedSSID = preferences.getString("ssid", "");
  savedPASS = preferences.getString("pass", "");
  preferences.end();
}

void saveCredentials(const String& ssid, const String& pass) {
  preferences.begin("wifiCreds", false);
  preferences.putString("ssid", ssid);
  preferences.putString("pass", pass);
  preferences.end();

  savedSSID = ssid;
  savedPASS = pass;
}

void loadContacts() {

  // Clear all contacts at boot

  for (int i = 0; i < 5; i++) {

    contacts[i].name = "";
    contacts[i].number = "";
    contacts[i].smsEnabled = false;
    contacts[i].callEnabled = false;

  }

  Serial.println("Contacts reset at boot (RAM mode)");

}

void saveContacts() {

  // Allow only one call-enabled contact
  int selectedCall = -1;

  for (int i = 0; i < 5; i++) {
    if (contacts[i].callEnabled) {
      selectedCall = i;
      break;
    }
  }

  if (selectedCall >= 0) {
    for (int i = 0; i < 5; i++) {
      contacts[i].callEnabled = (i == selectedCall);
    }
  }

  // DO NOT SAVE TO FLASH
  Serial.println("Contacts stored temporarily (RAM only)");

}
void loadSettings() {
  preferences.begin("settings", true);

  callDurationSeconds = preferences.getInt("callDur", 20);
  emergencyCallRepeat = preferences.getInt("callRep", 1);

  ledEnabled = preferences.getBool("ledEn", true);
  ledRunSeconds = preferences.getInt("ledSec", 10);
  ledPattern = preferences.getInt("ledPat", 1);

  buzzerEnabled = preferences.getBool("buzEn", true);
  buzzerRunSeconds = preferences.getInt("buzSec", 10);
  buzzerPattern = preferences.getInt("buzPat", 1);

  preferences.end();

  if (callDurationSeconds < 5) callDurationSeconds = 5;
  if (callDurationSeconds > 120) callDurationSeconds = 120;
  if (emergencyCallRepeat < 1) emergencyCallRepeat = 1;
  if (emergencyCallRepeat > 10) emergencyCallRepeat = 10;

  if (ledRunSeconds < 0) ledRunSeconds = 0;
  if (ledRunSeconds > 60) ledRunSeconds = 60;
  if (ledPattern < 1) ledPattern = 1;
  if (ledPattern > 5) ledPattern = 5;

  if (buzzerRunSeconds < 0) buzzerRunSeconds = 0;
  if (buzzerRunSeconds > 60) buzzerRunSeconds = 60;
  if (buzzerPattern < 1) buzzerPattern = 1;
  if (buzzerPattern > 5) buzzerPattern = 5;
}

void saveSettings() {
  preferences.begin("settings", false);

  preferences.putInt("callDur", callDurationSeconds);
  preferences.putInt("callRep", emergencyCallRepeat);

  preferences.putBool("ledEn", ledEnabled);
  preferences.putInt("ledSec", ledRunSeconds);
  preferences.putInt("ledPat", ledPattern);

  preferences.putBool("buzEn", buzzerEnabled);
  preferences.putInt("buzSec", buzzerRunSeconds);
  preferences.putInt("buzPat", buzzerPattern);

  preferences.end();
}

// ---------------- WiFi ----------------
void updateWiFiStatus() {
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    wifiIP = WiFi.localIP().toString();
    wifiSSIDNow = WiFi.SSID();
  } else {
    wifiConnected = false;
    internetOK = false;
    wifiIP = "";
    wifiSSIDNow = "";
  }
}

bool connectToRouter(const String& ssid, const String& pass, unsigned long timeoutMs) {
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());

  unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    if (WiFi.status() == WL_CONNECTED) {
      updateWiFiStatus();
      internetOK = checkInternet();
      saveCredentials(ssid, pass);
      setDebug("WiFi connected: " + ssid + " IP: " + WiFi.localIP().toString());
      return true;
    }
    delay(200);
    yield();
  }

  updateWiFiStatus();
  setDebug("WiFi connect failed for SSID: " + ssid);
  return false;
}

// ---------------- GPS ----------------
void updateGPSData() {
  while (gpsSerial.available()) {
    gps.encode(gpsSerial.read());
  }

  if (gps.location.isValid()) {
    gpsFix = "Fix Available";
    gpsLat = String(gps.location.lat(), 6);
    gpsLng = String(gps.location.lng(), 6);
  } else {
    gpsFix = "No Fix";
    gpsLat = "-";
    gpsLng = "-";
  }

  gpsAlt = gps.altitude.isValid() ? String(gps.altitude.meters(), 2) + " m" : "-";
  gpsSpeed = gps.speed.isValid() ? String(gps.speed.kmph(), 2) + " km/h" : "-";
  gpsSats = gps.satellites.isValid() ? String(gps.satellites.value()) : "0";

  if (gps.time.isValid()) {
    char buf[20];
    sprintf(buf, "%02d:%02d:%02d", gps.time.hour(), gps.time.minute(), gps.time.second());
    gpsTime = String(buf);
  } else gpsTime = "-";

  if (gps.date.isValid()) {
    char buf[20];
    sprintf(buf, "%02d/%02d/%04d", gps.date.day(), gps.date.month(), gps.date.year());
    gpsDate = String(buf);
  } else gpsDate = "-";

  if (gps.charsProcessed() < 10) gpsRawStatus = "No GPS serial data";
  else if (!gps.location.isValid()) gpsRawStatus = "Receiving GPS data, waiting for fix";
  else gpsRawStatus = "GPS fix acquired";
}

// ---------------- SIM ----------------
String sendAT(String cmd, unsigned long timeout, bool printResp) {
  while (simSerial.available()) simSerial.read();

  simSerial.println(cmd);
  unsigned long start = millis();
  String resp = "";

  while (millis() - start < timeout) {
    while (simSerial.available()) {
      resp += (char)simSerial.read();
    }
    yield();
  }

  if (printResp) {
    Serial.println("CMD: " + cmd);
    Serial.println("RESP: " + resp);
  }

  return resp;
}

bool initSIM800L() {
  String r = sendAT("AT", 1500, true);
  if (r.indexOf("OK") == -1) {
    setDebug("SIM800L not responding");
    simReady = false;
    return false;
  }

  sendAT("ATE0", 700, false);
  sendAT("AT+CMGF=1", 700, false);
  sendAT("AT+CSCS=\"GSM\"", 700, false);

  String cpin = sendAT("AT+CPIN?", 1200, true);
  simReady = (cpin.indexOf("READY") >= 0);

  setDebug(simReady ? "SIM800L initialized" : "SIM not ready");
  return simReady;
}

void quickSIMCheck() {
  String r = sendAT("AT+CSQ", 1000, false);
  int idx = r.indexOf("+CSQ:");
  if (idx >= 0) {
    int comma = r.indexOf(",", idx);
    if (comma > idx) {
      String val = r.substring(idx + 6, comma);
      val.trim();
      simSignal = val.toInt();

      if (simSignal == 99) simSignalText = "No Signal";
      else if (simSignal >= 25) simSignalText = "Excellent";
      else if (simSignal >= 20) simSignalText = "Good";
      else if (simSignal >= 15) simSignalText = "Fair";
      else if (simSignal >= 10) simSignalText = "Weak";
      else if (simSignal >= 0) simSignalText = "Very Weak";
      else simSignalText = "Unknown";
    }
  }
}

void slowSIMCheck() {
  String cpin = sendAT("AT+CPIN?", 1200, false);
  simReady = (cpin.indexOf("READY") >= 0);

  String creg = sendAT("AT+CREG?", 1200, false);
  simRegistered = (creg.indexOf(",1") >= 0 || creg.indexOf(",5") >= 0);
  simNetworkStatus = simRegistered ? "Registered" : "Not Registered";

  String cops = sendAT("AT+COPS?", 1500, false);
  int firstQuote = cops.indexOf("\"");
  int secondQuote = cops.indexOf("\"", firstQuote + 1);
  if (firstQuote >= 0 && secondQuote > firstQuote) {
    simOperator = cops.substring(firstQuote + 1, secondQuote);
  } else {
    simOperator = "-";
  }
}

String buildEmergencySMS() {
  String msg = "EMERGENCY ALERT\n";
  msg += "Lat: " + gpsLat + "\n";
  msg += "Lng: " + gpsLng + "\n";
  msg += "Alt: " + gpsAlt + "\n";
  msg += "Speed: " + gpsSpeed + "\n";
  msg += "UTC Time: " + gpsTime + "\n";
  msg += "Date: " + gpsDate + "\n";
  msg += "Map: " + buildGoogleMapsLink() + "\n";
  return msg;
}

bool sendSMS(String number, String message) {
  number = normalizePhoneNumber(number);

  if (number.length() < 8) {
    lastSmsStatus = "Invalid number: " + number;
    setDebug(lastSmsStatus);
    return false;
  }

  String r1 = sendAT("AT", 1000, false);
  if (r1.indexOf("OK") == -1) {
    lastSmsStatus = "SIM not responding before SMS";
    setDebug(lastSmsStatus);
    return false;
  }

  String r2 = sendAT("AT+CMGF=1", 1000, false);
  if (r2.indexOf("OK") == -1) {
    lastSmsStatus = "Failed to set text mode";
    setDebug(lastSmsStatus);
    return false;
  }

  while (simSerial.available()) simSerial.read();

  simSerial.print("AT+CMGS=\"");
  simSerial.print(number);
  simSerial.println("\"");

  unsigned long start = millis();
  String resp = "";
  bool gotPrompt = false;

  while (millis() - start < 7000) {
    while (simSerial.available()) {
      resp += (char)simSerial.read();
    }
    if (resp.indexOf(">") >= 0) {
      gotPrompt = true;
      break;
    }
    yield();
  }

  if (!gotPrompt) {
    lastSmsStatus = "No SMS prompt for " + number;
    setDebug(lastSmsStatus);
    return false;
  }

  simSerial.print(message);
  delay(200);
  simSerial.write(26);

  start = millis();
  resp = "";

  while (millis() - start < 20000) {
    while (simSerial.available()) {
      resp += (char)simSerial.read();
    }

    if (resp.indexOf("+CMGS:") >= 0 && resp.indexOf("OK") >= 0) {
      lastSmsStatus = "SMS sent to " + number;
      setDebug(lastSmsStatus);
      return true;
    }

    if (resp.indexOf("ERROR") >= 0) {
      lastSmsStatus = "SMS error for " + number;
      setDebug(lastSmsStatus);
      return false;
    }
    yield();
  }

  lastSmsStatus = "SMS timeout for " + number;
  setDebug(lastSmsStatus);
  return false;
}

bool makeCall(String number, unsigned long callDurationMs) {
  number = normalizePhoneNumber(number);

  if (number.length() < 8) {
    lastCallStatus = "Invalid call number: " + number;
    setDebug(lastCallStatus);
    return false;
  }

  String atResp = sendAT("AT", 1000, false);
  if (atResp.indexOf("OK") == -1) {
    lastCallStatus = "SIM not responding before call";
    setDebug(lastCallStatus);
    return false;
  }

  while (simSerial.available()) simSerial.read();

  simSerial.print("ATD");
  simSerial.print(number);
  simSerial.println(";");

  unsigned long start = millis();
  String resp = "";

  while (millis() - start < 5000) {
    while (simSerial.available()) {
      resp += (char)simSerial.read();
    }
    if (resp.indexOf("OK") >= 0 || resp.indexOf("CONNECT") >= 0) {
      lastCallStatus = "Calling " + number;
      setDebug(lastCallStatus);

      unsigned long callStart = millis();
      while (millis() - callStart < callDurationMs) {
        delay(50);
        yield();
      }

      sendAT("ATH", 1000, false);
      lastCallStatus = "Call ended for " + number;
      setDebug(lastCallStatus);
      return true;
    }
    if (resp.indexOf("ERROR") >= 0 || resp.indexOf("BUSY") >= 0 || resp.indexOf("NO CARRIER") >= 0) {
      lastCallStatus = "Call failed for " + number + " resp: " + resp;
      setDebug(lastCallStatus);
      return false;
    }
    yield();
  }

  sendAT("ATH", 1000, false);
  lastCallStatus = "Call timeout for " + number;
  setDebug(lastCallStatus);
  return false;
}

// ---------------- LED Ring ----------------
void ringOff() {
  ring.clear();
  ring.show();
}

void setAllRingRed(uint8_t r) {
  for (int i = 0; i < RING_COUNT; i++) ring.setPixelColor(i, ring.Color(r, 0, 0));
  ring.show();
}

void updateLedPattern() {

  if (!ledEffectActive) return;

  unsigned long now = millis();

  // Stop after run time
  if (now - ledEffectStart >= (unsigned long)ledRunSeconds * 1000UL) {

    ledEffectActive = false;
    ringOff();
    return;

  }

  // FIRST RED START PHASE
  if (!redStartDone) {

    if (now - ledEffectStart < redStartDuration) {

      // All LEDs RED
      for (int i = 0; i < RING_COUNT; i++) {

        ring.setPixelColor(i, ring.Color(255, 0, 0));

      }

      ring.show();
      return;

    }
    else {

      redStartDone = true;
      lastLedAnim = now;

    }
  }

  // Normal animation timing
  if (now - lastLedAnim < 80) return;

  lastLedAnim = now;

  static int step = 0;
  step++;

  switch (ledPattern) {

    case 1: // RGB blink

      if (step % 4 == 0) {

        for (int i = 0; i < RING_COUNT; i++)
          ring.setPixelColor(i, ring.Color(255, 0, 0));

      }
      else if (step % 4 == 1) {

        for (int i = 0; i < RING_COUNT; i++)
          ring.setPixelColor(i, ring.Color(0, 255, 0));

      }
      else if (step % 4 == 2) {

        for (int i = 0; i < RING_COUNT; i++)
          ring.setPixelColor(i, ring.Color(0, 0, 255));

      }
      else {

        ring.clear();

      }

      ring.show();
      break;


    case 2: // Rotating rainbow

      for (int i = 0; i < RING_COUNT; i++) {

        int pos = (i + step) % 8;

        switch (pos % 4) {

          case 0: ring.setPixelColor(i, ring.Color(255, 0, 0)); break;
          case 1: ring.setPixelColor(i, ring.Color(0, 255, 0)); break;
          case 2: ring.setPixelColor(i, ring.Color(0, 0, 255)); break;
          case 3: ring.setPixelColor(i, ring.Color(255, 255, 0)); break;

        }
      }

      ring.show();
      break;


    case 3: // Police style

      if ((step / 2) % 2 == 0) {

        for (int i = 0; i < RING_COUNT; i++) {

          if (i < RING_COUNT / 2)
            ring.setPixelColor(i, ring.Color(255, 0, 0));
          else
            ring.setPixelColor(i, ring.Color(0, 0, 255));

        }

      }
      else {

        for (int i = 0; i < RING_COUNT; i++) {

          if (i < RING_COUNT / 2)
            ring.setPixelColor(i, ring.Color(0, 0, 255));
          else
            ring.setPixelColor(i, ring.Color(255, 0, 0));

        }

      }

      ring.show();
      break;


    case 4: // Smooth RGB

      {

        int phase = step % 30;

        int r = 0;
        int g = 0;
        int b = 0;

        if (phase < 10) {

          r = 255 - phase * 25;
          g = phase * 25;

        }
        else if (phase < 20) {

          phase -= 10;

          g = 255 - phase * 25;
          b = phase * 25;

        }
        else {

          phase -= 20;

          r = phase * 25;
          b = 255 - phase * 25;

        }

        for (int i = 0; i < RING_COUNT; i++)
          ring.setPixelColor(i, ring.Color(r, g, b));

        ring.show();

      }

      break;


    case 5: // Multi chasing

      ring.clear();

      ring.setPixelColor((step + 0) % RING_COUNT, ring.Color(255, 0, 0));
      ring.setPixelColor((step + 2) % RING_COUNT, ring.Color(0, 255, 0));
      ring.setPixelColor((step + 4) % RING_COUNT, ring.Color(0, 0, 255));
      ring.setPixelColor((step + 6) % RING_COUNT, ring.Color(255, 255, 0));

      ring.show();

      break;

  }

}

// ---------------- Buzzer ----------------
// buzzer active when pin LOW (sink current)
void buzzerOn() {
  pinMode(buzzerPin, OUTPUT);
  digitalWrite(buzzerPin, LOW);
}
void buzzerOff() {
  pinMode(buzzerPin, INPUT);
}

void updateBuzzerPattern() {
  if (!buzzerEffectActive) return;

  if (millis() - buzzerEffectStart >= (unsigned long)buzzerRunSeconds * 1000UL) {
    buzzerEffectActive = false;
    buzzerOff();
    return;
  }

  if (millis() - lastBuzzAnim < 80) return;
  lastBuzzAnim = millis();

  static int step = 0;
  step++;

  switch (buzzerPattern) {
    case 1: // fast beep
      if (step % 2 == 0) buzzerOn();
      else buzzerOff();
      break;

    case 2: // slow beep
      if ((step / 4) % 2 == 0) buzzerOn();
      else buzzerOff();
      break;

    case 3: // double beep
      if (step % 10 == 0 || step % 10 == 2) buzzerOn();
      else buzzerOff();
      break;

    case 4: // sos-ish
      if (step % 16 < 2 || (step % 16 >= 4 && step % 16 < 6) || (step % 16 >= 8 && step % 16 < 12))
        buzzerOn();
      else
        buzzerOff();
      break;

    case 5: // pulse train
      if (step % 8 < 5) buzzerOn();
      else buzzerOff();
      break;
  }
}

// ---------------- Emergency ----------------
void triggerEmergency(const String& source) {
  emergencyActive = true;
  emergencyStartTime = millis();
  smsSentForCurrentEmergency = false;
  callDoneForCurrentEmergency = false;

  if (ledEnabled && ledRunSeconds > 0) {
    ledEffectActive = true;
    ledEffectStart = millis();
    redStartDone = false; 
  }

  if (buzzerEnabled && buzzerRunSeconds > 0) {
    buzzerEffectActive = true;
    buzzerEffectStart = millis();
  }

  setDebug("Emergency triggered from: " + source);
}

long getEmergencyRemainingSeconds() {
  if (!emergencyActive) return 0;
  unsigned long elapsed = millis() - emergencyStartTime;
  if (elapsed >= emergencyDuration) return 0;
  return (emergencyDuration - elapsed) / 1000;
}

String getEmergencyStatusText() {
  return emergencyActive ? "Button Pressed" : "Normal";
}

void trySendEmergencySMSAndCall() {
  if (!emergencyActive) return;

  if (!simReady || !simRegistered) {
    lastSmsStatus = "SIM not ready/registered, SMS not sent";
    lastCallStatus = "SIM not ready/registered, call not made";
    return;
  }

  if (!smsSentForCurrentEmergency) {
    updateGPSData();
    String msg = buildEmergencySMS();

    bool anySmsSent = false;
    for (int i = 0; i < 5; i++) {
      if (contacts[i].smsEnabled && contacts[i].number.length() > 0) {
        bool ok = sendSMS(contacts[i].number, msg);
        if (ok) anySmsSent = true;
        delay(1000);
      }
    }

    if (!anySmsSent) {
      lastSmsStatus = "No SMS sent to any enabled contact";
      setDebug(lastSmsStatus);
    }

    smsSentForCurrentEmergency = true;
  }

  if (!callDoneForCurrentEmergency) {
    int callIndex = -1;
    for (int i = 0; i < 5; i++) {
      if (contacts[i].callEnabled && contacts[i].number.length() > 0) {
        callIndex = i;
        break;
      }
    }

    if (callIndex >= 0) {
      for (int i = 0; i < emergencyCallRepeat; i++) {
        makeCall(contacts[callIndex].number, (unsigned long)callDurationSeconds * 1000UL);
        delay(1000);
      }
    } else {
      lastCallStatus = "No call-enabled contact selected";
      setDebug(lastCallStatus);
    }

    callDoneForCurrentEmergency = true;
  }
}

// ---------------- Physical button ----------------
void updateEmergencyButton() {
  bool reading = digitalRead(emergencyButtonPin);

  if (reading != lastButtonReading) {
    lastDebounceTime = millis();
    lastButtonReading = reading;
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading != stableButtonState) {
      stableButtonState = reading;
      if (stableButtonState == LOW) {
        triggerEmergency("Physical Button");
      }
    }
  }
}

// ---------------- Web Handlers ----------------
void handleRoot() {
  server.send(200, "text/html", htmlPage);
}

void handleStatus() {
  String json = "{";
  json += "\"wifiConnected\":" + String(wifiConnected ? "true" : "false") + ",";
  json += "\"internet\":" + String(internetOK ? "true" : "false") + ",";
  json += "\"ssid\":\"" + wifiSSIDNow + "\",";
  json += "\"ip\":\"" + wifiIP + "\",";
  json += "\"saved\":\"" + savedSSID + "\",";
  json += "\"simReady\":" + String(simReady ? "true" : "false") + ",";
  json += "\"simSignal\":" + String(simSignal) + ",";
  json += "\"simSignalText\":\"" + simSignalText + "\",";
  json += "\"simNetwork\":\"" + simNetworkStatus + "\",";
  json += "\"simOperator\":\"" + simOperator + "\",";
  json += "\"emergencyActive\":" + String(emergencyActive ? "true" : "false") + ",";
  json += "\"emergencyStatus\":\"" + getEmergencyStatusText() + "\",";
  json += "\"emergencyRemaining\":" + String(getEmergencyRemainingSeconds());
  json += "}";
  server.send(200, "application/json", json);
}

void handleGPS() {
  String json = "{";
  json += "\"fix\":\"" + gpsFix + "\",";
  json += "\"lat\":\"" + gpsLat + "\",";
  json += "\"lng\":\"" + gpsLng + "\",";
  json += "\"alt\":\"" + gpsAlt + "\",";
  json += "\"speed\":\"" + gpsSpeed + "\",";
  json += "\"sats\":\"" + gpsSats + "\",";
  json += "\"time\":\"" + gpsTime + "\",";
  json += "\"date\":\"" + gpsDate + "\"";
  json += "}";
  server.send(200, "application/json", json);
}

void handleTriggerEmergency() {
  triggerEmergency("Web Button");
  server.send(200, "application/json", "{\"success\":true}");
}

void handleTestSMSContact() {
  if (!simReady || !simRegistered) {
    server.send(200, "application/json", "{\"message\":\"SIM not ready/registered\"}");
    return;
  }

  int index = server.arg("index").toInt();
  if (index < 0 || index >= 5) {
    server.send(400, "application/json", "{\"message\":\"Invalid contact index\"}");
    return;
  }

  if (contacts[index].number.length() == 0) {
    server.send(200, "application/json", "{\"message\":\"Contact number empty\"}");
    return;
  }

  updateGPSData();
  String msg = buildEmergencySMS();
  bool sent = sendSMS(contacts[index].number, msg);
  server.send(200, "application/json", sent ? "{\"message\":\"Contact Test SMS sent\"}" : "{\"message\":\"Contact Test SMS failed\"}");
}

void handleTestCallContact() {
  if (!simReady || !simRegistered) {
    server.send(200, "application/json", "{\"message\":\"SIM not ready/registered\"}");
    return;
  }

  int index = server.arg("index").toInt();
  if (index < 0 || index >= 5) {
    server.send(400, "application/json", "{\"message\":\"Invalid contact index\"}");
    return;
  }

  if (contacts[index].number.length() == 0) {
    server.send(200, "application/json", "{\"message\":\"Contact number empty\"}");
    return;
  }

  bool called = makeCall(contacts[index].number, (unsigned long)callDurationSeconds * 1000UL);
  server.send(200, "application/json", called ? "{\"message\":\"Contact Test Call done\"}" : "{\"message\":\"Contact Test Call failed\"}");
}

void handleSaveSettings() {
  callDurationSeconds = server.arg("callDuration").toInt();
  emergencyCallRepeat = server.arg("callRepeat").toInt();

  ledEnabled = (server.arg("ledEnabled") == "1");
  ledRunSeconds = server.arg("ledRunSeconds").toInt();
  ledPattern = server.arg("ledPattern").toInt();

  buzzerEnabled = (server.arg("buzzerEnabled") == "1");
  buzzerRunSeconds = server.arg("buzzerRunSeconds").toInt();
  buzzerPattern = server.arg("buzzerPattern").toInt();

  if (callDurationSeconds < 5) callDurationSeconds = 5;
  if (callDurationSeconds > 120) callDurationSeconds = 120;
  if (emergencyCallRepeat < 1) emergencyCallRepeat = 1;
  if (emergencyCallRepeat > 10) emergencyCallRepeat = 10;

  if (ledRunSeconds < 0) ledRunSeconds = 0;
  if (ledRunSeconds > 60) ledRunSeconds = 60;
  if (ledPattern < 1) ledPattern = 1;
  if (ledPattern > 5) ledPattern = 5;

  if (buzzerRunSeconds < 0) buzzerRunSeconds = 0;
  if (buzzerRunSeconds > 60) buzzerRunSeconds = 60;
  if (buzzerPattern < 1) buzzerPattern = 1;
  if (buzzerPattern > 5) buzzerPattern = 5;

  saveSettings();
  server.send(200, "application/json", "{\"message\":\"Settings saved\"}");
}

void handleGetSettings() {
  String json = "{";
  json += "\"callDuration\":" + String(callDurationSeconds) + ",";
  json += "\"callRepeat\":" + String(emergencyCallRepeat) + ",";
  json += "\"ledEnabled\":" + String(ledEnabled ? "true" : "false") + ",";
  json += "\"ledRunSeconds\":" + String(ledRunSeconds) + ",";
  json += "\"ledPattern\":" + String(ledPattern) + ",";
  json += "\"buzzerEnabled\":" + String(buzzerEnabled ? "true" : "false") + ",";
  json += "\"buzzerRunSeconds\":" + String(buzzerRunSeconds) + ",";
  json += "\"buzzerPattern\":" + String(buzzerPattern);
  json += "}";
  server.send(200, "application/json", json);
}

void handleSaveContacts() {
  int selectedCall = -1;

  for (int i = 0; i < 5; i++) {
    contacts[i].name = server.arg("name" + String(i));
    contacts[i].number = server.arg("num" + String(i));
    contacts[i].smsEnabled = (server.arg("sms" + String(i)) == "1");
    contacts[i].callEnabled = (server.arg("call" + String(i)) == "1");

    if (contacts[i].callEnabled && selectedCall == -1) selectedCall = i;
  }

  for (int i = 0; i < 5; i++) contacts[i].callEnabled = (i == selectedCall);

  saveContacts();
  server.send(200, "application/json", "{\"message\":\"Contacts saved\"}");
}

void handleGetContacts() {
  String json = "[";
  for (int i = 0; i < 5; i++) {
    if (i > 0) json += ",";
    json += "{";
    json += "\"name\":\"" + contacts[i].name + "\",";
    json += "\"number\":\"" + contacts[i].number + "\",";
    json += "\"smsEnabled\":" + String(contacts[i].smsEnabled ? "true" : "false") + ",";
    json += "\"callEnabled\":" + String(contacts[i].callEnabled ? "true" : "false");
    json += "}";
  }
  json += "]";
  server.send(200, "application/json", json);
}

void handleScan() {
  WiFi.mode(WIFI_AP_STA);
  delay(50);
  int n = WiFi.scanNetworks();
  String json = "{\"networks\":[";
  for (int i = 0; i < n; i++) {
    if (i > 0) json += ",";
    String s = WiFi.SSID(i);
    s.replace("\\", "\\\\");
    s.replace("\"", "\\\"");
    json += "{\"ssid\":\"" + s + "\",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
  }
  json += "]}";
  server.send(200, "application/json", json);
}

void handleConnect() {
  String ssid = server.arg("ssid");
  String pass = server.arg("password");

  if (ssid.length() == 0) {
    server.send(400, "application/json", "{\"message\":\"SSID missing\"}");
    return;
  }

  bool ok = connectToRouter(ssid, pass, 15000);
  server.send(200, "application/json", ok ? "{\"message\":\"WiFi connected and saved\"}" : "{\"message\":\"WiFi connection failed\"}");
}

void handleDebug() {
  String dbg = lastDebugMessage + " | SMS: " + lastSmsStatus + " | CALL: " + lastCallStatus + " | OP: " + simOperator;
  dbg.replace("\\", "\\\\");
  dbg.replace("\"", "\\\"");
  server.send(200, "application/json", "{\"debug\":\"" + dbg + "\"}");
}

void handleNotFound() {
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

// ---------------- HTML ----------------
String htmlPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ESP32 Emergency Dashboard</title>
<style>
body{font-family:Arial;margin:0;background:#f5f9ff;color:#1f2937}
.wrap{max-width:1450px;margin:20px auto;padding:16px}
.card{background:#fff;border:1px solid #dbe7f5;border-radius:18px;padding:20px;box-shadow:0 10px 25px rgba(0,0,0,.05)}
h1{margin:0 0 8px;font-size:30px}
.sub{color:#6b7280;margin-bottom:16px}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:12px}
.grid3{display:grid;grid-template-columns:1fr 1fr 1fr;gap:12px}
.grid4{display:grid;grid-template-columns:1fr 1fr 1fr 1fr;gap:12px}
.grid5{display:grid;grid-template-columns:1fr 1fr 1fr 1fr 1fr;gap:12px}
.grid7{display:grid;grid-template-columns:1.1fr 1.2fr .6fr .6fr .8fr .8fr .8fr;gap:10px;align-items:end}
.box{background:#f9fbff;border:1px solid #dbe7f5;border-radius:14px;padding:14px}
label{display:block;margin:8px 0 4px;font-weight:600}
input[type=text],input[type=password],input[type=number],select{width:100%;padding:12px;border-radius:12px;border:1px solid #cdd9ea;box-sizing:border-box}
button{padding:12px 16px;border:none;border-radius:12px;cursor:pointer;font-weight:700;margin:8px 8px 8px 0}
.primary{background:#4f46e5;color:#fff}
.light{background:#eaf1ff;color:#24324a}
.danger{background:#dc2626;color:#fff}
.warn{background:#f59e0b;color:#fff}
.success{background:#10b981;color:#fff}
.dot{display:inline-block;width:12px;height:12px;border-radius:50%;margin-right:8px}
.green{background:#16a34a}
.red{background:#dc2626}
.switch{position:relative;display:inline-block;width:50px;height:26px}
.switch input{opacity:0;width:0;height:0}
.slider{position:absolute;cursor:pointer;inset:0;background:#cbd5e1;border-radius:30px}
.slider:before{content:"";position:absolute;height:20px;width:20px;left:3px;bottom:3px;background:#fff;border-radius:50%;transition:.2s}
input:checked + .slider{background:#22c55e}
input:checked + .slider:before{transform:translateX(24px)}
pre{background:#f8fbff;border:1px solid #dbe7f5;padding:12px;border-radius:12px;white-space:pre-wrap}
a.mapbtn{display:inline-block;padding:10px 14px;background:#10b981;color:#fff;text-decoration:none;border-radius:10px;font-weight:700}
.contactRow{margin-bottom:12px;padding:12px;border:1px solid #dbe7f5;border-radius:14px;background:#fbfdff}
@media(max-width:1100px){.grid,.grid3,.grid4,.grid5,.grid7{grid-template-columns:1fr}}
</style>
</head>
<body>
<div class="wrap">
  <div class="card">
    <h1>ESP32 Emergency Dashboard</h1>
    <div class="sub">Physical button on GPIO25 | LED ring on GPIO4 | Buzzer on GPIO33</div>

    <div class="box">
      <div><span id="netDot" class="dot red"></span><b>Internet Status:</b> <span id="internetStatus">Not Connected</span></div>
      <div style="margin-top:8px;"><b>Emergency:</b> <span id="emergencyStatus">Normal</span> <span id="emergencyTimer"></span></div>
      <button class="danger" onclick="triggerEmergency()">Trigger Emergency</button>
      <a id="mapBtn" class="mapbtn" href="#" target="_blank">Open Map</a>
    </div>

    <h3>WiFi</h3>
    <div class="grid">
      <div class="box"><b>Router WiFi:</b> <span id="wifiState">-</span><br><b>SSID:</b> <span id="ssidNow">-</span></div>
      <div class="box"><b>IP:</b> <span id="ipNow">-</span><br><b>Saved SSID:</b> <span id="savedSsid">-</span></div>
    </div>

    <h3>Connect WiFi</h3>
    <div class="box">
      <label>WiFi SSID</label>
      <input type="text" id="wifiSSID" placeholder="Enter WiFi SSID">
      <label>WiFi Password</label>
      <input type="password" id="wifiPASS" placeholder="Enter WiFi Password">
      <button class="primary" onclick="saveWiFi()">Connect & Save</button>
      <button class="light" onclick="scanWiFi()">Scan WiFi</button>
      <div id="scanResult" style="margin-top:10px;"></div>
    </div>

    <h3>SIM800L</h3>
    <div class="grid4">
      <div class="box"><b>SIM Ready:</b> <span id="simReady">-</span></div>
      <div class="box"><b>Network:</b> <span id="simNetwork">-</span></div>
      <div class="box"><b>Signal:</b> <span id="simSignal">-</span></div>
      <div class="box"><b>Operator:</b> <span id="simOperator">-</span></div>
    </div>

    <h3>GPS</h3>
    <div class="grid4">
      <div class="box"><b>Fix</b><br><span id="gpsFix">-</span></div>
      <div class="box"><b>Latitude</b><br><span id="gpsLat">-</span></div>
      <div class="box"><b>Longitude</b><br><span id="gpsLng">-</span></div>
      <div class="box"><b>Satellites</b><br><span id="gpsSats">-</span></div>
      <div class="box"><b>Altitude</b><br><span id="gpsAlt">-</span></div>
      <div class="box"><b>Speed</b><br><span id="gpsSpeed">-</span></div>
      <div class="box"><b>UTC Time</b><br><span id="gpsTime">-</span></div>
      <div class="box"><b>Date</b><br><span id="gpsDate">-</span></div>
    </div>

    <h3>Call Settings</h3>
    <div class="grid">
      <div class="box">
        <label>Call Duration (seconds)</label>
        <input type="number" id="callDuration" min="5" max="120">
      </div>
      <div class="box">
        <label>Emergency Call Repeat Count</label>
        <input type="number" id="callRepeat" min="1" max="10">
      </div>
    </div>

    <h3>LED Ring Settings</h3>
    <div class="grid5">
      <div class="box">
        <label>Enable LED</label>
        <label class="switch"><input type="checkbox" id="ledEnabled"><span class="slider"></span></label>
      </div>
      <div class="box">
        <label>LED Time (0-60 sec)</label>
        <input type="number" id="ledRunSeconds" min="0" max="60">
      </div>
      <div class="box">
        <label>LED Pattern</label>
        <select id="ledPattern">
          <option value="1">Pattern 1</option>
          <option value="2">Pattern 2</option>
          <option value="3">Pattern 3</option>
          <option value="4">Pattern 4</option>
          <option value="5">Pattern 5</option>
        </select>
      </div>
      <div class="box">
        <label>LED Test</label>
        <button class="warn" onclick="testLed()">Test LED</button>
      </div>
      <div class="box">
        <label>Save</label>
        <button class="primary" onclick="saveSettings()">Save Settings</button>
      </div>
    </div>

    <h3>Buzzer Settings</h3>
    <div class="grid5">
      <div class="box">
        <label>Enable Buzzer</label>
        <label class="switch"><input type="checkbox" id="buzzerEnabled"><span class="slider"></span></label>
      </div>
      <div class="box">
        <label>Buzzer Time (0-60 sec)</label>
        <input type="number" id="buzzerRunSeconds" min="0" max="60">
      </div>
      <div class="box">
        <label>Buzzer Pattern</label>
        <select id="buzzerPattern">
          <option value="1">Pattern 1</option>
          <option value="2">Pattern 2</option>
          <option value="3">Pattern 3</option>
          <option value="4">Pattern 4</option>
          <option value="5">Pattern 5</option>
        </select>
      </div>
      <div class="box">
        <label>Buzzer Test</label>
        <button class="warn" onclick="testBuzzer()">Test Buzzer</button>
      </div>
      <div class="box">
        <label>Save</label>
        <button class="primary" onclick="saveSettings()">Save Settings</button>
      </div>
    </div>

    <h3>Contacts (5)</h3>
    <div class="box">

      <div class="contactRow grid7">
        <div><label>Name 1</label><input type="text" id="name0"></div>
        <div><label>Number 1</label><input type="text" id="num0"></div>
        <div><label>SMS</label><label class="switch"><input type="checkbox" id="sms0"><span class="slider"></span></label></div>
        <div><label>Call</label><label class="switch"><input type="checkbox" id="call0" onchange="onlyOneCall(0)"><span class="slider"></span></label></div>
        <div><button class="warn" onclick="testSMSContact(0)">Test SMS</button></div>
        <div><button class="warn" onclick="testCallContact(0)">Test Call</button></div>
        <div><button class="light" onclick="removeContact(0)">Remove</button></div>
      </div>

      <div class="contactRow grid7">
        <div><label>Name 2</label><input type="text" id="name1"></div>
        <div><label>Number 2</label><input type="text" id="num1"></div>
        <div><label>SMS</label><label class="switch"><input type="checkbox" id="sms1"><span class="slider"></span></label></div>
        <div><label>Call</label><label class="switch"><input type="checkbox" id="call1" onchange="onlyOneCall(1)"><span class="slider"></span></label></div>
        <div><button class="warn" onclick="testSMSContact(1)">Test SMS</button></div>
        <div><button class="warn" onclick="testCallContact(1)">Test Call</button></div>
        <div><button class="light" onclick="removeContact(1)">Remove</button></div>
      </div>

      <div class="contactRow grid7">
        <div><label>Name 3</label><input type="text" id="name2"></div>
        <div><label>Number 3</label><input type="text" id="num2"></div>
        <div><label>SMS</label><label class="switch"><input type="checkbox" id="sms2"><span class="slider"></span></label></div>
        <div><label>Call</label><label class="switch"><input type="checkbox" id="call2" onchange="onlyOneCall(2)"><span class="slider"></span></label></div>
        <div><button class="warn" onclick="testSMSContact(2)">Test SMS</button></div>
        <div><button class="warn" onclick="testCallContact(2)">Test Call</button></div>
        <div><button class="light" onclick="removeContact(2)">Remove</button></div>
      </div>

      <div class="contactRow grid7">
        <div><label>Name 4</label><input type="text" id="name3"></div>
        <div><label>Number 4</label><input type="text" id="num3"></div>
        <div><label>SMS</label><label class="switch"><input type="checkbox" id="sms3"><span class="slider"></span></label></div>
        <div><label>Call</label><label class="switch"><input type="checkbox" id="call3" onchange="onlyOneCall(3)"><span class="slider"></span></label></div>
        <div><button class="warn" onclick="testSMSContact(3)">Test SMS</button></div>
        <div><button class="warn" onclick="testCallContact(3)">Test Call</button></div>
        <div><button class="light" onclick="removeContact(3)">Remove</button></div>
      </div>

      <div class="contactRow grid7">
        <div><label>Name 5</label><input type="text" id="name4"></div>
        <div><label>Number 5</label><input type="text" id="num4"></div>
        <div><label>SMS</label><label class="switch"><input type="checkbox" id="sms4"><span class="slider"></span></label></div>
        <div><label>Call</label><label class="switch"><input type="checkbox" id="call4" onchange="onlyOneCall(4)"><span class="slider"></span></label></div>
        <div><button class="warn" onclick="testSMSContact(4)">Test SMS</button></div>
        <div><button class="warn" onclick="testCallContact(4)">Test Call</button></div>
        <div><button class="light" onclick="removeContact(4)">Remove</button></div>
      </div>

      <button class="primary" onclick="saveContacts()">Save Contacts</button>
      <button class="light" onclick="loadContacts()">Load Contacts</button>
    </div>

    <h3>Debug</h3>
    <pre id="debugBox">Loading...</pre>
  </div>
</div>

<script>
function onlyOneCall(index){
  for(let i=0;i<5;i++){
    if(i !== index){
      document.getElementById('call'+i).checked = false;
    }
  }
}

function removeContact(index){
  document.getElementById('name'+index).value = '';
  document.getElementById('num'+index).value = '';
  document.getElementById('sms'+index).checked = false;
  document.getElementById('call'+index).checked = false;
}

async function refreshStatus(){
  const res = await fetch('/status');
  const d = await res.json();

  document.getElementById('internetStatus').textContent = d.internet ? 'Connected' : 'Not Connected';
  document.getElementById('netDot').className = 'dot ' + (d.internet ? 'green' : 'red');

  document.getElementById('wifiState').textContent = d.wifiConnected ? 'Connected' : 'Disconnected';
  document.getElementById('ssidNow').textContent = d.ssid || '-';
  document.getElementById('ipNow').textContent = d.ip || '-';
  document.getElementById('savedSsid').textContent = d.saved || '-';

  document.getElementById('simReady').textContent = d.simReady ? 'Yes' : 'No';
  document.getElementById('simNetwork').textContent = d.simNetwork || '-';
  document.getElementById('simSignal').textContent = (d.simSignalText || '-') + ' (CSQ: ' + d.simSignal + ')';
  document.getElementById('simOperator').textContent = d.simOperator || '-';

  document.getElementById('emergencyStatus').textContent = d.emergencyStatus || 'Normal';
  document.getElementById('emergencyTimer').textContent = d.emergencyActive ? ('(' + d.emergencyRemaining + ' sec)') : '';
}

async function refreshGPS(){
  const res = await fetch('/gps');
  const d = await res.json();

  document.getElementById('gpsFix').textContent = d.fix;
  document.getElementById('gpsLat').textContent = d.lat;
  document.getElementById('gpsLng').textContent = d.lng;
  document.getElementById('gpsAlt').textContent = d.alt;
  document.getElementById('gpsSpeed').textContent = d.speed;
  document.getElementById('gpsSats').textContent = d.sats;
  document.getElementById('gpsTime').textContent = d.time;
  document.getElementById('gpsDate').textContent = d.date;

  let mapHref = '#';
  if(d.lat !== '-' && d.lng !== '-'){
    mapHref = 'https://maps.google.com/?q=' + d.lat + ',' + d.lng;
  }
  document.getElementById('mapBtn').href = mapHref;
}

async function refreshDebug(){
  const res = await fetch('/debug');
  const d = await res.json();
  document.getElementById('debugBox').textContent = d.debug || '-';
}

async function triggerEmergency(){
  await fetch('/triggerEmergency', {method:'POST'});
  await refreshStatus();
  await refreshDebug();
}

async function testSMSContact(index){
  const body = new URLSearchParams();
  body.append('index', index);

  await fetch('/testSMSContact', {
    method:'POST',
    headers:{'Content-Type':'application/x-www-form-urlencoded'},
    body: body.toString()
  });
  await refreshDebug();
}

async function testCallContact(index){
  const body = new URLSearchParams();
  body.append('index', index);

  await fetch('/testCallContact', {
    method:'POST',
    headers:{'Content-Type':'application/x-www-form-urlencoded'},
    body: body.toString()
  });
  await refreshDebug();
}

async function testLed(){
  await fetch('/triggerEmergency', {method:'POST'});
  await refreshDebug();
}

async function testBuzzer(){
  await fetch('/triggerEmergency', {method:'POST'});
  await refreshDebug();
}

async function saveWiFi(){
  const body = new URLSearchParams();
  body.append('ssid', document.getElementById('wifiSSID').value);
  body.append('password', document.getElementById('wifiPASS').value);

  await fetch('/connect', {
    method:'POST',
    headers:{'Content-Type':'application/x-www-form-urlencoded'},
    body:body.toString()
  });

  await refreshStatus();
  await refreshDebug();
}

async function scanWiFi(){
  document.getElementById('scanResult').innerHTML = 'Scanning...';
  try{
    const res = await fetch('/scan');
    const d = await res.json();
    let html = '';
    if(!d.networks || d.networks.length === 0){
      html = 'No networks found';
    } else {
      d.networks.forEach(n => {
        html += `<div style="padding:8px;border-bottom:1px solid #ddd;cursor:pointer" onclick="pickSSID('${n.ssid}')">${n.ssid} (${n.rssi} dBm)</div>`;
      });
    }
    document.getElementById('scanResult').innerHTML = html;
  }catch(e){
    document.getElementById('scanResult').innerHTML = 'Scan failed';
  }
}

function pickSSID(s){
  document.getElementById('wifiSSID').value = s;
}

async function saveSettings(){
  const body = new URLSearchParams();

  body.append('callDuration', document.getElementById('callDuration').value);
  body.append('callRepeat', document.getElementById('callRepeat').value);

  body.append('ledEnabled', document.getElementById('ledEnabled').checked ? '1' : '0');
  body.append('ledRunSeconds', document.getElementById('ledRunSeconds').value);
  body.append('ledPattern', document.getElementById('ledPattern').value);

  body.append('buzzerEnabled', document.getElementById('buzzerEnabled').checked ? '1' : '0');
  body.append('buzzerRunSeconds', document.getElementById('buzzerRunSeconds').value);
  body.append('buzzerPattern', document.getElementById('buzzerPattern').value);

  await fetch('/saveSettings', {
    method:'POST',
    headers:{'Content-Type':'application/x-www-form-urlencoded'},
    body:body.toString()
  });

  await refreshDebug();
}

async function loadSettings(){
  const res = await fetch('/getSettings');
  const d = await res.json();

  document.getElementById('callDuration').value = d.callDuration || 20;
  document.getElementById('callRepeat').value = d.callRepeat || 1;

  document.getElementById('ledEnabled').checked = d.ledEnabled || false;
  document.getElementById('ledRunSeconds').value = d.ledRunSeconds || 10;
  document.getElementById('ledPattern').value = d.ledPattern || 1;

  document.getElementById('buzzerEnabled').checked = d.buzzerEnabled || false;
  document.getElementById('buzzerRunSeconds').value = d.buzzerRunSeconds || 10;
  document.getElementById('buzzerPattern').value = d.buzzerPattern || 1;
}

async function saveContacts(){
  const body = new URLSearchParams();
  for(let i=0;i<5;i++){
    body.append('name'+i, document.getElementById('name'+i).value);
    body.append('num'+i, document.getElementById('num'+i).value);
    body.append('sms'+i, document.getElementById('sms'+i).checked ? '1' : '0');
    body.append('call'+i, document.getElementById('call'+i).checked ? '1' : '0');
  }

  await fetch('/saveContacts', {
    method:'POST',
    headers:{'Content-Type':'application/x-www-form-urlencoded'},
    body:body.toString()
  });

  await refreshDebug();
}

async function loadContacts(){
  const res = await fetch('/getContacts');
  const d = await res.json();
  for(let i=0;i<5;i++){
    document.getElementById('name'+i).value = d[i].name || '';
    document.getElementById('num'+i).value = d[i].number || '';
    document.getElementById('sms'+i).checked = d[i].smsEnabled || false;
    document.getElementById('call'+i).checked = d[i].callEnabled || false;
  }
}

setInterval(async ()=>{
  await refreshStatus();
  await refreshGPS();
  await refreshDebug();
}, 3000);

window.onload = async function(){
  await refreshStatus();
  await refreshGPS();
  await refreshDebug();
  await loadContacts();
  await loadSettings();
}
</script>
</body>
</html>
)rawliteral";

// ---------------- Setup ----------------
void setup() {
  Serial.begin(115200);
  // CLEAR OLD SAVED CONTACTS (RUN ONLY ONCE)
  preferences.begin("contacts", false);
  preferences.clear();
  preferences.end();
  delay(500);

  pinMode(emergencyButtonPin, INPUT_PULLUP);

  buzzerOff();

  ring.begin();
  ring.setBrightness(50);
  ringOff();

  gpsSerial.begin(9600, SERIAL_8N1, 16, 17);
  simSerial.begin(9600, SERIAL_8N1, 27, 26);

  loadCredentials();
  loadContacts();
  loadSettings();

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(apSSID, apPassword);
  delay(200);

  if (savedSSID.length() > 0) {
    connectToRouter(savedSSID, savedPASS, 8000);
  }

  updateWiFiStatus();
  if (wifiConnected) internetOK = checkInternet();

  initSIM800L();
  quickSIMCheck();
  slowSIMCheck();
  updateGPSData();

  server.on("/", HTTP_GET, handleRoot);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/gps", HTTP_GET, handleGPS);
  server.on("/triggerEmergency", HTTP_POST, handleTriggerEmergency);
  server.on("/triggerEmergency", HTTP_GET, handleTriggerEmergency);
  server.on("/testSMSContact", HTTP_POST, handleTestSMSContact);
  server.on("/testCallContact", HTTP_POST, handleTestCallContact);
  server.on("/scan", HTTP_GET, handleScan);
  server.on("/connect", HTTP_POST, handleConnect);
  server.on("/saveSettings", HTTP_POST, handleSaveSettings);
  server.on("/getSettings", HTTP_GET, handleGetSettings);
  server.on("/saveContacts", HTTP_POST, handleSaveContacts);
  server.on("/getContacts", HTTP_GET, handleGetContacts);
  server.on("/debug", HTTP_GET, handleDebug);
  server.onNotFound(handleNotFound);

  server.begin();
  setDebug("System ready. Button on GPIO25, LED on GPIO4, buzzer on GPIO33");
}

// ---------------- Loop ----------------
void loop() {
  server.handleClient();
  yield();

  updateEmergencyButton();
  updateLedPattern();
  updateBuzzerPattern();

  if (millis() - lastGPSCheck >= gpsCheckInterval) {
    lastGPSCheck = millis();
    updateGPSData();
  }

  if (millis() - lastWiFiCheck >= wifiCheckInterval) {
    lastWiFiCheck = millis();
    updateWiFiStatus();
  }

  if (wifiConnected && millis() - lastInternetCheck >= internetCheckInterval) {
    lastInternetCheck = millis();
    internetOK = checkInternet();
  }

  if (millis() - lastSimSignalCheck >= simSignalCheckInterval) {
    lastSimSignalCheck = millis();
    quickSIMCheck();
  }

  if (millis() - lastSimRegisterCheck >= simRegisterCheckInterval) {
    lastSimRegisterCheck = millis();
    slowSIMCheck();
  }

  if (emergencyActive && millis() - emergencyStartTime >= emergencyDuration) {
    emergencyActive = false;
    smsSentForCurrentEmergency = false;
    callDoneForCurrentEmergency = false;
    setDebug("Emergency ended, back to normal");
  }

  trySendEmergencySMSAndCall();
}