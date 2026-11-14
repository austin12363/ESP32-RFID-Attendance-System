
// Web Audio Context setup
let audioCtx = null;
window.getAudioContext = function() {
  if (!audioCtx) {
    try {
      audioCtx = new (window.AudioContext || window.webkitAudioContext)();
    } catch(e) {}
  }
  if (audioCtx && audioCtx.state === 'suspended') {
    audioCtx.resume().catch(() => {});
  }
  return audioCtx;
};

['click', 'touchstart', 'keydown'].forEach(evt => {
  document.addEventListener(evt, () => window.getAudioContext(), { once: true, passive: true });
});

window.playAudioTone = function(type) {
  try {
    const ctx = window.getAudioContext();
    if (!ctx) return;
    const osc = ctx.createOscillator();
    const gain = ctx.createGain();
    osc.connect(gain);
    gain.connect(ctx.destination);
    const t = ctx.currentTime;
    if (type === 'success') {
      osc.frequency.setValueAtTime(587.33, t);
      osc.frequency.setValueAtTime(880, t + 0.1);
      gain.gain.setValueAtTime(0.18, t);
      gain.gain.exponentialRampToValueAtTime(0.001, t + 0.35);
      osc.start(t);
      osc.stop(t + 0.35);
    } else if (type === 'duplicate') {
      osc.type = 'triangle';
      osc.frequency.setValueAtTime(392, t);
      osc.frequency.setValueAtTime(329.63, t + 0.12);
      gain.gain.setValueAtTime(0.2, t);
      gain.gain.exponentialRampToValueAtTime(0.001, t + 0.3);
      osc.start(t);
      osc.stop(t + 0.3);
    } else {
      osc.type = 'sawtooth';
      osc.frequency.setValueAtTime(261.63, t);
      gain.gain.setValueAtTime(0.15, t);
      gain.gain.exponentialRampToValueAtTime(0.001, t + 0.25);
      osc.start(t);
      osc.stop(t + 0.25);
    }
  } catch(e) {}
};

// Global App State
let selectedDirection = 'Entrance';
let lastScanTimestamp = 0;
let isFirstPoll = true;
let isPollingReg = false;
let regPollInterval = null;
let allAttendeesData = [];
let allCardsData = [];
let firstBootModalShown = false;
let manualTimeSetByUser = false;
let scanDisplayTimeout = null;

// Theme Engine (Optimized Dark Mode)
window.initTheme = function() {
  const saved = localStorage.getItem('theme');
  const isDark = saved ? saved === 'dark' : (window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches);
  window.setTheme(isDark ? 'dark' : 'light');
};

window.setTheme = function(theme) {
  document.documentElement.setAttribute('data-theme', theme);
  localStorage.setItem('theme', theme);
  const isDark = theme === 'dark';
  document.querySelectorAll('.theme-sun').forEach(el => el.style.display = isDark ? 'inline-block' : 'none');
  document.querySelectorAll('.theme-moon').forEach(el => el.style.display = isDark ? 'none' : 'inline-block');
};

window.toggleTheme = function() {
  const current = document.documentElement.getAttribute('data-theme') || 'light';
  window.setTheme(current === 'dark' ? 'light' : 'dark');
};

// Entrance Screen Step 1 -> Step 2
window.goToEntranceStep = function(stepNum) {
  if (stepNum === 2) {
    const titleVal = document.getElementById('sessionTitleInput').value.trim() || 'General Session';
    document.getElementById('step2TitlePreview').innerText = titleVal;
    document.getElementById('entranceStep1').classList.remove('active');
    document.getElementById('entranceStep2').classList.add('active');
  } else {
    document.getElementById('entranceStep2').classList.remove('active');
    document.getElementById('entranceStep1').classList.add('active');
  }
};

window.selectEntranceDirection = function(dir) {
  selectedDirection = dir;
  document.getElementById('dirCardEntrance').classList.toggle('selected', dir === 'Entrance');
  document.getElementById('dirCardExit').classList.toggle('selected', dir === 'Exit');
};

// Start Session
window.startSession = async function() {
  const title = document.getElementById('sessionTitleInput').value.trim() || 'General Session';
  const formData = new URLSearchParams();
  formData.append('type', selectedDirection);
  formData.append('title', title);

  try {
    const res = await fetch('/api/session/start', { method: 'POST', body: formData });
    const data = await res.json();
    if (data.success) {
      window.setSessionSavedState(false);
      window.pollStatus();
      window.loadAttendanceLog();
    } else {
      alert('Notice: ' + data.message);
    }
  } catch(e) {
    alert('Network error: ' + e);
  }
};

// Stop Session (Returns to Entrance Screen)
window.stopSession = async function() {
  if (!confirm('End the current attendance session?')) return;
  try {
    const res = await fetch('/api/session/stop', { method: 'POST' });
    const data = await res.json();
    if (scanDisplayTimeout) {
      clearTimeout(scanDisplayTimeout);
      scanDisplayTimeout = null;
    }
    window.setSessionSavedState(false);
    window.showIdleTapPrompt();
    window.goToEntranceStep(1);
    window.pollStatus();
  } catch(e) {
    alert('Network error: ' + e);
  }
};

window.setSessionMode = async function(targetMode) {
  try {
    const isExit = (targetMode === 'Exit');
    const rfidModeEntrance = document.getElementById('rfidModeEntrance');
    const rfidModeExit = document.getElementById('rfidModeExit');
    if (rfidModeEntrance && rfidModeExit) {
      rfidModeEntrance.classList.toggle('active-in', !isExit);
      rfidModeExit.classList.toggle('active-out', isExit);
    }
    const idleModePill = document.getElementById('idleModePill');
    if (idleModePill) {
      idleModePill.innerText = `● ${targetMode.toUpperCase()} MODE (${isExit ? 'OUT' : 'IN'})`;
    }
    const modeChip = document.getElementById('activeModeChip');
    if (modeChip) {
      modeChip.innerText = targetMode;
      modeChip.className = isExit ? 'badge badge-out' : 'badge badge-in';
    }

    const formData = new URLSearchParams();
    formData.append('mode', targetMode);
    const res = await fetch('/api/session/set_mode', { method: 'POST', body: formData });
    const data = await res.json();
    if (data.success) {
      window.pollStatus();
    }
  } catch(e) {
    console.error('Mode switch error:', e);
  }
};

window.toggleSessionMode = async function() {
  const currentChip = document.getElementById('activeModeChip');
  const target = (currentChip && currentChip.innerText === 'Exit') ? 'Entrance' : 'Exit';
  await window.setSessionMode(target);
};

window.clearAttendanceLog = async function() {
  if (!confirm('Reset current attendance log?')) return;
  try {
    const res = await fetch('/api/session/clear', { method: 'POST' });
    const data = await res.json();
    window.loadAttendanceLog();
  } catch(e) {
    alert('Network error: ' + e);
  }
};

// Clock Synchronization & 12-Hour Format Helpers
window.formatTime12h = function(str) {
  if (!str || str === '--' || str === 'Time Not Set' || str.includes('Not Set')) return '--';
  str = String(str).trim();
  
  // If it already has AM/PM
  const hasAmPm = /am|pm/i.test(str);
  if (hasAmPm) {
    // If it has a date prefix like "2026-11-10 04:56:15 PM", strip the date portion
    const parts = str.split(/\s+/);
    if (parts.length >= 3 && parts[0].includes('-')) {
      return parts.slice(1).join(' ');
    }
    return str;
  }
  
  // Extract time portion if prefixed by date "YYYY-MM-DD HH:mm:ss"
  let timePart = str;
  if (timePart.includes(' ')) {
    timePart = timePart.split(' ')[1];
  }
  
  // Parse HH:mm:ss or HH:mm
  const parts = timePart.split(':');
  if (parts.length >= 2) {
    let h = parseInt(parts[0], 10);
    const m = parts[1];
    const s = (parts[2] !== undefined && parts[2] !== '') ? parts[2] : null;
    if (!isNaN(h)) {
      const ampm = h >= 12 ? 'PM' : 'AM';
      h = h % 12;
      if (h === 0) h = 12;
      const hStr = h < 10 ? '0' + h : '' + h;
      return s !== null ? `${hStr}:${m}:${s} ${ampm}` : `${hStr}:${m} ${ampm}`;
    }
  }
  return str;
};

