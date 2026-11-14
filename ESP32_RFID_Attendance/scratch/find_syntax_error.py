with open(r'C:\Attendance\test_page_with_tracker.html', 'r', encoding='utf-8') as f:
    lines = f.readlines()

for i in range(2560, min(2600, len(lines))):
    print(f"{i+1}: {repr(lines[i])}")
