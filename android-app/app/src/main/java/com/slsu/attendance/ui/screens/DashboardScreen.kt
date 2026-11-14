package com.slsu.attendance.ui.screens

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.slsu.attendance.data.model.AttendanceRecord
import com.slsu.attendance.viewmodel.AttendanceViewModel
import com.slsu.attendance.viewmodel.UiState

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun DashboardScreen(viewModel: AttendanceViewModel, uiState: UiState) {
    var showStartSessionDialog by remember { mutableStateOf(false) }
    var showManualEntryDialog by remember { mutableStateOf(false) }
    var searchQuery by remember { mutableStateOf("") }

    val status = uiState.status
    val isSessionActive = status?.sessionActive == true

    val filteredRecords = remember(uiState.attendanceList, searchQuery) {
        if (searchQuery.isBlank()) uiState.attendanceList
        else uiState.attendanceList.filter {
            (it.name ?: "").contains(searchQuery, ignoreCase = true) ||
            (it.studentId ?: "").contains(searchQuery, ignoreCase = true) ||
            (it.uid ?: "").contains(searchQuery, ignoreCase = true)
        }
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = {
                    Column {
                        Text("ESP32 Attendance Terminal", fontWeight = FontWeight.Bold, fontSize = 18.sp)
                        Text(
                            text = if (uiState.isConnected) "● Connected to ESP32 Hotspot" else "○ Disconnected (Connect to ESP32-Attendance)",
                            color = if (uiState.isConnected) Color(0xFF10B981) else Color(0xFFEF4444),
                            fontSize = 12.sp
                        )
                    }
                },
                actions = {
                    IconButton(onClick = { viewModel.refreshAll() }) {
                        Icon(Icons.Default.Refresh, contentDescription = "Refresh")
                    }
                }
            )
        },
        floatingActionButton = {
            if (isSessionActive) {
                FloatingActionButton(
                    onClick = { showManualEntryDialog = true },
                    containerColor = MaterialTheme.colorScheme.primary
                ) {
                    Icon(Icons.Default.PersonAdd, contentDescription = "Manual Entry")
                }
            }
        }
    ) { paddingValues ->
        LazyColumn(
            modifier = Modifier
                .fillMaxSize()
                .padding(paddingValues)
                .padding(horizontal = 16.dp),
            verticalArrangement = Arrangement.spacedBy(14.dp)
        ) {
            item {
                Spacer(modifier = Modifier.height(2.dp))
                // Active Session Overview Card
                Card(
                    modifier = Modifier.fillMaxWidth(),
                    shape = RoundedCornerShape(16.dp),
                    colors = CardDefaults.cardColors(
                        containerColor = if (isSessionActive) MaterialTheme.colorScheme.primaryContainer else MaterialTheme.colorScheme.surfaceVariant
                    )
                ) {
                    Column(modifier = Modifier.padding(16.dp)) {
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.SpaceBetween,
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Text(
                                text = if (isSessionActive) "ACTIVE SESSION" else "NO SESSION ACTIVE",
                                fontSize = 12.sp,
                                fontWeight = FontWeight.Bold,
                                color = if (isSessionActive) MaterialTheme.colorScheme.primary else Color.Gray
                            )

                            if (isSessionActive) {
                                FilterChip(
                                    selected = true,
                                    onClick = { viewModel.toggleMode() },
                                    label = { Text("Mode: ${status?.sessionType ?: "Entrance"}") },
                                    leadingIcon = { Icon(Icons.Default.SwapHoriz, contentDescription = null, Modifier.size(16.dp)) }
                                )
                            }
                        }

                        Text(
                            text = if (isSessionActive) (status?.sessionTitle ?: "Active Class") else "Scanner is Idle",
                            fontSize = 20.sp,
                            fontWeight = FontWeight.Bold,
                            modifier = Modifier.padding(vertical = 4.dp)
                        )

                        Text(
                            text = "ESP32 Time: ${status?.currentTime ?: "--"}",
                            fontSize = 13.sp,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )

                        Spacer(modifier = Modifier.height(12.dp))

                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.spacedBy(8.dp)
                        ) {
                            if (!isSessionActive) {
                                Button(
                                    onClick = { showStartSessionDialog = true },
                                    modifier = Modifier.weight(1f),
                                    shape = RoundedCornerShape(10.dp)
                                ) {
                                    Icon(Icons.Default.PlayArrow, contentDescription = null)
                                    Spacer(Modifier.width(6.dp))
                                    Text("Start Session")
                                }
                            } else {
                                OutlinedButton(
                                    onClick = { viewModel.saveSessionToHistory() },
                                    modifier = Modifier.weight(1f),
                                    shape = RoundedCornerShape(10.dp)
                                ) {
                                    Icon(Icons.Default.Save, contentDescription = null)
                                    Spacer(Modifier.width(6.dp))
                                    Text("Save History")
                                }

                                Button(
                                    onClick = { viewModel.stopSession() },
                                    colors = ButtonDefaults.buttonColors(containerColor = MaterialTheme.colorScheme.error),
                                    modifier = Modifier.weight(1f),
                                    shape = RoundedCornerShape(10.dp)
                                ) {
                                    Icon(Icons.Default.Stop, contentDescription = null)
                                    Spacer(Modifier.width(6.dp))
                                    Text("Stop")
                                }
                            }
                        }
                    }
                }
            }

            // Real-Time Stats Row
            item {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(10.dp)
                ) {
                    StatBox(
                        title = "Attendees",
                        value = "${status?.attendanceCount ?: uiState.attendanceList.size}",
                        icon = Icons.Default.Groups,
                        modifier = Modifier.weight(1f)
                    )
                    StatBox(
                        title = "Registered Cards",
                        value = "${status?.registeredCount ?: 0}",
                        icon = Icons.Default.CreditCard,
                        modifier = Modifier.weight(1f)
                    )
                    StatBox(
                        title = "Clients",
                        value = "${status?.wifiClients ?: 1}",
                        icon = Icons.Default.Wifi,
                        modifier = Modifier.weight(1f)
                    )
                }
            }

            // Last Scan Banner
            status?.lastScan?.let { last ->
                if (!last.uid.isNullOrBlank()) {
                    item {
                        Card(
                            modifier = Modifier.fillMaxWidth(),
                            shape = RoundedCornerShape(12.dp),
                            colors = CardDefaults.cardColors(
                                containerColor = if (last.isDuplicate == true) Color(0xFFFEF3C7) else Color(0xFFD1FAE5)
                            )
                        ) {
                            Row(
                                modifier = Modifier.padding(14.dp),
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                Icon(
                                    imageVector = if (last.isDuplicate == true) Icons.Default.Warning else Icons.Default.CheckCircle,
                                    contentDescription = null,
                                    tint = if (last.isDuplicate == true) Color(0xFFD97706) else Color(0xFF059669),
                                    modifier = Modifier.size(32.dp)
                                )
                                Spacer(modifier = Modifier.width(12.dp))
                                Column {
                                    Text(
                                        text = last.name?.ifBlank { "Unregistered Card (${last.uid})" } ?: "Scan Event",
                                        fontWeight = FontWeight.Bold,
                                        fontSize = 15.sp,
                                        color = Color.Black
                                    )
                                    Text(
                                        text = "${last.type ?: "Entrance"} at ${last.time ?: "--"} • ${last.message ?: ""}",
                                        fontSize = 12.sp,
                                        color = Color.DarkGray
                                    )
                                }
                            }
                        }
                    }
                }
            }

            // Search Bar & Roster Header
            item {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Text(
                        text = "Live Attendee Roster (${filteredRecords.size})",
                        fontWeight = FontWeight.Bold,
                        fontSize = 16.sp
                    )
                    if (isSessionActive && uiState.attendanceList.isNotEmpty()) {
                        TextButton(onClick = { viewModel.clearSessionRoster() }) {
                            Text("Clear Roster", color = MaterialTheme.colorScheme.error)
                        }
                    }
                }

                OutlinedTextField(
                    value = searchQuery,
                    onValueChange = { searchQuery = it },
                    modifier = Modifier.fillMaxWidth().padding(top = 8.dp),
                    placeholder = { Text("Search by name, student ID, or card UID...") },
                    leadingIcon = { Icon(Icons.Default.Search, contentDescription = null) },
                    trailingIcon = {
                        if (searchQuery.isNotEmpty()) {
                            IconButton(onClick = { searchQuery = "" }) {
                                Icon(Icons.Default.Close, contentDescription = null)
                            }
                        }
                    },
                    shape = RoundedCornerShape(12.dp),
                    singleLine = true
                )
            }

            // Attendance List
            if (filteredRecords.isEmpty()) {
                item {
                    Box(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(vertical = 40.dp),
                        contentAlignment = Alignment.Center
                    ) {
                        Text(
                            text = if (isSessionActive) "No attendance scans recorded yet.\nTap an RFID card on the terminal." else "Start a session to begin logging attendance.",
                            color = Color.Gray,
                            fontSize = 14.sp,
                            textAlign = androidx.compose.ui.text.style.TextAlign.Center
                        )
                    }
                }
            } else {
                items(filteredRecords, key = { it.uid ?: it.studentId ?: it.index.toString() }) { record ->
                    AttendeeRow(record = record, onDelete = { record.uid?.let { viewModel.deleteAttendee(it) } })
                }
            }

            item {
                Spacer(modifier = Modifier.height(70.dp))
            }
        }
    }

    // Start Session Dialog
    if (showStartSessionDialog) {
        var sessionTitle by remember { mutableStateOf("") }
        var sessionType by remember { mutableStateOf("Entrance") }
        var clearPrevious by remember { mutableStateOf(true) }

        AlertDialog(
            onDismissRequest = { showStartSessionDialog = false },
            title = { Text("Start Attendance Session") },
            text = {
                Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                    OutlinedTextField(
                        value = sessionTitle,
                        onValueChange = { sessionTitle = it },
                        label = { Text("Session Name / Subject") },
                        placeholder = { Text("e.g. CS101 Finals, Assembly") },
                        modifier = Modifier.fillMaxWidth()
                    )

                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        FilterChip(
                            selected = sessionType == "Entrance",
                            onClick = { sessionType = "Entrance" },
                            label = { Text("Entrance") },
                            modifier = Modifier.weight(1f)
                        )
                        FilterChip(
                            selected = sessionType == "Exit",
                            onClick = { sessionType = "Exit" },
                            label = { Text("Exit") },
                            modifier = Modifier.weight(1f)
                        )
                    }

                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Checkbox(checked = clearPrevious, onCheckedChange = { clearPrevious = it })
                        Text("Reset previous session records")
                    }
                }
            },
            confirmButton = {
                Button(onClick = {
                    viewModel.startSession(sessionTitle, sessionType, clearPrevious)
                    showStartSessionDialog = false
                }) {
                    Text("Start")
                }
            },
            dismissButton = {
                TextButton(onClick = { showStartSessionDialog = false }) {
                    Text("Cancel")
                }
            }
        )
    }

    // Manual Entry Dialog
    if (showManualEntryDialog) {
        var manualName by remember { mutableStateOf("") }
        var manualId by remember { mutableStateOf("") }
        var manualType by remember { mutableStateOf(status?.sessionType ?: "Entrance") }

        AlertDialog(
            onDismissRequest = { showManualEntryDialog = false },
            title = { Text("Manual Attendance Entry") },
            text = {
                Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                    Text("Manually record an attendee who forgot their RFID card.", fontSize = 12.sp, color = Color.Gray)
                    OutlinedTextField(
                        value = manualName,
                        onValueChange = { manualName = it },
                        label = { Text("Full Name *") },
                        modifier = Modifier.fillMaxWidth()
                    )
                    OutlinedTextField(
                        value = manualId,
                        onValueChange = { manualId = it },
                        label = { Text("Student / Employee ID") },
                        modifier = Modifier.fillMaxWidth()
                    )
                }
            },
            confirmButton = {
                Button(
                    enabled = manualName.isNotBlank(),
                    onClick = {
                        viewModel.recordManualAttendance(manualName, manualId, manualType)
                        showManualEntryDialog = false
                    }
                ) {
                    Text("Record")
                }
            },
            dismissButton = {
                TextButton(onClick = { showManualEntryDialog = false }) {
                    Text("Cancel")
                }
            }
        )
    }
}

