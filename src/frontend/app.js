/* ============================================================
   3D WebGL LED point-cloud previewer
   Renders a voxel grid received as binary frame 0x02:
     [0x02][w lo][w hi][h lo][h hi][d lo][d hi][R G B ...]
   Pixel layout: pixels[z * w * h + y * w + x]
   Camera: mouse-drag to orbit, wheel to zoom.
   ============================================================ */
let gl3 = null, gl3prog = null, gl3buf = null;
let gl3posLoc, gl3colLoc, gl3mvpLoc, gl3ptLoc;
let camTheta = 0.5, camPhi = 0.4, camDist = 2.2;
let lastFrame = null;   // cached vertex array for mouse-drag redraws

/* ---- Matrix helpers (column-major, WebGL convention) ---- */
function m4mul(a, b) {
    const r = new Float32Array(16);
    for (let c = 0; c < 4; c++)
        for (let row = 0; row < 4; row++) {
            let s = 0;
            for (let k = 0; k < 4; k++) s += a[k*4+row] * b[c*4+k];
            r[c*4+row] = s;
        }
    return r;
}
function m4persp(fov, asp, n, f) {
    const t = 1 / Math.tan(fov * 0.5);
    return new Float32Array([
        t/asp, 0, 0,               0,
        0,     t, 0,               0,
        0,     0, (f+n)/(n-f),    -1,
        0,     0, 2*f*n/(n-f),     0
    ]);
}
function m4lookat(ex,ey,ez, cx,cy,cz) {
    let fx=cx-ex, fy=cy-ey, fz=cz-ez;
    let fl=Math.hypot(fx,fy,fz); fx/=fl; fy/=fl; fz/=fl;
    let rx=fy*0-fz*1, ry=fz*0-fx*0, rz=fx*1-fy*0;  // right = forward × (0,1,0) approx
    // proper: right = forward × world-up
    rx=fy*1-fz*0; ry=fz*0-fx*1; rz=fx*0-fy*0;
    let rl=Math.hypot(rx,ry,rz); rx/=rl; ry/=rl; rz/=rl;
    let ux=ry*fz-rz*fy, uy=rz*fx-rx*fz, uz=rx*fy-ry*fx;
    return new Float32Array([
        rx, ux, -fx, 0,
        ry, uy, -fy, 0,
        rz, uz, -fz, 0,
        -(rx*ex+ry*ey+rz*ez), -(ux*ex+uy*ey+uz*ez), fx*ex+fy*ey+fz*ez, 1
    ]);
}
function mvp(canvas) {
    const ex = camDist * Math.sin(camTheta) * Math.cos(camPhi);
    const ey = camDist * Math.sin(camPhi);
    const ez = camDist * Math.cos(camTheta) * Math.cos(camPhi);
    const v  = m4lookat(ex,ey,ez, 0,0,0);
    const p  = m4persp(Math.PI/3, canvas.width/canvas.height, 0.1, 50);
    return m4mul(p, v);
}

/* ---- GL init ---- */
function initGL() {
    const canvas = document.getElementById('preview');
    // Size canvas to its CSS display size (square, max 50vh).
    const size = canvas.clientWidth || 320;
    canvas.width = canvas.height = size;

    const g = canvas.getContext('webgl');
    if (!g) { console.warn('WebGL not available'); return; }
    gl3 = g;

    const vs = `
        attribute vec3 aPos;
        attribute vec3 aCol;
        uniform mat4 uMVP;
        uniform float uPtSize;
        varying vec3 vCol;
        void main(){
            vCol = aCol;
            gl_Position = uMVP * vec4(aPos, 1.0);
            gl_PointSize = uPtSize / gl_Position.w;
        }`;
    const fs = `
        precision mediump float;
        varying vec3 vCol;
        void main(){
            vec2 c = gl_PointCoord - 0.5;
            float r = dot(c,c);
            if(r > 0.25) discard;
            float br = 1.0 - smoothstep(0.10, 0.25, r);
            gl_FragColor = vec4(vCol * br, 1.0);
        }`;

    function mksh(type, src) {
        const sh = g.createShader(type);
        g.shaderSource(sh, src); g.compileShader(sh);
        if (!g.getShaderParameter(sh, g.COMPILE_STATUS))
            console.error('shader:', g.getShaderInfoLog(sh));
        return sh;
    }
    gl3prog = g.createProgram();
    g.attachShader(gl3prog, mksh(g.VERTEX_SHADER, vs));
    g.attachShader(gl3prog, mksh(g.FRAGMENT_SHADER, fs));
    g.linkProgram(gl3prog); g.useProgram(gl3prog);

    gl3buf    = g.createBuffer();
    gl3mvpLoc = g.getUniformLocation(gl3prog, 'uMVP');
    gl3ptLoc  = g.getUniformLocation(gl3prog, 'uPtSize');
    gl3posLoc = g.getAttribLocation(gl3prog, 'aPos');
    gl3colLoc = g.getAttribLocation(gl3prog, 'aCol');
    g.enableVertexAttribArray(gl3posLoc);
    g.enableVertexAttribArray(gl3colLoc);

    g.clearColor(0.06, 0.06, 0.14, 1);
    g.enable(g.DEPTH_TEST);
    g.clear(g.COLOR_BUFFER_BIT | g.DEPTH_BUFFER_BIT);

    /* ---- Orbit camera mouse controls ---- */
    let drag = false, lx = 0, ly = 0;
    canvas.addEventListener('mousedown', e => { drag=true; lx=e.clientX; ly=e.clientY; });
    window.addEventListener('mouseup',   ()  => { drag=false; });
    window.addEventListener('mousemove', e  => {
        if (!drag) return;
        camTheta -= (e.clientX-lx) * 0.012;
        camPhi   += (e.clientY-ly) * 0.012;
        camPhi = Math.max(-1.4, Math.min(1.4, camPhi));
        lx=e.clientX; ly=e.clientY;
        redraw3d();
    });
    canvas.addEventListener('wheel', e => {
        e.preventDefault();
        camDist *= 1 + e.deltaY * 0.001;
        camDist = Math.max(0.5, Math.min(15, camDist));
        redraw3d();
    }, {passive: false});

    /* ---- Touch orbit ---- */
    let t0 = null;
    canvas.addEventListener('touchstart', e => { t0 = e.touches[0]; }, {passive:true});
    canvas.addEventListener('touchmove',  e => {
        if (!t0) return;
        const t1 = e.touches[0];
        camTheta -= (t1.clientX - t0.clientX) * 0.012;
        camPhi   += (t1.clientY - t0.clientY) * 0.012;
        camPhi = Math.max(-1.4, Math.min(1.4, camPhi));
        t0 = t1;
        redraw3d();
    }, {passive:true});
}

function redraw3d() {
    if (!gl3 || !lastFrame) return;
    const g = gl3, canvas = g.canvas;
    g.viewport(0, 0, canvas.width, canvas.height);
    g.clear(g.COLOR_BUFFER_BIT | g.DEPTH_BUFFER_BIT);
    if (lastFrame.count === 0) return;

    g.uniformMatrix4fv(gl3mvpLoc, false, mvp(canvas));
    g.uniform1f(gl3ptLoc, lastFrame.ptSize);

    g.bindBuffer(g.ARRAY_BUFFER, gl3buf);
    const stride = 24;  // 6 floats × 4 bytes
    g.vertexAttribPointer(gl3posLoc, 3, g.FLOAT, false, stride, 0);
    g.vertexAttribPointer(gl3colLoc, 3, g.FLOAT, false, stride, 12);
    g.drawArrays(g.POINTS, 0, lastFrame.count);
}

function renderPixelFrame(buf) {
    if (!gl3) initGL();
    if (!gl3) return;
    const view = new DataView(buf);
    if (view.byteLength < 7 || view.getUint8(0) !== 0x02) return;
    const w = view.getUint16(1, true);
    const h = view.getUint16(3, true);
    const d = view.getUint16(5, true);
    const pixels = new Uint8Array(buf, 7);
    if (pixels.length !== w * h * d * 3) return;

    // Build interleaved vertex data [x,y,z,r,g,b] for lit LEDs only.
    // Normalise LED positions to [-0.5, 0.5] on each axis.
    const tmp = new Float32Array(w * h * d * 6);
    let n = 0;
    for (let z = 0; z < d; z++) {
        for (let y = 0; y < h; y++) {
            for (let x = 0; x < w; x++) {
                const pi = (z * h * w + y * w + x) * 3;
                const r = pixels[pi], g = pixels[pi+1], b = pixels[pi+2];
                if (r + g + b === 0) continue;
                tmp[n++] = (x + 0.5) / w - 0.5;
                tmp[n++] = (y + 0.5) / h - 0.5;
                tmp[n++] = (z + 0.5) / (d > 1 ? d : 1) - (d > 1 ? 0.5 : 0);
                tmp[n++] = r / 255;
                tmp[n++] = g / 255;
                tmp[n++] = b / 255;
            }
        }
    }
    const count = n / 6;
    const data  = tmp.subarray(0, n);

    // Point size: target ~10 px per LED for a 320-px canvas.
    const maxDim  = Math.max(w, h, d);
    const ptSize  = Math.max(2, (gl3.canvas.width * 0.55) / maxDim);

    gl3.bindBuffer(gl3.ARRAY_BUFFER, gl3buf);
    gl3.bufferData(gl3.ARRAY_BUFFER, data, gl3.DYNAMIC_DRAW);
    lastFrame = {count, ptSize};
    redraw3d();
}

/* ============================================================
   WebSocket
   ============================================================ */
let wsConn = null;
let wsRetryDelay = 500;
let wsRetryTimer = null;
let wsPaused     = false;
let wsHeartbeat  = null;

let otaInProgress = false;
let otaActiveMsgEl = null;

