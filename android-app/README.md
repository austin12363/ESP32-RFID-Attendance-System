# ESP32 RFID Attendance System — Android Companion App

Native Android companion application built in **Kotlin** and **Jetpack Compose (Material 3)** to interface with the standalone **ESP32 + RC522 RFID + 16x2 LCD Attendance Terminal**.

---

## 📱 Features

1. **Dashboard & Real-Time Monitoring**:
   - Live attendee counter and ESP32 clock display.
   - Real-time last scan event card with duplicate/unregistered badges.
   - Live roster table with search by Name, Student ID, or Card UID.
   - Start / Stop / Clear active attendance sessions.
   - Manual attendee check-in (without physical card).

2. **RFID Card Management**:
   - Interactive two-step card registration:
     1. Enter attendee name & student/employee ID in app.
     2. Tap physical card on ESP32 RC522 scanner.
     3. App receives real-time polling confirmation (`success`, `conflict`, `same_card`).
   - List, search, and delete registered cards stored in LittleFS flash memory.

3. **Session History**:
   - Access up to 5 saved historical sessions stored in ESP32 flash memory.
   - Inspect full attendee lists and timestamps for past sessions.
   - Resume past sessions into the live scanner.

4. **Hardware Peripherals & Diagnostics**:
   - **One-Tap System Clock Sync**: Syncs phone time and timezone to the ESP32 RTC.
   - **LCD Display Controller**: Push custom 2-line announcements to the physical 16x2 I2C LCD.
   - **Display Power Management**: Put LCD to sleep or wake it up remotely.
   - **RGB LED Diagnostics**: Visual hardware test (Green, Yellow, Red, White).
   - **Reboot & Reset**: Clean system restart and factory defaults.

5. **Android Wi-Fi Hotspot Binding**:
   - Automatically utilizes `ConnectivityManager.bindProcessToNetwork` so Android stays connected to the `ESP32-Attendance` Access Point (`http://192.168.4.1`) without the phone disconnecting due to "No Internet Access".

---

## 🚀 Cloud Build via GitHub Actions (Zero Local Setup)

Whenever you push to `main` or `master`, GitHub Actions automatically compiles the application in the cloud:
1. Go to your repository on GitHub.
2. Click on the **Actions** tab.
3. Select the latest **Build Android APK** workflow run.
4. Under **Artifacts**, download `ESP32-Attendance-Debug-APK`.
5. Unzip and install `app-debug.apk` directly on your Android phone!

---

## 💻 Local Build (Optional)

### Using Android Studio
1. Open Android Studio.
2. Select **Open** and select the `android-app` folder.
3. Allow Gradle to sync.
4. Click **Build > Build Bundle(s) / APK(s) > Build APK(s)**.

### Using Command Line
```bash
cd android-app
./gradlew assembleDebug
```
The output APK will be generated at:
`app/build/outputs/apk/debug/app-debug.apk`
