with open(r'C:\Attendance\test_page.html', 'r', encoding='utf-8') as f:
    content = f.read()

# Inject error tracker right after <head>
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

content = content.replace('<head>', '<head>' + tracker, 1)

with open(r'C:\Attendance\test_page_with_tracker.html', 'w', encoding='utf-8') as f:
    f.write(content)

print("Saved test_page_with_tracker.html")
