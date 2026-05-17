// Server injects window.__MOONDECK__ via a <script> bootstrap in index.html
// before this file loads (keeps app.js a pure static asset — no templating).
const _MD = window.__MOONDECK__ || {};
const SCRIPTS = _MD.scripts || [];
const ESP32_ENVS = _MD.esp32Envs || [];
const RELEASES = _MD.releases || [];
const DEV_TASKS = _MD.devTasks || [];
const DOCS_BASE = 'http://127.0.0.1:8000/projectMM-v2';
const output = document.getElementById('output');
const panes = document.querySelectorAll('.pane');
const tabs = document.querySelectorAll('.tab');
const envSelect = document.getElementById('envSelect');
const portSelect = document.getElementById('portSelect');
const refreshBtn = document.getElementById('refreshPorts');
const elById = {};   // id -> { card, button, urlSpan, eventSource, spec }

// ── Tabs ──────────────────────────────────────────────────────────────────
function showTab(name) {
  for (const t of tabs)  t.classList.toggle('active', t.dataset.tab === name);
  for (const p of panes) p.classList.toggle('active', p.dataset.tab === name);
  localStorage.setItem('tab', name);
}
for (const t of tabs) t.addEventListener('click', () => showTab(t.dataset.tab));
showTab(localStorage.getItem('tab') || 'pc');

// ── Develop tab: release + sprint dropdowns → docs iframe ─────────────
const releaseSelect = document.getElementById('releaseSelect');
const sprintSelect  = document.getElementById('sprintSelect');
const openDocsBtn   = document.getElementById('openDocsBtn');

function populateReleases() {
  releaseSelect.innerHTML = '';
  for (const r of RELEASES) {
    const opt = document.createElement('option');
    opt.value = r.slug; opt.textContent = r.title;
    releaseSelect.appendChild(opt);
  }
  const stored = localStorage.getItem('release');
  const valid  = (s) => RELEASES.find(r => r.slug === s);
  // Default: last selected, else the latest release file (the highest-numbered
  // one once more land). Minimal: no need to track "current release" anywhere
  // else; sort order of the discovery is authoritative.
  releaseSelect.value =
    valid(stored) ? stored :
    RELEASES.length ? RELEASES[RELEASES.length - 1].slug : '';
}
function populateSprints() {
  const r = RELEASES.find(x => x.slug === releaseSelect.value);
  sprintSelect.innerHTML = '';
  if (!r) return;
  for (const s of r.sprints) {
    const opt = document.createElement('option');
    opt.value = s.id;
    // Label format: "Sprint 7 — Two-core + PSRAM scaling" (truncated if long)
    const title = s.title.length > 60 ? s.title.slice(0, 57) + '…' : s.title;
    opt.textContent = `${s.label} — ${title}`;
    sprintSelect.appendChild(opt);
  }
  // Per-release sprint memory: switching releases doesn't pin the same sprint
  // number, since "Sprint 8" means different things across releases.
  const stored = localStorage.getItem(`sprint-${r.slug}`);
  const valid  = (id) => r.sprints.find(s => s.id === id);
  sprintSelect.value =
    valid(stored) ? stored :
    r.sprints.length ? r.sprints[r.sprints.length - 1].id : '';
}
releaseSelect.addEventListener('change', () => {
  localStorage.setItem('release', releaseSelect.value);
  populateSprints();
});
sprintSelect.addEventListener('change', () => {
  if (releaseSelect.value) {
    localStorage.setItem(`sprint-${releaseSelect.value}`, sprintSelect.value);
  }
});
openDocsBtn.addEventListener('click', () => {
  const slug   = releaseSelect.value;
  const sprint = sprintSelect.value;
  if (!slug || !sprint) return;
  const r = RELEASES.find(x => x.slug === slug);
  const s = r && r.sprints.find(x => x.id === sprint);
  const label = `${r ? r.title.replace(/^Release \d+ — /, 'Rel') : slug} → ${s ? s.label : sprint}`;
  showDocsUrl(`${DOCS_BASE}/development/${slug}/#${sprint}`, label);
});

populateReleases();
populateSprints();