function connectWs() {
    if (wsRetryTimer) { clearTimeout(wsRetryTimer); wsRetryTimer = null; }
    if (wsConn) {
        wsConn.onopen = wsConn.onclose = wsConn.onerror = wsConn.onmessage = null;
        if (wsConn.readyState !== WebSocket.CLOSED) wsConn.close();
        wsConn = null;
    }
    const url = 'ws://' + location.hostname + ':81';
    wsConn = new WebSocket(url);
    wsConn.binaryType = 'arraybuffer';

    wsConn.onopen = () => {
        setWsDot(true);
        wsRetryDelay = 500;
        if (otaInProgress && otaActiveMsgEl) {
            otaActiveMsgEl.textContent = 'Reconnected! Update complete — device rebooted.';
            otaInProgress = false;
            otaActiveMsgEl = null;
        }
        // Heartbeat: send a ping every 25 s so Safari doesn't kill idle connections.
        clearInterval(wsHeartbeat);
        wsHeartbeat = setInterval(() => {
            if (wsConn && wsConn.readyState === WebSocket.OPEN) wsConn.send('ping');
        }, 25000);
        // Backfill log history from the ring buffer for late-connecting clients.
        fetch('/api/log').then(r => r.json()).then(data => {
            if (data && Array.isArray(data.entries))
                data.entries.forEach(e => appendLogLine(e));
        }).catch(() => {});
    };
    wsConn.onclose = () => {
        clearInterval(wsHeartbeat);
        setWsDot(false);
        if (otaInProgress && otaActiveMsgEl) {
            otaActiveMsgEl.textContent = 'Flash complete — device rebooting. Reconnecting...';
        }
        const delay = wsRetryDelay;
        wsRetryDelay = Math.min(wsRetryDelay * 2, 5000);
        wsRetryTimer = setTimeout(connectWs, delay);
    };
    wsConn.onerror = () => { wsConn.close(); };
    wsConn.onmessage = (evt) => {
        if (wsPaused) return;
        if (evt.data instanceof ArrayBuffer) {
            renderPixelFrame(evt.data);
        } else {
            try {
                const msg = JSON.parse(evt.data);
                if (msg && msg.t === 'log') {
                    appendLogLine(msg.m || '');
                } else if (msg && msg.t === 'schema') {
                    const modules = msg.modules || [];
                    // Only do a full DOM rebuild when the module list itself
                    // changed (adds/removes/reorder). A schemaDirty from a
                    // geometry control change has the same id set — patch
                    // controls in-place instead so the user's active slider
                    // is not torn down mid-drag.
                    if (schemaStructureChanged_(modules)) {
                        console.log('[WS] schema rebuild:', modules.length, 'modules');
                        render(modules);
                    } else {
                        patchControlSchema_(modules);
                    }
                } else {
                    handleStateUpdate(msg);
                }
            } catch (e) { console.error('[WS] parse error:', e, evt.data.slice(0, 120)); }
        }
    };
}

let logLineCount = 0;
let logAtBottom = true;
const LOG_MAX_LINES = 100;

function _logClass(text) {
    const t = text.toLowerCase();
    if (t.includes('error') || t.includes('fail')) return 'log-error';
    if (t.includes('warn'))                          return 'log-warn';
    return '';
}

function appendLogLine(text) {
    const out = document.getElementById('log-output');
    if (!out) return;
    const div = document.createElement('div');
    const extra = _logClass(text);
    div.className = extra ? `log-line ${extra}` : 'log-line';
    div.textContent = text;
    out.appendChild(div);
    // Trim oldest lines when over limit.
    while (out.childElementCount > LOG_MAX_LINES) out.removeChild(out.firstChild);
    if (logAtBottom) out.scrollTop = out.scrollHeight;
    logLineCount = out.childElementCount;
    const cnt = document.getElementById('log-count');
    if (cnt) cnt.textContent = String(logLineCount);
}


function setWsDot(connected) {
    const dot = document.getElementById('ws-dot');
    dot.className = 'ws-dot ' + (connected ? 'connected' : 'disconnected');
}

function handleStateUpdate(state) {
    if (wsPaused) return;
    for (const entry of state) {
        // Extract device name from any module that carries device_name
        if (entry.controls && entry.controls.device_name) {
            const name = entry.controls.device_name;
            const el = document.getElementById('device-name');
            if (el) el.textContent = name;
            document.title = name;
        }

        if (entry.timing) {
            timingCache[entry.id] = entry.timing;
            displayTiming(entry.id, timingCache[entry.id]);
        }
        if (entry.controls) {
            for (const [key, value] of Object.entries(entry.controls)) {
                const mid = esc(entry.id), k = esc(key);
                const input = document.querySelector(
                    'input[data-mid="' + mid + '"][data-key="' + k + '"]');
                if (input) {
                    const lastDrag = dragTs[entry.id + ':' + key] || 0;
                    // PATCH: drag-guard — client-side workaround because the push
                    // protocol has no "client owns this control" signal. Remove when
                    // the WS protocol gets a client-lock or optimistic-update frame.
                    const editing = document.activeElement === input;
                    if (!editing && Date.now() - lastDrag > 2000) {
                        if (input.dataset.toggle) {
                            input.checked = !!value;
                        } else {
                            input.value = value;
                            const disp = input.nextElementSibling;
                            if (disp && input.type !== 'text' && input.type !== 'password')
                                disp.textContent = input.dataset.integer
                                    ? String(Math.round(Number(value)))
                                    : fmt(value);
                        }
                    }
                }
                const disp = document.querySelector(
                    'span[data-disp][data-mid="' + mid + '"][data-key="' + k + '"]');
                if (disp) disp.textContent = fmtDisplay(value, disp.dataset.type);

                const prog = document.querySelector(
                    'progress[data-mid="' + mid + '"][data-key="' + k + '"]');
                if (prog) {
                    prog.value = Number(value);
                    const ptxt = prog.nextElementSibling;
                    if (ptxt) ptxt.textContent = fmtProgress(Number(value), Number(prog.max), prog.dataset.integer);
                }

                const sel = document.querySelector(
                    'select.select-input[data-mid="' + mid + '"][data-key="' + k + '"]');
                if (sel) {
                    const lastDrag = dragTs[entry.id + ':' + key] || 0;
                    if (Date.now() - lastDrag > 2000) sel.value = value;
                }

                const resetBtn = document.querySelector(
                    'button.reset-btn[data-mid="' + mid + '"][data-key="' + k + '"]');
                if (resetBtn) {
                    const def = parseFloat(resetBtn.dataset.defVal);
                    const atDefault = !isNaN(def) && Math.abs(Number(value) - def) < 0.001;
                    resetBtn.classList.toggle('active', !atDefault);
                }
            }
        }
    }
}

const dragTs = {};

// Last known module id list for structural-change detection.
let lastSchemaIds_ = [];

// Returns true if the schema event represents a structural change (new/removed/
// reordered modules). False means only control values or ranges changed.
function schemaStructureChanged_(modules) {
    // Fingerprint includes id + parent_id so reparent triggers a full rebuild.
    const ids = modules.map(m => m.id + '|' + (m.parent_id || ''));
    if (ids.length !== lastSchemaIds_.length) { lastSchemaIds_ = ids; return true; }
    for (let i = 0; i < ids.length; i++) {
        if (ids[i] !== lastSchemaIds_[i]) { lastSchemaIds_ = ids; return true; }
    }
    return false;
}

// PATCH: schema-diff — frontend diffs incoming schema to avoid full DOM rebuild
// at 1 Hz (rebuild resets focus, flickers cards, interrupts canvas). Remove when
// the backend sends separate schema-structure vs schema-values event types.
function patchControlSchema_(modules) {
    for (const mod of modules) {
        for (const ctrl of (mod.controls || [])) {
            const mid = esc(mod.id), k = esc(ctrl.key);
            const input = document.querySelector(
                'input[data-mid="' + mid + '"][data-key="' + k + '"]');
            if (input && ctrl.type === 'slider') {
                input.min  = ctrl.min;
                input.max  = ctrl.max;
                // Only update value if user is not actively interacting.
                const lastDrag = dragTs[mod.id + ':' + ctrl.key] || 0;
                if (document.activeElement !== input && Date.now() - lastDrag > 2000) {
                    input.value = ctrl.value;
                    const disp = input.nextElementSibling;
                    if (disp) disp.textContent = ctrl.integer
                        ? String(Math.round(Number(ctrl.value))) : fmt(ctrl.value);
                }
            }
        }
    }
}

/* ============================================================
   fps / ms timing toggle
   ============================================================ */
const TIMING_MODE_KEY = 'pmm_timing_mode';
let timingMode = localStorage.getItem(TIMING_MODE_KEY) || 'fps';
const TIMING_MODES = ['fps', 'ms'];
const timingCache = {};  // id → {us_per_tick, self_us_per_tick}

function fmtUs(us) {
    return us < 1000 ? us.toFixed(0) + 'µs' : (us / 1000).toFixed(2) + 'ms';
}

function displayTiming(id, t) {
    const el = document.getElementById('fps-' + id);
    if (!el) return;
    const us = t.us_per_tick;
    if (!us) { el.textContent = '—'; return; }
    if (timingMode === 'fps') {
        const fps = 1000000 / us;
        el.textContent = fps >= 1e3 ? (fps / 1e3).toFixed(1) + 'K fps'
                       : fps.toFixed(0) + ' fps';
    } else {
        el.textContent = fmtUs(us);
    }
}

function toggleTimingMode() {
    const idx = TIMING_MODES.indexOf(timingMode);
    timingMode = TIMING_MODES[(idx + 1) % TIMING_MODES.length];
    localStorage.setItem(TIMING_MODE_KEY, timingMode);
    for (const [id, t] of Object.entries(timingCache)) displayTiming(id, t);
}

/* ============================================================
   REST API
   ============================================================ */
let knownTypes = [];  // [{name, category, tags, allowedChildCategories}]

async function loadTypes() {
    try {
        const res = await fetch('/api/types');
        if (res.ok) knownTypes = await res.json();
    } catch (_) { knownTypes = []; }
}

async function loadModules() {
    try {
        const res = await fetch('/api/modules');
        if (!res.ok) throw new Error('HTTP ' + res.status);
        const modules = await res.json();
        render(modules);
    } catch (e) {
        document.getElementById('device-name').textContent = 'Error: ' + e.message;
    }
}

/* ============================================================
   Tree builder
   ============================================================ */
function buildTree(modules) {
    const byId = {};
    for (const m of modules) { byId[m.id] = m; m._children = []; }
    const roots = [];
    for (const m of modules) {
        if (m.parent_id && byId[m.parent_id]) {
            byId[m.parent_id]._children.push(m);
        } else {
            roots.push(m);
        }
    }
    // Propagate root core down to all descendants so the badge always shows
    // the actual core the module runs on (children run inside their parent's tick).
    function propagateCore(node, inheritedCore) {
        const effective = node.parent_id ? inheritedCore : node.core;
        node._displayCore = effective;
        for (const child of node._children) propagateCore(child, effective);
    }
    for (const root of roots) propagateCore(root, root.core);
    return roots;
}

/* ============================================================
   Navigation — one root module visible at a time
   ============================================================ */
