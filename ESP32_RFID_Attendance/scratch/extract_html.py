with open('ESP32_RFID_Attendance/ESP32_RFID_Attendance.ino', encoding='utf-8', errors='ignore') as f:
    text = f.read()

start = text.find('R"rawliteral(') + len('R"rawliteral(')
end = text.find(')rawliteral";', start)
html = text[start:end]

with open('test_page.html', 'w', encoding='utf-8') as f:
    f.write(html)

print("Saved test_page.html, size:", len(html))
