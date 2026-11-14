@echo off
echo ==============================================
echo ESP32 RFID Attendance - Quick Upload Utility
echo ==============================================
echo.
echo IMPORTANT: Please make sure the Serial Monitor in Arduino IDE is CLOSED!
echo.
pause
"C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe" upload -p COM7 --fqbn esp32:esp32:esp32 "C:\Attendance\ESP32_RFID_Attendance"
echo.
if %ERRORLEVEL% EQU 0 (
    echo [SUCCESS] Firmware uploaded successfully!
) else (
    echo [ERROR] Upload failed. Ensure COM7 is plugged in and Serial Monitor is closed.
)
echo.
pause
