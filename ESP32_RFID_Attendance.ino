/*
  =============================================================================
  ESP32 + RC522 RFID + I2C LCD 16x2 Standalone Attendance System
  =============================================================================
  Features:
    1. Standalone Wi-Fi Hotspot (AP Mode) with Captive Portal (192.168.4.1)
    2. Embedded Modern Material Design 3 Web App with Dark & Light Mode
    3. Dedicated Entrance Screen & Full-Screen Kiosk Console (60% RFID / 40% Log)
    4. Hardware I2C 16x2 LCD Support (Auto-detects address 0x27 or 0x3F)
       - Row 0: "TIME IN" or "TIME OUT" (Centered)
       - Row 1: "SURNAME, First letter of first name" (Displayed for 1.5 seconds)
       - Idle State: Shows live system clock and attendance mode
    5. Software RTC Clock Sync (No RTC Hardware Module Required)
    6. Two-Step Tag Registration in Settings Modal -> Saved permanently to NVS
    7. Session Attendance with Strict Deduplication ("Repeated cards listed as ONE")
    8. CSV Attendance Log & Card Database Export

  =============================================================================
  WIRING PINOUT GUIDE:
  =============================================================================
  1. MFRC522 RFID READER to ESP32 (SPI):
     RC522 Pin      | ESP32 Pin      | Notes
     --------------------------------------------------------------------------
     3.3V (VCC)     | 3V3            | WARNING: 3.3V ONLY! DO NOT CONNECT TO 5V!
     RST            | GPIO 4         | Reset Pin (Moved to 4 to free 22 for I2C)
     GND            | GND            | Common Ground
     IRQ            | (Leave empty)  | Not connected (software polling mode)
     MISO           | GPIO 19        | VSPI MISO
     MOSI           | GPIO 23        | VSPI MOSI
     SCK            | GPIO 18        | VSPI SCK
     SDA (SS)       | GPIO 5         | VSPI SS / Chip Select
     --------------------------------------------------------------------------
  2. I2C 16x2 LCD DISPLAY to ESP32 (I2C):
     LCD Pin        | ESP32 Pin      | Notes
     --------------------------------------------------------------------------
     GND            | GND            | Common Ground
     VCC            | VIN / 5V       | 5V Power for standard HD44780 LCD backpack
     SDA            | GPIO 21        | Hardware I2C SDA
     SCL            | GPIO 22        | Hardware I2C SCL (Auto-detects 0x27 / 0x3F)
     --------------------------------------------------------------------------
  3. BUILT-IN STATUS LED:
     LED Indicator  | GPIO 2         | Built-in blue LED flashes on card scan
  =============================================================================
*/

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Preferences.h>
#include <LittleFS.h>
#include "esp_partition.h"
#include <time.h>
#include <sys/time.h>
#include <vector>

// -----------------------------------------------------------------------------
// Pin Configuration
// -----------------------------------------------------------------------------
#define RC522_SS_PIN    5
#define RC522_RST_PIN   4   // Moved from 22 to 4 to free GPIO 22 for I2C SCL!
#define STATUS_LED      2
#define I2C_SDA_PIN    21   // Standard ESP32 I2C SDA
#define I2C_SCL_PIN    22   // Standard ESP32 I2C SCL

// Common Anode RGB LED Configuration (Longest pin to 3.3V, Cathodes via 220-330R resistors)
#define RGB_RED_PIN    25   // Red cathode
#define RGB_GREEN_PIN  26   // Green cathode
#define RGB_BLUE_PIN   27   // Blue cathode

// I2C LCD Hardware Backlight PWM Pin (Connect to I2C backpack backlight jumper)
#define LCD_BACKLIGHT_PWM_PIN 14

// -----------------------------------------------------------------------------
// I2C 16x2 LCD Configuration (Auto-detected at 0x27 or 0x3F)
// -----------------------------------------------------------------------------
LiquidCrystal_I2C lcd(0x27, 16, 2);
bool lcdAvailable = false;
bool lcdBacklightOn = true;
unsigned long lcdMessageUntil = 0;   // Timestamp until temporary card message expires (1.5s)
unsigned long lastLcdIdleUpdate = 0;
unsigned long lastLcdActivity = 0;
unsigned long lcdStandbyTimeoutMs = 45000UL; // 0 = always on
bool showLcdWifiGuide = true;

// LCD Backlight Brightness & Pre-Sleep Dimming Configuration (Full Range 0..255 PWM)
uint8_t lcdActiveBrightness = 255;        // 10..255 (Default 255 = 100% full brightness)
uint8_t lcdDimBrightness = 50;            // ~20% dim brightness
unsigned long lcdDimDurationMs = 10000UL; // 10s dim warning before complete sleep (0 = disabled)
uint8_t currentLcdPwm = 255;
bool lcdIsDimmed = false;
bool lcdManualSleep = false; // When true, manual screen sleep is active until wakeLcd()

// -----------------------------------------------------------------------------
// Wi-Fi Access Point & Captive Portal Configuration
// -----------------------------------------------------------------------------
const char* AP_SSID = "ESP32-Attendance";
String currentApPass = "12345678"; // Active Wi-Fi password for current boot
String nextApPass = "12345678";    // Wi-Fi password queued to apply on next boot
const byte  DNS_PORT = 53;
const IPAddress AP_IP(192, 168, 4, 1);
const IPAddress AP_NETMASK(255, 255, 255, 0);

DNSServer dnsServer;
WebServer server(80);
MFRC522   rfid(RC522_SS_PIN, RC522_RST_PIN);
Preferences prefs;

// -----------------------------------------------------------------------------
// Data Structures
// -----------------------------------------------------------------------------
struct RegisteredCard {
  String uid;
  String name;
  String studentId;
  String regDate;
};

struct SavedSessionItem {
  String id;
  String title;
  String type;
  String dateStr;
  String saveTime;
  int attendeeCount;
};
std::vector<SavedSessionItem> sessionHistory;

struct AttendanceRecord {
  String uid;
  String name;
  String studentId;
  String inTime;
  String outTime;
  time_t inTimestamp;
  time_t outTimestamp;
};

struct LastScan {
  String uid;
  String name;
  String timeStr;
  String type;
  bool isDuplicate;
  bool isRegistered;
  String message;
  unsigned long scanMillis;
};

// -----------------------------------------------------------------------------
// Global State Variables
// -----------------------------------------------------------------------------
std::vector<RegisteredCard> registeredCards;
std::vector<AttendanceRecord> attendanceRecords;

bool isTimeSet = false;
bool sessionActive = false;
String sessionType = "Entrance"; // "Entrance" or "Exit"
String sessionTitle = "General Session";
time_t sessionStartTime = 0;
bool requireRegistration = true; // Reject unknown cards if true
bool isSettingsOpen = false; // True when Settings/Modal is open on client
bool isRegistrationTab = false; // True when user is actively in single/batch card registration

enum RegState { REG_IDLE, REG_WAITING, REG_SUCCESS, REG_SAME_CARD, REG_CONFLICT, REG_TIMEOUT, REG_CANCELLED };
RegState regState = REG_IDLE;

enum RgbFeedbackType {
  RGB_FEEDBACK_NONE,
  RGB_FEEDBACK_RECORDED,         // Green: Recorded attendance or new tag registration (1.5s)
  RGB_FEEDBACK_DUPLICATE,        // Yellow: Duplicate / repeated card tap (1.5s)
  RGB_FEEDBACK_REJECTED,         // Red: Unregistered / Denied / Conflict (1.5s)
  RGB_FEEDBACK_WHITE_BLINK_TEST  // White blink test (1.5s)
};
String regCandidateName = "";
String regCandidateId = "";
String regBatchInfo = "";
String regConflictName = "";
String regConflictId = "";
String regResultUid = "";
String regResultMsg = "";
unsigned long regStartMillis = 0;
const unsigned long REG_TIMEOUT_MS = 30000;

unsigned long lastCardTapMillis = 0;
unsigned long cardDebounceMs = 1500;
unsigned long ledOffMillis = 0;

LastScan lastScan = {"", "", "", "", false, false, "System ready", 0};

// -----------------------------------------------------------------------------
// Helper: String JSON / CSV Escaping
// -----------------------------------------------------------------------------
String escapeJson(const String& s) {
  String out = "";
  out.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"') out += "\"";
    else if (c == '\\') out += "\\\\";
    else if (c == '\n') out += "\\n";
    else if (c == '\r') out += "\\r";
    else if (c == '\t') out += "\\t";
    else if ((uint8_t)c < 32) continue; // Drop non-printable control chars to ensure valid JSON
    else out += c;
  }
  return out;
}

String escapeCsv(const String& s) {
  String out = "";
  out.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"') out += "\"\"";
    else out += c;
  }
  return out;
}

String sanitizeField(const String& s) {
  String out = "";
  out.reserve(s.length());
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '\t' || c == '\n' || c == '\r') out += ' ';
    else out += c;
  }
  out.trim();
  return out;
}

String sanitizeFilename(const String& name) {
  String trimmed = name;
  trimmed.trim();
  if (trimmed.length() == 0) return "attendance";
  String out = "";
  out.reserve(trimmed.length() + 4);
  for (size_t i = 0; i < trimmed.length(); i++) {
    char c = trimmed[i];
    if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || 
        c == '"' || c == '<' || c == '>' || c == '|' || c == '\t' || 
        c == '\r' || c == '\n' || c == ' ' || c == ',' || c == ';') {
      if (out.length() > 0 && out[out.length() - 1] != '_') {
        out += '_';
      }
    } else {
      out += c;
    }
  }
  while (out.startsWith("_")) out = out.substring(1);
  while (out.endsWith("_")) out = out.substring(0, out.length() - 1);
  return out.length() > 0 ? out : "attendance";
}

// -----------------------------------------------------------------------------
// Clock & Time Helpers (12-Hour Basis with AM/PM)
// -----------------------------------------------------------------------------
String getCurrentTimeString() {
  if (!isTimeSet) {
    return "Time Not Set";
  }
  time_t now = time(nullptr);
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%d %I:%M:%S %p", &timeinfo);
  return String(buf);
}

// Returns date string (e.g. "2026-11-10")
String getCurrentDateString() {
  if (!isTimeSet) {
    return "1970-01-01";
  }
  time_t now = time(nullptr);
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  char buf[20];
  strftime(buf, sizeof(buf), "%Y-%m-%d", &timeinfo);
  return String(buf);
}

// Returns ONLY 12-hour time (e.g. "04:56:15 PM") for same-day attendance logs
String getCurrentTime12h() {
  if (!isTimeSet) {
    return "--:--:--";
  }
  time_t now = time(nullptr);
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  char buf[32];
  strftime(buf, sizeof(buf), "%I:%M:%S %p", &timeinfo);
  return String(buf);
}

// Compact 12-hour time for 16x2 LCD standby clock (e.g. "04:56 PM")
String getLcdTime12h() {
  if (!isTimeSet) {
    return "--:--";
  }
  time_t now = time(nullptr);
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  char buf[16];
  strftime(buf, sizeof(buf), "%I:%M %p", &timeinfo);
  return String(buf);
}

// -----------------------------------------------------------------------------
// I2C LCD Formatting & Control Functions
// -----------------------------------------------------------------------------
String centerText(String text, int width = 16) {
  if ((int)text.length() >= width) {
    return text.substring(0, width);
  }
  int totalSpaces = width - text.length();
  int leftSpaces = totalSpaces / 2;
  int rightSpaces = totalSpaces - leftSpaces;
  String res = "";
  for (int i = 0; i < leftSpaces; i++) res += " ";
  res += text;
  for (int i = 0; i < rightSpaces; i++) res += " ";
  return res;
}

// Formats name as: (SURNAME, First letter of first name)
// e.g. "Juan Dela Cruz" -> "DELA CRUZ, J."
// e.g. "John Smith"     -> "SMITH, J."
// e.g. "Cruz, Juan"     -> "CRUZ, J."
String formatLcdName(String raw) {
  raw.trim();
  if (raw.length() == 0) return "UNKNOWN";

  // If already formatted with comma (e.g. "Cruz, Juan")
  int commaIdx = raw.indexOf(',');
  if (commaIdx != -1) {
    String surname = raw.substring(0, commaIdx);
    surname.trim();
    surname.toUpperCase();
    String first = raw.substring(commaIdx + 1);
    first.trim();
    char initial = (first.length() > 0) ? toupper(first[0]) : ' ';
    if (initial != ' ') {
      return surname + ", " + String(initial) + ".";
    }
    return surname;
  }

  // Split into words by spaces
  std::vector<String> words;
  int start = 0;
  for (unsigned int i = 0; i <= raw.length(); i++) {
    if (i == raw.length() || raw[i] == ' ') {
      if (i > (unsigned int)start) {
        String w = raw.substring(start, i);
        w.trim();
        if (w.length() > 0) words.push_back(w);
      }
      start = i + 1;
    }
  }

  if (words.size() == 0) return "UNKNOWN";
  if (words.size() == 1) {
    String s = words[0];
    s.toUpperCase();
    return s;
  }

  // Check if second-to-last word is a compound prefix (e.g. "Dela", "De", "Del", "San")
  String surname = "";
  char initial = toupper(words[0][0]);

  if (words.size() >= 3) {
    String prev = words[words.size() - 2];
    prev.toLowerCase();
    if (prev == "de" || prev == "dela" || prev == "del" || prev == "delos" || prev == "san" || prev == "santa") {
      surname = words[words.size() - 2] + " " + words[words.size() - 1];
    } else {
      surname = words[words.size() - 1];
    }
  } else {
    surname = words[words.size() - 1];
  }

  surname.toUpperCase();
  return surname + ", " + String(initial) + ".";
}

bool initLcd() {
  pinMode(LCD_BACKLIGHT_PWM_PIN, OUTPUT);
  applyLcdPwm(lcdActiveBrightness);

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  delay(50);

  // Scan common I2C LCD backpack addresses (PCF8574: 0x20-0x27, PCF8574A: 0x38-0x3F)
  const uint8_t commonAddrs[] = { 0x27, 0x3F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E };
  for (size_t i = 0; i < sizeof(commonAddrs); i++) {
    uint8_t addr = commonAddrs[i];
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      lcd = LiquidCrystal_I2C(addr, 16, 2);
      lcd.init();
      lcd.backlight();
      lcdAvailable = true;
      lcdBacklightOn = true;
      lastLcdActivity = millis();
      Serial.printf("[LCD] I2C 16x2 LCD successfully initialized at address 0x%02X\n", addr);
      lcd.setCursor(0, 0);
      lcd.print(centerText("ESP32 ATTENDANCE"));
      lcd.setCursor(0, 1);
      lcd.print(centerText("SYSTEM READY"));
      delay(500);
      return true;
    }
  }

  Serial.println("[LCD NOTICE] No I2C LCD found at standard addresses. Operating in GPIO 14 PWM backlight mode.");
  lcdAvailable = false;
  return false;
}

// -----------------------------------------------------------------------------
// LCD Power Management & PWM Backlight Dimming Functions
// -----------------------------------------------------------------------------
void applyLcdPwm(uint8_t pwm) {
  currentLcdPwm = pwm;
  if (pwm == 0) {
    ledcDetach(LCD_BACKLIGHT_PWM_PIN);
    pinMode(LCD_BACKLIGHT_PWM_PIN, OUTPUT);
    digitalWrite(LCD_BACKLIGHT_PWM_PIN, LOW);
    if (lcdAvailable) {
      lcd.noBacklight();
      lcdBacklightOn = false;
    }
  } else {
    // Hardware PWM on GPIO 14 (5 kHz hardware timer, zero flicker, zero I2C traffic)
    pinMode(LCD_BACKLIGHT_PWM_PIN, OUTPUT);
    analogWrite(LCD_BACKLIGHT_PWM_PIN, pwm);
    if (lcdAvailable) {
      if (!lcdBacklightOn) {
        lcd.backlight();
        lcdBacklightOn = true;
      }
    }
  }
}

void wakeLcd() {
  lcdManualSleep = false;
  lastLcdActivity = millis();
  lcdIsDimmed = false;
  applyLcdPwm(lcdActiveBrightness);
  if (lcdAvailable) {
    lcd.backlight();
    lcdBacklightOn = true;
    lastLcdIdleUpdate = 0; // Trigger instant redraw on next loop
  }
  Serial.printf("[LCD] Backlight awakened (PWM: %u / 255).\n", lcdActiveBrightness);
}

void sleepLcd() {
  lcdManualSleep = true;
  lcdIsDimmed = false;
  applyLcdPwm(0);
  if (lcdAvailable) {
    lcd.noBacklight();
    lcd.clear();
    lcdBacklightOn = false;
  }
  Serial.println("[LCD] Screen turned OFF (GPIO 14 LOW, PWM 0).");
}

void checkLcdStandbyTimeout() {
  // If manual sleep is triggered from UI, stay asleep!
  if (lcdManualSleep) {
    if (currentLcdPwm != 0) {
      applyLcdPwm(0);
    }
    return;
  }

  if (millis() < lcdMessageUntil) {
    // Keep showing temporary attendance/test message
    if (currentLcdPwm != lcdActiveBrightness) {
      applyLcdPwm(lcdActiveBrightness);
      lcdIsDimmed = false;
    }
    return;
  }

  if (regState != REG_IDLE) {
    lastLcdActivity = millis(); // Never sleep while registering cards
    if (currentLcdPwm != lcdActiveBrightness) {
      applyLcdPwm(lcdActiveBrightness);
      lcdIsDimmed = false;
    }
    return;
  }

  if (lcdStandbyTimeoutMs == 0) {
    // Always ON: maintain configured active brightness
    if (currentLcdPwm != lcdActiveBrightness) {
      applyLcdPwm(lcdActiveBrightness);
      lcdIsDimmed = false;
    }
    return;
  }

  unsigned long elapsed = millis() - lastLcdActivity;
  if (elapsed >= lcdStandbyTimeoutMs) {
    // Screen OFF: Standby timeout elapsed
    if (lcdBacklightOn || currentLcdPwm > 0) {
      lcdIsDimmed = false;
      applyLcdPwm(0);
      if (lcdAvailable) {
        lcd.noBacklight();
        lcd.clear();
        lcdBacklightOn = false;
      }
      Serial.println("[LCD] Standby timeout reached -> Backlight turned OFF to save power.");
    }
  } else {
    // Check pre-sleep screen dimming warning
    unsigned long effectiveDimMs = (lcdDimDurationMs < lcdStandbyTimeoutMs) ? lcdDimDurationMs : (lcdStandbyTimeoutMs / 2);
    if (effectiveDimMs > 0 && elapsed >= (lcdStandbyTimeoutMs - effectiveDimMs)) {
      // Screen is currently in pre-sleep dim warning phase
      unsigned long dimElapsed = elapsed - (lcdStandbyTimeoutMs - effectiveDimMs);
      float factor = (float)dimElapsed / (float)effectiveDimMs; // 0.0 to 1.0
      uint8_t targetPwm = (uint8_t)roundf((float)lcdActiveBrightness * (1.0f - factor) + (float)lcdDimBrightness * factor);
      if (targetPwm != currentLcdPwm) {
        applyLcdPwm(targetPwm);
      }
      if (!lcdIsDimmed) {
        lcdIsDimmed = true;
        Serial.printf("[LCD] Pre-sleep dimming started (%u PWM)...\n", targetPwm);
      }
    } else {
      // Normal active state
      if (currentLcdPwm != lcdActiveBrightness) {
        applyLcdPwm(lcdActiveBrightness);
        lcdIsDimmed = false;
      }
    }
  }
}


void showLcdAttendance(const String& direction, const String& attendeeName) {
  wakeLcd();
  if (!lcdAvailable) return;
  String row0Text = (direction == "Exit") ? "TIME OUT" : "TIME IN";
  String row1Text = formatLcdName(attendeeName);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(centerText(row0Text));
  lcd.setCursor(0, 1);
  lcd.print(centerText(row1Text));

  lcdMessageUntil = millis() + 3000;
}

void showLcdDuplicate(const String& attendeeName) {
  wakeLcd();
  if (!lcdAvailable) return;
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(centerText("ALREADY LOGGED"));
  lcd.setCursor(0, 1);
  lcd.print(centerText(formatLcdName(attendeeName)));
  lcdMessageUntil = millis() + 3000;
}

void showLcdDenied() {
  wakeLcd();
  if (!lcdAvailable) return;
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(centerText("ACCESS DENIED"));
  lcd.setCursor(0, 1);
  lcd.print(centerText("UNREGISTERED TAG"));
  lcdMessageUntil = millis() + 3000;
}

void showLcdRegistered(const String& attendeeName) {
  wakeLcd();
  if (!lcdAvailable) return;
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(centerText("TAG REGISTERED"));
  lcd.setCursor(0, 1);
  lcd.print(centerText(formatLcdName(attendeeName)));
  lcdMessageUntil = millis() + 3000;
}

// Candidate Identity Matching Helper (Matches case, IDs, and name order variants)
bool isSameCandidate(const String& cardName, const String& cardId, const String& candName, const String& candId) {
  String cId = cardId; cId.trim();
  String cdId = candId; cdId.trim();
  if (cId.length() > 0 && cdId.length() > 0 && cId.equalsIgnoreCase(cdId)) return true;

  String cName = cardName; cName.trim();
  String cdName = candName; cdName.trim();
  if (cName.length() > 0 && cdName.length() > 0 && cName.equalsIgnoreCase(cdName)) return true;

  auto getTokens = [](String s) -> std::vector<String> {
    std::vector<String> tokens;
    s.toUpperCase();
    String cur = "";
    for (size_t i = 0; i < s.length(); i++) {
      char ch = s[i];
      if (isalnum(ch)) {
        cur += ch;
      } else if (cur.length() > 0) {
        tokens.push_back(cur);
        cur = "";
      }
    }
    if (cur.length() > 0) tokens.push_back(cur);
    std::sort(tokens.begin(), tokens.end());
    return tokens;
  };

  std::vector<String> t1 = getTokens(cName);
  std::vector<String> t2 = getTokens(cdName);
  if (!t1.empty() && t1 == t2) return true;

  return false;
}

void showLcdAlreadyRegistered(const String& attendeeName) {
  wakeLcd();
  if (!lcdAvailable) return;
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(centerText("CARD REGISTERED"));
  lcd.setCursor(0, 1);
  lcd.print(centerText(formatLcdName(attendeeName)));
  lcdMessageUntil = millis() + 3000;
}

void showLcdCardInUse(const String& existingOwner) {
  wakeLcd();
  if (!lcdAvailable) return;
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(centerText("USE ANOTHER CARD"));
  lcd.setCursor(0, 1);
  lcd.print(centerText(formatLcdName(existingOwner)));
  lcdMessageUntil = millis() + 3000;
}

void showLcdCardConflict(const String& existingOwner) {
  showLcdCardInUse(existingOwner);
}

void updateLcdIdle() {
  if (!lcdAvailable || !lcdBacklightOn || currentLcdPwm == 0) return;
  if (millis() < lcdMessageUntil) return; // Keep showing attendance message for 1.5s!

  if (millis() - lastLcdIdleUpdate < 1000) return; // Refresh once per second
  lastLcdIdleUpdate = millis();

  if (regState == REG_WAITING) {
    lcd.setCursor(0, 0);
    if (regBatchInfo.length() > 0) {
      lcd.print(centerText("BATCH REG " + regBatchInfo));
    } else {
      lcd.print(centerText("REGISTERING TAG"));
    }
    lcd.setCursor(0, 1);
    lcd.print(centerText(formatLcdName(regCandidateName)));
    return;
  }

  if (isRegistrationTab) {
    lcd.setCursor(0, 0);
    lcd.print(centerText("CARD REGISTRATION"));
    lcd.setCursor(0, 1);
    lcd.print(centerText("READY TO SCAN"));
    return;
  }

  if (sessionActive) {
    if (isSettingsOpen) {
      lcd.setCursor(0, 0);
      lcd.print(centerText("SETTINGS OPEN"));
      lcd.setCursor(0, 1);
      lcd.print(centerText("SCANNING PAUSED"));
    } else {
      // During active attendance: Normally show TAP CARD HERE, but every 12s show Wi-Fi info for 3s if enabled
      unsigned long activeStep = (millis() / 3000) % 4;
      if (showLcdWifiGuide && activeStep == 3) {
        lcd.setCursor(0, 0);
        lcd.print(centerText(String(AP_SSID)));
        lcd.setCursor(0, 1);
        String passStr = (currentApPass.length() > 0) ? ("Pass: " + currentApPass) : "(NO PASSWORD)";
        if (passStr.length() > 16 && currentApPass.length() > 0) passStr = currentApPass;
        lcd.print(centerText(passStr));
      } else {
        String modeStr = (sessionType == "Exit") ? "OUT" : "IN ";
        String timeStr = isTimeSet ? getLcdTime12h() : "--:--";
        String r0 = " [" + modeStr + "] " + timeStr + " ";
        String r1 = " TAP CARD HERE  ";

        lcd.setCursor(0, 0);
        lcd.print(centerText(r0));
        lcd.setCursor(0, 1);
        lcd.print(centerText(r1));
      }
    }
  } else {
    // Standby Mode (Session Inactive): Guide user to connect to Wi-Fi with SSID & Password if enabled
    if (showLcdWifiGuide) {
      unsigned long step = (millis() / 3000) % 4;
      if (step == 0) {
        // Slide 0: Guidance prompt
        lcd.setCursor(0, 0);
        lcd.print(centerText("CONNECT TO WIFI:"));
        lcd.setCursor(0, 1);
        lcd.print(centerText(String(AP_SSID)));
      } else if (step == 1) {
        // Slide 1: Exact Wi-Fi SSID and Password together
        lcd.setCursor(0, 0);
        lcd.print(centerText(String(AP_SSID)));
        lcd.setCursor(0, 1);
        String passStr = (currentApPass.length() > 0) ? ("Pass: " + currentApPass) : "(NO PASSWORD)";
        if (passStr.length() > 16 && currentApPass.length() > 0) passStr = currentApPass;
        lcd.print(centerText(passStr));
      } else if (step == 2) {
        // Slide 2: Web portal URL
        lcd.setCursor(0, 0);
        lcd.print(centerText("OPEN IN BROWSER:"));
        lcd.setCursor(0, 1);
        lcd.print(centerText("192.168.4.1"));
      } else {
        // Slide 3: Session prompt
        lcd.setCursor(0, 0);
        lcd.print(centerText("SESSION INACTIVE"));
        lcd.setCursor(0, 1);
        lcd.print(centerText("START IN WEBAPP"));
      }
    } else {
      String timeStr = isTimeSet ? getLcdTime12h() : "--:--";
      lcd.setCursor(0, 0);
      lcd.print(centerText("ESP32 ATTENDANCE"));
      lcd.setCursor(0, 1);
      lcd.print(centerText(timeStr + " READY"));
    }
  }
}

// -----------------------------------------------------------------------------
// LED Helper
// -----------------------------------------------------------------------------
void blinkLed(unsigned long durationMs = 200) {
  digitalWrite(STATUS_LED, HIGH);
  ledOffMillis = millis() + durationMs;
}

// -----------------------------------------------------------------------------
// Common Anode RGB LED Subsystem
// Longest pin (Common Anode) connects to 3.3V.
// Cathodes connect to GPIO 25 (Red), GPIO 26 (Green), GPIO 27 (Blue) via 220-330R resistors.
// Common Anode Logic (Inverted PWM):
//   analogWrite(pin, 255 - duty) where duty: 0 = OFF, 255 = Full Brightness
// -----------------------------------------------------------------------------
RgbFeedbackType currentRgbFeedback = RGB_FEEDBACK_NONE;
unsigned long rgbFeedbackStartMillis = 0;
const unsigned long RGB_FEEDBACK_DURATION_MS = 1500; // 1.5 seconds

// Permanent Bound: 0 to 168 PWM maximum duty (~66%) to safely protect pins and direct-wired LED
const uint8_t RGB_PWM_MAX_LIMIT = 168;
// Reduced bound for white blinking: 128 PWM (~50% duty) to limit combined 3-channel current & glare
const uint8_t RGB_WHITE_BLINK_PWM_LIMIT = 128;
uint8_t rgbActiveBrightness = 168; // 0 to 168 PWM (100% = 168)

void turnOffRgbLed() {
  // Inverted logic for Common Anode: 255 = 3.3V on cathode = 0V drop = OFF
  analogWrite(RGB_RED_PIN, 255);
  analogWrite(RGB_GREEN_PIN, 255);
  analogWrite(RGB_BLUE_PIN, 255);
}

void setRgbColorWithLimit(uint8_t r, uint8_t g, uint8_t b, uint8_t channelLimit) {
  uint8_t effectiveLimit = rgbActiveBrightness;
  if (effectiveLimit > channelLimit) effectiveLimit = channelLimit;

  uint8_t scaledR = (uint8_t)(((uint32_t)r * effectiveLimit) / 255);
  uint8_t scaledG = (uint8_t)(((uint32_t)g * effectiveLimit) / 255);
  uint8_t scaledB = (uint8_t)(((uint32_t)b * effectiveLimit) / 255);

  // Inverted PWM for Common Anode LED
  analogWrite(RGB_RED_PIN, 255 - scaledR);
  analogWrite(RGB_GREEN_PIN, 255 - scaledG);
  analogWrite(RGB_BLUE_PIN, 255 - scaledB);
}

void setRgbColor(uint8_t r, uint8_t g, uint8_t b) {
  setRgbColorWithLimit(r, g, b, RGB_PWM_MAX_LIMIT);
}

void triggerRgbFeedback(RgbFeedbackType type) {
  currentRgbFeedback = type;
  rgbFeedbackStartMillis = millis();
}

void initRgbLed() {
  pinMode(RGB_RED_PIN, OUTPUT);
  pinMode(RGB_GREEN_PIN, OUTPUT);
  pinMode(RGB_BLUE_PIN, OUTPUT);
  turnOffRgbLed();
}

void updateRgbLed() {
  unsigned long now = millis();

  // 1. Priority 1: Card Tap / Status Feedback in Session
  // Lights up solid for 1.5s with status color, then turns OFF right after
  if (currentRgbFeedback != RGB_FEEDBACK_NONE) {
    unsigned long elapsed = now - rgbFeedbackStartMillis;
    if (elapsed >= RGB_FEEDBACK_DURATION_MS) {
      currentRgbFeedback = RGB_FEEDBACK_NONE;
      turnOffRgbLed();
    } else {
      if (currentRgbFeedback == RGB_FEEDBACK_RECORDED) {
        // Solid Green: Recorded entrance, exit, or tag registration
        setRgbColor(0, 255, 0);
      } else if (currentRgbFeedback == RGB_FEEDBACK_DUPLICATE) {
        // Solid Yellow: Repeated card tap
        setRgbColor(255, 200, 0);
      } else if (currentRgbFeedback == RGB_FEEDBACK_REJECTED) {
        // Solid Red: Unregistered / Denied / Conflict
        setRgbColor(255, 0, 0);
      } else if (currentRgbFeedback == RGB_FEEDBACK_WHITE_BLINK_TEST) {
        // Solid White test (capped at 128 PWM)
        setRgbColorWithLimit(255, 255, 255, RGB_WHITE_BLINK_PWM_LIMIT);
      }
      return;
    }
  }

  // 2. Priority 2: Standby Sleep (When LCD is asleep from inactivity)
  bool isSleeping = (lcdStandbyTimeoutMs > 0 && (now - lastLcdActivity >= lcdStandbyTimeoutMs)) || !lcdBacklightOn;
  if (isSleeping) {
    turnOffRgbLed();
    return;
  }

  // 3. Priority 3: When in session (Normally OFF; lights up only when a card is read)
  if (sessionActive) {
    turnOffRgbLed();
    return;
  }

  // 4. Priority 4: Out of session (Smooth white breathing animation in low brightness)
  const unsigned long BREATH_CYCLE_MS = 2800; // 2.8s calm human breathing period
  float phase = (float)(now % BREATH_CYCLE_MS) / (float)BREATH_CYCLE_MS; // 0.0 to 1.0
  // Smooth cosine easing: 0.0 -> 1.0 -> 0.0
  float breathFactor = 0.5f * (1.0f - cosf(phase * 2.0f * 3.14159265f));
  // Quadratic curve matching human eye perception
  breathFactor = breathFactor * breathFactor;

  // Soft low brightness ceiling (capped at 36 PWM, ~14% duty)
  const uint8_t WHITE_BREATHE_MAX_PWM = 36;
  uint8_t effectiveMax = (uint8_t)(((uint32_t)rgbActiveBrightness * WHITE_BREATHE_MAX_PWM) / RGB_PWM_MAX_LIMIT);
  if (effectiveMax < 8) effectiveMax = 8;

  uint8_t currentBreathePwm = (uint8_t)roundf(breathFactor * (float)effectiveMax);
  setRgbColorWithLimit(255, 255, 255, currentBreathePwm);
}

// -----------------------------------------------------------------------------
// RFID Format Helper
// -----------------------------------------------------------------------------
String formatUID(MFRC522::Uid *uid) {
  String out = "";
  for (byte i = 0; i < uid->size; i++) {
    if (i > 0) out += ":";
    if (uid->uidByte[i] < 0x10) out += "0";
    out += String(uid->uidByte[i], HEX);
  }
  out.toUpperCase();
  return out;
}

// -----------------------------------------------------------------------------
// NVS Flash Persistence (Preferences)
// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
// LittleFS Session History Storage
// -----------------------------------------------------------------------------
void loadHistoryIndex() {
  sessionHistory.clear();
  if (!LittleFS.exists("/history_idx.txt")) {
    return;
  }
  File f = LittleFS.open("/history_idx.txt", "r");
  if (!f) return;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;
    int t1 = line.indexOf('\t');
    int t2 = line.indexOf('\t', t1 + 1);
    int t3 = line.indexOf('\t', t2 + 1);
    int t4 = line.indexOf('\t', t3 + 1);
    int t5 = line.indexOf('\t', t4 + 1);
    if (t1 != -1 && t2 != -1 && t3 != -1 && t4 != -1) {
      SavedSessionItem item;
      item.id = line.substring(0, t1);
      item.title = line.substring(t1 + 1, t2);
      item.type = line.substring(t2 + 1, t3);
      item.dateStr = line.substring(t3 + 1, t4);
      if (t5 != -1) {
        item.saveTime = line.substring(t4 + 1, t5);
        item.attendeeCount = line.substring(t5 + 1).toInt();
      } else {
        item.saveTime = line.substring(t4 + 1);
        item.attendeeCount = 0;
      }
      sessionHistory.push_back(item);
    }
  }
  f.close();
  while (sessionHistory.size() > 5) {
    SavedSessionItem oldest = sessionHistory.back();
    String oldPath = "/" + oldest.id + ".txt";
    if (LittleFS.exists(oldPath.c_str())) {
      LittleFS.remove(oldPath.c_str());
    }
    sessionHistory.pop_back();
  }
  Serial.printf("[LITTLEFS] Loaded %d sessions from history index (max 5 enforced).\n", sessionHistory.size());
}

void saveHistoryIndex() {
  File f = LittleFS.open("/history_idx.txt", "w");
  if (!f) {
    Serial.println("[LITTLEFS] Failed to open /history_idx.txt for writing");
    return;
  }
  for (const auto& item : sessionHistory) {
    f.printf("%s\t%s\t%s\t%s\t%s\t%d\n",
             sanitizeField(item.id).c_str(),
             sanitizeField(item.title).c_str(),
             sanitizeField(item.type).c_str(),
             sanitizeField(item.dateStr).c_str(),
             sanitizeField(item.saveTime).c_str(),
             item.attendeeCount);
  }
  f.close();
}

String saveCurrentSessionToHistory() {
  String id = "sess_" + String((unsigned long)time(nullptr)) + "_" + String(millis() % 1000);
  String dStr = isTimeSet ? getCurrentTimeString().substring(0, 10) : "Undated";
  String sTime = getCurrentTime12h();

  SavedSessionItem item;
  item.id = id;
  item.title = sessionTitle.length() > 0 ? sessionTitle : "General Session";
  item.type = sessionType;
  item.dateStr = dStr;
  item.saveTime = sTime;
  item.attendeeCount = attendanceRecords.size();

  // Save session records to individual file
  String path = "/" + id + ".txt";
  File f = LittleFS.open(path.c_str(), "w");
  if (f) {
    for (const auto& r : attendanceRecords) {
      String status = "Checked In";
      if (r.inTime.length() > 0 && r.inTime != "--" && r.outTime.length() > 0 && r.outTime != "--") status = "Completed";
      else if (r.outTime.length() > 0 && r.outTime != "--") status = "Checked Out";

      f.printf("%s\t%s\t%s\t%s\t%s\t%s\n",
               sanitizeField(r.name).c_str(),
               sanitizeField(r.studentId).c_str(),
               sanitizeField(r.uid).c_str(),
               sanitizeField(r.inTime).c_str(),
               sanitizeField(r.outTime).c_str(),
               sanitizeField(status).c_str());
    }
    f.close();
  }

  // Insert newest at top
  sessionHistory.insert(sessionHistory.begin(), item);

  // Enforce strict limit of 5 saved sessions: overwrite/delete oldest from flash
  while (sessionHistory.size() > 5) {
    SavedSessionItem oldest = sessionHistory.back();
    String oldPath = "/" + oldest.id + ".txt";
    if (LittleFS.exists(oldPath.c_str())) {
      LittleFS.remove(oldPath.c_str());
    }
    sessionHistory.pop_back();
    Serial.printf("[LITTLEFS] Session history limit (5) reached. Overwrote oldest session '%s' (ID: %s)\n",
                  oldest.title.c_str(), oldest.id.c_str());
  }

  saveHistoryIndex();
  Serial.printf("[LITTLEFS] Saved session '%s' with %d attendees (ID: %s)\n", item.title.c_str(), item.attendeeCount, id.c_str());
  return id;
}

void loadRegisteredCards() {
  registeredCards.clear();

  // 1. Primary: Load from LittleFS file (/cards.txt)
  if (LittleFS.exists("/cards.txt")) {
    File f = LittleFS.open("/cards.txt", "r");
    if (f) {
      while (f.available()) {
        String entry = f.readStringUntil('\n');
        entry.trim();
        if (entry.length() == 0) continue;
        int t1 = entry.indexOf('\t');
        int t2 = entry.indexOf('\t', t1 + 1);
        int t3 = entry.indexOf('\t', t2 + 1);
        if (t1 != -1) {
          RegisteredCard c;
          c.uid = entry.substring(0, t1);
          if (t2 != -1) {
            c.name = entry.substring(t1 + 1, t2);
            if (t3 != -1) {
              c.studentId = entry.substring(t2 + 1, t3);
              c.regDate = entry.substring(t3 + 1);
            } else {
              c.studentId = entry.substring(t2 + 1);
              c.regDate = "";
            }
          } else {
            c.name = entry.substring(t1 + 1);
          }
          registeredCards.push_back(c);
        }
      }
      f.close();
      Serial.printf("[LITTLEFS] Loaded %d registered cards from /cards.txt\n", registeredCards.size());
      return;
    }
  }

  // 2. Fallback / Migration: Load from NVS Preferences if /cards.txt does not exist yet
  prefs.begin("rfid_att", true);
  int count = prefs.getInt("count", 0);
  for (int i = 0; i < count; i++) {
    String key = "c" + String(i);
    String entry = prefs.getString(key.c_str(), "");
    if (entry.length() > 0) {
      int t1 = entry.indexOf('\t');
      int t2 = entry.indexOf('\t', t1 + 1);
      int t3 = entry.indexOf('\t', t2 + 1);
      if (t1 != -1) {
        RegisteredCard c;
        c.uid = entry.substring(0, t1);
        if (t2 != -1) {
          c.name = entry.substring(t1 + 1, t2);
          if (t3 != -1) {
            c.studentId = entry.substring(t2 + 1, t3);
            c.regDate = entry.substring(t3 + 1);
          } else {
            c.studentId = entry.substring(t2 + 1);
            c.regDate = "";
          }
        } else {
          c.name = entry.substring(t1 + 1);
        }
        registeredCards.push_back(c);
      }
    }
  }
  prefs.end();
  Serial.printf("[NVS] Loaded %d registered cards from flash.\n", registeredCards.size());

  // Automatically migrate existing NVS cards into LittleFS
  if (registeredCards.size() > 0) {
    saveRegisteredCards();
  }
}

void saveRegisteredCards() {
  // Save to LittleFS /cards.txt (supports thousands of cards!)
  File f = LittleFS.open("/cards.txt", "w");
  if (f) {
    for (size_t i = 0; i < registeredCards.size(); i++) {
      f.printf("%s\t%s\t%s\t%s\n",
               sanitizeField(registeredCards[i].uid).c_str(),
               sanitizeField(registeredCards[i].name).c_str(),
               sanitizeField(registeredCards[i].studentId).c_str(),
               sanitizeField(registeredCards[i].regDate).c_str());
    }
    f.close();
    Serial.printf("[LITTLEFS] Saved %d registered cards to /cards.txt\n", registeredCards.size());
  } else {
    Serial.println("[LITTLEFS ERROR] Failed to write /cards.txt");
  }

  // Also maintain NVS backup (first 100 cards) for backward compatibility
  prefs.begin("rfid_att", false);
  prefs.clear();
  int nvsCount = (registeredCards.size() > 100) ? 100 : registeredCards.size();
  prefs.putInt("count", nvsCount);
  for (int i = 0; i < nvsCount; i++) {
    String key = "c" + String(i);
    String entry = sanitizeField(registeredCards[i].uid) + "\t" + 
                   sanitizeField(registeredCards[i].name) + "\t" + 
                   sanitizeField(registeredCards[i].studentId) + "\t" + 
                   sanitizeField(registeredCards[i].regDate);
    prefs.putString(key.c_str(), entry);
  }
  prefs.end();
}

String lookupOwnerName(const String& uid) {
  for (const auto& card : registeredCards) {
    if (card.uid.equalsIgnoreCase(uid)) {
      return card.name;
    }
  }
  return "Unregistered Tag (" + uid + ")";
}

String lookupOwnerId(const String& uid) {
  for (const auto& card : registeredCards) {
    if (card.uid.equalsIgnoreCase(uid)) {
      return card.studentId;
    }
  }
  return "";
}

bool isCardRegistered(const String& uid) {
  for (const auto& card : registeredCards) {
    if (card.uid.equalsIgnoreCase(uid)) return true;
  }
  return false;
}

// -----------------------------------------------------------------------------
// System Settings & Wi-Fi Configuration Persistence (NVS)
// -----------------------------------------------------------------------------
void loadSystemSettings() {
  prefs.begin("rfid_sys", true);
  requireRegistration = prefs.getBool("req_reg", false);
  lcdStandbyTimeoutMs = prefs.getULong("lcd_timeout", 45000UL);
  showLcdWifiGuide = prefs.getBool("lcd_wifi", true);
  cardDebounceMs = prefs.getULong("debounce", 1500UL);
  lcdActiveBrightness = prefs.getUChar("lcd_bright", 255);
  if (lcdActiveBrightness < 10) lcdActiveBrightness = 255;
  lcdDimDurationMs = prefs.getULong("lcd_dim_ms", 10000UL);
  rgbActiveBrightness = prefs.getUChar("rgb_bright", 168);
  if (rgbActiveBrightness > RGB_PWM_MAX_LIMIT) rgbActiveBrightness = RGB_PWM_MAX_LIMIT;
  currentLcdPwm = lcdActiveBrightness;
  // Apply password saved for next boot as active Wi-Fi password for this boot
  currentApPass = prefs.getString("next_wifi_pass", "12345678");
  nextApPass = currentApPass;
  prefs.end();

  // Keep next_wifi_pass in sync in NVS
  prefs.begin("rfid_sys", false);
  prefs.putString("next_wifi_pass", currentApPass);
  prefs.end();
  Serial.printf("[SETTINGS] NVS Loaded: Wi-Fi Pass: '%s', LCD Guide: %s, LCD Timeout: %lu ms, Brightness: %u, Dim Duration: %lu ms\n",
                currentApPass.c_str(), showLcdWifiGuide ? "ON" : "OFF", lcdStandbyTimeoutMs, lcdActiveBrightness, lcdDimDurationMs);
}

void saveSystemSettings() {
  prefs.begin("rfid_sys", false);
  prefs.putBool("req_reg", requireRegistration);
  prefs.putULong("lcd_timeout", lcdStandbyTimeoutMs);
  prefs.putBool("lcd_wifi", showLcdWifiGuide);
  prefs.putULong("debounce", cardDebounceMs);
  prefs.putString("next_wifi_pass", nextApPass);
  prefs.putUChar("lcd_bright", lcdActiveBrightness);
  prefs.putULong("lcd_dim_ms", lcdDimDurationMs);
  prefs.putUChar("rgb_bright", rgbActiveBrightness);
  prefs.end();
}

// -----------------------------------------------------------------------------
// Web App HTML / CSS / JS Embedded Literal
// -----------------------------------------------------------------------------
const char INDEX_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
<title>RFID Attendance Console</title>
<style>
:root{--md-sys-color-primary:#0b57d0;--md-sys-color-on-primary:#ffffff;--md-sys-color-primary-container:#d3e3fd;--md-sys-color-on-primary-container:#041e49;--md-sys-color-secondary-container:#dbe2f9;--md-sys-color-on-secondary-container:#131c2b;--md-sys-color-surface:#f8fafd;--md-sys-color-surface-container-low:#f0f4f9;--md-sys-color-surface-container:#ffffff;--md-sys-color-surface-container-high:#eaeef4;--md-sys-color-surface-container-highest:#e0e2ec;--md-sys-color-surface-variant:#e1e2ec;--md-sys-color-on-surface:#1f1f1f;--md-sys-color-on-surface-variant:#444746;--md-sys-color-outline:#747775;--md-sys-color-outline-variant:#e0e2ec;--md-sys-color-success:#137333;--md-sys-color-success-container:#c4eed0;--md-sys-color-on-success-container:#072711;--md-sys-color-error:#ba1a1a;--md-sys-color-error-container:#ffdad6;--md-sys-color-on-error-container:#410002;--md-sys-color-warning:#b06000;--md-sys-color-warning-container:#ffdf9e;--md-sys-color-on-warning-container:#261600;--md-elevation-1:0 1px 3px 1px rgba(0,0,0,0.06),0 1px 2px 0 rgba(0,0,0,0.08);--md-elevation-2:0 2px 6px 2px rgba(0,0,0,0.06),0 1px 2px 0 rgba(0,0,0,0.08);--md-elevation-dialog:0 14px 36px 4px rgba(0,0,0,0.2);--md-shape-corner-small:8px;--md-shape-corner-medium:12px;--md-shape-corner-large:18px;--md-shape-corner-full:9999px;--md-shape-dialog:24px;color-scheme:light;}
[data-theme="dark"]{--md-sys-color-primary:#8ab4f8;--md-sys-color-on-primary:#002b75;--md-sys-color-primary-container:#17387a;--md-sys-color-on-primary-container:#d2e3fc;--md-sys-color-secondary-container:#283141;--md-sys-color-on-secondary-container:#dbe2f9;--md-sys-color-surface:#0e1013;--md-sys-color-surface-container-low:#14171d;--md-sys-color-surface-container:#1a1d24;--md-sys-color-surface-container-high:#232731;--md-sys-color-surface-container-highest:#333846;--md-sys-color-surface-variant:#262a34;--md-sys-color-on-surface:#f1f3f7;--md-sys-color-on-surface-variant:#9aa0a6;--md-sys-color-outline:#5f6368;--md-sys-color-outline-variant:#282c35;--md-sys-color-success:#34d399;--md-sys-color-success-container:#064e3b;--md-sys-color-on-success-container:#a7f3d0;--md-sys-color-error:#f87171;--md-sys-color-error-container:#7f1d1d;--md-sys-color-on-error-container:#fecaca;--md-sys-color-warning:#fbbf24;--md-sys-color-warning-container:#78350f;--md-sys-color-on-warning-container:#fef3c7;--md-elevation-1:0 1px 3px rgba(0,0,0,0.4);--md-elevation-2:0 2px 6px rgba(0,0,0,0.5);--md-elevation-dialog:0 16px 40px rgba(0,0,0,0.85);color-scheme:dark;}
*{box-sizing:border-box;margin:0;padding:0;font-family:Roboto,system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",Arial,sans-serif;-webkit-font-smoothing:antialiased;}
html,body{width:100%;height:100%;overflow:hidden;background:var(--md-sys-color-surface);color:var(--md-sys-color-on-surface);transition:background-color 0.2s,color 0.2s;}
svg,.icon,.icon-sm,.icon-lg,.icon-xl{fill:currentColor;display:inline-block;vertical-align:middle;flex-shrink:0;}
.icon{width:20px;height:20px;}
.icon-sm{width:16px;height:16px;}
.icon-lg{width:26px;height:26px;}
.icon-xl{width:44px;height:44px;}
input,select,textarea,button{font-family:inherit;color:inherit;}
input::placeholder,textarea::placeholder{color:var(--md-sys-color-outline);opacity:0.85;}
select,.form-input{background:var(--md-sys-color-surface-container);color:var(--md-sys-color-on-surface);border:1px solid var(--md-sys-color-outline-variant);border-radius:var(--md-shape-corner-small);padding:8px 12px;font-size:0.85rem;outline:none;transition:border-color 0.15s;}
select:focus,.form-input:focus{border-color:var(--md-sys-color-primary);}
select option{background:var(--md-sys-color-surface-container);color:var(--md-sys-color-on-surface);}
input[type="range"]{accent-color:var(--md-sys-color-primary);}
input[type="datetime-local"]{color-scheme:light dark;}
[data-theme="dark"] input[type="datetime-local"]{color-scheme:dark;}
.btn{display:inline-flex;align-items:center;justify-content:center;gap:8px;padding:10px 18px;border-radius:var(--md-shape-corner-full);font-size:0.9rem;font-weight:500;cursor:pointer;border:1px solid transparent;transition:all 0.15s cubic-bezier(0.2,0,0,1);text-decoration:none;white-space:nowrap;min-height:44px;}
.btn-primary{background:var(--md-sys-color-primary);color:var(--md-sys-color-on-primary);}
.btn-primary:hover{filter:brightness(0.92);}
.btn-tonal{background:var(--md-sys-color-primary-container);color:var(--md-sys-color-on-primary-container);}
.btn-tonal:hover{filter:brightness(0.95);}
.btn-outlined{background:transparent;border-color:var(--md-sys-color-outline);color:var(--md-sys-color-on-surface);}
.btn-outlined:hover{background:var(--md-sys-color-surface-container-high);}
[data-theme="dark"] .btn-outlined{border-color:var(--md-sys-color-outline);color:var(--md-sys-color-on-surface);}
.btn-danger-tonal{background:var(--md-sys-color-error-container);color:var(--md-sys-color-on-error-container);}
.btn-sm{padding:5px 12px;font-size:0.8rem;min-height:34px;}
.icon-btn{width:36px;height:36px;border-radius:var(--md-shape-corner-full);border:none;background:transparent;color:var(--md-sys-color-on-surface-variant);display:inline-flex;align-items:center;justify-content:center;cursor:pointer;transition:background-color 0.15s,color 0.15s;}
.icon-btn:hover{background:var(--md-sys-color-surface-container-high);color:var(--md-sys-color-on-surface);}
.text-field{margin-bottom:14px;text-align:left;}
.text-field label{display:block;font-size:0.8125rem;font-weight:500;color:var(--md-sys-color-on-surface-variant);margin-bottom:6px;}
.text-field input{width:100%;padding:12px 14px;background:var(--md-sys-color-surface-container);border:1px solid var(--md-sys-color-outline-variant);border-radius:var(--md-shape-corner-small);color:var(--md-sys-color-on-surface);font-size:0.95rem;outline:none;}
.text-field input:focus{border-color:var(--md-sys-color-primary);}
.entrance-viewport{width:100%;height:100%;display:flex;align-items:center;justify-content:center;padding:16px;background:var(--md-sys-color-surface);}
.entrance-card{background:var(--md-sys-color-surface-container);border:1px solid var(--md-sys-color-outline-variant);border-radius:var(--md-shape-dialog);max-width:440px;width:100%;padding:28px 24px;box-shadow:var(--md-elevation-dialog);text-align:center;}
.entrance-step{display:none;}
.entrance-step.active{display:block;}
.entrance-logo-circle{width:56px;height:56px;border-radius:16px;background:var(--md-sys-color-primary);color:var(--md-sys-color-on-primary);display:inline-flex;align-items:center;justify-content:center;margin-bottom:14px;box-shadow:var(--md-elevation-1);}
.entrance-title{font-size:1.35rem;font-weight:700;color:var(--md-sys-color-on-surface);margin-bottom:6px;}
.entrance-sub{font-size:0.875rem;color:var(--md-sys-color-on-surface-variant);margin-bottom:20px;}
.direction-grid{display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-bottom:20px;}
.direction-card{border:2px solid var(--md-sys-color-outline-variant);border-radius:var(--md-shape-corner-medium);padding:16px 12px;text-align:center;cursor:pointer;background:var(--md-sys-color-surface-container-low);color:var(--md-sys-color-on-surface);transition:all 0.15s;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:6px;min-height:100px;}
.direction-card:hover{border-color:var(--md-sys-color-primary);background:var(--md-sys-color-surface-container-high);}
.direction-card.selected{border-color:var(--md-sys-color-primary);background:var(--md-sys-color-primary-container);color:var(--md-sys-color-on-primary-container);}
.direction-card-title{font-size:0.95rem;font-weight:700;}
.direction-card-desc{font-size:0.72rem;opacity:0.85;}
.console-viewport{width:100%;height:100%;display:none;flex-direction:column;}
.console-app-bar{height:52px;flex-shrink:0;display:flex;align-items:center;justify-content:space-between;padding:0 12px;background:var(--md-sys-color-surface-container);border-bottom:1px solid var(--md-sys-color-outline-variant);gap:8px;z-index:10;}
.console-session-badge{display:flex;align-items:center;gap:8px;font-size:0.85rem;font-weight:600;min-width:0;}
.pulse-indicator{width:9px;height:9px;border-radius:50%;background:var(--md-sys-color-success);box-shadow:0 0 0 3px rgba(52,211,153,0.25);flex-shrink:0;}
.console-actions{display:flex;align-items:center;gap:6px;flex-shrink:0;}
.btn-lifecycle-text-full{display:inline;}
.btn-lifecycle-text-short{display:none;}
.hide-mobile{display:inline;}
.console-workspace{flex:1;display:flex;overflow:hidden;}
@media (min-width:768px){.console-workspace{flex-direction:row;}
.rfid-panel{flex:6;height:100%;padding:16px;border-right:1px solid var(--md-sys-color-outline-variant);background:var(--md-sys-color-surface);display:flex;flex-direction:column;overflow:hidden;}
.log-panel{flex:4;height:100%;display:flex;flex-direction:column;padding:14px 16px;background:var(--md-sys-color-surface-container-low);overflow:hidden;}
}
@media (max-width:767px){.console-app-bar{height:48px;padding:0 10px;gap:6px;display:flex;align-items:center;justify-content:space-between;overflow:hidden;}
.console-session-badge{gap:6px;flex:0 1 auto;min-width:0;overflow:hidden;display:flex;align-items:center;}
#activeTitleLabel{max-width:100px !important;font-size:0.82rem;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;}
#activeModeChip{display:none !important;}
.console-actions{gap:5px;flex:0 0 auto;margin-left:auto;display:flex;align-items:center;}
#headerClock{display:none !important;}
.btn-lifecycle-text-full{display:none !important;}
.btn-lifecycle-text-short{display:inline !important;}
.hide-mobile{display:none !important;}
.btn-history-header{padding:5px 8px !important;}
.icon-btn{width:32px !important;height:32px !important;padding:4px !important;flex-shrink:0;}
.console-workspace{flex-direction:column;}
.rfid-panel{flex:1;min-height:0;max-height:48%;padding:8px;border-bottom:2px solid var(--md-sys-color-outline-variant);background:var(--md-sys-color-surface);display:flex;flex-direction:column;overflow:hidden;}
.log-panel{flex:1;min-height:0;max-height:52%;display:flex;flex-direction:column;padding:8px;background:var(--md-sys-color-surface-container-low);overflow:hidden;}
.rfid-panel-fullcard{padding:8px !important;}
.rfid-mode-segment-bar{top:8px !important;}
.rfid-segment-btn{padding:6px 16px !important;font-size:0.82rem !important;}
.rfid-idle-container{padding-top:36px !important;}
.tap-sensor-graphic{width:72px !important;height:72px !important;margin-bottom:6px !important;}
.sensor-icon-center{width:46px !important;height:46px !important;}
.sensor-icon-center svg{width:24px !important;height:24px !important;}
.idle-mode-pill{padding:2px 8px !important;font-size:0.68rem !important;margin-bottom:4px !important;}
.idle-headline{font-size:1.15rem !important;margin-bottom:2px !important;}
.idle-subline{font-size:0.75rem !important;}
.result-attendee-name{font-size:1.35rem !important;}
.result-sub-message{font-size:0.82rem !important;}
.result-timeout-bar{margin-top:10px !important;width:140px !important;}
.log-header{margin-bottom:6px !important;}
.log-title{font-size:0.88rem !important;}
.search-box{margin-bottom:6px !important;}
.search-box input{padding:6px 10px 6px 28px !important;font-size:0.78rem !important;}
table.console-table th,table.console-table td{padding:6px 8px !important;font-size:0.75rem !important;}
}
.rfid-panel-fullcard{width:100%;height:100%;border-radius:var(--md-shape-corner-large);border:2px solid var(--md-sys-color-outline-variant);box-shadow:var(--md-elevation-1);position:relative;overflow:hidden;display:flex;flex-direction:column;align-items:center;justify-content:center;text-align:center;padding:24px;transition:all 0.3s cubic-bezier(0.2,0,0,1);}
.rfid-mode-segment-bar{position:absolute;top:16px;left:0;right:0;display:flex;justify-content:center;align-items:center;z-index:15;}
.rfid-segmented-control{display:inline-flex;align-items:center;background:var(--md-sys-color-surface-container-highest);border:1px solid var(--md-sys-color-outline-variant);border-radius:9999px;padding:4px;gap:4px;box-shadow:var(--md-elevation-1);}
.rfid-segment-btn{display:inline-flex;align-items:center;gap:8px;padding:8px 24px;border-radius:9999px;border:none;background:transparent;color:var(--md-sys-color-on-surface-variant);font-family:inherit;font-size:0.92rem;font-weight:600;cursor:pointer;transition:all 0.25s cubic-bezier(0.2,0,0,1);user-select:none;}
.rfid-segment-btn svg{width:18px;height:18px;fill:currentColor;}
.rfid-segment-btn:hover{background:var(--md-sys-color-surface-container-high);color:var(--md-sys-color-on-surface);}
.rfid-segment-btn.active-in{background:var(--md-sys-color-primary) !important;color:var(--md-sys-color-on-primary) !important;box-shadow:0 3px 8px rgba(11,87,208,0.35);font-weight:700;}
.rfid-segment-btn.active-out{background:#ea580c !important;color:#ffffff !important;box-shadow:0 3px 8px rgba(234,88,12,0.35);font-weight:700;}
.rfid-idle-container{width:100%;height:100%;display:flex;flex-direction:column;align-items:center;justify-content:center;padding-top:52px;}
.tap-sensor-graphic{position:relative;width:130px;height:130px;display:flex;align-items:center;justify-content:center;margin-bottom:20px;}
.pulse-ring{position:absolute;inset:0;border-radius:50%;border:2px solid var(--md-sys-color-primary);opacity:0;animation:tapRadarPulse 2.4s infinite cubic-bezier(0.2,0.8,0.4,1);}
.ring-1{animation-delay:0s;}
.ring-2{animation-delay:0.8s;}
.ring-3{animation-delay:1.6s;}
@keyframes tapRadarPulse{0%{transform:scale(0.6);opacity:0.85;}
60%{transform:scale(1.35);opacity:0.15;}
100%{transform:scale(1.6);opacity:0;}
}
.sensor-icon-center{width:74px;height:74px;border-radius:50%;background:var(--md-sys-color-primary-container);color:var(--md-sys-color-primary);display:flex;align-items:center;justify-content:center;z-index:2;box-shadow:0 4px 18px rgba(11,87,208,0.2);animation:gentleFloat 3s ease-in-out infinite alternate;}
@keyframes gentleFloat{0%{transform:translateY(0px) scale(1);}
100%{transform:translateY(-6px) scale(1.03);}
}
.idle-mode-pill{padding:6px 16px;border-radius:var(--md-shape-corner-full);font-size:0.8125rem;font-weight:700;letter-spacing:0.5px;text-transform:uppercase;margin-bottom:12px;background:var(--md-sys-color-surface-container-high);color:var(--md-sys-color-on-surface-variant);border:1px solid var(--md-sys-color-outline-variant);}
.idle-headline{font-size:clamp(1.8rem,3.2vw,2.7rem);font-weight:800;letter-spacing:-0.5px;margin-bottom:8px;color:var(--md-sys-color-on-surface);line-height:1.2;}
.idle-subline{font-size:1.05rem;color:var(--md-sys-color-on-surface-variant);font-weight:500;max-width:420px;}
.rfid-result-container{width:100%;height:100%;display:none;flex-direction:column;align-items:center;justify-content:center;animation:resultPopIn 0.28s cubic-bezier(0.175,0.885,0.32,1.275);}
@keyframes resultPopIn{0%{transform:scale(0.96);opacity:0;}
100%{transform:scale(1);opacity:1;}
}
.result-badge{display:inline-flex;align-items:center;gap:6px;padding:6px 18px;border-radius:var(--md-shape-corner-full);font-size:0.875rem;font-weight:800;letter-spacing:0.6px;text-transform:uppercase;margin-bottom:14px;}
.result-attendee-name{font-size:clamp(2rem,4.2vw,3.4rem);font-weight:800;line-height:1.18;margin-bottom:10px;letter-spacing:-0.5px;word-break:break-word;}
.result-sub-message{font-size:1.15rem;font-weight:500;opacity:0.92;margin-bottom:6px;}
.result-meta-tag{font-size:0.9rem;font-family:ui-monospace,SFMono-Regular,Consolas,monospace;opacity:0.85;margin-top:6px;}
.result-timeout-bar{width:180px;height:4px;background:rgba(0,0,0,0.12);border-radius:2px;overflow:hidden;margin-top:24px;}
.result-timeout-fill{height:100%;width:100%;background:currentColor;animation:shrinkTimeout 1.5s linear forwards;}
@keyframes shrinkTimeout{from{width:100%;}
to{width:0%;}
}
.rfid-panel-fullcard.theme-idle{background:var(--md-sys-color-surface-container);border-color:var(--md-sys-color-outline-variant);}
.rfid-panel-fullcard.theme-success{background:var(--md-sys-color-success-container);border-color:var(--md-sys-color-success);color:var(--md-sys-color-on-success-container);}
.rfid-panel-fullcard.theme-duplicate{background:var(--md-sys-color-warning-container);border-color:var(--md-sys-color-warning);color:var(--md-sys-color-on-warning-container);}
.rfid-panel-fullcard.theme-error{background:var(--md-sys-color-error-container);border-color:var(--md-sys-color-error);color:var(--md-sys-color-on-error-container);}
@media (max-width:480px){.idle-headline{font-size:1.15rem;}
.idle-subline{font-size:0.75rem;}
.result-attendee-name{font-size:1.35rem;}
.result-sub-message{font-size:0.82rem;}
}
.log-header{display:flex;align-items:center;justify-content:space-between;margin-bottom:8px;gap:6px;flex-wrap:nowrap;}
.log-title-area{display:flex;align-items:center;gap:6px;min-width:0;flex:1;overflow:hidden;}
.log-title{font-size:0.92rem;font-weight:700;white-space:nowrap;}
.log-summary-chip{font-size:0.73rem;color:var(--md-sys-color-on-surface-variant);font-weight:500;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;}
.log-actions{display:flex;align-items:center;gap:4px;flex-shrink:0;flex-wrap:nowrap;}
.log-btn{padding:5px 8px;height:30px;display:inline-flex;align-items:center;justify-content:center;gap:4px;font-size:0.75rem;font-weight:600;white-space:nowrap;border-radius:8px;}
.log-btn .icon-sm{width:15px;height:15px;flex-shrink:0;}
@media (max-width:540px){
  .log-btn .log-btn-text{display:none;}
  .log-btn{padding:0;width:32px;height:32px;border-radius:8px;justify-content:center;}
  .log-btn .icon-sm{width:16px;height:16px;}
  .log-title{font-size:0.86rem;}
}
.search-box{position:relative;margin-bottom:8px;}
.search-box input{width:100%;padding:8px 12px 8px 32px;background:var(--md-sys-color-surface-container);border:1px solid var(--md-sys-color-outline-variant);border-radius:var(--md-shape-corner-full);color:var(--md-sys-color-on-surface);font-size:0.8125rem;outline:none;}
.search-box .icon-sm{position:absolute;left:10px;top:50%;transform:translateY(-50%);color:var(--md-sys-color-on-surface-variant);fill:currentColor;}
.table-wrapper{flex:1;overflow-y:auto;overflow-x:auto;border:1px solid var(--md-sys-color-outline-variant);border-radius:var(--md-shape-corner-medium);background:var(--md-sys-color-surface-container);-webkit-overflow-scrolling:touch;}
table.console-table{width:100%;border-collapse:collapse;text-align:left;font-size:0.8125rem;color:var(--md-sys-color-on-surface);}
table.console-table th{position:sticky;top:0;background:var(--md-sys-color-surface-container-high);padding:8px 10px;color:var(--md-sys-color-on-surface-variant);font-weight:600;font-size:0.72rem;letter-spacing:0.2px;text-transform:uppercase;border-bottom:1px solid var(--md-sys-color-outline-variant);white-space:nowrap;z-index:2;}
table.console-table td{padding:8px 10px;border-bottom:1px solid var(--md-sys-color-outline-variant);white-space:nowrap;color:var(--md-sys-color-on-surface);}
table.console-table tr:hover td{background:var(--md-sys-color-surface-container-high);}
table.console-table tr:last-child td{border-bottom:none;}
.badge{display:inline-flex;align-items:center;padding:2px 6px;border-radius:var(--md-shape-corner-small);font-size:0.68rem;font-weight:600;}
.badge-in{background:var(--md-sys-color-primary-container);color:var(--md-sys-color-on-primary-container);}
.badge-out{background:#f1ddff;color:#3b0d5c;}
[data-theme="dark"] .badge-out{background:#3b174a;color:#e9bbfd;}
.badge-done{background:var(--md-sys-color-success-container);color:var(--md-sys-color-on-success-container);}
.mono-txt{font-family:ui-monospace,SFMono-Regular,Consolas,monospace;}
.modal-overlay{position:fixed;inset:0;background:rgba(0,0,0,0.6);display:none;align-items:center;justify-content:center;z-index:1000;padding:12px;overscroll-behavior:none !important;overscroll-behavior-y:none !important;touch-action:none;}
.modal-overlay.active{display:flex;}
.m3-dialog{background:var(--md-sys-color-surface-container);border-radius:var(--md-shape-dialog);max-width:460px;width:100%;max-height:88vh;overflow-y:auto;padding:20px;box-shadow:var(--md-elevation-dialog);overscroll-behavior-y:contain !important;touch-action:pan-y !important;-webkit-overflow-scrolling:touch !important;}
body.modal-open{position:fixed !important;inset:0 !important;width:100% !important;height:100% !important;overflow:hidden !important;overscroll-behavior:none !important;overscroll-behavior-y:none !important;touch-action:pan-y !important;}
.notion-settings-dialog{background:var(--md-sys-color-surface);border-radius:16px;width:960px;max-width:95vw;height:680px;max-height:90vh;display:flex;box-shadow:var(--md-elevation-dialog);overflow:hidden;position:relative;border:1px solid var(--md-sys-color-outline-variant);overscroll-behavior:none !important;overscroll-behavior-y:none !important;}
.notion-sidebar{width:250px;background:var(--md-sys-color-surface-container-low);border-right:1px solid var(--md-sys-color-outline-variant);display:flex;flex-direction:column;flex-shrink:0;overflow-y:auto;overscroll-behavior:contain !important;overscroll-behavior-y:contain !important;touch-action:pan-y !important;-webkit-overflow-scrolling:touch !important;user-select:none;}
.notion-sidebar-header{padding:14px 16px;display:flex;align-items:center;justify-content:space-between;border-bottom:1px solid var(--md-sys-color-outline-variant);flex-shrink:0;position:sticky;top:0;z-index:5;background:var(--md-sys-color-surface-container-low);}
.notion-sidebar-title{font-size:0.95rem;font-weight:700;color:var(--md-sys-color-on-surface);display:flex;align-items:center;gap:8px;}
.notion-nav-group-label{font-size:0.6875rem;font-weight:700;text-transform:uppercase;letter-spacing:0.8px;color:var(--md-sys-color-on-surface-variant);padding:14px 16px 4px 16px;}
.notion-nav-item{display:flex;align-items:center;gap:10px;padding:8px 14px;margin:2px 8px;border-radius:8px;border:none;background:transparent;color:var(--md-sys-color-on-surface);font-size:0.84rem;font-weight:500;cursor:pointer;text-align:left;transition:all 0.15s ease;position:relative;width:calc(100% - 16px);touch-action:pan-y;-webkit-tap-highlight-color:transparent;}
.notion-nav-item:hover{background:var(--md-sys-color-surface-container-high);}
.notion-nav-item.active{background:var(--md-sys-color-secondary-container);color:var(--md-sys-color-on-secondary-container);font-weight:600;}
.notion-nav-item .nav-badge{margin-left:auto;font-size:0.7rem;padding:2px 6px;border-radius:10px;background:var(--md-sys-color-surface-variant);color:var(--md-sys-color-on-surface-variant);font-weight:600;}
.notion-nav-item.active .nav-badge{background:var(--md-sys-color-primary);color:var(--md-sys-color-on-primary);}
.notion-nav-item .mobile-chevron{display:none;margin-left:auto;color:var(--md-sys-color-on-surface-variant);}
.notion-content-pane{flex:1;display:flex;flex-direction:column;overflow:hidden;background:var(--md-sys-color-surface);min-width:0;}
.notion-content-header{padding:14px 24px;border-bottom:1px solid var(--md-sys-color-outline-variant);display:flex;align-items:center;justify-content:space-between;background:var(--md-sys-color-surface);flex-shrink:0;}
.notion-header-back-btn{display:none;align-items:center;gap:4px;background:none;border:none;color:var(--md-sys-color-primary);font-size:0.875rem;font-weight:600;cursor:pointer;padding:6px 8px;margin-left:-8px;border-radius:6px;-webkit-tap-highlight-color:transparent;}
.notion-content-title-area{display:flex;flex-direction:column;}
.notion-content-title{font-size:1.15rem;font-weight:700;color:var(--md-sys-color-on-surface);}
.notion-content-subtitle{font-size:0.8rem;color:var(--md-sys-color-on-surface-variant);margin-top:2px;}
.notion-scroll-body{flex:1 1 0%;overflow-y:auto;overscroll-behavior:contain !important;overscroll-behavior-y:contain !important;touch-action:pan-y !important;-webkit-overflow-scrolling:touch !important;padding:20px 24px 30px 24px;min-height:0;}
.notion-pane{display:none;flex-direction:column;gap:16px;}
.notion-pane.active{display:flex;}
.notion-card{background:var(--md-sys-color-surface-container-low);border:1px solid var(--md-sys-color-outline-variant);border-radius:12px;padding:16px;margin-bottom:14px;color:var(--md-sys-color-on-surface);}
.notion-card-title{font-size:0.875rem;font-weight:600;color:var(--md-sys-color-on-surface);margin-bottom:4px;display:flex;align-items:center;gap:8px;}
.notion-card-desc{font-size:0.78rem;color:var(--md-sys-color-on-surface-variant);margin-bottom:12px;line-height:1.4;}
.notion-setting-row{display:flex;align-items:center;justify-content:space-between;padding:12px 0;border-bottom:1px solid var(--md-sys-color-outline-variant);gap:16px;-webkit-tap-highlight-color:transparent;}
.notion-setting-row:last-child{border-bottom:none;padding-bottom:0;}
.notion-setting-row.toggle-row{cursor:pointer;user-select:none;}
.notion-setting-row.toggle-row:active{opacity:0.8;}
.notion-row-label{font-size:0.84rem;font-weight:600;color:var(--md-sys-color-on-surface);}
.notion-row-desc{font-size:0.75rem;color:var(--md-sys-color-on-surface-variant);margin-top:2px;}
.cards-table-container{flex:1;overflow-x:auto;overflow-y:auto;border:1px solid var(--md-sys-color-outline-variant);border-radius:12px;background:var(--md-sys-color-surface-container-low);min-height:180px;overscroll-behavior-y:contain;-webkit-overflow-scrolling:touch;}
.cards-mobile-view{display:flex;flex-direction:column;gap:8px;}
.cards-desktop-view{display:block;}
@media (max-width:640px){.cards-mobile-view{display:flex !important;}
.cards-desktop-view{display:none !important;}
}
@media (min-width:641px){.cards-mobile-view{display:none !important;}
.cards-desktop-view{display:block !important;}
}
.mobile-only-btn{display:none;}
.mobile-only-text{display:none;}
@media (max-width:767px){#regModal.modal-overlay{z-index:2500 !important;}
#settingsModal.modal-overlay{padding:0 !important;position:fixed !important;inset:0 !important;width:100% !important;height:100% !important;height:100dvh !important;overflow:hidden !important;overscroll-behavior:none !important;overscroll-behavior-y:none !important;touch-action:pan-y !important;}
.notion-settings-dialog{width:100% !important;height:100% !important;height:100dvh !important;max-width:100% !important;max-height:100% !important;max-height:100dvh !important;border-radius:0 !important;border:none !important;box-shadow:none !important;display:flex !important;flex-direction:column !important;overflow:hidden !important;overscroll-behavior:none !important;overscroll-behavior-y:none !important;}
.notion-sidebar{width:100% !important;height:100% !important;height:100dvh !important;flex:1 1 0% !important;min-height:0 !important;display:flex !important;flex-direction:column !important;overflow-y:auto !important;overscroll-behavior:contain !important;overscroll-behavior-y:contain !important;touch-action:pan-y !important;-webkit-overflow-scrolling:touch !important;background:var(--md-sys-color-surface) !important;}
.notion-sidebar-header{background:var(--md-sys-color-surface) !important;}
.notion-content-pane{display:none !important;width:100% !important;height:100% !important;height:100dvh !important;flex:1 1 0% !important;min-height:0 !important;overflow:hidden !important;}
.notion-settings-dialog.mobile-detail-active .notion-sidebar{display:none !important;}
.notion-settings-dialog.mobile-detail-active .notion-content-pane{display:flex !important;flex-direction:column !important;}
.notion-header-back-btn{display:inline-flex !important;}
.mobile-only-btn{display:inline-flex !important;}
.mobile-only-text{display:block !important;font-size:0.72rem;color:var(--md-sys-color-on-surface-variant);margin-top:1px;}
.notion-nav-item{padding:13px 16px;margin:0;width:100%;border-radius:0;border-bottom:1px solid var(--md-sys-color-outline-variant);font-size:0.92rem;touch-action:pan-y;}
.notion-nav-item .mobile-chevron{display:block;}
.notion-content-header{padding:12px 14px;flex-shrink:0;}
.notion-scroll-body{flex:1 1 0% !important;min-height:0 !important;overflow-y:auto !important;overscroll-behavior:contain !important;overscroll-behavior-y:contain !important;touch-action:pan-y !important;-webkit-overflow-scrolling:touch !important;padding:14px !important;}
}
.m3-linear-progress{width:100%;height:5px;background:var(--md-sys-color-surface-container-high);border-radius:3px;overflow:hidden;margin:16px 0;}
.m3-linear-progress-bar{height:100%;background:var(--md-sys-color-primary);width:100%;transition:width 0.3s linear;}
.switch-label{display:flex;align-items:center;justify-content:space-between;padding:12px 0;cursor:pointer;-webkit-tap-highlight-color:transparent;}
.switch{position:relative;display:inline-block;width:50px;height:30px;flex-shrink:0;cursor:pointer;-webkit-tap-highlight-color:transparent;}
.switch input{opacity:0;width:0;height:0;position:absolute;}
.switch-slider{position:absolute;cursor:pointer;inset:0;background-color:var(--md-sys-color-surface-container-highest);border:1.5px solid var(--md-sys-color-outline-variant);border-radius:30px;transition:background-color 0.25s cubic-bezier(0.4,0,0.2,1),border-color 0.25s cubic-bezier(0.4,0,0.2,1);}
.switch-slider:before{position:absolute;content:"";height:22px;width:22px;left:3px;top:2.5px;background-color:#ffffff;border-radius:50%;box-shadow:0 2px 4px rgba(0,0,0,0.25);transition:transform 0.25s cubic-bezier(0.4,0,0.2,1);}
.switch input:checked + .switch-slider{background-color:var(--md-sys-color-primary);border-color:var(--md-sys-color-primary);}
.switch input:checked + .switch-slider:before{transform:translateX(20px);background-color:#ffffff;}
</style>
</head>
<body>


<div id="entranceScreen" class="entrance-viewport">
  <div class="entrance-card">
    <div class="entrance-logo-circle">
      <svg class="icon-lg" viewBox="0 0 24 24"><path d="M14.5 13.5c0-.83.67-1.5 1.5-1.5s1.5.67 1.5 1.5-.67 1.5-1.5 1.5-1.5-.67-1.5-1.5zm-5 0c0-.83.67-1.5 1.5-1.5s1.5.67 1.5 1.5-.67 1.5-1.5 1.5-1.5-.67-1.5-1.5zm10.5-8.5H4c-1.11 0-1.99.89-1.99 2L2 18c0 1.11.89 2 2 2h16c1.11 0 2-.89 2-2V7c0-1.11-.89-2-2-2zm0 13H4v-6h16v6zm0-9H4V7h16v2.5z"/></svg>
    </div>

    
    <div id="entranceStep1" class="entrance-step active">
      <h2 class="entrance-title">RFID Attendance</h2>
      <p class="entrance-sub">Enter session title to start attendance</p>

      <div class="text-field">
        <label>Session Title</label>
        <input type="text" id="sessionTitleInput" placeholder="e.g. CS101 Lecture, Morning Shift">
      </div>

      <button class="btn btn-primary" style="width: 100%; margin-top: 6px;" onclick="goToEntranceStep(2)">
        Next
      </button>

      <button class="btn btn-tonal" style="width: 100%; margin-top: 10px;" onclick="openHistoryModal()">
        <svg class="icon-sm" viewBox="0 0 24 24"><path d="M13 3a9 9 0 0 0-9 9H1l3.89 3.89.07.14L9 12H6c0-3.87 3.13-7 7-7s7 3.13 7 7-3.13 7-7 7c-1.93 0-3.68-.79-4.94-2.06l-1.42 1.42A8.954 8.954 0 0 0 13 21a9 9 0 0 0 0-18zm-1 5v5l4.28 2.54.72-1.21-3.5-2.08V8H12z"/></svg>
        Session History
      </button>

      <div style="display: flex; justify-content: center; gap: 14px; margin-top: 18px;">
        <button class="icon-btn" onclick="toggleTheme()" title="Toggle Theme">
          <svg class="icon-sm theme-sun" style="display:none;" viewBox="0 0 24 24"><path d="M12 7c-2.76 0-5 2.24-5 5s2.24 5 5 5 5-2.24 5-5-2.24-5-5-5zM2 13h2c.55 0 1-.45 1-1s-.45-1-1-1H2c-.55 0-1 .45-1 1s.45 1 1 1zm18 0h2c.55 0 1-.45 1-1s-.45-1-1-1h-2c-.55 0-1 .45-1 1s.45 1 1 1zM11 2v2c0 .55.45 1 1 1s1-.45 1-1V2c0-.55-.45-1-1-1s-1 .45-1 1zm0 18v2c0 .55.45 1 1 1s1-.45 1-1v-2c0-.55-.45-1-1-1s-1 .45-1 1zM5.99 4.58c-.39-.39-1.03-.39-1.41 0s-.39 1.03 0 1.41l1.06 1.06c.39.39 1.03.39 1.41 0s.39-1.03 0-1.41L5.99 4.58zm12.37 12.37c-.39-.39-1.03-.39-1.41 0s-.39 1.03 0 1.41l1.06 1.06c.39.39 1.03.39 1.41 0s.39-1.03 0-1.41l-1.06-1.06zm1.06-10.96c.39-.39.39-1.03 0-1.41s-1.03-.39-1.41 0l-1.06 1.06c-.39.39-.39 1.03 0 1.41s1.03.39 1.41 0l1.06-1.06zM7.05 18.36c.39-.39.39-1.03 0-1.41s-1.03-.39-1.41 0l-1.06 1.06c-.39.39-.39 1.03 0 1.41s1.03.39 1.41 0l1.06-1.06z"/></svg>
          <svg class="icon-sm theme-moon" viewBox="0 0 24 24"><path d="M12.3 2a10 10 0 0 0-.19 14 9.92 9.92 0 0 0 7.9 3.9 10 10 0 1 1-7.71-17.9z"/></svg>
        </button>
        <button class="icon-btn" onclick="openHistoryModal()" title="Session History">
          <svg class="icon" viewBox="0 0 24 24"><path d="M13 3a9 9 0 0 0-9 9H1l3.89 3.89.07.14L9 12H6c0-3.87 3.13-7 7-7s7 3.13 7 7-3.13 7-7 7c-1.93 0-3.68-.79-4.94-2.06l-1.42 1.42A8.954 8.954 0 0 0 13 21a9 9 0 0 0 0-18zm-1 5v5l4.28 2.54.72-1.21-3.5-2.08V8H12z"/></svg>
        </button>
        <button class="icon-btn" onclick="openSettingsModal()" title="Settings & Tags">
          <svg class="icon" viewBox="0 0 24 24"><path d="M19.14 12.94c.04-.3.06-.61.06-.94 0-.32-.02-.64-.07-.94l2.03-1.58c.18-.14.23-.41.12-.61l-1.92-3.32c-.12-.22-.37-.29-.59-.22l-2.39.96c-.5-.38-1.03-.7-1.62-.94l-.36-2.54c-.04-.24-.24-.41-.48-.41h-3.84c-.24 0-.43.17-.47.41l-.36 2.54c-.59.24-1.13.57-1.62.94l-2.39-.96c-.22-.08-.47 0-.59.22L2.74 8.87c-.12.21-.08.47.12.61l2.03 1.58c-.05.3-.09.63-.09.94s.02.64.07.94l-2.03 1.58c-.18.14-.23.41-.12.61l1.92 3.32c.12.22.37.29.59.22l2.39-.96c.5.38 1.03.7 1.62.94l.36 2.54c.05.24.24.41.48.41h3.84c.24 0 .44-.17.47-.41l.36-2.54c.59-.24 1.13-.56 1.62-.94l2.39.96c.22.08.47 0 .59-.22l1.92-3.32c.12-.22.07-.47-.12-.61l-2.01-1.58zM12 15.6c-1.98 0-3.6-1.62-3.6-3.6s1.62-3.6 3.6-3.6 3.6 1.62 3.6 3.6-1.62 3.6-3.6 3.6z"/></svg>
        </button>
      </div>
    </div>

    
    <div id="entranceStep2" class="entrance-step">
      <div style="display: flex; align-items: center; justify-content: space-between; margin-bottom: 8px;">
        <h2 class="entrance-title" style="margin: 0;">Session Direction</h2>
        <button class="btn btn-outlined btn-sm" onclick="goToEntranceStep(1)">&larr; Back</button>
      </div>
      <p class="entrance-sub">Session: <strong id="step2TitlePreview" style="color: var(--md-sys-color-primary);">General</strong></p>

      <div class="direction-grid">
        <div id="dirCardEntrance" class="direction-card selected" onclick="selectEntranceDirection('Entrance')">
          <svg class="icon-lg" viewBox="0 0 24 24"><path d="M11 7L9.6 8.4l2.6 2.6H2v2h10.2l-2.6 2.6L11 17l5-5-5-5zm9 12h-8v2h8c1.1 0 2-.9 2-2V5c0-1.1-.9-2-2-2h-8v2h8v14z"/></svg>
          <div class="direction-card-title">Entrance (IN)</div>
          <div class="direction-card-desc">Check-in arrival</div>
        </div>
        <div id="dirCardExit" class="direction-card" onclick="selectEntranceDirection('Exit')">
          <svg class="icon-lg" viewBox="0 0 24 24"><path d="M17 7l-1.41 1.41L18.17 11H8v2h10.17l-2.58 2.58L17 17l5-5zM4 5h8V3H4c-1.1 0-2 .9-2 2v14c0 1.1.9 2 2 2h8v-2H4V5z"/></svg>
          <div class="direction-card-title">Exit (OUT)</div>
          <div class="direction-card-desc">Departure check-out</div>
        </div>
      </div>

      <button class="btn btn-primary" style="width: 100%;" onclick="startSession()">
        <svg class="icon" viewBox="0 0 24 24"><path d="M8 5v14l11-7z"/></svg>
        Start Attendance Session
      </button>
    </div>
  </div>
</div>


<div id="consoleScreen" class="console-viewport">

  <header class="console-app-bar">
    <div class="console-session-badge">
      <div class="pulse-indicator"></div>
      <span id="activeTitleLabel" style="white-space: nowrap; overflow: hidden; text-overflow: ellipsis; max-width: 140px;">Session</span>
      <span id="activeModeChip" class="badge badge-in">Entrance</span>
    </div>

    <div class="console-actions">
      <div class="mono-txt" id="headerClock" style="font-size: 0.8rem; padding: 4px 6px; cursor: pointer;" onclick="openTimeModal()" title="Click to sync clock">--:--:--</div>
      
      <button id="btnSessionLifecycle" class="btn btn-tonal btn-sm" onclick="handleCombinedSessionAction()" title="Save Session to History">
        <svg id="lifecycleBtnIcon" class="icon-sm" viewBox="0 0 24 24"><path d="M17 3H5c-1.11 0-2 .9-2 2v14c0 1.1.89 2 2 2h14c1.1 0 2-.9 2-2V7l-4-4zm2 16H5V5h11.17L19 7.83V19zm-7-7c-1.66 0-3 1.34-3 3s1.34 3 3 3 3-1.34 3-3-1.34-3-3-3zM6 6h9v4H6z"/></svg>
        <span id="lifecycleBtnText">
          <span class="btn-lifecycle-text-full">Save Session</span>
          <span class="btn-lifecycle-text-short">Save</span>
        </span>
      </button>
      <button class="btn btn-tonal btn-sm btn-history-header" onclick="openHistoryModal()" title="View Session History">
        <svg class="icon-sm" viewBox="0 0 24 24"><path d="M13 3a9 9 0 0 0-9 9H1l3.89 3.89.07.14L9 12H6c0-3.87 3.13-7 7-7s7 3.13 7 7-3.13 7-7 7c-1.93 0-3.68-.79-4.94-2.06l-1.42 1.42A8.954 8.954 0 0 0 13 21a9 9 0 0 0 0-18zm-1 5v5l4.28 2.54.72-1.21-3.5-2.08V8H12z"/></svg>
        <span class="hide-mobile">History</span>
      </button>
      <button class="icon-btn" onclick="toggleTheme()" title="Toggle Theme">
        <svg class="icon-sm theme-sun" style="display:none;" viewBox="0 0 24 24"><path d="M12 7c-2.76 0-5 2.24-5 5s2.24 5 5 5 5-2.24 5-5-2.24-5-5-5zM2 13h2c.55 0 1-.45 1-1s-.45-1-1-1H2c-.55 0-1 .45-1 1s.45 1 1 1zm18 0h2c.55 0 1-.45 1-1s-.45-1-1-1h-2c-.55 0-1 .45-1 1s.45 1 1 1zM11 2v2c0 .55.45 1 1 1s1-.45 1-1V2c0-.55-.45-1-1-1s-1 .45-1 1zm0 18v2c0 .55.45 1 1 1s1-.45 1-1v-2c0-.55-.45-1-1-1s-1 .45-1 1zM5.99 4.58c-.39-.39-1.03-.39-1.41 0s-.39 1.03 0 1.41l1.06 1.06c.39.39 1.03.39 1.41 0s.39-1.03 0-1.41L5.99 4.58zm12.37 12.37c-.39-.39-1.03-.39-1.41 0s-.39 1.03 0 1.41l1.06 1.06c.39.39 1.03.39 1.41 0s.39-1.03 0-1.41l-1.06-1.06zm1.06-10.96c.39-.39.39-1.03 0-1.41s-1.03-.39-1.41 0l-1.06 1.06c-.39.39-.39 1.03 0 1.41s1.03.39 1.41 0l1.06-1.06zM7.05 18.36c.39-.39.39-1.03 0-1.41s-1.03-.39-1.41 0l-1.06 1.06c-.39.39-.39 1.03 0 1.41s1.03.39 1.41 0l1.06-1.06z"/></svg>
        <svg class="icon-sm theme-moon" viewBox="0 0 24 24"><path d="M12.3 2a10 10 0 0 0-.19 14 9.92 9.92 0 0 0 7.9 3.9 10 10 0 1 1-7.71-17.9z"/></svg>
      </button>
      <button class="icon-btn" onclick="openSettingsModal()" title="Settings & Tags">
        <svg class="icon" viewBox="0 0 24 24"><path d="M19.14 12.94c.04-.3.06-.61.06-.94 0-.32-.02-.64-.07-.94l2.03-1.58c.18-.14.23-.41.12-.61l-1.92-3.32c-.12-.22-.37-.29-.59-.22l-2.39.96c-.5-.38-1.03-.7-1.62-.94l-.36-2.54c-.04-.24-.24-.41-.48-.41h-3.84c-.24 0-.43.17-.47.41l-.36 2.54c-.59.24-1.13.57-1.62.94l-2.39-.96c-.22-.08-.47 0-.59.22L2.74 8.87c-.12.21-.08.47.12.61l2.03 1.58c-.05.3-.09.63-.09.94s.02.64.07.94l-2.03 1.58c-.18.14-.23.41-.12.61l1.92 3.32c.12.22.37.29.59.22l2.39-.96c.5.38 1.03.7 1.62.94l.36 2.54c.05.24.24.41.48.41h3.84c.24 0 .44-.17.47-.41l.36-2.54c.59-.24 1.13-.56 1.62-.94l2.39.96c.22.08.47 0 .59-.22l1.92-3.32c.12-.22.07-.47-.12-.61l-2.01-1.58zM12 15.6c-1.98 0-3.6-1.62-3.6-3.6s1.62-3.6 3.6-3.6 3.6 1.62 3.6 3.6-1.62 3.6-3.6 3.6z"/></svg>
      </button>
    </div>
  </header>

  
  <div class="console-workspace">

    
    <div class="rfid-panel">
      <div id="bigRfidFullCard" class="rfid-panel-fullcard theme-idle">

        
        <div class="rfid-mode-segment-bar">
          <div class="rfid-segmented-control" role="group" aria-label="Session Mode">
            <button id="rfidModeEntrance" type="button" class="rfid-segment-btn active-in" onclick="setSessionMode('Entrance')" title="Record Time In">
              <svg class="icon-sm" viewBox="0 0 24 24"><path d="M19 19H5V5h7V3H5c-1.11 0-2 .9-2 2v14c0 1.1.89 2 2 2h14c1.1 0 2-.9 2-2v-7h-2v7zM14 3v2h3.59l-9.83 9.83 1.41 1.41L19 6.41V10h2V3h-7z"/></svg>
              <span>Entrance</span>
            </button>
            <button id="rfidModeExit" type="button" class="rfid-segment-btn" onclick="setSessionMode('Exit')" title="Record Time Out">
              <svg class="icon-sm" viewBox="0 0 24 24"><path d="M10.09 15.59L11.5 17l5-5-5-5-1.41 1.41L12.67 11H3v2h9.67l-2.58 2.59zM19 3H5c-1.11 0-2 .9-2 2v4h2V5h14v14H5v-4H3v4c0 1.1.89 2 2 2h14c1.1 0 2-.9 2-2V5c0-1.1-.9-2-2-2z"/></svg>
              <span>Exit</span>
            </button>
          </div>
        </div>

        
        <div id="rfidIdleView" class="rfid-idle-container">
          <div class="tap-sensor-graphic">
            <div class="pulse-ring ring-1"></div>
            <div class="pulse-ring ring-2"></div>
            <div class="pulse-ring ring-3"></div>
            <div class="sensor-icon-center">
              <svg class="icon-xl" viewBox="0 0 24 24"><path d="M20 4H4c-1.11 0-1.99.89-1.99 2L2 18c0 1.11.89 2 2 2h16c1.11 0 2-.89 2-2V6c0-1.11-.89-2-2-2zm0 14H4v-6h16v6zm0-10H4V6h16v2z"/></svg>
            </div>
          </div>
          <div class="idle-mode-pill" id="idleModePill">● ENTRANCE MODE (IN)</div>
          <div class="idle-headline">TAP YOUR RFID CARD</div>
        </div>

        
        <div id="rfidResultView" class="rfid-result-container">
          <div id="resultStatusBadge" class="result-badge" style="background: var(--md-sys-color-surface-container-high); color: var(--md-sys-color-on-surface-variant);">
            RECORDED
          </div>
          <div id="resultAttendeeName" class="result-attendee-name">Attendee Name</div>
          <div id="resultSubMessage" class="result-sub-message">Recorded at 00:00:00</div>
          <div id="resultMetaTag" class="result-meta-tag">UID: -- -- -- --</div>
          <div class="result-timeout-bar">
            <div id="resultTimeoutFill" class="result-timeout-fill"></div>
          </div>
        </div>

      </div>
    </div>

    
    <div class="log-panel">
      <div class="log-header">
        <div class="log-title-area">
          <span class="log-title">Attendance Log</span>
          <span class="log-summary-chip">&bull; <span id="statSummaryText">0 logged</span></span>
        </div>
        <div class="log-actions">
          <button class="btn btn-tonal btn-sm log-btn" onclick="openHistoryModal()" title="Session History">
            <svg class="icon-sm" viewBox="0 0 24 24"><path d="M13 3a9 9 0 0 0-9 9H1l3.89 3.89.07.14L9 12H6c0-3.87 3.13-7 7-7s7 3.13 7 7-3.13 7-7 7c-1.93 0-3.68-.79-4.94-2.06l-1.42 1.42A8.954 8.954 0 0 0 13 21a9 9 0 0 0 0-18zm-1 5v5l4.28 2.54.72-1.21-3.5-2.08V8H12z"/></svg>
            <span class="log-btn-text">History</span>
          </button>
          <button id="copyTableBtn" class="btn btn-tonal btn-sm log-btn" onclick="copySessionTable()" title="Copy table for Google Sheets / Excel">
            <svg class="icon-sm" viewBox="0 0 24 24"><path d="M16 1H4c-1.1 0-2 .9-2 2v14h2V3h12V1zm3 4H8c-1.1 0-2 .9-2 2v14c0 1.1.9 2 2 2h11c1.1 0 2-.9 2-2V7c0-1.1-.9-2-2-2zm0 16H8V7h11v14z"/></svg>
            <span class="log-btn-text">Copy</span>
          </button>
          <a id="btnExportLiveCsv" href="/api/attendance/export.csv" class="btn btn-tonal btn-sm log-btn" download="attendance.csv" title="Export CSV">
            <svg class="icon-sm" viewBox="0 0 24 24"><path d="M19 9h-4V3H9v6H5l7 7 7-7zM5 18v2h14v-2H5z"/></svg>
            <span class="log-btn-text">CSV</span>
          </a>
          <button class="btn btn-outlined btn-sm log-btn" onclick="clearAttendanceLog()" title="Reset Attendance Log">
            <svg class="icon-sm" viewBox="0 0 24 24"><path d="M17.65 6.35A7.958 7.958 0 0 0 12 4c-4.42 0-7.99 3.58-7.99 8s3.57 8 7.99 8c3.73 0 6.84-2.55 7.73-6h-2.08A5.99 5.99 0 0 1 12 18c-3.31 0-6-2.69-6-6s2.69-6 6-6c1.66 0 3.14.69 4.22 1.78L13 11h7V4l-2.35 2.35z"/></svg>
            <span class="log-btn-text">Reset</span>
          </button>
        </div>
      </div>

      <div class="search-box">
        <svg class="icon-sm" viewBox="0 0 24 24"><path d="M15.5 14h-.79l-.28-.27A6.471 6.471 0 0 0 16 9.5 6.5 6.5 0 1 0 9.5 16c1.61 0 3.09-.59 4.23-1.57l.27.28v.79l5 4.99L20.49 19l-4.99-5zm-6 0C7.01 14 5 11.99 5 9.5S7.01 5 9.5 5 14 7.01 14 9.5 11.99 14 9.5 14z"/></svg>
        <input type="text" id="attendanceSearch" placeholder="Search attendee name or UID..." oninput="filterAttendanceTable()">
      </div>

      <div class="table-wrapper">
        <table class="console-table" id="attendanceTable">
          <thead>
            <tr>
              <th style="width: 30px;">#</th>
              <th>Attendee Name</th>
              <th>Time IN</th>
              <th>Time OUT</th>
              <th>Status</th>
            </tr>
          </thead>
          <tbody id="attendanceTableBody">
            <tr>
              <td colspan="5" style="text-align: center; color: var(--md-sys-color-on-surface-variant); padding: 24px;">
                No attendance records yet.
              </td>
            </tr>
          </tbody>
        </table>
      </div>
    </div>

  </div>
</div>


<div id="settingsModal" class="modal-overlay" onclick="handleOverlayClick(event, 'settingsModal')">
  <div class="notion-settings-dialog" id="notionSettingsDialog">

    
    <div class="notion-sidebar" id="notionSidebar">
      <div class="notion-sidebar-header">
        <div class="notion-sidebar-title">
          <svg class="icon-sm" viewBox="0 0 24 24"><path d="M19.14 12.94c.04-.3.06-.61.06-.94 0-.32-.02-.64-.07-.94l2.03-1.58c.18-.14.23-.41.12-.61l-1.92-3.32c-.12-.22-.37-.29-.59-.22l-2.39.96c-.5-.38-1.03-.7-1.62-.94l-.36-2.54c-.04-.24-.24-.41-.48-.41h-3.84c-.24 0-.43.17-.47.41l-.36 2.54c-.59.24-1.13.57-1.62.94l-2.39-.96c-.22-.08-.47 0-.59.22L2.74 8.87c-.12.21-.08.47.12.61l2.03 1.58c-.05.3-.09.63-.09.94s.02.64.07.94l-2.03 1.58c-.18.14-.23.41-.12.61l1.92 3.32c.12.22.37.29.59.22l2.39-.96c.5.38 1.03.7 1.62.94l.36 2.54c.05.24.24.41.48.41h3.84c.24 0 .44-.17.47-.41l.36-2.54c.59-.24 1.13-.56 1.62-.94l2.39.96c.22.08.47 0 .59-.22l1.92-3.32c.12-.22.07-.47-.12-.61l-2.01-1.58zM12 15.6c-1.98 0-3.6-1.62-3.6-3.6s1.62-3.6 3.6-3.6 3.6 1.62 3.6 3.6-1.62 3.6-3.6 3.6z"/></svg>
          <span>Settings</span>
        </div>
        <button class="icon-btn mobile-only-btn" onclick="closeSettingsModal()" title="Close Settings">
          <svg class="icon-sm" viewBox="0 0 24 24"><path d="M19 6.41L17.59 5 12 10.59 6.41 5 5 6.41 10.59 12 5 17.59 6.41 19 12 13.41 17.59 19 19 17.59 13.41 12z"/></svg>
        </button>
      </div>

      
      <div class="notion-nav-group-label">Registration</div>
      <button class="notion-nav-item active" onclick="switchNotionTab('notion-reg-single')">
        <svg class="icon-sm" viewBox="0 0 24 24"><path d="M15 12c2.21 0 4-1.79 4-4s-1.79-4-4-4-4 1.79-4 4 1.79 4 4 4zm-9-2V7H4v3H1v2h3v3h2v-3h3v-2H6zm9 4c-2.67 0-8 1.34-8 4v2h16v-2c0-2.66-5.33-4-8-4z"/></svg>
        <div style="flex: 1;">
          <div>Single Registration</div>
          <div class="mobile-only-text">Assign card</div>
        </div>
        <svg class="mobile-chevron icon-sm" viewBox="0 0 24 24"><path d="M8.59 16.59L13.17 12 8.59 7.41 10 6l6 6-6 6-1.41-1.41z"/></svg>
      </button>

      <button class="notion-nav-item" onclick="switchNotionTab('notion-reg-batch')">
        <svg class="icon-sm" viewBox="0 0 24 24"><path d="M16 11c1.66 0 2.99-1.34 2.99-3S17.66 5 16 5c-1.66 0-3 1.34-3 3s1.34 3 3 3zm-8 0c1.66 0 2.99-1.34 2.99-3S9.66 5 8 5C6.34 5 5 6.34 5 8s1.34 3 3 3zm0 2c-2.33 0-7 1.17-7 3.5V19h14v-2.5c0-2.33-4.67-3.5-7-3.5zm8 0c-.29 0-.62.02-.97.05 1.16.84 1.97 1.97 1.97 3.45V19h6v-2.5c0-2.33-4.67-3.5-7-3.5z"/></svg>
        <div style="flex: 1;">
          <div>Batch Registration</div>
          <div class="mobile-only-text">Roster queue</div>
        </div>
        <svg class="mobile-chevron icon-sm" viewBox="0 0 24 24"><path d="M8.59 16.59L13.17 12 8.59 7.41 10 6l6 6-6 6-1.41-1.41z"/></svg>
      </button>

      <button class="notion-nav-item" onclick="switchNotionTab('notion-cards')">
        <svg class="icon-sm" viewBox="0 0 24 24"><path d="M20 4H4c-1.11 0-1.99.89-1.99 2L2 18c0 1.11.89 2 2 2h16c1.11 0 2-.89 2-2V6c0-1.11-.89-2-2-2zm0 14H4v-6h16v6zm0-10H4V6h16v2z"/></svg>
        <div style="flex: 1;">
          <div>Cards Database</div>
          <div class="mobile-only-text">Manage cards</div>
        </div>
        <span class="nav-badge" id="notionCardsBadge">0</span>
        <svg class="mobile-chevron icon-sm" viewBox="0 0 24 24"><path d="M8.59 16.59L13.17 12 8.59 7.41 10 6l6 6-6 6-1.41-1.41z"/></svg>
      </button>

      
      <div class="notion-nav-group-label">Data</div>
      <button class="notion-nav-item" onclick="switchNotionTab('notion-history')">
        <svg class="icon-sm" viewBox="0 0 24 24"><path d="M13 3c-4.97 0-9 4.03-9 9H1l3.89 3.89.07.14L9 12H6c0-3.87 3.13-7 7-7s7 3.13 7 7-3.13 7-7 7c-1.93 0-3.68-.79-4.94-2.06l-1.42 1.42C8.27 19.99 10.51 21 13 21c4.97 0 9-4.03 9-9s-4.03-9-9-9zm-1 5v5l4.28 2.54.72-1.21-3.5-2.08V8H12z"/></svg>
        <div style="flex: 1;">
          <div>Session History</div>
          <div class="mobile-only-text">Past sessions</div>
        </div>
        <span class="nav-badge" id="notionHistoryBadge">0</span>
        <svg class="mobile-chevron icon-sm" viewBox="0 0 24 24"><path d="M8.59 16.59L13.17 12 8.59 7.41 10 6l6 6-6 6-1.41-1.41z"/></svg>
      </button>

      <button class="notion-nav-item" onclick="switchNotionTab('notion-export')">
        <svg class="icon-sm" viewBox="0 0 24 24"><path d="M19 9h-4V3H9v6H5l7 7 7-7zM5 18v2h14v-2H5z"/></svg>
        <div style="flex: 1;">
          <div>Export &amp; Backup</div>
          <div class="mobile-only-text">Download CSV &amp; JSON</div>
        </div>
        <svg class="mobile-chevron icon-sm" viewBox="0 0 24 24"><path d="M8.59 16.59L13.17 12 8.59 7.41 10 6l6 6-6 6-1.41-1.41z"/></svg>
      </button>

      <button class="notion-nav-item" onclick="switchNotionTab('notion-import')">
        <svg class="icon-sm" viewBox="0 0 24 24"><path d="M9 16h6v-6h4l-7-7-7 7h4v6zm-4 2h14v2H5v-2z"/></svg>
        <div style="flex: 1;">
          <div>Import Cards</div>
          <div class="mobile-only-text">CSV import</div>
        </div>
        <svg class="mobile-chevron icon-sm" viewBox="0 0 24 24"><path d="M8.59 16.59L13.17 12 8.59 7.41 10 6l6 6-6 6-1.41-1.41z"/></svg>
      </button>

      
      <div class="notion-nav-group-label">Device</div>
      <button class="notion-nav-item" onclick="switchNotionTab('notion-rules')">
        <svg class="icon-sm" viewBox="0 0 24 24"><path d="M3 17v2h6v-2H3zM3 5v2h10V5H3zm10 16v-2h8v-2h-8v-2h-2v6h2zM7 9v2H3v2h4v2h2V9H7zm14 4v-2H11v2h10zm-6-4h2V7h4V5h-4V3h-2v6z"/></svg>
        <div style="flex: 1;">
          <div>Attendance Rules</div>
          <div class="mobile-only-text">Check-in policies</div>
        </div>
        <svg class="mobile-chevron icon-sm" viewBox="0 0 24 24"><path d="M8.59 16.59L13.17 12 8.59 7.41 10 6l6 6-6 6-1.41-1.41z"/></svg>
      </button>

      <button class="notion-nav-item" onclick="switchNotionTab('notion-hardware')">
        <svg class="icon-sm" viewBox="0 0 24 24"><path d="M20 3H4c-1.1 0-2 .9-2 2v11c0 1.1.9 2 2 2h3l-1 1v2h12v-2l-1-1h3c1.1 0 2-.9 2-2V5c0-1.1-.9-2-2-2zm0 13H4V5h16v11z"/></svg>
        <div style="flex: 1;">
          <div>Display &amp; LED</div>
          <div class="mobile-only-text">Screen &amp; light</div>
        </div>
        <svg class="mobile-chevron icon-sm" viewBox="0 0 24 24"><path d="M8.59 16.59L13.17 12 8.59 7.41 10 6l6 6-6 6-1.41-1.41z"/></svg>
      </button>

      <button class="notion-nav-item" onclick="switchNotionTab('notion-datetime')">
        <svg class="icon-sm" viewBox="0 0 24 24"><path d="M11.99 2C6.47 2 2 6.48 2 12s4.47 10 9.99 10C17.52 22 22 17.52 22 12S17.52 2 11.99 2zM12 20c-4.42 0-8-3.58-8-8s3.58-8 8-8 8 3.58 8 8-3.58 8-8 8zm.5-13H11v6l5.25 3.15.75-1.23-4.5-2.67z"/></svg>
        <div style="flex: 1;">
          <div>Date &amp; Time</div>
          <div class="mobile-only-text">Clock sync</div>
        </div>
        <svg class="mobile-chevron icon-sm" viewBox="0 0 24 24"><path d="M8.59 16.59L13.17 12 8.59 7.41 10 6l6 6-6 6-1.41-1.41z"/></svg>
      </button>

      
      <div class="notion-nav-group-label">System</div>
      <button class="notion-nav-item" onclick="switchNotionTab('notion-network')">
        <svg class="icon-sm" viewBox="0 0 24 24"><path d="M12 4C7.31 4 3.07 5.9 0 8.98L12 21 24 8.98C20.93 5.9 16.69 4 12 4zm0 3.5c3.27 0 6.27 1.25 8.54 3.32L12 19.34 3.46 10.82C5.73 8.75 8.73 7.5 12 7.5z"/></svg>
        <div style="flex: 1;">
          <div>Wi-Fi Hotspot</div>
          <div class="mobile-only-text">SSID &amp; password</div>
        </div>
        <svg class="mobile-chevron icon-sm" viewBox="0 0 24 24"><path d="M8.59 16.59L13.17 12 8.59 7.41 10 6l6 6-6 6-1.41-1.41z"/></svg>
      </button>

      <button class="notion-nav-item" onclick="switchNotionTab('notion-system')">
        <svg class="icon-sm" viewBox="0 0 24 24"><path d="M11 17h2v-6h-2v6zm1-15C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm0 18c-4.41 0-8-3.59-8-8s3.59-8 8-8 8 3.59 8 8-3.59 8-8 8zM11 9h2V7h-2v2z"/></svg>
        <div style="flex: 1;">
          <div>System &amp; Storage</div>
          <div class="mobile-only-text">Device status</div>
        </div>
        <svg class="mobile-chevron icon-sm" viewBox="0 0 24 24"><path d="M8.59 16.59L13.17 12 8.59 7.41 10 6l6 6-6 6-1.41-1.41z"/></svg>
      </button>
    </div>

    
    <div class="notion-content-pane">
      <div class="notion-content-header">
        <button class="notion-header-back-btn" onclick="closeNotionMobileDetail()">
          <svg class="icon-sm" viewBox="0 0 24 24"><path d="M15.41 7.41L14 6l-6 6 6 6 1.41-1.41L10.83 12z"/></svg>
          <span>Settings</span>
        </button>
        <div class="notion-content-title-area">
          <div class="notion-content-title" id="notionPaneTitle">Single Registration</div>
          <div class="notion-content-subtitle" id="notionPaneSubtitle">Assign an RFID card to an attendee</div>
        </div>
        <button class="icon-btn" onclick="closeSettingsModal()" title="Close Settings">
          <svg class="icon-sm" viewBox="0 0 24 24"><path d="M19 6.41L17.59 5 12 10.59 6.41 5 5 6.41 10.59 12 5 17.59 6.41 19 12 13.41 17.59 19 19 17.59 13.41 12z"/></svg>
        </button>
      </div>

      <div class="notion-scroll-body" id="notionScrollBody">

        
        <div id="notion-reg-single" class="notion-pane active" data-title="Single Registration" data-subtitle="Assign an RFID card to an attendee">
          <div class="notion-card">
            <div class="notion-card-title">Attendee Details</div>

            <div style="display: flex; gap: 8px; flex-wrap: wrap;">
              <div class="text-field" style="flex: 2; min-width: 130px;">
                <label>First Name *</label>
                <input type="text" id="regFirstName" placeholder="e.g. Juan" autocomplete="off" oninput="updateRegNamePreview()">
              </div>
              <div class="text-field" style="flex: 1; min-width: 70px; max-width: 100px;">
                <label>M.I.</label>
                <input type="text" id="regMiddleInitial" placeholder="e.g. D." maxlength="4" autocomplete="off" oninput="updateRegNamePreview()">
              </div>
            </div>

            <div class="text-field" style="margin-top: 10px;">
              <label>Last Name *</label>
              <input type="text" id="regLastName" placeholder="e.g. Dela Cruz" autocomplete="off" oninput="updateRegNamePreview()">
            </div>

            <div class="text-field" style="margin-top: 10px;">
              <label>ID Number (Optional)</label>
              <input type="text" id="regOwnerId" placeholder="e.g. 2024-0001" autocomplete="off">
            </div>

            
            <div id="regNamePreviewRow" style="margin-top: 10px; padding: 8px 12px; border-radius: 8px; background: var(--md-sys-color-surface-container-high); font-size: 0.8rem; display: flex; align-items: center; justify-content: space-between; flex-wrap: wrap; gap: 6px;">
              <span>LCD Display: <code id="regLcdPreview" style="font-weight: 700; color: var(--md-sys-color-primary);">--</code></span>
              <span id="regFullPreview" style="font-size: 0.75rem; color: var(--md-sys-color-on-surface-variant);"></span>
            </div>

            <input type="hidden" id="regOwnerName" value="">

            <div id="regStatusCard" style="display: none; margin: 12px 0; padding: 12px; border-radius: 8px; font-size: 0.85rem; font-weight: 500;"></div>

            <div style="display: flex; gap: 8px; margin-top: 14px; flex-wrap: wrap;">
              <button id="regStartBtn" class="btn btn-primary" onclick="startSingleCardRegistration()">
                <svg class="icon-sm" viewBox="0 0 24 24"><path d="M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm1 15h-2v-2h2v2zm0-4h-2V7h2v6z"/></svg>
                Tap Card to Register
              </button>
              <button class="btn btn-outlined" onclick="clearRegistrationFields()">Clear</button>
              <button id="regCancelBtn" class="btn btn-danger-tonal" style="display: none;" onclick="cancelSingleRegistration()">Cancel</button>
            </div>
          </div>
        </div>

        
        <div id="notion-reg-batch" class="notion-pane" data-title="Batch Registration" data-subtitle="Register cards in sequence">
          
          <div id="batchInputView" style="display: flex; flex-direction: column;">
            <div class="notion-card">
              <div class="notion-card-title">Roster List</div>
              <div class="notion-card-desc">Enter names, one per line (optional: Name, ID):</div>
              <div class="text-field" style="margin-bottom: 12px;">
                <textarea id="batchNamesInput" rows="6" style="font-family: monospace; font-size: 0.85rem; padding: 10px; border-radius: var(--md-shape-corner-small); border: 1px solid var(--md-sys-color-outline); background: var(--md-sys-color-surface); color: var(--md-sys-color-on-surface); resize: vertical; width: 100%; box-sizing: border-box;" placeholder="Juan D. Dela Cruz, 2024-001&#10;Maria C. Santos, 2024-002" oninput="updateBatchCount()"></textarea>
              </div>
              <div style="display: flex; justify-content: space-between; align-items: center; flex-wrap: wrap; gap: 8px;">
                <span id="batchCountBadge" class="badge">0 names</span>
                <div style="display: flex; gap: 8px;">
                  <button class="btn btn-outlined btn-sm" onclick="clearBatchInput()">Clear</button>
                  <button class="btn btn-primary btn-sm" onclick="startBatchScanning()">
                    <svg class="icon-sm" viewBox="0 0 24 24"><path d="M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm1 15h-2v-2h2v2zm0-4h-2V7h2v6z"/></svg>
                    Start Scanning
                  </button>
                </div>
              </div>
            </div>
          </div>

          
          <div id="batchScanningView" style="display: none; flex-direction: column;">
            <div style="margin-bottom: 10px;">
              <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 4px;">
                <span style="font-weight: 600; font-size: 0.85rem;" id="batchProgressLabel">Ready to scan</span>
                <span style="font-size: 0.8rem; color: var(--md-sys-color-on-surface-variant);" id="batchProgressPct">0%</span>
              </div>
              <div class="m3-linear-progress" style="margin: 0;">
                <div id="batchProgressBar" class="m3-linear-progress-bar" style="width: 0%;"></div>
              </div>
            </div>

            
            <div id="batchSpotlightCard" class="notion-card" style="text-align: center; border: 2px solid var(--md-sys-color-primary); background: var(--md-sys-color-surface-container-high);">
              <div style="font-size: 0.72rem; text-transform: uppercase; letter-spacing: 1px; font-weight: 700; color: var(--md-sys-color-primary); margin-bottom: 4px;" id="batchStepChip">TAP CARD FOR:</div>
              <div id="batchActiveName" style="font-size: 1.5rem; font-weight: 700; color: var(--md-sys-color-on-surface);">--</div>
              <div id="batchActiveId" style="font-size: 0.85rem; color: var(--md-sys-color-on-surface-variant); margin-top: 2px;">--</div>

              <div id="batchStatusPrompt" style="margin-top: 10px; padding: 10px; border-radius: 8px; background: var(--md-sys-color-surface); border: 1px dashed var(--md-sys-color-outline); font-size: 0.85rem; font-weight: 600;">
                Present card to the reader...
              </div>

              <div style="display: flex; justify-content: center; gap: 8px; margin-top: 12px;">
                <button class="btn btn-outlined btn-sm" onclick="skipBatchCurrent()">Skip</button>
                <button class="btn btn-danger-tonal btn-sm" onclick="cancelBatchProcess()">Cancel</button>
              </div>
            </div>

            
            <div class="notion-card" style="margin-top: 12px; padding: 0; overflow: hidden;">
              <div style="padding: 10px 14px; font-size: 0.82rem; font-weight: 700; border-bottom: 1px solid var(--md-sys-color-outline-variant); color: var(--md-sys-color-on-surface-variant);">
                Registration Queue
              </div>
              <div style="max-height: 220px; overflow-y: auto;">
                <table class="data-table" style="width: 100%; margin: 0; font-size: 0.82rem;">
                  <thead>
                    <tr>
                      <th style="width: 32px;">#</th>
                      <th>Name</th>
                      <th>ID</th>
                      <th>UID</th>
                      <th>Status</th>
                    </tr>
                  </thead>
                  <tbody id="batchQueueTableBody"></tbody>
                </table>
              </div>
            </div>
          </div>

          
          <div id="batchDoneView" style="display: none; flex-direction: column;">
            <div class="notion-card" style="text-align: center; padding: 24px;">
              <svg class="icon-xl" style="fill: var(--md-sys-color-primary); margin-bottom: 8px;" viewBox="0 0 24 24"><path d="M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm-2 15l-5-5 1.41-1.41L10 14.17l7.59-7.59L19 8l-9 9z"/></svg>
              <div style="font-size: 1.15rem; font-weight: 700; color: var(--md-sys-color-on-surface);">Batch Complete</div>
              <div style="font-size: 0.85rem; color: var(--md-sys-color-on-surface-variant); margin-top: 4px;" id="batchSummaryStats">All attendees processed.</div>
              <div style="margin-top: 16px; display: flex; justify-content: center; gap: 8px;">
                <button class="btn btn-outlined btn-sm" onclick="resetBatchInputView()">Register More</button>
                <button class="btn btn-primary btn-sm" onclick="finishBatchRegistration()">Done</button>
              </div>
            </div>
          </div>
        </div>

        
        <div id="notion-cards" class="notion-pane" data-title="Cards Database" data-subtitle="Registered RFID cards">
          <div style="display: flex; justify-content: space-between; align-items: center; flex-wrap: wrap; gap: 8px; margin-bottom: 10px;">
            <div class="search-box" style="flex: 1; max-width: 320px;">
              <svg class="icon-sm" viewBox="0 0 24 24"><path d="M15.5 14h-.79l-.28-.27A6.471 6.471 0 0 0 16 9.5 6.5 6.5 0 1 0 9.5 16c1.61 0 3.09-.59 4.23-1.57l.27.28v.79l5 4.99L20.49 19l-4.99-5zm-6 0C7.01 14 5 11.99 5 9.5S7.01 5 9.5 5 14 7.01 14 9.5 14z"/></svg>
              <input type="text" id="notionCardsSearch" placeholder="Search cards..." oninput="filterCardsTable()">
            </div>
            <div style="display: flex; gap: 6px; align-items: center;">
              <span id="notionCardsCountBadge" class="badge">0 cards</span>
              <a href="/api/cards/export.csv" class="btn btn-outlined btn-sm" download="registered_cards.csv">Export CSV</a>
              <button class="btn btn-outlined btn-sm" style="color: var(--md-sys-color-error);" onclick="clearAllCards()">Delete All</button>
            </div>
          </div>

          
          <div id="notionCardsListContainer" class="cards-mobile-view">
            <div class="empty-state" style="text-align: center; padding: 24px;">No cards registered yet.</div>
          </div>

          
          <div class="cards-table-container cards-desktop-view">
            <table class="console-table">
              <thead>
                <tr>
                  <th style="width: 36px;">#</th>
                  <th>Name</th>
                  <th>ID Number</th>
                  <th>Card UID</th>
                  <th>Registered</th>
                  <th style="width: 70px; text-align: center;">Actions</th>
                </tr>
              </thead>
              <tbody id="notionCardsTableBody">
                <tr><td colspan="6" style="text-align: center; padding: 24px;">No cards registered yet.</td></tr>
              </tbody>
            </table>
          </div>
        </div>

        
        <div id="notion-history" class="notion-pane" data-title="Session History" data-subtitle="Saved attendance sessions">
          
          <div id="historyListView" style="display: flex; flex-direction: column; gap: 10px;">
            <div style="display: flex; justify-content: space-between; align-items: center; flex-wrap: wrap; gap: 8px;">
              <div class="search-box" style="flex: 1; max-width: 300px;">
                <svg class="icon-sm" viewBox="0 0 24 24"><path d="M15.5 14h-.79l-.28-.27A6.471 6.471 0 0 0 16 9.5 6.5 6.5 0 1 0 9.5 16c1.61 0 3.09-.59 4.23-1.57l.27.28v.79l5 4.99L20.49 19l-4.99-5zm-6 0C7.01 14 5 11.99 5 9.5S7.01 5 9.5 5 14 7.01 14 9.5 14z"/></svg>
                <input type="text" id="historySearchInput" placeholder="Search sessions..." oninput="filterHistoryList()">
              </div>
              <div style="display: flex; gap: 6px; align-items: center;">
                <button class="btn btn-outlined btn-sm" onclick="loadSessionHistory()">Refresh</button>
                <button class="btn btn-outlined btn-sm" style="color: var(--md-sys-color-error);" onclick="clearAllHistory()">Delete All</button>
              </div>
            </div>

            
            <div id="historyListContainer" class="cards-mobile-view">
              <div class="empty-state" style="text-align: center; padding: 24px;">No saved sessions found.</div>
            </div>

            
            <div class="cards-table-container cards-desktop-view">
              <table class="console-table">
                <thead>
                  <tr>
                    <th style="width: 36px;">#</th>
                    <th>Session Title</th>
                    <th>Mode</th>
                    <th>Date &amp; Time</th>
                    <th>Attendees</th>
                    <th style="width: 120px; text-align: center;">Actions</th>
                  </tr>
                </thead>
                <tbody id="historyTableBody">
                  <tr><td colspan="6" style="text-align: center; padding: 24px;">No saved sessions found.</td></tr>
                </tbody>
              </table>
            </div>
          </div>

          
          <div id="historyDetailView" style="display: none; flex-direction: column; gap: 10px;">
            <div style="display: flex; justify-content: space-between; align-items: center; flex-wrap: wrap; gap: 8px;">
              <button class="btn btn-outlined btn-sm" onclick="backToHistoryList()">
                <svg class="icon-sm" viewBox="0 0 24 24"><path d="M20 11H7.83l5.59-5.59L12 4l-8 8 8 8 1.41-1.41L7.83 13H20v-2z"/></svg>
                Back to Sessions
              </button>
              <div style="display: flex; gap: 6px; align-items: center; flex-wrap: wrap;">
                <button id="historyResumeBtn" class="btn btn-primary btn-sm" onclick="resumeCurrentDetailSession()">
                  <svg class="icon-sm" viewBox="0 0 24 24"><path d="M8 5v14l11-7z"/></svg>
                  Continue Session
                </button>
                <button id="historyCopyTableBtn" class="btn btn-outlined btn-sm" onclick="copyHistoryDetailTable()" title="Copy table for Google Sheets / Excel">
                  <svg class="icon-sm" viewBox="0 0 24 24"><path d="M16 1H4c-1.1 0-2 .9-2 2v14h2V3h12V1zm3 4H8c-1.1 0-2 .9-2 2v14c0 1.1.9 2 2 2h11c1.1 0 2-.9 2-2V7c0-1.1-.9-2-2-2zm0 16H8V7h11v14z"/></svg>
                  Copy Table
                </button>
                <a id="historyExportCsvBtn" href="#" class="btn btn-outlined btn-sm">
                  <svg class="icon-sm" viewBox="0 0 24 24"><path d="M19 9h-4V3H9v6H5l7 7 7-7zM5 18v2h14v-2H5z"/></svg>
                  Export CSV
                </a>
                <button id="historyDeleteDetailBtn" class="btn btn-outlined btn-sm" style="color: var(--md-sys-color-error);" onclick="deleteCurrentDetailSession()">
                  <svg class="icon-sm" viewBox="0 0 24 24"><path d="M6 19c0 1.1.9 2 2 2h8c1.1 0 2-.9 2-2V7H6v12zM19 4h-3.5l-1-1h-5l-1 1H5v2h14V4z"/></svg>
                  Delete
                </button>
              </div>
            </div>

            <div id="historyDetailHeaderCard" class="notion-card" style="margin-bottom: 0;">
              <div style="display: flex; justify-content: space-between; align-items: center; flex-wrap: wrap; gap: 8px;">
                <div>
                  <div id="historyDetailTitle" style="font-size: 1.05rem; font-weight: 700;">--</div>
                  <div id="historyDetailMeta" style="font-size: 0.8rem; color: var(--md-sys-color-on-surface-variant); margin-top: 2px;">--</div>
                </div>
                <div id="historyDetailCountBadge" class="badge badge-done" style="font-size: 0.85rem; padding: 5px 10px;">0 Attendees</div>
              </div>
            </div>

            <div class="search-box">
              <svg class="icon-sm" viewBox="0 0 24 24"><path d="M15.5 14h-.79l-.28-.27A6.471 6.471 0 0 0 16 9.5 6.5 6.5 0 1 0 9.5 16c1.61 0 3.09-.59 4.23-1.57l.27.28v.79l5 4.99L20.49 19l-4.99-5zm-6 0C7.01 14 5 11.99 5 9.5S7.01 5 9.5 5 14 7.01 14 9.5 14z"/></svg>
              <input type="text" id="historyDetailSearch" placeholder="Search attendees..." oninput="filterHistoryDetailTable()">
            </div>

            
            <div id="historyDetailListContainer" class="cards-mobile-view">
              <div class="empty-state" style="text-align: center; padding: 24px;">No attendees in this session.</div>
            </div>

            
            <div class="cards-table-container cards-desktop-view">
              <table class="console-table">
                <thead>
                  <tr>
                    <th style="width: 36px;">#</th>
                    <th>Name</th>
                    <th>ID Number</th>
                    <th>Card UID</th>
                    <th>Time IN</th>
                    <th>Time OUT</th>
                    <th>Status</th>
                  </tr>
                </thead>
                <tbody id="historyDetailTableBody">
                  <tr><td colspan="7" style="text-align: center; padding: 24px;">No attendees in this session.</td></tr>
                </tbody>
              </table>
            </div>
          </div>
        </div>

        
        <div id="notion-export" class="notion-pane" data-title="Export &amp; Backup" data-subtitle="Download attendance data and system backups">
          
          <div class="notion-card">
            <div class="notion-card-title">
              <svg class="icon-sm" viewBox="0 0 24 24"><path d="M14 2H6c-1.1 0-1.99.9-1.99 2L4 20c0 1.1.89 2 1.99 2H18c1.1 0 2-.9 2-2V8l-6-6zm2 16H8v-2h8v2zm0-4H8v-2h8v2zm-3-5V3.5L18.5 9H13z"/></svg>
              <span>Current Session</span>
            </div>
            <div class="notion-card-desc">Download attendance records for the active session.</div>
            <a id="btnExportLiveCsv2" href="/api/attendance/export.csv" class="btn btn-primary btn-sm" download="attendance.csv">
              <svg class="icon-sm" viewBox="0 0 24 24"><path d="M19 9h-4V3H9v6H5l7 7 7-7zM5 18v2h14v-2H5z"/></svg>
              Export CSV
            </a>
          </div>

          
          <div class="notion-card">
            <div class="notion-card-title">
              <svg class="icon-sm" viewBox="0 0 24 24"><path d="M20 4H4c-1.11 0-1.99.89-1.99 2L2 18c0 1.11.89 2 2 2h16c1.11 0 2-.89 2-2V6c0-1.11-.89-2-2-2zm0 14H4v-6h16v6zm0-10H4V6h16v2z"/></svg>
              <span>Cards Directory</span>
            </div>
            <div class="notion-card-desc">Download all registered attendee cards and UIDs.</div>
            <a id="btnExportCardsCsv2" href="/api/cards/export.csv" class="btn btn-outlined btn-sm" download="registered_cards.csv">
              <svg class="icon-sm" viewBox="0 0 24 24"><path d="M19 9h-4V3H9v6H5l7 7 7-7zM5 18v2h14v-2H5z"/></svg>
              Export CSV
            </a>
          </div>

          
          <div class="notion-card">
            <div class="notion-card-title">
              <svg class="icon-sm" viewBox="0 0 24 24"><path d="M19.35 10.04C18.67 6.59 15.64 4 12 4 9.11 4 6.6 5.64 5.35 8.04 2.34 8.36 0 10.91 0 14c0 3.31 2.69 6 6 6h13c2.76 0 5-2.24 5-5 0-2.64-2.05-4.78-4.65-4.96zM17 13l-5 5-5-5h3V9h4v4h3z"/></svg>
              <span>System Backup</span>
            </div>
            <div class="notion-card-desc">Download complete JSON snapshot of cards, sessions, and settings.</div>
            <a id="btnExportBackupJson" href="/api/system/backup.json" class="btn btn-outlined btn-sm" download="esp32_attendance_backup.json">
              <svg class="icon-sm" viewBox="0 0 24 24"><path d="M19 9h-4V3H9v6H5l7 7 7-7zM5 18v2h14v-2H5z"/></svg>
              Export JSON
            </a>
          </div>
        </div>

        
        <div id="notion-import" class="notion-pane" data-title="Import Cards" data-subtitle="Add cardholders from CSV">
          <div class="notion-card">
            <div class="notion-card-title">CSV Import</div>
            <div class="notion-card-desc">Paste CSV text or select a file (format: Name, ID, Card UID):</div>

            <div class="text-field" style="margin-bottom: 12px;">
              <textarea id="importCsvTextarea" rows="6" style="font-family: monospace; font-size: 0.82rem; padding: 10px; border-radius: var(--md-shape-corner-small); border: 1px solid var(--md-sys-color-outline); background: var(--md-sys-color-surface); color: var(--md-sys-color-on-surface); resize: vertical; width: 100%; box-sizing: border-box;" placeholder="Juan D. Dela Cruz, 2024-001, 12:34:AB:CD
Maria C. Santos, 2024-002, EF:56:78:90"></textarea>
            </div>

            <div style="display: flex; gap: 8px; align-items: center; flex-wrap: wrap; margin-bottom: 12px;">
              <input type="file" id="importCsvFileInput" accept=".csv,.txt" style="display: none;" onchange="handleImportFileSelect(event)">
              <button class="btn btn-outlined btn-sm" onclick="document.getElementById('importCsvFileInput').click()">Choose File...</button>
              <button class="btn btn-primary btn-sm" onclick="executeDirectCardsImport()">Import Directly</button>
              <button class="btn btn-tonal btn-sm" onclick="loadImportIntoBatchQueue()">Queue for Scanning</button>
            </div>

            <div id="importFeedbackBox" style="display: none; padding: 10px 12px; border-radius: 8px; font-size: 0.8rem; font-weight: 500;"></div>
          </div>
        </div>

        
        <div id="notion-rules" class="notion-pane" data-title="Attendance Rules" data-subtitle="Check-in policies and session controls">
          <div class="notion-card">
            <div class="notion-card-title">Check-in Policy</div>

            <label class="notion-setting-row toggle-row" for="requireRegToggle">
              <div>
                <div class="notion-row-label">Require Registration</div>
                <div class="notion-row-desc">Only registered cards are logged as attendance.</div>
              </div>
              <div class="switch">
                <input type="checkbox" id="requireRegToggle" checked onchange="updateSettings()">
                <span class="switch-slider"></span>
              </div>
            </label>

            <div class="notion-setting-row">
              <div>
                <div class="notion-row-label">Tap Cooldown</div>
                <div class="notion-row-desc">Ignore duplicate taps from the same card within this window.</div>
              </div>
              <select id="cardDebounceSelect" class="form-input" style="width: auto; padding: 6px 10px; font-size: 0.85rem;" onchange="updateSettings()">
                <option value="1000">1.0 second</option>
                <option value="1500" selected>1.5 seconds (Default)</option>
                <option value="2000">2.0 seconds</option>
                <option value="3000">3.0 seconds</option>
                <option value="5000">5.0 seconds</option>
              </select>
            </div>
          </div>

          <div class="notion-card">
            <div class="notion-card-title">Session Controls</div>
            <div class="notion-card-desc">Switch mode or manage current session:</div>

            <div style="display: flex; gap: 8px; flex-wrap: wrap; margin-bottom: 12px;">
              <button class="btn btn-tonal btn-sm" onclick="setSessionMode('entrance');">
                <svg class="icon-sm" viewBox="0 0 24 24"><path d="M19 3H5c-1.11 0-2 .9-2 2v4h2V5h14v14H5v-4H3v4c0 1.1.89 2 2 2h14c1.1 0 2-.9 2-2V5c0-1.1-.9-2-2-2zm-8.92 12.58L11.5 17l5-5-5-5-1.42 1.41L12.67 11H3v2h9.67l-2.59 2.58z"/></svg>
                Entrance Mode
              </button>
              <button class="btn btn-tonal btn-sm" onclick="setSessionMode('exit');">
                <svg class="icon-sm" viewBox="0 0 24 24"><path d="M10.09 15.59L11.5 17l5-5-5-5-1.41 1.41L12.67 11H3v2h9.67l-2.58 2.59zM19 3H5c-1.11 0-2 .9-2 2v4h2V5h14v14H5v-4H3v4c0 1.1.89 2 2 2h14c1.1 0 2-.9 2-2V5c0-1.1-.9-2-2-2z"/></svg>
                Exit Mode
              </button>
            </div>

            <div style="display: flex; gap: 8px; flex-wrap: wrap;">
              <button class="btn btn-primary btn-sm" onclick="saveCurrentSession();">Save Session</button>
              <button class="btn btn-outlined btn-sm" onclick="stopSession();">End Session</button>
              <button class="btn btn-outlined btn-sm" onclick="clearAttendanceLog();">Reset Log</button>
            </div>
          </div>
        </div>

        
        <div id="notion-hardware" class="notion-pane" data-title="Display &amp; LED" data-subtitle="Screen brightness and status light">
          <div class="notion-card">
            <div class="notion-card-title">LCD Display</div>

            <div class="notion-setting-row">
              <div>
                <div class="notion-row-label">Brightness</div>
                <div class="notion-row-desc">Backlight intensity (0–100%).</div>
              </div>
              <div style="display: flex; align-items: center; gap: 8px;">
                <input type="range" id="lcdBrightnessSlider" min="0" max="100" value="100" style="width: 120px;" oninput="onLcdBrightnessInput(this.value)" onchange="updateSettings()">
                <span id="lcdBrightnessLabel" style="font-size: 0.82rem; font-weight: 600; min-width: 42px; text-align: right;">100%</span>
              </div>
            </div>

            <div class="notion-setting-row">
              <div>
                <div class="notion-row-label">Dim Before Sleep</div>
                <div class="notion-row-desc">Dims display backlight before turning off.</div>
              </div>
              <select id="lcdDimDurationSelect" class="form-input" style="width: auto; padding: 6px 10px; font-size: 0.85rem;" onchange="updateSettings()">
                <option value="5000">5 seconds</option>
                <option value="10000" selected>10 seconds (Default)</option>
                <option value="15000">15 seconds</option>
                <option value="0">Off (Instant)</option>
              </select>
            </div>

            <div class="notion-setting-row">
              <div>
                <div class="notion-row-label">Screen Timeout</div>
                <div class="notion-row-desc">Turn off backlight after inactivity.</div>
              </div>
              <select id="lcdTimeoutSelect" class="form-input" style="width: auto; padding: 6px 10px; font-size: 0.85rem;" onchange="updateSettings()">
                <option value="15000">15 seconds</option>
                <option value="30000">30 seconds</option>
                <option value="45000">45 seconds (Default)</option>
                <option value="60000">1 minute</option>
                <option value="120000">2 minutes</option>
                <option value="0">Never</option>
              </select>
            </div>

            <label class="notion-setting-row toggle-row" for="lcdWifiGuideToggle">
              <div>
                <div class="notion-row-label">Show Wi-Fi on Screen</div>
                <div class="notion-row-desc">Display network name and password during standby.</div>
              </div>
              <div class="switch">
                <input type="checkbox" id="lcdWifiGuideToggle" checked onchange="updateSettings()">
                <span class="switch-slider"></span>
              </div>
            </label>

            <div style="font-size: 0.75rem; color: var(--md-sys-color-on-surface-variant); margin-top: 8px;">
              Note: Connect LCD backlight jumper to ESP32 GPIO 14 for brightness control.
            </div>
          </div>

          <div class="notion-card">
            <div class="notion-card-title">Status Light (RGB LED)</div>

            <div class="notion-setting-row">
              <div>
                <div class="notion-row-label">Brightness</div>
                <div class="notion-row-desc">Status indicator brightness (0–100%).</div>
              </div>
              <div style="display: flex; align-items: center; gap: 8px;">
                <input type="range" id="rgbBrightnessSlider" min="0" max="100" value="100" style="width: 120px;" oninput="onRgbBrightnessInput(this.value)" onchange="updateSettings()">
                <span id="rgbBrightnessLabel" style="font-size: 0.82rem; font-weight: 600; min-width: 42px; text-align: right;">100%</span>
              </div>
            </div>

            <div class="notion-card-desc" style="margin-top: 10px;">Indicator states:</div>

            <div style="display: grid; grid-template-columns: repeat(auto-fit, minmax(180px, 1fr)); gap: 8px; margin: 10px 0;">
              <div style="background: rgba(255,255,255,0.03); border: 1px solid var(--md-sys-color-outline-variant); border-radius: 8px; padding: 8px 10px; font-size: 0.78rem;">
                <b>🟢 Green (1.5s)</b>: Card Recorded
              </div>
              <div style="background: rgba(255,255,255,0.03); border: 1px solid var(--md-sys-color-outline-variant); border-radius: 8px; padding: 8px 10px; font-size: 0.78rem;">
                <b>🟡 Yellow (1.5s)</b>: Duplicate Tap
              </div>
              <div style="background: rgba(255,255,255,0.03); border: 1px solid var(--md-sys-color-outline-variant); border-radius: 8px; padding: 8px 10px; font-size: 0.78rem;">
                <b>🔴 Red (1.5s)</b>: Not Registered
              </div>
              <div style="background: rgba(255,255,255,0.03); border: 1px solid var(--md-sys-color-outline-variant); border-radius: 8px; padding: 8px 10px; font-size: 0.78rem;">
                <b>⚪ White Blink</b>: Out of Session
              </div>
            </div>

            <div style="font-size: 0.8rem; font-weight: 600; margin-bottom: 6px;">Test Status Light</div>
            <div style="display: flex; gap: 8px; flex-wrap: wrap;">
              <button class="btn btn-tonal btn-sm" onclick="triggerRgbTest('recorded')" style="color: #22c55e;">Test Green</button>
              <button class="btn btn-tonal btn-sm" onclick="triggerRgbTest('duplicate')" style="color: #eab308;">Test Yellow</button>
              <button class="btn btn-tonal btn-sm" onclick="triggerRgbTest('rejected')" style="color: #ef4444;">Test Red</button>
              <button class="btn btn-tonal btn-sm" onclick="triggerRgbTest('white')">Test White</button>
            </div>
          </div>

          <div class="notion-card">
            <div class="notion-card-title">Display Actions</div>
            <div style="display: flex; gap: 8px; flex-wrap: wrap;">
              <button id="btnLcdOff" class="btn btn-tonal btn-sm" onclick="triggerLcdSleep()">Turn Off Screen</button>
              <button id="btnLcdWake" class="btn btn-tonal btn-sm" onclick="triggerLcdWake()">Wake Screen</button>
              <button id="btnLcdTest" class="btn btn-outlined btn-sm" onclick="triggerLcdTest()">Test LCD</button>
            </div>
          </div>
        </div>

        
        <div id="notion-datetime" class="notion-pane" data-title="Date &amp; Time" data-subtitle="Real-time clock synchronization">
          <div class="notion-card">
            <div class="notion-card-title">Device Clock</div>
            <div class="notion-card-desc">Current device time: <strong id="notionClockDisplay" style="font-family: monospace; color: var(--md-sys-color-primary);">--:--:--</strong></div>

            <div style="margin-bottom: 16px;">
              <button class="btn btn-primary btn-sm" onclick="syncBrowserTime()">
                <svg class="icon-sm" viewBox="0 0 24 24"><path d="M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm1 15h-2v-2h2v2zm0-4h-2V7h2v6z"/></svg>
                Sync with Phone Clock
              </button>
            </div>

            <div style="border-top: 1px solid var(--md-sys-color-outline-variant); padding-top: 12px;">
              <div class="notion-row-label" style="margin-bottom: 6px;">Set Manually</div>
              <div style="display: flex; gap: 8px; flex-wrap: wrap;">
                <input type="datetime-local" id="notionCustomTimeInput" class="form-input" style="width: auto; padding: 6px 10px; font-size: 0.85rem;">
                <button class="btn btn-outlined btn-sm" onclick="setCustomDeviceTime()">Apply</button>
              </div>
            </div>
          </div>
        </div>

        
        <div id="notion-network" class="notion-pane" data-title="Wi-Fi Hotspot" data-subtitle="Network credentials and status">
          <div class="notion-card">
            <div class="notion-card-title">Hotspot Status</div>
            <div class="notion-setting-row">
              <span class="notion-row-label">Network Name (SSID)</span>
              <span id="notionSsidText" style="font-family: monospace; font-weight: 600;">ESP32-Attendance</span>
            </div>
            <div class="notion-setting-row">
              <span class="notion-row-label">IP Address</span>
              <span id="notionIpText" style="font-family: monospace;">192.168.4.1</span>
            </div>
            <div class="notion-setting-row">
              <span class="notion-row-label">Gateway</span>
              <span id="notionGwText" style="font-family: monospace;">192.168.4.1</span>
            </div>
            <div class="notion-setting-row">
              <span class="notion-row-label">Connected Clients</span>
              <span id="notionClientsBadge" class="badge">1 device</span>
            </div>
          </div>

          <div class="notion-card">
            <div class="notion-card-title">Hotspot Password</div>
            <div class="notion-card-desc">Takes effect after restart.</div>

            <div style="margin-bottom: 10px;">
              <div style="display: flex; gap: 8px;">
                <input type="text" id="newWifiPassInput" class="form-input" placeholder="Min. 8 characters (or blank for open)" style="flex: 1;" maxlength="63">
                <button class="btn btn-primary" type="button" onclick="submitChangeWifiPassword()">
                  Save
                </button>
              </div>
              <div style="font-size: 0.73rem; color: var(--md-sys-color-on-surface-variant); margin-top: 6px;">
                Password will be shown on the LCD screen on next boot.
              </div>
            </div>
          </div>
        </div>

        
        <div id="notion-system" class="notion-pane" data-title="System &amp; Storage" data-subtitle="Device information and maintenance">
          <div class="notion-card">
            <div class="notion-card-title">Device Information</div>
            <div class="notion-setting-row">
              <span class="notion-row-label">Microcontroller</span>
              <span>ESP32 Dual-Core (240 MHz)</span>
            </div>
            <div class="notion-setting-row">
              <span class="notion-row-label">Free Dynamic RAM</span>
              <span id="notionHeapText" style="font-family: monospace;">--</span>
            </div>
            <div class="notion-setting-row">
              <span class="notion-row-label">System Uptime</span>
              <span id="notionUptimeText" style="font-family: monospace;">--</span>
            </div>
          </div>

          <div class="notion-card">
            <div class="notion-card-title">Storage &amp; Capacity</div>
            <div class="notion-setting-row">
              <span class="notion-row-label">Flash Partition</span>
              <span id="notionFsTotalText">2.00 MB LittleFS</span>
            </div>
            <div class="notion-setting-row">
              <span class="notion-row-label">Used Space</span>
              <span id="notionFsUsedText" style="font-family: monospace;">--</span>
            </div>
            <div class="notion-setting-row">
              <span class="notion-row-label">Available Free Space</span>
              <span id="notionFsFreeText" style="font-family: monospace;">--</span>
            </div>
            <div class="notion-setting-row">
              <span class="notion-row-label">Registered Cards</span>
              <span id="notionCardsTotalText">--</span>
            </div>
            <div class="notion-setting-row">
              <span class="notion-row-label">Estimated Remaining Cards</span>
              <span id="notionCardsEstCapacity" style="font-weight: 700; color: var(--md-sys-color-primary);">--</span>
            </div>
            <div class="notion-setting-row">
              <span class="notion-row-label">Saved Sessions (Max 5)</span>
              <span id="notionHistoryTotalText">--</span>
            </div>
          </div>

          <div class="notion-card" style="border-color: var(--md-sys-color-error);">
            <div class="notion-card-title" style="color: var(--md-sys-color-error);">Maintenance</div>
            <div class="notion-card-desc">Reset system or erase data:</div>
            <div style="display: flex; gap: 8px; flex-wrap: wrap; margin-top: 8px;">
              <button class="btn btn-outlined btn-sm" onclick="triggerSystemReboot()">Restart Device</button>
              <button class="btn btn-outlined btn-sm" onclick="clearAllCards()" style="color: var(--md-sys-color-error);">Erase All Cards</button>
              <button class="btn btn-danger-tonal btn-sm" onclick="triggerDeviceReset()">Device Reset</button>
            </div>
          </div>
        </div>

      </div>
    </div>

  </div>
</div>

<div id="historyModal" style="display: none;"></div>


<div id="regModal" class="modal-overlay">
  <div class="m3-dialog" style="text-align: center; max-width: 360px;">
    <div id="regModalIconWrap" style="width: 48px; height: 48px; border-radius: 50%; background: var(--md-sys-color-primary-container); color: var(--md-sys-color-primary); display: inline-flex; align-items: center; justify-content: center; margin-bottom: 12px; transition: all 0.25s ease;">
      <svg id="regModalIcon" class="icon" viewBox="0 0 24 24"><path fill="currentColor" d="M20 4H4c-1.11 0-1.99.89-1.99 2L2 18c0 1.11.89 2 2 2h16c1.11 0 2-.89 2-2V6c0-1.11-.89-2-2-2zm0 14H4v-6h16v6zm0-10H4V6h16v2z"/></svg>
    </div>
    <div id="regModalTitle" class="m3-dialog-title" style="font-size: 1.15rem; font-weight: 700; margin-bottom: 4px;">Register Card</div>
    <div style="font-size: 0.85rem; color: var(--md-sys-color-on-surface-variant); margin-bottom: 12px;">
      Assigning to <strong id="regModalOwnerName" style="color: var(--md-sys-color-on-surface); font-weight: 600;">--</strong>
    </div>

    <div id="regModalStatusText" style="font-size: 0.875rem; font-weight: 500; color: var(--md-sys-color-on-surface); margin-bottom: 12px; line-height: 1.4; min-height: 40px; display: flex; align-items: center; justify-content: center;">
      Present card to the reader...
    </div>

    <div id="regCountdownProgress" class="m3-linear-progress" style="margin-bottom: 14px;">
      <div id="regCountdownBar" class="m3-linear-progress-bar"></div>
    </div>

    <div id="regModalSuccessActions" style="display: none; gap: 8px; margin-top: 14px;">
      <button class="btn btn-primary btn-sm" style="flex: 1;" onclick="registerAnotherCard()">Register Another</button>
      <button class="btn btn-outlined btn-sm" style="flex: 1;" onclick="closeRegModal()">Done</button>
    </div>

    <div id="regModalCancelAction" style="margin-top: 10px;">
      <button id="regModalCancelBtn" class="btn btn-outlined btn-sm" style="width: 100%;" onclick="cancelRegistration()">
        Cancel
      </button>
    </div>
  </div>
</div>


<div id="timeModal" class="modal-overlay" onclick="handleOverlayClick(event, 'timeModal')">
  <div class="m3-dialog">
    <div class="m3-dialog-header">
      <div class="m3-dialog-title">System Clock</div>
      <button class="icon-btn" onclick="closeTimeModal()">
        <svg class="icon" viewBox="0 0 24 24"><path d="M19 6.41L17.59 5 12 10.59 6.41 5 5 6.41 10.59 12 5 17.59 6.41 19 12 13.41 17.59 19 19 17.59 13.41 12z"/></svg>
      </button>
    </div>

    <button class="btn btn-primary" style="width: 100%; margin-bottom: 12px;" onclick="syncBrowserTime(); closeTimeModal();">
      <svg class="icon-sm" viewBox="0 0 24 24"><path d="M11.99 2C6.47 2 2 6.48 2 12s4.47 10 9.99 10C17.52 22 22 17.52 22 12S17.52 2 11.99 2zM12 20c-4.42 0-8-3.58-8-8s3.58-8 8-8 8 3.58 8 8-3.58 8-8 8zm.5-13H11v6l5.25 3.15.75-1.23-4.5-2.67z"/></svg>
      Sync Device Time (1-Click)
    </button>

    <div class="text-field">
      <label>Manual Date &amp; Time:</label>
      <input type="datetime-local" id="modalDateTimeInput">
    </div>

    <div style="display: flex; justify-content: flex-end; gap: 8px;">
      <button class="btn btn-outlined btn-sm" onclick="closeTimeModal()">Cancel</button>
      <button class="btn btn-primary btn-sm" onclick="applyModalManualTime()">Save</button>
    </div>
  </div>
</div>

<div id="firstBootModal" style="display: none;"></div>

<script>
window.escapeHtml = function(val) {
  if (val === null || val === undefined) return '';
  return String(val)
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
    .replace(/'/g, '&#039;');
};

window.saveCurrentSession = function() { if (window.saveActiveSession) window.saveActiveSession(); };
window.startSingleCardRegistration = function() { if (window.startRegistration) window.startRegistration(); };
window.cancelSingleRegistration = function() { if (window.cancelRegistration) window.cancelRegistration(); };
window.updateRegNamePreview = function() {
  const first = document.getElementById('regFirstName')?.value.trim() || '';
  const mi = document.getElementById('regMiddleInitial')?.value.trim() || '';
  const last = document.getElementById('regLastName')?.value.trim() || '';
  const miClean = mi ? (mi.endsWith('.') ? mi : mi + '.') : '';

  let full = '';
  if (last && first) {
    full = `${last}, ${first}${miClean ? ' ' + miClean : ''}`;
  } else if (first || last) {
    full = last || first;
  }

  const hidden = document.getElementById('regOwnerName');
  if (hidden) hidden.value = full;

  const lcdEl = document.getElementById('regLcdPreview');
  const fullEl = document.getElementById('regFullPreview');
  if (lcdEl) {
    if (last && first) {
      lcdEl.innerText = `${last.toUpperCase()}, ${first.charAt(0).toUpperCase()}.`;
    } else if (last || first) {
      lcdEl.innerText = (last || first).toUpperCase();
    } else {
      lcdEl.innerText = '--';
    }
  }
  if (fullEl) fullEl.innerText = full ? `(${full})` : '';
};

window.clearRegistrationFields = function() {
  const f = document.getElementById('regFirstName');
  const m = document.getElementById('regMiddleInitial');
  const l = document.getElementById('regLastName');
  const i = document.getElementById('regOwnerId');
  const n = document.getElementById('regOwnerName');
  if (f) f.value = '';
  if (m) m.value = '';
  if (l) l.value = '';
  if (i) i.value = '';
  if (n) n.value = '';
  window.updateRegNamePreview();
  const s = document.getElementById('regStatusCard');
  if (s) s.style.display = 'none';
};
window.finishBatchRegistration = function() { window.switchNotionTab('notion-cards'); };
window.executeDirectCardsImport = function() { if (window.submitImportCards) window.submitImportCards(); };
window.loadImportIntoBatchQueue = function() { if (window.sendImportToBatch) window.sendImportToBatch(); };
window.triggerDeviceReset = async function() {
  const confirmed = confirm(
    "⚠️ WARNING: DEVICE RESET\n\n" +
    "This will permanently erase:\n" +
    "• All registered RFID cards\n" +
    "• All saved attendance sessions\n" +
    "• All custom system settings & preferences\n\n" +
    "The ESP32 will restore factory defaults and reboot.\n\n" +
    "Are you sure you want to proceed?"
  );
  if (!confirmed) return;

  const phrase = prompt("Type RESET in all capital letters to confirm:");
  if (phrase !== "RESET") {
    alert("Device reset cancelled.");
    return;
  }

  try {
    const res = await fetch('/api/system/reset', { method: 'POST' });
    const data = await res.json();
    alert(data.message || 'Device reset complete. System is rebooting...');
    setTimeout(() => { window.location.reload(); }, 4000);
  } catch(e) {
    alert('Device reset initiated. Restarting...');
    setTimeout(() => { window.location.reload(); }, 4000);
  }
};
window.triggerFactoryReset = window.triggerDeviceReset;

function initAllTouchScrolls() {
}

let audioCtx = null;
window.getAudioContext = function() {
  if (!audioCtx) {
    try {
      audioCtx = new (window.AudioContext || window.webkitAudioContext)();
    } catch(e) {}
  }
  if (audioCtx && audioCtx.state === 'suspended') {
    audioCtx.resume().catch(() => {});
  }
  return audioCtx;
};

['click', 'touchstart', 'keydown'].forEach(evt => {
  document.addEventListener(evt, () => window.getAudioContext(), { once: true, passive: true });
});

window.playAudioTone = function(type) {
  try {
    const ctx = window.getAudioContext();
    if (!ctx) return;
    const osc = ctx.createOscillator();
    const gain = ctx.createGain();
    osc.connect(gain);
    gain.connect(ctx.destination);
    const t = ctx.currentTime;
    if (type === 'success') {
      osc.frequency.setValueAtTime(587.33, t);
      osc.frequency.setValueAtTime(880, t + 0.1);
      gain.gain.setValueAtTime(0.18, t);
      gain.gain.exponentialRampToValueAtTime(0.001, t + 0.35);
      osc.start(t);
      osc.stop(t + 0.35);
    } else if (type === 'duplicate') {
      osc.type = 'triangle';
      osc.frequency.setValueAtTime(392, t);
      osc.frequency.setValueAtTime(329.63, t + 0.12);
      gain.gain.setValueAtTime(0.2, t);
      gain.gain.exponentialRampToValueAtTime(0.001, t + 0.3);
      osc.start(t);
      osc.stop(t + 0.3);
    } else {
      osc.type = 'sawtooth';
      osc.frequency.setValueAtTime(261.63, t);
      gain.gain.setValueAtTime(0.15, t);
      gain.gain.exponentialRampToValueAtTime(0.001, t + 0.25);
      osc.start(t);
      osc.stop(t + 0.25);
    }
  } catch(e) {}
};

let selectedDirection = 'Entrance';
let lastScanTimestamp = 0;
let isFirstPoll = true;
let isPollingReg = false;
let regPollInterval = null;
let allAttendeesData = [];
let allCardsData = [];
let firstBootModalShown = false;
let manualTimeSetByUser = false;
let scanDisplayTimeout = null;

window.initTheme = function() {
  const saved = localStorage.getItem('theme');
  const isDark = saved ? saved === 'dark' : (window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches);
  window.setTheme(isDark ? 'dark' : 'light');
};

window.setTheme = function(theme) {
  document.documentElement.setAttribute('data-theme', theme);
  localStorage.setItem('theme', theme);
  const isDark = theme === 'dark';
  document.querySelectorAll('.theme-sun').forEach(el => el.style.display = isDark ? 'inline-block' : 'none');
  document.querySelectorAll('.theme-moon').forEach(el => el.style.display = isDark ? 'none' : 'inline-block');
};

window.toggleTheme = function() {
  const current = document.documentElement.getAttribute('data-theme') || 'light';
  window.setTheme(current === 'dark' ? 'light' : 'dark');
};

window.goToEntranceStep = function(stepNum) {
  if (stepNum === 2) {
    const titleVal = document.getElementById('sessionTitleInput').value.trim() || 'General Session';
    document.getElementById('step2TitlePreview').innerText = titleVal;
    document.getElementById('entranceStep1').classList.remove('active');
    document.getElementById('entranceStep2').classList.add('active');
  } else {
    document.getElementById('entranceStep2').classList.remove('active');
    document.getElementById('entranceStep1').classList.add('active');
  }
};

window.selectEntranceDirection = function(dir) {
  selectedDirection = dir;
  document.getElementById('dirCardEntrance').classList.toggle('selected', dir === 'Entrance');
  document.getElementById('dirCardExit').classList.toggle('selected', dir === 'Exit');
};

window.startSession = async function() {
  const title = document.getElementById('sessionTitleInput').value.trim() || 'General Session';
  const formData = new URLSearchParams();
  formData.append('type', selectedDirection);
  formData.append('title', title);

  try {
    const res = await fetch('/api/session/start', { method: 'POST', body: formData });
    const data = await res.json();
    if (data.success) {
      window.setSessionSavedState(false);
      window.pollStatus();
      window.loadAttendanceLog();
    } else {
      alert('Notice: ' + data.message);
    }
  } catch(e) {
    alert('Network error: ' + e);
  }
};

window.stopSession = async function() {
  if (!confirm('End the current attendance session?')) return;
  try {
    const res = await fetch('/api/session/stop', { method: 'POST' });
    const data = await res.json();
    if (scanDisplayTimeout) {
      clearTimeout(scanDisplayTimeout);
      scanDisplayTimeout = null;
    }
    window.setSessionSavedState(false);
    window.showIdleTapPrompt();
    window.goToEntranceStep(1);
    window.pollStatus();
  } catch(e) {
    alert('Network error: ' + e);
  }
};

window.setSessionMode = async function(targetMode) {
  try {
    const isExit = (targetMode === 'Exit');
    const rfidModeEntrance = document.getElementById('rfidModeEntrance');
    const rfidModeExit = document.getElementById('rfidModeExit');
    if (rfidModeEntrance && rfidModeExit) {
      rfidModeEntrance.classList.toggle('active-in', !isExit);
      rfidModeExit.classList.toggle('active-out', isExit);
    }
    const idleModePill = document.getElementById('idleModePill');
    if (idleModePill) {
      idleModePill.innerText = `● ${targetMode.toUpperCase()} MODE (${isExit ? 'OUT' : 'IN'})`;
    }
    const modeChip = document.getElementById('activeModeChip');
    if (modeChip) {
      modeChip.innerText = targetMode;
      modeChip.className = isExit ? 'badge badge-out' : 'badge badge-in';
    }

    const formData = new URLSearchParams();
    formData.append('mode', targetMode);
    const res = await fetch('/api/session/set_mode', { method: 'POST', body: formData });
    const data = await res.json();
    if (data.success) {
      window.pollStatus();
    }
  } catch(e) {
    console.error('Mode switch error:', e);
  }
};

window.toggleSessionMode = async function() {
  const currentChip = document.getElementById('activeModeChip');
  const target = (currentChip && currentChip.innerText === 'Exit') ? 'Entrance' : 'Exit';
  await window.setSessionMode(target);
};

window.clearAttendanceLog = async function() {
  if (!confirm('Reset current attendance log?')) return;
  try {
    const res = await fetch('/api/session/clear', { method: 'POST' });
    const data = await res.json();
    window.loadAttendanceLog();
  } catch(e) {
    alert('Network error: ' + e);
  }
};

window.formatTime12h = function(str) {
  if (!str || str === '--' || str === 'Time Not Set' || str.includes('Not Set')) return '--';
  str = String(str).trim();
  
  const hasAmPm = /am|pm/i.test(str);
  if (hasAmPm) {
    const parts = str.split(/\s+/);
    if (parts.length >= 3 && parts[0].includes('-')) {
      return parts.slice(1).join(' ');
    }
    return str;
  }
  
  let timePart = str;
  if (timePart.includes(' ')) {
    timePart = timePart.split(' ')[1];
  }
  
  const parts = timePart.split(':');
  if (parts.length >= 2) {
    let h = parseInt(parts[0], 10);
    const m = parts[1];
    const s = (parts[2] !== undefined && parts[2] !== '') ? parts[2] : null;
    if (!isNaN(h)) {
      const ampm = h >= 12 ? 'PM' : 'AM';
      h = h % 12;
      if (h === 0) h = 12;
      const hStr = h < 10 ? '0' + h : '' + h;
      return s !== null ? `${hStr}:${m}:${s} ${ampm}` : `${hStr}:${m} ${ampm}`;
    }
  }
  return str;
};

window.formatClock = function(str) {
  if (!str || str === 'Time Not Set' || str.includes('Not Set')) return '--:--:--';
  return window.formatTime12h(str);
};

window.sendTimeSync = async function(epochSec, tzOffsetMin) {
  const formData = new URLSearchParams();
  formData.append('epoch', epochSec);
  formData.append('tz_offset', tzOffsetMin);
  const url = '/api/time?epoch=' + encodeURIComponent(epochSec) + '&tz_offset=' + encodeURIComponent(tzOffsetMin);
  const res = await fetch(url, { method: 'POST', body: formData });
  return await res.json();
};

let isAutoSyncingTime = false;
window.autoSyncTime = async function() {
  if (isAutoSyncingTime) return;
  isAutoSyncingTime = true;
  const now = new Date();
  const epoch = Math.floor(now.getTime() / 1000);
  const tzOffset = now.getTimezoneOffset();
  try {
    const data = await window.sendTimeSync(epoch, tzOffset);
    if (data.success) {
      manualTimeSetByUser = true;
      const clockHeader = document.getElementById('headerClock');
      if (clockHeader) clockHeader.innerText = window.formatClock(data.time);
      const notionClock = document.getElementById('notionClockDisplay');
      if (notionClock) notionClock.innerText = window.formatClock(data.time);
      console.log('[TIME] Auto-synced with browser time:', data.time);
    }
  } catch(e) {
    console.error('[TIME] Auto-sync error:', e);
  } finally {
    isAutoSyncingTime = false;
  }
};

window.syncBrowserTime = async function() {
  const now = new Date();
  const epoch = Math.floor(now.getTime() / 1000);
  const tzOffset = now.getTimezoneOffset();
  try {
    const data = await window.sendTimeSync(epoch, tzOffset);
    if (data.success) {
      manualTimeSetByUser = true;
      window.pollStatus();
      alert('Clock synchronized: ' + data.time);
    } else {
      alert('Sync failed: ' + data.message);
    }
  } catch(e) {
    alert('Network error: ' + e);
  }
};

window.syncBrowserTimeFromModal = async function() {
  const now = new Date();
  const epoch = Math.floor(now.getTime() / 1000);
  const tzOffset = now.getTimezoneOffset();
  try {
    const data = await window.sendTimeSync(epoch, tzOffset);
    if (data.success) {
      manualTimeSetByUser = true;
      const fb = document.getElementById('firstBootTimeFeedback');
      fb.style.display = 'block';
      fb.innerHTML = '&#10003; Clock synchronized: <b>' + data.time + '</b>';
      window.pollStatus();
    }
  } catch(e) {
    alert('Error syncing time: ' + e);
  }
};

window.applyFirstBootManualTime = async function() {
  const val = document.getElementById('firstBootDateTime').value;
  if (!val) { alert('Please pick date and time'); return; }
  const d = new Date(val);
  const epoch = Math.floor(d.getTime() / 1000);
  const tzOffset = d.getTimezoneOffset();
  try {
    const data = await window.sendTimeSync(epoch, tzOffset);
    if (data.success) {
      manualTimeSetByUser = true;
      const fb = document.getElementById('firstBootTimeFeedback');
      fb.style.display = 'block';
      fb.innerHTML = '&#10003; Clock set: <b>' + data.time + '</b>';
      window.pollStatus();
    }
  } catch(e) {
    alert('Error: ' + e);
  }
};

window.dismissFirstBootModal = function() {
  document.getElementById('firstBootModal').classList.remove('active');
  window.notifyModalState();
};

window.openTimeModal = function() {
  const now = new Date();
  now.setMinutes(now.getMinutes() - now.getTimezoneOffset());
  document.getElementById('modalDateTimeInput').value = now.toISOString().slice(0, 16);
  document.getElementById('timeModal').classList.add('active');
  window.notifyModalState();
};

window.closeTimeModal = function() {
  document.getElementById('timeModal').classList.remove('active');
  window.notifyModalState();
};

window.applyModalManualTime = async function() {
  const val = document.getElementById('modalDateTimeInput').value;
  if (!val) { alert('Please select date and time'); return; }
  const d = new Date(val);
  const epoch = Math.floor(d.getTime() / 1000);
  const tzOffset = d.getTimezoneOffset();
  try {
    const data = await window.sendTimeSync(epoch, tzOffset);
    if (data.success) {
      manualTimeSetByUser = true;
      window.closeTimeModal();
      window.pollStatus();
      alert('Clock set: ' + data.time);
    } else {
      alert('Error: ' + data.message);
    }
  } catch(e) {
    alert('Error: ' + e);
  }
};

window.notifyModalState = function() {
  const isSettings = document.getElementById('settingsModal')?.classList.contains('active');
  const isTime = document.getElementById('timeModal')?.classList.contains('active');
  const isFirstBoot = document.getElementById('firstBootModal')?.classList.contains('active');
  const isHistory = document.getElementById('historyModal')?.classList.contains('active');
  const activePane = document.querySelector('.notion-pane.active')?.id || '';
  const anyModalOpen = Boolean(isSettings || isTime || isFirstBoot || isHistory);
  let url = '/api/settings/state?open=' + (anyModalOpen ? '1' : '0');
  if (isSettings && activePane) {
    url += '&tab=' + encodeURIComponent(activePane);
  }
  fetch(url, { method: 'POST' }).catch(()=>{});
};


let allHistoryData = [];
let currentDetailSession = null;
let currentDetailAttendees = [];

let isHistoryLoading = false;
window.loadSessionHistory = async function() {
  if (isHistoryLoading) return;
  isHistoryLoading = true;
  const container = document.getElementById('historyListContainer');
  const tbody = document.getElementById('historyTableBody');
  try {
    const res = await fetch('/api/history');
    if (!res.ok) throw new Error('HTTP ' + res.status);
    const data = await res.json();
    allHistoryData = Array.isArray(data) ? data : [];
    const len = allHistoryData.length;
    const badge = document.getElementById('notionHistoryBadge');
    if (badge) badge.innerText = len;
    const totalEl = document.getElementById('notionHistoryTotalText');
    if (totalEl) totalEl.innerText = `${len} sessions`;
    const searchEl = document.getElementById('historySearchInput');
    const q = searchEl ? searchEl.value.trim() : '';
    if (q) window.filterHistoryList();
    else window.renderHistoryList(allHistoryData);
  } catch(e) {
    console.error('History load error:', e);
    if (container) {
      container.innerHTML = `<div class="empty-state" style="text-align: center; padding: 24px; color: var(--md-sys-color-error);">
        Unable to load sessions. <button class="btn btn-outlined btn-sm" style="margin-top: 8px;" onclick="loadSessionHistory()">Retry</button>
      </div>`;
    }
  } finally {
    isHistoryLoading = false;
  }
};

window.renderHistoryList = function(sessions) {
  const container = document.getElementById('historyListContainer');
  const tbody = document.getElementById('historyTableBody');
  if (!sessions || sessions.length === 0) {
    if (container) {
      container.innerHTML = '<div class="empty-state" style="text-align: center; padding: 32px 16px; color: var(--md-sys-color-on-surface-variant); font-size: 0.875rem;">No saved sessions found.</div>';
    }
    if (tbody) {
      tbody.innerHTML = '<tr><td colspan="6" style="text-align: center; color: var(--md-sys-color-on-surface-variant); padding: 28px;">No saved sessions in history.</td></tr>';
    }
    return;
  }
  let cardHtml = '';
  let tableHtml = '';
  sessions.forEach((s, idx) => {
    const modeBadge = s.type === 'Exit' ? '<span class="badge badge-out">Exit</span>' : '<span class="badge badge-in">Entrance</span>';
    const safeTitle = (s.title || 'session').trim().replace(/[/\\:*?"<>|\s,;]+/g, '_').replace(/^_+|_+$/g, '') || 'session';
    const count = s.attendee_count !== undefined ? s.attendee_count : 0;

    cardHtml += `<div class="notion-card" style="display: flex; justify-content: space-between; align-items: center; padding: 12px 14px; gap: 12px; border: 1px solid var(--md-sys-color-outline-variant); border-radius: 8px; background: var(--md-sys-color-surface); margin-bottom: 0;">
      <div style="flex: 1; min-width: 0;">
        <div style="display: flex; align-items: center; gap: 8px; margin-bottom: 4px; flex-wrap: wrap;">
          <strong style="font-size: 0.9375rem; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; max-width: 180px;">${window.escapeHtml(s.title || 'Session')}</strong>
          ${modeBadge}
          <span class="badge badge-done">${count} Attendees</span>
        </div>
        <div class="mono-txt" style="font-size: 0.8125rem; color: var(--md-sys-color-on-surface-variant);">
          ${window.escapeHtml(s.date)} • ${window.formatTime12h(s.save_time)}
        </div>
      </div>
      <div style="display: inline-flex; gap: 6px; align-items: center; flex-shrink: 0; flex-wrap: wrap;">
        <button class="btn btn-primary btn-sm" onclick="resumeSessionFromList('${window.escapeHtml(s.id)}')">Continue</button>
        <button class="btn btn-tonal btn-sm" onclick="viewSessionDetail('${window.escapeHtml(s.id)}')">View</button>
        <a href="/api/history/export.csv?id=${encodeURIComponent(s.id)}" download="${window.escapeHtml(safeTitle)}.csv" class="btn btn-outlined btn-sm">CSV</a>
        <button class="icon-btn" onclick="deleteHistorySession('${window.escapeHtml(s.id)}')" title="Delete" style="color: var(--md-sys-color-error); width: 28px; height: 28px;">
          <svg class="icon-sm" viewBox="0 0 24 24"><path d="M6 19c0 1.1.9 2 2 2h8c1.1 0 2-.9 2-2V7H6v12zM19 4h-3.5l-1-1h-5l-1 1H5v2h14V4z"/></svg>
        </button>
      </div>
    </div>`;

    tableHtml += `<tr>
      <td>${idx + 1}</td>
      <td><strong>${window.escapeHtml(s.title || 'Session')}</strong></td>
      <td>${modeBadge}</td>
      <td class="mono-txt" style="font-size: 0.8125rem;">${window.escapeHtml(s.date)} ${window.formatTime12h(s.save_time)}</td>
      <td><span class="badge badge-done">${count} Attendees</span></td>
      <td style="text-align: center;">
        <div style="display: inline-flex; gap: 4px; flex-wrap: wrap; justify-content: center;">
          <button class="btn btn-primary btn-sm" onclick="resumeSessionFromList('${window.escapeHtml(s.id)}')">Continue</button>
          <button class="btn btn-tonal btn-sm" onclick="viewSessionDetail('${window.escapeHtml(s.id)}')">View</button>
          <a href="/api/history/export.csv?id=${encodeURIComponent(s.id)}" download="${window.escapeHtml(safeTitle)}.csv" class="btn btn-outlined btn-sm">CSV</a>
          <button class="icon-btn" onclick="deleteHistorySession('${window.escapeHtml(s.id)}')" title="Delete" style="color: var(--md-sys-color-error); width: 28px; height: 28px;">
            <svg class="icon-sm" viewBox="0 0 24 24"><path d="M6 19c0 1.1.9 2 2 2h8c1.1 0 2-.9 2-2V7H6v12zM19 4h-3.5l-1-1h-5l-1 1H5v2h14V4z"/></svg>
          </button>
        </div>
      </td>
    </tr>`;
  });

  if (container) container.innerHTML = cardHtml;
  if (tbody) tbody.innerHTML = tableHtml;
};

window.filterHistoryList = function() {
  const searchEl = document.getElementById('historySearchInput');
  const q = searchEl ? searchEl.value.toLowerCase().trim() : '';
  if (!q) { window.renderHistoryList(allHistoryData); return; }
  const filtered = allHistoryData.filter(s => 
    (s.title && String(s.title).toLowerCase().includes(q)) || 
    (s.date && String(s.date).toLowerCase().includes(q)) || 
    (s.type && String(s.type).toLowerCase().includes(q))
  );
  window.renderHistoryList(filtered);
};

window.viewSessionDetail = async function(id) {
  try {
    const res = await fetch('/api/history/session?id=' + encodeURIComponent(id));
    if (!res.ok) throw new Error('HTTP ' + res.status);
    const data = await res.json();
    if (!data || !data.id) { alert('Session not found'); return; }
    currentDetailSession = data;
    currentDetailAttendees = data.records || [];

    document.getElementById('historyListView').style.display = 'none';
    document.getElementById('historyDetailView').style.display = 'flex';
    const dlgTitle = document.getElementById('notionPaneTitle');
    if (dlgTitle) dlgTitle.innerText = data.title;
    document.getElementById('historyDetailTitle').innerText = data.title;
    document.getElementById('historyDetailMeta').innerText = `Date: ${data.date} • Saved: ${window.formatTime12h(data.save_time)} • Mode: ${data.type}`;
    document.getElementById('historyDetailCountBadge').innerText = `${currentDetailAttendees.length} Attendees`;
    document.getElementById('historyExportCsvBtn').href = `/api/history/export.csv?id=${encodeURIComponent(id)}`;
    const safeDetailTitle = (data.title || 'session').trim().replace(/[/\\:*?"<>|\s,;]+/g, '_').replace(/^_+|_+$/g, '') || 'session';
    document.getElementById('historyExportCsvBtn').setAttribute('download', safeDetailTitle + '.csv');

    window.renderHistoryDetailTable(currentDetailAttendees);
  } catch(e) {
    alert('Error loading session details: ' + (e.message || e));
  }
};

window.backToHistoryList = function() {
  const detail = document.getElementById('historyDetailView');
  const list = document.getElementById('historyListView');
  if (detail) detail.style.display = 'none';
  if (list) list.style.display = 'flex';
  const dlgTitle = document.getElementById('notionPaneTitle');
  if (dlgTitle) dlgTitle.innerText = 'Session History';
  currentDetailSession = null;
};

window.writeClipboardTable = async function(tsv, html, btnId) {
  try {
    if (navigator.clipboard && window.ClipboardItem) {
      const textBlob = new Blob([tsv], { type: 'text/plain' });
      const htmlBlob = new Blob([html], { type: 'text/html' });
      await navigator.clipboard.write([
        new ClipboardItem({
          'text/plain': textBlob,
          'text/html': htmlBlob
        })
      ]);
    } else if (navigator.clipboard) {
      await navigator.clipboard.writeText(tsv);
    } else {
      const ta = document.createElement('textarea');
      ta.value = tsv;
      ta.style.position = 'fixed';
      ta.style.opacity = '0';
      document.body.appendChild(ta);
      ta.select();
      document.execCommand('copy');
      document.body.removeChild(ta);
    }

    const btn = document.getElementById(btnId);
    if (btn) {
      const origHtml = btn.innerHTML;
      const textSpan = btn.querySelector('.log-btn-text');
      if (textSpan) {
        textSpan.innerText = 'Copied!';
      } else {
        btn.innerHTML = '<svg class="icon-sm" viewBox="0 0 24 24"><path d="M9 16.17L4.83 12l-1.42 1.41L9 19 21 7l-1.41-1.41z"/></svg>';
      }
      btn.style.color = 'var(--md-sys-color-success)';
      btn.style.borderColor = 'var(--md-sys-color-success)';
      setTimeout(() => {
        btn.innerHTML = origHtml;
        btn.style.color = '';
        btn.style.borderColor = '';
      }, 1800);
    } else {
      alert('Table copied to clipboard! Ready to paste into Google Sheets or Excel.');
    }
  } catch(e) {
    try {
      const ta = document.createElement('textarea');
      ta.value = tsv;
      ta.style.position = 'fixed';
      ta.style.opacity = '0';
      document.body.appendChild(ta);
      ta.select();
      document.execCommand('copy');
      document.body.removeChild(ta);
      alert('Table copied to clipboard! Ready to paste into Google Sheets.');
    } catch(err) {
      alert('Could not copy table: ' + e);
    }
  }
};

window.copySessionTable = async function() {
  const records = allAttendeesData || [];
  if (!records || records.length === 0) {
    alert('No attendance records to copy in current session.');
    return;
  }

  let tsv = '#\tAttendee Name\tID Number\tTime IN\tTime OUT\tStatus\n';
  let html = '<table><thead><tr><th>#</th><th>Attendee Name</th><th>ID Number</th><th>Time IN</th><th>Time OUT</th><th>Status</th></tr></thead><tbody>';

  records.forEach((r, idx) => {
    const hasIn = r.time_in && r.time_in !== '--' && r.time_in !== '';
    const hasOut = r.time_out && r.time_out !== '--' && r.time_out !== '';
    let status = 'Present';
    if (hasIn && hasOut) status = 'Completed';
    else if (hasIn) status = 'In';
    else if (hasOut) status = 'Out';

    const num = idx + 1;
    const name = r.name || 'Unknown';
    const id = r.student_id || '';
    const inTime = window.formatTime12h(r.time_in);
    const outTime = window.formatTime12h(r.time_out);

    tsv += `${num}\t${name}\t${id}\t${inTime}\t${outTime}\t${status}\n`;
    html += `<tr><td>${num}</td><td>${window.escapeHtml(name)}</td><td>${window.escapeHtml(id)}</td><td>${inTime}</td><td>${outTime}</td><td>${status}</td></tr>`;
  });
  html += '</tbody></table>';

  await window.writeClipboardTable(tsv, html, 'copyTableBtn');
};

window.copyHistoryDetailTable = async function() {
  const records = currentDetailAttendees || [];
  if (!records || records.length === 0) {
    alert('No records to copy in this session.');
    return;
  }

  let tsv = '#\tAttendee Name\tID Number\tTime IN\tTime OUT\tStatus\n';
  let html = '<table><thead><tr><th>#</th><th>Attendee Name</th><th>ID Number</th><th>Time IN</th><th>Time OUT</th><th>Status</th></tr></thead><tbody>';

  records.forEach((r, idx) => {
    const num = idx + 1;
    const name = r.name || 'Unknown';
    const id = r.student_id || r.id || '';
    const inTime = window.formatTime12h(r.time_in || r.in_time);
    const outTime = window.formatTime12h(r.time_out || r.out_time);
    const status = r.status || 'Present';

    tsv += `${num}\t${name}\t${id}\t${inTime}\t${outTime}\t${status}\n`;
    html += `<tr><td>${num}</td><td>${window.escapeHtml(name)}</td><td>${window.escapeHtml(id)}</td><td>${inTime}</td><td>${outTime}</td><td>${status}</td></tr>`;
  });
  html += '</tbody></table>';

  await window.writeClipboardTable(tsv, html, 'historyCopyTableBtn');
};

window.resumeSessionFromList = async function(id) {
  const sess = (allHistoryData || []).find(s => s.id === id);
  const title = sess ? sess.title : 'Selected Session';
  const count = sess ? (sess.attendee_count || 0) : 0;
  if (!confirm(`Continue saved session "${title}"?\n\nThis will restore its ${count} attendee records into the active scanner session so you can continue scanning.`)) {
    return;
  }
  try {
    const res = await fetch('/api/history/resume?id=' + encodeURIComponent(id), { method: 'POST' });
    const data = await res.json();
    if (!data.success) {
      alert('Error continuing session: ' + (data.message || 'Unknown error'));
      return;
    }

    // Close settings dialog & history
    window.closeSettingsModal();
    window.closeHistoryModal();

    // Populate active log immediately from restored response
    window.setSessionSavedState(false);
    if (data.records && Array.isArray(data.records)) {
      allAttendeesData = data.records;
      window.renderAttendanceTable(allAttendeesData);
      window.updateAttendanceStats(allAttendeesData);
    }

    // Update active session title & mode chips immediately
    if (data.title) {
      const titleEl = document.getElementById('activeTitleLabel');
      if (titleEl) {
        titleEl.innerText = data.title;
        titleEl.title = data.title;
      }
      const liveCsvBtn = document.getElementById('btnExportLiveCsv');
      if (liveCsvBtn) {
        const safeLiveTitle = (data.title || 'attendance').trim().replace(/[/\\:*?"<>|\s,;]+/g, '_').replace(/^_+|_+$/g, '') || 'attendance';
        liveCsvBtn.setAttribute('download', safeLiveTitle + '.csv');
      }
    }
    if (data.type) {
      const modeChip = document.getElementById('activeModeChip');
      if (modeChip) {
        modeChip.innerText = data.type;
        modeChip.className = (data.type === 'Exit') ? 'badge badge-out' : 'badge badge-in';
      }
      const isExit = (data.type === 'Exit');
      const rfidModeEntrance = document.getElementById('rfidModeEntrance');
      const rfidModeExit = document.getElementById('rfidModeExit');
      if (rfidModeEntrance && rfidModeExit) {
        rfidModeEntrance.classList.toggle('active-in', !isExit);
        rfidModeExit.classList.toggle('active-out', isExit);
      }
      const idleModePill = document.getElementById('idleModePill');
      if (idleModePill) {
        idleModePill.innerText = `● ${data.type.toUpperCase()} MODE (${isExit ? 'OUT' : 'IN'})`;
      }
    }

    // Switch to console view immediately
    const entranceScreen = document.getElementById('entranceScreen');
    const consoleScreen = document.getElementById('consoleScreen');
    if (entranceScreen) entranceScreen.style.display = 'none';
    if (consoleScreen) consoleScreen.style.display = 'flex';

    // Synchronize full state from backend
    await window.loadAttendanceLog();
    await window.pollStatus();
  } catch(e) {
    alert('Network error: ' + (e.message || e));
  }
};

window.resumeCurrentDetailSession = async function() {
  if (!currentDetailSession) return;
  await window.resumeSessionFromList(currentDetailSession.id);
};

window.renderHistoryDetailTable = function(records) {
  const listEl = document.getElementById('historyDetailListContainer');
  const tbody = document.getElementById('historyDetailTableBody');
  if (!records || records.length === 0) {
    if (listEl) listEl.innerHTML = '<div class="empty-state" style="text-align: center; padding: 24px; color: var(--md-sys-color-on-surface-variant);">No attendees recorded in this session.</div>';
    if (tbody) tbody.innerHTML = '<tr><td colspan="7" style="text-align: center; color: var(--md-sys-color-on-surface-variant); padding: 24px;">No attendees recorded in this session.</td></tr>';
    return;
  }
  let cardHtml = '';
  let tableHtml = '';
  records.forEach((r, idx) => {
    const sId = r.student_id ? window.escapeHtml(r.student_id) : '';
    const idBadge = sId ? `<span class="badge badge-in">${sId}</span>` : '';
    const safeName = window.escapeHtml(r.name || 'Unknown');
    const safeUid = window.escapeHtml(r.uid || '--');
    const statusText = window.escapeHtml(r.status || 'Present');

    cardHtml += `<div class="notion-card" style="display: flex; justify-content: space-between; align-items: center; padding: 12px 14px; gap: 12px; border: 1px solid var(--md-sys-color-outline-variant); border-radius: 8px; background: var(--md-sys-color-surface); margin-bottom: 0;">
      <div style="flex: 1; min-width: 0;">
        <div style="display: flex; align-items: center; gap: 8px; margin-bottom: 4px; flex-wrap: wrap;">
          <strong style="font-size: 0.9375rem;">${safeName}</strong>
          ${idBadge}
          <span class="badge badge-done">${statusText}</span>
        </div>
        <div class="mono-txt" style="font-size: 0.8125rem; color: var(--md-sys-color-on-surface-variant);">
          ${safeUid} • In: ${window.formatTime12h(r.time_in)} ${r.time_out && r.time_out !== '--' ? '• Out: ' + window.formatTime12h(r.time_out) : ''}
        </div>
      </div>
    </div>`;

    tableHtml += `<tr>
      <td>${idx + 1}</td>
      <td><strong>${safeName}</strong></td>
      <td>${sId || '<span style="color: var(--md-sys-color-outline);">&mdash;</span>'}</td>
      <td><span class="mono-txt">${safeUid}</span></td>
      <td class="mono-txt">${window.formatTime12h(r.time_in)}</td>
      <td class="mono-txt">${window.formatTime12h(r.time_out)}</td>
      <td><span class="badge badge-done">${statusText}</span></td>
    </tr>`;
  });

  if (listEl) listEl.innerHTML = cardHtml;
  if (tbody) tbody.innerHTML = tableHtml;
};

window.filterHistoryDetailTable = function() {
  const searchEl = document.getElementById('historyDetailSearch');
  const q = searchEl ? searchEl.value.toLowerCase().trim() : '';
  if (!q) { window.renderHistoryDetailTable(currentDetailAttendees); return; }
  const filtered = currentDetailAttendees.filter(r => 
    (r.name && String(r.name).toLowerCase().includes(q)) || 
    (r.student_id && String(r.student_id).toLowerCase().includes(q)) || 
    (r.uid && String(r.uid).toLowerCase().includes(q))
  );
  window.renderHistoryDetailTable(filtered);
};

window.deleteHistorySession = async function(id) {
  if (!confirm('Delete this saved session from history?')) return;
  try {
    const res = await fetch('/api/history/delete?id=' + encodeURIComponent(id), { method: 'POST' });
    const data = await res.json();
    if (data.success) window.loadSessionHistory();
    else alert('Error: ' + data.message);
  } catch(e) {
    alert('Error: ' + e);
  }
};

window.deleteCurrentDetailSession = async function() {
  if (!currentDetailSession) return;
  await window.deleteHistorySession(currentDetailSession.id);
  window.backToHistoryList();
};

window.clearAllHistory = async function() {
  if (!confirm('Are you sure you want to delete ALL saved session history?')) return;
  if (!confirm('Confirm again: This cannot be undone.')) return;
  try {
    const res = await fetch('/api/history/clear', { method: 'POST' });
    const data = await res.json();
    window.loadSessionHistory();
  } catch(e) {
    alert('Error: ' + e);
  }
};

let isSessionSaved = false;

window.setSessionSavedState = function(isSaved) {
  isSessionSaved = isSaved;
  const btn = document.getElementById('btnSessionLifecycle');
  const icon = document.getElementById('lifecycleBtnIcon');
  const txt = document.getElementById('lifecycleBtnText');
  if (!btn || !icon || !txt) return;

  if (isSaved) {
    btn.className = 'btn btn-danger-tonal btn-sm';
    btn.title = 'Session saved! Click to finish and end session';
    icon.innerHTML = '<path d="M6 6h12v12H6z"/>';
    txt.innerHTML = '<span class="btn-lifecycle-text-full">End Session</span><span class="btn-lifecycle-text-short">End</span>';
  } else {
    btn.className = 'btn btn-tonal btn-sm';
    btn.title = 'Save active session to history';
    icon.innerHTML = '<path d="M17 3H5c-1.11 0-2 .9-2 2v14c0 1.1.89 2 2 2h14c1.1 0 2-.9 2-2V7l-4-4zm2 16H5V5h11.17L19 7.83V19zm-7-7c-1.66 0-3 1.34-3 3s1.34 3 3 3 3-1.34 3-3-1.34-3-3-3zM6 6h9v4H6z"/>';
    txt.innerHTML = '<span class="btn-lifecycle-text-full">Save Session</span><span class="btn-lifecycle-text-short">Save</span>';
  }
};

window.saveActiveSession = async function() {
  try {
    const res = await fetch('/api/session/save', { method: 'POST' });
    if (!res.ok) {
      const errTxt = await res.text();
      throw new Error(`Server returned HTTP ${res.status}: ${errTxt}`);
    }
    const data = await res.json();
    if (data.success) {
      alert(data.message || 'Session saved to history!');
      window.setSessionSavedState(true);
      window.loadSessionHistory();
    } else {
      if (confirm((data.message || 'Could not save session.') + '\n\nDo you want to end the session now anyway?')) {
        await window.stopSession();
      }
    }
  } catch(e) {
    alert('Failed to save session: ' + (e.message || e));
  }
};

window.handleCombinedSessionAction = async function() {
  if (!isSessionSaved) {
    await window.saveActiveSession();
  } else {
    await window.stopSession();
  }
};

window.switchNotionTab = function(paneId) {
  if (paneId === 'set-register' || paneId === 'set-reg') paneId = 'notion-reg-single';
  else if (paneId === 'set-batch') paneId = 'notion-reg-batch';
  else if (paneId === 'set-cards') paneId = 'notion-cards';
  else if (paneId === 'set-history') paneId = 'notion-history';
  else if (paneId === 'set-prefs') paneId = 'notion-rules';

  document.querySelectorAll('.notion-pane').forEach(el => el.classList.remove('active'));
  document.querySelectorAll('.notion-nav-item').forEach(el => el.classList.remove('active'));

  const target = document.getElementById(paneId);
  if (target) target.classList.add('active');

  const navBtn = Array.from(document.querySelectorAll('.notion-nav-item')).find(b => {
    const oc = b.getAttribute('onclick');
    return oc && oc.includes(paneId);
  });
  if (navBtn) navBtn.classList.add('active');

  const titleEl = document.getElementById('notionPaneTitle');
  const subEl = document.getElementById('notionPaneSubtitle');
  if (target) {
    if (titleEl) titleEl.innerText = target.getAttribute('data-title') || 'Settings';
    if (subEl) subEl.innerText = target.getAttribute('data-subtitle') || '';
  }

  const dialog = document.getElementById('notionSettingsDialog');
  if (dialog) dialog.classList.add('mobile-detail-active');

  if (paneId === 'notion-cards') {
    window.loadCardsDatabase();
  } else if (paneId === 'notion-history') {
    window.backToHistoryList();
    window.loadSessionHistory();
  } else if (paneId === 'notion-reg-batch') {
    if (!isBatchRunning) {
      window.resetBatchInputView();
    } else {
      const inView = document.getElementById('batchInputView');
      const scView = document.getElementById('batchScanningView');
      const dnView = document.getElementById('batchDoneView');
      if (inView) inView.style.display = 'none';
      if (scView) scView.style.display = 'flex';
      if (dnView) dnView.style.display = 'none';
      window.renderBatchQueue();
    }
  }
  window.notifyModalState();
};

window.closeNotionMobileDetail = function() {
  const dialog = document.getElementById('notionSettingsDialog');
  if (dialog) dialog.classList.remove('mobile-detail-active');
};

window.openSettingsModal = function(targetTab) {
  const modal = document.getElementById('settingsModal');
  const dialog = document.getElementById('notionSettingsDialog');
  document.body.classList.add('modal-open');
  if (modal) modal.classList.add('active');
  if (dialog && !targetTab) {
    dialog.classList.remove('mobile-detail-active');
  }
  if (targetTab) {
    window.switchNotionTab(targetTab);
  } else {
    setTimeout(() => window.loadCardsDatabase(), 80);
    setTimeout(() => window.loadSessionHistory(), 280);
  }
  window.notifyModalState();
};

window.closeSettingsModal = function() {
  const modal = document.getElementById('settingsModal');
  document.body.classList.remove('modal-open');
  if (modal) modal.classList.remove('active');
  window.notifyModalState();
};

window.openHistoryModal = function() {
  window.openSettingsModal('notion-history');
};

window.closeHistoryModal = function() {
  window.closeSettingsModal();
};

window.switchSettingsTab = function(paneId) {
  window.switchNotionTab(paneId);
};

let lcdBrightDebounceTimer = null;
window.onLcdBrightnessInput = function(val) {
  const pct = parseInt(val);
  const lbl = document.getElementById('lcdBrightnessLabel');
  if (lbl) lbl.innerText = pct + '%';

  clearTimeout(lcdBrightDebounceTimer);
  lcdBrightDebounceTimer = setTimeout(() => {
    fetch('/api/lcd/brightness?pct=' + pct, { method: 'POST' }).catch(()=>{});
  }, 40);
};

let rgbBrightDebounceTimer = null;
window.onRgbBrightnessInput = function(val) {
  const pct = parseInt(val);
  const lbl = document.getElementById('rgbBrightnessLabel');
  if (lbl) lbl.innerText = pct + '%';

  clearTimeout(rgbBrightDebounceTimer);
  rgbBrightDebounceTimer = setTimeout(() => {
    fetch('/api/rgb/brightness?pct=' + pct, { method: 'POST' }).catch(()=>{});
  }, 40);
};

window.updateSettings = async function() {
  const reqReg = document.getElementById('requireRegToggle')?.checked ? '1' : '0';
  const debounceVal = document.getElementById('cardDebounceSelect')?.value || '1500';
  const lcdTimeoutVal = document.getElementById('lcdTimeoutSelect')?.value || '45000';
  const wifiGuideVal = document.getElementById('lcdWifiGuideToggle')?.checked ? '1' : '0';
  const lcdBrightPct = document.getElementById('lcdBrightnessSlider')?.value || '100';
  const rgbBrightPct = document.getElementById('rgbBrightnessSlider')?.value || '100';
  const lcdDimDuration = document.getElementById('lcdDimDurationSelect')?.value || '10000';

  const formData = new URLSearchParams();
  formData.append('require_registration', reqReg);
  formData.append('card_debounce', debounceVal);
  formData.append('lcd_timeout', lcdTimeoutVal);
  formData.append('lcd_wifi_guide', wifiGuideVal);
  formData.append('lcd_brightness_pct', lcdBrightPct);
  formData.append('rgb_brightness_pct', rgbBrightPct);
  formData.append('lcd_dim_duration', lcdDimDuration);

  try {
    await fetch('/api/settings', { method: 'POST', body: formData });
  } catch(e) {
    console.error('Settings update error:', e);
  }
};

let isWifiPassVisible = false;
window.toggleWifiPassVisibility = function() {
  isWifiPassVisible = !isWifiPassVisible;
  const el = document.getElementById('notionActiveWifiPass');
  const eyeShow = document.getElementById('eyeIconShow');
  const eyeHide = document.getElementById('eyeIconHide');
  if (!el) return;
  if (isWifiPassVisible) {
    el.innerText = window._currentWifiPass || '(no password)';
    if (eyeShow) eyeShow.style.display = 'none';
    if (eyeHide) eyeHide.style.display = 'inline';
  } else {
    el.innerText = (window._currentWifiPass && window._currentWifiPass.length > 0) ? '••••••••' : '(no password)';
    if (eyeShow) eyeShow.style.display = 'inline';
    if (eyeHide) eyeHide.style.display = 'none';
  }
};

window.submitChangeWifiPassword = async function() {
  const input = document.getElementById('newWifiPassInput');
  if (!input) return;
  const newPass = input.value.trim();
  if (newPass.length > 0 && newPass.length < 8) {
    alert('Wi-Fi password must be at least 8 characters for WPA2 (or leave empty for an open network).');
    input.focus();
    return;
  }

  const confirmMsg = newPass.length === 0
    ? 'Are you sure you want to change to an OPEN Wi-Fi network (no password)?\n\nThis change will take effect on next boot, and the LCD guide will be automatically enabled.'
    : `Change Wi-Fi password to "${newPass}"?\n\nThis change will take effect on NEXT BOOT, and the 16x2 LCD password display will be automatically enabled.`;

  if (!confirm(confirmMsg)) return;

  try {
    const fd = new URLSearchParams();
    fd.append('password', newPass);
    const res = await fetch('/api/wifi/password', { method: 'POST', body: fd });
    const data = await res.json();
    if (data.success) {
      input.value = '';
      const gToggle = document.getElementById('lcdWifiGuideToggle');
      if (gToggle) gToggle.checked = true;

      const rebootNow = confirm(data.message + '\n\nWould you like to reboot the ESP32 now to apply the new Wi-Fi password immediately?');
      if (rebootNow) {
        await window.triggerSystemReboot();
      } else {
        window.pollStatus();
      }
    } else {
      alert('Error: ' + data.message);
    }
  } catch(e) {
    alert('Network error: ' + (e.message || e));
  }
};

window.triggerLcdSleep = async function() {
  const btn = document.getElementById('btnLcdOff');
  const origText = btn ? btn.innerText : '';
  try {
    await fetch('/api/lcd/sleep', { method: 'POST' });
    if (btn) { btn.innerText = 'Screen Off ✓'; setTimeout(() => { btn.innerText = origText; }, 1500); }
  } catch(e) { if (btn) btn.innerText = 'Failed'; }
};

window.triggerLcdWake = async function() {
  const btn = document.getElementById('btnLcdWake');
  const origText = btn ? btn.innerText : '';
  try {
    await fetch('/api/lcd/wake', { method: 'POST' });
    if (btn) { btn.innerText = 'Awakened ✓'; setTimeout(() => { btn.innerText = origText; }, 1500); }
  } catch(e) { if (btn) btn.innerText = 'Failed'; }
};

window.triggerLcdTest = async function() {
  const btn = document.getElementById('btnLcdTest');
  const origText = btn ? btn.innerText : '';
  try {
    if (btn) btn.innerText = 'Testing...';
    await fetch('/api/lcd/test', { method: 'POST' });
    if (btn) { btn.innerText = 'Pulsed ✓'; setTimeout(() => { btn.innerText = origText; }, 2000); }
  } catch(e) { if (btn) btn.innerText = 'Failed'; }
};

window.triggerRgbTest = async function(mode) {
  try {
    const res = await fetch('/api/rgb/test?mode=' + encodeURIComponent(mode), { method: 'POST' });
    const d = await res.json();
    alert(d.message || 'RGB test triggered: ' + mode);
  } catch(e) { alert('Error: ' + e); }
};

window.triggerSystemReboot = async function() {
  if (!confirm('Reboot ESP32 now? Connected Wi-Fi clients will temporarily disconnect.')) return;
  try {
    await fetch('/api/system/reboot', { method: 'POST' });
    alert('ESP32 is restarting... Please reconnect to the Wi-Fi hotspot in 5 seconds.');
    window.closeSettingsModal();
  } catch(e) {
    alert('Reboot signal sent. Reconnecting...');
    window.closeSettingsModal();
  }
};

window.setCustomDeviceTime = async function() {
  const val = document.getElementById('notionCustomTimeInput')?.value;
  if (!val) { alert('Please pick a date and time.'); return; }
  const d = new Date(val);
  const epoch = Math.floor(d.getTime() / 1000);
  const tzOffset = d.getTimezoneOffset();
  try {
    const res = await window.sendTimeSync(epoch, tzOffset);
    if (res.success) {
      manualTimeSetByUser = true;
      window.pollStatus();
      alert('Device time updated: ' + res.time);
    } else {
      alert('Error setting time: ' + res.message);
    }
  } catch(e) { alert('Error: ' + e); }
};

window.handleImportFileSelect = function(event) {
  const file = event.target.files[0];
  if (!file) return;
  const reader = new FileReader();
  reader.onload = function(e) {
    const input = document.getElementById('importCsvTextarea') || document.getElementById('importCardsInput');
    if (input) input.value = e.target.result;
    const msg = document.getElementById('importFeedbackBox') || document.getElementById('importStatusMsg');
    if (msg) {
      msg.style.display = 'block';
      msg.style.color = 'var(--md-sys-color-primary)';
      msg.innerText = `Loaded file: ${file.name} (${file.size} bytes). Ready to import or queue for batch.`;
    }
  };
  reader.readAsText(file);
};

window.submitImportCards = async function() {
  const input = document.getElementById('importCsvTextarea') || document.getElementById('importCardsInput');
  const data = input ? input.value.trim() : '';
  const statusEl = document.getElementById('importFeedbackBox') || document.getElementById('importStatusMsg');
  if (!data) {
    alert('Please enter or upload CSV card data.');
    return;
  }
  const formData = new URLSearchParams();
  formData.append('data', data);
  try {
    const res = await fetch('/api/cards/import', { method: 'POST', body: formData });
    const json = await res.json();
    if (statusEl) {
      statusEl.style.display = 'block';
      statusEl.style.color = json.success ? 'var(--md-sys-color-primary)' : 'var(--md-sys-color-error)';
      statusEl.innerText = json.message || 'Import completed.';
    }
    alert(json.message || 'Import completed.');
    window.loadCardsDatabase();
  } catch(e) {
    alert('Import error: ' + e);
  }
};

window.sendImportToBatch = function() {
  const input = document.getElementById('importCsvTextarea') || document.getElementById('importCardsInput');
  const data = input ? input.value.trim() : '';
  if (!data) { alert('Please paste names into the import box first.'); return; }
  const batchInput = document.getElementById('batchNamesInput');
  if (batchInput) batchInput.value = data;
  window.switchNotionTab('notion-reg-batch');
  window.updateBatchCount();
};

window.startRegistration = async function() {
  const first = document.getElementById('regFirstName')?.value.trim() || '';
  const mi = document.getElementById('regMiddleInitial')?.value.trim() || '';
  const last = document.getElementById('regLastName')?.value.trim() || '';
  const idNum = document.getElementById('regOwnerId')?.value.trim() || '';

  let name = '';
  if (first && last) {
    const miClean = mi ? (mi.endsWith('.') ? mi : mi + '.') : '';
    name = `${last}, ${first}${miClean ? ' ' + miClean : ''}`;
  } else if (document.getElementById('regOwnerName')?.value.trim()) {
    name = document.getElementById('regOwnerName').value.trim();
  }

  if (!first && !name) {
    alert('Please enter attendee First Name.');
    document.getElementById('regFirstName')?.focus();
    return;
  }
  if (!last && !name) {
    alert('Please enter attendee Last Name.');
    document.getElementById('regLastName')?.focus();
    return;
  }
  if (!name) name = `${last}, ${first}`;

  const hidden = document.getElementById('regOwnerName');
  if (hidden) hidden.value = name;

  const inlineStatus = document.getElementById('regStatusCard');
  if (inlineStatus) {
    inlineStatus.style.display = 'block';
    inlineStatus.style.background = 'var(--md-sys-color-surface-container-high)';
    inlineStatus.style.color = 'var(--md-sys-color-on-surface)';
    inlineStatus.style.border = '1px solid var(--md-sys-color-primary)';
    inlineStatus.innerHTML = '<div style="display:flex;align-items:center;gap:8px;">' +
      '<svg class="icon-sm" style="color:var(--md-sys-color-primary);flex-shrink:0;" viewBox="0 0 24 24"><path fill="currentColor" d="M20 4H4c-1.11 0-1.99.89-1.99 2L2 18c0 1.11.89 2 2 2h16c1.11 0 2-.89 2-2V6c0-1.11-.89-2-2-2zm0 14H4v-6h16v6zm0-10H4V6h16v2z"/></svg>' +
      '<div><b>Ready for Scan</b><br>Present RFID card to the reader for <b>' + window.escapeHtml(name) + '</b>...</div>' +
      '</div>';
  }

  const regParams = new URLSearchParams();
  regParams.append('name', name);
  if (idNum) regParams.append('student_id', idNum);
  window._activeRegParams = regParams;
  window._activeRegCandidate = { name, idNum };

  try {
    const res = await fetch('/api/register/start?' + regParams.toString(), { method: 'POST', body: regParams });
    const data = await res.json();
    if (data.success) {
      const ownerEl = document.getElementById('regModalOwnerName');
      if (ownerEl) ownerEl.innerText = name + (idNum ? ' (' + idNum + ')' : '');

      const titleEl = document.getElementById('regModalTitle');
      if (titleEl) titleEl.innerText = 'Register Card';

      const iconWrap = document.getElementById('regModalIconWrap');
      if (iconWrap) {
        iconWrap.style.background = 'var(--md-sys-color-primary-container)';
        iconWrap.style.color = 'var(--md-sys-color-primary)';
      }
      const iconEl = document.getElementById('regModalIcon');
      if (iconEl) {
        iconEl.innerHTML = '<path fill="currentColor" d="M20 4H4c-1.11 0-1.99.89-1.99 2L2 18c0 1.11.89 2 2 2h16c1.11 0 2-.89 2-2V6c0-1.11-.89-2-2-2zm0 14H4v-6h16v6zm0-10H4V6h16v2z"/>';
      }

      const statusEl = document.getElementById('regModalStatusText');
      if (statusEl) {
        statusEl.innerText = 'Present card to the reader...';
        statusEl.style.color = 'var(--md-sys-color-on-surface)';
      }

      const progEl = document.getElementById('regCountdownProgress');
      if (progEl) progEl.style.display = 'block';
      const barEl = document.getElementById('regCountdownBar');
      if (barEl) barEl.style.width = '100%';

      const successAct = document.getElementById('regModalSuccessActions');
      if (successAct) successAct.style.display = 'none';

      const cancelBtn = document.getElementById('regModalCancelBtn');
      if (cancelBtn) cancelBtn.style.display = 'block';

      document.getElementById('regModal')?.classList.add('active');
      window.startRegPolling();
    } else {
      alert('Error: ' + data.message);
    }
  } catch(e) {
    alert('Network error: ' + e);
  }
};

window.startRegPolling = function() {
  if (regPollInterval) clearInterval(regPollInterval);
  isPollingReg = true;
  regPollInterval = setInterval(async () => {
    try {
      const res = await fetch('/api/register/status');
      const data = await res.json();

      const candName = window._activeRegCandidate ? window._activeRegCandidate.name : '';
      const iconWrap = document.getElementById('regModalIconWrap');
      const iconEl = document.getElementById('regModalIcon');
      const titleEl = document.getElementById('regModalTitle');
      const statusText = document.getElementById('regModalStatusText');
      const progEl = document.getElementById('regCountdownProgress');
      const successAct = document.getElementById('regModalSuccessActions');
      const cancelBtn = document.getElementById('regModalCancelBtn');
      const inlineStatus = document.getElementById('regStatusCard');

      if (data.state === 'waiting') {
        const pct = Math.max(0, Math.min(100, (data.remaining_ms / 30000) * 100));
        const barEl = document.getElementById('regCountdownBar');
        if (barEl) barEl.style.width = pct + '%';
      } else if (data.state === 'success') {
        // Successfully assigned new card
        window.playAudioTone('success');
        if (iconWrap) {
          iconWrap.style.background = 'var(--md-sys-color-success-container)';
          iconWrap.style.color = 'var(--md-sys-color-success)';
        }
        if (iconEl) {
          iconEl.innerHTML = '<path fill="currentColor" d="M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm-2 15l-5-5 1.41-1.41L10 14.17l7.59-7.59L19 8l-9 9z"/>';
        }
        if (titleEl) titleEl.innerText = 'Card Registered';
        if (statusText) {
          statusText.innerHTML = '<div>UID <code>' + window.escapeHtml(data.result_uid) + '</code> is now registered to <b>' + window.escapeHtml(candName) + '</b>.</div>';
          statusText.style.color = 'var(--md-sys-color-on-surface)';
        }
        if (progEl) progEl.style.display = 'none';
        if (cancelBtn) cancelBtn.style.display = 'none';
        if (successAct) successAct.style.display = 'flex';

        if (inlineStatus) {
          inlineStatus.style.display = 'block';
          inlineStatus.style.background = 'var(--md-sys-color-success-container)';
          inlineStatus.style.color = 'var(--md-sys-color-on-success-container)';
          inlineStatus.style.border = '1px solid var(--md-sys-color-success)';
          inlineStatus.innerHTML = '<div style="display:flex;align-items:center;gap:8px;">' +
            '<svg class="icon-sm" style="color:var(--md-sys-color-success);flex-shrink:0;" viewBox="0 0 24 24"><path fill="currentColor" d="M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm-2 15l-5-5 1.41-1.41L10 14.17l7.59-7.59L19 8l-9 9z"/></svg>' +
            '<div><b>Card Registered</b><br>UID <code>' + window.escapeHtml(data.result_uid) + '</code> registered to <b>' + window.escapeHtml(candName) + '</b>.</div>' +
            '</div>';
        }
        window.loadCardsDatabase();
      } else if (data.state === 'same_card') {
        // Same card for same person tapped
        window.playAudioTone('success');
        if (iconWrap) {
          iconWrap.style.background = 'var(--md-sys-color-primary-container)';
          iconWrap.style.color = 'var(--md-sys-color-primary)';
        }
        if (iconEl) {
          iconEl.innerHTML = '<path fill="currentColor" d="M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm-2 15l-5-5 1.41-1.41L10 14.17l7.59-7.59L19 8l-9 9z"/>';
        }
        if (titleEl) titleEl.innerText = 'Already Registered';
        const ownerName = data.conflict_name || candName;
        if (statusText) {
          statusText.innerHTML = '<div>This card (UID: <code>' + window.escapeHtml(data.result_uid || '') + '</code>) is already registered to <b>' + window.escapeHtml(ownerName) + '</b>.</div>';
          statusText.style.color = 'var(--md-sys-color-on-surface)';
        }
        if (progEl) progEl.style.display = 'none';
        if (cancelBtn) cancelBtn.style.display = 'none';
        if (successAct) successAct.style.display = 'flex';

        if (inlineStatus) {
          inlineStatus.style.display = 'block';
          inlineStatus.style.background = 'var(--md-sys-color-surface-container-high)';
          inlineStatus.style.color = 'var(--md-sys-color-on-surface)';
          inlineStatus.style.border = '1px solid var(--md-sys-color-primary)';
          inlineStatus.innerHTML = '<div style="display:flex;align-items:center;gap:8px;">' +
            '<svg class="icon-sm" style="color:var(--md-sys-color-primary);flex-shrink:0;" viewBox="0 0 24 24"><path fill="currentColor" d="M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm-2 15l-5-5 1.41-1.41L10 14.17l7.59-7.59L19 8l-9 9z"/></svg>' +
            '<div><b>Already Registered</b><br>UID <code>' + window.escapeHtml(data.result_uid) + '</code> is already registered to <b>' + window.escapeHtml(ownerName) + '</b>.</div>' +
            '</div>';
        }
      } else if (data.state === 'conflict') {
        // Card is already registered to someone else -> Prompt to use another tag instead!
        window.playAudioTone('alert');
        const conflictOwner = data.conflict_name || 'another person';
        const conflictId = data.conflict_id ? ' (' + data.conflict_id + ')' : '';
        if (iconWrap) {
          iconWrap.style.background = 'var(--md-sys-color-error-container)';
          iconWrap.style.color = 'var(--md-sys-color-error)';
        }
        if (iconEl) {
          iconEl.innerHTML = '<path fill="currentColor" d="M1 21h22L12 2 1 21zm12-3h-2v-2h2v2zm0-4h-2v-4h2v4z"/>';
        }
        if (titleEl) titleEl.innerText = 'Card Already in Use';
        if (statusText) {
          statusText.innerHTML = '<div>Card is already registered to <b>' + window.escapeHtml(conflictOwner) + conflictId + '</b>.<br><span style="color:var(--md-sys-color-primary);font-size:0.8rem;font-weight:600;">Please use another card to register for ' + window.escapeHtml(candName) + '...</span></div>';
          statusText.style.color = 'var(--md-sys-color-error)';
        }

        if (inlineStatus) {
          inlineStatus.style.display = 'block';
          inlineStatus.style.background = 'var(--md-sys-color-error-container)';
          inlineStatus.style.color = 'var(--md-sys-color-on-error-container)';
          inlineStatus.style.border = '1px solid var(--md-sys-color-error)';
          inlineStatus.innerHTML = '<div style="display:flex;align-items:center;gap:8px;">' +
            '<svg class="icon-sm" style="color:var(--md-sys-color-error);flex-shrink:0;" viewBox="0 0 24 24"><path fill="currentColor" d="M1 21h22L12 2 1 21zm12-3h-2v-2h2v2zm0-4h-2v-4h2v4z"/></svg>' +
            '<div><b>Card Already in Use</b><br>UID <code>' + window.escapeHtml(data.result_uid) + '</code> is registered to <b>' + window.escapeHtml(conflictOwner) + conflictId + '</b>.<br>Please use another card.</div>' +
            '</div>';
        }

        // Re-arm registration on ESP32 after 1.2s so the user can immediately tap another card
        if (window._activeRegParams) {
          setTimeout(async () => {
            if (document.getElementById('regModal')?.classList.contains('active')) {
              try {
                await fetch('/api/register/start?' + window._activeRegParams.toString(), { method: 'POST', body: window._activeRegParams });
              } catch(e) {}
            }
          }, 1200);
        }
      } else if (data.state === 'timeout') {
        clearInterval(regPollInterval);
        isPollingReg = false;
        window.playAudioTone('alert');
        if (statusText) {
          statusText.innerText = 'Timed out. No card detected.';
          statusText.style.color = 'var(--md-sys-color-error)';
        }
      } else if (data.state === 'cancelled') {
        clearInterval(regPollInterval);
        isPollingReg = false;
        document.getElementById('regModal')?.classList.remove('active');
      }
    } catch(e) {}
  }, 450);
};

window.registerAnotherCard = function() {
  document.getElementById('regModal')?.classList.remove('active');
  document.getElementById('regFirstName').value = '';
  document.getElementById('regMiddleInitial').value = '';
  document.getElementById('regLastName').value = '';
  document.getElementById('regOwnerId').value = '';
  document.getElementById('regOwnerName').value = '';
  window.updateRegNamePreview();
  const inlineStatus = document.getElementById('regStatusCard');
  if (inlineStatus) inlineStatus.style.display = 'none';
  document.getElementById('regFirstName')?.focus();
};

window.closeRegModal = function() {
  document.getElementById('regModal')?.classList.remove('active');
  window.switchNotionTab('notion-reg-single');
};

window.cancelRegistration = async function() {
  if (regPollInterval) clearInterval(regPollInterval);
  isPollingReg = false;
  try {
    await fetch('/api/register/cancel', { method: 'POST' });
  } catch(e) {}
  document.getElementById('regModal').classList.remove('active');
};


  let batchAttendees = [];
  let batchCurrentIndex = 0;
  let batchPollInterval = null;
  let isBatchRunning = false;

  window.updateBatchCount = function() {
    const text = document.getElementById('batchNamesInput').value;
    const lines = text.split('\n').map(l => l.trim()).filter(l => l.length > 0);
    const badge = document.getElementById('batchCountBadge');
    if (badge) {
      badge.innerText = `${lines.length} attendee${lines.length === 1 ? '' : 's'} detected`;
      badge.className = lines.length > 0 ? 'badge badge-in' : 'badge';
    }
  };

  window.clearBatchInput = function() {
    document.getElementById('batchNamesInput').value = '';
    window.updateBatchCount();
  };

  window.startBatchScanning = async function() {
    const text = document.getElementById('batchNamesInput')?.value || '';
    const lines = text.split('\n').map(l => l.trim()).filter(l => l.length > 0);
    if (lines.length === 0) {
      alert('Please enter or paste at least one name.');
      return;
    }

    batchAttendees = lines.map((line, idx) => {
      let name = line;
      let id = '';
      if (line.includes(',')) {
        const parts = line.split(',');
        name = parts[0].trim();
        id = parts.slice(1).join(',').trim();
      }
      return {
        index: idx + 1,
        name: name,
        id: id,
        uid: '',
        status: 'pending' // 'pending', 'active', 'done', 'skipped'
      };
    });

    batchCurrentIndex = 0;
    isBatchRunning = true;

    const inView = document.getElementById('batchInputView');
    const scView = document.getElementById('batchScanningView');
    const dnView = document.getElementById('batchDoneView') || document.getElementById('batchCompleteView');
    if (inView) inView.style.display = 'none';
    if (scView) scView.style.display = 'flex';
    if (dnView) dnView.style.display = 'none';

    const total = batchAttendees.length;
    const lbl = document.getElementById('batchProgressLabel');
    const pctEl = document.getElementById('batchProgressPct');
    const bar = document.getElementById('batchProgressBar');
    if (lbl) lbl.innerText = `Scanning 1 of ${total}`;
    if (pctEl) pctEl.innerText = `${Math.round((1 / total) * 100)}%`;
    if (bar) bar.style.width = `${Math.round((1 / total) * 100)}%`;

    window.renderBatchQueue();
    window.processNextBatchAttendee();
  };

  window.processNextBatchAttendee = async function() {
    if (!isBatchRunning) return;

    if (batchCurrentIndex >= batchAttendees.length) {
      window.finishBatchScanning();
      return;
    }

    const current = batchAttendees[batchCurrentIndex];
    current.status = 'active';
    window.renderBatchQueue();

    const total = batchAttendees.length;
    const step = batchCurrentIndex + 1;
    const pct = Math.round((step / total) * 100);

    const lbl = document.getElementById('batchProgressLabel');
    const pctEl = document.getElementById('batchProgressPct');
    const bar = document.getElementById('batchProgressBar');
    if (lbl) lbl.innerText = `Scanning ${step} of ${total}`;
    if (pctEl) pctEl.innerText = `${pct}%`;
    if (bar) bar.style.width = `${pct}%`;

    const nameEl = document.getElementById('batchActiveName');
    const idEl = document.getElementById('batchActiveId');
    if (nameEl) nameEl.innerText = current.name;
    if (idEl) idEl.innerText = current.id ? `ID: ${current.id}` : '(No ID assigned)';

    const spotlight = document.getElementById('batchSpotlightCard');
    if (spotlight) {
      spotlight.style.borderColor = 'var(--md-sys-color-primary)';
      spotlight.style.background = 'var(--md-sys-color-surface-container-high)';
    }

    const promptBox = document.getElementById('batchStatusPrompt');
    if (promptBox) {
      promptBox.innerHTML = `Present card to reader for <b>${window.escapeHtml(current.name)}</b>...`;
      promptBox.style.color = 'var(--md-sys-color-on-surface)';
      promptBox.style.borderColor = 'var(--md-sys-color-outline)';
    }

    const formData = new URLSearchParams();
    formData.append('name', current.name);
    if (current.id) formData.append('student_id', current.id);
    formData.append('batch_info', `${step}/${total}`);

    try {
      const res = await fetch('/api/register/start?' + formData.toString(), { method: 'POST', body: formData });
      const data = await res.json();
      if (!data.success) {
        alert('Error starting registration: ' + data.message);
        return;
      }
      window.startBatchPolling();
    } catch(e) {
      alert('Network error: ' + e);
    }
  };

  window.startBatchPolling = function() {
    if (batchPollInterval) clearInterval(batchPollInterval);

    batchPollInterval = setInterval(async () => {
      if (!isBatchRunning) {
        clearInterval(batchPollInterval);
        batchPollInterval = null;
        return;
      }

      try {
        const res = await fetch('/api/register/status');
        const data = await res.json();

        const current = batchAttendees[batchCurrentIndex];
        const spotlight = document.getElementById('batchSpotlightCard');
        const promptBox = document.getElementById('batchStatusPrompt');

        if (data.state === 'success') {
          clearInterval(batchPollInterval);
          batchPollInterval = null;

          current.uid = data.result_uid;
          current.status = 'done';
          window.playAudioTone('success');

          if (spotlight) {
            spotlight.style.borderColor = 'var(--md-sys-color-success)';
            spotlight.style.background = 'var(--md-sys-color-success-container)';
          }
          if (promptBox) {
            promptBox.innerHTML = `? <b>Card Allocated!</b> UID: <code>${data.result_uid}</code> registered to ${window.escapeHtml(current.name)}`;
            promptBox.style.color = 'var(--md-sys-color-on-success-container)';
            promptBox.style.borderColor = 'var(--md-sys-color-success)';
          }

          window.renderBatchQueue();
          window.loadCardsDatabase();

          setTimeout(() => {
            if (!isBatchRunning) return;
            batchCurrentIndex++;
            window.processNextBatchAttendee();
          }, 800);

        } else if (data.state === 'same_card') {
          // Card belongs to this attendee: advance
          clearInterval(batchPollInterval);
          batchPollInterval = null;
          current.uid = data.result_uid;
          current.status = 'done';
          window.playAudioTone('success');
          if (spotlight) {
            spotlight.style.borderColor = 'var(--md-sys-color-success)';
            spotlight.style.background = 'var(--md-sys-color-success-container)';
          }
          if (promptBox) {
            promptBox.innerHTML = `Card already registered to <b>${window.escapeHtml(current.name)}</b>.`;
            promptBox.style.color = 'var(--md-sys-color-on-success-container)';
            promptBox.style.borderColor = 'var(--md-sys-color-success)';
          }
          window.renderBatchQueue();
          window.loadCardsDatabase();
          setTimeout(() => {
            if (!isBatchRunning) return;
            batchCurrentIndex++;
            window.processNextBatchAttendee();
          }, 800);
        } else if (data.state === 'conflict') {
          // Card belongs to someone else: prompt to use another tag and re-arm
          clearInterval(batchPollInterval);
          batchPollInterval = null;
          window.playAudioTone('alert');

          if (spotlight) {
            spotlight.style.borderColor = 'var(--md-sys-color-error)';
            spotlight.style.background = 'var(--md-sys-color-error-container)';
          }
          if (promptBox) {
            promptBox.innerHTML = `Card already registered to <b>${window.escapeHtml(data.conflict_name || 'another person')}</b>.<br>Please use another card.`;
            promptBox.style.color = 'var(--md-sys-color-on-error-container)';
            promptBox.style.borderColor = 'var(--md-sys-color-error)';
          }

          setTimeout(() => {
            if (!isBatchRunning) return;
            window.processNextBatchAttendee();
          }, 1400);

        } else if (data.state === 'timeout') {
          clearInterval(batchPollInterval);
          batchPollInterval = null;
          if (promptBox) promptBox.innerHTML = `? Registration timed out for <b>${window.escapeHtml(current.name)}</b>. Tap card or click Skip.`;
          setTimeout(() => {
            if (!isBatchRunning) return;
            window.processNextBatchAttendee();
          }, 1200);
        }
      } catch(e) {}
    }, 350);
  };

  window.skipBatchCurrent = async function() {
    if (!isBatchRunning || batchCurrentIndex >= batchAttendees.length) return;
    if (batchPollInterval) {
      clearInterval(batchPollInterval);
      batchPollInterval = null;
    }
    try {
      await fetch('/api/register/cancel', { method: 'POST' });
    } catch(e) {}

    batchAttendees[batchCurrentIndex].status = 'skipped';
    window.renderBatchQueue();

    batchCurrentIndex++;
    window.processNextBatchAttendee();
  };

  window.cancelBatchProcess = async function() {
    if (!confirm('Cancel batch registration? Cards already scanned will remain saved.')) return;
    isBatchRunning = false;
    if (batchPollInterval) {
      clearInterval(batchPollInterval);
      batchPollInterval = null;
    }
    try {
      await fetch('/api/register/cancel', { method: 'POST' });
    } catch(e) {}

    const hasDoneCards = (batchAttendees || []).some(a => a.status === 'done');
    if (hasDoneCards) {
      window.finishBatchScanning();
    } else {
      window.resetBatchInputView();
    }
  };

  window.finishBatchScanning = function() {
    isBatchRunning = false;
    if (batchPollInterval) {
      clearInterval(batchPollInterval);
      batchPollInterval = null;
    }

    const doneCount = (batchAttendees || []).filter(a => a.status === 'done').length;
    const skipCount = (batchAttendees || []).filter(a => a.status === 'skipped').length;
    const totalCount = (batchAttendees || []).length;

    const scView = document.getElementById('batchScanningView');
    const dnView = document.getElementById('batchDoneView') || document.getElementById('batchCompleteView');
    const summary = document.getElementById('batchSummaryStats') || document.getElementById('batchCompleteSummary');

    if (scView) scView.style.display = 'none';
    if (dnView) dnView.style.display = 'flex';
    if (summary) {
      summary.innerText = `${doneCount} card(s) registered successfully, ${skipCount} skipped out of ${totalCount} total attendees.`;
    }

    window.loadCardsDatabase();
  };

  window.resetBatchInputView = function() {
    isBatchRunning = false;
    if (batchPollInterval) {
      clearInterval(batchPollInterval);
      batchPollInterval = null;
    }
    const inView = document.getElementById('batchInputView');
    const scView = document.getElementById('batchScanningView');
    const dnView = document.getElementById('batchDoneView') || document.getElementById('batchCompleteView');
    if (dnView) dnView.style.display = 'none';
    if (scView) scView.style.display = 'none';
    if (inView) inView.style.display = 'flex';
    window.updateBatchCount();
  };

  window.renderBatchQueue = function() {
    const tbody = document.getElementById('batchQueueTableBody');
    if (!tbody || !batchAttendees) return;

    let html = '';
    batchAttendees.forEach((a, idx) => {
      let statusBadge = '<span class="badge" style="opacity: 0.6;">Queued</span>';
      let rowStyle = '';
      if (a.status === 'active') {
        statusBadge = '<span class="badge badge-in">? Scanning...</span>';
        rowStyle = 'background: rgba(14, 165, 233, 0.12); font-weight: 700;';
      } else if (a.status === 'done') {
        statusBadge = '<span class="badge badge-done" style="background: rgba(34, 197, 94, 0.15); color: var(--md-sys-color-success);">? Done</span>';
      } else if (a.status === 'skipped') {
        statusBadge = '<span class="badge badge-out" style="opacity: 0.7;">Skipped</span>';
      }

      html += `<tr style="${rowStyle}">
        <td style="text-align: center; color: var(--md-sys-color-on-surface-variant);">${idx + 1}</td>
        <td><strong>${window.escapeHtml(a.name)}</strong></td>
        <td>${a.id ? window.escapeHtml(a.id) : '<span style="color:var(--md-sys-color-outline);">&mdash;</span>'}</td>
        <td><span class="mono-txt">${a.uid ? window.escapeHtml(a.uid) : '&mdash;'}</span></td>
        <td>${statusBadge}</td>
      </tr>`;
    });
    tbody.innerHTML = html;
  };

  let isCardsLoading = false;
window.loadCardsDatabase = async function() {
  if (isCardsLoading) return;
  isCardsLoading = true;
  const listEl = document.getElementById('notionCardsListContainer');
  const tbody = document.getElementById('notionCardsTableBody');
  try {
    const res = await fetch('/api/cards');
    if (!res.ok) throw new Error('HTTP ' + res.status);
    const data = await res.json();
    allCardsData = Array.isArray(data) ? data : [];
    
    const len = allCardsData.length;
    const cntBadge = document.getElementById('notionCardsCountBadge');
    if (cntBadge) cntBadge.innerText = `${len} card${len === 1 ? '' : 's'}`;
    const sideBadge = document.getElementById('notionCardsBadge');
    if (sideBadge) sideBadge.innerText = len;
    const totEl = document.getElementById('notionCardsTotalText');
    if (totEl) totEl.innerText = `${len} cards`;

    const searchEl = document.getElementById('notionCardsSearch');
    const q = searchEl ? searchEl.value.trim() : '';
    if (q) window.filterCardsTable();
    else window.renderCardsTable(allCardsData);
  } catch(e) {
    console.error('Cards load error:', e);
    if (listEl) {
      listEl.innerHTML = `<div class="empty-state" style="text-align: center; padding: 24px; color: var(--md-sys-color-error);">
        Unable to load cards database. <button class="btn btn-outlined btn-sm" style="margin-top: 8px;" onclick="loadCardsDatabase()">Retry</button>
      </div>`;
    }
    if (tbody) {
      tbody.innerHTML = `<tr><td colspan="6" style="text-align: center; color: var(--md-sys-color-error); padding: 24px;">
        Unable to load cards database. <button class="btn btn-outlined btn-sm" onclick="loadCardsDatabase()">Retry</button>
      </td></tr>`;
    }
  } finally {
    isCardsLoading = false;
  }
};

window.renderCardsTable = function(cards) {
  const listEl = document.getElementById('notionCardsListContainer');
  const tbody = document.getElementById('notionCardsTableBody');
  if (!cards || cards.length === 0) {
    const emptyMsg = 'No cards registered yet.';
    if (listEl) {
      listEl.innerHTML = `<div class="empty-state" style="text-align: center; padding: 32px 16px; color: var(--md-sys-color-on-surface-variant); font-size: 0.875rem;">${emptyMsg}</div>`;
    }
    if (tbody) {
      tbody.innerHTML = `<tr><td colspan="6" style="text-align: center; color: var(--md-sys-color-on-surface-variant); padding: 24px;">${emptyMsg}</td></tr>`;
    }
    return;
  }

  let cardHtml = '';
  let tableHtml = '';

  cards.forEach((c, idx) => {
    const safeName = window.escapeHtml(c.name || 'Unnamed Card');
    const safeUid = window.escapeHtml(c.uid || '--');
    const safeId = c.student_id ? window.escapeHtml(c.student_id) : '';
    const idBadge = safeId ? `<span class="badge badge-in">${safeId}</span>` : '';
    const studentIdCell = safeId ? safeId : '<span style="color: var(--md-sys-color-outline);">&mdash;</span>';
    const regDate = c.reg_date ? window.formatClock(c.reg_date) : '<span style="color: var(--md-sys-color-outline);">&mdash;</span>';

    cardHtml += `<div class="notion-card" style="display: flex; justify-content: space-between; align-items: center; padding: 12px 14px; gap: 12px; border: 1px solid var(--md-sys-color-outline-variant); border-radius: 8px; background: var(--md-sys-color-surface); margin-bottom: 0;">
      <div style="flex: 1; min-width: 0;">
        <div style="display: flex; align-items: center; gap: 8px; margin-bottom: 4px; flex-wrap: wrap;">
          <strong style="font-size: 0.9375rem; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; max-width: 180px;">${safeName}</strong>
          ${idBadge}
        </div>
        <div class="mono-txt" style="font-size: 0.8125rem; color: var(--md-sys-color-on-surface-variant);">
          ${safeUid}${c.reg_date ? ' • ' + regDate : ''}
        </div>
      </div>
      <div style="display: inline-flex; gap: 6px; align-items: center; flex-shrink: 0;">
        <button class="icon-btn" onclick="deleteCard('${window.escapeHtml(c.uid)}')" title="Delete Card" style="color: var(--md-sys-color-error); width: 32px; height: 32px;">
          <svg class="icon-sm" viewBox="0 0 24 24"><path d="M6 19c0 1.1.9 2 2 2h8c1.1 0 2-.9 2-2V7H6v12zM19 4h-3.5l-1-1h-5l-1 1H5v2h14V4z"/></svg>
        </button>
      </div>
    </div>`;

    tableHtml += `<tr>
      <td>${idx + 1}</td>
      <td><strong>${safeName}</strong></td>
      <td>${studentIdCell}</td>
      <td><span class="mono-txt">${safeUid}</span></td>
      <td class="mono-txt" style="font-size: 0.8rem; color: var(--md-sys-color-on-surface-variant);">${regDate}</td>
      <td style="text-align: center;">
        <button class="icon-btn" onclick="deleteCard('${window.escapeHtml(c.uid)}')" title="Delete Card" style="color: var(--md-sys-color-error); width: 32px; height: 32px; margin: 0 auto;">
          <svg class="icon-sm" viewBox="0 0 24 24"><path d="M6 19c0 1.1.9 2 2 2h8c1.1 0 2-.9 2-2V7H6v12zM19 4h-3.5l-1-1h-5l-1 1H5v2h14V4z"/></svg>
        </button>
      </td>
    </tr>`;
  });

  if (listEl) listEl.innerHTML = cardHtml;
  if (tbody) tbody.innerHTML = tableHtml;
};

window.filterCardsTable = function() {
  const searchEl = document.getElementById('notionCardsSearch');
  const q = searchEl ? searchEl.value.toLowerCase().trim() : '';
  if (!q) { window.renderCardsTable(allCardsData); return; }
  const filtered = allCardsData.filter(c => 
    (c.name && String(c.name).toLowerCase().includes(q)) || 
    (c.student_id && String(c.student_id).toLowerCase().includes(q)) || 
    (c.uid && String(c.uid).toLowerCase().includes(q))
  );
  window.renderCardsTable(filtered);
};

window.deleteCard = async function(uid) {
  if (!confirm(`Delete card UID: ${uid}?`)) return;
  const formData = new URLSearchParams();
  formData.append('uid', uid);
  try {
    const res = await fetch('/api/cards/delete', { method: 'POST', body: formData });
    const data = await res.json();
    if (data.success) window.loadCardsDatabase();
    else alert('Delete failed: ' + data.message);
  } catch(e) {
    alert('Error: ' + e);
  }
};

window.clearAllCards = async function() {
  if (!confirm('Delete ALL registered cards from memory?')) return;
  if (!confirm('Confirm again: Cannot be undone.')) return;
  try {
    const res = await fetch('/api/cards/clear', { method: 'POST' });
    const data = await res.json();
    window.loadCardsDatabase();
    alert(data.message);
  } catch(e) {
    alert('Error: ' + e);
  }
};

window.loadAttendanceLog = async function() {
  try {
    const res = await fetch('/api/attendance');
    const data = await res.json();
    allAttendeesData = data;
    const q = document.getElementById('attendanceSearch').value.trim();
    if (q) window.filterAttendanceTable();
    else window.renderAttendanceTable(data);
    window.updateAttendanceStats(data);
  } catch(e) {}
};

window.updateAttendanceStats = function(data) {
  let inCount = 0;
  let outCount = 0;
  let completedCount = 0;
  data.forEach(r => {
    const hasIn = r.time_in && r.time_in !== '--' && r.time_in !== '';
    const hasOut = r.time_out && r.time_out !== '--' && r.time_out !== '';
    if (hasIn) inCount++;
    if (hasOut) outCount++;
    if (hasIn && hasOut) completedCount++;
  });
  document.getElementById('statSummaryText').innerText = `${data.length} total (${inCount} in, ${outCount} out)`;
};

window.renderAttendanceTable = function(records) {
  const tbody = document.getElementById('attendanceTableBody');
  if (!records || records.length === 0) {
    tbody.innerHTML = '<tr><td colspan="5" style="text-align: center; color: var(--md-sys-color-on-surface-variant); padding: 22px;">No attendees logged yet.</td></tr>';
    return;
  }
  let html = '';
  const sorted = [...records].reverse();
  sorted.forEach((r, idx) => {
    const hasIn = r.time_in && r.time_in !== '--' && r.time_in !== '';
    const hasOut = r.time_out && r.time_out !== '--' && r.time_out !== '';
    let statusBadge = '';
    if (hasIn && hasOut) {
      statusBadge = '<span class="badge badge-done">&#10003; Done</span>';
    } else if (hasIn) {
      statusBadge = '<span class="badge badge-in">In</span>';
    } else if (hasOut) {
      statusBadge = '<span class="badge badge-out">Out</span>';
    } else {
      statusBadge = '<span class="badge badge-in">Present</span>';
    }

    html += `<tr>
      <td>${records.length - idx}</td>
      <td><strong>${window.escapeHtml(r.name)}</strong></td>
      <td class="mono-txt">${window.formatTime12h(r.time_in)}</td>
      <td class="mono-txt">${window.formatTime12h(r.time_out)}</td>
      <td>${statusBadge}</td>
    </tr>`;
  });
  tbody.innerHTML = html;
};

window.filterAttendanceTable = function() {
  const q = document.getElementById('attendanceSearch').value.toLowerCase().trim();
  if (!q) { window.renderAttendanceTable(allAttendeesData); return; }
  const filtered = allAttendeesData.filter(r => 
    r.name.toLowerCase().includes(q) || 
    (r.student_id && r.student_id.toLowerCase().includes(q)) || 
    r.uid.toLowerCase().includes(q)
  );
  window.renderAttendanceTable(filtered);
};

window.pollStatus = async function() {
  try {
    const res = await fetch('/api/status');
    const d = await res.json();

    document.getElementById('headerClock').innerText = d.time_set ? window.formatClock(d.current_time) : '--:--:--';

    const entranceScreen = document.getElementById('entranceScreen');
    const consoleScreen = document.getElementById('consoleScreen');

    if (d.session_active) {
      entranceScreen.style.display = 'none';
      consoleScreen.style.display = 'flex';
      const titleEl = document.getElementById('activeTitleLabel');
      if (titleEl) {
        titleEl.innerText = d.session_title;
        titleEl.title = d.session_title;
      }
      const liveCsvBtn = document.getElementById('btnExportLiveCsv');
      if (liveCsvBtn && d.session_title) {
        const safeLiveTitle = (d.session_title || 'attendance').trim().replace(/[/\\:*?"<>|\s,;]+/g, '_').replace(/^_+|_+$/g, '') || 'attendance';
        liveCsvBtn.setAttribute('download', safeLiveTitle + '.csv');
      }
      const modeChip = document.getElementById('activeModeChip');
      if (modeChip) {
        modeChip.innerText = d.session_type;
        modeChip.className = (d.session_type === 'Exit') ? 'badge badge-out' : 'badge badge-in';
      }

      const switchTarget = document.getElementById('switchTargetMode');
      if (switchTarget) {
        switchTarget.innerText = (d.session_type === 'Entrance') ? 'Exit' : 'Entrance';
      }

      const isExit = (d.session_type === 'Exit');
      const rfidModeEntrance = document.getElementById('rfidModeEntrance');
      const rfidModeExit = document.getElementById('rfidModeExit');
      if (rfidModeEntrance && rfidModeExit) {
        rfidModeEntrance.classList.toggle('active-in', !isExit);
        rfidModeExit.classList.toggle('active-out', isExit);
      }

      const idleModePill = document.getElementById('idleModePill');
      if (idleModePill) {
        idleModePill.innerText = `● ${d.session_type.toUpperCase()} MODE (${isExit ? 'OUT' : 'IN'})`;
      }
    } else {
      consoleScreen.style.display = 'none';
      entranceScreen.style.display = 'flex';
      window.setSessionSavedState(false);
    }

    if (!d.time_set) {
      window.autoSyncTime();
    }

    const cardsBadge = document.getElementById('notionCardsBadge');
    if (cardsBadge && d.registered_count !== undefined) cardsBadge.innerText = d.registered_count;
    const histBadge = document.getElementById('notionHistoryBadge');
    if (histBadge && d.history_count !== undefined) histBadge.innerText = d.history_count;
    const clkDisp = document.getElementById('notionClockDisplay');
    if (clkDisp) clkDisp.innerText = d.time_set ? window.formatClock(d.current_time) : '--:--:--';
    const wifiClients = document.getElementById('notionWifiClients');
    if (wifiClients && d.wifi_clients !== undefined) wifiClients.innerText = `${d.wifi_clients} connected`;
    const heapText = document.getElementById('notionHeapText');
    if (heapText && d.free_heap) heapText.innerText = `${Math.round(d.free_heap / 1024)} KB free`;

    const fsUsed = (typeof d.fs_used === 'number') ? d.fs_used : 0;
    const fsTotal = (typeof d.fs_total === 'number' && d.fs_total > 0) ? d.fs_total : 1966080;
    const fsFree = Math.max(0, fsTotal - fsUsed);
    const estRemainingCards = Math.max(0, Math.floor(fsFree / 96));

    const totalText = document.getElementById('notionFsTotalText');
    if (totalText) {
      totalText.innerText = `${(fsTotal / (1024 * 1024)).toFixed(2)} MB LittleFS`;
    }

    const fsText = document.getElementById('notionFsUsedText');
    if (fsText) {
      const pct = fsTotal > 0 ? Math.round((fsUsed / fsTotal) * 100) : 0;
      fsText.innerText = `${Math.round(fsUsed / 1024)} KB used (${pct}%)`;
    }

    const freeText = document.getElementById('notionFsFreeText');
    if (freeText) freeText.innerText = `${Math.round(fsFree / 1024)} KB free`;

    const estCardsText = document.getElementById('notionCardsEstCapacity');
    if (estCardsText) {
      estCardsText.innerText = `~${estRemainingCards.toLocaleString()} cards (~${Math.round(fsFree / 1024)} KB free)`;
    }

    const cardsTotal = document.getElementById('notionCardsTotalText');
    if (cardsTotal && d.registered_count !== undefined) cardsTotal.innerText = `${d.registered_count} cards registered`;
    const histTotal = document.getElementById('notionHistoryTotalText');
    if (histTotal && d.history_count !== undefined) histTotal.innerText = `${d.history_count} / 5 sessions`;
    const uptimeText = document.getElementById('notionUptimeText');
    if (uptimeText && d.uptime_sec !== undefined) {
      const uH = Math.floor(d.uptime_sec / 3600);
      const uM = Math.floor((d.uptime_sec % 3600) / 60);
      const uS = d.uptime_sec % 60;
      uptimeText.innerText = `${uH}h ${uM}m ${uS}s`;
    }
    const liveCsvBtn2 = document.getElementById('btnExportLiveCsv2');
    if (liveCsvBtn2 && d.session_title) {
      const safeLiveTitle = (d.session_title || 'attendance').trim().replace(/[/\\:*?"<>|\s,;]+/g, '_').replace(/^_+|_+$/g, '') || 'attendance';
      liveCsvBtn2.setAttribute('download', safeLiveTitle + '.csv');
    }
    if (d.current_wifi_pass !== undefined) {
      window._currentWifiPass = d.current_wifi_pass;
      const passEl = document.getElementById('notionActiveWifiPass');
      if (passEl && !isWifiPassVisible) {
        passEl.innerText = (d.current_wifi_pass.length > 0) ? '••••••••' : '(no password)';
      } else if (passEl && isWifiPassVisible) {
        passEl.innerText = d.current_wifi_pass || '(no password)';
      }
    }
    if (d.wifi_ssid !== undefined) {
      const ssidEl = document.getElementById('notionWifiSsid');
      if (ssidEl) ssidEl.innerText = d.wifi_ssid;
    }
    const pBanner = document.getElementById('pendingPassBanner');
    const pText = document.getElementById('notionPendingPassText');
    if (pBanner && pText) {
      if (d.wifi_pass_pending) {
        pBanner.style.display = 'flex';
        pText.innerText = d.next_wifi_pass ? `"${d.next_wifi_pass}"` : '(open network / no pass)';
      } else {
        pBanner.style.display = 'none';
      }
    }
    if (d.show_lcd_wifi !== undefined) {
      const gToggle = document.getElementById('lcdWifiGuideToggle');
      if (gToggle) gToggle.checked = d.show_lcd_wifi;
    }
    if (!window._settingsLoaded && d.lcd_timeout !== undefined) {
      window._settingsLoaded = true;
      const tSelect = document.getElementById('lcdTimeoutSelect');
      if (tSelect) tSelect.value = d.lcd_timeout;
      const dSelect = document.getElementById('cardDebounceSelect');
      if (dSelect) dSelect.value = d.card_debounce;
      const gToggle = document.getElementById('lcdWifiGuideToggle');
      if (gToggle) gToggle.checked = d.show_lcd_wifi;
      const rToggle = document.getElementById('requireRegToggle');
      if (rToggle) rToggle.checked = d.require_registration;
      if (d.lcd_brightness_pct !== undefined || d.lcd_brightness !== undefined) {
        const bSlider = document.getElementById('lcdBrightnessSlider');
        const pct = (d.lcd_brightness_pct !== undefined) ? d.lcd_brightness_pct : Math.round((d.lcd_brightness / 255) * 100);
        if (bSlider) {
          bSlider.value = pct;
          const lbl = document.getElementById('lcdBrightnessLabel');
          if (lbl) lbl.innerText = pct + '%';
        }
      }
      if (d.rgb_brightness_pct !== undefined || d.rgb_brightness !== undefined) {
        const rgbSlider = document.getElementById('rgbBrightnessSlider');
        const pct = (d.rgb_brightness_pct !== undefined) ? d.rgb_brightness_pct : Math.round((d.rgb_brightness / 168) * 100);
        if (rgbSlider) {
          rgbSlider.value = pct;
          const lbl = document.getElementById('rgbBrightnessLabel');
          if (lbl) lbl.innerText = pct + '%';
        }
      }
      if (d.lcd_dim_duration !== undefined) {
        const dimSelect = document.getElementById('lcdDimDurationSelect');
        if (dimSelect) dimSelect.value = d.lcd_dim_duration;
      }
    }

    if (isFirstPoll) {
      isFirstPoll = false;
      lastScanTimestamp = d.last_scan ? d.last_scan.timestamp : 0;
    } else if (d.last_scan && d.last_scan.timestamp && d.last_scan.timestamp !== lastScanTimestamp) {
      lastScanTimestamp = d.last_scan.timestamp;
      
      const isSettingsOpen = document.getElementById('settingsModal')?.classList.contains('active');
      const isTimeModalOpen = document.getElementById('timeModal')?.classList.contains('active');
      const isFirstBootOpen = document.getElementById('firstBootModal')?.classList.contains('active');
      const isHistoryOpen = document.getElementById('historyModal')?.classList.contains('active');
      const isConsoleVisible = (consoleScreen.style.display === 'flex');

      const isSingleRegActive = document.getElementById('notion-reg-single')?.classList.contains('active');
      const inlineStatus = document.getElementById('regStatusCard');

      if (d.last_scan.type === 'CardAlreadyRegistered' && isSingleRegActive) {
        if (inlineStatus) {
          inlineStatus.style.display = 'block';
          inlineStatus.style.background = 'var(--md-sys-color-surface-container-high)';
          inlineStatus.style.color = 'var(--md-sys-color-on-surface)';
          inlineStatus.style.border = '1px solid var(--md-sys-color-primary)';
          inlineStatus.innerHTML = '<div style="display:flex;align-items:center;gap:8px;">' +
            '<svg class="icon-sm" style="color:var(--md-sys-color-primary);flex-shrink:0;" viewBox="0 0 24 24"><path fill="currentColor" d="M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm-2 15l-5-5 1.41-1.41L10 14.17l7.59-7.59L19 8l-9 9z"/></svg>' +
            '<div><b>Already Registered</b><br>UID <code>' + window.escapeHtml(d.last_scan.uid) + '</code> is already registered to <b>' + window.escapeHtml(d.last_scan.name) + '</b>.</div>' +
            '</div>';
        }
        window.playAudioTone('success');
      } else if (d.last_scan.type === 'CardDetected' && isSingleRegActive && inlineStatus) {
        const enteredName = document.getElementById('regOwnerName')?.value.trim();
        if (enteredName && (!window.isPollingReg)) {
          window.startRegistration();
        } else {
          inlineStatus.style.display = 'block';
          inlineStatus.style.background = 'var(--md-sys-color-surface-container-high)';
          inlineStatus.style.color = 'var(--md-sys-color-on-surface)';
          inlineStatus.style.border = '1px solid var(--md-sys-color-primary)';
          inlineStatus.innerHTML = '<div style="display:flex;align-items:center;gap:8px;">' +
            '<svg class="icon-sm" style="color:var(--md-sys-color-primary);flex-shrink:0;" viewBox="0 0 24 24"><path fill="currentColor" d="M20 4H4c-1.11 0-1.99.89-1.99 2L2 18c0 1.11.89 2 2 2h16c1.11 0 2-.89 2-2V6c0-1.11-.89-2-2-2zm0 14H4v-6h16v6zm0-10H4V6h16v2z"/></svg>' +
            '<div><b>Tag Detected: <code>' + window.escapeHtml(d.last_scan.uid) + '</code></b><br>Enter attendee details and click <b>Tap Card to Register</b>.</div>' +
            '</div>';
          window.playAudioTone('success');
        }
      } else if (d.last_scan.type === 'CardConflict' && isSingleRegActive && inlineStatus) {
        inlineStatus.style.display = 'block';
        inlineStatus.style.background = 'var(--md-sys-color-error-container)';
        inlineStatus.style.color = 'var(--md-sys-color-on-error-container)';
        inlineStatus.style.border = '1px solid var(--md-sys-color-error)';
        inlineStatus.innerHTML = '<div style="display:flex;align-items:center;gap:8px;">' +
          '<svg class="icon-sm" style="color:var(--md-sys-color-error);flex-shrink:0;" viewBox="0 0 24 24"><path fill="currentColor" d="M1 21h22L12 2 1 21zm12-3h-2v-2h2v2zm0-4h-2v-4h2v4z"/></svg>' +
          '<div><b>Card Already in Use</b><br>UID <code>' + window.escapeHtml(d.last_scan.uid) + '</code> is registered to <b>' + window.escapeHtml(d.last_scan.name) + '</b>.<br>Please use another card.</div>' +
          '</div>';
        window.playAudioTone('alert');
      } else if (d.last_scan.type === 'Registration') {
        window.handleNewScan(d.last_scan);
      } else if (d.session_active && isConsoleVisible && !isSettingsOpen && !isTimeModalOpen && !isFirstBootOpen && !isHistoryOpen) {
        window.handleNewScan(d.last_scan);
      }
    }
  } catch(e) {}
};

window.showIdleTapPrompt = function() {
  const fullCard = document.getElementById('bigRfidFullCard');
  const idleView = document.getElementById('rfidIdleView');
  const resultView = document.getElementById('rfidResultView');

  if (!fullCard || !idleView || !resultView) return;

  fullCard.className = 'rfid-panel-fullcard theme-idle';
  resultView.style.display = 'none';
  idleView.style.display = 'flex';
};

window.handleNewScan = function(scan) {
  if (scanDisplayTimeout) {
    clearTimeout(scanDisplayTimeout);
    scanDisplayTimeout = null;
  }

  const fullCard = document.getElementById('bigRfidFullCard');
  const idleView = document.getElementById('rfidIdleView');
  const resultView = document.getElementById('rfidResultView');
  const badge = document.getElementById('resultStatusBadge');
  const nameEl = document.getElementById('resultAttendeeName');
  const subEl = document.getElementById('resultSubMessage');
  const metaEl = document.getElementById('resultMetaTag');

  const timeStr = window.formatTime12h(scan.time);
  let cleanMsg = (scan.message || '').replace(/\d{4}-\d{2}-\d{2}\s+(\d{1,2}:\d{2}:\d{2}(\s*[AaPp][Mm])?)/g, (m, t) => window.formatTime12h(t));
  cleanMsg = cleanMsg.replace(/\b(\d{2}:\d{2}:\d{2})\b/g, (m, t) => window.formatTime12h(t));

  idleView.style.display = 'none';
  resultView.style.display = 'flex';

  if (scan.is_duplicate) {
    fullCard.className = 'rfid-panel-fullcard theme-duplicate';
    badge.innerText = 'REPEATED TAP (IGNORED)';
    nameEl.innerText = scan.name;
    subEl.innerText = cleanMsg || `Already checked in at ${timeStr}. (Listed as 1 entry)`;
    metaEl.innerText = `UID: ${scan.uid} \u2022 ${timeStr}`;
    window.playAudioTone('duplicate');
  } else if (scan.type === 'Entrance' || scan.type === 'Exit') {
    fullCard.className = 'rfid-panel-fullcard theme-success';
    badge.innerText = `${scan.type.toUpperCase()} RECORDED`;
    nameEl.innerText = scan.name;
    subEl.innerText = `Recorded at ${timeStr} \u2022 ${scan.type}`;
    metaEl.innerText = `Card UID: ${scan.uid}`;
    window.playAudioTone('success');
    window.loadAttendanceLog();
  } else if (scan.type === 'Registration') {
    fullCard.className = 'rfid-panel-fullcard theme-success';
    badge.innerText = 'CARD ALLOCATED';
    nameEl.innerText = scan.name;
    subEl.innerText = scan.message || 'Card registered successfully.';
    metaEl.innerText = `UID: ${scan.uid} \u2022 ${timeStr}`;
    window.playAudioTone('success');
    window.loadCardsDatabase();
  } else if (scan.type === 'Rejected') {
    fullCard.className = 'rfid-panel-fullcard theme-error';
    badge.innerText = 'ACCESS DENIED';
    nameEl.innerText = 'Unregistered Tag';
    subEl.innerText = scan.message || 'This card is not registered in the database.';
    metaEl.innerText = `UID: ${scan.uid} \u2022 ${timeStr}`;
    window.playAudioTone('alert');
  } else {
    fullCard.className = 'rfid-panel-fullcard theme-idle';
    badge.innerText = 'NOTICE';
    nameEl.innerText = scan.name;
    subEl.innerText = scan.message;
    metaEl.innerText = `${timeStr}`;
    window.playAudioTone('alert');
  }

  const bar = document.getElementById('resultTimeoutFill');
  if (bar) {
    bar.style.animation = 'none';
    bar.offsetHeight; /* trigger reflow */
    bar.style.animation = 'shrinkTimeout 1.5s linear forwards';
  }

  scanDisplayTimeout = setTimeout(() => {
    window.showIdleTapPrompt();
    scanDisplayTimeout = null;
  }, 1500);
};

window.handleOverlayClick = function(e, modalId) {
  if (e.target.id === modalId) {
    if (modalId === 'timeModal') closeTimeModal();
    else if (modalId === 'settingsModal') closeSettingsModal();
    else if (modalId === 'historyModal') closeHistoryModal();
    else if (modalId === 'firstBootModal') dismissFirstBootModal();
  }
};

document.addEventListener('keydown', (e) => {
  if (e.key === 'Escape') {
    closeTimeModal();
    closeSettingsModal();
    closeHistoryModal();
    dismissFirstBootModal();
  }
});

window.escapeHtml = function(str) {
  if (!str) return '';
  return str.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
};

window.addEventListener('DOMContentLoaded', () => {
  window.initTheme();
  window.autoSyncTime(); // Automatically sync date & time upon opening webapp
  window.pollStatus();
  window.loadAttendanceLog();

  let pollTimer = null;
  function scheduleNextPoll() {
    const isModalOpen = document.getElementById('settingsModal')?.classList.contains('active');
    const delay = isModalOpen ? 1500 : 350;
    pollTimer = setTimeout(async () => {
      await window.pollStatus();
      scheduleNextPoll();
    }, delay);
  }
  scheduleNextPoll();

  const now = new Date();
  now.setMinutes(now.getMinutes() - now.getTimezoneOffset());
  document.getElementById('modalDateTimeInput').value = now.toISOString().slice(0, 16);
  document.getElementById('sessionTitleInput').value = 'Attendance ' + now.toISOString().slice(0, 10);
});
</script>
</body>
</html>
)rawliteral";

// -----------------------------------------------------------------------------
// Web Server Request Handlers
// -----------------------------------------------------------------------------
void handleRoot() {
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "0");
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleStatus() {
  String regStateStr = "idle";
  if (regState == REG_WAITING) regStateStr = "waiting";
  else if (regState == REG_SUCCESS) regStateStr = "success";
  else if (regState == REG_SAME_CARD) regStateStr = "same_card";
  else if (regState == REG_CONFLICT) regStateStr = "conflict";
  else if (regState == REG_TIMEOUT) regStateStr = "timeout";
  else if (regState == REG_CANCELLED) regStateStr = "cancelled";

  String json = "{";
  json += "\"time_set\":" + String(isTimeSet ? "true" : "false") + ",";
  json += "\"current_time\":\"" + getCurrentTimeString() + "\",";
  json += "\"session_active\":" + String(sessionActive ? "true" : "false") + ",";
  json += "\"session_type\":\"" + sessionType + "\",";
  json += "\"session_title\":\"" + escapeJson(sessionTitle) + "\",";
  json += "\"require_registration\":" + String(requireRegistration ? "true" : "false") + ",";
  json += "\"is_settings_open\":" + String(isSettingsOpen ? "true" : "false") + ",";
  json += "\"reg_state\":\"" + regStateStr + "\",";
  json += "\"registered_count\":" + String(registeredCards.size()) + ",";
  json += "\"attendance_count\":" + String(attendanceRecords.size()) + ",";
  json += "\"history_count\":" + String(sessionHistory.size()) + ",";
  json += "\"free_heap\":" + String(ESP.getFreeHeap()) + ",";
  json += "\"total_heap\":" + String(ESP.getHeapSize()) + ",";
  size_t fsUsedBytes = LittleFS.usedBytes();
  size_t fsTotalBytes = LittleFS.totalBytes();
  if (fsTotalBytes == 0) {
    // If LittleFS filesystem not mounted or reports 0, query flash partition table directly
    const esp_partition_t* part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "spiffs");
    if (!part) part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "spiffs");
    if (!part) part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, NULL);
    if (!part) part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_LITTLEFS, NULL);
    if (part) {
      fsTotalBytes = part->size;
    } else {
      fsTotalBytes = 1966080; // 1.875 MB (2MB No-OTA SPIFFS/LittleFS partition)
    }
  }
  size_t fsFreeBytes = (fsTotalBytes > fsUsedBytes) ? (fsTotalBytes - fsUsedBytes) : 0;
  size_t estCardsCap = fsFreeBytes / 96;
  json += "\"fs_used\":" + String(fsUsedBytes) + ",";
  json += "\"fs_total\":" + String(fsTotalBytes) + ",";
  json += "\"fs_free\":" + String(fsFreeBytes) + ",";
  json += "\"est_cards_free\":" + String(estCardsCap) + ",";
  json += "\"wifi_clients\":" + String(WiFi.softAPgetStationNum()) + ",";
  json += "\"wifi_ssid\":\"" + escapeJson(String(AP_SSID)) + "\",";
  json += "\"current_wifi_pass\":\"" + escapeJson(currentApPass) + "\",";
  json += "\"next_wifi_pass\":\"" + escapeJson(nextApPass) + "\",";
  json += "\"wifi_pass_pending\":" + String(currentApPass != nextApPass ? "true" : "false") + ",";
  json += "\"uptime_sec\":" + String(millis() / 1000) + ",";
  json += "\"lcd_timeout\":" + String(lcdStandbyTimeoutMs) + ",";
  json += "\"card_debounce\":" + String(cardDebounceMs) + ",";
  json += "\"show_lcd_wifi\":" + String(showLcdWifiGuide ? "true" : "false") + ",";
  uint8_t lcdPct = (uint8_t)round(((float)lcdActiveBrightness / 255.0f) * 100.0f);
  uint8_t rgbPct = (uint8_t)round(((float)rgbActiveBrightness / (float)RGB_PWM_MAX_LIMIT) * 100.0f);
  json += "\"lcd_brightness\":" + String(lcdActiveBrightness) + ",";
  json += "\"lcd_brightness_pct\":" + String(lcdPct) + ",";
  json += "\"rgb_brightness\":" + String(rgbActiveBrightness) + ",";
  json += "\"rgb_brightness_pct\":" + String(rgbPct) + ",";
  json += "\"lcd_dim_duration\":" + String(lcdDimDurationMs) + ",";
  json += "\"current_lcd_pwm\":" + String(currentLcdPwm) + ",";
  json += "\"lcd_is_dimmed\":" + String(lcdIsDimmed ? "true" : "false") + ",";
  json += "\"last_scan\":{";
  json += "\"uid\":\"" + escapeJson(lastScan.uid) + "\",";
  json += "\"name\":\"" + escapeJson(lastScan.name) + "\",";
  json += "\"time\":\"" + escapeJson(lastScan.timeStr) + "\",";
  json += "\"type\":\"" + escapeJson(lastScan.type) + "\",";
  json += "\"is_duplicate\":" + String(lastScan.isDuplicate ? "true" : "false") + ",";
  json += "\"is_registered\":" + String(lastScan.isRegistered ? "true" : "false") + ",";
  json += "\"message\":\"" + escapeJson(lastScan.message) + "\",";
  json += "\"timestamp\":" + String(lastScan.scanMillis);
  json += "}";
  json += "}";

  server.send(200, "application/json", json);
}

void handleSetTime() {
  wakeLcd();
  if (server.hasArg("epoch")) {
    long epoch = server.arg("epoch").toInt();
    long tz_offset = 0;
    if (server.hasArg("tz_offset")) {
      tz_offset = server.arg("tz_offset").toInt(); // browser offset in minutes
    }
    time_t local_sec = epoch - (tz_offset * 60);
    struct timeval tv = { local_sec, 0 };
    settimeofday(&tv, NULL);
    isTimeSet = true;
    Serial.println("[TIME] Synchronized: " + getCurrentTimeString());
    server.send(200, "application/json", "{\"success\":true,\"message\":\"Time synchronized!\",\"time\":\"" + getCurrentTimeString() + "\"}");
  } else {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"Missing epoch param\"}");
  }
}

void handleStartSession() {
  wakeLcd();
  if (!isTimeSet) {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"Please set system clock first before starting attendance session!\"}");
    return;
  }
  if (server.hasArg("clear_previous") && server.arg("clear_previous") == "1") {
    attendanceRecords.clear();
  }
  if (server.hasArg("type")) {
    String t = server.arg("type");
    if (t == "Exit" || t == "Entrance") {
      sessionType = t;
    }
  }
  if (server.hasArg("title")) {
    String st = server.arg("title");
    st.trim();
    if (st.length() > 0) sessionTitle = st;
  }

  sessionActive = true;
  sessionStartTime = time(nullptr);
  Serial.printf("[SESSION STARTED] Title: '%s' | Mode: %s\n", sessionTitle.c_str(), sessionType.c_str());
  server.send(200, "application/json", "{\"success\":true,\"message\":\"Session started!\",\"type\":\"" + sessionType + "\",\"title\":\"" + escapeJson(sessionTitle) + "\"}");
}

void handleStopSession() {
  wakeLcd();
  sessionActive = false;
  Serial.println("[SESSION STOPPED]");
  server.send(200, "application/json", "{\"success\":true,\"message\":\"Session ended!\"}");
}

void handleToggleMode() {
  wakeLcd();
  if (sessionType == "Entrance") {
    sessionType = "Exit";
  } else {
    sessionType = "Entrance";
  }
  Serial.println("[SESSION MODE TOGGLED] Now: " + sessionType);
  server.send(200, "application/json", "{\"success\":true,\"message\":\"Mode changed!\",\"type\":\"" + sessionType + "\"}");
}

void handleSetMode() {
  wakeLcd();
  String target = "";
  if (server.hasArg("mode")) target = server.arg("mode");
  else if (server.hasArg("type")) target = server.arg("type");

  if (target.equalsIgnoreCase("Exit") || target.equalsIgnoreCase("OUT")) {
    sessionType = "Exit";
  } else if (target.equalsIgnoreCase("Entrance") || target.equalsIgnoreCase("IN")) {
    sessionType = "Entrance";
  } else {
    sessionType = (sessionType == "Entrance") ? "Exit" : "Entrance";
  }
  Serial.println("[SESSION MODE SET] Now: " + sessionType);
  server.send(200, "application/json", "{\"success\":true,\"message\":\"Mode updated!\",\"type\":\"" + sessionType + "\"}");
}

void handleClearSession() {
  wakeLcd();
  attendanceRecords.clear();
  Serial.println("[SESSION ROSTER CLEARED]");
  server.send(200, "application/json", "{\"success\":true,\"message\":\"Attendance log reset!\"}");
}

void handleSettings() {
  if (server.hasArg("require_registration")) {
    requireRegistration = (server.arg("require_registration") == "1");
    Serial.printf("[SETTINGS] Require registration: %s\n", requireRegistration ? "ENABLED" : "DISABLED");
  }
  if (server.hasArg("lcd_timeout")) {
    lcdStandbyTimeoutMs = (unsigned long)server.arg("lcd_timeout").toInt();
    Serial.printf("[SETTINGS] LCD Timeout set to: %lu ms\n", lcdStandbyTimeoutMs);
  }
  if (server.hasArg("lcd_wifi_guide")) {
    showLcdWifiGuide = (server.arg("lcd_wifi_guide") == "1");
  }
  if (server.hasArg("card_debounce")) {
    cardDebounceMs = (unsigned long)server.arg("card_debounce").toInt();
    Serial.printf("[SETTINGS] Card Debounce set to: %lu ms\n", cardDebounceMs);
  }
  if (server.hasArg("lcd_brightness_pct")) {
    int pct = server.arg("lcd_brightness_pct").toInt();
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    lcdActiveBrightness = (uint8_t)round((pct / 100.0) * 255.0);
    applyLcdPwm(lcdActiveBrightness);
    Serial.printf("[SETTINGS] LCD Brightness set to: %u / 255 (%d%%)\n", lcdActiveBrightness, pct);
  } else if (server.hasArg("lcd_brightness")) {
    int b = server.arg("lcd_brightness").toInt();
    if (b < 0) b = 0;
    if (b > 255) b = 255;
    lcdActiveBrightness = (uint8_t)b;
    applyLcdPwm(lcdActiveBrightness);
    Serial.printf("[SETTINGS] LCD Brightness set to: %u / 255\n", lcdActiveBrightness);
  }

  if (server.hasArg("rgb_brightness_pct")) {
    int pct = server.arg("rgb_brightness_pct").toInt();
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    rgbActiveBrightness = (uint8_t)round((pct / 100.0) * (float)RGB_PWM_MAX_LIMIT);
    Serial.printf("[SETTINGS] RGB Brightness set to: %u / 168 (%d%%)\n", rgbActiveBrightness, pct);
  } else if (server.hasArg("rgb_brightness")) {
    int b = server.arg("rgb_brightness").toInt();
    if (b < 0) b = 0;
    if (b > RGB_PWM_MAX_LIMIT) b = RGB_PWM_MAX_LIMIT;
    rgbActiveBrightness = (uint8_t)b;
    Serial.printf("[SETTINGS] RGB Brightness set to: %u / 168\n", rgbActiveBrightness);
  }
  if (server.hasArg("lcd_dim_duration")) {
    lcdDimDurationMs = (unsigned long)server.arg("lcd_dim_duration").toInt();
    Serial.printf("[SETTINGS] LCD Dim Duration set to: %lu ms\n", lcdDimDurationMs);
  }
  saveSystemSettings();
  server.send(200, "application/json", "{\"success\":true,\"require_registration\":" + String(requireRegistration ? "true" : "false") + ",\"lcd_timeout\":" + String(lcdStandbyTimeoutMs) + ",\"card_debounce\":" + String(cardDebounceMs) + ",\"show_lcd_wifi\":" + String(showLcdWifiGuide ? "true" : "false") + "}");
}

void handleSetWifiPassword() {
  wakeLcd();
  if (!server.hasArg("password")) {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"Missing password parameter.\"}");
    return;
  }
  String newPass = server.arg("password");
  newPass.trim();

  // Validate: min 8 characters for WPA2 standard, or empty string for open network
  if (newPass.length() > 0 && newPass.length() < 8) {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"Wi-Fi password must be at least 8 characters for WPA2 (or leave empty for an open network).\"}");
    return;
  }
  if (newPass.length() > 63) {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"Wi-Fi password cannot exceed 63 characters.\"}");
    return;
  }

  // Update in-memory next password
  nextApPass = newPass;

  // MANDATORY: Automatically enable LCD display of Wi-Fi guide
  showLcdWifiGuide = true;

  // Persist to NVS
  saveSystemSettings();

  Serial.printf("[WIFI] New password queued for next boot: '%s'. LCD guide MANDATORY ENABLED.\n", nextApPass.c_str());

  String json = "{";
  json += "\"success\":true,";
  json += "\"current_pass\":\"" + escapeJson(currentApPass) + "\",";
  json += "\"next_pass\":\"" + escapeJson(nextApPass) + "\",";
  json += "\"show_lcd_wifi\":true,";
  json += "\"is_pending\":" + String(currentApPass != nextApPass ? "true" : "false") + ",";
  json += "\"message\":\"Wi-Fi password saved! It will take effect on next boot. LCD password display has been automatically enabled.\"";
  json += "}";

  server.send(200, "application/json", json);
}

void handleLcdSleep() {
  sleepLcd();
  server.send(200, "application/json", "{\"success\":true,\"message\":\"LCD backlight turned OFF.\"}");
}

void handleLcdWake() {
  wakeLcd();
  server.send(200, "application/json", "{\"success\":true,\"message\":\"LCD backlight awakened.\"}");
}

void handleLcdBrightness() {
  wakeLcd();
  int b = -1;
  if (server.hasArg("pct")) {
    int pct = server.arg("pct").toInt();
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    b = (int)round((pct / 100.0) * 255.0);
  } else if (server.hasArg("brightness")) {
    b = server.arg("brightness").toInt();
  }
  if (b >= 0) {
    if (b > 255) b = 255;
    lcdActiveBrightness = (uint8_t)b;
    applyLcdPwm(lcdActiveBrightness);
    saveSystemSettings();
    uint8_t pct = (uint8_t)round(((float)lcdActiveBrightness / 255.0f) * 100.0f);
    Serial.printf("[LCD] Live brightness set: %u / 255 (%u%%)\n", lcdActiveBrightness, pct);
    server.send(200, "application/json", "{\"success\":true,\"brightness\":" + String(lcdActiveBrightness) + ",\"pct\":" + String(pct) + "}");
    return;
  }
  server.send(400, "application/json", "{\"success\":false,\"message\":\"Missing brightness or pct parameter\"}");
}

void handleRgbBrightness() {
  int b = -1;
  if (server.hasArg("pct")) {
    int pct = server.arg("pct").toInt();
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    b = (int)round((pct / 100.0) * (float)RGB_PWM_MAX_LIMIT);
  } else if (server.hasArg("brightness")) {
    b = server.arg("brightness").toInt();
  }
  if (b >= 0) {
    if (b > RGB_PWM_MAX_LIMIT) b = RGB_PWM_MAX_LIMIT;
    rgbActiveBrightness = (uint8_t)b;
    saveSystemSettings();
    // Test light briefly at new brightness so user gets visual feedback
    setRgbColor(255, 255, 255);
    triggerRgbFeedback(RGB_FEEDBACK_WHITE_BLINK_TEST);
    uint8_t pct = (uint8_t)round(((float)rgbActiveBrightness / (float)RGB_PWM_MAX_LIMIT) * 100.0f);
    Serial.printf("[RGB] Live brightness set: %u / 168 (%u%%)\n", rgbActiveBrightness, pct);
    server.send(200, "application/json", "{\"success\":true,\"brightness\":" + String(rgbActiveBrightness) + ",\"pct\":" + String(pct) + "}");
    return;
  }
  server.send(400, "application/json", "{\"success\":false,\"message\":\"Missing brightness or pct parameter\"}");
}

void handleLcdTest() {
  wakeLcd();
  if (!lcdAvailable) {
    initLcd();
  }
  if (lcdAvailable) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(centerText("ESP32 LCD TEST"));
    lcd.setCursor(0, 1);
    lcd.print(centerText("GPIO 14 PWM 100%"));
    lcdMessageUntil = millis() + 4000;
  }
  // Visibly pulse GPIO 14 PWM backlight
  for (int p = 0; p < 2; p++) {
    applyLcdPwm(30);
    delay(200);
    applyLcdPwm(255);
    delay(250);
  }
  applyLcdPwm(lcdActiveBrightness);
  server.send(200, "application/json", "{\"success\":true,\"message\":\"Display tested: Backlight pulsed on GPIO 14.\"}");
}

void handleRgbTest() {
  wakeLcd();
  String mode = server.hasArg("mode") ? server.arg("mode") : "recorded";
  if (mode == "recorded" || mode == "green") {
    triggerRgbFeedback(RGB_FEEDBACK_RECORDED);
    server.send(200, "application/json", "{\"success\":true,\"message\":\"Testing RGB: Green (Recorded 1.5s)\"}");
  } else if (mode == "duplicate" || mode == "yellow") {
    triggerRgbFeedback(RGB_FEEDBACK_DUPLICATE);
    server.send(200, "application/json", "{\"success\":true,\"message\":\"Testing RGB: Yellow (Duplicate 1.5s)\"}");
  } else if (mode == "rejected" || mode == "red" || mode == "denied") {
    triggerRgbFeedback(RGB_FEEDBACK_REJECTED);
    server.send(200, "application/json", "{\"success\":true,\"message\":\"Testing RGB: Red (Denied 1.5s)\"}");
  } else if (mode == "white" || mode == "blink") {
    triggerRgbFeedback(RGB_FEEDBACK_WHITE_BLINK_TEST);
    server.send(200, "application/json", "{\"success\":true,\"message\":\"Testing RGB: White Light (1.5s)\"}");
  } else if (mode == "off") {
    currentRgbFeedback = RGB_FEEDBACK_NONE;
    turnOffRgbLed();
    server.send(200, "application/json", "{\"success\":true,\"message\":\"RGB LED turned OFF\"}");
  } else {
    currentRgbFeedback = RGB_FEEDBACK_NONE;
    turnOffRgbLed();
    server.send(200, "application/json", "{\"success\":true,\"message\":\"RGB feedback reset.\"}");
  }
}

void handleSystemReboot() {
  Serial.println("[SYSTEM] Soft reboot requested by web interface.");
  server.send(200, "application/json", "{\"success\":true,\"message\":\"ESP32 is restarting now...\"}");
  delay(600);
  ESP.restart();
}

void handleDeviceReset() {
  Serial.println("[SYSTEM] Device Reset requested. Erasing all data & restoring factory defaults...");

  // 1. Erase all registered cards
  registeredCards.clear();
  if (LittleFS.exists("/cards.txt")) LittleFS.remove("/cards.txt");
  if (LittleFS.exists("/cards.json")) LittleFS.remove("/cards.json");
  if (LittleFS.exists("/cards.csv")) LittleFS.remove("/cards.csv");

  // 2. Erase all session history
  for (const auto& item : sessionHistory) {
    String p = "/" + item.id + ".txt";
    if (LittleFS.exists(p.c_str())) LittleFS.remove(p.c_str());
  }
  sessionHistory.clear();
  if (LittleFS.exists("/history_idx.txt")) LittleFS.remove("/history_idx.txt");

  // 3. Clear active session & records
  attendanceRecords.clear();
  sessionActive = false;
  sessionTitle = "";

  // 4. Wipe flash preferences
  prefs.begin("rfid_att", false);
  prefs.clear();
  prefs.end();
  prefs.begin("rfid_sys", false);
  prefs.clear();
  prefs.end();

  // 5. Restore default runtime settings
  lcdActiveBrightness = 255;
  lcdDimBrightness = 76;
  lcdStandbyTimeoutMs = 15000;
  lcdDimDurationMs = 5000;
  lcdManualSleep = false;
  rgbActiveBrightness = RGB_PWM_MAX_LIMIT;

  wakeLcd();
  if (lcdAvailable) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(centerText("DEVICE RESET"));
    lcd.setCursor(0, 1);
    lcd.print(centerText("RESTARTING..."));
  }

  server.send(200, "application/json", "{\"success\":true,\"message\":\"Device reset complete. Restoring factory defaults and rebooting...\"}");
  delay(1000);
  ESP.restart();
}

void handleExportSystemBackup() {
  String json = "{\r\n";
  json += "  \"backup_version\": \"2.0\",\r\n";
  json += "  \"created_at\": \"" + getCurrentDateString() + " " + getCurrentTimeString() + "\",\r\n";
  json += "  \"system_info\": {\r\n";
  json += "    \"require_registration\": " + String(requireRegistration ? "true" : "false") + ",\r\n";
  json += "    \"session_type\": \"" + escapeJson(sessionType) + "\",\r\n";
  json += "    \"session_title\": \"" + escapeJson(sessionTitle) + "\"\r\n";
  json += "  },\r\n";
  json += "  \"registered_cards\": [\r\n";
  for (size_t i = 0; i < registeredCards.size(); i++) {
    const auto& c = registeredCards[i];
    if (i > 0) json += ",\r\n";
    json += "    {\"uid\":\"" + escapeJson(c.uid) + "\",\"name\":\"" + escapeJson(c.name) + "\",\"student_id\":\"" + escapeJson(c.studentId) + "\",\"reg_date\":\"" + escapeJson(c.regDate) + "\"}";
  }
  json += "\r\n  ],\r\n";
  json += "  \"saved_sessions\": [\r\n";
  for (size_t i = 0; i < sessionHistory.size(); i++) {
    const auto& s = sessionHistory[i];
    if (i > 0) json += ",\r\n";
    json += "    {\"id\":\"" + escapeJson(s.id) + "\",\"title\":\"" + escapeJson(s.title) + "\",\"type\":\"" + escapeJson(s.type) + "\",\"date\":\"" + escapeJson(s.dateStr) + "\",\"save_time\":\"" + escapeJson(s.saveTime) + "\",\"attendees\":" + String(s.attendeeCount) + "}";
  }
  json += "\r\n  ]\r\n";
  json += "}\r\n";

  String fname = "attendance_backup_" + sanitizeFilename(getCurrentDateString()) + ".json";
  server.sendHeader("Content-Disposition", "attachment; filename=\"" + fname + "\"");
  server.send(200, "application/json", json);
}

void handleImportCards() {
  wakeLcd();
  if (!server.hasArg("data")) {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"Missing card data\"}");
    return;
  }
  String data = server.arg("data");
  int importedCount = 0;
  int skippedCount = 0;

  int startIdx = 0;
  while (startIdx < data.length()) {
    int nextNewline = data.indexOf('\n', startIdx);
    String line;
    if (nextNewline == -1) {
      line = data.substring(startIdx);
      startIdx = data.length();
    } else {
      line = data.substring(startIdx, nextNewline);
      startIdx = nextNewline + 1;
    }
    line.trim();
    if (line.length() == 0 || line.startsWith("#") || line.startsWith("Index") || line.startsWith("Owner Name")) continue;

    char delim = (line.indexOf('\t') != -1) ? '\t' : ',';
    int p1 = line.indexOf(delim);
    if (p1 == -1) continue;
    int p2 = line.indexOf(delim, p1 + 1);

    String name = "";
    String sId = "";
    String uid = "";

    if (p2 == -1) {
      name = sanitizeField(line.substring(0, p1));
      sId = sanitizeField(line.substring(p1 + 1));
    } else {
      int p3 = line.indexOf(delim, p2 + 1);
      String f0 = line.substring(0, p1);
      f0.trim();
      bool firstIsNum = true;
      for (size_t k = 0; k < f0.length(); k++) {
        if (!isDigit(f0[k])) { firstIsNum = false; break; }
      }

      if (firstIsNum && p3 != -1) {
        name = sanitizeField(line.substring(p1 + 1, p2));
        sId = sanitizeField(line.substring(p2 + 1, p3));
        int p4 = line.indexOf(delim, p3 + 1);
        uid = sanitizeField(p4 != -1 ? line.substring(p3 + 1, p4) : line.substring(p3 + 1));
      } else {
        name = sanitizeField(line.substring(0, p1));
        sId = sanitizeField(line.substring(p1 + 1, p2));
        uid = sanitizeField(p3 != -1 ? line.substring(p2 + 1, p3) : line.substring(p2 + 1));
      }
    }

    if (name.length() == 0) continue;
    uid.toUpperCase();

    if (uid.length() > 0) {
      if (isCardRegistered(uid)) {
        skippedCount++;
        continue;
      }
      RegisteredCard nc;
      nc.uid = uid;
      nc.name = name;
      nc.studentId = sId;
      nc.regDate = getCurrentDateString();
      registeredCards.push_back(nc);
      importedCount++;
    }
  }

  if (importedCount > 0) {
    saveRegisteredCards();
  }

  server.send(200, "application/json", "{\"success\":true,\"imported\":" + String(importedCount) + ",\"skipped\":" + String(skippedCount) + ",\"message\":\"Imported " + String(importedCount) + " cards (" + String(skippedCount) + " skipped duplicate UIDs).\"}");
}

void handleSettingsState() {
  if (server.hasArg("open")) {
    isSettingsOpen = (server.arg("open") == "1");
  }
  if (server.hasArg("tab")) {
    String tab = server.arg("tab");
    isRegistrationTab = (tab == "notion-reg-single" || tab == "notion-reg-batch" || tab == "set-register" || tab == "set-batch");
  } else if (!isSettingsOpen) {
    isRegistrationTab = false;
  }
  Serial.printf("[SETTINGS] Modal: %s | RegMode: %s\n",
                isSettingsOpen ? "OPEN" : "CLOSED",
                isRegistrationTab ? "ACTIVE" : "OFF");
  server.send(200, "application/json", "{\"success\":true,\"is_settings_open\":" + String(isSettingsOpen ? "true" : "false") + ",\"is_registration_tab\":" + String(isRegistrationTab ? "true" : "false") + "}");
}

void handleGetAttendance() {
  String json = "[";
  json.reserve(attendanceRecords.size() * 128 + 16);
  for (size_t i = 0; i < attendanceRecords.size(); i++) {
    if (i > 0) json += ",";
    json += "{";
    json += "\"index\":" + String(i + 1) + ",";
    json += "\"uid\":\"" + escapeJson(attendanceRecords[i].uid) + "\",";
    json += "\"name\":\"" + escapeJson(attendanceRecords[i].name) + "\",";
    json += "\"student_id\":\"" + escapeJson(attendanceRecords[i].studentId) + "\",";
    json += "\"time_in\":\"" + escapeJson(attendanceRecords[i].inTime) + "\",";
    json += "\"time_out\":\"" + escapeJson(attendanceRecords[i].outTime) + "\"";
    json += "}";
  }
  json += "]";
  server.send(200, "application/json", json);
}


void handleSaveSession() {
  wakeLcd();
  if (attendanceRecords.empty()) {
    server.send(200, "application/json", "{\"success\":false,\"message\":\"No attendance records to save in this session.\"}");
    return;
  }
  String sessId = saveCurrentSessionToHistory();
  server.send(200, "application/json", "{\"success\":true,\"id\":\"" + sessId + "\",\"message\":\"Session saved to history successfully!\"}");
}

void handleGetHistory() {
  String json = "[";
  json.reserve(sessionHistory.size() * 140 + 16);
  for (size_t i = 0; i < sessionHistory.size(); i++) {
    if (i > 0) json += ",";
    json += "{";
    json += "\"id\":\"" + escapeJson(sessionHistory[i].id) + "\",";
    json += "\"title\":\"" + escapeJson(sessionHistory[i].title) + "\",";
    json += "\"type\":\"" + escapeJson(sessionHistory[i].type) + "\",";
    json += "\"date\":\"" + escapeJson(sessionHistory[i].dateStr) + "\",";
    json += "\"save_time\":\"" + escapeJson(sessionHistory[i].saveTime) + "\",";
    json += "\"attendee_count\":" + String(sessionHistory[i].attendeeCount);
    json += "}";
  }
  json += "]";
  server.send(200, "application/json", json);
}

void handleGetHistorySession() {
  if (!server.hasArg("id")) {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"Missing session id\"}");
    return;
  }
  String id = server.arg("id");
  SavedSessionItem* targetItem = nullptr;
  for (auto& item : sessionHistory) {
    if (item.id == id) {
      targetItem = &item;
      break;
    }
  }
  if (!targetItem) {
    server.send(404, "application/json", "{\"success\":false,\"message\":\"Session not found\"}");
    return;
  }

  String json = "{";
  json += "\"success\":true,";
  json += "\"id\":\"" + escapeJson(targetItem->id) + "\",";
  json += "\"title\":\"" + escapeJson(targetItem->title) + "\",";
  json += "\"type\":\"" + escapeJson(targetItem->type) + "\",";
  json += "\"date\":\"" + escapeJson(targetItem->dateStr) + "\",";
  json += "\"save_time\":\"" + escapeJson(targetItem->saveTime) + "\",";
  json += "\"attendee_count\":" + String(targetItem->attendeeCount) + ",";
  json += "\"records\":[";

  String path = "/" + id + ".txt";
  if (LittleFS.exists(path.c_str())) {
    File f = LittleFS.open(path.c_str(), "r");
    bool first = true;
    while (f.available()) {
      String line = f.readStringUntil('\n');
      line.trim();
      if (line.length() == 0) continue;
      int t1 = line.indexOf('\t');
      int t2 = line.indexOf('\t', t1 + 1);
      int t3 = line.indexOf('\t', t2 + 1);
      int t4 = line.indexOf('\t', t3 + 1);
      int t5 = line.indexOf('\t', t4 + 1);
      if (t1 != -1 && t2 != -1 && t3 != -1 && t4 != -1) {
        if (!first) json += ",";
        first = false;
        json += "{";
        json += "\"name\":\"" + escapeJson(line.substring(0, t1)) + "\",";
        json += "\"student_id\":\"" + escapeJson(line.substring(t1 + 1, t2)) + "\",";
        json += "\"uid\":\"" + escapeJson(line.substring(t2 + 1, t3)) + "\",";
        json += "\"time_in\":\"" + escapeJson(line.substring(t3 + 1, t4)) + "\",";
        if (t5 != -1) {
          json += "\"time_out\":\"" + escapeJson(line.substring(t4 + 1, t5)) + "\",";
          json += "\"status\":\"" + escapeJson(line.substring(t5 + 1)) + "\"";
        } else {
          json += "\"time_out\":\"" + escapeJson(line.substring(t4 + 1)) + "\",";
          json += "\"status\":\"Checked In\"";
        }
        json += "}";
      }
    }
    f.close();
  }
  json += "]}";
  server.send(200, "application/json", json);
}

void handleExportHistoryCsv() {
  if (!server.hasArg("id")) {
    server.send(400, "text/plain", "Missing session id");
    return;
  }
  String id = server.arg("id");
  SavedSessionItem* targetItem = nullptr;
  for (auto& item : sessionHistory) {
    if (item.id == id) {
      targetItem = &item;
      break;
    }
  }

  String sTitle = targetItem ? targetItem->title : "Session";
  String csv = "Index,Attendee Name,ID Number,Card UID,Time IN,Time OUT,Status,Session Title\r\n";
  String path = "/" + id + ".txt";

  if (LittleFS.exists(path.c_str())) {
    File f = LittleFS.open(path.c_str(), "r");
    int idx = 1;
    while (f.available()) {
      String line = f.readStringUntil('\n');
      line.trim();
      if (line.length() == 0) continue;
      int t1 = line.indexOf('\t');
      int t2 = line.indexOf('\t', t1 + 1);
      int t3 = line.indexOf('\t', t2 + 1);
      int t4 = line.indexOf('\t', t3 + 1);
      int t5 = line.indexOf('\t', t4 + 1);
      if (t1 != -1 && t2 != -1 && t3 != -1 && t4 != -1) {
        String name = line.substring(0, t1);
        String sId = line.substring(t1 + 1, t2);
        String uid = line.substring(t2 + 1, t3);
        String inTime = line.substring(t3 + 1, t4);
        String outTime = (t5 != -1) ? line.substring(t4 + 1, t5) : line.substring(t4 + 1);
        String status = (t5 != -1) ? line.substring(t5 + 1) : "Checked In";

        csv += String(idx++) + ",";
        csv += "\"" + escapeCsv(name) + "\",";
        csv += "\"" + escapeCsv(sId) + "\",";
        csv += "\"" + escapeCsv(uid) + "\",";
        csv += "\"" + escapeCsv(inTime) + "\",";
        csv += "\"" + escapeCsv(outTime) + "\",";
        csv += "\"" + escapeCsv(status) + "\",";
        csv += "\"" + escapeCsv(sTitle) + "\"\r\n";
      }
    }
    f.close();
  }

  String filename = sanitizeFilename(sTitle) + ".csv";
  server.sendHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");
  server.send(200, "text/csv", csv);
}

void handleDeleteHistorySession() {
  if (!server.hasArg("id")) {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"Missing id\"}");
    return;
  }
  String id = server.arg("id");
  String path = "/" + id + ".txt";
  if (LittleFS.exists(path.c_str())) {
    LittleFS.remove(path.c_str());
  }
  for (size_t i = 0; i < sessionHistory.size(); i++) {
    if (sessionHistory[i].id == id) {
      sessionHistory.erase(sessionHistory.begin() + i);
      break;
    }
  }
  saveHistoryIndex();
  server.send(200, "application/json", "{\"success\":true,\"message\":\"Session deleted from history.\"}");
}

void handleResumeHistorySession() {
  wakeLcd();
  if (!server.hasArg("id")) {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"Missing session id\"}");
    return;
  }
  String id = server.arg("id");
  id.trim();

  SavedSessionItem* targetItem = nullptr;
  size_t targetIdx = 0;
  for (size_t i = 0; i < sessionHistory.size(); i++) {
    if (sessionHistory[i].id == id || sessionHistory[i].id.equalsIgnoreCase(id)) {
      targetItem = &sessionHistory[i];
      targetIdx = i;
      break;
    }
  }
  if (!targetItem) {
    server.send(404, "application/json", "{\"success\":false,\"message\":\"Session not found\"}");
    return;
  }

  // Restore live session state
  sessionActive = true;
  sessionStartTime = time(nullptr);
  sessionTitle = targetItem->title;
  sessionType = targetItem->type;
  attendanceRecords.clear();

  // Read saved attendees from session file
  String path = "/" + targetItem->id + ".txt";
  if (!LittleFS.exists(path.c_str())) {
    path = targetItem->id + ".txt";
  }
  if (!LittleFS.exists(path.c_str())) {
    path = "/" + id + ".txt";
  }

  if (LittleFS.exists(path.c_str())) {
    File f = LittleFS.open(path.c_str(), "r");
    if (f) {
      while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;

        char delim = '\t';
        if (line.indexOf('\t') == -1 && line.indexOf(',') != -1) {
          delim = ',';
        }
        int t1 = line.indexOf(delim);
        int t2 = line.indexOf(delim, t1 + 1);
        int t3 = line.indexOf(delim, t2 + 1);
        int t4 = line.indexOf(delim, t3 + 1);
        int t5 = line.indexOf(delim, t4 + 1);

        if (t1 != -1) {
          AttendanceRecord rec;
          rec.name = line.substring(0, t1);
          rec.studentId = (t2 != -1) ? line.substring(t1 + 1, t2) : "";
          rec.uid = (t2 != -1 && t3 != -1) ? line.substring(t2 + 1, t3) : ((t2 != -1) ? line.substring(t2 + 1) : "");
          rec.inTime = (t3 != -1 && t4 != -1) ? line.substring(t3 + 1, t4) : ((t3 != -1) ? line.substring(t3 + 1) : "--");
          if (t4 != -1 && t5 != -1) {
            rec.outTime = line.substring(t4 + 1, t5);
          } else if (t4 != -1) {
            rec.outTime = line.substring(t4 + 1);
          } else {
            rec.outTime = "--";
          }
          rec.name.trim();
          rec.studentId.trim();
          rec.uid.trim();
          rec.inTime.trim();
          rec.outTime.trim();
          if (rec.inTime.length() == 0) rec.inTime = "--";
          if (rec.outTime.length() == 0) rec.outTime = "--";
          rec.inTimestamp = (time_t)0;
          rec.outTimestamp = (time_t)0;

          if (rec.name.length() > 0 || rec.uid.length() > 0) {
            attendanceRecords.push_back(rec);
          }
        }
      }
      f.close();
    }
  }

  if (lcdAvailable) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(centerText("SESSION RESUMED"));
    lcd.setCursor(0, 1);
    lcd.print(centerText(formatLcdName(sessionTitle)));
    lcdMessageUntil = millis() + 2500;
  }

  Serial.printf("[SESSION] Resumed session '%s' with %d attendees (from %s)\n",
                sessionTitle.c_str(), (int)attendanceRecords.size(), path.c_str());

  // Return full restored session data and records in response
  String json = "{";
  json += "\"success\":true,";
  json += "\"id\":\"" + escapeJson(targetItem->id) + "\",";
  json += "\"title\":\"" + escapeJson(sessionTitle) + "\",";
  json += "\"type\":\"" + escapeJson(sessionType) + "\",";
  json += "\"attendee_count\":" + String(attendanceRecords.size()) + ",";
  json += "\"records\":[";
  for (size_t i = 0; i < attendanceRecords.size(); i++) {
    if (i > 0) json += ",";
    json += "{";
    json += "\"index\":" + String(i + 1) + ",";
    json += "\"uid\":\"" + escapeJson(attendanceRecords[i].uid) + "\",";
    json += "\"name\":\"" + escapeJson(attendanceRecords[i].name) + "\",";
    json += "\"student_id\":\"" + escapeJson(attendanceRecords[i].studentId) + "\",";
    json += "\"time_in\":\"" + escapeJson(attendanceRecords[i].inTime) + "\",";
    json += "\"time_out\":\"" + escapeJson(attendanceRecords[i].outTime) + "\"";
    json += "}";
  }
  json += "],";
  json += "\"message\":\"Session resumed with " + String(attendanceRecords.size()) + " attendees.\"}";

  server.send(200, "application/json", json);
}

void handleClearAllHistory() {
  for (const auto& item : sessionHistory) {
    String path = "/" + item.id + ".txt";
    if (LittleFS.exists(path.c_str())) LittleFS.remove(path.c_str());
  }
  sessionHistory.clear();
  if (LittleFS.exists("/history_idx.txt")) LittleFS.remove("/history_idx.txt");
  server.send(200, "application/json", "{\"success\":true,\"message\":\"All session history cleared.\"}");
}

void handleExportCsv() {
  String csv = "Index,Attendee Name,ID Number,Card UID,Time IN,Time OUT,Status,Session Title\r\n";
  for (size_t i = 0; i < attendanceRecords.size(); i++) {
    const auto& r = attendanceRecords[i];
    String status = "Checked In";
    if (r.inTime.length() > 0 && r.inTime != "--" && r.outTime.length() > 0 && r.outTime != "--") {
      status = "Completed";
    } else if (r.outTime.length() > 0 && r.outTime != "--") {
      status = "Checked Out";
    }
    csv += String(i + 1) + ",";
    csv += "\"" + escapeCsv(r.name) + "\",";
    csv += "\"" + escapeCsv(r.studentId) + "\",";
    csv += "\"" + escapeCsv(r.uid) + "\",";
    csv += "\"" + escapeCsv(r.inTime) + "\",";
    csv += "\"" + escapeCsv(r.outTime) + "\",";
    csv += "\"" + escapeCsv(status) + "\",";
    csv += "\"" + escapeCsv(sessionTitle) + "\"\r\n";
  }

  String filename = sanitizeFilename(sessionTitle) + ".csv";
  server.sendHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");
  server.send(200, "text/csv", csv);
}

void handleRegisterStart() {
  wakeLcd();
  String name = "";
  if (server.hasArg("name")) {
    name = server.arg("name");
  }
  name.trim();
  if (name.length() == 0) {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"Name cannot be empty\"}");
    return;
  }
  regCandidateName = name;
  regCandidateId = server.hasArg("student_id") ? server.arg("student_id") : "";
  regCandidateId.trim();
  regBatchInfo = server.hasArg("batch_info") ? server.arg("batch_info") : "";
  regBatchInfo.trim();

  regConflictName = "";
  regConflictId = "";
  regState = REG_WAITING;
  regStartMillis = millis();
  regResultUid = "";
  regResultMsg = "";
  isRegistrationTab = true;

  Serial.printf("[REGISTRATION] Waiting for RFID tag tap for owner '%s' (ID: %s | Batch: %s)...\n",
                regCandidateName.c_str(), regCandidateId.c_str(), regBatchInfo.c_str());
  server.send(200, "application/json", "{\"success\":true,\"message\":\"Ready! Tap RFID card to reader now.\"}");
}

void handleRegisterStatus() {
  String st = "idle";
  unsigned long remaining = 0;
  if (regState == REG_WAITING) {
    st = "waiting";
    unsigned long elapsed = millis() - regStartMillis;
    if (elapsed < REG_TIMEOUT_MS) remaining = REG_TIMEOUT_MS - elapsed;
  } else if (regState == REG_SUCCESS) st = "success";
  else if (regState == REG_SAME_CARD) st = "same_card";
  else if (regState == REG_CONFLICT) st = "conflict";
  else if (regState == REG_TIMEOUT) st = "timeout";
  else if (regState == REG_CANCELLED) st = "cancelled";

  String json = "{";
  json += "\"state\":\"" + st + "\",";
  json += "\"remaining_ms\":" + String(remaining) + ",";
  json += "\"result_uid\":\"" + escapeJson(regResultUid) + "\",";
  json += "\"conflict_name\":\"" + escapeJson(regConflictName) + "\",";
  json += "\"conflict_id\":\"" + escapeJson(regConflictId) + "\",";
  json += "\"message\":\"" + escapeJson(regResultMsg) + "\"";
  json += "}";
  server.send(200, "application/json", json);
}

void handleRegisterCancel() {
  wakeLcd();
  regState = REG_CANCELLED;
  Serial.println("[REGISTRATION] Cancelled by user.");
  server.send(200, "application/json", "{\"success\":true,\"message\":\"Registration cancelled\"}");
}

void handleGetCards() {
  String json = "[";
  json.reserve(registeredCards.size() * 96 + 16);
  for (size_t i = 0; i < registeredCards.size(); i++) {
    if (i > 0) json += ",";
    json += "{";
    json += "\"uid\":\"" + escapeJson(registeredCards[i].uid) + "\",";
    json += "\"name\":\"" + escapeJson(registeredCards[i].name) + "\",";
    json += "\"student_id\":\"" + escapeJson(registeredCards[i].studentId) + "\",";
    json += "\"reg_date\":\"" + escapeJson(registeredCards[i].regDate) + "\"";
    json += "}";
  }
  json += "]";
  server.send(200, "application/json", json);
}

void handleDeleteCard() {
  if (!server.hasArg("uid")) {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"Missing UID parameter\"}");
    return;
  }
  String uid = server.arg("uid");
  uid.trim();
  bool found = false;
  for (auto it = registeredCards.begin(); it != registeredCards.end(); ++it) {
    if (it->uid.equalsIgnoreCase(uid)) {
      registeredCards.erase(it);
      found = true;
      break;
    }
  }
  if (found) {
    saveRegisteredCards();
    server.send(200, "application/json", "{\"success\":true,\"message\":\"Card deleted\"}");
  } else {
    server.send(404, "application/json", "{\"success\":false,\"message\":\"Card UID not found\"}");
  }
}

void handleClearCards() {
  registeredCards.clear();
  saveRegisteredCards();
  Serial.println("[REGISTRATION] All cards cleared.");
  server.send(200, "application/json", "{\"success\":true,\"message\":\"All registered cards deleted\"}");
}

void handleExportCardsCsv() {
  String csv = "Index,Owner Name,ID Number,Card UID,Registered Date\r\n";
  for (size_t i = 0; i < registeredCards.size(); i++) {
    const auto& c = registeredCards[i];
    csv += String(i + 1) + ",";
    csv += "\"" + escapeCsv(c.name) + "\",";
    csv += "\"" + escapeCsv(c.studentId) + "\",";
    csv += "\"" + escapeCsv(c.uid) + "\",";
    csv += "\"" + escapeCsv(c.regDate) + "\"\r\n";
  }
  server.sendHeader("Content-Disposition", "attachment; filename=\"registered_cards.csv\"");
  server.send(200, "text/csv", csv);
}

void handleCaptivePortalRedirect() {
  server.sendHeader("Location", "http://192.168.4.1/", true);
  server.send(302, "text/plain", "Redirecting to Attendance Hub...");
}

void handleNotFound() {
  String host = server.hostHeader();
  if (host != "192.168.4.1" && host != "192.168.4.1:80") {
    handleCaptivePortalRedirect();
    return;
  }
  server.send(404, "text/plain", "Not Found");
}

// -----------------------------------------------------------------------------
// SETUP
// -----------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(400);
  Serial.println("\n\n========================================================");
  Serial.println("  ESP32 + RC522 RFID + I2C LCD Attendance System       ");
  Serial.println("========================================================");

  // Status LED
  pinMode(STATUS_LED, OUTPUT);
  digitalWrite(STATUS_LED, LOW);

  // Initialize Common Anode RGB LED
  initRgbLed();

  // Initialize I2C 16x2 LCD
  initLcd();

  // Initialize SPI & RC522 (Reset pin is GPIO 4)
  SPI.begin();
  rfid.PCD_Init();
  delay(50);
  byte v = rfid.PCD_ReadRegister(rfid.VersionReg);
  Serial.printf("[RFID] RC522 Reader Version: 0x%02X\n", v);
  if (v == 0x00 || v == 0xFF) {
    Serial.println("[RFID WARNING] Reader communication failed! Check 3.3V, GND, and SPI wiring.");
  } else {
    Serial.println("[RFID] RC522 Reader initialized and ready.");
    rfid.PCD_SetAntennaGain(MFRC522::RxGain_max); // Maximize RF antenna gain
  }

  // Initialize LittleFS Flash Storage (mount or auto-format on first use)
  bool fsMounted = LittleFS.begin(false);
  if (!fsMounted) {
    Serial.println("[FS] Initial mount failed. Formatting LittleFS partition...");
    LittleFS.format();
    fsMounted = LittleFS.begin(true);
  }
  if (!fsMounted) {
    Serial.println("[FS] Retrying mount with explicit partition label 'spiffs'...");
    fsMounted = LittleFS.begin(true, "/littlefs", 10, "spiffs");
  }

  if (fsMounted) {
    Serial.printf("[FS] LittleFS Mounted Successfully! Total: %u bytes, Used: %u bytes\n",
                  (unsigned int)LittleFS.totalBytes(), (unsigned int)LittleFS.usedBytes());
    loadHistoryIndex();
  } else {
    Serial.println("[FS ERROR] LittleFS Mount Failed completely!");
  }
  loadRegisteredCards();

  // Load System Settings from NVS (including active Wi-Fi pass)
  loadSystemSettings();

  // Configure Wi-Fi Access Point (supports up to 8 stations)
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(AP_IP, AP_IP, AP_NETMASK);
  if (currentApPass.length() >= 8) {
    WiFi.softAP(AP_SSID, currentApPass.c_str(), 1, 0, 8);
  } else {
    WiFi.softAP(AP_SSID, "", 1, 0, 8); // Open network if pass < 8 chars
  }
  Serial.printf("[WIFI] Hotspot Started: SSID: '%s' | Active Password: '%s'\n", AP_SSID, (currentApPass.length() >= 8 ? currentApPass.c_str() : "<Open>"));
  Serial.println("[WIFI] IP Address: http://192.168.4.1");

  // Captive Portal DNS Server
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(DNS_PORT, "*", AP_IP);
  Serial.println("[DNS] Captive Portal DNS active on port 53.");

  // Web Server Routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/time", HTTP_POST, handleSetTime);
  server.on("/api/session/start", HTTP_POST, handleStartSession);
  server.on("/api/session/stop", HTTP_POST, handleStopSession);
  server.on("/api/session/toggle_mode", HTTP_POST, handleToggleMode);
  server.on("/api/session/set_mode", HTTP_POST, handleSetMode);
  server.on("/api/session/set_mode", HTTP_GET, handleSetMode);
  server.on("/api/session/clear", HTTP_POST, handleClearSession);
  server.on("/api/session/save", HTTP_POST, handleSaveSession);
  server.on("/api/session/save", HTTP_GET, handleSaveSession);
  server.on("/api/history", HTTP_GET, handleGetHistory);
  server.on("/api/history/session", HTTP_GET, handleGetHistorySession);
  server.on("/api/history/export.csv", HTTP_GET, handleExportHistoryCsv);
  server.on("/api/history/resume", HTTP_POST, handleResumeHistorySession);
  server.on("/api/history/delete", HTTP_POST, handleDeleteHistorySession);
  server.on("/api/history/clear", HTTP_POST, handleClearAllHistory);
  server.on("/api/settings", HTTP_POST, handleSettings);
  server.on("/api/settings/state", HTTP_POST, handleSettingsState);
  server.on("/api/wifi/password", HTTP_POST, handleSetWifiPassword);
  server.on("/api/attendance", HTTP_GET, handleGetAttendance);
  server.on("/api/attendance/export.csv", HTTP_GET, handleExportCsv);
  server.on("/api/register/start", HTTP_POST, handleRegisterStart);
  server.on("/api/register/status", HTTP_GET, handleRegisterStatus);
  server.on("/api/register/cancel", HTTP_POST, handleRegisterCancel);
  server.on("/api/cards", HTTP_GET, handleGetCards);
  server.on("/api/cards/delete", HTTP_POST, handleDeleteCard);
  server.on("/api/cards/clear", HTTP_POST, handleClearCards);
  server.on("/api/cards/export.csv", HTTP_GET, handleExportCardsCsv);
  server.on("/api/cards/import", HTTP_POST, handleImportCards);
  server.on("/api/lcd/sleep", HTTP_POST, handleLcdSleep);
  server.on("/api/lcd/wake", HTTP_POST, handleLcdWake);
  server.on("/api/lcd/test", HTTP_POST, handleLcdTest);
  server.on("/api/lcd/brightness", HTTP_POST, handleLcdBrightness);
  server.on("/api/lcd/brightness", HTTP_GET, handleLcdBrightness);
  server.on("/api/rgb/brightness", HTTP_POST, handleRgbBrightness);
  server.on("/api/rgb/brightness", HTTP_GET, handleRgbBrightness);
  server.on("/api/rgb/test", HTTP_POST, handleRgbTest);
  server.on("/api/rgb/test", HTTP_GET, handleRgbTest);
  server.on("/api/system/reboot", HTTP_POST, handleSystemReboot);
  server.on("/api/system/reset", HTTP_POST, handleDeviceReset);
  server.on("/api/system/backup.json", HTTP_GET, handleExportSystemBackup);

  // Captive portal probes
  server.on("/generate_204", handleCaptivePortalRedirect);
  server.on("/gen_204", handleCaptivePortalRedirect);
  server.on("/hotspot-detect.html", handleCaptivePortalRedirect);
  server.on("/canonical.html", handleCaptivePortalRedirect);
  server.on("/connecttest.txt", handleCaptivePortalRedirect);
  server.on("/ncsi.txt", handleCaptivePortalRedirect);
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("[HTTP] Web server started on port 80.");
  blinkLed(600); // Startup confirmation blink
}

// -----------------------------------------------------------------------------
// MAIN LOOP
// -----------------------------------------------------------------------------
void loop() {
  // 1. Process Captive Portal DNS requests
  dnsServer.processNextRequest();

  // 2. Handle HTTP clients
  server.handleClient();

  // 3. Status LED blink off
  if (ledOffMillis > 0 && millis() >= ledOffMillis) {
    digitalWrite(STATUS_LED, LOW);
    ledOffMillis = 0;
  }

  // 4. Update I2C LCD Idle Status & Standby Sleep Watchdog
  checkLcdStandbyTimeout();
  updateLcdIdle();

  // 5. Update Common Anode RGB LED Breathing & Multi-State Engine
  updateRgbLed();

  // 6. Registration Timeout Watchdog
  if (regState == REG_WAITING) {
    if (millis() - regStartMillis > REG_TIMEOUT_MS) {
      regState = REG_TIMEOUT;
      regResultMsg = "Registration timed out. No card detected in 30 seconds.";
      Serial.println("[REGISTRATION] Timed out waiting for card.");
    }
  }

  // 6. RFID Debounce Check
  if (millis() - lastCardTapMillis < cardDebounceMs) {
    return;
  }

  // 7. Check for RFID Card Tap
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {
    return;
  }

  // Any card brought to the reader awakens the LCD!
  wakeLcd();

  // ---------------------------------------------------------------------------
  // SMART RFID SCANNING GUARD:
  // Attendance is strictly recorded ONLY when session is ACTIVE and NOT in settings.
  // Card registration is allowed whenever in REG_WAITING or on a registration tab!
  // ---------------------------------------------------------------------------
  bool canScanAttendance = sessionActive && !isSettingsOpen;
  bool canScanRegistration = (regState == REG_WAITING) || isRegistrationTab;

  if (!canScanAttendance && !canScanRegistration) {
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
    return;
  }

  // Valid Card Detected for Registration or Active Attendance!
  lastCardTapMillis = millis();
  String uid = formatUID(&(rfid.uid));
  blinkLed(300);

  // Halt card and stop crypto immediately
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();

  Serial.printf("[RFID TAP] UID: %s\n", uid.c_str());

  // ---------------------------------------------------------------------------
  // CASE A: System is in CARD REGISTRATION MODE (Strict 1 Card = 1 Person)
  // ---------------------------------------------------------------------------
  if (regState == REG_WAITING) {
    bool alreadyRegistered = false;
    String existingOwner = "";
    String existingId = "";
    for (const auto& c : registeredCards) {
      if (c.uid.equalsIgnoreCase(uid)) {
        alreadyRegistered = true;
        existingOwner = c.name;
        existingId = c.studentId;
        break;
      }
    }

    if (alreadyRegistered) {
      bool isSame = isSameCandidate(existingOwner, existingId, regCandidateName, regCandidateId);
      if (isSame) {
        // Tapped same card for same person: acknowledge already registered
        regState = REG_SAME_CARD;
        regResultUid = uid;
        regConflictName = existingOwner;
        regConflictId = existingId;
        regResultMsg = "Card is already registered to " + existingOwner;
        Serial.printf("[REGISTRATION] Same card tapped for '%s' (UID: %s)\n", existingOwner.c_str(), uid.c_str());

        lastScan.uid = uid;
        lastScan.name = existingOwner;
        lastScan.timeStr = getCurrentTime12h();
        lastScan.type = "CardAlreadyRegistered";
        lastScan.isDuplicate = true;
        lastScan.isRegistered = true;
        lastScan.message = "Card is already registered to " + existingOwner;
        lastScan.scanMillis = millis();

        showLcdAlreadyRegistered(existingOwner);
        triggerRgbFeedback(RGB_FEEDBACK_RECORDED);
        return;
      } else {
        // Card is registered to someone else: prompt to use another card
        regState = REG_CONFLICT;
        regResultUid = uid;
        regConflictName = existingOwner;
        regConflictId = existingId;
        regResultMsg = "Card already registered to " + existingOwner + ". Please use another card.";
        Serial.printf("[REGISTRATION CONFLICT] UID %s already registered to '%s' (Attempted: '%s')\n",
                      uid.c_str(), existingOwner.c_str(), regCandidateName.c_str());

        lastScan.uid = uid;
        lastScan.name = existingOwner;
        lastScan.timeStr = getCurrentTime12h();
        lastScan.type = "CardConflict";
        lastScan.isDuplicate = true;
        lastScan.isRegistered = true;
        lastScan.message = "Card already registered to " + existingOwner + ". Please use another card.";
        lastScan.scanMillis = millis();

        showLcdCardInUse(existingOwner);
        triggerRgbFeedback(RGB_FEEDBACK_REJECTED);
        return;
      }
    }

    // Unassigned card -> Register successfully
    RegisteredCard newCard;
    newCard.uid = uid;
    newCard.name = regCandidateName;
    newCard.studentId = regCandidateId;
    newCard.regDate = getCurrentTimeString();
    registeredCards.push_back(newCard);
    saveRegisteredCards();

    regResultUid = uid;
    regResultMsg = "Card registered to " + regCandidateName;
    regState = REG_SUCCESS;

    lastScan.uid = uid;
    lastScan.name = regCandidateName;
    lastScan.timeStr = getCurrentTime12h();
    lastScan.type = "Registration";
    lastScan.isDuplicate = false;
    lastScan.isRegistered = true;
    lastScan.message = "Card Registered: " + regCandidateName + " (" + uid + ")";
    lastScan.scanMillis = millis();
    Serial.printf("[REGISTRATION] Success! UID %s mapped to '%s'\n", uid.c_str(), regCandidateName.c_str());
    showLcdRegistered(regCandidateName);
    triggerRgbFeedback(RGB_FEEDBACK_RECORDED);
    return;
  }

  // ---------------------------------------------------------------------------
  // CASE A.2: Card tapped while on Registration Tab (Direct / Pre-Arm Tag Tap)
  // ---------------------------------------------------------------------------
  if (isRegistrationTab) {
    bool alreadyRegistered = false;
    String existingOwner = "";
    String existingId = "";
    for (const auto& c : registeredCards) {
      if (c.uid.equalsIgnoreCase(uid)) {
        alreadyRegistered = true;
        existingOwner = c.name;
        existingId = c.studentId;
        break;
      }
    }

    if (alreadyRegistered) {
      bool isSame = (regCandidateName.length() > 0) &&
                    isSameCandidate(existingOwner, existingId, regCandidateName, regCandidateId);

      lastScan.uid = uid;
      lastScan.name = existingOwner;
      lastScan.timeStr = getCurrentTime12h();
      lastScan.type = isSame ? "CardAlreadyRegistered" : "CardConflict";
      lastScan.isDuplicate = true;
      lastScan.isRegistered = true;
      lastScan.message = isSame ? ("Card is already registered to " + existingOwner)
                                : ("Card already registered to " + existingOwner + ". Please use another card.");
      lastScan.scanMillis = millis();

      wakeLcd();
      if (lcdAvailable) {
        if (isSame) {
          showLcdAlreadyRegistered(existingOwner);
        } else {
          showLcdCardInUse(existingOwner);
        }
      }
      triggerRgbFeedback(isSame ? RGB_FEEDBACK_RECORDED : RGB_FEEDBACK_REJECTED);
      Serial.printf("[REGISTRATION TAB TAP] UID %s detected (Owner: %s | Same Person: %s)\n",
                    uid.c_str(), existingOwner.c_str(), isSame ? "YES" : "NO");
      return;
    } else {
      // Unassigned card tapped
      lastScan.uid = uid;
      lastScan.name = "Unassigned Tag";
      lastScan.timeStr = getCurrentTime12h();
      lastScan.type = "CardDetected";
      lastScan.isDuplicate = false;
      lastScan.isRegistered = false;
      lastScan.message = "Unassigned tag: " + uid;
      lastScan.scanMillis = millis();

      wakeLcd();
      if (lcdAvailable) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print(centerText("TAG DETECTED"));
        lcd.setCursor(0, 1);
        lcd.print(centerText(uid));
        lcdMessageUntil = millis() + 2500;
      }
      triggerRgbFeedback(RGB_FEEDBACK_RECORDED);
      Serial.printf("[REGISTRATION TAB TAP] Unassigned UID %s detected\n", uid.c_str());
      return;
    }
  }

  // ---------------------------------------------------------------------------
  // CASE B: ATTENDANCE SESSION CHECK (Safety fallback)
  // ---------------------------------------------------------------------------
  if (!sessionActive || isSettingsOpen) {
    return;
  }

  // ---------------------------------------------------------------------------
  // CASE C: ATTENDANCE SESSION IS ACTIVE -> VERIFY REGISTRATION
  // ---------------------------------------------------------------------------
  bool regStatus = isCardRegistered(uid);
  if (!regStatus && requireRegistration) {
    lastScan.uid = uid;
    lastScan.name = "Unknown Tag (" + uid + ")";
    lastScan.timeStr = getCurrentTime12h();
    lastScan.type = "Rejected";
    lastScan.isDuplicate = false;
    lastScan.isRegistered = false;
    lastScan.message = "Access Denied: Unregistered card. Please register this tag in the webapp first.";
    lastScan.scanMillis = millis();
    Serial.printf("[ATTENDANCE REJECTED] Unregistered UID: %s\n", uid.c_str());
    blinkLed(100);
    showLcdDenied();
    triggerRgbFeedback(RGB_FEEDBACK_REJECTED);
    return;
  }

  String attendeeName = lookupOwnerName(uid);
  String attendeeId = lookupOwnerId(uid);
  String scanTime = getCurrentTime12h();
  time_t nowTs = time(nullptr);

  // ---------------------------------------------------------------------------
  // CASE D: CHECK FOR EXISTING ATTENDEE RECORD & DUPLICATES
  // Requirement: Repeated cards listed as ONE
  // ---------------------------------------------------------------------------
  AttendanceRecord* existingRec = nullptr;
  for (auto& rec : attendanceRecords) {
    if (rec.uid.equalsIgnoreCase(uid)) {
      existingRec = &rec;
      break;
    }
  }

  if (sessionType == "Entrance") {
    if (existingRec != nullptr) {
      if (existingRec->inTime.length() > 0 && existingRec->inTime != "--") {
        // Repeated Entrance Tap!
        lastScan.uid = uid;
        lastScan.name = existingRec->name;
        lastScan.timeStr = existingRec->inTime;
        lastScan.type = "Entrance";
        lastScan.isDuplicate = true;
        lastScan.isRegistered = regStatus;
        lastScan.message = "Repeated Tap: " + existingRec->name + " already checked in at " + existingRec->inTime + " (Listed as 1 entry)";
        lastScan.scanMillis = millis();
        Serial.printf("[ATTENDANCE] Repeated Entrance tap ignored for '%s'. Already logged at %s\n",
                      existingRec->name.c_str(), existingRec->inTime.c_str());
        showLcdDuplicate(existingRec->name);
        triggerRgbFeedback(RGB_FEEDBACK_DUPLICATE);
        return;
      } else {
        // Had an exit record earlier, now logs entrance
        existingRec->inTime = scanTime;
        existingRec->inTimestamp = nowTs;
        lastScan.uid = uid;
        lastScan.name = existingRec->name;
        lastScan.timeStr = scanTime;
        lastScan.type = "Entrance";
        lastScan.isDuplicate = false;
        lastScan.isRegistered = regStatus;
        lastScan.message = "Recorded Entrance: " + existingRec->name + " at " + scanTime;
        lastScan.scanMillis = millis();
        Serial.printf("[ATTENDANCE IN UPDATED] %s at %s\n", existingRec->name.c_str(), scanTime.c_str());
        showLcdAttendance("Entrance", existingRec->name);
        triggerRgbFeedback(RGB_FEEDBACK_RECORDED);
        return;
      }
    } else {
      // First time attendee in this session
      AttendanceRecord rec;
      rec.uid = uid;
      rec.name = attendeeName;
      rec.studentId = attendeeId;
      rec.inTime = scanTime;
      rec.outTime = "--";
      rec.inTimestamp = nowTs;
      rec.outTimestamp = 0;
      attendanceRecords.push_back(rec);

      lastScan.uid = uid;
      lastScan.name = attendeeName;
      lastScan.timeStr = scanTime;
      lastScan.type = "Entrance";
      lastScan.isDuplicate = false;
      lastScan.isRegistered = regStatus;
      lastScan.message = "Recorded Entrance: " + attendeeName + " at " + scanTime;
      lastScan.scanMillis = millis();
      Serial.printf("[ATTENDANCE RECORDED] #%d (Entrance): %s [%s] at %s\n",
                    attendanceRecords.size(), attendeeName.c_str(), uid.c_str(), scanTime.c_str());
      showLcdAttendance("Entrance", attendeeName);
      triggerRgbFeedback(RGB_FEEDBACK_RECORDED);
      return;
    }
  } else {
    // Mode is "Exit"
    if (existingRec != nullptr) {
      if (existingRec->outTime.length() > 0 && existingRec->outTime != "--") {
        // Repeated Exit Tap!
        lastScan.uid = uid;
        lastScan.name = existingRec->name;
        lastScan.timeStr = existingRec->outTime;
        lastScan.type = "Exit";
        lastScan.isDuplicate = true;
        lastScan.isRegistered = regStatus;
        lastScan.message = "Repeated Tap: " + existingRec->name + " already checked out at " + existingRec->outTime + " (Listed as 1 entry)";
        lastScan.scanMillis = millis();
        Serial.printf("[ATTENDANCE] Repeated Exit tap ignored for '%s'. Already logged at %s\n",
                      existingRec->name.c_str(), existingRec->outTime.c_str());
        showLcdDuplicate(existingRec->name);
        triggerRgbFeedback(RGB_FEEDBACK_DUPLICATE);
        return;
      } else {
        // Update exit time
        existingRec->outTime = scanTime;
        existingRec->outTimestamp = nowTs;
        lastScan.uid = uid;
        lastScan.name = existingRec->name;
        lastScan.timeStr = scanTime;
        lastScan.type = "Exit";
        lastScan.isDuplicate = false;
        lastScan.isRegistered = regStatus;
        lastScan.message = "Recorded Exit: " + existingRec->name + " at " + scanTime;
        lastScan.scanMillis = millis();
        Serial.printf("[ATTENDANCE OUT UPDATED] %s at %s\n", existingRec->name.c_str(), scanTime.c_str());
        showLcdAttendance("Exit", existingRec->name);
        triggerRgbFeedback(RGB_FEEDBACK_RECORDED);
        return;
      }
    } else {
      // Tapped exit directly without prior entrance record
      AttendanceRecord rec;
      rec.uid = uid;
      rec.name = attendeeName;
      rec.studentId = attendeeId;
      rec.inTime = "--";
      rec.outTime = scanTime;
      rec.inTimestamp = 0;
      rec.outTimestamp = nowTs;
      attendanceRecords.push_back(rec);

      lastScan.uid = uid;
      lastScan.name = attendeeName;
      lastScan.timeStr = scanTime;
      lastScan.type = "Exit";
      lastScan.isDuplicate = false;
      lastScan.isRegistered = regStatus;
      lastScan.message = "Recorded Exit: " + attendeeName + " at " + scanTime;
      lastScan.scanMillis = millis();
      Serial.printf("[ATTENDANCE RECORDED] #%d (Exit): %s [%s] at %s\n",
                    attendanceRecords.size(), attendeeName.c_str(), uid.c_str(), scanTime.c_str());
      showLcdAttendance("Exit", attendeeName);
      triggerRgbFeedback(RGB_FEEDBACK_RECORDED);
      return;
    }
  }
}