@Composable
fun StatBox(title: String, value: String, icon: androidx.compose.ui.graphics.vector.ImageVector, modifier: Modifier = Modifier) {
    Card(
        modifier = modifier,
        shape = RoundedCornerShape(12.dp),
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant)
    ) {
        Column(
            modifier = Modifier.padding(12.dp),
            horizontalAlignment = Alignment.Start
        ) {
            Icon(icon, contentDescription = null, tint = MaterialTheme.colorScheme.primary, modifier = Modifier.size(20.dp))
            Spacer(modifier = Modifier.height(4.dp))
            Text(text = value, fontSize = 20.sp, fontWeight = FontWeight.Bold)
            Text(text = title, fontSize = 11.sp, color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
    }
}

@Composable
fun AttendeeRow(record: AttendanceRecord, onDelete: () -> Unit) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(10.dp),
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface)
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(12.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Box(
                modifier = Modifier
                    .size(36.dp)
                    .clip(RoundedCornerShape(8.dp))
                    .background(MaterialTheme.colorScheme.primaryContainer),
                contentAlignment = Alignment.Center
            ) {
                Text(
                    text = "${record.index ?: "#"}",
                    fontWeight = FontWeight.Bold,
                    color = MaterialTheme.colorScheme.primary
                )
            }

            Spacer(modifier = Modifier.width(12.dp))

            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = record.name?.ifBlank { "Unregistered (${record.uid})" } ?: "Attendee",
                    fontWeight = FontWeight.SemiBold,
                    fontSize = 15.sp
                )
                Text(
                    text = "ID: ${record.studentId?.ifBlank { "--" }} • In: ${record.timeIn ?: "--"} • Out: ${record.timeOut ?: "--"}",
                    fontSize = 12.sp,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }

            IconButton(onClick = onDelete) {
                Icon(Icons.Default.DeleteOutline, contentDescription = "Delete", tint = Color.Gray)
            }
        }
    }
}
