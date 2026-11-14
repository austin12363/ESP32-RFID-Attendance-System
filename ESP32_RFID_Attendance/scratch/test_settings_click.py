with open(r'C:\Attendance\test_page_with_tracker.html', 'r', encoding='utf-8') as f:
    html = f.read()

# Add a script at the very end of <body> to trigger openSettingsModal()
trigger_script = """
<script>
window.addEventListener('load', () => {
  window.openSettingsModal();
  var m = document.getElementById('settingsModal').className;
  var d = document.getElementById('notionSettingsDialog').className;
  var res = document.createElement('div');
  res.id = 'settings-test-result';
  res.innerText = 'MODAL:' + m + '|DIALOG:' + d;
  document.body.appendChild(res);
});
</script>
"""

test_html = html.replace('</body>', trigger_script + '</body>', 1)

with open(r'C:\Attendance\test_settings_page.html', 'w', encoding='utf-8') as f:
    f.write(test_html)

print("Saved test_settings_page.html")
