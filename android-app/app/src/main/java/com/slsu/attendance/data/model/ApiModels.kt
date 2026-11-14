package com.slsu.attendance.data.model

import com.google.gson.annotations.SerializedName

data class StatusResponse(
    @SerializedName("time_set") val timeSet: Boolean? = false,
    @SerializedName("current_time") val currentTime: String? = null,
    @SerializedName("session_active") val sessionActive: Boolean? = false,
    @SerializedName("session_type") val sessionType: String? = "Entrance",
    @SerializedName("session_title") val sessionTitle: String? = "No Active Session",
    @SerializedName("require_registration") val requireRegistration: Boolean? = false,
    @SerializedName("is_settings_open") val isSettingsOpen: Boolean? = false,
    @SerializedName("reg_state") val regState: String? = "idle",
    @SerializedName("registered_count") val registeredCount: Int? = 0,
    @SerializedName("attendance_count") val attendanceCount: Int? = 0,
    @SerializedName("history_count") val historyCount: Int? = 0,
    @SerializedName("free_heap") val freeHeap: Long? = 0,
    @SerializedName("total_heap") val totalHeap: Long? = 0,
    @SerializedName("fs_used") val fsUsed: Long? = 0,
    @SerializedName("fs_total") val fsTotal: Long? = 0,
    @SerializedName("fs_free") val fsFree: Long? = 0,
    @SerializedName("wifi_clients") val wifiClients: Int? = 0,
    @SerializedName("wifi_ssid") val wifiSsid: String? = "ESP32-Attendance",
    @SerializedName("uptime_sec") val uptimeSec: Long? = 0,
    @SerializedName("last_scan") val lastScan: LastScan? = null
)

data class LastScan(
    @SerializedName("uid") val uid: String? = "",
    @SerializedName("name") val name: String? = "",
    @SerializedName("time") val time: String? = "",
    @SerializedName("type") val type: String? = "",
    @SerializedName("is_duplicate") val isDuplicate: Boolean? = false,
    @SerializedName("is_registered") val isRegistered: Boolean? = true,
    @SerializedName("message") val message: String? = ""
)

data class AttendanceRecord(
    @SerializedName("index") val index: Int? = null,
    @SerializedName("uid") val uid: String? = "",
    @SerializedName("name") val name: String? = "",
    @SerializedName("student_id") val studentId: String? = "",
    @SerializedName("time_in") val timeIn: String? = "--",
    @SerializedName("time_out") val timeOut: String? = "--",
    @SerializedName("status") val status: String? = null
)

data class CardItem(
    @SerializedName("index") val index: Int? = null,
    @SerializedName("uid") val uid: String? = "",
    @SerializedName("name") val name: String? = "",
    @SerializedName("student_id") val studentId: String? = "",
    @SerializedName("reg_date") val regDate: String? = ""
)

data class RegisterStatusResponse(
    @SerializedName("state") val state: String? = "idle",
    @SerializedName("message") val message: String? = "",
    @SerializedName("result_uid") val resultUid: String? = "",
    @SerializedName("candidate_name") val candidateName: String? = "",
    @SerializedName("candidate_id") val candidateId: String? = "",
    @SerializedName("conflict_owner") val conflictOwner: String? = "",
    @SerializedName("conflict_id") val conflictId: String? = ""
)

data class HistorySessionItem(
    @SerializedName("id") val id: String? = "",
    @SerializedName("title") val title: String? = "",
    @SerializedName("type") val type: String? = "",
    @SerializedName("date") val date: String? = "",
    @SerializedName("save_time") val saveTime: String? = "",
    @SerializedName("attendee_count") val attendeeCount: Int? = 0
)

data class HistoryDetailsResponse(
    @SerializedName("success") val success: Boolean? = false,
    @SerializedName("id") val id: String? = "",
    @SerializedName("title") val title: String? = "",
    @SerializedName("type") val type: String? = "",
    @SerializedName("date") val date: String? = "",
    @SerializedName("save_time") val saveTime: String? = "",
    @SerializedName("attendee_count") val attendeeCount: Int? = 0,
    @SerializedName("records") val records: List<AttendanceRecord>? = emptyList()
)

data class ApiResponse(
    @SerializedName("success") val success: Boolean? = true,
    @SerializedName("message") val message: String? = "",
    @SerializedName("id") val id: String? = null,
    @SerializedName("title") val title: String? = null,
    @SerializedName("type") val type: String? = null,
    @SerializedName("time") val time: String? = null,
    @SerializedName("attendee_count") val attendeeCount: Int? = null,
    @SerializedName("state") val state: String? = null
)