const NAV_KEY = 'pmm_selectedRoot';
let selectedRootId = localStorage.getItem(NAV_KEY) || null;

/* ---- Part D: reorder + reparent — persisted in backend, no localStorage ---- */

function saveNavOrder(roots) {
    fetch('/api/modules/reorder', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ parent_id: '', ids: roots.map(r => r.id) })
    });
}

function saveChildOrder(parentId, children) {
    fetch('/api/modules/reorder', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ parent_id: parentId, ids: children.map(c => c.id) })
    });
}

// Move a module to a new parent (or to root when newParentId is '').
// The backend fires auto-wire and re-sorts loop order; we refresh immediately
// so the tree reflects the change without waiting for the 1 Hz WS push.
async function reparentModule(id, newParentId) {
    await fetch('/api/modules/reparent', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ id, parent_id: newParentId })
    });
    await loadModules();
}

// Module id being dragged across levels (set on dragstart of a child handle
// or a root nav item; cleared on dragend).
let crossDragId   = null;  // id of the module being dragged cross-level
let crossDragType = null;  // 'root' (nav item) or 'child' (child handle)

function updateNav(roots) {
    const nav = document.getElementById('nav-links');
    nav.innerHTML = '';
    let dragSrc = null;
    for (const mod of roots) {
        const btn = document.createElement('button');
        btn.className = 'nav-item' + (mod.id === selectedRootId ? ' active' : '');
        btn.draggable = true;
        btn.dataset.id = mod.id;
        btn.innerHTML =
            esc(mod.name) +
            '<span class="nav-item-sub">' + esc(mod.id) + '</span>';
        btn.onclick = () => {
            selectedRootId = mod.id;
            localStorage.setItem(NAV_KEY, mod.id);
            updateNav(roots);
            renderSelected(roots);
            if (window.innerWidth < 820) closeNav();
        };
        btn.addEventListener('dragstart', e => {
            dragSrc = btn;
            crossDragId   = mod.id;
            crossDragType = 'root';
            e.dataTransfer.effectAllowed = 'move';
            e.dataTransfer.setData('text/plain', mod.id);
        });
        btn.addEventListener('dragend', () => {
            crossDragId = null; crossDragType = null; dragSrc = null;
        });
        btn.addEventListener('dragover', e => {
            e.preventDefault();
            e.dataTransfer.dropEffect = 'move';
            nav.querySelectorAll('.nav-item').forEach(b => b.classList.remove('drag-over', 'drag-over-reparent'));
            if (btn === dragSrc) return;
            // A child being dragged → this nav item = reparent indicator.
            // A root being dragged → sibling reorder indicator.
            if (crossDragType === 'child') btn.classList.add('drag-over-reparent');
            else                           btn.classList.add('drag-over');
        });
        btn.addEventListener('dragleave', () => btn.classList.remove('drag-over', 'drag-over-reparent'));
        btn.addEventListener('drop', e => {
            e.preventDefault();
            btn.classList.remove('drag-over', 'drag-over-reparent');
            if (!dragSrc && crossDragType !== 'child') return;

            // Child → root nav item: reparent the child under this root module.
            if (crossDragType === 'child' && crossDragId && crossDragId !== mod.id) {
                reparentModule(crossDragId, mod.id);
                crossDragId = null; crossDragType = null;
                return;
            }

            // Root → root: sibling reorder.
            if (!dragSrc || dragSrc === btn) return;
            const items = [...nav.querySelectorAll('.nav-item[data-id]')];
            const fromIdx = items.indexOf(dragSrc);
            const toIdx   = items.indexOf(btn);
            if (fromIdx < 0 || toIdx < 0) return;
            roots.splice(toIdx, 0, roots.splice(fromIdx, 1)[0]);
            saveNavOrder(roots);
            updateNav(roots);
            renderSelected(roots);
        });
        nav.appendChild(btn);
    }

    // Drop zone at the bottom of nav: child handle dropped here → promote to root.
    const rootDrop = document.createElement('div');
    rootDrop.className = 'nav-root-drop';
    rootDrop.textContent = '↑ promote to root';
    rootDrop.addEventListener('dragover', e => {
        if (crossDragType !== 'child') return;
        e.preventDefault();
        rootDrop.classList.add('drag-over-reparent');
    });
    rootDrop.addEventListener('dragleave', () => rootDrop.classList.remove('drag-over-reparent'));
    rootDrop.addEventListener('drop', e => {
        e.preventDefault();
        rootDrop.classList.remove('drag-over-reparent');
        if (crossDragType === 'child' && crossDragId) {
            reparentModule(crossDragId, '');
            crossDragId = null; crossDragType = null;
        }
    });
    nav.appendChild(rootDrop);
}

function fmtBytes(n) {
    if (!n) return '0 B';
    return n >= 1024 ? (n / 1024).toFixed(1) + ' KB' : n + ' B';
}

function renderSelected(roots) {
    const container = document.getElementById('modules');
    container.innerHTML = '';
    const mod = roots.find(r => r.id === selectedRootId);
    if (mod) container.appendChild(buildCard(mod));
}

/* ============================================================
   Render
   ============================================================ */
function render(modules) {
    window._lastModules = modules;
    const roots = buildTree(modules);

    // Keep selection valid; fall back to first root if current id is gone
    if (!roots.find(r => r.id === selectedRootId)) {
        selectedRootId = roots.length > 0 ? roots[0].id : null;
    }

    updateNav(roots);
    renderSelected(roots);
    if (canvasView) renderCanvas(modules);
}

/* ============================================================
   Card builder
   ============================================================ */
const TYPE_TO_DOC = {
    'ModuleManager':            'core/module-manager',
    'EffectsLayer':             'layers/effects-layer',
    'DriverLayer':              'layers/driver-layer',
    'GridLayout':               'layouts/grid-layout',
    'SineEffectModule':         'effects/sine-effect-module',
    'RipplesEffectModule':      'effects/ripples-effect-module',
    'LinesEffectModule':        'effects/lines-effect-module',
    'BrightnessModifierModule': 'modifiers/brightness-mod-module',
    'PreviewModule':            'drivers/preview-module',
    'SystemStatusModule':       'system/system-info-module',
    'NetworkModule':            'network/network-module',
    'WifiStaModule':            'network/wifi-sta-module',
    'WifiApModule':             'network/wifi-ap-module',
    'EthernetModule':           'network/ethernet-module',
};

