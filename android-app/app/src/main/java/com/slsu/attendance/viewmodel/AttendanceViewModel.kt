package com.slsu.attendance.viewmodel

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.slsu.attendance.data.model.*
import com.slsu.attendance.data.network.NetworkClient
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import java.util.TimeZone

data class UiState(
    val isConnected: Boolean = false,
    val connectionError: String? = null,
    val status: StatusResponse? = null,
    val attendanceList: List<AttendanceRecord> = emptyList(),
    val registeredCards: List<CardItem> = emptyList(),
    val historyList: List<HistorySessionItem> = emptyList(),
    val currentHistoryDetail: HistoryDetailsResponse? = null,
    val registrationState: RegisterStatusResponse? = null,
    val isRegistering: Boolean = false,
    val infoMessage: String? = null
)

class AttendanceViewModel : ViewModel() {

    private val api get() = NetworkClient.apiService

    private val _uiState = MutableStateFlow(UiState())
    val uiState: StateFlow<UiState> = _uiState.asStateFlow()

    private var pollingJob: Job? = null
    private var regPollingJob: Job? = null
    private var lastAttendanceCount: Int = -1

    init {
        startStatusPolling()
        loadCards()
        loadHistory()
    }

    fun startStatusPolling() {
        pollingJob?.cancel()
        pollingJob = viewModelScope.launch(Dispatchers.IO) {
            while (isActive) {
                try {
                    val resp = api.getStatus()
                    if (resp.isSuccessful && resp.body() != null) {
                        val status = resp.body()!!
                        val currentCount = status.attendanceCount ?: 0
                        val shouldFetchRoster = currentCount != lastAttendanceCount || _uiState.value.attendanceList.isEmpty()
                        lastAttendanceCount = currentCount

                        _uiState.value = _uiState.value.copy(
                            isConnected = true,
                            connectionError = null,
                            status = status
                        )

                        if (shouldFetchRoster && (status.sessionActive == true || currentCount > 0)) {
                            fetchLiveRosterInternal()
                        }
                    } else {
                        _uiState.value = _uiState.value.copy(
                            isConnected = false,
                            connectionError = "HTTP ${resp.code()}: ${resp.message()}"
                        )
                    }
                } catch (e: Exception) {
                    _uiState.value = _uiState.value.copy(
                        isConnected = false,
                        connectionError = e.localizedMessage ?: "Connection failed"
                    )
                }
                delay(1500)
            }
        }
    }

    private suspend fun fetchLiveRosterInternal() {
        try {
            val resp = api.getAttendance()
            if (resp.isSuccessful && resp.body() != null) {
                _uiState.value = _uiState.value.copy(attendanceList = resp.body()!!)
            }
        } catch (_: Exception) {}
    }

    fun refreshAll() {
        viewModelScope.launch(Dispatchers.IO) {
            fetchLiveRosterInternal()
            loadCards()
            loadHistory()
        }
    }

    fun startSession(title: String, type: String, clearPrevious: Boolean) {
        viewModelScope.launch(Dispatchers.IO) {
            try {
                val body = mapOf(
                    "title" to title.ifBlank { "Class Session" },
                    "type" to type,
                    "clear_previous" to if (clearPrevious) "1" else "0"
                )
                val resp = api.startSession(body)
                if (resp.isSuccessful) {
                    showMessage("Session started: $title ($type)")
                    fetchLiveRosterInternal()
                } else {
                    showMessage("Failed to start session: ${resp.message()}")
                }
            } catch (e: Exception) {
                showMessage("Error: ${e.message}")
            }
        }
    }

    fun stopSession() {
        viewModelScope.launch(Dispatchers.IO) {
            try {
                val resp = api.stopSession()
                if (resp.isSuccessful) {
                    showMessage("Session stopped.")
                }
            } catch (e: Exception) {
                showMessage("Error: ${e.message}")
            }
        }
    }

    fun toggleMode() {
        viewModelScope.launch(Dispatchers.IO) {
            try {
                val resp = api.toggleMode()
                if (resp.isSuccessful) {
                    showMessage("Mode switched to ${resp.body()?.type ?: "updated mode"}")
                }
            } catch (e: Exception) {
                showMessage("Error: ${e.message}")
            }
        }
    }

