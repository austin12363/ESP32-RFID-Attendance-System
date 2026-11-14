package com.slsu.attendance

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.viewModels
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.currentBackStackEntryAsState
import androidx.navigation.compose.rememberNavController
import com.slsu.attendance.data.network.NetworkClient
import com.slsu.attendance.ui.screens.CardsScreen
import com.slsu.attendance.ui.screens.DashboardScreen
import com.slsu.attendance.ui.screens.RosterScreen
import com.slsu.attendance.ui.screens.SettingsScreen
import com.slsu.attendance.ui.theme.Esp32AttendanceTheme
import com.slsu.attendance.viewmodel.AttendanceViewModel
import kotlinx.coroutines.launch

sealed class Screen(val route: String, val title: String, val icon: ImageVector) {
    object Terminal : Screen("terminal", "Terminal", Icons.Default.Contactless)
    object Roster : Screen("roster", "Roster", Icons.Default.FormatListNumbered)
    object Cards : Screen("cards", "Cards", Icons.Default.CreditCard)
    object Settings : Screen("settings", "Settings", Icons.Default.Tune)
}

class MainActivity : ComponentActivity() {

    private val viewModel: AttendanceViewModel by viewModels()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        // Force Android to route traffic through the ESP32 Wi-Fi hotspot even with no internet
        NetworkClient.bindToWifiHotspot(this)

        setContent {
            Esp32AttendanceTheme {
                val uiState by viewModel.uiState.collectAsState()
                val navController = rememberNavController()
                val snackbarHostState = remember { SnackbarHostState() }
                val scope = rememberCoroutineScope()

                // Display feedback messages
                LaunchedEffect(uiState.infoMessage) {
                    uiState.infoMessage?.let { msg ->
                        scope.launch {
                            snackbarHostState.showSnackbar(msg)
                            viewModel.clearMessage()
                        }
                    }
                }

                val items = listOf(
                    Screen.Terminal,
                    Screen.Roster,
                    Screen.Cards,
                    Screen.Settings
                )

                Scaffold(
                    snackbarHost = { SnackbarHost(snackbarHostState) },
                    bottomBar = {
                        NavigationBar {
                            val navBackStackEntry by navController.currentBackStackEntryAsState()
                            val currentRoute = navBackStackEntry?.destination?.route

                            items.forEach { screen ->
                                NavigationBarItem(
                                    icon = { Icon(screen.icon, contentDescription = screen.title) },
                                    label = { Text(screen.title) },
                                    selected = currentRoute == screen.route,
                                    onClick = {
                                        if (currentRoute != screen.route) {
                                            navController.navigate(screen.route) {
                                                popUpTo(navController.graph.startDestinationId) {
                                                    saveState = true
                                                }
                                                launchSingleTop = true
                                                restoreState = true
                                            }
                                        }
                                    }
                                )
                            }
                        }
                    }
                ) { innerPadding ->
                    NavHost(
                        navController = navController,
                        startDestination = Screen.Terminal.route,
                        modifier = Modifier
                            .fillMaxSize()
                            .padding(innerPadding)
                    ) {
                        composable(Screen.Terminal.route) {
                            DashboardScreen(viewModel = viewModel, uiState = uiState)
                        }
                        composable(Screen.Roster.route) {
                            RosterScreen(viewModel = viewModel, uiState = uiState)
                        }
                        composable(Screen.Cards.route) {
                            CardsScreen(viewModel = viewModel, uiState = uiState)
                        }
                        composable(Screen.Settings.route) {
                            SettingsScreen(viewModel = viewModel, uiState = uiState)
                        }
                    }
                }
            }
        }
    }
}