function buildCard(mod, parentId = null) {
    const card = document.createElement('div');
    card.className = 'module-card';
    if (parentId !== null) {
        card.dataset.id = mod.id;
    }

    const hdr = document.createElement('div');
    hdr.className = 'module-header';

    if (parentId !== null) {
        const handle = document.createElement('span');
        handle.className = 'drag-handle';
        handle.textContent = '☰';
        handle.title = 'drag to reorder';
        hdr.appendChild(handle);
    }

    const hdrText = document.createElement('div');
    hdrText.className = 'module-header-text';
    const classSz   = fmtBytes(mod.class_size_bytes || 0);
    const psramBytes = mod.psram_size_bytes || 0;
    const heapBytes  = (mod.heap_size_bytes || 0) - psramBytes;
    const memParts   = [];
    if (heapBytes  > 0) memParts.push(fmtBytes(heapBytes)  + ' heap');
    if (psramBytes > 0) memParts.push(fmtBytes(psramBytes) + ' PSRAM');
    const memStr = memParts.length ? memParts.join(' + ') : '0 B';
    const coreVal = mod._displayCore !== undefined ? mod._displayCore : mod.core;
    const coreBadge = (coreVal !== undefined)
        ? '<span class="core-badge c' + coreVal + '" title="FreeRTOS core affinity">C' + coreVal + '</span>'
        : '';
    const CATEGORY_EMOJI = { system: '⚙️', network: '🌐', effect: '✨', driver: '💡', layout: '📐' };
    const catEmoji = CATEGORY_EMOJI[mod.category] || '';
    const catBadge = catEmoji ? '<span class="cat-badge" title="' + esc(mod.category) + '">' + catEmoji + '</span>' : '';
    const replaceBtn = '<button class="replace-btn" title="replace module type">✎</button>';
    const setupDot = '<span class="setup-dot ' + (mod.setup_ok === false ? 'fail' : 'ok') +
        '" title="' + esc(mod.health || '') + '">&#9679;</span>';
    hdrText.innerHTML =
        '<div class="module-name">' + setupDot + esc(mod.name) + replaceBtn + '</div>' +
        '<div class="module-meta">' + esc(mod.id) +
        ' &middot; ' + esc(mod.category) + catBadge + coreBadge + '</div>' +
        '<div class="module-stats" title="click to toggle: fps / ms">' +
        '<span id="fps-' + esc(mod.id) + '">—</span>' +
        ' &middot; ' + classSz + ' &middot; ' + memStr + '</div>';
    hdr.appendChild(hdrText);
    // Wire the stats line as the fps/ms toggle target.
    hdrText.querySelector('.module-stats').onclick = toggleTimingMode;
    // Wire the replace button to open the same-category type picker.
    const rb = hdrText.querySelector('.replace-btn');
    if (rb) rb.onclick = (e) => {
        e.stopPropagation();
        showTypePicker(parentId, card, rb, parentId ? mod.type : null, mod.id, mod.category);
    };

    const helpLink = document.createElement('a');
    helpLink.className = 'help-link';
    helpLink.textContent = '?';
    helpLink.title = 'Documentation for ' + mod.name;
    const docPath = TYPE_TO_DOC[mod.type];
    helpLink.href = 'https://ewowi.github.io/projectMM/' + (docPath ? 'modules/' + docPath + '/' : 'user-guide/modules/');
    helpLink.target = '_blank';
    helpLink.rel = 'noopener';
    hdr.appendChild(helpLink);

    card.appendChild(hdr);

    // Controls section — for group modules wrap in a collapsible so the
    // children list is the visual focus; for leaf modules render inline.
    const controls = mod.controls || [];
    const isGroup  = mod.is_group === true;
    if (isGroup && controls.length > 0) {
        const details = document.createElement('details');
        details.className = 'group-controls';
        const summary = document.createElement('summary');
        summary.textContent = 'controls (' + controls.length + ')';
        details.appendChild(summary);
        for (const ctrl of controls) details.appendChild(buildControl(mod.id, ctrl));
        card.appendChild(details);
    } else if (controls.length === 0) {
        if (!isGroup) {
            const none = document.createElement('div');
            none.className = 'no-controls';
            none.textContent = 'no controls';
            card.appendChild(none);
        }
    } else {
        for (const ctrl of controls) card.appendChild(buildControl(mod.id, ctrl));
    }

    // Connections subsection — show string-ID inputs so the implicit data-flow
    // edges are visible in the tree view without needing the canvas.
    const stringCtrls = controls.filter(c => c.type === 'text' && c.value);
    if (stringCtrls.length > 0) {
        const connDiv = document.createElement('div');
        connDiv.className = 'connections';
        connDiv.innerHTML = '<span class="connections-label">connections</span>' +
            stringCtrls.map(c =>
                '<span class="connection-item">' +
                esc(c.key) + ' → <strong>' + esc(c.value) + '</strong>' +
                '</span>'
            ).join('');
        card.appendChild(connDiv);
    }

    if (mod._children && mod._children.length > 0) {
        const childrenDiv = document.createElement('div');
        childrenDiv.className = 'children';
        const orderedKids = [...mod._children];
        let dragSrc = null;
        const appendKid = (child) => {
            const cc = buildCard(child, mod.id);
            const handle = cc.querySelector('.drag-handle');
            if (handle) {
                handle.draggable = true;
                handle.addEventListener('dragstart', e => {
                    dragSrc = cc;
                    crossDragId   = child.id;
                    crossDragType = 'child';
                    e.dataTransfer.effectAllowed = 'move';
                    e.dataTransfer.setDragImage(cc, 20, 20);
                    e.stopPropagation();
                    document.getElementById('nav-links').classList.add('child-dragging');
                });
                handle.addEventListener('dragend', () => {
                    crossDragId = null; crossDragType = null;
                    document.getElementById('nav-links').classList.remove('child-dragging');
                });
            }
            cc.addEventListener('dragover', e => {
                e.preventDefault();
                e.stopPropagation();
                e.dataTransfer.dropEffect = 'move';
                [...childrenDiv.querySelectorAll(':scope > .module-card')]
                    .forEach(c => c.classList.remove('drag-over'));
                if (cc !== dragSrc) cc.classList.add('drag-over');
            });
            cc.addEventListener('dragleave', e => {
                if (!cc.contains(e.relatedTarget)) cc.classList.remove('drag-over');
            });
            cc.addEventListener('drop', e => {
                e.preventDefault();
                e.stopPropagation();
                cc.classList.remove('drag-over');
                if (!dragSrc || dragSrc === cc) return;
                const all = [...childrenDiv.querySelectorAll(':scope > .module-card')];
                const fromIdx = all.indexOf(dragSrc);
                const toIdx   = all.indexOf(cc);
                if (fromIdx < 0 || toIdx < 0) return;
                orderedKids.splice(toIdx, 0, orderedKids.splice(fromIdx, 1)[0]);
                saveChildOrder(mod.id, orderedKids);
                childrenDiv.innerHTML = '';
                for (const k of orderedKids) appendKid(k);
            });
            childrenDiv.appendChild(cc);
        };
        for (const child of orderedKids) appendKid(child);
        card.appendChild(childrenDiv);
    }

    // Firmware update UI injected for FirmwareUpdateModule cards.
    if (mod.type === 'FirmwareUpdateModule') card.appendChild(buildOtaPanel());

    const actions = document.createElement('div');
    actions.className = 'card-actions';

    const addBtn = document.createElement('button');
    addBtn.className = 'add-btn';
    addBtn.textContent = '+ add child';
    addBtn.onclick = () => showTypePicker(mod.id, card, addBtn, mod.type);
    actions.appendChild(addBtn);

    const hasKids = mod._children && mod._children.length > 0;
    const delBtn = document.createElement('button');
    delBtn.className = 'del-btn';
    delBtn.textContent = '✕ delete';
    delBtn.disabled = hasKids;
    delBtn.title = hasKids ? 'remove children first' : 'delete ' + mod.id;
    delBtn.onclick = () => deleteModule(mod.id);
    actions.appendChild(delBtn);

    card.appendChild(actions);

    // Root card drop zone: a root nav item dragged over this card becomes a
    // child of this module. Only active when this card is itself a root module
    // (parentId === null) and a root nav item is being dragged (crossDragType === 'root').
    if (parentId === null) {
        card.addEventListener('dragover', e => {
            if (crossDragType !== 'root' || crossDragId === mod.id) return;
            e.preventDefault();
            e.stopPropagation();
            card.classList.add('drag-over-reparent');
        });
        card.addEventListener('dragleave', e => {
            if (!card.contains(e.relatedTarget)) card.classList.remove('drag-over-reparent');
        });
        card.addEventListener('drop', e => {
            card.classList.remove('drag-over-reparent');
            if (crossDragType !== 'root' || !crossDragId || crossDragId === mod.id) return;
            e.preventDefault();
            e.stopPropagation();
            reparentModule(crossDragId, mod.id);
            crossDragId = null; crossDragType = null;
        });
    }

    return card;
}

/* ============================================================
   Type picker — context-aware with emoji chips + search
   ============================================================ */

// Root-only categories: types whose category is NOT in this list can only
// be added as children, never at the root level.
const CHILD_ONLY_CATEGORIES = ['effect', 'modifier', 'layout'];

function showTypePicker(parentId, card, addBtn, parentType, replaceId = null, replaceCategory = null) {
    if (card.querySelector('.type-picker')) return;
    addBtn.disabled = true;

    const isReplace = replaceId !== null;

    // Determine context filter: which categories are valid here.
    let allowedCats = null;  // null = root (exclude child-only categories)
    if (isReplace) {
        // Replace mode: same category only.
        allowedCats = replaceCategory ? [replaceCategory] : [];
    } else if (parentType !== null && parentType !== undefined) {
        const parentMeta = knownTypes.find(t => t.name === parentType);
        const acc = parentMeta && parentMeta.allowedChildCategories
                    ? parentMeta.allowedChildCategories.trim() : '';
        allowedCats = acc ? acc.split(' ') : [];  // empty array = no restriction
    }

    // Compute the context-filtered base list.
    function contextFiltered() {
        if (allowedCats === null) {
            return knownTypes.filter(t => !CHILD_ONLY_CATEGORIES.includes(t.category));
        }
        if (allowedCats.length === 0) return [...knownTypes];
        return knownTypes.filter(t => allowedCats.includes(t.category));
    }

    const base = contextFiltered();

    // Collect all unique emoji from the base list (Unicode code point iteration).
    const emojiOrder = [];
    const emojiSet = new Set();
    for (const t of base) {
        for (const ch of (t.tags || '')) {
            if (!emojiSet.has(ch)) { emojiSet.add(ch); emojiOrder.push(ch); }
        }
    }

    // Active chip state and search text.
    const activeChips = new Set();
    let searchText = '';

    // Build picker container.
    const picker = document.createElement('div');
    picker.className = 'type-picker';

    // Emoji chip row.
    const chipRow = document.createElement('div');
    chipRow.className = 'picker-chips';
    for (const emoji of emojiOrder) {
        const btn = document.createElement('button');
        btn.className = 'picker-chip';
        btn.textContent = emoji;
        btn.title = emoji;
        btn.type = 'button';
        btn.onclick = () => {
            if (activeChips.has(emoji)) { activeChips.delete(emoji); btn.classList.remove('active'); }
            else { activeChips.add(emoji); btn.classList.add('active'); }
            refreshList();
        };
        chipRow.appendChild(btn);
    }
    if (emojiOrder.length > 0) picker.appendChild(chipRow);

    // Search input.
    const search = document.createElement('input');
    search.type = 'text';
    search.className = 'picker-search';
    search.placeholder = 'search…';
    search.oninput = () => { searchText = search.value.toLowerCase(); refreshList(); };
    search.onkeydown = (e) => {
        if (e.key === 'ArrowDown') {
            e.preventDefault();
            const sel = listDiv.querySelector('.picker-type-item.selected') ||
                        listDiv.querySelector('.picker-type-item');
            if (sel) sel.focus();
        } else if (e.key === 'Enter') {
            e.preventDefault();
            if (selectedType) { picker.remove(); addBtn.disabled = false; isReplace ? apiReplaceModule(replaceId, selectedType) : createModule(selectedType); }
        }
    };
    picker.appendChild(search);

    // Action buttons (declared early so refreshList can reference createBtn).
    const actions = document.createElement('div');
    actions.className = 'picker-actions';

    const createBtn = document.createElement('button');
    createBtn.className = 'create-btn';
    createBtn.textContent = isReplace ? 'replace' : 'create';
    createBtn.disabled = base.length === 0;
    createBtn.onclick = async () => {
        if (!selectedType) return;
        picker.remove();
        addBtn.disabled = false;
        await (isReplace ? apiReplaceModule(replaceId, selectedType) : createModule(selectedType));
    };
    actions.appendChild(createBtn);

    const cancelBtn = document.createElement('button');
    cancelBtn.className = 'cancel-btn';
    cancelBtn.textContent = 'cancel';
    cancelBtn.onclick = () => { picker.remove(); addBtn.disabled = false; };
    actions.appendChild(cancelBtn);

    // Filtered type list.
    let selectedType = base.length > 0 ? base[0].name : '';
    const listDiv = document.createElement('div');
    listDiv.className = 'picker-list';

    function refreshList() {
        listDiv.innerHTML = '';
        const q = searchText;
        const visible = base.filter(t => {
            if (q && !t.name.toLowerCase().includes(q)) return false;
            if (activeChips.size > 0) {
                for (const chip of activeChips)
                    if (!(t.tags || '').includes(chip)) return false;
            }
            return true;
        });
        if (visible.length === 0) {
            const empty = document.createElement('div');
            empty.className = 'picker-type-item';
            empty.style.opacity = '0.5';
            empty.textContent = 'no matches';
            listDiv.appendChild(empty);
            selectedType = '';
            createBtn.disabled = true;
            return;
        }
        if (!visible.find(t => t.name === selectedType)) selectedType = visible[0].name;
        createBtn.disabled = false;
        for (const t of visible) {
            const row = document.createElement('div');
            row.className = 'picker-type-item' + (t.name === selectedType ? ' selected' : '');
            const nameEl = document.createElement('span');
            nameEl.className = 'picker-type-name';
            nameEl.textContent = t.name;
            const tagsEl = document.createElement('span');
            tagsEl.className = 'picker-type-tags';
            tagsEl.textContent = t.tags || '';
            row.appendChild(nameEl);
            row.appendChild(tagsEl);
            row.tabIndex = 0;
            row.onclick = () => {
                selectedType = t.name;
                listDiv.querySelectorAll('.picker-type-item').forEach(r => r.classList.remove('selected'));
                row.classList.add('selected');
            };
            row.onfocus = () => {
                selectedType = t.name;
                listDiv.querySelectorAll('.picker-type-item').forEach(r => r.classList.remove('selected'));
                row.classList.add('selected');
            };
            row.ondblclick = async () => {
                selectedType = t.name;
                picker.remove();
                addBtn.disabled = false;
                await (isReplace ? apiReplaceModule(replaceId, selectedType) : createModule(selectedType));
            };
            row.onkeydown = (e) => {
                if (e.key === 'ArrowDown') {
                    e.preventDefault();
                    const next = row.nextElementSibling;
                    if (next) next.focus();
                } else if (e.key === 'ArrowUp') {
                    e.preventDefault();
                    const prev = row.previousElementSibling;
                    if (prev) prev.focus(); else search.focus();
                } else if (e.key === 'Enter') {
                    e.preventDefault();
                    picker.remove();
                    addBtn.disabled = false;
                    isReplace ? apiReplaceModule(replaceId, t.name) : createModule(t.name);
                }
            };
            listDiv.appendChild(row);
        }
    }
    refreshList();
    picker.appendChild(listDiv);
    picker.appendChild(actions);

    card.appendChild(picker);
    search.focus();

    async function createModule(type) {
        const suffix = (Date.now() % 100000).toString();
        const id = type.toLowerCase().replace(/[^a-z0-9]/g, '') + '_' + suffix;
        await apiAddModule(type, id, parentId);
    }
}

