package com.slsu.attendance.ui.screens

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
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
import com.slsu.attendance.data.model.HistorySessionItem
import com.slsu.attendance.viewmodel.AttendanceViewModel
import com.slsu.attendance.viewmodel.UiState

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun HistoryScreen(viewModel: AttendanceViewModel, uiState: UiState) {
    var selectedSession by remember { mutableStateOf<HistorySessionItem?>(null) }
    var sessionToDelete by remember { mutableStateOf<HistorySessionItem?>(null) }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Session History (LittleFS)", fontWeight = FontWeight.Bold) },
                actions = {
                    IconButton(onClick = { viewModel.loadHistory() }) {
                        Icon(Icons.Default.Refresh, contentDescription = "Refresh")
                    }
                }
            )
        }
    ) { paddingValues ->
        LazyColumn(
            modifier = Modifier
                .fillMaxSize()
                .padding(paddingValues)
                .padding(horizontal = 16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            item {
                Card(
                    modifier = Modifier.fillMaxWidth().padding(top = 4.dp),
                    shape = RoundedCornerShape(14.dp),
                    colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant)
                ) {
                    Column(modifier = Modifier.padding(16.dp)) {
                        Text(
                            text = "Stored Sessions (${uiState.historyList.size}/5)",
                            fontSize = 17.sp,
                            fontWeight = FontWeight.Bold
                        )
                        Text(
                            text = "Saved automatically or via 'Save History'. Max 5 sessions kept in ESP32 flash memory.",
                            fontSize = 12.sp,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                }
            }

            if (uiState.historyList.isEmpty()) {
                item {
                    Box(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(vertical = 40.dp),
                        contentAlignment = Alignment.Center
                    ) {
                        Text(
                            text = "No saved attendance sessions in history.\nSave an active session from the Dashboard to see it here.",
                            color = Color.Gray,
                            fontSize = 14.sp,
                            textAlign = androidx.compose.ui.text.style.TextAlign.Center
                        )
                    }
                }
            } else {
                items(uiState.historyList, key = { it.id ?: "" }) { session ->
                    HistoryCard(
                        session = session,
                        onClick = {
                            selectedSession = session
                            session.id?.let { viewModel.loadHistoryDetails(it) }
                        },
                        onResume = { session.id?.let { viewModel.resumeHistorySession(it) } },
                        onDelete = { sessionToDelete = session }
                    )
                }
            }

            item {
                Spacer(modifier = Modifier.height(70.dp))
            }
        }
    }

    // Session Details Dialog
    selectedSession?.let { sess ->
        val details = uiState.currentHistoryDetail
        AlertDialog(
            onDismissRequest = { selectedSession = null },
            title = {
                Column {
                    Text(sess.title?.ifBlank { "Session Details" } ?: "Session Details", fontWeight = FontWeight.Bold)
                    Text("${sess.date} • ${sess.saveTime} • ${sess.type}", fontSize = 12.sp, color = Color.Gray)
                }
            },
            text = {
                Column(modifier = Modifier.fillMaxWidth()) {
                    Text("Total Attendees: ${details?.attendeeCount ?: sess.attendeeCount}", fontWeight = FontWeight.SemiBold)
                    Spacer(Modifier.height(8.dp))
                    Divider()
                    Spacer(Modifier.height(8.dp))

                    if (details?.records.isNullOrEmpty()) {
                        Text("Loading attendee records...", fontSize = 13.sp, color = Color.Gray)
                    } else {
                        LazyColumn(
                            modifier = Modifier.heightIn(max = 280.dp),
                            verticalArrangement = Arrangement.spacedBy(6.dp)
                        ) {
                            items(details!!.records!!) { rec ->
                                Row(
                                    modifier = Modifier.fillMaxWidth().padding(vertical = 4.dp),
                                    horizontalArrangement = Arrangement.SpaceBetween
                                ) {
                                    Column(modifier = Modifier.weight(1f)) {
                                        Text(rec.name ?: "Unknown", fontWeight = FontWeight.Medium, fontSize = 14.sp)
                                        Text("ID: ${rec.studentId ?: "--"}", fontSize = 11.sp, color = Color.Gray)
                                    }
                                    Column(horizontalAlignment = Alignment.End) {
                                        Text("In: ${rec.timeIn ?: "--"}", fontSize = 12.sp)
                                        Text("Out: ${rec.timeOut ?: "--"}", fontSize = 11.sp, color = Color.Gray)
                                    }
                                }
                            }
                        }
                    }
                }
            },
            confirmButton = {
                Button(onClick = {
                    sess.id?.let { viewModel.resumeHistorySession(it) }
                    selectedSession = null
                }) {
                    Text("Resume in Scanner")
                }
            },
            dismissButton = {
                TextButton(onClick = { selectedSession = null }) {
                    Text("Close")
                }
            }
        )
    }

    // Delete Session Confirmation Dialog
    sessionToDelete?.let { sess ->
        AlertDialog(
            onDismissRequest = { sessionToDelete = null },
            title = { Text("Delete Saved Session?") },
            text = {
                Text("Are you sure you want to permanently delete '${sess.title}' from ESP32 LittleFS storage?")
            },
            confirmButton = {
                Button(
                    colors = ButtonDefaults.buttonColors(containerColor = MaterialTheme.colorScheme.error),
                    onClick = {
                        sess.id?.let { viewModel.deleteHistorySession(it) }
                        sessionToDelete = null
                    }
                ) {
                    Text("Delete")
                }
            },
            dismissButton = {
                TextButton(onClick = { sessionToDelete = null }) {
                    Text("Cancel")
                }
            }
        )
    }
}

@Composable
fun HistoryCard(
    session: HistorySessionItem,
    onClick: () -> Unit,
    onResume: () -> Unit,
    onDelete: () -> Unit
) {
    Card(
        modifier = Modifier
            .fillMaxWidth()
            .clickable(onClick = onClick),
        shape = RoundedCornerShape(12.dp),
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface)
    ) {
        Column(modifier = Modifier.padding(14.dp)) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = session.title?.ifBlank { "Untitled Session" } ?: "Session",
                    fontWeight = FontWeight.Bold,
                    fontSize = 16.sp
                )
                Badge(containerColor = MaterialTheme.colorScheme.primaryContainer) {
                    Text("${session.attendeeCount ?: 0} Attendees", color = MaterialTheme.colorScheme.primary, modifier = Modifier.padding(4.dp))
                }
            }

            Spacer(Modifier.height(4.dp))
            Text(
                text = "Mode: ${session.type ?: "Entrance"} • Saved: ${session.date ?: ""} at ${session.saveTime ?: ""}",
                fontSize = 12.sp,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )

            Spacer(Modifier.height(10.dp))
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.End,
                verticalAlignment = Alignment.CenterVertically
            ) {
                IconButton(onClick = onDelete) {
                    Icon(Icons.Default.DeleteOutline, contentDescription = "Delete", tint = Color.Gray)
                }
                Spacer(Modifier.width(8.dp))
                OutlinedButton(onClick = onResume, shape = RoundedCornerShape(8.dp)) {
                    Icon(Icons.Default.Restore, contentDescription = null, modifier = Modifier.size(16.dp))
                    Spacer(Modifier.width(4.dp))
                    Text("Resume")
                }
            }
        }
    }
}
