package com.slsu.attendance.ui.screens

import androidx.compose.animation.*
import androidx.compose.animation.core.*
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.scale
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.slsu.attendance.ui.theme.MdExitOrange
import com.slsu.attendance.ui.theme.MdPrimaryLight
import com.slsu.attendance.viewmodel.AttendanceViewModel
import com.slsu.attendance.viewmodel.UiState

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun DashboardScreen(viewModel: AttendanceViewModel, uiState: UiState) {
    var showStartDialog by remember { mutableStateOf(false) }

    val status = uiState.status
    val isSessionActive = status?.sessionActive == true
    val isExitMode = status?.sessionType.equals("Exit", ignoreCase = true)
    val lastScan = status?.lastScan

    Scaffold(
        topBar = {
            TopAppBar(
                title = {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Box(
                            modifier = Modifier
                                .size(10.dp)
                                .clip(CircleShape)
                                .background(if (uiState.isConnected) Color(0xFF10B981) else Color(0xFFEF4444))
                        )
                        Spacer(Modifier.width(8.dp))
                        Column {
                            Text("ESP32 Attendance Terminal", fontWeight = FontWeight.Bold, fontSize = 16.sp)
                            Text(
                                text = if (uiState.isConnected) "192.168.4.1 • Hotspot Connected" else "Searching for ESP32-Attendance...",
                                fontSize = 11.sp,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }
                    }
                },
                actions = {
                    IconButton(onClick = { viewModel.refreshAll() }) {
                        Icon(Icons.Default.Refresh, contentDescription = "Refresh")
                    }
                }
            )
        }
    ) { paddingValues ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(paddingValues)
                .padding(horizontal = 20.dp, vertical = 8.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp),
            horizontalAlignment = Alignment.CenterHorizontally
        ) {

            // 1. Webapp Signature Mode Segmented Control Pill
            Box(
                modifier = Modifier
                    .clip(CircleShape)
                    .background(MaterialTheme.colorScheme.surfaceVariant)
                    .border(1.dp, MaterialTheme.colorScheme.outline.copy(alpha = 0.2f), CircleShape)
                    .padding(4.dp)
            ) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    // TIME IN Button
                    Button(
                        onClick = { if (isExitMode) viewModel.toggleMode() },
                        colors = ButtonDefaults.buttonColors(
                            containerColor = if (!isExitMode) MdPrimaryLight else Color.Transparent,
                            contentColor = if (!isExitMode) Color.White else MaterialTheme.colorScheme.onSurfaceVariant
                        ),
                        shape = CircleShape,
                        elevation = if (!isExitMode) ButtonDefaults.buttonElevation(defaultElevation = 2.dp) else null,
                        contentPadding = PaddingValues(horizontal = 20.dp, vertical = 6.dp)
                    ) {
                        Text("TIME IN", fontWeight = FontWeight.Bold, fontSize = 12.sp)
                    }

                    Spacer(Modifier.width(4.dp))

                    // TIME OUT Button
                    Button(
                        onClick = { if (!isExitMode) viewModel.toggleMode() },
                        colors = ButtonDefaults.buttonColors(
                            containerColor = if (isExitMode) MdExitOrange else Color.Transparent,
                            contentColor = if (isExitMode) Color.White else MaterialTheme.colorScheme.onSurfaceVariant
                        ),
                        shape = CircleShape,
                        elevation = if (isExitMode) ButtonDefaults.buttonElevation(defaultElevation = 2.dp) else null,
                        contentPadding = PaddingValues(horizontal = 20.dp, vertical = 6.dp)
                    ) {
                        Text("TIME OUT", fontWeight = FontWeight.Bold, fontSize = 12.sp)
                    }
                }
            }

            // 2. Hero Tap Sensor Card (With Animated Radar Rings)
            Card(
                modifier = Modifier
                    .fillMaxWidth()
                    .weight(1f),
                shape = RoundedCornerShape(24.dp),
                colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.6f)),
                border = androidx.compose.foundation.BorderStroke(
                    1.dp,
                    if (lastScan?.isDuplicate == true) Color(0xFFF59E0B)
                    else if (lastScan?.uid?.isNotBlank() == true) Color(0xFF10B981)
                    else MaterialTheme.colorScheme.outline.copy(alpha = 0.2f)
                )
            ) {
                Box(
                    modifier = Modifier
                        .fillMaxSize()
                        .padding(20.dp),
                    contentAlignment = Alignment.Center
                ) {
                    // When a card was recently scanned, show the animated scan result
                    val hasRecentScan = !lastScan?.uid.isNullOrBlank()

                    AnimatedContent(
                        targetState = hasRecentScan,
                        label = "ScanStateAnimation"
                    ) { isScanned ->
                        if (!isScanned) {
                            // IDLE RADAR STATE
                            Column(
                                horizontalAlignment = Alignment.CenterHorizontally,
                                verticalArrangement = Arrangement.Center
                            ) {
                                RadarSensorAnimation(isExitMode = isExitMode)

                                Spacer(Modifier.height(18.dp))

                                Surface(
                                    shape = CircleShape,
                                    color = MaterialTheme.colorScheme.surface,
                                    border = androidx.compose.foundation.BorderStroke(1.dp, MaterialTheme.colorScheme.outline.copy(alpha = 0.2f))
                                ) {
                                    Text(
                                        text = if (isExitMode) "EXIT SCAN ACTIVE" else "ENTRANCE SCAN ACTIVE",
                                        fontWeight = FontWeight.ExtraBold,
                                        fontSize = 11.sp,
                                        letterSpacing = 0.8.sp,
                                        color = if (isExitMode) MdExitOrange else MdPrimaryLight,
                                        modifier = Modifier.padding(horizontal = 14.dp, vertical = 4.dp)
                                    )
                                }

                                Spacer(Modifier.height(10.dp))

                                Text(
                                    text = "Ready for RFID Card",
                                    fontSize = 20.sp,
                                    fontWeight = FontWeight.Bold,
                                    textAlign = TextAlign.Center
                                )

                                Text(
                                    text = "Hold card near the ESP32 terminal",
                                    fontSize = 13.sp,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                                    modifier = Modifier.padding(top = 4.dp)
                                )
                            }
                        } else {
                            // SCANNED RESULT POP-IN
                            Column(
                                horizontalAlignment = Alignment.CenterHorizontally,
                                verticalArrangement = Arrangement.Center,
                                modifier = Modifier.fillMaxWidth()
                            ) {
                                val isDup = lastScan?.isDuplicate == true

                                Surface(
                                    shape = CircleShape,
                                    color = if (isDup) Color(0xFFFEF3C7) else Color(0xFFD1FAE5)
                                ) {
                                    Text(
                                        text = if (isDup) "⚠️ ALREADY RECORDED" else "✓ RECORDED: ${lastScan?.type ?: "ATTENDANCE"}",
                                        fontWeight = FontWeight.Black,
                                        fontSize = 12.sp,
                                        color = if (isDup) Color(0xFFB45309) else Color(0xFF047857),
                                        modifier = Modifier.padding(horizontal = 16.dp, vertical = 6.dp)
                                    )
                                }

                                Spacer(Modifier.height(14.dp))

                                Text(
                                    text = lastScan?.name?.ifBlank { "Unregistered Card" } ?: "Scan Event",
                                    fontSize = 26.sp,
                                    fontWeight = FontWeight.ExtraBold,
                                    textAlign = TextAlign.Center,
                                    lineHeight = 30.sp
                                )

                                Spacer(Modifier.height(6.dp))

                                Text(
                                    text = "UID: ${lastScan?.uid ?: "--"}",
                                    fontSize = 13.sp,
                                    fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant
                                )

                                Text(
                                    text = "${lastScan?.time ?: "--"} • ${lastScan?.message ?: ""}",
                                    fontSize = 12.sp,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                                    modifier = Modifier.padding(top = 4.dp)
                                )
                            }
                        }
                    }
                }
            }

            // 3. Clean 3-Pill Metrics Row
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                MetricPill(
                    label = "Attendees",
                    value = "${status?.attendanceCount ?: uiState.attendanceList.size}",
                    color = MdPrimaryLight,
                    modifier = Modifier.weight(1f)
                )
                MetricPill(
                    label = "Registered",
                    value = "${status?.registeredCount ?: 0}",
                    color = Color(0xFF10B981),
                    modifier = Modifier.weight(1f)
                )
                MetricPill(
                    label = "Session",
                    value = if (isSessionActive) (status?.sessionTitle ?: "Active") else "Idle",
                    color = if (isSessionActive) Color(0xFFF59E0B) else Color.Gray,
                    modifier = Modifier.weight(1f)
                )
            }

            // 4. Session Action Buttons (Start / Stop)
            if (!isSessionActive) {
                Button(
                    onClick = { showStartDialog = true },
                    modifier = Modifier.fillMaxWidth().height(48.dp),
                    shape = RoundedCornerShape(14.dp),
                    colors = ButtonDefaults.buttonColors(containerColor = MdPrimaryLight)
                ) {
                    Icon(Icons.Default.PlayArrow, contentDescription = null)
                    Spacer(Modifier.width(8.dp))
                    Text("Start Attendance Session", fontWeight = FontWeight.Bold, fontSize = 14.sp)
                }
            } else {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(10.dp)
                ) {
                    OutlinedButton(
                        onClick = { viewModel.saveSessionToHistory() },
                        modifier = Modifier.weight(1f).height(46.dp),
                        shape = RoundedCornerShape(12.dp)
                    ) {
                        Icon(Icons.Default.Save, contentDescription = null, Modifier.size(16.dp))
                        Spacer(Modifier.width(6.dp))
                        Text("Save History", fontSize = 12.sp)
                    }

                    Button(
                        onClick = { viewModel.stopSession() },
                        colors = ButtonDefaults.buttonColors(containerColor = MaterialTheme.colorScheme.error),
                        modifier = Modifier.weight(1f).height(46.dp),
                        shape = RoundedCornerShape(12.dp)
                    ) {
                        Icon(Icons.Default.Stop, contentDescription = null, Modifier.size(16.dp))
                        Spacer(Modifier.width(6.dp))
                        Text("End Session", fontSize = 12.sp)
                    }
                }
            }
        }
    }

    if (showStartDialog) {
        var title by remember { mutableStateOf("") }
        var type by remember { mutableStateOf("Entrance") }

        AlertDialog(
            onDismissRequest = { showStartDialog = false },
            title = { Text("Start Session", fontWeight = FontWeight.Bold) },
            text = {
                Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                    OutlinedTextField(
                        value = title,
                        onValueChange = { title = it },
                        label = { Text("Session Name / Subject") },
                        placeholder = { Text("e.g. CS101 Finals") },
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
                Button(onClick = {
                    viewModel.startSession(title, type, clearPrevious = true)
                    showStartDialog = false
                }) {
                    Text("Start")
                }
            },
            dismissButton = {
                TextButton(onClick = { showStartDialog = false }) {
                    Text("Cancel")
                }
            }
        )
    }
}