/* ============================================================
   Module add / delete
   ============================================================ */
async function apiAddModule(type, id, parentId) {
    try {
        const body = { type, id };
        if (parentId) body.parent_id = parentId;
        const res = await fetch('/api/modules', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(body)
        });
        if (res.ok) {
            // Navigate to the new module: root modules become the selection directly;
            // child modules select their parent root card.
            selectedRootId = parentId || id;
            await loadModules();
        } else {
            const err = await res.json().catch(() => ({}));
            console.error('Add failed: ' + (err.error || res.status));
        }
    } catch (e) { console.error('Add error: ' + e.message); }
}

async function apiReplaceModule(id, newType) {
    try {
        const res = await fetch('/api/modules/replace', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ id, type: newType })
        });
        if (res.ok) {
            await loadModules();
        } else {
            const err = await res.json().catch(() => ({}));
            console.error('Replace failed: ' + (err.error || res.status));
        }
    } catch (e) { console.error('Replace error: ' + e.message); }
}

async function deleteModule(id) {
    try {
        const res = await fetch('/api/modules/' + encodeURIComponent(id),
                                { method: 'DELETE' });
        if (res.ok) {
            await loadModules();
        } else {
            const err = await res.json().catch(() => ({}));
            console.error('Delete failed: ' + (err.error || res.status));
        }
    } catch (e) { console.error('Delete error: ' + e.message); }
}

/* ============================================================
   Control builder
   ============================================================ */
function makeResetBtn(moduleId, key, currentValue, defaultValue) {
    const atDefault = Math.abs(Number(currentValue) - Number(defaultValue)) < 0.001;
    const btn = document.createElement('button');
    btn.className = 'reset-btn' + (atDefault ? '' : ' active');
    btn.textContent = '↺';
    btn.title = 'Reset to default (' + defaultValue + ')';
    btn.dataset.mid = moduleId;
    btn.dataset.key = key;
    btn.dataset.defVal = String(defaultValue);
    btn.onclick = () => postControl(moduleId, key, defaultValue);
    return btn;
}

function buildControl(moduleId, ctrl) {
    const row = document.createElement('div');
    row.className = 'control-row';

    const label = document.createElement('span');
    label.className = 'control-label';
    label.textContent = ctrl.key;
    row.appendChild(label);

    if (ctrl.type === 'slider') {
        const range = ctrl.max - ctrl.min;
        const step = ctrl.integer ? 1 : (range > 0 ? range / 200 : 0.01);

        const input = document.createElement('input');
        input.type  = 'range';
        input.min   = ctrl.min;
        input.max   = ctrl.max;
        input.step  = step;
        input.value = ctrl.value;
        input.dataset.mid = moduleId;
        input.dataset.key = ctrl.key;
        if (ctrl.integer) input.dataset.integer = '1';

        const display = document.createElement('span');
        display.className = 'value-display';
        display.textContent = ctrl.integer
            ? String(Math.round(Number(ctrl.value)))
            : fmt(ctrl.value);

        let timer = null;
        // pointerdown marks the drag start so the 1s state push doesn't
        // overwrite the slider before the first input event fires.
        input.addEventListener('pointerdown', () => {
            dragTs[moduleId + ':' + ctrl.key] = Date.now();
        });
        input.addEventListener('input', () => {
            dragTs[moduleId + ':' + ctrl.key] = Date.now();
            display.textContent = ctrl.integer
                ? String(Math.round(Number(input.value)))
                : fmt(input.value);
            clearTimeout(timer);
            timer = setTimeout(
                () => postControl(moduleId, ctrl.key, ctrl.integer ? parseInt(input.value) : parseFloat(input.value)), 150);
        });

        row.appendChild(input);
        row.appendChild(display);
        if (ctrl.default !== undefined)
            row.appendChild(makeResetBtn(moduleId, ctrl.key, ctrl.value, ctrl.default));

    } else if (ctrl.type === 'display' || ctrl.type === 'time') {
        const val = document.createElement('span');
        val.className = 'display-value';
        val.dataset.mid  = moduleId;
        val.dataset.key  = ctrl.key;
        val.dataset.disp = '1';
        val.dataset.type = ctrl.type;
        val.textContent  = fmtDisplay(ctrl.value, ctrl.type);
        row.appendChild(val);

    } else if (ctrl.type === 'progress') {
        const bar = document.createElement('progress');
        bar.className = 'progress-bar';
        bar.min   = ctrl.min != null ? ctrl.min : 0;
        bar.max   = ctrl.max != null ? ctrl.max : 100;
        bar.value = ctrl.value != null ? ctrl.value : 0;
        bar.dataset.mid = moduleId;
        bar.dataset.key = ctrl.key;
        if (ctrl.integer) bar.dataset.integer = '1';
        row.appendChild(bar);

        const txt = document.createElement('span');
        txt.className = 'progress-text';
        txt.textContent = fmtProgress(Number(ctrl.value), Number(bar.max), ctrl.integer);
        row.appendChild(txt);

    } else if (ctrl.type === 'button') {
        const btn = document.createElement('button');
        btn.className = 'action-btn';
        btn.textContent = ctrl.key.replace(/_/g, ' ');
        btn.onclick = () => postControl(moduleId, ctrl.key, 1);
        row.appendChild(btn);

    } else if (ctrl.type === 'text' || ctrl.type === 'password') {
        const input = document.createElement('input');
        input.type = ctrl.type === 'password' ? 'password' : 'text';
        input.className = 'text-input';
        input.placeholder = ctrl.type === 'password'
            ? (ctrl.setLen > 0 ? '•'.repeat(ctrl.setLen) : '(not set)')
            : '';
        input.value = ctrl.value != null ? ctrl.value : '';
        input.dataset.mid = moduleId;
        input.dataset.key = ctrl.key;

        let timer = null;
        input.addEventListener('input', () => {
            dragTs[moduleId + ':' + ctrl.key] = Date.now();
            clearTimeout(timer);
            timer = setTimeout(
                () => postControl(moduleId, ctrl.key, input.value), 500);
        });
        row.appendChild(input);

        if (ctrl.type === 'password') {
            const peek = document.createElement('button');
            peek.className = 'peek-btn';
            peek.textContent = '👁';
            peek.title = 'Hold to show';
            const showPw = () => { input.type = 'text'; };
            const hidePw = () => { input.type = 'password'; };
            peek.addEventListener('mousedown',   showPw);
            peek.addEventListener('touchstart',  showPw, { passive: true });
            peek.addEventListener('mouseup',     hidePw);
            peek.addEventListener('mouseleave',  hidePw);
            peek.addEventListener('touchend',    hidePw);
            peek.addEventListener('touchcancel', hidePw);
            row.appendChild(peek);
        }

    } else if (ctrl.type === 'toggle') {
        const cb = document.createElement('input');
        cb.type = 'checkbox';
        cb.className = 'toggle-input';
        cb.checked = !!ctrl.value;
        cb.dataset.mid    = moduleId;
        cb.dataset.key    = ctrl.key;
        cb.dataset.toggle = '1';

        cb.addEventListener('change', () => {
            dragTs[moduleId + ':' + ctrl.key] = Date.now();
            postControl(moduleId, ctrl.key, cb.checked ? 1 : 0);
        });
        row.appendChild(cb);
        if (ctrl.default !== undefined)
            row.appendChild(makeResetBtn(moduleId, ctrl.key, ctrl.value ? 1 : 0, ctrl.default));

    } else if (ctrl.type === 'select' && Array.isArray(ctrl.options)) {
        const sel = document.createElement('select');
        sel.className = 'select-input';
        sel.dataset.mid = moduleId;
        sel.dataset.key = ctrl.key;
        ctrl.options.forEach((label, i) => {
            const opt = document.createElement('option');
            opt.value = i;
            opt.textContent = label;
            if (i === ctrl.value) opt.selected = true;
            sel.appendChild(opt);
        });
        sel.addEventListener('change', () => {
            dragTs[moduleId + ':' + ctrl.key] = Date.now();
            postControl(moduleId, ctrl.key, parseInt(sel.value));
        });
        row.appendChild(sel);
        if (ctrl.default !== undefined)
            row.appendChild(makeResetBtn(moduleId, ctrl.key, ctrl.value, ctrl.default));
    }
    return row;
}

async function postControl(moduleId, key, value) {
    try {
        await fetch('/api/control', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ id: moduleId, key, value })
        });
    } catch (_) {}
}

/* ============================================================
   Helpers
   ============================================================ */
function fmt(v) { return Number(v).toFixed(2); }

function fmtTime(v) {
    const s = Math.floor(Number(v));
    const d = Math.floor(s / 86400);
    const h = Math.floor((s % 86400) / 3600);
    const m = Math.floor((s % 3600) / 60);
    const sec = s % 60;
    let out = '';
    if (d > 0) out += d + 'd ';
    if (d > 0 || h > 0) out += h + 'h ';
    if (d > 0 || h > 0 || m > 0) out += m + 'm ';
    return (out + sec + 's').trim();
}

