package com.slsu.attendance.data.network

import com.slsu.attendance.data.model.*
import okhttp3.ResponseBody
import retrofit2.Response
import retrofit2.http.*

interface Esp32ApiService {

    // --- System & Status ---
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
    suspend fun startSession(@Body body: Map<String, String>): Response<ApiResponse>

    @POST("/api/session/stop")
    suspend fun stopSession(): Response<ApiResponse>

    @POST("/api/session/toggle_mode")
    suspend fun toggleMode(): Response<ApiResponse>

    @POST("/api/session/set_mode")
    suspend fun setMode(@Body body: Map<String, String>): Response<ApiResponse>

    @POST("/api/session/clear")
    suspend fun clearSession(): Response<ApiResponse>

    @POST("/api/session/save")
    suspend fun saveSession(): Response<ApiResponse>

    // --- Live Attendance Log ---
    @GET("/api/attendance")
    suspend fun getAttendance(): Response<List<AttendanceRecord>>

    @POST("/api/attendance/manual")
    suspend fun recordManualAttendance(@Body body: Map<String, String>): Response<ApiResponse>

    @POST("/api/attendance/delete")
    suspend fun deleteAttendee(@Body body: Map<String, String>): Response<ApiResponse>

    // --- Session History ---
    @GET("/api/history")
    suspend fun getHistory(): Response<List<HistorySessionItem>>

    @GET("/api/history/session")
    suspend fun getHistoryDetails(@Query("id") sessionId: String): Response<HistoryDetailsResponse>

    @POST("/api/history/resume")
    suspend fun resumeHistorySession(@Body body: Map<String, String>): Response<ApiResponse>

    @POST("/api/history/delete")
    suspend fun deleteHistorySession(@Body body: Map<String, String>): Response<ApiResponse>

    @POST("/api/history/clear")
    suspend fun clearAllHistory(): Response<ApiResponse>

    // --- Registered Cards ---
    @GET("/api/cards")
    suspend fun getCards(): Response<List<CardItem>>

    @POST("/api/register/start")
    suspend fun startRegistration(@Body body: Map<String, String>): Response<ApiResponse>

    @GET("/api/register/status")
    suspend fun getRegistrationStatus(): Response<RegisterStatusResponse>

    @POST("/api/register/cancel")
    suspend fun cancelRegistration(): Response<ApiResponse>

    @POST("/api/cards/add")
    suspend fun addCardDirect(@Body body: Map<String, String>): Response<ApiResponse>

    @POST("/api/cards/delete")
    suspend fun deleteCard(@Body body: Map<String, String>): Response<ApiResponse>

    @POST("/api/cards/clear")
    suspend fun clearCards(): Response<ApiResponse>

    // --- Hardware Controls ---
    @POST("/api/lcd/message")
    suspend fun showLcdMessage(@Body body: Map<String, @JvmSuppressWildcards Any>): Response<ApiResponse>

    @POST("/api/lcd/sleep")
    suspend fun sleepLcd(): Response<ApiResponse>

    @POST("/api/lcd/wake")
    suspend fun wakeLcd(): Response<ApiResponse>

    @POST("/api/rgb/test")
    suspend fun testRgbLed(@Query("mode") mode: String): Response<ApiResponse>

    // --- Settings ---
    @GET("/api/settings")
    suspend fun getSettings(): Response<Map<String, Any>>

    @POST("/api/settings")
    suspend fun updateSettings(@Body body: Map<String, String>): Response<ApiResponse>

    // --- Data Export ---
    @GET("/api/attendance/export.csv")
    @Streaming
    suspend fun downloadAttendanceCsv(): Response<ResponseBody>

    @GET("/api/history/export.csv")
    @Streaming
    suspend fun downloadHistoryCsv(@Query("id") sessionId: String): Response<ResponseBody>

    @GET("/api/cards/export.csv")
    @Streaming
    suspend fun downloadCardsCsv(): Response<ResponseBody>
}
