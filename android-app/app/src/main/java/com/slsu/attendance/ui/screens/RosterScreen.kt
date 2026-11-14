package com.slsu.attendance.ui.screens

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
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
import com.slsu.attendance.ui.theme.MdExitOrange
import com.slsu.attendance.ui.theme.MdPrimaryLight
import com.slsu.attendance.viewmodel.AttendanceViewModel
import com.slsu.attendance.viewmodel.UiState

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun RosterScreen(viewModel: AttendanceViewModel, uiState: UiState) {
    var searchQuery by remember { mutableStateOf("") }
    var filterType by remember { mutableStateOf("All") } // "All", "In", "Out"
    var showManualDialog by remember { mutableStateOf(false) }

    val rawList = uiState.attendanceList

    val filteredRecords = remember(rawList, searchQuery, filterType) {
        rawList.filter { rec ->
            val matchesSearch = searchQuery.isBlank() ||
                (rec.name ?: "").contains(searchQuery, ignoreCase = true) ||
                (rec.studentId ?: "").contains(searchQuery, ignoreCase = true) ||
                (rec.uid ?: "").contains(searchQuery, ignoreCase = true)

            val matchesFilter = when (filterType) {
                "In" -> rec.timeOut.isNullOrBlank() || rec.timeOut == "--"
                "Out" -> !rec.timeOut.isNullOrBlank() && rec.timeOut != "--"
                else -> true
            }

            matchesSearch && matchesFilter
        }
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = {
                    Column {
                        Text("Live Attendance Roster", fontWeight = FontWeight.Bold, fontSize = 18.sp)
                        Text(
                            "${rawList.size} Attendees Logged",
                            fontSize = 11.sp,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                },
                actions = {
                    if (rawList.isNotEmpty()) {
                        IconButton(onClick = { viewModel.clearSessionRoster() }) {
                            Icon(Icons.Default.DeleteSweep, contentDescription = "Clear", tint = MaterialTheme.colorScheme.error)
                        }
                    }
                }
            )
        },
        floatingActionButton = {
            ExtendedFloatingActionButton(
                onClick = { showManualDialog = true },
                icon = { Icon(Icons.Default.PersonAdd, contentDescription = null) },
                text = { Text("Manual Entry") },
                containerColor = MdPrimaryLight,
                contentColor = Color.White
            )
        }
    ) { paddingValues ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(paddingValues)
                .padding(horizontal = 16.dp)
        ) {
            // Search Box
            OutlinedTextField(
                value = searchQuery,
                onValueChange = { searchQuery = it },
                modifier = Modifier.fillMaxWidth().padding(top = 4.dp),
                placeholder = { Text("Search name, student ID, or UID...") },
                leadingIcon = { Icon(Icons.Default.Search, contentDescription = null) },
                trailingIcon = {
                    if (searchQuery.isNotEmpty()) {
                        IconButton(onClick = { searchQuery = "" }) {
                            Icon(Icons.Default.Close, contentDescription = null)
                        }
                    }
                },
                shape = RoundedCornerShape(16.dp),
                singleLine = true
            )

            // Filter Chips Row (All / Checked In / Checked Out)
            Row(
                modifier = Modifier.fillMaxWidth().padding(vertical = 10.dp),
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                FilterChip(
                    selected = filterType == "All",
                    onClick = { filterType = "All" },
                    label = { Text("All (${rawList.size})") }
                )
                FilterChip(
                    selected = filterType == "In",
                    onClick = { filterType = "In" },
                    label = { Text("Checked In") }
                )
                FilterChip(
                    selected = filterType == "Out",
                    onClick = { filterType = "Out" },
                    label = { Text("Checked Out") }
                )
            }

            // Attendee List
            if (filteredRecords.isEmpty()) {
                Box(
                    modifier = Modifier.fillMaxSize().padding(bottom = 80.dp),
                    contentAlignment = Alignment.Center
                ) {
                    Column(horizontalAlignment = Alignment.CenterHorizontally) {
                        Icon(Icons.Default.Groups, contentDescription = null, tint = Color.Gray, modifier = Modifier.size(48.dp))
                        Spacer(Modifier.height(8.dp))
                        Text(
                            text = if (rawList.isEmpty()) "No attendees checked in yet.\nTap an RFID card or add manually." else "No records match your filter.",
                            color = Color.Gray,
                            fontSize = 13.sp,
                            textAlign = androidx.compose.ui.text.style.TextAlign.Center
                        )
                    }
                }
            } else {
                LazyColumn(
                    modifier = Modifier.fillMaxSize(),
                    verticalArrangement = Arrangement.spacedBy(8.dp),
                    contentPadding = PaddingValues(bottom = 80.dp)
                ) {
                    items(filteredRecords, key = { it.uid ?: it.studentId ?: it.index.toString() }) { record ->
                        RosterAttendeeCard(
                            record = record,
                            onDelete = { record.uid?.let { viewModel.deleteAttendee(it) } }
                        )
                    }
                }
            }
        }
    }

    if (showManualDialog) {
        var name by remember { mutableStateOf("") }
        var studentId by remember { mutableStateOf("") }
        var type by remember { mutableStateOf("Entrance") }

        AlertDialog(
            onDismissRequest = { showManualDialog = false },
            title = { Text("Manual Check-In", fontWeight = FontWeight.Bold) },
            text = {
                Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                    Text("Manually log an attendee who doesn't have their card.", fontSize = 12.sp, color = Color.Gray)
                    OutlinedTextField(
                        value = name,
                        onValueChange = { name = it },
                        label = { Text("Full Name *") },
                        modifier = Modifier.fillMaxWidth()
                    )
                    OutlinedTextField(
                        value = studentId,
                        onValueChange = { studentId = it },
                        label = { Text("Student / Employee ID") },
                        modifier = Modifier.fillMaxWidth()
                    )
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        FilterChip(
                            selected = type == "Entrance",
                            onClick = { type = "Entrance" },
                            label = { Text("Entrance") },
                            modifier = Modifier.weight(1f)
                        )
                        FilterChip(
                            selected = type == "Exit",
                            onClick = { type = "Exit" },
                            label = { Text("Exit") },
                            modifier = Modifier.weight(1f)
                        )
                    }
                }
            },
            confirmButton = {
                Button(
                    enabled = name.isNotBlank(),
                    onClick = {
                        viewModel.recordManualAttendance(name, studentId, type)
                        showManualDialog = false
                    }
                ) {
                    Text("Record")
                }
            },
            dismissButton = {
                TextButton(onClick = { showManualDialog = false }) {
                    Text("Cancel")
                }
            }
        )
    }
}

