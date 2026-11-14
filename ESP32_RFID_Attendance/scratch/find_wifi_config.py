with open('ESP32_RFID_Attendance/ESP32_RFID_Attendance.ino', encoding='utf-8', errors='ignore') as f:
    for i, line in enumerate(f):
        if any(k in line for k in ['AP_PASS', 'AP_SSID', 'showLcdWifiGuide', 'show_lcd_wifi']):
            print(f"{i+1}: {line.strip()[:100]}")
