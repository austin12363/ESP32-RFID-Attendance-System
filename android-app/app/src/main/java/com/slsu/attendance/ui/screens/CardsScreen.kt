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

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Card Management", fontWeight = FontWeight.Bold) },
                actions = {
                    IconButton(onClick = { viewModel.loadCards() }) {
                        Icon(Icons.Default.Refresh, contentDescription = "Refresh")
                    }
                }
            )
        },
        floatingActionButton = {
            ExtendedFloatingActionButton(
                onClick = { showRegisterDialog = true },
                icon = { Icon(Icons.Default.Contactless, contentDescription = null) },
                text = { Text("Register Card") },
                containerColor = MaterialTheme.colorScheme.primary
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
                    colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.primaryContainer)
                ) {
                    Row(
                        modifier = Modifier.padding(16.dp),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Icon(
                            Icons.Default.CreditCard,
                            contentDescription = null,
                            tint = MaterialTheme.colorScheme.primary,
                            modifier = Modifier.size(36.dp)
                        )
                        Spacer(Modifier.width(14.dp))
                        Column {
                            Text(
                                text = "${uiState.registeredCards.size} Registered Cards",
                                fontSize = 18.sp,
                                fontWeight = FontWeight.Bold,
                                color = MaterialTheme.colorScheme.onPrimaryContainer
                            )
                            Text(
                                text = "Stored permanently in ESP32 Flash Memory",
                                fontSize = 12.sp,
                                color = MaterialTheme.colorScheme.onPrimaryContainer.copy(alpha = 0.8f)
                            )
                        }
                    }
                }
            }

            item {
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
                    shape = RoundedCornerShape(12.dp),
                    singleLine = true
                )
            }

            if (filteredCards.isEmpty()) {
                item {
                    Box(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(vertical = 40.dp),
                        contentAlignment = Alignment.Center
                    ) {
                        Text(
                            text = if (uiState.registeredCards.isEmpty()) "No registered cards found in database.\nClick 'Register Card' to pair your first RFID tag." else "No cards match your search.",
                            color = Color.Gray,
                            fontSize = 14.sp,
                            textAlign = androidx.compose.ui.text.style.TextAlign.Center
                        )
                    }
                }
            } else {
                items(filteredCards, key = { it.uid ?: it.studentId ?: it.index.toString() }) { card ->
                    CardRow(card = card, onDelete = { cardToDelete = card })
                }
            }

            item {
                Spacer(modifier = Modifier.height(70.dp))
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
                Text(if (!uiState.isRegistering) "Register New RFID Card" else "Tap Card on Reader")
            },
            text = {
                Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                    if (!uiState.isRegistering) {
                        Text(
                            "Enter attendee info first, then tap the physical card on the ESP32 RC522 scanner.",
                            fontSize = 13.sp,
                            color = Color.Gray
                        )
                        OutlinedTextField(
                            value = candidateName,
                            onValueChange = { candidateName = it },
                            label = { Text("Full Name *") },
                            placeholder = { Text("e.g. Juan D. Dela Cruz") },
                            modifier = Modifier.fillMaxWidth()
                        )
                        OutlinedTextField(
                            value = candidateId,
                            onValueChange = { candidateId = it },
                            label = { Text("Student / Employee ID *") },
                            placeholder = { Text("e.g. 2024-00129") },
                            modifier = Modifier.fillMaxWidth()
                        )
                    } else {
                        // Live Scanning State
                        Box(
                            modifier = Modifier
                                .fillMaxWidth()
                                .clip(RoundedCornerShape(12.dp))
                                .background(
                                    when (regState?.state) {
                                        "success", "same_card" -> Color(0xFFD1FAE5)
                                        "conflict" -> Color(0xFFFEE2E2)
                                        else -> MaterialTheme.colorScheme.surfaceVariant
                                    }
                                )
                                .padding(16.dp),
                            contentAlignment = Alignment.Center
                        ) {
                            Column(horizontalAlignment = Alignment.CenterHorizontally) {
                                if (regState?.state == "waiting") {
                                    CircularProgressIndicator(modifier = Modifier.size(36.dp))
                                    Spacer(Modifier.height(12.dp))
                                    Text("Waiting for physical card tap...", fontWeight = FontWeight.Bold)
                                    Text("Bring RFID tag close to RC522 module", fontSize = 12.sp, color = Color.Gray)
                                } else if (regState?.state == "success" || regState?.state == "same_card") {
                                    Icon(Icons.Default.CheckCircle, contentDescription = null, tint = Color(0xFF059669), modifier = Modifier.size(40.dp))
                                    Spacer(Modifier.height(8.dp))
                                    Text("Card Registered!", fontWeight = FontWeight.Bold, color = Color(0xFF047857))
                                    Text("UID: ${regState.resultUid}", fontSize = 13.sp)
                                    Text("Assigned to: ${regState.candidateName}", fontSize = 13.sp)
                                } else if (regState?.state == "conflict") {
                                    Icon(Icons.Default.Error, contentDescription = null, tint = Color(0xFFDC2626), modifier = Modifier.size(40.dp))
                                    Spacer(Modifier.height(8.dp))
                                    Text("Card Already In Use!", fontWeight = FontWeight.Bold, color = Color(0xFF991B1B))
                                    Text("Owner: ${regState.conflictOwner} (${regState.conflictId})", fontSize = 12.sp)
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
                        onClick = {
                            viewModel.startCardRegistration(candidateName, candidateId)
                        }
                    ) {
                        Text("Arm Scanner")
                    }
                } else if (regState?.state == "success" || regState?.state == "same_card") {
                    Button(onClick = {
                        viewModel.cancelRegistration()
                        showRegisterDialog = false
                    }) {
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
            title = { Text("Delete Card?") },
            text = {
                Text("Are you sure you want to remove card UID ${card.uid} assigned to ${card.name}?")
            },
            confirmButton = {
                Button(
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
fun CardRow(card: CardItem, onDelete: () -> Unit) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(10.dp),
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface)
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(14.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Box(
                modifier = Modifier
                    .size(40.dp)
                    .clip(RoundedCornerShape(10.dp))
                    .background(MaterialTheme.colorScheme.secondaryContainer),
                contentAlignment = Alignment.Center
            ) {
                Icon(
                    Icons.Default.CreditCard,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.secondary,
                    modifier = Modifier.size(24.dp)
                )
            }

            Spacer(modifier = Modifier.width(14.dp))

            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = card.name?.ifBlank { "Unassigned" } ?: "Attendee",
                    fontWeight = FontWeight.SemiBold,
                    fontSize = 15.sp
                )
                Text(
                    text = "ID: ${card.studentId?.ifBlank { "--" }} • UID: ${card.uid ?: "--"}",
                    fontSize = 12.sp,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                if (!card.regDate.isNullOrBlank()) {
                    Text(
                        text = "Registered: ${card.regDate}",
                        fontSize = 11.sp,
                        color = Color.Gray
                    )
                }
            }

            IconButton(onClick = onDelete) {
                Icon(Icons.Default.DeleteOutline, contentDescription = "Delete", tint = Color.Gray)
            }
        }
    }
}