    fun clearSessionRoster() {
        viewModelScope.launch(Dispatchers.IO) {
            try {
                val resp = api.clearSession()
                if (resp.isSuccessful) {
                    showMessage("Session records cleared.")
                    _uiState.value = _uiState.value.copy(attendanceList = emptyList())
                }
            } catch (e: Exception) {
                showMessage("Error: ${e.message}")
            }
        }
    }

    fun saveSessionToHistory() {
        viewModelScope.launch(Dispatchers.IO) {
            try {
                val resp = api.saveSession()
                if (resp.isSuccessful) {
                    showMessage("Session saved to history successfully!")
                    loadHistory()
                } else {
                    showMessage("Failed: ${resp.message()}")
                }
            } catch (e: Exception) {
                showMessage("Error: ${e.message}")
            }
        }
    }

    fun recordManualAttendance(name: String, studentId: String, type: String) {
        viewModelScope.launch(Dispatchers.IO) {
            try {
                val body = mapOf(
                    "name" to name,
                    "student_id" to studentId,
                    "type" to type
                )
                val resp = api.recordManualAttendance(body)
                if (resp.isSuccessful) {
                    showMessage("Manual attendance recorded for $name")
                    fetchLiveRosterInternal()
                } else {
                    showMessage("Failed: ${resp.message()}")
                }
            } catch (e: Exception) {
                showMessage("Error: ${e.message}")
            }
        }
    }

    fun deleteAttendee(uid: String) {
        viewModelScope.launch(Dispatchers.IO) {
            try {
                val resp = api.deleteAttendee(mapOf("uid" to uid))
                if (resp.isSuccessful) {
                    showMessage("Attendee removed.")
                    fetchLiveRosterInternal()
                }
            } catch (e: Exception) {
                showMessage("Error: ${e.message}")
            }
        }
    }

    // --- Card Database & Registration ---
    fun loadCards() {
        viewModelScope.launch(Dispatchers.IO) {
            try {
                val resp = api.getCards()
                if (resp.isSuccessful && resp.body() != null) {
                    _uiState.value = _uiState.value.copy(registeredCards = resp.body()!!)
                }
            } catch (_: Exception) {}
        }
    }

    fun startCardRegistration(name: String, studentId: String) {
        viewModelScope.launch(Dispatchers.IO) {
            try {
                val resp = api.startRegistration(mapOf("name" to name, "student_id" to studentId))
                if (resp.isSuccessful) {
                    _uiState.value = _uiState.value.copy(
                        isRegistering = true,
                        registrationState = RegisterStatusResponse(state = "waiting", message = "Please tap card on reader...")
                    )
                    pollRegistrationStatus()
                } else {
                    showMessage("Failed to arm registration.")
                }
            } catch (e: Exception) {
                showMessage("Error: ${e.message}")
            }
        }
    }

    private fun pollRegistrationStatus() {
        regPollingJob?.cancel()
        regPollingJob = viewModelScope.launch(Dispatchers.IO) {
            while (isActive) {
                try {
                    val resp = api.getRegistrationStatus()
                    if (resp.isSuccessful && resp.body() != null) {
                        val reg = resp.body()!!
                        _uiState.value = _uiState.value.copy(registrationState = reg)

                        if (reg.state == "success" || reg.state == "conflict" || reg.state == "same_card" || reg.state == "timeout") {
                            if (reg.state == "success" || reg.state == "same_card") {
                                loadCards()
                            }
                            break
                        }
                    }
                } catch (_: Exception) {}
                delay(500)
            }
        }
    }

    fun cancelRegistration() {
        regPollingJob?.cancel()
        viewModelScope.launch(Dispatchers.IO) {
            try {
                api.cancelRegistration()
            } catch (_: Exception) {}
            _uiState.value = _uiState.value.copy(isRegistering = false, registrationState = null)
        }
    }

