with open('ESP32_RFID_Attendance/ESP32_RFID_Attendance.ino', encoding='utf-8', errors='ignore') as f:
    for i, line in enumerate(f):
        if 'Next' in line or 'step1' in line or 'Step 1' in line or 'goToEntranceStep' in line or 'selectEntrance' in line or 'startSession' in line:
            print(f"{i+1}: {line.strip()[:100]}")