window.formatClock = function(str) {
  if (!str || str === 'Time Not Set' || str.includes('Not Set')) return '--:--:--';
  return window.formatTime12h(str);
};

window.sendTimeSync = async function(epochSec, tzOffsetMin) {
  const formData = new URLSearchParams();
  formData.append('epoch', epochSec);
  formData.append('tz_offset', tzOffsetMin);
  const res = await fetch('/api/time', { method: 'POST', body: formData });
  return await res.json();
};

window.syncBrowserTime = async function() {
  const now = new Date();
  const epoch = Math.floor(now.getTime() / 1000);
  const tzOffset = now.getTimezoneOffset();
  try {
    const data = await window.sendTimeSync(epoch, tzOffset);
    if (data.success) {
      manualTimeSetByUser = true;
      window.pollStatus();
      alert('Clock synchronized: ' + data.time);
    } else {
      alert('Sync failed: ' + data.message);
    }
  } catch(e) {
    alert('Network error: ' + e);
  }
};

window.syncBrowserTimeFromModal = async function() {
  const now = new Date();
  const epoch = Math.floor(now.getTime() / 1000);
  const tzOffset = now.getTimezoneOffset();
  try {
    const data = await window.sendTimeSync(epoch, tzOffset);
    if (data.success) {
      manualTimeSetByUser = true;
      const fb = document.getElementById('firstBootTimeFeedback');
      fb.style.display = 'block';
      fb.innerHTML = '&#10003; Clock synchronized: <b>' + data.time + '</b>';
      window.pollStatus();
    }
  } catch(e) {
    alert('Error syncing time: ' + e);
  }
};

window.applyFirstBootManualTime = async function() {
  const val = document.getElementById('firstBootDateTime').value;
  if (!val) { alert('Please pick date and time'); return; }
  const d = new Date(val);
  const epoch = Math.floor(d.getTime() / 1000);
  const tzOffset = d.getTimezoneOffset();
  try {
    const data = await window.sendTimeSync(epoch, tzOffset);
    if (data.success) {
      manualTimeSetByUser = true;
      const fb = document.getElementById('firstBootTimeFeedback');
      fb.style.display = 'block';
      fb.innerHTML = '&#10003; Clock set: <b>' + data.time + '</b>';
      window.pollStatus();
    }
  } catch(e) {
    alert('Error: ' + e);
  }
};

window.dismissFirstBootModal = function() {
  document.getElementById('firstBootModal').classList.remove('active');
  window.notifyModalState();
};

window.openTimeModal = function() {
  const now = new Date();
  now.setMinutes(now.getMinutes() - now.getTimezoneOffset());
  document.getElementById('modalDateTimeInput').value = now.toISOString().slice(0, 16);
  document.getElementById('timeModal').classList.add('active');
  window.notifyModalState();
};

window.closeTimeModal = function() {
  document.getElementById('timeModal').classList.remove('active');
  window.notifyModalState();
};

window.applyModalManualTime = async function() {
  const val = document.getElementById('modalDateTimeInput').value;
  if (!val) { alert('Please select date and time'); return; }
  const d = new Date(val);
  const epoch = Math.floor(d.getTime() / 1000);
  const tzOffset = d.getTimezoneOffset();
  try {
    const data = await window.sendTimeSync(epoch, tzOffset);
    if (data.success) {
      manualTimeSetByUser = true;
      window.closeTimeModal();
      window.pollStatus();
      alert('Clock set: ' + data.time);
    } else {
      alert('Error: ' + data.message);
    }
  } catch(e) {
    alert('Error: ' + e);
  }
};

// Modal State Synchronization & Scanning Guard
window.notifyModalState = function() {
  const isSettings = document.getElementById('settingsModal')?.classList.contains('active');
  const isTime = document.getElementById('timeModal')?.classList.contains('active');
  const isFirstBoot = document.getElementById('firstBootModal')?.classList.contains('active');
  const isHistory = document.getElementById('historyModal')?.classList.contains('active');
  const anyModalOpen = Boolean(isSettings || isTime || isFirstBoot || isHistory);
  fetch('/api/settings/state?open=' + (anyModalOpen ? '1' : '0'), { method: 'POST' }).catch(()=>{});
};


// ============================================================================
// SESSION HISTORY & SAVING ENGINE
// ============================================================================
let allHistoryData = [];
let currentDetailSession = null;
let currentDetailAttendees = [];

window.loadSessionHistory = async function() {
  try {
    const res = await fetch('/api/history');
    const data = await res.json();
    allHistoryData = data;
    const badge = document.getElementById('historyCountBadge') || document.getElementById('notionHistoryBadge');
    if (badge) badge.innerText = data.length;
    const totalEl = document.getElementById('notionHistoryTotalText');
    if (totalEl) totalEl.innerText = `${data.length} sessions`;
    const q = document.getElementById('historySearch')?.value.trim();
    if (q) window.filterHistoryList();
    else window.renderHistoryList(data);
  } catch(e) {
    console.error('History load error:', e);
  }
};
    if (q) window.filterHistoryList();
    else window.renderHistoryList(data);
  } catch(e) {
    console.error('History load error:', e);
  }
};

