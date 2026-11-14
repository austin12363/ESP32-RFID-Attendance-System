package com.slsu.attendance.ui.screens

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
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
import com.slsu.attendance.data.network.NetworkClient
import com.slsu.attendance.viewmodel.AttendanceViewModel
import com.slsu.attendance.viewmodel.UiState

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SettingsScreen(viewModel: AttendanceViewModel, uiState: UiState) {
    var hostInput by remember { mutableStateOf(NetworkClient.currentBaseUrl) }
    var lcdLine1 by remember { mutableStateOf("") }
    var lcdLine2 by remember { mutableStateOf("") }
    var showRebootConfirm by remember { mutableStateOf(false) }

    val status = uiState.status

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Terminal Hardware & Settings", fontWeight = FontWeight.Bold) }
            )
        }
    ) { paddingValues ->
        LazyColumn(
            modifier = Modifier
                .fillMaxSize()
                .padding(paddingValues)
                .padding(horizontal = 16.dp),
            verticalArrangement = Arrangement.spacedBy(14.dp)
        ) {
            // Connection & Host Setting
            item {
                Card(
                    modifier = Modifier.fillMaxWidth().padding(top = 4.dp),
                    shape = RoundedCornerShape(14.dp),
                    colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant)
                ) {
                    Column(modifier = Modifier.padding(16.dp)) {
                        Text("ESP32 Connection Endpoint", fontWeight = FontWeight.Bold, fontSize = 16.sp)
                        Text("Default ESP32 Access Point IP is 192.168.4.1", fontSize = 12.sp, color = Color.Gray)
                        Spacer(Modifier.height(8.dp))

                        Row(verticalAlignment = Alignment.CenterVertically) {
                            OutlinedTextField(
                                value = hostInput,
                                onValueChange = { hostInput = it },
                                label = { Text("Base URL") },
                                modifier = Modifier.weight(1f),
                                singleLine = true
                            )
                            Spacer(Modifier.width(8.dp))
                            Button(onClick = { viewModel.updateHost(hostInput) }) {
                                Text("Save")
                            }
                        }
                    }
                }
            }

            // Time Synchronization
            item {
                Card(
                    modifier = Modifier.fillMaxWidth(),
                    shape = RoundedCornerShape(14.dp),
                    colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant)
                ) {
                    Column(modifier = Modifier.padding(16.dp)) {
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.SpaceBetween,
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Column {
                                Text("System Clock Synchronization", fontWeight = FontWeight.Bold, fontSize = 16.sp)
                                Text(
                                    "ESP32 Time: ${status?.currentTime ?: "Unsynchronized"}",
                                    fontSize = 13.sp,
                                    color = if (status?.timeSet == true) Color(0xFF059669) else Color(0xFFDC2626)
                                )
                            }
                            Button(onClick = { viewModel.syncPhoneTime() }) {
                                Icon(Icons.Default.AccessTime, contentDescription = null, modifier = Modifier.size(18.dp))
                                Spacer(Modifier.width(4.dp))
                                Text("Sync Now")
                            }
                        }
                    }
                }
            }

            // LCD Display Controls
            item {
                Card(
                    modifier = Modifier.fillMaxWidth(),
                    shape = RoundedCornerShape(14.dp),
                    colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant)
                ) {
                    Column(modifier = Modifier.padding(16.dp)) {
                        Text("16x2 I2C LCD Custom Message", fontWeight = FontWeight.Bold, fontSize = 16.sp)
                        Text("Send a 2-line announcement directly to the terminal screen.", fontSize = 12.sp, color = Color.Gray)
                        Spacer(Modifier.height(8.dp))

                        OutlinedTextField(
                            value = lcdLine1,
                            onValueChange = { if (it.length <= 16) lcdLine1 = it },
                            label = { Text("Line 1 (Max 16 chars)") },
                            modifier = Modifier.fillMaxWidth(),
                            singleLine = true
                        )
                        Spacer(Modifier.height(6.dp))
                        OutlinedTextField(
                            value = lcdLine2,
                            onValueChange = { if (it.length <= 16) lcdLine2 = it },
                            label = { Text("Line 2 (Max 16 chars)") },
                            modifier = Modifier.fillMaxWidth(),
                            singleLine = true
                        )
                        Spacer(Modifier.height(10.dp))

                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.spacedBy(8.dp)
                        ) {
                            Button(
                                enabled = lcdLine1.isNotBlank() || lcdLine2.isNotBlank(),
                                onClick = {
                                    viewModel.setLcdMessage(lcdLine1, lcdLine2)
                                    lcdLine1 = ""
                                    lcdLine2 = ""
                                },
                                modifier = Modifier.weight(1f)
                            ) {
                                Text("Send to LCD")
                            }

                            OutlinedButton(
                                onClick = { viewModel.toggleLcdSleep(true) },
                                modifier = Modifier.weight(1f)
                            ) {
                                Text("LCD Sleep")
                            }
                            OutlinedButton(
                                onClick = { viewModel.toggleLcdSleep(false) },
                                modifier = Modifier.weight(1f)
                            ) {
                                Text("LCD Wake")
                            }
                        }
                    }
                }
            }

            // Status LED Diagnostics
            item {
                Card(
                    modifier = Modifier.fillMaxWidth(),
                    shape = RoundedCornerShape(14.dp),
                    colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant)
                ) {
                    Column(modifier = Modifier.padding(16.dp)) {
                        Text("RGB Status LED Diagnostics", fontWeight = FontWeight.Bold, fontSize = 16.sp)
                        Text("Test physical LED visual indicators on the terminal.", fontSize = 12.sp, color = Color.Gray)
                        Spacer(Modifier.height(10.dp))

                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.spacedBy(6.dp)
                        ) {
                            Button(
                                onClick = { viewModel.testRgb("green") },
                                colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF10B981)),
                                modifier = Modifier.weight(1f),
                                contentPadding = PaddingValues(horizontal = 4.dp, vertical = 6.dp)
                            ) {
                                Text("Green", color = Color.White, fontSize = 12.sp)
                            }

                            Button(
                                onClick = { viewModel.testRgb("yellow") },
                                colors = ButtonDefaults.buttonColors(containerColor = Color(0xFFF59E0B)),
                                modifier = Modifier.weight(1f),
                                contentPadding = PaddingValues(horizontal = 4.dp, vertical = 6.dp)
                            ) {
                                Text("Yellow", color = Color.White, fontSize = 12.sp)
                            }

                            Button(
                                onClick = { viewModel.testRgb("red") },
                                colors = ButtonDefaults.buttonColors(containerColor = Color(0xFFEF4444)),
                                modifier = Modifier.weight(1f),
                                contentPadding = PaddingValues(horizontal = 4.dp, vertical = 6.dp)
                            ) {
                                Text("Red", color = Color.White, fontSize = 12.sp)
                            }

                            Button(
                                onClick = { viewModel.testRgb("white") },
                                colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF6B7280)),
                                modifier = Modifier.weight(1f),
                                contentPadding = PaddingValues(horizontal = 4.dp, vertical = 6.dp)
                            ) {
                                Text("White", color = Color.White, fontSize = 12.sp)
                            }
                        }
                    }
                }
            }

            // System Health & Power
            item {
                Card(
                    modifier = Modifier.fillMaxWidth(),
                    shape = RoundedCornerShape(14.dp),
                    colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant)
                ) {
                    Column(modifier = Modifier.padding(16.dp)) {
                        Text("Microcontroller Diagnostics", fontWeight = FontWeight.Bold, fontSize = 16.sp)
                        Spacer(Modifier.height(6.dp))

                        Text("Free RAM: ${((status?.freeHeap ?: 0) / 1024)} KB / ${((status?.totalHeap ?: 0) / 1024)} KB", fontSize = 13.sp)
                        Text("LittleFS Storage: ${((status?.fsUsed ?: 0) / 1024)} KB used of ${((status?.fsTotal ?: 0) / 1024)} KB", fontSize = 13.sp)
                        Text("Uptime: ${(status?.uptimeSec ?: 0) / 60} minutes", fontSize = 13.sp)

                        Spacer(Modifier.height(14.dp))

                        Button(
                            onClick = { showRebootConfirm = true },
                            colors = ButtonDefaults.buttonColors(containerColor = MaterialTheme.colorScheme.error),
                            modifier = Modifier.fillMaxWidth()
                        ) {
                            Icon(Icons.Default.RestartAlt, contentDescription = null)
                            Spacer(Modifier.width(6.dp))
                            Text("Reboot ESP32 Terminal")
                        }
                    }
                }
            }

            item {
                Spacer(modifier = Modifier.height(70.dp))
            }
        }
    }

    if (showRebootConfirm) {
        AlertDialog(
            onDismissRequest = { showRebootConfirm = false },
            title = { Text("Reboot ESP32?") },
            text = { Text("The microcontroller will restart cleanly. Wi-Fi will temporarily reconnect in 5-10 seconds.") },
            confirmButton = {
                Button(
                    colors = ButtonDefaults.buttonColors(containerColor = MaterialTheme.colorScheme.error),
                    onClick = {
                        viewModel.rebootDevice()
                        showRebootConfirm = false
                    }
                ) {
                    Text("Reboot")
                }
            },
            dismissButton = {
                TextButton(onClick = { showRebootConfirm = false }) {
                    Text("Cancel")
                }
            }
        )
    }
}
