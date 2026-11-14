# ESP32 + RC522 RFID Attendance System (Self-Contained Offline Web App)

A complete, production-ready, standalone RFID attendance system powered by an **ESP32** microcontroller and an **MFRC522** RFID reader.

The system broadcasts its own **Wi-Fi Hotspot** with a built-in **Captive Portal** and serves a modern, responsive web application directly from the ESP32's flash memory. **No internet connection, no external RTC hardware module, and no external servers or cloud dependencies are required.**

---

## Table of Contents
1. [Key Features](#key-features)
2. [Hardware Wiring Diagram](#hardware-wiring-diagram)
3. [Arduino IDE Setup & Installation](#arduino-ide-setup--installation)
4. [How to Use the System](#how-to-use-the-system)
   - [1. Connect to the Wi-Fi Hotspot](#1-connect-to-the-wi-fi-hotspot)
   - [2. First Boot Onboarding (Time Sync & Direction Prompt)](#2-first-boot-onboarding-time-sync--direction-prompt)
   - [3. Register RFID Tags / Cards](#3-register-rfid-tags--cards)
   - [4. Run an Attendance Session (Entrance & Exit)](#4-run-an-attendance-session-entrance--exit)
   - [5. Export Attendance & Database to CSV](#5-export-attendance--database-to-csv)
5. [API Endpoints Reference](#api-endpoints-reference)
6. [Troubleshooting & Diagnostics](#troubleshooting--diagnostics)

---

## Key Features

- **All-in-One INO Sketch:** Everything (C++ logic, web server routes, DNS captive portal, persistent NVS flash storage, HTML5, CSS3, JavaScript, and Web Audio chimes) is self-contained in a single `.ino` file.
- **Standalone Wi-Fi Hotspot & Captive Portal:** Broadcasts `ESP32-Attendance` on `192.168.4.1` with support for up to 8 concurrent clients. Connecting smartphones and laptops automatically open or redirect to the dashboard.
- **First-Boot Time Onboarding (No RTC Module Required):** Because ESP32 has no battery RTC, upon entering the webapp for the first time, an onboarding setup modal immediately asks the user to synchronize the system clock (1-click browser time sync or manual date-time picker) AND select the initial attendance direction (Entrance/Exit).
- **Two-Step Card Registration Flow:** In the webapp, enter the owner's name and optional ID, confirm, and tap the RFID card against the RC522 reader. Saved permanently into ESP32 non-volatile storage (NVS Preferences). Allows rapid continuous registration for entire classes.
- **Session-Based Attendance:** Attendance is accepted **only** when a session is active (`sessionActive == true`).
- **Strict "Repeated Cards Listed as ONE" Enforcement:**
  - Each attendee is represented by **exactly one row** in the roster table, displaying their Name, Student/Employee ID, Card UID, Time IN, Time OUT, and status badge (*Checked In*, *Checked Out*, or *Completed*).
  - Repeated taps in the same direction are detected and ignored as duplicates with amber warnings and audio blips, never creating duplicate rows.
  - Toggling between Entrance and Exit seamlessly updates the attendee's IN and OUT timestamps without duplicate records.
- **Strict Registration Policy Toggle:** Configurable in Settings to block unregistered cards with an audio warning and visual notice, or allow unregistered scans.
- **RF Sensitivity Boost:** Hardware antenna gain is configured to maximum (48 dB `RxGain_max`), maximizing read range and responsiveness across Mifare Classic, Ultralight, and NTAG cards.
- **Instant CSV Export:** One-click download of the complete session roster to a CSV spreadsheet directly on your phone or laptop.

---

## Hardware Wiring Diagram

Connect the **MFRC522 RFID Reader** and **I2C 16x2 LCD** to your **ESP32** board:

### 1. MFRC522 RFID Reader (SPI)
| RC522 Pin | ESP32 Pin | Wire Color Guide | Function / Critical Notes |
| :--- | :--- | :--- | :--- |
| **3.3V (VCC)** | **3V3** | Red | **CRITICAL: Connect to 3.3V ONLY! 5V will burn the RC522.** |
| **RST** | **GPIO 4** | Orange | Reset Pin *(Moved to GPIO 4 to free GPIO 22 for I2C SCL)* |
| **GND** | **GND** | Black | Ground (common ground) |
| **IRQ** | *(Leave empty)* | -- | Not connected (software polling mode) |
| **MISO** | **GPIO 19** | Blue | Standard VSPI MISO |
| **MOSI** | **GPIO 23** | Green | Standard VSPI MOSI |
| **SCK** | **GPIO 18** | Yellow | Standard VSPI SCK |
| **SDA (SS)** | **GPIO 5** | Purple | Standard VSPI Slave Select (CS) |

### 2. I2C 16x2 LCD Display (I2C)
| LCD Pin | ESP32 Pin | Wire Color Guide | Function / Notes |
| :--- | :--- | :--- | :--- |
| **GND** | **GND** | Black | Common Ground |
| **VCC** | **VIN / 5V** | Red | Connect to 5V (standard HD44780 contrast requires 5V) |
| **SDA** | **GPIO 21** | Blue | Hardware I2C SDA |
| **SCL** | **GPIO 22** | Yellow | Hardware I2C SCL *(Address auto-detected 0x27 or 0x3F)* |

> **Onboard LED:** GPIO 2 (built-in blue LED on most ESP32 Dev Modules) automatically flashes whenever an RFID card is tapped or registered.

---

## Arduino IDE Setup & Installation

Follow these easy steps to upload the code to your ESP32:

### Step 1: Install ESP32 Board Package
1. Open **Arduino IDE** (version 2.x recommended).
2. Go to **File -> Preferences**.
3. In **Additional boards manager URLs**, add:
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
4. Click **OK**.
5. Go to **Tools -> Board -> Boards Manager...** (or click the board icon on the left sidebar).
6. Search for `esp32` by **Espressif Systems** and click **Install**.

### Step 2: Install MFRC522 Library
1. In Arduino IDE, go to **Tools -> Manage Libraries...** (or press `Ctrl + Shift + I`).
2. In the search bar, type:
   ```
   MFRC522
   ```
3. Look for **MFRC522** by **GithubCommunity** (originally Miguel Balboa) and click **Install**.

### Step 3: Open the Sketch & Upload
1. Open [`ESP32_RFID_Attendance/ESP32_RFID_Attendance.ino`](file:///c:/Users/austin/OneDrive%20-%20slsu.edu.ph/Desktop/Attendance%20system/ESP32_RFID_Attendance/ESP32_RFID_Attendance.ino) in Arduino IDE.
2. Connect your ESP32 board to your computer via micro-USB or USB-C cable.
3. In **Tools -> Board**, select **ESP32 Dev Module** (or your specific ESP32 board model).
4. In **Tools -> Port**, select the COM port of your ESP32 (e.g. `COM3`, `COM5`).
5. (Optional) Set **Tools -> Upload Speed** to `921600` for faster uploading.
6. Click the **Upload** arrow button (or press `Ctrl + U`).
7. Once upload completes ("Done uploading"), open **Tools -> Serial Monitor** and set baud rate to `115200` to view diagnostic startup logs.

---

## How to Use the System

### 1. Connect to the Wi-Fi Hotspot
1. On your smartphone, tablet, or laptop, open Wi-Fi settings.
2. Connect to the network:
   - **SSID:** `ESP32-Attendance`
   - **Password:** `12345678`
3. A "Sign in to Wi-Fi network" captive portal prompt will appear automatically.
4. If it doesn't open automatically, open your browser (Chrome, Safari, Edge, Firefox) and type:
   ```
   http://192.168.4.1
   ```

### 2. First Boot Onboarding (Time Sync & Direction Prompt)
- Upon entering the webapp on a fresh boot, an onboarding setup modal will automatically pop up:
  - **Step 1:** Click **⚡ Sync Phone / Device Time Now (1-Click)** to synchronize the ESP32 software crystal clock with your phone or PC's current local time and timezone.
  - **Step 2:** Choose the attendance direction: **Entrance (IN)** or **Exit (OUT)**, and give your session a name.
  - Click **▶ Start Attendance Session Now** to jump directly into live scanning!

### 3. Register RFID Tags / Cards
1. In the webapp, click the **➕ Register Tag** tab.
2. Enter the student or employee's **Full Name** (e.g., *Juan Dela Cruz*).
3. Optionally enter their **ID Number** (e.g., *2026-0042*).
4. Click **Step 2: Confirm & Tap Card Now**.
5. An animated modal pops up: *"Waiting for RFID card tap against reader..."* with a 30-second countdown.
6. Tap the RFID card or key fob onto the RC522 reader.
7. The ESP32 flashes its LED, plays a success chime in the browser, and permanently binds the card UID to the person's name in NVS flash memory.
8. Click **➕ Register Another Tag** to immediately register the next card without navigating away.

### 4. Run an Attendance Session (Entrance & Exit)
1. In the webapp, go to the **📋 Live Attendance** tab.
2. Select session mode: **Entrance (IN)** or **Exit (OUT)**.
3. Click **▶ Start Attendance Session**.
4. The session is now **ACTIVE** and will accept card taps.
5. As attendees tap their cards:
   - The browser chimes and flashes a **green alert** showing their name and timestamp.
   - The attendee is added to the roster table with their IN timestamp.
   - **Repeated Card Protection:** If the student taps their card again while in the same mode, the webapp plays an alert tone, flashes an amber notice (*"Repeated Tap: Already recorded at [time]. Listed as 1 entry"*), and keeps the roster at exactly one entry.
6. To record departure, click **🔄 Switch to Exit Mode**.
7. When attendees tap out:
   - Their existing roster row updates with their OUT timestamp, and status changes to **✓ Completed**.
   - Repeated exit taps are similarly ignored as duplicates.

### 5. Export Attendance & Database to CSV
- **Session Roster CSV:** Click **📥 Export CSV** on the Live Attendance tab to download `attendance_log.csv` (Columns: Index, Attendee Name, ID Number, Card UID, Time IN, Time OUT, Status, Session Title).
- **Cards Database CSV:** Click **📥 Export Cards CSV** on the Registered Database tab to download `registered_cards.csv` (Columns: Index, Owner Name, ID Number, Card UID, Registered Date).

---

## API Endpoints Reference

| Endpoint | Method | Description |
| :--- | :--- | :--- |
| `/` | `GET` | Serves the complete responsive dashboard HTML/CSS/JS |
| `/api/status` | `GET` | Returns live system status (clock, session state, counts, latest scan) |
| `/api/time` | `POST` | Synchronizes ESP32 clock (`epoch` timestamp, `tz_offset`) |
| `/api/session/start` | `POST` | Starts attendance session (`type="Entrance"\|"Exit"`, `title`, optional `clear_previous=1`) |
| `/api/session/stop` | `POST` | Stops active attendance session |
| `/api/session/toggle_mode`| `POST` | Toggles active session between Entrance and Exit |
| `/api/session/clear` | `POST` | Clears current attendance roster |
| `/api/settings` | `POST` | Configures system policies (`require_registration=1\|0`) |
| `/api/attendance` | `GET` | Returns JSON array of recorded attendees for current session |
| `/api/attendance/export.csv` | `GET` | Streams CSV file download of attendance roster |
| `/api/register/start` | `POST` | Puts reader into registration mode for 30s (`name`, `student_id`) |
| `/api/register/status` | `GET` | Polls registration progress and returns captured UID |
| `/api/register/cancel` | `POST` | Cancels active registration mode |
| `/api/cards` | `GET` | Returns JSON array of all registered cards from NVS |
| `/api/cards/delete` | `POST` | Deletes a registered card by `uid` |
| `/api/cards/clear` | `POST` | Clears all registered cards from flash storage |
| `/api/cards/export.csv` | `GET` | Streams CSV file download of all registered cards in NVS |

---

## Troubleshooting & Diagnostics

1. **Serial Monitor shows `[RFID WARNING] Reader communication failed! Version: 0x00 or 0xFF`:**
   - Check your wiring connections. Loose jumper wires are the most common cause.
   - Confirm **VCC** is connected to **3V3** (not 5V).
   - Verify MISO is on GPIO 19, MOSI on GPIO 23, SCK on GPIO 18, and SDA/SS on GPIO 5.

2. **Captive portal doesn't pop up automatically on phone:**
   - Some phones require you to open your mobile browser and navigate directly to `http://192.168.4.1`.
   - Make sure mobile data is temporarily toggled off if your phone tries to use 4G/5G instead of local Wi-Fi.

3. **Card tap is rejected / ignored during attendance:**
   - Check if the session is **ON**. Attendance is only recorded when the session is active.
   - If **Strict Registration Mode** is enabled in Settings, the card must first be registered in the **Register Tag** tab.
   - If the card was already tapped in this direction, it is acknowledged as a duplicate and kept as a single entry.

4. **Time resets after powering off:**
   - Because there is no hardware coin-battery RTC module, this is normal behavior. Simply click **⚡ Sync Phone Time** when you power up the device and connect.
