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

    val primaryColor = MaterialTheme.colorScheme.primary
    val secondaryColor = MaterialTheme.colorScheme.secondary

    Scaffold(
        containerColor = MaterialTheme.colorScheme.background
    ) { paddingValues ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(paddingValues)
                .padding(horizontal = 20.dp, vertical = 12.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {

            // 1. Material You Greeting & Status Pills (Matching Reference Image 2)
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Column {
                        Text(
                            text = "ESP32 TERMINAL",
                            style = MaterialTheme.typography.labelSmall,
                            fontWeight = FontWeight.Black,
                            letterSpacing = 1.2.sp,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                        Text(
                            text = "Attendance Hub",
                            style = MaterialTheme.typography.headlineMedium,
                            fontWeight = FontWeight.Black,
                            color = MaterialTheme.colorScheme.onSurface
                        )
                    }

                    // Online Status Capsule
                    Surface(
                        shape = CircleShape,
                        color = if (uiState.isConnected) Color(0xFF10B981).copy(alpha = 0.18f) else Color(0xFFEF4444).copy(alpha = 0.18f),
                        border = androidx.compose.foundation.BorderStroke(
                            1.dp,
                            if (uiState.isConnected) Color(0xFF10B981).copy(alpha = 0.4f) else Color(0xFFEF4444).copy(alpha = 0.4f)
                        )
                    ) {
                        Row(
                            modifier = Modifier.padding(horizontal = 12.dp, vertical = 6.dp),
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(6.dp)
                        ) {
                            Box(
                                modifier = Modifier
                                    .size(7.dp)
                                    .clip(CircleShape)
                                    .background(if (uiState.isConnected) Color(0xFF10B981) else Color(0xFFEF4444))
                            )
                            Text(
                                text = if (uiState.isConnected) "Ready" else "Offline",
                                fontSize = 11.sp,
                                fontWeight = FontWeight.Black,
                                color = if (uiState.isConnected) Color(0xFF10B981) else Color(0xFFEF4444)
                            )
                        }
                    }
                }

                // Weather-Style Status Pills Row (Image 2)
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(6.dp)
                ) {
                    StatusPillTag(
                        text = "${status?.attendanceCount ?: uiState.attendanceList.size} Present",
                        containerColor = primaryColor.copy(alpha = 0.15f),
                        contentColor = primaryColor
                    )
                    StatusPillTag(
                        text = "${status?.registeredCount ?: 0} Cards",
                        containerColor = Color(0xFF10B981).copy(alpha = 0.15f),
                        contentColor = Color(0xFF10B981)
                    )
                    StatusPillTag(
                        text = if (isSessionActive) (status?.sessionTitle ?: "Active") else "Idle",
                        containerColor = Color(0xFFF59E0B).copy(alpha = 0.15f),
                        contentColor = Color(0xFFF59E0B)
                    )
                }
            }

            // 2. Chunky Segmented Capsule Switcher (TIME IN / TIME OUT)
            Surface(
                modifier = Modifier.fillMaxWidth(),
                shape = CircleShape,
                color = MaterialTheme.colorScheme.surfaceVariant,
                border = androidx.compose.foundation.BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant)
            ) {
                Row(
                    modifier = Modifier.padding(4.dp),
                    horizontalArrangement = Arrangement.spacedBy(4.dp)
                ) {
                    Button(
                        onClick = { if (isExitMode) viewModel.toggleMode() },
                        modifier = Modifier.weight(1f),
                        colors = ButtonDefaults.buttonColors(
                            containerColor = if (!isExitMode) primaryColor else Color.Transparent,
                            contentColor = if (!isExitMode) Color.White else MaterialTheme.colorScheme.onSurfaceVariant
                        ),
                        shape = CircleShape,
                        elevation = if (!isExitMode) ButtonDefaults.buttonElevation(defaultElevation = 2.dp) else null,
                        contentPadding = PaddingValues(vertical = 10.dp)
                    ) {
                        Icon(Icons.Default.Login, contentDescription = null, Modifier.size(16.dp))
                        Spacer(Modifier.width(6.dp))
                        Text("TIME IN", fontWeight = FontWeight.Black, fontSize = 12.sp)
                    }

                    Button(
                        onClick = { if (!isExitMode) viewModel.toggleMode() },
                        modifier = Modifier.weight(1f),
                        colors = ButtonDefaults.buttonColors(
                            containerColor = if (isExitMode) secondaryColor else Color.Transparent,
                            contentColor = if (isExitMode) Color.White else MaterialTheme.colorScheme.onSurfaceVariant
                        ),
                        shape = CircleShape,
                        elevation = if (isExitMode) ButtonDefaults.buttonElevation(defaultElevation = 2.dp) else null,
                        contentPadding = PaddingValues(vertical = 10.dp)
                    ) {
                        Icon(Icons.Default.Logout, contentDescription = null, Modifier.size(16.dp))
                        Spacer(Modifier.width(6.dp))
                        Text("TIME OUT", fontWeight = FontWeight.Black, fontSize = 12.sp)
                    }
                }
            }

            // 3. Hero Tap Sensor Card (Chunky 32dp Rounded Corners)
            ElevatedCard(
                modifier = Modifier
                    .fillMaxWidth()
                    .weight(1f),
                shape = RoundedCornerShape(32.dp),
                colors = CardDefaults.elevatedCardColors(
                    containerColor = MaterialTheme.colorScheme.surfaceVariant
                ),
                elevation = CardDefaults.elevatedCardElevation(defaultElevation = 3.dp)
            ) {
                Box(
                    modifier = Modifier
                        .fillMaxSize()
                        .padding(24.dp),
                    contentAlignment = Alignment.Center
                ) {
                    val hasRecentScan = !lastScan?.uid.isNullOrBlank()

                    AnimatedContent(
                        targetState = hasRecentScan,
                        label = "SensorAnimation"
                    ) { isScanned ->
                        if (!isScanned) {
                            Column(
                                horizontalAlignment = Alignment.CenterHorizontally,
                                verticalArrangement = Arrangement.Center
                            ) {
                                MaterialYouRadarAnimation(isExitMode = isExitMode)

                                Spacer(Modifier.height(18.dp))

                                Surface(
                                    shape = CircleShape,
                                    color = if (isExitMode) secondaryColor.copy(alpha = 0.16f) else primaryColor.copy(alpha = 0.16f),
                                    border = androidx.compose.foundation.BorderStroke(
                                        1.dp,
                                        if (isExitMode) secondaryColor.copy(alpha = 0.35f) else primaryColor.copy(alpha = 0.35f)
                                    )
                                ) {
                                    Text(
                                        text = if (isExitMode) "EXIT SCAN ACTIVE" else "ENTRANCE SCAN ACTIVE",
                                        fontWeight = FontWeight.Black,
                                        fontSize = 11.sp,
                                        letterSpacing = 1.sp,
                                        color = if (isExitMode) secondaryColor else primaryColor,
                                        modifier = Modifier.padding(horizontal = 14.dp, vertical = 5.dp)
                                    )
                                }

                                Spacer(Modifier.height(8.dp))

                                Text(
                                    text = "Tap RFID Card",
                                    style = MaterialTheme.typography.headlineSmall,
                                    fontWeight = FontWeight.Black,
                                    textAlign = TextAlign.Center
                                )

                                Text(
                                    text = "Hold card near the terminal",
                                    style = MaterialTheme.typography.bodyMedium,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant
                                )
                            }
                        } else {
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
                                        text = if (isDup) "⚠️ ALREADY LOGGED" else "✓ RECORDED: ${lastScan?.type ?: "ATTENDANCE"}",
                                        fontWeight = FontWeight.Black,
                                        fontSize = 12.sp,
                                        color = if (isDup) Color(0xFFB45309) else Color(0xFF047857),
                                        modifier = Modifier.padding(horizontal = 16.dp, vertical = 6.dp)
                                    )
                                }

                                Spacer(Modifier.height(14.dp))

                                Text(
                                    text = lastScan?.name?.ifBlank { "Unregistered Card" } ?: "Scan Event",
                                    style = MaterialTheme.typography.headlineMedium,
                                    fontWeight = FontWeight.Black,
                                    textAlign = TextAlign.Center
                                )

                                Spacer(Modifier.height(6.dp))

                                Text(
                                    text = "ID: ${lastScan?.uid ?: "--"}",
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

            // 4. Chunky Active Session Card (24dp rounded)
            Surface(
                modifier = Modifier.fillMaxWidth(),
                shape = RoundedCornerShape(24.dp),
                color = MaterialTheme.colorScheme.surfaceVariant,
                border = androidx.compose.foundation.BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant)
            ) {
                Row(
                    modifier = Modifier.padding(14.dp),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(12.dp)
                    ) {
                        Surface(
                            modifier = Modifier.size(42.dp),
                            shape = RoundedCornerShape(14.dp),
                            color = primaryColor.copy(alpha = 0.16f)
                        ) {
                            Box(contentAlignment = Alignment.Center) {
                                Icon(Icons.Default.Event, contentDescription = null, tint = primaryColor, modifier = Modifier.size(22.dp))
                            }
                        }
                        Column {
                            Text(
                                text = if (isSessionActive) (status?.sessionTitle ?: "Active Session") else "No Session Active",
                                fontWeight = FontWeight.Bold,
                                fontSize = 14.sp
                            )
                            Text(
                                text = "ESP32 Time: ${status?.currentTime ?: "--"}",
                                fontSize = 11.sp,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }
                    }

                    if (!isSessionActive) {
                        Button(
                            onClick = { showStartDialog = true },
                            shape = CircleShape,
                            colors = ButtonDefaults.buttonColors(containerColor = primaryColor),
                            contentPadding = PaddingValues(horizontal = 16.dp, vertical = 8.dp)
                        ) {
                            Text("Start", fontWeight = FontWeight.Bold, fontSize = 12.sp)
                        }
                    } else {
                        Button(
                            onClick = { viewModel.stopSession() },
                            shape = CircleShape,
                            colors = ButtonDefaults.buttonColors(containerColor = MaterialTheme.colorScheme.error),
                            contentPadding = PaddingValues(horizontal = 16.dp, vertical = 8.dp)
                        ) {
                            Text("Stop", fontWeight = FontWeight.Bold, fontSize = 12.sp)
                        }
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
            title = { Text("Start Session", fontWeight = FontWeight.Black) },
            text = {
                Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                    OutlinedTextField(
                        value = title,
                        onValueChange = { title = it },
                        label = { Text("Session Name / Subject") },
                        placeholder = { Text("e.g. CS101 Finals") },
                        modifier = Modifier.fillMaxWidth(),
                        shape = RoundedCornerShape(16.dp)
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
fun StatusPillTag(text: String, containerColor: Color, contentColor: Color) {
    Surface(
        shape = CircleShape,
        color = containerColor
    ) {
        Text(
            text = text,
            fontSize = 11.sp,
            fontWeight = FontWeight.Black,
            color = contentColor,
            modifier = Modifier.padding(horizontal = 12.dp, vertical = 4.dp)
        )
    }
}

@Composable
fun MaterialYouRadarAnimation(isExitMode: Boolean) {
    val infiniteTransition = rememberInfiniteTransition(label = "RadarTransition")
    val scale by infiniteTransition.animateFloat(
        initialValue = 0.85f,
        targetValue = 1.35f,
        animationSpec = infiniteRepeatable(
            animation = tween(2200, easing = FastOutSlowInEasing),
            repeatMode = RepeatMode.Restart
        ),
        label = "RadarScale"
    )

    val color = if (isExitMode) MaterialTheme.colorScheme.secondary else MaterialTheme.colorScheme.primary

    Box(
        modifier = Modifier.size(120.dp),
        contentAlignment = Alignment.Center
    ) {
        // Outer pulsing ring
        Box(
            modifier = Modifier
                .size(120.dp)
                .scale(scale)
                .clip(CircleShape)
                .background(color.copy(alpha = 0.12f))
        )

        // Center floating rounded sensor icon (28dp shape)
        Surface(
            modifier = Modifier.size(76.dp),
            shape = RoundedCornerShape(26.dp),
            color = color.copy(alpha = 0.22f),
            border = androidx.compose.foundation.BorderStroke(2.dp, color.copy(alpha = 0.45f))
        ) {
            Box(contentAlignment = Alignment.Center) {
                Icon(
                    Icons.Default.Contactless,
                    contentDescription = null,
                    tint = color,
                    modifier = Modifier.size(38.dp)
                )
            }
        }
    }
}