@Composable
fun RadarSensorAnimation(isExitMode: Boolean) {
    val infiniteTransition = rememberInfiniteTransition(label = "RadarTransition")
    val scale by infiniteTransition.animateFloat(
        initialValue = 0.85f,
        targetValue = 1.25f,
        animationSpec = infiniteRepeatable(
            animation = tween(2000, easing = LinearEasing),
            repeatMode = RepeatMode.Restart
        ),
        label = "RadarScale"
    )

    Box(
        modifier = Modifier.size(110.dp),
        contentAlignment = Alignment.Center
    ) {
        // Outer pulsing ring
        Box(
            modifier = Modifier
                .size(110.dp)
                .scale(scale)
                .clip(CircleShape)
                .background(
                    if (isExitMode) MdExitOrange.copy(alpha = 0.12f)
                    else MdPrimaryLight.copy(alpha = 0.12f)
                )
        )

        // Center floating sensor circle
        Surface(
            modifier = Modifier.size(68.dp),
            shape = CircleShape,
            color = if (isExitMode) MdExitOrange.copy(alpha = 0.18f) else MdPrimaryLight.copy(alpha = 0.18f),
            border = androidx.compose.foundation.BorderStroke(
                1.5.dp,
                if (isExitMode) MdExitOrange.copy(alpha = 0.4f) else MdPrimaryLight.copy(alpha = 0.4f)
            )
        ) {
            Box(contentAlignment = Alignment.Center) {
                Icon(
                    Icons.Default.Contactless,
                    contentDescription = null,
                    tint = if (isExitMode) MdExitOrange else MdPrimaryLight,
                    modifier = Modifier.size(34.dp)
                )
            }
        }
    }
}

@Composable
fun MetricPill(label: String, value: String, color: Color, modifier: Modifier = Modifier) {
    Surface(
        modifier = modifier,
        shape = RoundedCornerShape(16.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.8f),
        border = androidx.compose.foundation.BorderStroke(1.dp, MaterialTheme.colorScheme.outline.copy(alpha = 0.12f))
    ) {
        Column(
            modifier = Modifier.padding(vertical = 10.dp, horizontal = 8.dp),
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            Text(
                text = label.uppercase(),
                fontSize = 10.sp,
                fontWeight = FontWeight.Bold,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            Spacer(Modifier.height(2.dp))
            Text(
                text = value,
                fontSize = 16.sp,
                fontWeight = FontWeight.ExtraBold,
                color = color,
                maxLines = 1
            )
        }
    }
}