@Composable
fun RosterAttendeeCard(record: AttendanceRecord, onDelete: () -> Unit) {
    val isCompleted = !record.timeOut.isNullOrBlank() && record.timeOut != "--"

    Card(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(14.dp),
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.7f))
    ) {
        Row(
            modifier = Modifier.fillMaxWidth().padding(14.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            // Index Pill
            Box(
                modifier = Modifier
                    .size(34.dp)
                    .clip(CircleShape)
                    .background(MaterialTheme.colorScheme.surface),
                contentAlignment = Alignment.Center
            ) {
                Text(
                    text = "${record.index ?: "#"}",
                    fontWeight = FontWeight.Bold,
                    fontSize = 12.sp,
                    color = MaterialTheme.colorScheme.onSurface
                )
            }

            Spacer(Modifier.width(12.dp))

            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = record.name?.ifBlank { "Unregistered Attendee" } ?: "Attendee",
                    fontWeight = FontWeight.Bold,
                    fontSize = 15.sp
                )
                Text(
                    text = "ID: ${record.studentId?.ifBlank { "--" }} • UID: ${record.uid ?: "--"}",
                    fontSize = 12.sp,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                Row(
                    modifier = Modifier.padding(top = 4.dp),
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    Text(
                        text = "In: ${record.timeIn ?: "--"}",
                        fontSize = 11.sp,
                        fontWeight = FontWeight.SemiBold,
                        color = MdPrimaryLight
                    )
                    if (isCompleted) {
                        Text(
                            text = "Out: ${record.timeOut ?: "--"}",
                            fontSize = 11.sp,
                            fontWeight = FontWeight.SemiBold,
                            color = MdExitOrange
                        )
                    }
                }
            }

            // Status Badge
            Surface(
                shape = CircleShape,
                color = if (isCompleted) Color(0xFFF3E8FF) else Color(0xFFD1FAE5)
            ) {
                Text(
                    text = if (isCompleted) "COMPLETED" else "IN",
                    fontWeight = FontWeight.ExtraBold,
                    fontSize = 10.sp,
                    color = if (isCompleted) Color(0xFF7E22CE) else Color(0xFF047857),
                    modifier = Modifier.padding(horizontal = 10.dp, vertical = 4.dp)
                )
            }

            IconButton(onClick = onDelete) {
                Icon(Icons.Default.Close, contentDescription = "Remove", tint = Color.Gray, modifier = Modifier.size(16.dp))
            }
        }
    }
}
