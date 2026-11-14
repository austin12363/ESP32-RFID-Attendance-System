package com.slsu.attendance.ui.screens

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
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.slsu.attendance.data.model.AttendanceRecord
import com.slsu.attendance.viewmodel.AttendanceViewModel
import com.slsu.attendance.viewmodel.UiState

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun RosterScreen(viewModel: AttendanceViewModel, uiState: UiState) {
    var searchQuery by remember { mutableStateOf("") }
    var filterType by remember { mutableStateOf("All") }
    var showManualDialog by remember { mutableStateOf(false) }

    val rawList = uiState.attendanceList
    val primaryColor = MaterialTheme.colorScheme.primary

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
        containerColor = MaterialTheme.colorScheme.background,
        floatingActionButton = {
            ExtendedFloatingActionButton(
                onClick = { showManualDialog = true },
                icon = { Icon(Icons.Default.PersonAdd, contentDescription = null) },
                text = { Text("Manual Entry", fontWeight = FontWeight.Bold) },
                containerColor = primaryColor,
                contentColor = Color.White,
                shape = RoundedCornerShape(20.dp)
            )
        }
    ) { paddingValues ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(paddingValues)
                .padding(horizontal = 20.dp, vertical = 12.dp),
            verticalArrangement = Arrangement.spacedBy(14.dp)
        ) {
            // Header
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Column {
                    Text(
                        text = "LIVE ROSTER",
                        style = MaterialTheme.typography.labelSmall,
                        fontWeight = FontWeight.Black,
                        letterSpacing = 1.2.sp,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    Text(
                        text = "Attendees (${rawList.size})",
                        style = MaterialTheme.typography.headlineMedium,
                        fontWeight = FontWeight.Black
                    )
                }

                if (rawList.isNotEmpty()) {
                    IconButton(onClick = { viewModel.clearSessionRoster() }) {
                        Icon(Icons.Default.DeleteSweep, contentDescription = "Clear", tint = MaterialTheme.colorScheme.error)
                    }
                }
            }

            // Material You Search Pill (Full Capsule)
            OutlinedTextField(
                value = searchQuery,
                onValueChange = { searchQuery = it },
                modifier = Modifier.fillMaxWidth(),
                placeholder = { Text("Search by name, student ID, or UID...") },
                leadingIcon = { Icon(Icons.Default.Search, contentDescription = null) },
                trailingIcon = {
                    if (searchQuery.isNotEmpty()) {
                        IconButton(onClick = { searchQuery = "" }) {
                            Icon(Icons.Default.Close, contentDescription = null)
                        }
                    }
                },
                shape = CircleShape,
                singleLine = true,
                colors = OutlinedTextFieldDefaults.colors(
                    focusedContainerColor = MaterialTheme.colorScheme.surfaceVariant,
                    unfocusedContainerColor = MaterialTheme.colorScheme.surfaceVariant
                )
            )

            // Filter Pills Row
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                FilterChip(
                    selected = filterType == "All",
                    onClick = { filterType = "All" },
                    label = { Text("All (${rawList.size})", fontWeight = FontWeight.Bold) },
                    shape = CircleShape
                )
                FilterChip(
                    selected = filterType == "In",
                    onClick = { filterType = "In" },
                    label = { Text("Checked In", fontWeight = FontWeight.Bold) },
                    shape = CircleShape
                )
                FilterChip(
                    selected = filterType == "Out",
                    onClick = { filterType = "Out" },
                    label = { Text("Checked Out", fontWeight = FontWeight.Bold) },
                    shape = CircleShape
                )
            }

            // List of Chunky Attendee Cards (24dp rounded)
            if (filteredRecords.isEmpty()) {
                Box(
                    modifier = Modifier.fillMaxSize().padding(bottom = 80.dp),
                    contentAlignment = Alignment.Center
                ) {
                    Column(horizontalAlignment = Alignment.CenterHorizontally) {
                        Surface(
                            modifier = Modifier.size(64.dp),
                            shape = CircleShape,
                            color = MaterialTheme.colorScheme.surfaceVariant
                        ) {
                            Box(contentAlignment = Alignment.Center) {
                                Icon(Icons.Default.Groups, contentDescription = null, tint = Color.Gray, modifier = Modifier.size(32.dp))
                            }
                        }
                        Spacer(Modifier.height(10.dp))
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
                    verticalArrangement = Arrangement.spacedBy(10.dp),
                    contentPadding = PaddingValues(bottom = 90.dp)
                ) {
                    items(filteredRecords, key = { it.uid ?: it.studentId ?: it.index.toString() }) { record ->
                        ChunkyAttendeeCard(
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
            title = { Text("Manual Check-In", fontWeight = FontWeight.Black) },
            text = {
                Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                    Text("Manually log an attendee who doesn't have their card.", fontSize = 12.sp, color = Color.Gray)
                    OutlinedTextField(
                        value = name,
                        onValueChange = { name = it },
                        label = { Text("Full Name *") },
                        modifier = Modifier.fillMaxWidth(),
                        shape = RoundedCornerShape(16.dp)
                    )
                    OutlinedTextField(
                        value = studentId,
                        onValueChange = { studentId = it },
                        label = { Text("Student / Employee ID") },
                        modifier = Modifier.fillMaxWidth(),
                        shape = RoundedCornerShape(16.dp)
                    )
                }
            },
            confirmButton = {
                Button(
                    enabled = name.isNotBlank(),
                    shape = CircleShape,
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
fun ChunkyAttendeeCard(record: AttendanceRecord, onDelete: () -> Unit) {
    val isCompleted = !record.timeOut.isNullOrBlank() && record.timeOut != "--"
    val primaryColor = MaterialTheme.colorScheme.primary

    // Generate Initials
    val initials = remember(record.name) {
        val parts = (record.name ?: "A").trim().split(" ")
        if (parts.size >= 2) "${parts[0].firstOrNull() ?: ' '}${parts[1].firstOrNull() ?: ' '}".uppercase()
        else (record.name ?: "A").take(2).uppercase()
    }

    ElevatedCard(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(24.dp),
        colors = CardDefaults.elevatedCardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant),
        elevation = CardDefaults.elevatedCardElevation(defaultElevation = 1.dp)
    ) {
        Row(
            modifier = Modifier.fillMaxWidth().padding(14.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            // Chunky Avatar with Initials
            Surface(
                modifier = Modifier.size(44.dp),
                shape = RoundedCornerShape(16.dp),
                color = if (isCompleted) Color(0xFF8B5CF6).copy(alpha = 0.2f) else primaryColor.copy(alpha = 0.2f)
            ) {
                Box(contentAlignment = Alignment.Center) {
                    Text(
                        text = initials,
                        fontWeight = FontWeight.Black,
                        fontSize = 14.sp,
                        color = if (isCompleted) Color(0xFF8B5CF6) else primaryColor
                    )
                }
            }

            Spacer(Modifier.width(12.dp))

            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = record.name?.ifBlank { "Unregistered Attendee" } ?: "Attendee",
                    fontWeight = FontWeight.Bold,
                    fontSize = 14.sp
                )
                Text(
                    text = "ID: ${record.studentId?.ifBlank { "--" }} • UID: ${record.uid ?: "--"}",
                    fontSize = 11.sp,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                Row(
                    modifier = Modifier.padding(top = 4.dp),
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    Text(
                        text = "In: ${record.timeIn ?: "--"}",
                        fontSize = 11.sp,
                        fontWeight = FontWeight.Bold,
                        color = primaryColor
                    )
                    if (isCompleted) {
                        Text(
                            text = "Out: ${record.timeOut ?: "--"}",
                            fontSize = 11.sp,
                            fontWeight = FontWeight.Bold,
                            color = MaterialTheme.colorScheme.secondary
                        )
                    }
                }
            }

            // Bubbly Status Badge
            Surface(
                shape = CircleShape,
                color = if (isCompleted) Color(0xFFF3E8FF) else Color(0xFFD1FAE5)
            ) {
                Text(
                    text = if (isCompleted) "DONE" else "IN",
                    fontWeight = FontWeight.Black,
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