window.renderHistoryList = function(sessions) {
  const tbody = document.getElementById('historyTableBody');
  if (!sessions || sessions.length === 0) {
    tbody.innerHTML = '<tr><td colspan="6" style="text-align: center; color: var(--md-sys-color-on-surface-variant); padding: 28px;">No saved sessions in history.</td></tr>';
    return;
  }
  let html = '';
  sessions.forEach((s, idx) => {
    const modeBadge = s.type === 'Exit' ? '<span class="badge badge-out">Exit</span>' : '<span class="badge badge-in">Entrance</span>';
    const safeTitle = (s.title || 'session').trim().replace(/[/\\:*?"<>|\s,;]+/g, '_').replace(/^_+|_+$/g, '') || 'session';
    html += `<tr>
      <td>${idx + 1}</td>
      <td><strong>${window.escapeHtml(s.title)}</strong></td>
      <td>${modeBadge}</td>
      <td class="mono-txt" style="font-size: 0.8125rem;">${window.escapeHtml(s.date)} ${window.formatTime12h(s.save_time)}</td>
      <td><span class="badge badge-done">${s.attendee_count} Attendees</span></td>
      <td style="text-align: center;">
        <div style="display: inline-flex; gap: 4px;">
          <button class="btn btn-tonal btn-sm" onclick="viewSessionDetail('${s.id}')" title="View Attendance">View</button>
          <a href="/api/history/export.csv?id=${encodeURIComponent(s.id)}" download="${window.escapeHtml(safeTitle)}.csv" class="btn btn-outlined btn-sm" title="Export CSV">CSV</a>
          <button class="icon-btn" onclick="deleteHistorySession('${s.id}')" title="Delete" style="color: var(--md-sys-color-error); width: 28px; height: 28px;">
            <svg class="icon-sm" viewBox="0 0 24 24"><path d="M6 19c0 1.1.9 2 2 2h8c1.1 0 2-.9 2-2V7H6v12zM19 4h-3.5l-1-1h-5l-1 1H5v2h14V4z"/></svg>
          </button>
        </div>
      </td>
    </tr>`;
  });
  tbody.innerHTML = html;
};

window.filterHistoryList = function() {
  const q = document.getElementById('historySearch').value.toLowerCase().trim();
  if (!q) { window.renderHistoryList(allHistoryData); return; }
  const filtered = allHistoryData.filter(s => 
    s.title.toLowerCase().includes(q) || 
    s.date.toLowerCase().includes(q) || 
    s.type.toLowerCase().includes(q)
  );
  window.renderHistoryList(filtered);
};

window.viewSessionDetail = async function(id) {
  try {
    const res = await fetch('/api/history/session?id=' + encodeURIComponent(id));
    const data = await res.json();
    if (!data.success) { alert(data.message || 'Could not load session'); return; }
    currentDetailSession = data;
    currentDetailAttendees = data.records || [];

    document.getElementById('historyListView').style.display = 'none';
    document.getElementById('historyDetailView').style.display = 'flex';
    const dlgTitle = document.getElementById('historyDialogTitle') || document.getElementById('notionPaneTitle');
    if (dlgTitle) dlgTitle.innerText = data.title;
    document.getElementById('historyDetailTitle').innerText = data.title;
    document.getElementById('historyDetailMeta').innerText = `Date: ${data.date} • Saved: ${window.formatTime12h(data.save_time)} • Mode: ${data.type}`;
    document.getElementById('historyDetailCountBadge').innerText = `${currentDetailAttendees.length} Attendees`;
    document.getElementById('historyExportCsvBtn').href = `/api/history/export.csv?id=${encodeURIComponent(id)}`;
    const safeDetailTitle = (data.title || 'session').trim().replace(/[/\\:*?"<>|\s,;]+/g, '_').replace(/^_+|_+$/g, '') || 'session';
    document.getElementById('historyExportCsvBtn').setAttribute('download', safeDetailTitle + '.csv');

    window.renderHistoryDetailTable(currentDetailAttendees);
  } catch(e) {
    alert('Error loading session details: ' + e);
  }
};

window.backToHistoryList = function() {
  const detail = document.getElementById('historyDetailView');
  const list = document.getElementById('historyListView');
  if (detail) detail.style.display = 'none';
  if (list) list.style.display = 'flex';
  const dlgTitle = document.getElementById('historyDialogTitle') || document.getElementById('notionPaneTitle');
  if (dlgTitle) dlgTitle.innerText = 'Session History';
  currentDetailSession = null;
};

window.renderHistoryDetailTable = function(records) {
  const tbody = document.getElementById('historyDetailTableBody');
  if (!records || records.length === 0) {
    tbody.innerHTML = '<tr><td colspan="7" style="text-align: center; color: var(--md-sys-color-on-surface-variant); padding: 24px;">No attendees recorded in this session.</td></tr>';
    return;
  }
  let html = '';
  records.forEach((r, idx) => {
    const sId = r.student_id ? window.escapeHtml(r.student_id) : '<span style="color: var(--md-sys-color-outline);">&mdash;</span>';
    html += `<tr>
      <td>${idx + 1}</td>
      <td><strong>${window.escapeHtml(r.name)}</strong></td>
      <td>${sId}</td>
      <td><span class="mono-txt">${window.escapeHtml(r.uid)}</span></td>
      <td class="mono-txt">${window.formatTime12h(r.time_in)}</td>
      <td class="mono-txt">${window.formatTime12h(r.time_out)}</td>
      <td><span class="badge badge-done">${window.escapeHtml(r.status || 'Present')}</span></td>
    </tr>`;
  });
  tbody.innerHTML = html;
};

window.filterHistoryDetailTable = function() {
  const q = document.getElementById('historyDetailSearch').value.toLowerCase().trim();
  if (!q) { window.renderHistoryDetailTable(currentDetailAttendees); return; }
  const filtered = currentDetailAttendees.filter(r => 
    r.name.toLowerCase().includes(q) || 
    (r.student_id && r.student_id.toLowerCase().includes(q)) || 
    r.uid.toLowerCase().includes(q)
  );
  window.renderHistoryDetailTable(filtered);
};

window.deleteHistorySession = async function(id) {
  if (!confirm('Delete this saved session from history?')) return;
  try {
    const res = await fetch('/api/history/delete?id=' + encodeURIComponent(id), { method: 'POST' });
    const data = await res.json();
    if (data.success) window.loadSessionHistory();
    else alert('Error: ' + data.message);
  } catch(e) {
    alert('Error: ' + e);
  }
};

window.deleteCurrentDetailSession = async function() {
  if (!currentDetailSession) return;
  await window.deleteHistorySession(currentDetailSession.id);
  window.backToHistoryList();
};

window.clearAllHistory = async function() {
  if (!confirm('Are you sure you want to delete ALL saved session history?')) return;
  if (!confirm('Confirm again: This cannot be undone.')) return;
  try {
    const res = await fetch('/api/history/clear', { method: 'POST' });
    const data = await res.json();
    window.loadSessionHistory();
  } catch(e) {
    alert('Error: ' + e);
  }
};

let isSessionSaved = false;

window.setSessionSavedState = function(isSaved) {
  isSessionSaved = isSaved;
  const btn = document.getElementById('btnSessionLifecycle');
  const icon = document.getElementById('lifecycleBtnIcon');
  const txt = document.getElementById('lifecycleBtnText');
  if (!btn || !icon || !txt) return;

  if (isSaved) {
    btn.className = 'btn btn-danger-tonal btn-sm';
    btn.title = 'Session saved! Click to finish and end session';
    icon.innerHTML = '<path d="M6 6h12v12H6z"/>';
    txt.innerHTML = '<span class="btn-lifecycle-text-full">End Session</span><span class="btn-lifecycle-text-short">End</span>';
  } else {
    btn.className = 'btn btn-tonal btn-sm';
    btn.title = 'Save active session to history';
    icon.innerHTML = '<path d="M17 3H5c-1.11 0-2 .9-2 2v14c0 1.1.89 2 2 2h14c1.1 0 2-.9 2-2V7l-4-4zm2 16H5V5h11.17L19 7.83V19zm-7-7c-1.66 0-3 1.34-3 3s1.34 3 3 3 3-1.34 3-3-1.34-3-3-3zM6 6h9v4H6z"/>';
    txt.innerHTML = '<span class="btn-lifecycle-text-full">Save Session</span><span class="btn-lifecycle-text-short">Save</span>';
  }
};

window.saveActiveSession = async function() {
  try {
    const res = await fetch('/api/session/save', { method: 'POST' });
    if (!res.ok) {
      const errTxt = await res.text();
      throw new Error(`Server returned HTTP ${res.status}: ${errTxt}`);
    }
    const data = await res.json();
    if (data.success) {
      alert(data.message || 'Session saved to history!');
      window.setSessionSavedState(true);
      window.loadSessionHistory();
    } else {
      if (confirm((data.message || 'Could not save session.') + '\n\nDo you want to end the session now anyway?')) {
        await window.stopSession();
      }
    }
  } catch(e) {
    alert('Failed to save session: ' + (e.message || e));
  }
};

window.handleCombinedSessionAction = async function() {
  if (!isSessionSaved) {
    // First shown: Save Session
    await window.saveActiveSession();
  } else {
    // After saved: End Session
    await window.stopSession();
  }
};

// ============================================================================
// NOTION-STYLE SETTINGS CONTROLS & TRIGGERS
// ============================================================================
window.switchNotionTab = function(paneId) {
  if (paneId === 'set-register' || paneId === 'set-reg') paneId = 'notion-reg-single';
  else if (paneId === 'set-batch') paneId = 'notion-reg-batch';
  else if (paneId === 'set-cards') paneId = 'notion-cards';
  else if (paneId === 'set-history') paneId = 'notion-history';
  else if (paneId === 'set-prefs') paneId = 'notion-rules';

  document.querySelectorAll('.notion-pane').forEach(el => el.classList.remove('active'));
  document.querySelectorAll('.notion-nav-item').forEach(el => el.classList.remove('active'));

  const target = document.getElementById(paneId);
  if (target) target.classList.add('active');

  const navBtn = Array.from(document.querySelectorAll('.notion-nav-item')).find(b => {
    const oc = b.getAttribute('onclick');
    return oc && oc.includes(paneId);
  });
  if (navBtn) navBtn.classList.add('active');

  const titleEl = document.getElementById('notionPaneTitle');
  const subEl = document.getElementById('notionPaneSubtitle');
  if (target) {
    if (titleEl) titleEl.innerText = target.getAttribute('data-title') || 'Settings';
    if (subEl) subEl.innerText = target.getAttribute('data-subtitle') || '';
  }

  // Mobile navigation: slide into detail view
  const dialog = document.getElementById('notionSettingsDialog');
  if (dialog) dialog.classList.add('mobile-detail-active');

  // Trigger data refreshes on tab activation
  if (paneId === 'notion-cards') {
    window.loadCardsDatabase();
  } else if (paneId === 'notion-history') {
    window.backToHistoryList();
    window.loadSessionHistory();
  }
};

window.closeNotionMobileDetail = function() {
  const dialog = document.getElementById('notionSettingsDialog');
  if (dialog) dialog.classList.remove('mobile-detail-active');
};

window.openSettingsModal = function(targetTab) {
  const modal = document.getElementById('settingsModal');
  const dialog = document.getElementById('notionSettingsDialog');
  if (modal) modal.classList.add('active');
  if (dialog && !targetTab) {
    dialog.classList.remove('mobile-detail-active');
  }
  if (targetTab) {
    window.switchNotionTab(targetTab);
  }
  window.loadCardsDatabase();
  window.loadSessionHistory();
  window.notifyModalState();
};

window.closeSettingsModal = function() {
  const modal = document.getElementById('settingsModal');
  if (modal) modal.classList.remove('active');
  window.notifyModalState();
};

window.openHistoryModal = function() {
  window.openSettingsModal('notion-history');
};

window.closeHistoryModal = function() {
  window.closeSettingsModal();
};

window.switchSettingsTab = function(paneId) {
  window.switchNotionTab(paneId);
};

// Settings Form Updater (Registration, Debounce, LCD Timeout, Wi-Fi Guide)
window.updateSettings = async function() {
  const reqReg = document.getElementById('requireRegToggle')?.checked ? '1' : '0';
  const debounceVal = document.getElementById('cardDebounceSelect')?.value || '1500';
  const lcdTimeoutVal = document.getElementById('lcdTimeoutSelect')?.value || '45000';
  const wifiGuideVal = document.getElementById('lcdWifiGuideToggle')?.checked ? '1' : '0';

  const formData = new URLSearchParams();
  formData.append('require_registration', reqReg);
  formData.append('card_debounce', debounceVal);
  formData.append('lcd_timeout', lcdTimeoutVal);
  formData.append('lcd_wifi_guide', wifiGuideVal);

  try {
    await fetch('/api/settings', { method: 'POST', body: formData });
  } catch(e) {
    console.error('Settings update error:', e);
  }
};

// Hardware & LCD Triggers
window.triggerLcdSleep = async function() {
  try {
    const res = await fetch('/api/lcd/sleep', { method: 'POST' });
    const d = await res.json();
    alert(d.message || 'LCD backlight turned off.');
  } catch(e) { alert('Error: ' + e); }
};

window.triggerLcdWake = async function() {
  try {
    const res = await fetch('/api/lcd/wake', { method: 'POST' });
    const d = await res.json();
    alert(d.message || 'LCD backlight awakened.');
  } catch(e) { alert('Error: ' + e); }
};

window.triggerLcdTest = async function() {
  try {
    const res = await fetch('/api/lcd/test', { method: 'POST' });
    const d = await res.json();
    alert(d.message || 'Test pattern sent to LCD.');
  } catch(e) { alert('Error: ' + e); }
};

// System Reboot Trigger
window.triggerSystemReboot = async function() {
  if (!confirm('Reboot ESP32 now? Connected Wi-Fi clients will temporarily disconnect.')) return;
  try {
    await fetch('/api/system/reboot', { method: 'POST' });
    alert('ESP32 is restarting... Please reconnect to the Wi-Fi hotspot in 5 seconds.');
    window.closeSettingsModal();
  } catch(e) {
    alert('Reboot signal sent. Reconnecting...');
    window.closeSettingsModal();
  }
};

// Date & Time Custom Setter
window.setCustomDeviceTime = async function() {
  const val = document.getElementById('notionCustomTimeInput')?.value;
  if (!val) { alert('Please pick a date and time.'); return; }
  const d = new Date(val);
  const epoch = Math.floor(d.getTime() / 1000);
  const tzOffset = d.getTimezoneOffset();
  try {
    const res = await window.sendTimeSync(epoch, tzOffset);
    if (res.success) {
      manualTimeSetByUser = true;
      window.pollStatus();
      alert('Device time updated: ' + res.time);
    } else {
      alert('Error setting time: ' + res.message);
    }
  } catch(e) { alert('Error: ' + e); }
};

// Bulk Import Handlers
window.handleImportFileSelect = function(event) {
  const file = event.target.files[0];
  if (!file) return;
  const reader = new FileReader();
  reader.onload = function(e) {
    document.getElementById('importCardsInput').value = e.target.result;
    const msg = document.getElementById('importStatusMsg');
    if (msg) {
      msg.style.display = 'block';
      msg.style.color = 'var(--md-sys-color-primary)';
      msg.innerText = `Loaded file: ${file.name} (${file.size} bytes). Click "Import to Cards Database" to save.`;
    }
  };
  reader.readAsText(file);
};

window.submitImportCards = async function() {
  const data = document.getElementById('importCardsInput')?.value.trim();
  const statusEl = document.getElementById('importStatusMsg');
  if (!data) {
    alert('Please enter or upload CSV card data.');
    return;
  }
  const formData = new URLSearchParams();
  formData.append('data', data);
  try {
    const res = await fetch('/api/cards/import', { method: 'POST', body: formData });
    const json = await res.json();
    if (statusEl) {
      statusEl.style.display = 'block';
      statusEl.style.color = json.success ? 'var(--md-sys-color-primary)' : 'var(--md-sys-color-error)';
      statusEl.innerText = json.message || 'Import finished.';
    }
    alert(json.message || 'Import completed.');
    window.loadCardsDatabase();
  } catch(e) {
    alert('Import error: ' + e);
  }
};

window.sendImportToBatch = function() {
  const data = document.getElementById('importCardsInput')?.value.trim();
  if (!data) { alert('Please paste names into the import box first.'); return; }
  document.getElementById('batchNamesInput').value = data;
  window.switchNotionTab('notion-reg-batch');
  window.updateBatchCount();
};

// Tag Registration
window.startRegistration = async function() {
  const name = document.getElementById('regOwnerName').value.trim();
  const idNum = document.getElementById('regOwnerId').value.trim();
  if (!name) {
    alert('Please enter owner full name.');
    document.getElementById('regOwnerName').focus();
    return;
  }
  const formData = new URLSearchParams();
  formData.append('name', name);
  formData.append('student_id', idNum);

  try {
    const res = await fetch('/api/register/start', { method: 'POST', body: formData });
    const data = await res.json();
    if (data.success) {
      document.getElementById('regModalOwnerName').innerText = name + (idNum ? ' (' + idNum + ')' : '');
      document.getElementById('regModalStatusText').innerText = 'Hold card against the RC522 reader...';
      document.getElementById('regModalStatusText').style.color = 'var(--md-sys-color-warning)';
      document.getElementById('regCountdownBar').style.width = '100%';
      document.getElementById('regModalSuccessActions').style.display = 'none';
      document.getElementById('regModalCancelBtn').style.display = 'block';
      document.getElementById('regModal').classList.add('active');
      window.startRegPolling();
    } else {
      alert('Error: ' + data.message);
    }
  } catch(e) {
    alert('Network error: ' + e);
  }
};

window.startRegPolling = function() {
  if (regPollInterval) clearInterval(regPollInterval);
  isPollingReg = true;
  regPollInterval = setInterval(async () => {
    try {
      const res = await fetch('/api/register/status');
      const data = await res.json();

      if (data.state === 'waiting') {
        const pct = Math.max(0, Math.min(100, (data.remaining_ms / 30000) * 100));
        document.getElementById('regCountdownBar').style.width = pct + '%';
      } else if (data.state === 'success') {
        clearInterval(regPollInterval);
        isPollingReg = false;
        window.playAudioTone('success');
        document.getElementById('regModalStatusText').innerHTML = '&#10003; Card Registered! UID: <b>' + data.result_uid + '</b>';
        document.getElementById('regModalStatusText').style.color = 'var(--md-sys-color-success)';
        document.getElementById('regCountdownBar').style.width = '100%';
        document.getElementById('regModalCancelBtn').style.display = 'none';
        document.getElementById('regModalSuccessActions').style.display = 'flex';
        window.loadCardsDatabase();
      } else if (data.state === 'conflict') {
        clearInterval(regPollInterval);
        isPollingReg = false;
        window.playAudioTone('alert');
        const conflictOwner = data.conflict_name || 'another person';
        const conflictId = data.conflict_id ? ' (' + data.conflict_id + ')' : '';
        document.getElementById('regModalStatusText').innerHTML = '⚠️ <b>Card Already Assigned!</b><br>UID: <code>' + (data.result_uid || '') + '</code> is already bound to <b>' + window.escapeHtml(conflictOwner) + conflictId + '</b>.<br>One card is strictly registered to one person! Please tap a different card.';
        document.getElementById('regModalStatusText').style.color = 'var(--md-sys-color-error)';
        document.getElementById('regCountdownBar').style.width = '100%';
        // Automatically re-arm after 1.8s so user can just tap an unassigned card
        setTimeout(() => {
          if (document.getElementById('regModal').classList.contains('active')) {
            window.startRegistration();
          }
        }, 1800);
      } else if (data.state === 'timeout') {
        clearInterval(regPollInterval);
        isPollingReg = false;
        window.playAudioTone('alert');
        document.getElementById('regModalStatusText').innerText = 'Timed out. No tag was detected.';
        document.getElementById('regModalStatusText').style.color = 'var(--md-sys-color-error)';
      } else if (data.state === 'cancelled') {
        clearInterval(regPollInterval);
        isPollingReg = false;
        document.getElementById('regModal').classList.remove('active');
      }
    } catch(e) {}
  }, 450);
};

window.registerAnotherCard = function() {
  document.getElementById('regModal').classList.remove('active');
  document.getElementById('regOwnerName').value = '';
  document.getElementById('regOwnerId').value = '';
  document.getElementById('regOwnerName').focus();
};

window.closeRegModal = function() {
  document.getElementById('regModal').classList.remove('active');
  window.switchSettingsTab('set-cards');
};

window.cancelRegistration = async function() {
  if (regPollInterval) clearInterval(regPollInterval);
  isPollingReg = false;
  try {
    await fetch('/api/register/cancel', { method: 'POST' });
  } catch(e) {}
  document.getElementById('regModal').classList.remove('active');
};

// Cards Database

  // ============================================================================
  // BATCH REGISTRATION & GUIDED SCANNING SYSTEM
  // ============================================================================
  let batchAttendees = [];
  let batchCurrentIndex = 0;
  let batchPollInterval = null;
  let isBatchRunning = false;

  window.updateBatchCount = function() {
    const text = document.getElementById('batchNamesInput').value;
    const lines = text.split('\n').map(l => l.trim()).filter(l => l.length > 0);
    const badge = document.getElementById('batchCountBadge');
    if (badge) {
      badge.innerText = `${lines.length} attendee${lines.length === 1 ? '' : 's'} detected`;
      badge.className = lines.length > 0 ? 'badge badge-in' : 'badge';
    }
  };

  window.clearBatchInput = function() {
    document.getElementById('batchNamesInput').value = '';
    window.updateBatchCount();
  };

  window.startBatchScanning = async function() {
    const text = document.getElementById('batchNamesInput').value;
    const lines = text.split('\n').map(l => l.trim()).filter(l => l.length > 0);
    if (lines.length === 0) {
      alert('Please enter or paste at least one name.');
      return;
    }

    batchAttendees = lines.map((line, idx) => {
      let name = line;
      let id = '';
      if (line.includes(',')) {
        const parts = line.split(',');
        name = parts[0].trim();
        id = parts.slice(1).join(',').trim();
      }
      return {
        index: idx + 1,
        name: name,
        id: id,
        uid: '',
        status: 'pending' // 'pending', 'active', 'done', 'skipped'
      };
    });

    batchCurrentIndex = 0;
    isBatchRunning = true;

    document.getElementById('batchInputView').style.display = 'none';
    document.getElementById('batchScanningView').style.display = 'flex';
    document.getElementById('batchCompleteView').style.display = 'none';

    window.renderBatchQueue();
    window.processNextBatchAttendee();
  };

  window.processNextBatchAttendee = async function() {
    if (!isBatchRunning) return;

    if (batchCurrentIndex >= batchAttendees.length) {
      window.finishBatchScanning();
      return;
    }

    const current = batchAttendees[batchCurrentIndex];
    current.status = 'active';
    window.renderBatchQueue();

    const total = batchAttendees.length;
    const step = batchCurrentIndex + 1;
    const pct = Math.round((step / total) * 100);

    document.getElementById('batchProgressLabel').innerText = `Scanning ${step} of ${total}`;
    document.getElementById('batchProgressPct').innerText = `${pct}%`;
    document.getElementById('batchProgressBar').style.width = `${pct}%`;

    document.getElementById('batchActiveName').innerText = current.name;
    document.getElementById('batchActiveId').innerText = current.id ? `ID: ${current.id}` : '(No ID assigned)';

    const spotlight = document.getElementById('batchSpotlightCard');
    spotlight.style.borderColor = 'var(--md-sys-color-primary)';
    spotlight.style.background = 'var(--md-sys-color-surface-container-high)';

    const promptBox = document.getElementById('batchStatusPrompt');
    promptBox.innerHTML = `👉 Place RFID card or keyfob on reader for <b>${window.escapeHtml(current.name)}</b>...`;
    promptBox.style.color = 'var(--md-sys-color-on-surface)';
    promptBox.style.borderColor = 'var(--md-sys-color-outline)';

    const formData = new URLSearchParams();
    formData.append('name', current.name);
    if (current.id) formData.append('student_id', current.id);
    formData.append('batch_info', `${step}/${total}`);

    try {
      const res = await fetch('/api/register/start', { method: 'POST', body: formData });
      const data = await res.json();
      if (!data.success) {
        alert('Error starting registration: ' + data.message);
        return;
      }
      window.startBatchPolling();
    } catch(e) {
      alert('Network error: ' + e);
    }
  };

  window.startBatchPolling = function() {
    if (batchPollInterval) clearInterval(batchPollInterval);

    batchPollInterval = setInterval(async () => {
      if (!isBatchRunning) {
        clearInterval(batchPollInterval);
        return;
      }

      try {
        const res = await fetch('/api/register/status');
        const data = await res.json();

        const current = batchAttendees[batchCurrentIndex];
        const spotlight = document.getElementById('batchSpotlightCard');
        const promptBox = document.getElementById('batchStatusPrompt');

        if (data.state === 'success') {
          clearInterval(batchPollInterval);
          batchPollInterval = null;

          current.uid = data.result_uid;
          current.status = 'done';
          window.playAudioTone('success');

          spotlight.style.borderColor = 'var(--md-sys-color-success)';
          spotlight.style.background = 'var(--md-sys-color-success-container)';
          promptBox.innerHTML = `✓ <b>Card Allocated!</b> UID: <code>${data.result_uid}</code> registered to ${window.escapeHtml(current.name)}`;
          promptBox.style.color = 'var(--md-sys-color-on-success-container)';
          promptBox.style.borderColor = 'var(--md-sys-color-success)';

          window.renderBatchQueue();
          window.loadCardsDatabase();

          setTimeout(() => {
            if (!isBatchRunning) return;
            batchCurrentIndex++;
            window.processNextBatchAttendee();
          }, 800);

        } else if (data.state === 'conflict') {
          // STRICT 1-TO-1 PROTECTION: REJECT & DO NOT ADVANCE!
          clearInterval(batchPollInterval);
          batchPollInterval = null;
          window.playAudioTone('alert');

          spotlight.style.borderColor = 'var(--md-sys-color-error)';
          spotlight.style.background = 'var(--md-sys-color-error-container)';
          promptBox.innerHTML = `⚠️ <b>Card Already Bound!</b> This card (<code>${data.result_uid}</code>) is already registered to <b>${window.escapeHtml(data.conflict_name || 'another person')}</b>.<br>One card is strictly registered to one person! Please tap a <b>different, unassigned card</b>.`;
          promptBox.style.color = 'var(--md-sys-color-on-error-container)';
          promptBox.style.borderColor = 'var(--md-sys-color-error)';

          // Re-arm after 1.6s so user can immediately tap an unassigned card
          setTimeout(() => {
            if (!isBatchRunning) return;
            window.processNextBatchAttendee();
          }, 1600);

        } else if (data.state === 'timeout') {
          promptBox.innerHTML = `⏳ Still waiting for card tap for <b>${window.escapeHtml(current.name)}</b>...`;
          setTimeout(() => {
            if (!isBatchRunning) return;
            window.processNextBatchAttendee();
          }, 800);
        }
      } catch(e) {}
    }, 350);
  };

  window.skipBatchCurrent = async function() {
    if (!isBatchRunning || batchCurrentIndex >= batchAttendees.length) return;
    if (batchPollInterval) clearInterval(batchPollInterval);
    try {
      await fetch('/api/register/cancel', { method: 'POST' });
    } catch(e) {}

    batchAttendees[batchCurrentIndex].status = 'skipped';
    window.renderBatchQueue();

    batchCurrentIndex++;
    window.processNextBatchAttendee();
  };

  window.cancelBatchProcess = async function() {
    if (!confirm('Cancel batch registration? Cards already scanned will remain saved.')) return;
    isBatchRunning = false;
    if (batchPollInterval) clearInterval(batchPollInterval);
    try {
      await fetch('/api/register/cancel', { method: 'POST' });
    } catch(e) {}

    window.finishBatchScanning();
  };

  window.finishBatchScanning = function() {
    isBatchRunning = false;
    if (batchPollInterval) clearInterval(batchPollInterval);

    const doneCount = batchAttendees.filter(a => a.status === 'done').length;
    const skipCount = batchAttendees.filter(a => a.status === 'skipped').length;

    document.getElementById('batchScanningView').style.display = 'none';
    document.getElementById('batchCompleteView').style.display = 'flex';
    document.getElementById('batchCompleteSummary').innerText = `${doneCount} card(s) registered successfully, ${skipCount} skipped out of ${batchAttendees.length} total attendees.`;

    window.loadCardsDatabase();
  };

  window.resetBatchInputView = function() {
    document.getElementById('batchCompleteView').style.display = 'none';
    document.getElementById('batchScanningView').style.display = 'none';
    document.getElementById('batchInputView').style.display = 'flex';
    document.getElementById('batchNamesInput').value = '';
    window.updateBatchCount();
  };

  window.renderBatchQueue = function() {
    const tbody = document.getElementById('batchQueueTableBody');
    if (!tbody) return;

    let html = '';
    batchAttendees.forEach((a, idx) => {
      let statusBadge = '<span class="badge" style="opacity: 0.6;">Queued</span>';
      let rowStyle = '';
      if (a.status === 'active') {
        statusBadge = '<span class="badge badge-in">▶ Scanning...</span>';
        rowStyle = 'background: rgba(14, 165, 233, 0.1); font-weight: 700;';
      } else if (a.status === 'done') {
        statusBadge = '<span class="badge badge-done">✓ Done</span>';
      } else if (a.status === 'skipped') {
        statusBadge = '<span class="badge badge-out">Skipped</span>';
      }

      html += `<tr style="${rowStyle}">
        <td>${idx + 1}</td>
        <td><strong>${window.escapeHtml(a.name)}</strong></td>
        <td>${a.id ? window.escapeHtml(a.id) : '<span style="color:var(--md-sys-color-outline);">&mdash;</span>'}</td>
        <td><span class="mono-txt">${a.uid ? window.escapeHtml(a.uid) : '&mdash;'}</span></td>
        <td>${statusBadge}</td>
      </tr>`;
    });
    tbody.innerHTML = html;
  };

  window.loadCardsDatabase = async function() {
  try {
    const res = await fetch('/api/cards');
    const data = await res.json();
    allCardsData = data;
    const q = document.getElementById('cardDbSearch').value.trim();
    if (q) window.filterCardsTable();
    else window.renderCardsTable(data);
    const cntEl = document.getElementById('notionCardsBadge') || document.getElementById('setCardsCount');
    if (cntEl) cntEl.innerText = data.length;
    const totEl = document.getElementById('notionCardsTotalText');
    if (totEl) totEl.innerText = `${data.length} cards`;
  } catch(e) {}
};

window.renderCardsTable = function(cards) {
  const tbody = document.getElementById('cardsTableBody');
  if (!cards || cards.length === 0) {
    tbody.innerHTML = '<tr><td colspan="6" style="text-align: center; color: var(--md-sys-color-on-surface-variant); padding: 24px;">No cards registered yet.</td></tr>';
    return;
  }
  let html = '';
  cards.forEach((c, idx) => {
    const studentId = c.student_id ? window.escapeHtml(c.student_id) : '<span style="color: var(--md-sys-color-outline);">&mdash;</span>';
    const regDate = c.reg_date ? window.formatClock(c.reg_date) : '<span style="color: var(--md-sys-color-outline);">&mdash;</span>';
    html += `<tr>
      <td>${idx + 1}</td>
      <td><strong>${window.escapeHtml(c.name)}</strong></td>
      <td>${studentId}</td>
      <td><span class="mono-txt">${window.escapeHtml(c.uid)}</span></td>
      <td class="mono-txt" style="font-size: 0.8rem; color: var(--md-sys-color-on-surface-variant);">${regDate}</td>
      <td style="text-align: center;">
        <button class="icon-btn" onclick="deleteCard('${c.uid}')" title="Delete Card" style="color: var(--md-sys-color-error); width: 32px; height: 32px; margin: 0 auto;">
          <svg class="icon-sm" viewBox="0 0 24 24"><path d="M6 19c0 1.1.9 2 2 2h8c1.1 0 2-.9 2-2V7H6v12zM19 4h-3.5l-1-1h-5l-1 1H5v2h14V4z"/></svg>
        </button>
      </td>
    </tr>`;
  });
  tbody.innerHTML = html;
};

window.filterCardsTable = function() {
  const q = document.getElementById('cardDbSearch').value.toLowerCase().trim();
  if (!q) { window.renderCardsTable(allCardsData); return; }
  const filtered = allCardsData.filter(c => 
    c.name.toLowerCase().includes(q) || 
    (c.student_id && c.student_id.toLowerCase().includes(q)) || 
    c.uid.toLowerCase().includes(q)
  );
  window.renderCardsTable(filtered);
};

window.deleteCard = async function(uid) {
  if (!confirm(`Delete card UID: ${uid}?`)) return;
  const formData = new URLSearchParams();
  formData.append('uid', uid);
  try {
    const res = await fetch('/api/cards/delete', { method: 'POST', body: formData });
    const data = await res.json();
    if (data.success) window.loadCardsDatabase();
    else alert('Delete failed: ' + data.message);
  } catch(e) {
    alert('Error: ' + e);
  }
};

window.clearAllCards = async function() {
  if (!confirm('Delete ALL registered cards from memory?')) return;
  if (!confirm('Confirm again: Cannot be undone.')) return;
  try {
    const res = await fetch('/api/cards/clear', { method: 'POST' });
    const data = await res.json();
    window.loadCardsDatabase();
    alert(data.message);
  } catch(e) {
    alert('Error: ' + e);
  }
};

// Attendance Log
window.loadAttendanceLog = async function() {
  try {
    const res = await fetch('/api/attendance');
    const data = await res.json();
    allAttendeesData = data;
    const q = document.getElementById('attendanceSearch').value.trim();
    if (q) window.filterAttendanceTable();
    else window.renderAttendanceTable(data);
    window.updateAttendanceStats(data);
  } catch(e) {}
};

window.updateAttendanceStats = function(data) {
  let inCount = 0;
  let outCount = 0;
  let completedCount = 0;
  data.forEach(r => {
    const hasIn = r.time_in && r.time_in !== '--' && r.time_in !== '';
    const hasOut = r.time_out && r.time_out !== '--' && r.time_out !== '';
    if (hasIn) inCount++;
    if (hasOut) outCount++;
    if (hasIn && hasOut) completedCount++;
  });
  document.getElementById('statSummaryText').innerText = `${data.length} total (${inCount} in, ${outCount} out)`;
};

window.renderAttendanceTable = function(records) {
  const tbody = document.getElementById('attendanceTableBody');
  if (!records || records.length === 0) {
    tbody.innerHTML = '<tr><td colspan="5" style="text-align: center; color: var(--md-sys-color-on-surface-variant); padding: 22px;">No attendees logged yet.</td></tr>';
    return;
  }
  let html = '';
  const sorted = [...records].reverse();
  sorted.forEach((r, idx) => {
    const hasIn = r.time_in && r.time_in !== '--' && r.time_in !== '';
    const hasOut = r.time_out && r.time_out !== '--' && r.time_out !== '';
    let statusBadge = '';
    if (hasIn && hasOut) {
      statusBadge = '<span class="badge badge-done">&#10003; Done</span>';
    } else if (hasIn) {
      statusBadge = '<span class="badge badge-in">In</span>';
    } else if (hasOut) {
      statusBadge = '<span class="badge badge-out">Out</span>';
    } else {
      statusBadge = '<span class="badge badge-in">Present</span>';
    }

    html += `<tr>
      <td>${records.length - idx}</td>
      <td><strong>${window.escapeHtml(r.name)}</strong></td>
      <td class="mono-txt">${window.formatTime12h(r.time_in)}</td>
      <td class="mono-txt">${window.formatTime12h(r.time_out)}</td>
      <td>${statusBadge}</td>
    </tr>`;
  });
  tbody.innerHTML = html;
};

window.filterAttendanceTable = function() {
  const q = document.getElementById('attendanceSearch').value.toLowerCase().trim();
  if (!q) { window.renderAttendanceTable(allAttendeesData); return; }
  const filtered = allAttendeesData.filter(r => 
    r.name.toLowerCase().includes(q) || 
    (r.student_id && r.student_id.toLowerCase().includes(q)) || 
    r.uid.toLowerCase().includes(q)
  );
  window.renderAttendanceTable(filtered);
};

// Central System Status Poller
window.pollStatus = async function() {
  try {
    const res = await fetch('/api/status');
    const d = await res.json();

    document.getElementById('headerClock').innerText = d.time_set ? window.formatClock(d.current_time) : '--:--:--';

    const entranceScreen = document.getElementById('entranceScreen');
    const consoleScreen = document.getElementById('consoleScreen');

    if (d.session_active) {
      entranceScreen.style.display = 'none';
      consoleScreen.style.display = 'flex';
      const titleEl = document.getElementById('activeTitleLabel');
      if (titleEl) {
        titleEl.innerText = d.session_title;
        titleEl.title = d.session_title;
      }
      const liveCsvBtn = document.getElementById('btnExportLiveCsv');
      if (liveCsvBtn && d.session_title) {
        const safeLiveTitle = (d.session_title || 'attendance').trim().replace(/[/\\:*?"<>|\s,;]+/g, '_').replace(/^_+|_+$/g, '') || 'attendance';
        liveCsvBtn.setAttribute('download', safeLiveTitle + '.csv');
      }
      const modeChip = document.getElementById('activeModeChip');
      if (modeChip) {
        modeChip.innerText = d.session_type;
        modeChip.className = (d.session_type === 'Exit') ? 'badge badge-out' : 'badge badge-in';
      }

      const switchTarget = document.getElementById('switchTargetMode');
      if (switchTarget) {
        switchTarget.innerText = (d.session_type === 'Entrance') ? 'Exit' : 'Entrance';
      }

      // Update Segmented Mode Switcher in RFID Panel
      const isExit = (d.session_type === 'Exit');
      const rfidModeEntrance = document.getElementById('rfidModeEntrance');
      const rfidModeExit = document.getElementById('rfidModeExit');
      if (rfidModeEntrance && rfidModeExit) {
        rfidModeEntrance.classList.toggle('active-in', !isExit);
        rfidModeExit.classList.toggle('active-out', isExit);
      }

      const idleModePill = document.getElementById('idleModePill');
      if (idleModePill) {
        idleModePill.innerText = `● ${d.session_type.toUpperCase()} MODE (${isExit ? 'OUT' : 'IN'})`;
      }
    } else {
      consoleScreen.style.display = 'none';
      entranceScreen.style.display = 'flex';
      window.setSessionSavedState(false);
    }

    if (!d.time_set && !firstBootModalShown) {
      firstBootModalShown = true;
      const now = new Date();
      now.setMinutes(now.getMinutes() - now.getTimezoneOffset());
      document.getElementById('firstBootDateTime').value = now.toISOString().slice(0, 16);
      document.getElementById('firstBootModal').classList.add('active');
    }

    // Notion Settings Dynamic Badges & Indicators
    const cardsBadge = document.getElementById('notionCardsBadge');
    if (cardsBadge && d.registered_count !== undefined) cardsBadge.innerText = d.registered_count;
    const histBadge = document.getElementById('notionHistoryBadge');
    if (histBadge && d.history_count !== undefined) histBadge.innerText = d.history_count;
    const clkDisp = document.getElementById('notionClockDisplay');
    if (clkDisp) clkDisp.innerText = d.time_set ? window.formatClock(d.current_time) : '--:--:--';
    const wifiClients = document.getElementById('notionWifiClients');
    if (wifiClients && d.wifi_clients !== undefined) wifiClients.innerText = `${d.wifi_clients} connected`;
    const heapText = document.getElementById('notionHeapText');
    if (heapText && d.free_heap) heapText.innerText = `${Math.round(d.free_heap / 1024)} KB free`;
    const fsText = document.getElementById('notionFsUsedText');
    if (fsText && d.fs_used !== undefined) fsText.innerText = `${Math.round(d.fs_used / 1024)} KB used / 1.4 MB`;
    const cardsTotal = document.getElementById('notionCardsTotalText');
    if (cardsTotal && d.registered_count !== undefined) cardsTotal.innerText = `${d.registered_count} cards`;
    const histTotal = document.getElementById('notionHistoryTotalText');
    if (histTotal && d.history_count !== undefined) histTotal.innerText = `${d.history_count} sessions`;
    const uptimeText = document.getElementById('notionUptimeText');
    if (uptimeText && d.uptime_sec !== undefined) {
      const uH = Math.floor(d.uptime_sec / 3600);
      const uM = Math.floor((d.uptime_sec % 3600) / 60);
      const uS = d.uptime_sec % 60;
      uptimeText.innerText = `${uH}h ${uM}m ${uS}s`;
    }
    const liveCsvBtn2 = document.getElementById('btnExportLiveCsv2');
    if (liveCsvBtn2 && d.session_title) {
      const safeLiveTitle = (d.session_title || 'attendance').trim().replace(/[/\\:*?"<>|\s,;]+/g, '_').replace(/^_+|_+$/g, '') || 'attendance';
      liveCsvBtn2.setAttribute('download', safeLiveTitle + '.csv');
    }
    if (!window._settingsLoaded && d.lcd_timeout !== undefined) {
      window._settingsLoaded = true;
      const tSelect = document.getElementById('lcdTimeoutSelect');
      if (tSelect) tSelect.value = d.lcd_timeout;
      const dSelect = document.getElementById('cardDebounceSelect');
      if (dSelect) dSelect.value = d.card_debounce;
      const gToggle = document.getElementById('lcdWifiGuideToggle');
      if (gToggle) gToggle.checked = d.show_lcd_wifi;
      const rToggle = document.getElementById('requireRegToggle');
      if (rToggle) rToggle.checked = d.require_registration;
    }

    if (isFirstPoll) {
      isFirstPoll = false;
      lastScanTimestamp = d.last_scan ? d.last_scan.timestamp : 0;
    } else if (d.last_scan && d.last_scan.timestamp && d.last_scan.timestamp !== lastScanTimestamp) {
      lastScanTimestamp = d.last_scan.timestamp;
      
      const isSettingsOpen = document.getElementById('settingsModal')?.classList.contains('active');
      const isTimeModalOpen = document.getElementById('timeModal')?.classList.contains('active');
      const isFirstBootOpen = document.getElementById('firstBootModal')?.classList.contains('active');
      const isHistoryOpen = document.getElementById('historyModal')?.classList.contains('active');
      const isConsoleVisible = (consoleScreen.style.display === 'flex');

      // Strictly trigger scan display ONLY when in Registration modal OR on the active recording screen!
      if (d.last_scan.type === 'Registration') {
        window.handleNewScan(d.last_scan);
      } else if (d.session_active && isConsoleVisible && !isSettingsOpen && !isTimeModalOpen && !isFirstBootOpen && !isHistoryOpen) {
        window.handleNewScan(d.last_scan);
      }
    }
  } catch(e) {}
};

// ============================================================================
// FULL-SECTION RFID SCREEN HANDLER (Occupies whole section, disappears in 3.5s)
// ============================================================================
window.showIdleTapPrompt = function() {
  const fullCard = document.getElementById('bigRfidFullCard');
  const idleView = document.getElementById('rfidIdleView');
  const resultView = document.getElementById('rfidResultView');

  if (!fullCard || !idleView || !resultView) return;

  fullCard.className = 'rfid-panel-fullcard theme-idle';
  resultView.style.display = 'none';
  idleView.style.display = 'flex';
};

window.handleNewScan = function(scan) {
  // Clear any existing timeout
  if (scanDisplayTimeout) {
    clearTimeout(scanDisplayTimeout);
    scanDisplayTimeout = null;
  }

  const fullCard = document.getElementById('bigRfidFullCard');
  const idleView = document.getElementById('rfidIdleView');
  const resultView = document.getElementById('rfidResultView');
  const badge = document.getElementById('resultStatusBadge');
  const nameEl = document.getElementById('resultAttendeeName');
  const subEl = document.getElementById('resultSubMessage');
  const metaEl = document.getElementById('resultMetaTag');

  const timeStr = window.formatTime12h(scan.time);
  let cleanMsg = (scan.message || '').replace(/\d{4}-\d{2}-\d{2}\s+(\d{1,2}:\d{2}:\d{2}(\s*[AaPp][Mm])?)/g, (m, t) => window.formatTime12h(t));
  cleanMsg = cleanMsg.replace(/\b(\d{2}:\d{2}:\d{2})\b/g, (m, t) => window.formatTime12h(t));

  // Switch to Result View
  idleView.style.display = 'none';
  resultView.style.display = 'flex';

  if (scan.is_duplicate) {
    fullCard.className = 'rfid-panel-fullcard theme-duplicate';
    badge.innerText = 'REPEATED TAP (IGNORED)';
    nameEl.innerText = scan.name;
    subEl.innerText = cleanMsg || `Already checked in at ${timeStr}. (Listed as 1 entry)`;
    metaEl.innerText = `UID: ${scan.uid} \u2022 ${timeStr}`;
    window.playAudioTone('duplicate');
  } else if (scan.type === 'Entrance' || scan.type === 'Exit') {
    fullCard.className = 'rfid-panel-fullcard theme-success';
    badge.innerText = `${scan.type.toUpperCase()} RECORDED`;
    nameEl.innerText = scan.name;
    subEl.innerText = `Recorded at ${timeStr} \u2022 ${scan.type}`;
    metaEl.innerText = `Card UID: ${scan.uid}`;
    window.playAudioTone('success');
    window.loadAttendanceLog();
  } else if (scan.type === 'Registration') {
    fullCard.className = 'rfid-panel-fullcard theme-success';
    badge.innerText = 'CARD ALLOCATED';
    nameEl.innerText = scan.name;
    subEl.innerText = scan.message || 'Card registered successfully.';
    metaEl.innerText = `UID: ${scan.uid} \u2022 ${timeStr}`;
    window.playAudioTone('success');
    window.loadCardsDatabase();
  } else if (scan.type === 'Rejected') {
    fullCard.className = 'rfid-panel-fullcard theme-error';
    badge.innerText = 'ACCESS DENIED';
    nameEl.innerText = 'Unregistered Tag';
    subEl.innerText = scan.message || 'This card is not registered in the database.';
    metaEl.innerText = `UID: ${scan.uid} \u2022 ${timeStr}`;
    window.playAudioTone('alert');
  } else {
    fullCard.className = 'rfid-panel-fullcard theme-idle';
    badge.innerText = 'NOTICE';
    nameEl.innerText = scan.name;
    subEl.innerText = scan.message;
    metaEl.innerText = `${timeStr}`;
    window.playAudioTone('alert');
  }

  // Trigger smooth 3.5s shrinking progress bar
  const bar = document.getElementById('resultTimeoutFill');
  if (bar) {
    bar.style.animation = 'none';
    bar.offsetHeight; /* trigger reflow */
    bar.style.animation = 'shrinkTimeout 1.5s linear forwards';
  }

  // Auto-disappear after 1.5 seconds and return to animated idle prompt!
  scanDisplayTimeout = setTimeout(() => {
    window.showIdleTapPrompt();
    scanDisplayTimeout = null;
  }, 1500);
};

window.handleOverlayClick = function(e, modalId) {
  if (e.target.id === modalId) {
    if (modalId === 'timeModal') closeTimeModal();
    else if (modalId === 'settingsModal') closeSettingsModal();
    else if (modalId === 'historyModal') closeHistoryModal();
    else if (modalId === 'firstBootModal') dismissFirstBootModal();
  }
};

document.addEventListener('keydown', (e) => {
  if (e.key === 'Escape') {
    closeTimeModal();
    closeSettingsModal();
    closeHistoryModal();
    dismissFirstBootModal();
  }
});

window.escapeHtml = function(str) {
  if (!str) return '';
  return str.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
};

window.addEventListener('DOMContentLoaded', () => {
  window.initTheme();
  window.pollStatus();
  window.loadAttendanceLog();
  setInterval(window.pollStatus, 250);

  const now = new Date();
  now.setMinutes(now.getMinutes() - now.getTimezoneOffset());
  document.getElementById('modalDateTimeInput').value = now.toISOString().slice(0, 16);
  document.getElementById('sessionTitleInput').value = 'Attendance ' + now.toISOString().slice(0, 10);
});