function fmtDisplay(v, type) {
    if (type === 'time') return fmtTime(v);
    if (typeof v === 'string') return v;
    const n = Number(v);
    if (isNaN(n)) return String(v);
    return Number.isInteger(n) ? String(n) : n.toFixed(1);
}

function fmtProgress(value, max, integer) {
    const v = Number(value), m = Number(max);
    if (isNaN(v) || isNaN(m) || m === 0) return '—';
    return integer ? Math.round(v) + ' / ' + Math.round(m) : v.toFixed(1) + ' / ' + m.toFixed(1);
}

function esc(s) {
    return String(s)
        .replace(/&/g,'&amp;').replace(/</g,'&lt;')
        .replace(/>/g,'&gt;').replace(/"/g,'&quot;');
}

function openNav()  {
    document.getElementById('side-nav').classList.add('open');
    document.getElementById('nav-overlay').classList.add('open');
}
function closeNav() {
    document.getElementById('side-nav').classList.remove('open');
    document.getElementById('nav-overlay').classList.remove('open');
}

/* ============================================================
   Day / night theme
   ============================================================ */
const THEME_KEY = 'pmm_theme';
let curTheme = localStorage.getItem(THEME_KEY) || 'dark';

function applyTheme(t) {
    curTheme = t;
    document.body.dataset.theme = t;
    const btn = document.getElementById('theme-btn');
    if (btn) btn.textContent = t === 'dark' ? '☀' : '🌙';
    localStorage.setItem(THEME_KEY, t);
}

/* ============================================================
   Firmware update — OTA upload helpers and GitHub release check
   ============================================================ */
let currentEnv = null;
let currentFirmwareVersion = null;

const GH_CACHE_KEY = 'pmm_gh_releases';
const GH_CACHE_TTL = 3600000;  // 1 hour

async function fetchGithubReleases(useCache = true) {
    try {
        if (useCache) {
            const cached = sessionStorage.getItem(GH_CACHE_KEY);
            if (cached) {
                const obj = JSON.parse(cached);
                if (Date.now() - obj.ts < GH_CACHE_TTL) return obj.data;
            }
        }
        const res = await fetch('https://api.github.com/repos/ewowi/projectMM/releases?per_page=5');
        if (!res.ok) return null;
        const data = await res.json();
        sessionStorage.setItem(GH_CACHE_KEY, JSON.stringify({ ts: Date.now(), data }));
        return data;
    } catch (_) {
        return null;
    }
}

function isNewerVersion(current, candidate) {
    const strip = v => v.replace(/^v/, '').split('-')[0].split('.').map(Number);
    const a = strip(String(current)), b = strip(String(candidate));
    for (let i = 0; i < Math.max(a.length, b.length); i++) {
        const x = a[i] || 0, y = b[i] || 0;
        if (y > x) return true;
        if (y < x) return false;
    }
    return false;
}

async function checkForUpdate(version, env) {
    currentFirmwareVersion = version;
    currentEnv = env;
    const badge = document.getElementById('update-badge');
    if (!badge || !env || env === 'PC') { if (badge) badge.style.display = 'none'; return; }
    const releases = await fetchGithubReleases();
    if (!releases) { badge.style.display = 'none'; return; }
    for (const r of releases) {
        if (r.prerelease) continue;
        if (!isNewerVersion(version, r.tag_name)) continue;
        const assetName = 'projectMM-' + env + '.bin';
        const asset = (r.assets || []).find(a => a.name === assetName);
        if (!asset) continue;
        badge.textContent = r.tag_name + ' available';
        badge.style.display = '';
        badge.title = 'New firmware — open FirmwareUpdate module to install';
        badge.dataset.assetUrl = asset.browser_download_url;
        badge.dataset.tag = r.tag_name;
        return;
    }
    badge.style.display = 'none';
}

function doUpload(buf, label, msgEl, progEl) {
    return new Promise(resolve => {
        otaInProgress = true;
        otaActiveMsgEl = msgEl;
        msgEl.textContent = 'Uploading ' + label + '...';
        progEl.style.display = '';
        progEl.value = 0;
        const xhr = new XMLHttpRequest();
        xhr.open('POST', '/api/firmware');
        xhr.setRequestHeader('Content-Type', 'application/octet-stream');
        xhr.upload.onprogress = e => {
            if (e.lengthComputable) {
                progEl.value = Math.round(e.loaded * 100 / e.total);
                msgEl.textContent = 'Flashing: ' + progEl.value + '%';
            }
        };
        xhr.onload = () => {
            if (xhr.status === 200) {
                progEl.value = 100;
                msgEl.textContent = 'Flash complete — device rebooting automatically...';
            } else {
                otaInProgress = false;
                otaActiveMsgEl = null;
                msgEl.textContent = 'Upload failed: HTTP ' + xhr.status;
                progEl.style.display = 'none';
            }
            resolve();
        };
        xhr.onerror = () => {
            // Connection drop during OTA usually means the device rebooted successfully
            // before sending the response. Treat the same as success via WS disconnect.
            if (!otaInProgress) {
                msgEl.textContent = 'Upload error (connection lost)';
                progEl.style.display = 'none';
            }
            resolve();
        };
        xhr.send(buf);
    });
}

async function installFromUrl(url, label, msgEl, progEl) {
    msgEl.textContent = 'Downloading ' + label + '...';
    progEl.style.display = '';
    progEl.value = 0;
    try {
        const res = await fetch(url);
        if (!res.ok) throw new Error('Download failed: HTTP ' + res.status);
        const buf = await res.arrayBuffer();
        await doUpload(buf, label, msgEl, progEl);
    } catch (e) {
        msgEl.textContent = 'Error: ' + e.message;
        progEl.style.display = 'none';
    }
}

async function installFromDeviceUrl(url, label, msgEl, progEl) {
    msgEl.textContent = 'Sending URL to device...';
    progEl.style.display = 'none';
    try {
        const res = await fetch('/api/firmware/url', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ url })
        });
        const j = await res.json();
        if (!j.ok) throw new Error(j.error || 'failed');
        otaInProgress = true;
        otaActiveMsgEl = msgEl;
        msgEl.textContent = 'Device is downloading and flashing ' + label + '... (watch update_status for progress)';
    } catch (e) {
        msgEl.textContent = 'Error: ' + e.message;
    }
}

async function populateGhList(listEl, msgEl, progEl) {
    listEl.innerHTML = '';
    const env = currentEnv;
    if (!env) {
        listEl.innerHTML = '<div class="ota-msg">Waiting for device status...</div>';
        return;
    }
    listEl.innerHTML = '<div class="ota-msg">Loading...</div>';
    const releases = await fetchGithubReleases(false);
    if (!releases) {
        listEl.innerHTML = '<div class="ota-msg">GitHub unreachable — use file upload.</div>';
        return;
    }
    listEl.innerHTML = '';
    const assetName = env === 'PC' ? null : 'projectMM-' + env + '.bin';
    for (const r of releases) {
        const item = document.createElement('div');
        item.className = 'release-item';
        const info = document.createElement('span');
        const date = r.published_at ? r.published_at.substring(0, 10) : '';
        info.innerHTML =
            '<a class="release-tag" href="' + esc(r.html_url) + '" target="_blank" rel="noopener">' + esc(r.tag_name) + '</a>' +
            (r.prerelease ? '<span class="release-pre">pre</span>' : '') +
            '<span class="release-date">' + esc(date) + '</span>';
        item.appendChild(info);
        const asset = assetName ? (r.assets || []).find(a => a.name === assetName) : null;
        const btn = document.createElement('button');
        btn.className = 'install-btn';
        btn.textContent = 'Install';
        if (!asset) {
            btn.disabled = true;
            btn.title = assetName ? 'No ' + assetName + ' in this release' : 'Install not supported on PC';
        } else {
            btn.onclick = async () => {
                btn.disabled = true;
                await installFromDeviceUrl(asset.browser_download_url, r.tag_name, msgEl, progEl);
            };
        }
        item.appendChild(btn);
        listEl.appendChild(item);
    }
    if (!listEl.children.length) {
        listEl.innerHTML = '<div class="ota-msg">No releases found.</div>';
    }
}

function buildOtaPanel() {
    const section = document.createElement('div');
    section.className = 'ota-section';

    const tabs = document.createElement('div');
    tabs.className = 'ota-tabs';
    const tabFile = document.createElement('button');
    tabFile.className = 'ota-tab active';
    tabFile.textContent = 'File Upload';
    const tabGh = document.createElement('button');
    tabGh.className = 'ota-tab';
    tabGh.textContent = 'GitHub';
    tabs.appendChild(tabFile);
    tabs.appendChild(tabGh);
    section.appendChild(tabs);

    const msgEl = document.createElement('div');
    msgEl.className = 'ota-msg';
    section.appendChild(msgEl);

    const progEl = document.createElement('progress');
    progEl.className = 'ota-progress';
    progEl.max = 100;
    progEl.value = 0;
    progEl.style.display = 'none';
    section.appendChild(progEl);

    // File upload panel
    const filePanel = document.createElement('div');
    filePanel.className = 'ota-panel active';
    const fileRow = document.createElement('div');
    fileRow.className = 'ota-file-row';
    const fileInput = document.createElement('input');
    fileInput.type = 'file';
    fileInput.accept = '.bin';
    fileInput.className = 'ota-file-input';
    const uploadBtn = document.createElement('button');
    uploadBtn.className = 'install-btn';
    uploadBtn.textContent = 'Upload';
    uploadBtn.disabled = true;
    fileInput.onchange = () => { uploadBtn.disabled = !fileInput.files || !fileInput.files.length; };
    uploadBtn.onclick = async () => {
        if (!fileInput.files || !fileInput.files.length) return;
        uploadBtn.disabled = true;
        fileInput.disabled = true;
        const buf = await fileInput.files[0].arrayBuffer();
        await doUpload(buf, fileInput.files[0].name, msgEl, progEl);
        uploadBtn.disabled = false;
        fileInput.disabled = false;
    };
    fileRow.appendChild(fileInput);
    fileRow.appendChild(uploadBtn);
    filePanel.appendChild(fileRow);
    section.appendChild(filePanel);

    // GitHub releases panel
    const ghPanel = document.createElement('div');
    ghPanel.className = 'ota-panel';
    const ghList = document.createElement('div');
    ghPanel.appendChild(ghList);
    section.appendChild(ghPanel);

    // Tab switching
    tabFile.onclick = () => {
        tabFile.classList.add('active'); tabGh.classList.remove('active');
        filePanel.classList.add('active'); ghPanel.classList.remove('active');
    };
    tabGh.onclick = async () => {
        tabGh.classList.add('active'); tabFile.classList.remove('active');
        ghPanel.classList.add('active'); filePanel.classList.remove('active');
        await populateGhList(ghList, msgEl, progEl);
    };

    return section;
}