// ── Develop tab: dev-task cards (Run invokes the agent) ───────────────
const devTasksGroup = document.getElementById('devTasksGroup');
const devTaskBtnById = {};
for (const t of DEV_TASKS) {
  const c = document.createElement('div'); c.className = 'card';
  const dot = document.createElement('span'); dot.className = 'dot';
  const lbl = document.createElement('span'); lbl.className = 'label'; lbl.textContent = t.label;
  const help = document.createElement('a');
  help.className = 'help'; help.textContent = '?';
  help.href = `${DOCS_BASE}/developer-guide/deploy/#${t.docs_anchor}`;
  help.title = `Open docs in the right panel`;
  help.addEventListener('click', (ev) => {
    ev.preventDefault();
    showDocsUrl(`${DOCS_BASE}/developer-guide/deploy/#${t.docs_anchor}`, t.label);
  });
  const btn = document.createElement('button'); btn.textContent = 'Run';
  btn.addEventListener('click', () => runDevTask(t));
  c.append(dot, lbl, help, btn);
  devTasksGroup.appendChild(c);
  devTaskBtnById[t.id] = {card: c, button: btn};
}

async function runDevTask(t) {
  const e = devTaskBtnById[t.id];
  if (!e) return;
  showOutput();
  output.textContent = '';
  append(`<span class="meta">$ ${esc(t.id)}\n</span>`);
  append(`<span class="meta">--- Agent task: ${esc(t.label)} (streaming, may take minutes) ---</span>\n`);
  e.card.className = 'card running';
  e.button.disabled = true; e.button.textContent = 'Running…';
  try {
    const res = await callAgentStream('/agent-task', {task: t.id}, appendAgentLine);
    e.card.className = 'card ' + (res.exit_code === 0 ? 'ok' : 'fail');
  } catch (err) {
    append(`<span class="fail">Task failed: ${esc(String(err))}</span>\n`);
    e.card.className = 'card fail';
  } finally {
    e.button.disabled = false; e.button.textContent = 'Run';
  }
}

// ── ESP32 selectors (env + port) ──────────────────────────────────────────
for (const env of ESP32_ENVS) {
  const opt = document.createElement('option');
  opt.value = env; opt.textContent = env;
  envSelect.appendChild(opt);
}
envSelect.value = localStorage.getItem('env') || ESP32_ENVS[0];
envSelect.addEventListener('change', () => localStorage.setItem('env', envSelect.value));
function selectedEnv()  { return envSelect.value || ''; }
function selectedPort() { return portSelect.value || ''; }

function refreshPorts() {
  return fetch('/ports').then(r => r.json()).then(({ ports }) => {
    const prev = localStorage.getItem('port') || portSelect.value;
    portSelect.innerHTML = '';
    if (ports.length === 0) {
      const opt = document.createElement('option');
      opt.value = ''; opt.textContent = '(no USB serial devices)';
      portSelect.appendChild(opt);
    } else {
      for (const p of ports) {
        const opt = document.createElement('option');
        opt.value = p.path;
        opt.textContent = p.label ? `${p.path} — ${p.label}` : p.path;
        portSelect.appendChild(opt);
      }
      if (ports.map(p => p.path).includes(prev)) portSelect.value = prev;
    }
    updateGates();
  });
}
portSelect.addEventListener('change', () => { localStorage.setItem('port', portSelect.value); updateGates(); });
refreshBtn.addEventListener('click', refreshPorts);

function updateGates() {
  // Disable Run buttons on cards that need a port when none is selected. The
  // env selector is always populated from ESP32_ENVS, so it never gates.
  const hasPort = !!selectedPort();
  for (const e of Object.values(elById)) {
    if (!e.spec.needs_port) continue;
    if (e.button.classList.contains('stop')) continue;  // don't override a running long-runner
    e.button.disabled = !hasPort;
    e.button.title = hasPort ? '' : 'select a port first';
  }
}

