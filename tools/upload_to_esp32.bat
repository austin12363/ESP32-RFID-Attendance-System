@echo off
echo ==============================================
echo ESP32 RFID Attendance - Quick Upload Utility
echo ==============================================
echo.
echo IMPORTANT: Please ensure the Serial Monitor in Arduino IDE is CLOSED!
echo.
set /p COM_PORT=Enter your ESP32 COM Port (e.g. COM3, COM7) [Default: COM7]: 
if "%COM_PORT%"=="" set COM_PORT=COM7

echo.
echo Uploading firmware to %COM_PORT%...
if exist "C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe" (
    "C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe" upload -p %COM_PORT% --fqbn esp32:esp32:esp32 "%~dp0..\ESP32_RFID_Attendance"
) else (
    arduino-cli upload -p %COM_PORT% --fqbn esp32:esp32:esp32 "%~dp0..\ESP32_RFID_Attendance"
)

echo.
if %ERRORLEVEL% EQU 0 (
    echo [SUCCESS] Firmware uploaded successfully!
) else (
    echo [ERROR] Upload failed. Ensure %COM_PORT% is connected, drivers are installed, and Serial Monitor is closed.
)
echo.
pause