/* ============================================================
   System status panel — polls GET /api/system every 2 s
   ============================================================ */
async function loadSystemStatus() {
    try {
        const res = await fetch('/api/system');
        if (!res.ok) return;
        const d = await res.json();
        const el = document.getElementById('sys-status');
        if (!el) return;
        const parts = [];
        if (d.uptime_s != null) parts.push(fmtTime(d.uptime_s));
        if (d.heap_free_kb > 0) {
          const maxPart = d.max_alloc_kb > 0 ? ' / ' + d.max_alloc_kb.toFixed(0) + 'K max' : '';
          parts.push(d.heap_free_kb.toFixed(0) + 'K free' + maxPart + ' heap');
        }
        if (d.core_temp != null && d.core_temp > 0) parts.push(d.core_temp.toFixed(1) + '°C');
        el.textContent = parts.join(' · ');
        document.getElementById('reboot-btn')
            .classList.toggle('crashed', !!d.is_crash);
        if (d.firmware_version && d.env) checkForUpdate(d.firmware_version, d.env);
    } catch (_) {}
}

/* ============================================================
   Part F: Health panel — polls GET /api/test every 30 s
   ============================================================ */
async function loadHealth() {
    try {
        const res = await fetch('/api/test');
        const badge = document.getElementById('health-badge');
        const tbody = document.getElementById('health-tbody');
        if (!res.ok) { badge.textContent = '—'; badge.className = ''; return; }
        const data = await res.json();
        const failed = data.failed || 0;
        const total  = data.total  || 0;
        badge.textContent = failed === 0 ? (total + ' pass') : (failed + ' fail');
        badge.className   = failed === 0 ? '' : 'fail';
        tbody.innerHTML = '';
        for (const c of (data.cases || [])) {
            const tr = document.createElement('tr');
            tr.innerHTML =
                '<td>' + esc(c.name) + '</td>' +
                '<td class="' + (c.status === 'pass' ? 'pass' : 'fail') + '">' +
                esc(c.status) + '</td>' +
                '<td>' + (c.ms != null ? Number(c.ms).toFixed(3) + ' ms' : '') + '</td>';
            tbody.appendChild(tr);
        }
    } catch (_) {}
}

/* ============================================================
   Step 3: Canvas (node-graph) view
   ============================================================ */
let canvasView = false;        // false = tree, true = canvas
let canvasPan  = { x: 40, y: 40 };
let canvasDrag = null;         // { type:'pan'|'box', id, ox, oy, px, py }
const CANVAS_POS_KEY = 'mm_canvas_pos';

function canvasPositions() {
    try { return JSON.parse(localStorage.getItem(CANVAS_POS_KEY) || '{}'); } catch { return {}; }
}
function saveCanvasPos(id, x, y) {
    const p = canvasPositions(); p[id] = { x, y };
    localStorage.setItem(CANVAS_POS_KEY, JSON.stringify(p));
}

function renderCanvas(modules) {
    const viewport = document.getElementById('canvas-viewport');
    const world    = document.getElementById('canvas-world');
    const svg      = document.getElementById('canvas-svg');
    const sidebar  = document.getElementById('canvas-sidebar');
    if (!viewport || !world || !svg) return;

    const positions = canvasPositions();
    const moduleIds = new Set(modules.map(m => m.id));

    // Build parent→children tree. Roots are modules with no (resolvable) parent;
    // children render nested inside their parent box, like the tree view.
    const byId = {};
    modules.forEach(m => { byId[m.id] = m; m._kids = []; });
    const roots = [];
    modules.forEach(m => {
        if (m.parent_id && byId[m.parent_id]) byId[m.parent_id]._kids.push(m);
        else roots.push(m);
    });

    // Auto-layout (only roots get a canvas position) is done in a second pass
    // below — root heights vary with nesting depth, so we measure rendered
    // boxes before stacking them top-to-bottom.
    const GAP_Y = 30;

    // Render world transform
    world.style.transform = `translate(${canvasPan.x}px,${canvasPan.y}px)`;
    svg.style.transform   = `translate(${canvasPan.x}px,${canvasPan.y}px)`;

    // --- Boxes (recursive: a box contains its children) ---
    function buildBox(m, isRoot) {
        const box = document.createElement('div');
        box.className = 'cbox' + (isRoot ? ' cbox-root' : ' cbox-child');
        box.dataset.id = m.id;

        const head = document.createElement('div');
        head.className = 'cbox-head';

        const title = document.createElement('div');
        title.className = 'cbox-title';
        title.textContent = m.id + (m.is_group ? ' ⬡' : '');
        head.appendChild(title);

        const sub = document.createElement('div');
        sub.className = 'cbox-type';
        sub.textContent = m.type || '';
        head.appendChild(sub);
        box.appendChild(head);

        title.addEventListener('click', e => {
            e.stopPropagation();
            showCanvasSidebar(m, modules, sidebar);
        });

        if (m._kids.length) {
            const kidWrap = document.createElement('div');
            kidWrap.className = 'cbox-kids';
            m._kids.forEach(k => kidWrap.appendChild(buildBox(k, false)));
            box.appendChild(kidWrap);
        }
        return box;
    }

    world.innerHTML = '';
    const rootBoxes = [];
    roots.forEach(r => {
        const box = buildBox(r, true);
        box.style.left = '40px';   // provisional; vertical layout pass fixes y
        box.style.top  = '40px';

        // Drag the whole root subtree by its header only
        box.querySelector('.cbox-head').addEventListener('mousedown', e => {
            if (e.button !== 0) return;
            e.stopPropagation();
            const p = canvasPositions()[r.id] || { x: parseInt(box.style.left), y: parseInt(box.style.top) };
            canvasDrag = { type: 'box', id: r.id, ox: e.clientX - p.x, oy: e.clientY - p.y };
        });

        world.appendChild(box);
        rootBoxes.push({ r, box });
    });

    // --- Topology-aware auto-layout ---
    // A root that consumes data from another root's subtree is placed in the
    // next column to the right of its source, vertically near it. Roots with
    // a saved position keep it and are skipped by auto-layout.
    const GAP_X = 90;
    const rootIds = roots.map(r => r.id);
    const rootIdx = {};
    roots.forEach((r, i) => { rootIdx[r.id] = i; });

    function rootOf(id) {
        let m = byId[id], guard = 0;
        while (m && m.parent_id && byId[m.parent_id] && guard++ < 64) m = byId[m.parent_id];
        return m ? m.id : id;
    }

    // Root→root edges: consumer root depends on source root (source feeds it).
    // srcModuleOf[consumerRoot] = the specific source module id it reads from,
    // used to vertically align the consumer next to that node.
    const feeds = {};       // sourceRoot -> [consumerRoot...]
    const inDeg = {};
    const srcModuleOf = {};
    rootIds.forEach(id => { feeds[id] = []; inDeg[id] = 0; });
    modules.forEach(m => {
        (m.controls || []).forEach(c => {
            if (c.type === 'text' && c.value && moduleIds.has(c.value)) {
                const cons = rootOf(m.id), src = rootOf(c.value);
                if (cons !== src && feeds[src] && !feeds[src].includes(cons)) {
                    feeds[src].push(cons);
                    inDeg[cons]++;
                    if (!srcModuleOf[cons]) srcModuleOf[cons] = c.value;
                }
            }
        });
    });

    // Longest-path column assignment (Kahn topological order).
    const colOf = {};
    rootIds.forEach(id => { colOf[id] = 0; });
    const rem = { ...inDeg };
    const q = rootIds.filter(id => inDeg[id] === 0);
    while (q.length) {
        const id = q.shift();
        feeds[id].forEach(nxt => {
            colOf[nxt] = Math.max(colOf[nxt], colOf[id] + 1);
            if (--rem[nxt] === 0) q.push(nxt);
        });
    }

    // Place: per column, stack top-to-bottom by measured height. Manually
    // positioned roots are honored and excluded from the auto flow.
    const colNextY = {};
    const colX = {};
    let runX = 40;
    const maxCol = Math.max(0, ...rootIds.map(id => colOf[id]));
    for (let c = 0; c <= maxCol; c++) {
        colX[c] = runX;
        colNextY[c] = 40;
        // Column width = widest auto-positioned root box in this column.
        let colW = 0;
        rootBoxes.forEach(({ r, box }) => {
            if (!positions[r.id] && colOf[r.id] === c) colW = Math.max(colW, box.offsetWidth);
        });
        runX += (colW || 240) + GAP_X;
    }

    const boxOf = {};
    rootBoxes.forEach(({ r, box }) => { boxOf[r.id] = box; });

    // Place column-by-column so a consumer can read its source's final Y.
    // Honor manual positions first, then auto-flow the rest per column.
    rootBoxes.forEach(({ r, box }) => {
        if (positions[r.id]) {
            box.style.left = positions[r.id].x + 'px';
            box.style.top  = positions[r.id].y + 'px';
        }
    });

    for (let c = 0; c <= maxCol; c++) {
        rootBoxes
            .filter(({ r }) => !positions[r.id] && colOf[r.id] === c)
            .sort((a, b) => rootIdx[a.r.id] - rootIdx[b.r.id])
            .forEach(({ r, box }) => {
                box.style.left = colX[c] + 'px';

                // Prefer aligning to the source module's rendered top
                // (works for deeply-nested sources via rect math).
                let wantY = colNextY[c];
                const srcId = srcModuleOf[r.id];
                if (srcId) {
                    const srcEl = world.querySelector(
                        '.cbox[data-id="' + CSS.escape(srcId) + '"] > .cbox-head');
                    if (srcEl) {
                        const wRect = world.getBoundingClientRect();
                        wantY = srcEl.getBoundingClientRect().top - wRect.top;
                    }
                }
                // Never overlap a box already placed above in this column.
                const y = Math.max(wantY, colNextY[c]);
                box.style.top = y + 'px';
                colNextY[c] = y + box.offsetHeight + GAP_Y;
            });
    }

    // --- SVG data-flow noodles ---
    // Drawn between actual box DOM rects so nested children connect correctly.
    svg.setAttribute('width', '6000');
    svg.setAttribute('height', '4000');

    const dataEdges = [];
    modules.forEach(m => {
        (m.controls || []).forEach(c => {
            if (c.type === 'text' && c.value && moduleIds.has(c.value)) {
                dataEdges.push({ from: m.id, to: c.value, label: c.key });
            }
        });
    });
    window._canvasEdges = dataEdges;
    drawCanvasNoodles();
}

