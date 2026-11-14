# ESP32 RFID Attendance System — Complete Control REST API Specification
> **Firmware Target**: ESP32 Dev Module (No OTA 2MB APP / 2MB SPIFFS)  
> **Default Host**: `http://192.168.4.1` (Port 80)  
> **Protocol**: HTTP/1.1 REST  
> **Supported Request Formats**: `application/json`, `application/x-www-form-urlencoded`, and URL query parameters  
> **CORS**: Globally Enabled (`Access-Control-Allow-Origin: *`, `Access-Control-Allow-Methods: *`, `Access-Control-Allow-Headers: *`)

---

## Table of Contents
1. [Network & Connection Guidelines](#1-network--connection-guidelines)
2. [API Architecture & Android Polling Flow](#2-api-architecture--android-polling-flow)
3. [System & Device Health APIs](#3-system--device-health-apis)
4. [Active Attendance Session APIs](#4-active-attendance-session-apis)
5. [Live Roster & Manual Attendance APIs](#5-live-roster--manual-attendance-apis)
6. [Session History APIs (Max 5 Sessions)](#6-session-history-apis-max-5-sessions)
7. [Card Database & Registration APIs](#7-card-database--registration-apis)
8. [Hardware, Display & Peripherals APIs](#8-hardware-display--peripherals-apis)
9. [Settings & Configuration APIs](#9-settings--configuration-apis)
10. [Data Export & Backup APIs](#10-data-export--backup-apis)
11. [Android Integration Guide (Kotlin + Retrofit)](#11-android-integration-guide-kotlin--retrofit)

---

## 1. Network & Connection Guidelines

* **Default Wi-Fi SSID**: `ESP32-Attendance`
* **Default IP Address**: `192.168.4.1`
* **Port**: `80`
* **Base URL**: `http://192.168.4.1`
* **Authentication**: None required on the local AP subnet.
* **Captive Portal Bypass**: In Android, when connected to a Wi-Fi hotspot without Internet, Android may route traffic through mobile data. Call `ConnectivityManager.bindProcessToNetwork(wifiNetwork)` in your Android app to force all HTTP requests to go through the ESP32 Wi-Fi interface.
* **Universal Request Body Parser**: All `POST` endpoints accept parameters via:
  1. Standard JSON body (`Content-Type: application/json`)
  2. Form URL-Encoded (`Content-Type: application/x-www-form-urlencoded`)
  3. URL Query parameters (`?param=value`)

---

## 2. API Architecture & Android Polling Flow

```
[Android App] ──────────(Every 1.5s)─────────> GET /api/status
              (Live clock, mode, attendee count, storage, and last card scan)

              ┌── If attendance_count changes ──> GET /api/attendance (Sync live roster)
              │
              ├── If registering card ───────────> GET /api/register/status (Every 500ms)
              │
              └── If user triggers action ──────> POST /api/... (Immediate response)
```

---

## 3. System & Device Health APIs

### 3.1 Device Status & Heartbeat
Retrieves real-time system state, time, active session details, LittleFS storage estimations, and the most recent RFID scan event.

* **Method**: `GET`
* **Path**: `/api/status`
* **Response `200 OK`**:
```json
{
  "time_set": true,
  "current_time": "2026-11-14 17:30:00",
  "session_active": true,
  "session_type": "Entrance",
  "session_title": "Morning Assembly",
  "require_registration": false,
  "is_settings_open": false,
  "reg_state": "idle",
  "registered_count": 6,
  "attendance_count": 12,
  "history_count": 2,
  "free_heap": 276604,
  "total_heap": 327680,
  "fs_used": 12288,
  "fs_total": 1966080,
  "fs_free": 1953792,
  "est_cards_free": 20352,
  "wifi_clients": 1,
  "wifi_ssid": "ESP32-Attendance",
  "current_wifi_pass": "",
  "next_wifi_pass": "",
  "wifi_pass_pending": false,
  "uptime_sec": 845,
  "lcd_timeout": 45000,
  "card_debounce": 1500,
  "show_lcd_wifi": true,
  "last_scan": {
    "uid": "A1B2C3D4",
    "name": "Juan D. Dela Cruz",
    "time": "05:30 PM",
    "type": "Entrance",
    "is_duplicate": false,
    "is_registered": true,
    "message": "Recorded Entrance: Juan D. Dela Cruz at 05:30 PM"
  }
}
```

---

### 3.2 Synchronize System Clock
Sets the ESP32 software RTC. Recommended to execute immediately upon app connection.

* **Method**: `POST`
* **Path**: `/api/time`
* **Request Body (JSON)**:
```json
{
  "epoch": 1794648600,
  "tz_offset": -480,
  "iso": "2026-11-14T17:30:00.000Z"
}
```
* **Parameters**:
  * `epoch` *(number, required)*: Unix timestamp in seconds (`System.currentTimeMillis() / 1000`).
  * `tz_offset` *(number, optional)*: Timezone offset in minutes (e.g., `-480` for UTC+8).
  * `iso` *(string, optional)*: ISO 8601 formatted datetime string.
* **Response `200 OK`**:
```json
{
  "success": true,
  "time": "2026-11-14 17:30:00",
  "message": "Time synchronized successfully!"
}
```

---

### 3.3 Reboot Device
Restarts the ESP32 microcontroller cleanly.

* **Method**: `POST`
* **Path**: `/api/system/reboot`
* **Response `200 OK`**:
```json
{
  "success": true,
  "message": "Rebooting system now..."
}
```

---

### 3.4 Device Reset (Factory Defaults)
Erases all registered cards, deletes session history, clears NVS settings, and reboots.

* **Method**: `POST`
* **Path**: `/api/system/reset`
* **Response `200 OK`**:
```json
{
  "success": true,
  "message": "System reset to factory defaults. Rebooting now..."
}
```

---

## 4. Active Attendance Session APIs

### 4.1 Start Session
Activates the RFID scanner for attendance recording.

* **Method**: `POST`
* **Path**: `/api/session/start`
* **Request Body (JSON)**:
```json
{
  "title": "Computer Science 101",
  "type": "Entrance",
  "clear_previous": "0"
}
```
* **Parameters**:
  * `title` *(string, required)*: Name/title of the attendance session.
  * `type` *(string, required)*: Mode — `"Entrance"` or `"Exit"`.
  * `clear_previous` *(string, optional)*: Set to `"1"` to wipe live roster before starting; `"0"` to preserve.
* **Response `200 OK`**:
```json
{
  "success": true,
  "title": "Computer Science 101",
  "type": "Entrance",
  "message": "Attendance session started!"
}
```

---

### 4.2 Stop Session
Deactivates the live attendance session.

* **Method**: `POST`
* **Path**: `/api/session/stop`
* **Response `200 OK`**:
```json
{
  "success": true,
  "message": "Attendance session stopped!"
}
```

---

### 4.3 Toggle Mode
Switches between `"Entrance"` and `"Exit"` mode.

* **Method**: `POST`
* **Path**: `/api/session/toggle_mode`
* **Response `200 OK`**:
```json
{
  "success": true,
  "type": "Exit",
  "message": "Switched to Exit mode"
}
```

---

### 4.4 Set Specific Mode
Explicitly sets the scanning mode.

* **Method**: `POST`
* **Path**: `/api/session/set_mode`
* **Request Body (JSON)**:
```json
{
  "type": "Entrance"
}
```
* **Response `200 OK`**:
```json
{
  "success": true,
  "type": "Entrance",
  "message": "Mode set to Entrance"
}
```

---

### 4.5 Clear Active Session Roster
Resets the live attendance table without ending the session.

* **Method**: `POST`
* **Path**: `/api/session/clear`
* **Response `200 OK`**:
```json
{
  "success": true,
  "message": "Session records cleared."
}
```

---

### 4.6 Save Session to History
Saves the live attendance session to LittleFS history (FIFO queue of max 5 sessions).

* **Method**: `POST`
* **Path**: `/api/session/save`
* **Response `200 OK`**:
```json
{
  "success": true,
  "id": "sess_1794648600_123",
  "message": "Session saved to history successfully!"
}
```

---

## 5. Live Roster & Manual Attendance APIs

### 5.1 Get Live Attendance Log
Returns the complete attendee list for the active session.

* **Method**: `GET`
* **Path**: `/api/attendance`
* **Response `200 OK`**:
```json
[
  {
    "index": 1,
    "uid": "12345678",
    "name": "Juan D. Dela Cruz",
    "student_id": "2024-001",
    "time_in": "08:05 AM",
    "time_out": "--"
  },
  {
    "index": 2,
    "uid": "87654321",
    "name": "Maria S. Santos",
    "student_id": "2024-002",
    "time_in": "08:12 AM",
    "time_out": "11:45 AM"
  }
]
```

---

### 5.2 Manual Attendance Entry (Without Card)
Allows an administrator/teacher to manually check in or check out an attendee directly from the Android app (e.g. if the attendee forgot their card).

* **Method**: `POST`
* **Path**: `/api/attendance/manual`
* **Request Body (JSON)**:
```json
{
  "name": "Juan D. Dela Cruz",
  "student_id": "2024-001",
  "type": "Entrance"
}
```
* **Parameters**:
  * `name` *(string, required)*: Full name of attendee.
  * `student_id` *(string, optional)*: Student / Employee ID number.
  * `type` *(string, optional)*: `"Entrance"` or `"Exit"`. Defaults to active session mode.
  * `uid` *(string, optional)*: Optional manual card UID.
* **Response `200 OK`**:
```json
{
  "success": true,
  "name": "Juan D. Dela Cruz",
  "type": "Entrance",
  "time": "08:30 AM",
  "message": "Manual attendance recorded!"
}
```

---

### 5.3 Remove Attendee from Active Session
Deletes an accidental or incorrect attendance scan entry from the active session.

* **Method**: `POST`
* **Path**: `/api/attendance/delete`
* **Request Body (JSON)**:
```json
{
  "uid": "12345678"
}
```
*(Alternatively pass `"index": 1`)*
* **Response `200 OK`**:
```json
{
  "success": true,
  "attendee_count": 1,
  "message": "Record removed from session."
}
```

---

## 6. Session History APIs (Max 5 Sessions)

### 6.1 List Saved Sessions
Returns list of up to 5 saved historical sessions.

* **Method**: `GET`
* **Path**: `/api/history`
* **Response `200 OK`**:
```json
[
  {
    "id": "sess_1794648600_123",
    "title": "CS101 Morning Lecture",
    "type": "Entrance",
    "date": "2026-11-14",
    "save_time": "11:30 AM",
    "attendee_count": 28
  }
]
```

---

### 6.2 Get Saved Session Details
Retrieves attendee records for a specific historical session.

* **Method**: `GET`
* **Path**: `/api/history/session?id=sess_1794648600_123`
* **Response `200 OK`**:
```json
{
  "success": true,
  "id": "sess_1794648600_123",
  "title": "CS101 Morning Lecture",
  "type": "Entrance",
  "date": "2026-11-14",
  "save_time": "11:30 AM",
  "attendee_count": 2,
  "records": [
    {
      "name": "Juan D. Dela Cruz",
      "student_id": "2024-001",
      "uid": "12345678",
      "time_in": "08:05 AM",
      "time_out": "--",
      "status": "Checked In"
    }
  ]
}
```

---

### 6.3 Resume Saved Session (Continue Session)
Restores a historical session into live scanner memory to continue attendance logging.

* **Method**: `POST`
* **Path**: `/api/history/resume`
* **Request Body (JSON) or Query Parameter**:
```json
{
  "id": "sess_1794648600_123"
}
```
* **Response `200 OK`**:
```json
{
  "success": true,
  "id": "sess_1794648600_123",
  "title": "CS101 Morning Lecture",
  "type": "Entrance",
  "attendee_count": 2,
  "records": [
    {
      "index": 1,
      "uid": "12345678",
      "name": "Juan D. Dela Cruz",
      "student_id": "2024-001",
      "time_in": "08:05 AM",
      "time_out": "--"
    }
  ],
  "message": "Session resumed with 2 attendees."
}
```

---

### 6.4 Delete Saved Session
* **Method**: `POST`
* **Path**: `/api/history/delete`
* **Request Body (JSON)**:
```json
{
  "id": "sess_1794648600_123"
}
```
* **Response `200 OK`**:
```json
{
  "success": true,
  "message": "Session deleted from history."
}
```

---

### 6.5 Clear All History
* **Method**: `POST`
* **Path**: `/api/history/clear`
* **Response `200 OK`**:
```json
{
  "success": true,
  "message": "All session history cleared from LittleFS."
}
```

---

## 7. Card Database & Registration APIs

### 7.1 List Registered Cards
Returns all registered RFID cards stored in LittleFS.

* **Method**: `GET`
* **Path**: `/api/cards`
* **Response `200 OK`**:
```json
[
  {
    "index": 1,
    "uid": "A1B2C3D4",
    "name": "Juan D. Dela Cruz",
    "student_id": "2024-00129",
    "reg_date": "2026-11-14"
  }
]
```

---

### 7.2 Start Interactive RFID Card Registration
Arms the physical RC522 RFID reader to link the next tapped tag to an attendee.

* **Method**: `POST`
* **Path**: `/api/register/start`
* **Request Body (JSON)**:
```json
{
  "name": "Juan D. Dela Cruz",
  "student_id": "2024-00129",
  "batch_info": ""
}
```
* **Response `200 OK`**:
```json
{
  "success": true,
  "name": "Juan D. Dela Cruz",
  "student_id": "2024-00129",
  "message": "Registration armed. Please tap card on RFID reader."
}
```

---

### 7.3 Poll Registration Status
Poll every **500ms** while waiting for the physical card tap.

* **Method**: `GET`
* **Path**: `/api/register/status`
* **Possible `state` values**:
  * `"waiting"`: Reader is active, waiting for a card tap.
  * `"success"`: New card successfully assigned and saved.
  * `"same_card"`: Tapped card is already assigned to this exact person (positive confirmation).
  * `"conflict"`: Tapped card is already assigned to a *different* person (`conflict_owner` returned).
  * `"timeout"`: 30 seconds elapsed with no card detected.
  * `"idle"` / `"cancelled"`: Registration not armed.
* **Sample Response (Success)**:
```json
{
  "state": "success",
  "message": "Card registered successfully!",
  "result_uid": "A1B2C3D4",
  "candidate_name": "Juan D. Dela Cruz",
  "candidate_id": "2024-00129",
  "conflict_owner": "",
  "conflict_id": ""
}
```
* **Sample Response (Conflict - Used by Someone Else)**:
```json
{
  "state": "conflict",
  "message": "Card already in use by Maria S. Santos. Tap a different card.",
  "result_uid": "E5F6G7H8",
  "candidate_name": "Juan D. Dela Cruz",
  "candidate_id": "2024-00129",
  "conflict_owner": "Maria S. Santos",
  "conflict_id": "2024-00088"
}
```

---

### 7.4 Cancel Registration Mode
Disarms the reader from registration mode.

* **Method**: `POST`
* **Path**: `/api/register/cancel`
* **Response `200 OK`**:
```json
{
  "success": true,
  "message": "Registration cancelled."
}
```

---

### 7.5 Direct Add or Update Card (Without Scanning)
Allows adding a card directly if the UID is already known, or editing an existing card's name/ID without re-scanning.

* **Method**: `POST`
* **Path**: `/api/cards/add` (or `/api/cards/save`)
* **Request Body (JSON)**:
```json
{
  "uid": "A1B2C3D4",
  "name": "Juan D. Dela Cruz",
  "student_id": "2024-00129"
}
```
* **Response `200 OK`**:
```json
{
  "success": true,
  "updated": true,
  "uid": "A1B2C3D4",
  "name": "Juan D. Dela Cruz",
  "message": "Card saved successfully!"
}
```

---

### 7.6 Delete Single Registered Card
* **Method**: `POST`
* **Path**: `/api/cards/delete`
* **Request Body (JSON)**:
```json
{
  "uid": "A1B2C3D4"
}
```
* **Response `200 OK`**:
```json
{
  "success": true,
  "message": "Card deleted."
}
```

---

### 7.7 Clear All Registered Cards
* **Method**: `POST`
* **Path**: `/api/cards/clear`
* **Response `200 OK`**:
```json
{
  "success": true,
  "message": "All registered cards deleted"
}
```

---

### 7.8 Bulk Import Cards (CSV / TSV)
Bulk imports cards with pre-assigned UIDs in TSV or CSV format (`UID\tName\tID`).

* **Method**: `POST`
* **Path**: `/api/cards/import`
* **Request Body (JSON)**:
```json
{
  "data": "A1B2C3D4\tJuan Dela Cruz\t2024-001\nE5F6G7H8\tMaria Santos\t2024-002"
}
```
* **Response `200 OK`**:
```json
{
  "success": true,
  "imported_count": 2,
  "total_cards": 8,
  "message": "Successfully imported 2 cards!"
}
```

---

## 8. Hardware, Display & Peripherals APIs

### 8.1 LCD Display Custom Message
Displays custom text across the 2-line physical 16x2 LCD screen directly from the Android app (useful for greetings, announcements, or custom alerts).

* **Method**: `POST`
* **Path**: `/api/lcd/message`
* **Request Body (JSON)**:
```json
{
  "line1": "WELCOME GUESTS!",
  "line2": "PLEASE TAP CARD",
  "duration": 4000
}
```
* **Parameters**:
  * `line1` *(string, optional)*: Text for Row 0 (centered automatically, max 16 chars).
  * `line2` *(string, optional)*: Text for Row 1 (centered automatically, max 16 chars).
  * `duration` *(number, optional)*: Display duration in milliseconds (1000–15000ms, default 3000ms).
* **Response `200 OK`**:
```json
{
  "success": true,
  "message": "Custom message displayed on LCD."
}
```

---

### 8.2 LCD Backlight Sleep & Wake
* `POST /api/lcd/sleep` — Manually turns LCD backlight OFF.
* `POST /api/lcd/wake` — Manually wakes up LCD backlight.
* `POST /api/lcd/test` — Visually pulses display.

---

### 8.3 RGB Status LED Controls
* `POST /api/rgb/test?mode=green` — Test Green feedback (Recorded).
* `POST /api/rgb/test?mode=yellow` — Test Yellow feedback (Duplicate).
* `POST /api/rgb/test?mode=red` — Test Red feedback (Rejected).
* `POST /api/rgb/test?mode=white` — Test White breathing / test glow.

---

## 9. Settings & Configuration APIs

### 9.1 Read All Current Settings
Retrieves all system settings in a single JSON call.

* **Method**: `GET`
* **Path**: `/api/settings`
* **Response `200 OK`**:
```json
{
  "success": true,
  "require_registration": false,
  "card_debounce": 1500,
  "lcd_timeout": 45000,
  "show_lcd_wifi": true,
  "lcd_brightness_pct": 100,
  "lcd_dim_duration": 10000,
  "rgb_brightness_pct": 100,
  "wifi_ssid": "ESP32-Attendance",
  "wifi_pass_pending": false
}
```

---

### 9.2 Update System Settings
* **Method**: `POST`
* **Path**: `/api/settings`
* **Request Body (JSON)**:
```json
{
  "require_registration": "0",
  "card_debounce": "1500",
  "lcd_timeout": "45000",
  "lcd_wifi_guide": "1",
  "lcd_dim_duration": "10000",
  "lcd_brightness_pct": "100",
  "rgb_brightness_pct": "100"
}
```
* **Parameters**:
  * `require_registration` *(string)*: `"1"` (Only registered cards accepted) or `"0"` (Allow all RFID tags).
  * `card_debounce` *(string)*: Minimum delay between consecutive taps in ms (e.g. `"1500"`).
  * `lcd_timeout` *(string)*: Screen standby timeout in ms (e.g. `"45000"`, `"0"` = never sleep).
  * `lcd_wifi_guide` *(string)*: `"1"` (periodically show Wi-Fi info on LCD) or `"0"`.
  * `lcd_dim_duration` *(string)*: Pre-sleep dim warning duration in ms (e.g. `"10000"`).
  * `lcd_brightness_pct` *(string)*: Active brightness percentage `0` to `100`.
  * `rgb_brightness_pct` *(string)*: Status LED brightness percentage `0` to `100`.

---

### 9.3 Change Wi-Fi Hotspot Password
* **Method**: `POST`
* **Path**: `/api/wifi/password`
* **Request Body (JSON)**:
```json
{
  "password": "NewSecretPassword123"
}
```

---

## 10. Data Export & Backup APIs

### 10.1 Live Attendance CSV Export
* **Method**: `GET`
* **Path**: `/api/attendance/export.csv`
* **Content-Type**: `text/csv`

---

### 10.2 Historical Session CSV Export
* **Method**: `GET`
* **Path**: `/api/history/export.csv?id=sess_1794648600_123`
* **Content-Type**: `text/csv`

---

### 10.3 Registered Cards CSV Export
* **Method**: `GET`
* **Path**: `/api/cards/export.csv`
* **Content-Type**: `text/csv`

---

### 10.4 Full System Backup (JSON)
Downloads all registered cards, session metadata, and system configuration.

* **Method**: `GET`
* **Path**: `/api/system/backup.json`
* **Content-Type**: `application/json`

---

## 11. Android Integration Guide (Kotlin + Retrofit)

### 11.1 Gradle Dependencies (`build.gradle.kts`)
```kotlin
dependencies {
    implementation("com.squareup.retrofit2:retrofit:2.11.0")
    implementation("com.squareup.retrofit2:converter-gson:2.11.0")
    implementation("com.squareup.okhttp3:okhttp:4.12.0")
    implementation("com.squareup.okhttp3:logging-interceptor:4.12.0")
}
```

### 11.2 Complete Retrofit Service Interface (`Esp32AttendanceApi.kt`)
```kotlin
package com.example.attendance.data

import retrofit2.Response
import retrofit2.http.*

interface Esp32AttendanceApi {

    // --- System & Health ---
    @GET("/api/status")
    suspend fun getStatus(): Response<StatusResponse>

    @POST("/api/time")
    suspend fun syncTime(@Body body: Map<String, @JvmSuppressWildcards Any>): Response<ApiResponse>

    @POST("/api/system/reboot")
    suspend fun reboot(): Response<ApiResponse>

    @POST("/api/system/reset")
    suspend fun resetDevice(): Response<ApiResponse>

    // --- Active Session ---
    @POST("/api/session/start")
    suspend fun startSession(@Body body: StartSessionRequest): Response<ApiResponse>

    @POST("/api/session/stop")
    suspend fun stopSession(): Response<ApiResponse>

    @POST("/api/session/toggle_mode")
    suspend fun toggleMode(): Response<ApiResponse>

    @POST("/api/session/set_mode")
    suspend fun setMode(@Body body: Map<String, String>): Response<ApiResponse>

    @POST("/api/session/clear")
    suspend fun clearActiveSession(): Response<ApiResponse>

    @POST("/api/session/save")
    suspend fun saveSession(): Response<ApiResponse>

    // --- Live Attendance ---
    @GET("/api/attendance")
    suspend fun getLiveAttendance(): Response<List<AttendeeRecord>>

    @POST("/api/attendance/manual")
    suspend fun recordManualAttendance(@Body body: ManualAttendanceRequest): Response<ApiResponse>

    @POST("/api/attendance/delete")
    suspend fun deleteAttendanceRecord(@Body body: Map<String, String>): Response<ApiResponse>

    // --- Session History ---
    @GET("/api/history")
    suspend fun getHistoryList(): Response<List<HistoryItem>>

    @GET("/api/history/session")
    suspend fun getHistorySessionDetails(@Query("id") id: String): Response<HistoryDetailResponse>

    @POST("/api/history/resume")
    suspend fun resumeSession(@Body body: Map<String, String>): Response<ResumeSessionResponse>

    @POST("/api/history/delete")
    suspend fun deleteHistorySession(@Body body: Map<String, String>): Response<ApiResponse>

    @POST("/api/history/clear")
    suspend fun clearAllHistory(): Response<ApiResponse>

    // --- Cards Management ---
    @GET("/api/cards")
    suspend fun getRegisteredCards(): Response<List<RegisteredCardItem>>

    @POST("/api/register/start")
    suspend fun startRegistration(@Body body: RegisterStartRequest): Response<ApiResponse>

    @GET("/api/register/status")
    suspend fun getRegistrationStatus(): Response<RegistrationStatusResponse>

    @POST("/api/register/cancel")
    suspend fun cancelRegistration(): Response<ApiResponse>

    @POST("/api/cards/add")
    suspend fun addOrUpdateCard(@Body body: CardItemRequest): Response<ApiResponse>

    @POST("/api/cards/delete")
    suspend fun deleteCard(@Body body: Map<String, String>): Response<ApiResponse>

    @POST("/api/cards/clear")
    suspend fun clearAllCards(): Response<ApiResponse>

    // --- Hardware & Settings ---
    @GET("/api/settings")
    suspend fun getSettings(): Response<SettingsResponse>

    @POST("/api/settings")
    suspend fun updateSettings(@Body body: Map<String, String>): Response<ApiResponse>

    @POST("/api/lcd/message")
    suspend fun sendLcdMessage(@Body body: LcdMessageRequest): Response<ApiResponse>

    @POST("/api/lcd/sleep")
    suspend fun sleepLcd(): Response<ApiResponse>

    @POST("/api/lcd/wake")
    suspend fun wakeLcd(): Response<ApiResponse>

    @POST("/api/rgb/test")
    suspend fun testRgb(@Query("mode") mode: String): Response<ApiResponse>
}
```

### 11.3 Data Transfer Objects (`AttendanceModels.kt`)
```kotlin
package com.example.attendance.data

import com.google.gson.annotations.SerializedName

data class StatusResponse(
    @SerializedName("session_active") val sessionActive: Boolean,
    @SerializedName("session_type") val sessionType: String,
    @SerializedName("session_title") val sessionTitle: String,
    @SerializedName("current_time") val currentTime: String,
    @SerializedName("registered_count") val registeredCount: Int,
    @SerializedName("attendance_count") val attendanceCount: Int,
    @SerializedName("free_heap") val freeHeap: Long,
    @SerializedName("fs_free") val fsFreeBytes: Long,
    @SerializedName("est_cards_free") val estCardsFree: Long,
    @SerializedName("last_scan") val lastScan: LastScanEvent?
)

data class LastScanEvent(
    val uid: String,
    val name: String,
    val time: String,
    val type: String,
    @SerializedName("is_duplicate") val isDuplicate: Boolean,
    @SerializedName("is_registered") val isRegistered: Boolean,
    val message: String
)

data class AttendeeRecord(
    val index: Int,
    val uid: String,
    val name: String,
    @SerializedName("student_id") val studentId: String,
    @SerializedName("time_in") val timeIn: String,
    @SerializedName("time_out") val timeOut: String
)

data class StartSessionRequest(
    val title: String,
    val type: String,
    @SerializedName("clear_previous") val clearPrevious: String = "0"
)

data class ManualAttendanceRequest(
    val name: String,
    @SerializedName("student_id") val studentId: String = "",
    val type: String = "Entrance"
)

data class CardItemRequest(
    val uid: String,
    val name: String,
    @SerializedName("student_id") val studentId: String = ""
)

data class RegisteredCardItem(
    val index: Int,
    val uid: String,
    val name: String,
    @SerializedName("student_id") val studentId: String,
    @SerializedName("reg_date") val regDate: String
)

data class RegisterStartRequest(
    val name: String,
    @SerializedName("student_id") val studentId: String = "",
    @SerializedName("batch_info") val batchInfo: String = ""
)

data class RegistrationStatusResponse(
    val state: String, // "waiting", "success", "same_card", "conflict", "timeout", "idle"
    val message: String,
    @SerializedName("result_uid") val resultUid: String?,
    @SerializedName("candidate_name") val candidateName: String?,
    @SerializedName("conflict_owner") val conflictOwner: String?
)

data class HistoryItem(
    val id: String,
    val title: String,
    val type: String,
    val date: String,
    @SerializedName("save_time") val saveTime: String,
    @SerializedName("attendee_count") val attendeeCount: Int
)

data class HistoryDetailResponse(
    val success: Boolean,
    val id: String,
    val title: String,
    val type: String,
    val records: List<AttendeeRecord>
)

data class ResumeSessionResponse(
    val success: Boolean,
    val id: String,
    val title: String,
    val type: String,
    val records: List<AttendeeRecord>,
    val message: String
)

data class SettingsResponse(
    val success: Boolean,
    @SerializedName("require_registration") val requireRegistration: Boolean,
    @SerializedName("card_debounce") val cardDebounceMs: Long,
    @SerializedName("lcd_timeout") val lcdTimeoutMs: Long,
    @SerializedName("show_lcd_wifi") val showLcdWifi: Boolean,
    @SerializedName("lcd_brightness_pct") val lcdBrightnessPct: Int,
    @SerializedName("rgb_brightness_pct") val rgbBrightnessPct: Int,
    @SerializedName("wifi_ssid") val wifiSsid: String
)

data class LcdMessageRequest(
    val line1: String,
    val line2: String = "",
    val duration: Long = 3000
)

data class ApiResponse(
    val success: Boolean,
    val message: String?
)
```
