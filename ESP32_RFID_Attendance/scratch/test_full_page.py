with open('ESP32_RFID_Attendance/ESP32_RFID_Attendance.ino', encoding='utf-8', errors='ignore') as f:
    text = f.read()

start = text.find('R"rawliteral(') + len('R"rawliteral(')
end = text.find(')rawliteral";', start)
html = text[start:end]

tracker = """
<script>
window.addEventListener('error', function(e) {
  var d = document.createElement('div');
  d.id = 'captured-js-error';
  d.style.color = 'red';
  d.innerText = 'JS_ERROR: ' + e.message + ' at ' + e.filename + ':' + e.lineno + ':' + e.colno;
  document.body.appendChild(d);
});
</script>
"""

html_with_tracker = html.replace('<head>', '<head>' + tracker, 1)

with open(r'C:\Attendance\test_page_with_tracker.html', 'w', encoding='utf-8') as f:
    f.write(html_with_tracker)

print("Updated C:\\Attendance\\test_page_with_tracker.html, size:", len(html_with_tracker))