// ── Render cards into their tab panes, grouped by `group` ────────────────
const groupsByTab = {};
for (const s of SCRIPTS) {
  const t = s.tab || 'pc';
  (groupsByTab[t] = groupsByTab[t] || {});
  (groupsByTab[t][s.group] = groupsByTab[t][s.group] || []).push(s);
}
for (const [tabName, groups] of Object.entries(groupsByTab)) {
  const pane = document.querySelector(`.pane[data-tab="${tabName}"]`);
  if (!pane) continue;
  for (const [name, items] of Object.entries(groups)) {
    const g = document.createElement('div');
    g.className = 'group';
    const h = document.createElement('h2'); h.textContent = name; g.appendChild(h);
    for (const s of items) {
      const c = document.createElement('div');
      c.className = 'card'; c.dataset.id = s.id;
      const dot = document.createElement('span'); dot.className = 'dot';
      const lbl = document.createElement('span'); lbl.className = 'label'; lbl.textContent = s.label;
      const urlSpan = document.createElement('span'); urlSpan.className = 'url';
      const help = document.createElement('a');
      help.className = 'help'; help.textContent = '?';
      help.href = `${DOCS_BASE}/developer-guide/deploy/#${s.id}`;
      help.title = `Open docs in the right panel (middle-click → new tab)`;
      // Left-click loads docs in the iframe; preserving the href means
      // middle-click and right-click → new tab still work for power users.
      help.addEventListener('click', (ev) => { ev.preventDefault(); showDocs(s); });
      const btn = document.createElement('button'); btn.textContent = s.long_running ? 'Start' : 'Run';
      btn.onclick = () => toggle(s);
      c.append(dot, lbl, urlSpan, help, btn);
      g.appendChild(c);
      elById[s.id] = { card: c, button: btn, urlSpan, eventSource: null, spec: s };
    }
    pane.appendChild(g);
  }
}

// On load, populate ports then reflect anything currently running on the server.
refreshPorts().then(() => fetch('/status')).then(r => r.json()).then(({ running }) => {
  for (const id of running) {
    const e = elById[id]; if (!e) continue;
    e.card.className = 'card running';
    if (e.spec.long_running) {
      setRunningUI(e);
      attachStream(e, /*alreadyRunning=*/true);
    }
  }
});

// ── Live: device list (persisted to moondeck.json) ────────────────────────
const deviceList     = document.getElementById('deviceList');
const refreshDevsBtn = document.getElementById('refreshDevicesBtn');
const scanBtn        = document.getElementById('scanSubnetBtn');
const scanSubnetLbl  = document.getElementById('scanSubnetLbl');
const addDevBtn      = document.getElementById('addDeviceBtn');
const newHostInput   = document.getElementById('newDeviceHost');
let uiState = null;

function persistUiState() {
  return fetch('/ui-state', {method: 'POST', headers: {'Content-Type': 'application/json'},
                              body: JSON.stringify(uiState)});
}

function renderDevices() {
  deviceList.innerHTML = '';
  scanSubnetLbl.textContent = `${uiState.scan_subnet}  port ${uiState.scan_port}`;
  if (uiState.devices.length === 0) {
    const empty = document.createElement('div');
    empty.className = 'device-row'; empty.style.color = '#888';
    empty.textContent = '(no devices — click Discover or Add)';
    deviceList.appendChild(empty);
    return;
  }
  uiState.devices.forEach((d, idx) => {
    const row = document.createElement('div'); row.className = 'device-row';
    const cb = document.createElement('input'); cb.type = 'checkbox'; cb.checked = !!d.enabled;
    cb.onchange = () => { d.enabled = cb.checked; persistUiState(); };
    // Caret: ▸ collapsed / ▾ expanded. Toggles inline metrics table below.
    const caret = document.createElement('span'); caret.className = 'caret';
    caret.textContent = d._expanded ? '▾' : '▸';
    caret.title = 'Show module metrics (GET /api/modules every 2s)';
    caret.onclick = () => toggleMetrics(d, caret);
    const name = document.createElement('span'); name.className = 'name'; name.textContent = d.name || '(unnamed)';
    name.style.cursor = 'pointer';
    name.title = `Click to open ${d.host}:${d.port}/ in the right panel` + (d.chip || d.env ? `\n${[d.chip, d.env].filter(Boolean).join(' / ')}` : '');
    name.onclick = () => showDevice(d);
    const host = document.createElement('span'); host.className = 'host'; host.textContent = `${d.host}:${d.port}`;
    const status = document.createElement('span'); status.className = 'status';
    status.textContent = d.last_status || '';
    if (d.last_status === 'reachable') status.classList.add('ok');
    else if (d.last_status === 'unreachable') status.classList.add('fail');
    const setup = document.createElement('button');
    setup.className = 'rm'; setup.textContent = '+light';
    setup.title = 'Create the reference light setup (layout → ripples → preview/artnet) on this device';
    setup.onclick = () => createLightSetup(d, setup);
    const rm = document.createElement('button'); rm.className = 'rm'; rm.textContent = '×'; rm.title = 'Remove';
    rm.onclick = () => {
      stopMetricsPoll(d);
      uiState.devices.splice(idx, 1);
      persistUiState().then(renderDevices);
    };
    row.append(caret, cb, name, host, status, setup, rm);
    deviceList.appendChild(row);
    // If this device was expanded before re-render (e.g. checkbox toggled
    // somewhere else), recreate its metrics panel and resume polling.
    if (d._expanded) mountMetricsPanel(d);
  });
}

