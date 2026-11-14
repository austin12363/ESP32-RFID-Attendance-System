@echo off
title Push to GitHub - ESP32 Attendance System
echo ================================================================
echo Pushing Android App & Workflows to GitHub...
echo Repo: https://github.com/austin12363/ESP32-RFID-Attendance-System
echo ================================================================
echo.

set "PATH=%LOCALAPPDATA%\Programs\MinGit\cmd;%PATH%"

git push -u origin main --force

echo.
echo ================================================================
echo If the push completed successfully, open:
echo https://github.com/austin12363/ESP32-RFID-Attendance-System/actions
echo to monitor your APK build!
echo ================================================================
pause
