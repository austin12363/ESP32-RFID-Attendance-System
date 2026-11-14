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
import com.slsu.attendance.data.model.CardItem
import com.slsu.attendance.viewmodel.AttendanceViewModel
import com.slsu.attendance.viewmodel.UiState

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun CardsScreen(viewModel: AttendanceViewModel, uiState: UiState) {
    var showRegisterDialog by remember { mutableStateOf(false) }
    var searchQuery by remember { mutableStateOf("") }
    var cardToDelete by remember { mutableStateOf<CardItem?>(null) }

    val filteredCards = remember(uiState.registeredCards, searchQuery) {
        if (searchQuery.isBlank()) uiState.registeredCards
        else uiState.registeredCards.filter {
            (it.name ?: "").contains(searchQuery, ignoreCase = true) ||
            (it.studentId ?: "").contains(searchQuery, ignoreCase = true) ||
            (it.uid ?: "").contains(searchQuery, ignoreCase = true)
        }
    }

    val primaryColor = MaterialTheme.colorScheme.primary

    Scaffold(
        containerColor = MaterialTheme.colorScheme.background,
        floatingActionButton = {
            ExtendedFloatingActionButton(
                onClick = { showRegisterDialog = true },
                icon = { Icon(Icons.Default.Contactless, contentDescription = null) },
                text = { Text("Register Card", fontWeight = FontWeight.Bold) },
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
                        text = "DATABASE REGISTRY",
                        style = MaterialTheme.typography.labelSmall,
                        fontWeight = FontWeight.Black,
                        letterSpacing = 1.2.sp,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    Text(
                        text = "RFID Cards",
                        style = MaterialTheme.typography.headlineMedium,
                        fontWeight = FontWeight.Black
                    )
                }

                IconButton(onClick = { viewModel.loadCards() }) {
                    Icon(Icons.Default.Refresh, contentDescription = "Refresh")
                }
            }

            // Summary Card (28dp rounded)
            ElevatedCard(
                modifier = Modifier.fillMaxWidth(),
                shape = RoundedCornerShape(28.dp),
                colors = CardDefaults.elevatedCardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant),
                elevation = CardDefaults.elevatedCardElevation(defaultElevation = 1.dp)
            ) {
                Row(
                    modifier = Modifier.padding(18.dp),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(14.dp)
                ) {
                    Surface(
                        modifier = Modifier.size(46.dp),
                        shape = RoundedCornerShape(16.dp),
                        color = primaryColor.copy(alpha = 0.16f)
                    ) {
                        Box(contentAlignment = Alignment.Center) {
                            Icon(Icons.Default.CreditCard, contentDescription = null, tint = primaryColor, modifier = Modifier.size(24.dp))
                        }
                    }
                    Column {
                        Text(
                            text = "${uiState.registeredCards.size} Stored Tags",
                            fontSize = 16.sp,
                            fontWeight = FontWeight.Black
                        )
                        Text(
                            text = "Permanent LittleFS storage on ESP32",
                            fontSize = 11.sp,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                }
            }

            // Search Pill (CircleShape)
            OutlinedTextField(
                value = searchQuery,
                onValueChange = { searchQuery = it },
                modifier = Modifier.fillMaxWidth(),
                placeholder = { Text("Search cards by name or student ID...") },
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

            // Cards List
            if (filteredCards.isEmpty()) {
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
                                Icon(Icons.Default.CreditCard, contentDescription = null, tint = Color.Gray, modifier = Modifier.size(32.dp))
                            }
                        }
                        Spacer(Modifier.height(10.dp))
                        Text(
                            text = if (uiState.registeredCards.isEmpty()) "No registered cards found in database.\nClick 'Register Card' to pair your first tag." else "No cards match your search.",
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
                    items(filteredCards, key = { it.uid ?: it.studentId ?: it.index.toString() }) { card ->
                        ChunkyCardItemRow(card = card, onDelete = { cardToDelete = card })
                    }
                }
            }
        }
    }

    // Two-step Registration Dialog
    if (showRegisterDialog) {
        var candidateName by remember { mutableStateOf("") }
        var candidateId by remember { mutableStateOf("") }
        val regState = uiState.registrationState

        AlertDialog(
            onDismissRequest = {
                if (uiState.isRegistering) viewModel.cancelRegistration()
                showRegisterDialog = false
            },
            title = {
                Text(
                    text = if (!uiState.isRegistering) "Register RFID Card" else "Tap Card on Reader",
                    fontWeight = FontWeight.Black
                )
            },
            text = {
                Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                    if (!uiState.isRegistering) {
                        Text(
                            "Enter attendee info, then tap the physical card on the scanner.",
                            fontSize = 12.sp,
                            color = Color.Gray
                        )
                        OutlinedTextField(
                            value = candidateName,
                            onValueChange = { candidateName = it },
                            label = { Text("Full Name *") },
                            placeholder = { Text("e.g. Juan D. Dela Cruz") },
                            modifier = Modifier.fillMaxWidth(),
                            shape = RoundedCornerShape(16.dp)
                        )
                        OutlinedTextField(
                            value = candidateId,
                            onValueChange = { candidateId = it },
                            label = { Text("Student / Employee ID *") },
                            placeholder = { Text("e.g. 2024-00129") },
                            modifier = Modifier.fillMaxWidth(),
                            shape = RoundedCornerShape(16.dp)
                        )
                    } else {
                        // Live Scanning State
                        ElevatedCard(
                            modifier = Modifier.fillMaxWidth(),
                            shape = RoundedCornerShape(24.dp),
                            colors = CardDefaults.elevatedCardColors(
                                containerColor = when (regState?.state) {
                                    "success", "same_card" -> Color(0xFFD1FAE5)
                                    "conflict" -> Color(0xFFFEE2E2)
                                    else -> MaterialTheme.colorScheme.surfaceVariant
                                }
                            )
                        ) {
                            Column(
                                modifier = Modifier.padding(20.dp).fillMaxWidth(),
                                horizontalAlignment = Alignment.CenterHorizontally
                            ) {
                                if (regState?.state == "waiting") {
                                    CircularProgressIndicator(modifier = Modifier.size(36.dp), color = primaryColor)
                                    Spacer(Modifier.height(14.dp))
                                    Text("Waiting for physical card tap...", fontWeight = FontWeight.Black)
                                    Text("Hold RFID tag near the scanner", fontSize = 11.sp, color = Color.Gray)
                                } else if (regState?.state == "success" || regState?.state == "same_card") {
                                    Icon(Icons.Default.CheckCircle, contentDescription = null, tint = Color(0xFF059669), modifier = Modifier.size(44.dp))
                                    Spacer(Modifier.height(8.dp))
                                    Text("Card Registered!", fontWeight = FontWeight.Black, color = Color(0xFF047857))
                                    Text("UID: ${regState.resultUid}", fontSize = 12.sp)
                                    Text("Assigned to: ${regState.candidateName}", fontSize = 12.sp)
                                } else if (regState?.state == "conflict") {
                                    Icon(Icons.Default.Error, contentDescription = null, tint = Color(0xFFDC2626), modifier = Modifier.size(44.dp))
                                    Spacer(Modifier.height(8.dp))
                                    Text("Card Already In Use!", fontWeight = FontWeight.Black, color = Color(0xFF991B1B))
                                    Text("Owner: ${regState.conflictOwner}", fontSize = 12.sp)
                                } else {
                                    Text(regState?.message ?: "Status: ${regState?.state ?: "idle"}")
                                }
                            }
                        }
                    }
                }
            },
            confirmButton = {
                if (!uiState.isRegistering) {
                    Button(
                        enabled = candidateName.isNotBlank() && candidateId.isNotBlank(),
                        shape = CircleShape,
                        onClick = {
                            viewModel.startCardRegistration(candidateName, candidateId)
                        }
                    ) {
                        Text("Arm Scanner")
                    }
                } else if (regState?.state == "success" || regState?.state == "same_card") {
                    Button(
                        shape = CircleShape,
                        onClick = {
                            viewModel.cancelRegistration()
                            showRegisterDialog = false
                        }
                    ) {
                        Text("Done")
                    }
                }
            },
            dismissButton = {
                TextButton(onClick = {
                    if (uiState.isRegistering) viewModel.cancelRegistration()
                    showRegisterDialog = false
                }) {
                    Text(if (uiState.isRegistering && (regState?.state == "success" || regState?.state == "same_card")) "Close" else "Cancel")
                }
            }
        )
    }

    // Delete Card Confirmation Dialog
    cardToDelete?.let { card ->
        AlertDialog(
            onDismissRequest = { cardToDelete = null },
            title = { Text("Delete Card?", fontWeight = FontWeight.Black) },
            text = {
                Text("Are you sure you want to remove card UID ${card.uid} assigned to ${card.name}?")
            },
            confirmButton = {
                Button(
                    shape = CircleShape,
                    colors = ButtonDefaults.buttonColors(containerColor = MaterialTheme.colorScheme.error),
                    onClick = {
                        card.uid?.let { viewModel.deleteCard(it) }
                        cardToDelete = null
                    }
                ) {
                    Text("Delete")
                }
            },
            dismissButton = {
                TextButton(onClick = { cardToDelete = null }) {
                    Text("Cancel")
                }
            }
        )
    }
}