// Inline-metrics state lives on the device object as `_expanded` (bool),
// `_metricsEl` (the DOM panel), and `_metricsHandle` (the setInterval id).
// Kept off uiState.devices' persisted shape — these are runtime-only.
function toggleMetrics(d, caret) {
  if (d._expanded) {
    stopMetricsPoll(d);
    d._expanded = false;
    caret.textContent = '▸';
  } else {
    d._expanded = true;
    caret.textContent = '▾';
    mountMetricsPanel(d);
  }
}

function stopMetricsPoll(d) {
  if (d._metricsHandle) { clearInterval(d._metricsHandle); d._metricsHandle = null; }
  if (d._metricsEl) { d._metricsEl.remove(); d._metricsEl = null; }
}

function mountMetricsPanel(d) {
  // Locate the parent row so the panel inserts directly under it.
  const rows = Array.from(deviceList.querySelectorAll('.device-row'));
  const row = rows.find(r => r.querySelector('.host')?.textContent === `${d.host}:${d.port}`);
  if (!row) return;
  const panel = document.createElement('div'); panel.className = 'device-metrics';
  panel.innerHTML = '<span class="meta">loading…</span>';
  row.after(panel);
  d._metricsEl = panel;
  refreshMetrics(d);
  d._metricsHandle = setInterval(() => refreshMetrics(d), 2000);
}

async function refreshMetrics(d) {
  if (!d._metricsEl) return;
  try {
    const r = await fetch('/device-modules', {method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({host: d.host, port: d.port})});
    const j = await r.json();
    const mods = j.modules || [];
    if (!d._metricsEl) return;  // user may have collapsed between request + reply
    if (mods.length === 0) {
      d._metricsEl.innerHTML = '<span class="err">no response from /api/modules</span>';
      return;
    }
    const fmtKB = b => (b/1024).toFixed(b < 1024 ? 2 : 1);
    let html = '<table><thead><tr><th>module</th><th>type</th><th class="num">core</th><th class="num">ms/tick</th><th class="num">heap</th><th class="num">psram</th><th class="num">class</th></tr></thead><tbody>';
    for (const m of mods) {
      const ms = (m.ms_per_tick ?? 0).toFixed(2);
      html += `<tr><td>${esc(m.id || '')}</td><td>${esc(m.type || '')}</td>`
            + `<td class="num">${m.core ?? 0}</td>`
            + `<td class="num">${ms}</td>`
            + `<td class="num">${fmtKB(m.heap_size_bytes ?? 0)} KB</td>`
            + `<td class="num">${fmtKB(m.psram_size_bytes ?? 0)} KB</td>`
            + `<td class="num">${m.class_size_bytes ?? 0} B</td></tr>`;
    }
    html += '</tbody></table>';
    html += `<div class="meta">${mods.length} module(s) · refresh 2s · ${new Date().toLocaleTimeString()}</div>`;
    d._metricsEl.innerHTML = html;
  } catch (e) {
    if (d._metricsEl) d._metricsEl.innerHTML = `<span class="err">fetch failed: ${esc(String(e))}</span>`;
  }
}

async function createLightSetup(d, btn) {
  const prev = btn.textContent;
  btn.disabled = true; btn.textContent = '…';
  try {
    const r = await fetch('/device-setup', {method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({host: d.host, port: d.port})});
    const j = await r.json();
    const summary = (j.log || []).join('\n');
    if (j.ok) {
      btn.textContent = '✓';
      // If the metrics panel is open, refresh it so the new tree shows.
      if (d._expanded) refreshMetrics(d);
    } else {
      btn.textContent = '✗';
      alert('Light setup failed on ' + d.host + ':\n\n' + summary);
    }
    console.log('[light-setup ' + d.host + ']\n' + summary);
  } catch (e) {
    btn.textContent = '✗';
    alert('Light setup request failed: ' + e);
  } finally {
    setTimeout(() => { btn.disabled = false; btn.textContent = prev; }, 1500);
  }
}

