with open('test_page.html', encoding='utf-8') as f:
    content = f.read()

s_idx = content.find('<script>')
e_idx = content.find('</script>')
js_code = content[s_idx+8:e_idx]

with open('extracted_script.js', 'w', encoding='utf-8') as f:
    f.write(js_code)

print("Saved extracted_script.js, line count:", len(js_code.splitlines()))