// Redraw data-flow noodles from current box DOM positions. Called after
// render and live during a box drag so wires track the moving box.
function drawCanvasNoodles() {
    const world = document.getElementById('canvas-world');
    const svg   = document.getElementById('canvas-svg');
    const edges = window._canvasEdges;
    if (!world || !svg || !edges) return;
    svg.innerHTML = '';

    function bezier(x1, y1, x2, y2) {
        const dx = Math.max(40, Math.abs(x2 - x1) / 2);
        return `M${x1},${y1} C${x1 + dx},${y1} ${x2 - dx},${y2} ${x2},${y2}`;
    }

    const worldBox = world.getBoundingClientRect();
    function anchor(id, side) {
        const el = world.querySelector('.cbox[data-id="' + CSS.escape(id) + '"] > .cbox-head');
        if (!el) return null;
        const r = el.getBoundingClientRect();
        return {
            x: (side === 'left' ? r.left : r.right) - worldBox.left,
            y: (r.top + r.height / 2) - worldBox.top
        };
    }

    edges.forEach(({ from, to, label }) => {
        const a = anchor(to, 'right');     // source (data origin) right edge
        const b = anchor(from, 'left');    // consumer left edge
        if (!a || !b) return;
        const path = document.createElementNS('http://www.w3.org/2000/svg', 'path');
        path.setAttribute('d', bezier(a.x, a.y, b.x, b.y));
        path.setAttribute('class', 'noodle-data');
        path.setAttribute('title', label);
        svg.appendChild(path);
    });
}

function showCanvasSidebar(mod, modules, sidebar) {
    sidebar.innerHTML = '';
    sidebar.style.display = 'block';
    const closeBtn = document.createElement('button');
    closeBtn.className = 'csb-close';
    closeBtn.textContent = '✕';
    closeBtn.onclick = () => { sidebar.style.display = 'none'; };
    sidebar.appendChild(closeBtn);

    // Minimal info: id, type, parent
    const info = document.createElement('div');
    info.className = 'csb-info';
    info.innerHTML = '<strong>' + esc(mod.id) + '</strong>'
        + '<span class="csb-type">' + esc(mod.type || '') + '</span>'
        + (mod.parent_id ? '<span class="csb-parent">⬆ ' + esc(mod.parent_id) + '</span>' : '');
    sidebar.appendChild(info);

    // Controls list
    (mod.controls || []).forEach(c => {
        const row = document.createElement('div');
        row.className = 'csb-ctrl';
        row.textContent = c.key + ': ' + (c.value !== undefined ? c.value : '');
        sidebar.appendChild(row);
    });
}

function initCanvasEvents() {
    const viewport = document.getElementById('canvas-viewport');
    if (!viewport) return;

    // Pan on background mousedown
    viewport.addEventListener('mousedown', e => {
        if (e.button === 0 && e.target === viewport) {
            canvasDrag = { type: 'pan', ox: e.clientX - canvasPan.x, oy: e.clientY - canvasPan.y };
        }
    });

    window.addEventListener('mousemove', e => {
        if (!canvasDrag) return;
        if (canvasDrag.type === 'pan') {
            canvasPan.x = e.clientX - canvasDrag.ox;
            canvasPan.y = e.clientY - canvasDrag.oy;
            const world = document.getElementById('canvas-world');
            const svg   = document.getElementById('canvas-svg');
            if (world) world.style.transform = `translate(${canvasPan.x}px,${canvasPan.y}px)`;
            if (svg)   svg.style.transform   = `translate(${canvasPan.x}px,${canvasPan.y}px)`;
        } else if (canvasDrag.type === 'box') {
            const x = e.clientX - canvasDrag.ox;
            const y = e.clientY - canvasDrag.oy;
            const box = document.querySelector('.cbox[data-id="' + CSS.escape(canvasDrag.id) + '"]');
            if (box) { box.style.left = x + 'px'; box.style.top = y + 'px'; drawCanvasNoodles(); }
        }
    });

    window.addEventListener('mouseup', e => {
        if (!canvasDrag) return;
        if (canvasDrag.type === 'box') {
            const box = document.querySelector('.cbox[data-id="' + CSS.escape(canvasDrag.id) + '"]');
            if (box) {
                saveCanvasPos(canvasDrag.id, parseInt(box.style.left), parseInt(box.style.top));
                drawCanvasNoodles();
            }
        }
        canvasDrag = null;
    });

    // Zoom with wheel
    viewport.addEventListener('wheel', e => {
        e.preventDefault();
        // simple pan-only on wheel (zoom kept for future sprint)
        canvasPan.x -= e.deltaX * 0.5;
        canvasPan.y -= e.deltaY * 0.5;
        const world = document.getElementById('canvas-world');
        const svg   = document.getElementById('canvas-svg');
        if (world) world.style.transform = `translate(${canvasPan.x}px,${canvasPan.y}px)`;
        if (svg)   svg.style.transform   = `translate(${canvasPan.x}px,${canvasPan.y}px)`;
    }, { passive: false });
}

function toggleView(toCanvas) {
    canvasView = toCanvas;
    document.getElementById('modules').style.display    = toCanvas ? 'none' : '';
    document.getElementById('canvas-view').style.display = toCanvas ? '' : 'none';
    document.getElementById('view-tree-btn').classList.toggle('active', !toCanvas);
    document.getElementById('view-canvas-btn').classList.toggle('active', toCanvas);
    if (toCanvas && window._lastModules) renderCanvas(window._lastModules);
}

/* ============================================================
   Init
   ============================================================ */
document.addEventListener('DOMContentLoaded', () => {
    // Set favicon from brand image so the base64 is declared only once.
    const brandImg = document.querySelector('#app-brand img');
    if (brandImg) {
        const favicon = document.createElement('link');
        favicon.rel = 'icon';
        favicon.type = 'image/png';
        favicon.href = brandImg.src;
        document.head.appendChild(favicon);
    }
    initGL();
    loadTypes();
    loadModules();
    connectWs();
    loadHealth();
    document.getElementById('log-clear').onclick = () => {
        document.getElementById('log-output').innerHTML = '';
        logLineCount = 0;
        logAtBottom = true;
        document.getElementById('log-count').textContent = '0';
    };
    const logOut = document.getElementById('log-output');
    if (logOut) logOut.addEventListener('scroll', () => {
        logAtBottom = logOut.scrollTop + logOut.clientHeight >= logOut.scrollHeight - 5;
    }, { passive: true });
    setInterval(loadHealth, 30000);

    // Theme
    applyTheme(curTheme);
    document.getElementById('theme-btn').onclick = () =>
        applyTheme(curTheme === 'dark' ? 'light' : 'dark');

    // System status panel
    loadSystemStatus();
    setInterval(loadSystemStatus, 2000);

    document.getElementById('add-root-btn').onclick = () => {
        const area = document.getElementById('root-picker-area');
        showTypePicker(null, area, document.getElementById('add-root-btn'), null);
    };

    document.getElementById('hamburger').onclick = openNav;
    document.getElementById('nav-overlay').onclick = closeNav;

    document.getElementById('view-tree-btn').onclick = () => toggleView(false);
    document.getElementById('view-canvas-btn').onclick = () => toggleView(true);
    initCanvasEvents();

    document.getElementById('reconnect-btn').onclick = () => {
        wsRetryDelay = 500;
        connectWs();
    };

    document.getElementById('reboot-btn').onclick = async () => {
        if (!confirm('Reboot device?')) return;
        await fetch('/api/reboot', { method: 'POST' });
    };

    // Pause rendering when the tab is hidden; keep the socket alive to avoid
    // Safari's slow reconnect penalty. Resume immediately on show.
    document.addEventListener('visibilitychange', () => {
        if (document.hidden) {
            wsPaused = true;
        } else {
            wsPaused = false;
            wsRetryDelay = 500;
            if (!wsConn || wsConn.readyState === WebSocket.CLOSED ||
                           wsConn.readyState === WebSocket.CLOSING) {
                connectWs();
            }
        }
    });

    // Preview shrinks from full to 50% as user scrolls (over SHRINK_SCROLL px).
    // Drives max-height on the canvas so the browser CSS engine handles the
    // aspect ratio (width:100%; aspect-ratio:1/1 stays square automatically).
    // The section height follows the canvas with no extra surrounding space.
    // WebGL buffer is resynced only when the rendered size actually changes.
    const previewCanvas = document.getElementById('preview');
    let naturalMaxH  = null;   // canvas height at rest; captured on first scroll
    let rafShrink    = null;
    const SHRINK_SCROLL = 300; // scroll distance (px) over which canvas halves

    function applyPreviewShrink() {
        rafShrink = null;
        if (!naturalMaxH)
            naturalMaxH = previewCanvas.getBoundingClientRect().height || window.innerHeight * 0.5;
        const ratio = Math.min(window.scrollY / SHRINK_SCROLL, 1);
        previewCanvas.style.maxHeight = Math.round(naturalMaxH * (1 - ratio * 0.5)) + 'px';
        // Sync WebGL buffer to actual CSS-rendered size; redraw only when changed.
        const r = previewCanvas.getBoundingClientRect();
        const w = Math.round(r.width), h = Math.round(r.height);
        if (w > 0 && h > 0 && (previewCanvas.width !== w || previewCanvas.height !== h)) {
            previewCanvas.width  = w;
            previewCanvas.height = h;
            redraw3d();
        }
    }
    window.addEventListener('scroll', () => {
        if (!rafShrink) rafShrink = requestAnimationFrame(applyPreviewShrink);
    }, { passive: true });
    window.addEventListener('resize', () => {
        naturalMaxH = null;
        previewCanvas.style.maxHeight = '';
        requestAnimationFrame(applyPreviewShrink);
    }, { passive: true });

    // Safari restores pages from bfcache on back/forward; DOMContentLoaded
    // does not re-fire but pageshow does.
    window.addEventListener('pageshow', (e) => {
        if (e.persisted) {
            wsPaused = false;
            wsRetryDelay = 500;
            connectWs();
        }
    });
});