async function probeOne(d) {
  const r = await fetch('/probe', {method: 'POST', headers: {'Content-Type': 'application/json'},
                                    body: JSON.stringify({host: d.host, port: d.port})});
  const j = await r.json();
  d.last_status = j.reachable ? 'reachable' : 'unreachable';
  if (j.reachable && j.info) {
    d.chip = j.info.chip_model || d.chip;
    d.mac  = j.info.mac_address || d.mac;
    d.env  = j.info.env || d.env;
    if (j.info.mac_address && (!d.name || /^MM-/.test(d.name))) {
      const tail = j.info.mac_address.replace(/:/g, '').slice(-4).toUpperCase();
      d.name = `MM-${tail}`;
    }
  }
}

async function refreshAllDevices() {
  refreshDevsBtn.disabled = true; refreshDevsBtn.textContent = 'Probing…';
  await Promise.all(uiState.devices.map(probeOne));
  await persistUiState();
  renderDevices();
  refreshDevsBtn.disabled = false; refreshDevsBtn.textContent = 'Refresh';
}

async function runScan() {
  scanBtn.disabled = true; scanBtn.textContent = 'Scanning…';
  try {
    const r = await fetch('/scan', {method: 'POST', headers: {'Content-Type': 'application/json'},
                                    body: JSON.stringify({subnet: uiState.scan_subnet, port: uiState.scan_port})});
    const { devices } = await r.json();
    // Merge: existing entries keep their `enabled` toggle; scan-found
    // entries are added or have their chip/mac/env refreshed.
    const byHost = new Map(uiState.devices.map(d => [d.host + ':' + d.port, d]));
    for (const found of devices) {
      const key = found.host + ':' + found.port;
      const existing = byHost.get(key);
      if (existing) {
        Object.assign(existing, {chip: found.chip, mac: found.mac, env: found.env,
                                 last_status: 'reachable', name: existing.name || found.name});
      } else {
        uiState.devices.push({...found, last_status: 'reachable'});
      }
    }
    await persistUiState();
    renderDevices();
  } finally {
    scanBtn.disabled = false; scanBtn.textContent = 'Discover';
  }
}

function addDeviceFromInput() {
  const raw = (newHostInput.value || '').trim();
  if (!raw) return;
  // host[:port] — default 80
  const m = raw.match(/^([^\s:]+)(?::(\d+))?$/);
  if (!m) { alert('Use the form host or host:port (e.g. 192.168.1.156:80)'); return; }
  const host = m[1];
  const port = m[2] ? parseInt(m[2], 10) : 80;
  if (uiState.devices.some(d => d.host === host && d.port === port)) {
    alert('Already in the list.'); return;
  }
  uiState.devices.push({name: host, host, port, enabled: true, discovered: 'manual'});
  newHostInput.value = '';
  persistUiState().then(renderDevices);
}

refreshDevsBtn.addEventListener('click', refreshAllDevices);
scanBtn.addEventListener('click', runScan);
addDevBtn.addEventListener('click', addDeviceFromInput);
newHostInput.addEventListener('keydown', ev => { if (ev.key === 'Enter') addDeviceFromInput(); });

fetch('/ui-state').then(r => r.json()).then(s => { uiState = s; renderDevices(); });