    fun deleteCard(uid: String) {
        viewModelScope.launch(Dispatchers.IO) {
            try {
                val resp = api.deleteCard(mapOf("uid" to uid))
                if (resp.isSuccessful) {
                    showMessage("Card removed.")
                    loadCards()
                }
            } catch (e: Exception) {
                showMessage("Error: ${e.message}")
            }
        }
    }

    // --- History ---
    fun loadHistory() {
        viewModelScope.launch(Dispatchers.IO) {
            try {
                val resp = api.getHistory()
                if (resp.isSuccessful && resp.body() != null) {
                    _uiState.value = _uiState.value.copy(historyList = resp.body()!!)
                }
            } catch (_: Exception) {}
        }
    }

    fun loadHistoryDetails(sessionId: String) {
        viewModelScope.launch(Dispatchers.IO) {
            try {
                val resp = api.getHistoryDetails(sessionId)
                if (resp.isSuccessful) {
                    _uiState.value = _uiState.value.copy(currentHistoryDetail = resp.body())
                }
            } catch (_: Exception) {}
        }
    }

    fun resumeHistorySession(sessionId: String) {
        viewModelScope.launch(Dispatchers.IO) {
            try {
                val resp = api.resumeHistorySession(mapOf("id" to sessionId))
                if (resp.isSuccessful) {
                    showMessage("Session resumed into active scanner.")
                    fetchLiveRosterInternal()
                }
            } catch (e: Exception) {
                showMessage("Error: ${e.message}")
            }
        }
    }

    fun deleteHistorySession(sessionId: String) {
        viewModelScope.launch(Dispatchers.IO) {
            try {
                val resp = api.deleteHistorySession(mapOf("id" to sessionId))
                if (resp.isSuccessful) {
                    showMessage("Session deleted from history.")
                    loadHistory()
                }
            } catch (e: Exception) {
                showMessage("Error: ${e.message}")
            }
        }
    }

    // --- Hardware & System ---
    fun syncPhoneTime() {
        viewModelScope.launch(Dispatchers.IO) {
            try {
                val epoch = System.currentTimeMillis() / 1000
                val tzOffset = -TimeZone.getDefault().rawOffset / (1000 * 60)
                val body = mapOf(
                    "epoch" to epoch,
                    "tz_offset" to tzOffset
                )
                val resp = api.syncTime(body)
                if (resp.isSuccessful) {
                    showMessage("ESP32 clock synchronized with phone!")
                } else {
                    showMessage("Failed to sync time.")
                }
            } catch (e: Exception) {
                showMessage("Error: ${e.message}")
            }
        }
    }

    fun setLcdMessage(line1: String, line2: String) {
        viewModelScope.launch(Dispatchers.IO) {
            try {
                api.showLcdMessage(mapOf("line1" to line1, "line2" to line2, "duration" to 4000))
                showMessage("Message sent to 16x2 LCD.")
            } catch (e: Exception) {
                showMessage("Error: ${e.message}")
            }
        }
    }

    fun toggleLcdSleep(turnOff: Boolean) {
        viewModelScope.launch(Dispatchers.IO) {
            try {
                if (turnOff) api.sleepLcd() else api.wakeLcd()
                showMessage(if (turnOff) "LCD display powered down" else "LCD display awakened")
            } catch (e: Exception) {
                showMessage("Error: ${e.message}")
            }
        }
    }

    fun testRgb(color: String) {
        viewModelScope.launch(Dispatchers.IO) {
            try {
                api.testRgbLed(color)
                showMessage("Testing LED: $color")
            } catch (e: Exception) {
                showMessage("Error: ${e.message}")
            }
        }
    }

    fun rebootDevice() {
        viewModelScope.launch(Dispatchers.IO) {
            try {
                api.reboot()
                showMessage("Reboot signal sent. Reconnecting shortly...")
            } catch (e: Exception) {
                showMessage("Error: ${e.message}")
            }
        }
    }

    fun updateHost(newHost: String) {
        NetworkClient.updateBaseUrl(newHost)
        startStatusPolling()
        refreshAll()
    }

    fun showMessage(msg: String) {
        _uiState.value = _uiState.value.copy(infoMessage = msg)
    }

    fun clearMessage() {
        _uiState.value = _uiState.value.copy(infoMessage = null)
    }
}