@Composable
fun ChunkyCardItemRow(card: CardItem, onDelete: () -> Unit) {
    val primaryColor = MaterialTheme.colorScheme.primary

    ElevatedCard(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(24.dp),
        colors = CardDefaults.elevatedCardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant),
        elevation = CardDefaults.elevatedCardElevation(defaultElevation = 1.dp)
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(14.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Surface(
                modifier = Modifier.size(44.dp),
                shape = RoundedCornerShape(16.dp),
                color = primaryColor.copy(alpha = 0.16f)
            ) {
                Box(contentAlignment = Alignment.Center) {
                    Icon(
                        Icons.Default.CreditCard,
                        contentDescription = null,
                        tint = primaryColor,
                        modifier = Modifier.size(22.dp)
                    )
                }
            }

            Spacer(Modifier.width(12.dp))

            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = card.name?.ifBlank { "Unassigned" } ?: "Attendee",
                    fontWeight = FontWeight.Bold,
                    fontSize = 14.sp
                )
                Text(
                    text = "ID: ${card.studentId?.ifBlank { "--" }} • UID: ${card.uid ?: "--"}",
                    fontSize = 11.sp,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                if (!card.regDate.isNullOrBlank()) {
                    Text(
                        text = "Registered: ${card.regDate}",
                        fontSize = 10.sp,
                        color = Color.Gray
                    )
                }
            }

            IconButton(onClick = onDelete) {
                Icon(Icons.Default.Close, contentDescription = "Delete", tint = Color.Gray, modifier = Modifier.size(16.dp))
            }
        }
    }
}
