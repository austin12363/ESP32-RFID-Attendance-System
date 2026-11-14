with open(r'C:\Attendance\test_page_with_tracker.html', 'r', encoding='utf-8') as f:
    html = f.read()

# Add a script at the very end of <body> to trigger goToEntranceStep(2)
trigger_script = """
<script>
window.addEventListener('load', () => {
  document.getElementById('sessionTitleInput').value = 'Test Class';
  window.goToEntranceStep(2);
  var s1 = document.getElementById('entranceStep1').className;
  var s2 = document.getElementById('entranceStep2').className;
  var preview = document.getElementById('step2TitlePreview').innerText;
  var res = document.createElement('div');
  res.id = 'next-test-result';
  res.innerText = 'STEP1:' + s1 + '|STEP2:' + s2 + '|PREVIEW:' + preview;
  document.body.appendChild(res);
});
</script>
"""

test_html = html.replace('</body>', trigger_script + '</body>', 1)

with open(r'C:\Attendance\test_click_page.html', 'w', encoding='utf-8') as f:
    f.write(test_html)

print("Saved test_click_page.html")