function append(html) { output.insertAdjacentHTML('beforeend', html); output.scrollTop = output.scrollHeight; }
function esc(s) { return s.replace(/[&<>]/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;'})[c]); }

// ── Right-pane view toggle: Output ↔ Device iframe ────────────────────────
const deviceFrame = document.getElementById('deviceFrame');
const viewMode    = document.getElementById('viewMode');
const viewUrl     = document.getElementById('viewUrl');
const viewBack    = document.getElementById('viewBack');
const agentBar    = document.getElementById('agentBar');
const analyzeBtn  = document.getElementById('analyzeBtn');
const fixBtn      = document.getElementById('fixBtn');
const askInput    = document.getElementById('askInput');
const askBtn      = document.getElementById('askBtn');
let   lastAnalysis = '';

function showOutput() {
  output.classList.remove('hidden');
  agentBar.classList.remove('hidden');
  deviceFrame.classList.add('hidden');
  viewBack.classList.add('hidden');
  viewMode.textContent = 'Output';
  viewUrl.innerHTML = '';
}
function showDevice(d) {
  const url = `http://${d.host}:${d.port}/`;
  // Only reload if the URL actually changed — avoids re-mount + WS reconnect
  // when the user clicks the same device twice.
  if (deviceFrame.src !== url) deviceFrame.src = url;
  output.classList.add('hidden');
  agentBar.classList.add('hidden');
  deviceFrame.classList.remove('hidden');
  viewBack.classList.remove('hidden');
  viewMode.textContent = `Device — ${d.name || (d.host + ':' + d.port)}`;
  viewUrl.innerHTML = `<a href="${url}" target="_blank" rel="noopener">open in new tab ↗</a>`;
}
function showDocsUrl(url, label) {
  // Generic docs viewer. Requires the MkDocs serve card to be running
  // (port 8000); if it's not, the iframe shows a connection-refused page
  // and the hint is self-evident.
  if (deviceFrame.src !== url) deviceFrame.src = url;
  output.classList.add('hidden');
  agentBar.classList.add('hidden');
  deviceFrame.classList.remove('hidden');
  viewBack.classList.remove('hidden');
  viewMode.textContent = `Docs — ${label}`;
  viewUrl.innerHTML = `<a href="${url}" target="_blank" rel="noopener">open in new tab ↗</a>`;
}
function showDocs(s) {
  // Card `?` click adapter.
  showDocsUrl(`${DOCS_BASE}/developer-guide/deploy/#${s.id}`, s.label);
}
viewBack.addEventListener('click', showOutput);

// ── Agent loop: Analyze / Fix / Ask — all stream via SSE so the user sees
//    the agent's narration + tool calls live (same UX as a terminal run).
async function callAgentStream(endpoint, payload, onLine) {
  const r = await fetch(endpoint, {method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify(payload)});
  if (!r.ok) {
    const text = await r.text();
    throw new Error(`${r.status}: ${text}`);
  }
  // Parse SSE off the response body manually (EventSource is GET-only).
  const reader = r.body.getReader();
  const decoder = new TextDecoder();
  let buffer = '';
  let full = '';
  let exitCode = 0;
  while (true) {
    const {value, done} = await reader.read();
    if (done) break;
    buffer += decoder.decode(value, {stream: true});
    let idx;
    while ((idx = buffer.indexOf('\n\n')) !== -1) {
      const block = buffer.slice(0, idx);
      buffer = buffer.slice(idx + 2);
      let ev = '', data = '';
      for (const ln of block.split('\n')) {
        if      (ln.startsWith('event: ')) ev = ln.slice(7);
        else if (ln.startsWith('data: '))  data += (data ? '\n' : '') + ln.slice(6);
      }
      if (ev === 'line') {
        onLine(data);
        full += data + '\n';
      } else if (ev === 'done') {
        exitCode = parseInt(data) || 0;
      }
    }
  }
  return {output: full, exit_code: exitCode};
}
function appendAgentLine(data) { append(esc(data) + '\n'); }

analyzeBtn.addEventListener('click', async () => {
  const log = output.innerText.trim();
  if (!log) { alert('Nothing in the log yet — run a script first.'); return; }
  analyzeBtn.disabled = true; analyzeBtn.textContent = 'Analyzing…';
  fixBtn.classList.add('hidden');
  append(`\n<span class="meta">--- Agent analysis ---</span>\n`);
  try {
    const res = await callAgentStream('/analyze', {log}, appendAgentLine);
    lastAnalysis = res.output || '';
    const firstLine = (lastAnalysis.split('\n', 1)[0] || '').trim();
    if (firstLine.startsWith('ISSUE')) fixBtn.classList.remove('hidden');
  } catch (e) {
    append(`<span class="fail">Analyze failed: ${esc(String(e))}</span>\n`);
  } finally {
    analyzeBtn.disabled = false; analyzeBtn.textContent = 'Analyze (by agent)';
  }
});
fixBtn.addEventListener('click', async () => {
  if (!lastAnalysis) { alert('Run Analyze first.'); return; }
  if (!confirm('The agent will edit files in this repo. Continue?')) return;
  const log = output.innerText.trim();
  fixBtn.disabled = true; fixBtn.textContent = 'Fixing…';
  append(`\n<span class="meta">--- Agent fix ---</span>\n`);
  try {
    await callAgentStream('/fix', {log, analysis: lastAnalysis}, appendAgentLine);
  } catch (e) {
    append(`<span class="fail">Fix failed: ${esc(String(e))}</span>\n`);
  } finally {
    fixBtn.disabled = false; fixBtn.textContent = 'Fix';
  }
});

async function sendAsk() {
  const prompt = askInput.value.trim();
  if (!prompt) { askInput.focus(); return; }
  const log = output.innerText.trim();
  askBtn.disabled = true; askInput.disabled = true; askBtn.textContent = 'Asking…';
  append(`\n<span class="meta">--- Ask: ${esc(prompt)} ---</span>\n`);
  try {
    await callAgentStream('/ask', {log, prompt}, appendAgentLine);
    askInput.value = '';   // clear on success; let user see the question in the log
  } catch (e) {
    append(`<span class="fail">Ask failed: ${esc(String(e))}</span>\n`);
  } finally {
    askBtn.disabled = false; askInput.disabled = false; askBtn.textContent = 'Ask';
    askInput.focus();
  }
}
askBtn.addEventListener('click', sendAsk);
askInput.addEventListener('keydown', (ev) => { if (ev.key === 'Enter' && !ev.shiftKey) { ev.preventDefault(); sendAsk(); } });

function setRunningUI(e) {
  e.button.textContent = 'Stop';
  e.button.classList.add('stop');
  e.button.disabled = false;
  if (e.spec.url) e.urlSpan.innerHTML = `<a href="${e.spec.url}" target="_blank">open ↗</a>`;
}
function setIdleUI(e, code) {
  e.button.textContent = e.spec.long_running ? 'Start' : 'Run';
  e.button.classList.remove('stop');
  e.button.disabled = false;
  e.urlSpan.innerHTML = '';
  if (code !== undefined) e.card.className = 'card ' + (code === 0 ? 'ok' : 'fail');
}

function toggle(s) {
  const e = elById[s.id];
  if (e.button.classList.contains('stop')) {
    fetch(`/stop/${encodeURIComponent(s.id)}`, { method: 'POST' });
    if (!e.eventSource) {
      // Stopping a long-runner we are no longer streaming (another script took over the panel).
      setIdleUI(e);
      e.card.className = 'card';
    }
    // Otherwise the SSE `done` event clears the UI when the kill lands.
    return;
  }
  // Close any other open client streams so the output panel shows just this run.
  // Server processes keep running; reload to re-attach to a long-running one.
  Object.values(elById).forEach(other => {
    if (other !== e && other.eventSource) { other.eventSource.close(); other.eventSource = null; }
  });
  // Disable other Run buttons only for short-running scripts (they share the output panel).
  // Long-running scripts (mkdocs serve, monitor) run in the background; leave the UI usable.
  if (!s.long_running) {
    document.querySelectorAll('button.tab').forEach(b => { /* never disable tabs */ });
    for (const e2 of Object.values(elById)) {
      if (!e2.button.classList.contains('stop')) e2.button.disabled = true;
    }
  }
  e.button.disabled = true;
  output.textContent = '';
  showOutput();  // swap back from a device iframe view, if currently shown
  append(`<span class="meta">$ ${esc(s.id)}\n</span>`);
  e.card.className = 'card running';
  if (s.long_running) setRunningUI(e);
  attachStream(e, /*alreadyRunning=*/false);
}

function attachStream(e, alreadyRunning) {
  const qs = new URLSearchParams();
  if (alreadyRunning)                       qs.set('attach', '1');
  if (e.spec.needs_env  && selectedEnv())   qs.set('env',  selectedEnv());
  if (e.spec.needs_port && selectedPort())  qs.set('port', selectedPort());
  const url = `/run/${encodeURIComponent(e.spec.id)}${qs.toString() ? '?' + qs : ''}`;
  const es = new EventSource(url);
  e.eventSource = es;
  es.addEventListener('line', ev => append(esc(ev.data) + '\n'));
  es.addEventListener('step', ev => append(`<span class="meta">--- ${esc(ev.data)} ---\n</span>`));
  es.addEventListener('done', ev => {
    const code = parseInt(ev.data);
    append(`<span class="${code === 0 ? 'ok' : 'fail'}">exit ${code}\n</span>`);
    setIdleUI(e, code);
    for (const e2 of Object.values(elById)) {
      if (!e2.button.classList.contains('stop')) e2.button.disabled = false;
    }
    updateGates();
    es.close(); e.eventSource = null;
  });
  es.onerror = () => {
    if (e.card.classList.contains('running') && !e.spec.long_running) e.card.className = 'card fail';
    setIdleUI(e);
    for (const e2 of Object.values(elById)) {
      if (!e2.button.classList.contains('stop')) e2.button.disabled = false;
    }
    updateGates();
    es.close(); e.eventSource = null;
  };
}
