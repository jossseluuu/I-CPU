// I-CPU - interfaz web. Habla con el servidor local (/api/*) y dibuja los
// resultados. Todo texto que viene del usuario o del servidor se inserta con
// textContent o atributos (nunca innerHTML). Sin dependencias externas.
'use strict';

// ===========================================================================
// Sesion y API
// ===========================================================================
// El token llega en el fragmento de la URL (#t=...) y se guarda en
// sessionStorage para sobrevivir a una recarga de la pestana.
const TOKEN = (() => {
  const fromHash = new URLSearchParams(location.hash.slice(1)).get('t');
  try {
    if (fromHash) sessionStorage.setItem('icpu-token', fromHash);
    return fromHash || sessionStorage.getItem('icpu-token') || '';
  } catch {
    return fromHash || '';
  }
})();
if (location.hash) history.replaceState(null, '', location.pathname);

async function request(path, form) {
  try {
    return await fetch(path, { method: form ? 'POST' : 'GET', headers: { 'X-ICPU-Token': TOKEN }, body: form });
  } catch {
    throw new Error('No hay conexión con I-CPU. ¿Se ha cerrado el programa?');
  }
}

async function api(path, form) {
  const res = await request(path, form);
  let data;
  try { data = await res.json(); } catch { throw new Error(`Respuesta inesperada del servidor (HTTP ${res.status})`); }
  if (!res.ok) throw new Error(data.error || `Error HTTP ${res.status}`);
  return data;
}

// Respuesta binaria (vistas previas PNG).
async function apiBlob(path, form) {
  const res = await request(path, form);
  if (!res.ok) {
    let msg = `Error HTTP ${res.status}`;
    try { msg = (await res.json()).error || msg; } catch { /* sin cuerpo JSON */ }
    throw new Error(msg);
  }
  return res.blob();
}

// Respuesta en streaming, una linea JSON por evento (experimento).
async function apiStream(path, form, onEvent) {
  const res = await request(path, form);
  if (!res.ok) {
    let msg = `Error HTTP ${res.status}`;
    try { msg = (await res.json()).error || msg; } catch { /* sin cuerpo JSON */ }
    throw new Error(msg);
  }
  const reader = res.body.getReader();
  const decoder = new TextDecoder();
  let buffer = '';
  for (;;) {
    const { value, done } = await reader.read();
    if (value) buffer += decoder.decode(value, { stream: true });
    let nl;
    while ((nl = buffer.indexOf('\n')) >= 0) {
      const line = buffer.slice(0, nl).trim();
      buffer = buffer.slice(nl + 1);
      if (line) onEvent(JSON.parse(line));
    }
    if (done) break;
  }
}

// ===========================================================================
// Utilidades de DOM y formato
// ===========================================================================
const $ = (id) => document.getElementById(id);

function h(tag, props, ...children) {
  const el = document.createElement(tag);
  for (const [k, v] of Object.entries(props || {})) {
    if (v == null || v === false) continue;
    if (k === 'class') el.className = v;
    else if (k === 'text') el.textContent = v;
    else if (k.startsWith('on')) el.addEventListener(k.slice(2), v);
    else el.setAttribute(k, v === true ? '' : v);
  }
  for (const c of children.flat()) {
    if (c == null || c === false) continue;
    el.append(c instanceof Node ? c : document.createTextNode(String(c)));
  }
  return el;
}

const SVG_NS = 'http://www.w3.org/2000/svg';
function s(tag, attrs, ...children) {
  const el = document.createElementNS(SVG_NS, tag);
  for (const [k, v] of Object.entries(attrs || {})) if (v != null) el.setAttribute(k, v);
  for (const c of children.flat()) if (c != null) el.append(c instanceof Node ? c : document.createTextNode(String(c)));
  return el;
}

function fmtBytes(n) {
  if (n < 1024) return `${n} B`;
  if (n < 1024 * 1024) return `${(n / 1024).toFixed(1).replace('.', ',')} KiB`;
  return `${(n / 1024 / 1024).toFixed(2).replace('.', ',')} MiB`;
}
const fmtNum = (v, d = 3) => (v == null || v < 0 ? '–' : Number(v).toFixed(d).replace('.', ','));
const fmtPct = (r, d = 1) => `${(r * 100).toFixed(d).replace('.', ',')} %`;
const fmtPsnr = (v) => (v == null ? '∞' : `${v.toFixed(2).replace('.', ',')} dB`); // null = identicas
const fmtInt = (n) => Number(n).toLocaleString('es');
const fmtTime = (t) => new Date(t).toLocaleString('es', { dateStyle: 'short', timeStyle: 'short' });
const utf8Length = (str) => new TextEncoder().encode(str).length;

function base64ToBytes(b64) {
  const bin = atob(b64);
  const out = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i);
  return out;
}
const blobUrl = (bytes, type) => URL.createObjectURL(new Blob([bytes], { type }));
const pngUrl = (b64) => blobUrl(base64ToBytes(b64), 'image/png');

function downloadBlob(blob, filename) {
  const url = URL.createObjectURL(blob);
  const a = h('a', { href: url, download: filename });
  document.body.append(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 10000);
}
const downloadBytes = (bytes, filename, mime = 'application/octet-stream') => downloadBlob(new Blob([bytes], { type: mime }), filename);

function toast(text, kind = '') {
  const el = h('div', { class: `toast ${kind}`, role: kind === 'bad' ? 'alert' : 'status' },
    icon(kind === 'bad' ? 'alert' : 'check'), h('span', { text }));
  $('toasts').append(el);
  setTimeout(() => { el.classList.add('leaving'); setTimeout(() => el.remove(), 220); }, kind === 'bad' ? 6000 : 3200);
}

// Boton ocupado mientras dura una operacion; los errores se muestran como aviso.
async function withBusy(button, busyText, fn) {
  if (button.disabled) return;
  const original = [...button.childNodes];
  button.disabled = true;
  button.replaceChildren(h('span', { class: 'spinner', 'aria-hidden': 'true' }), busyText);
  try {
    await fn();
  } catch (e) {
    toast(e.message, 'bad');
  } finally {
    button.disabled = false;
    button.replaceChildren(...original);
  }
}

// ===========================================================================
// Iconos (SVG propios, trazo de 24x24)
// ===========================================================================
const ICONS = {
  lock: ['rect:3,11,18,10,2', 'M7 11V7a5 5 0 0 1 10 0v4'],
  key: ['circle:7.5,15.5,4.5', 'M10.7 12.3 20 3M16 7l3 3M14 9l2 2'],
  chart: ['M3 3v18h18', 'M7 15l4-4 3 3 5-6'],
  flask: ['M9 3h6M10 3v6L4.6 18.5A1.7 1.7 0 0 0 6.1 21h11.8a1.7 1.7 0 0 0 1.5-2.5L14 9V3', 'M7.5 15h9'],
  clock: ['circle:12,12,9', 'M12 7v5l3 2'],
  sun: ['circle:12,12,4', 'M12 2v2M12 20v2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M2 12h2M20 12h2M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4'],
  moon: ['M20.5 14.5A8.5 8.5 0 1 1 9.5 3.5a7 7 0 0 0 11 11z'],
  monitor: ['rect:2,4,20,13,2', 'M8 21h8M12 17v4'],
  keyboard: ['rect:2,5,20,14,2', 'M6 9h.01M10 9h.01M14 9h.01M18 9h.01M6 13h.01M10 13h.01M14 13h.01M18 13h.01M8 16.5h8'],
  power: ['M12 3v9', 'M18.4 6.6a9 9 0 1 1-12.8 0'],
  upload: ['M12 15V4M7.5 8.5 12 4l4.5 4.5', 'M4 15v3a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2v-3'],
  download: ['M12 4v11M7.5 10.5 12 15l4.5-4.5', 'M4 15v3a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2v-3'],
  copy: ['rect:9,9,12,12,2', 'M5 15H4a1 1 0 0 1-1-1V4a1 1 0 0 1 1-1h10a1 1 0 0 1 1 1v1'],
  check: ['M20 6 9 17l-5-5'],
  alert: ['M10.3 3.9 1.8 18a2 2 0 0 0 1.7 3h17a2 2 0 0 0 1.7-3L13.7 3.9a2 2 0 0 0-3.4 0z', 'M12 9v4M12 17h.01'],
  x: ['M18 6 6 18M6 6l12 12'],
  zoom: ['circle:11,11,7', 'm20 20-4-4M11 8v6M8 11h6'],
  image: ['rect:3,3,18,18,2', 'circle:9,9,2', 'm21 15-5-5L5 21'],
  file: ['M14 3H6a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V9z', 'M14 3v6h6'],
  sparkles: ['M12 3l1.8 4.9L18.7 9.7l-4.9 1.8L12 16.4l-1.8-4.9L5.3 9.7l4.9-1.8z', 'M19 15l.8 2.2L22 18l-2.2.8L19 21l-.8-2.2L16 18l2.2-.8z'],
  eye: ['M2 12s3.6-7 10-7 10 7 10 7-3.6 7-10 7S2 12 2 12z', 'circle:12,12,3'],
  'eye-off': ['M3 3l18 18', 'M10.6 5.1A10 10 0 0 1 12 5c6.4 0 10 7 10 7a17 17 0 0 1-2.9 3.8M6.6 6.6C3.8 8.4 2 12 2 12s3.6 7 10 7a9.6 9.6 0 0 0 5.4-1.6', 'M9.9 9.9a3 3 0 0 0 4.2 4.2'],
  trash: ['M3 6h18M8 6V4h8v2', 'M5.5 6l1 14h11l1-14'],
  arrow: ['M5 12h14M13 6l6 6-6 6'],
  layers: ['M12 3 2 8l10 5 10-5-10-5z', 'm2 16 10 5 10-5M2 12l10 5 10-5'],
  columns: ['rect:3,3,18,18,2', 'M12 3v18'],
  'image-down': ['M12 3v9M8.5 8.5 12 12l3.5-3.5', 'M4 14v4a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2v-4'],
};

function icon(name) {
  const svg = s('svg', { class: 'icon', viewBox: '0 0 24 24', 'aria-hidden': 'true' });
  for (const part of ICONS[name] || []) {
    if (part.startsWith('rect:')) {
      const [x, y, w, hh, r] = part.slice(5).split(',');
      svg.append(s('rect', { x, y, width: w, height: hh, rx: r }));
    } else if (part.startsWith('circle:')) {
      const [cx, cy, r] = part.slice(7).split(',');
      svg.append(s('circle', { cx, cy, r }));
    } else {
      svg.append(s('path', { d: part }));
    }
  }
  return svg;
}
function hydrateIcons(root = document) {
  for (const el of root.querySelectorAll('[data-icon]')) {
    if (!el.firstChild || el.firstChild.nodeName !== 'svg') el.prepend(icon(el.dataset.icon));
  }
}

// ===========================================================================
// Tema: automatico (sistema) -> claro -> oscuro
// ===========================================================================
const THEMES = [
  { id: 'system', icon: 'monitor', label: 'automático' },
  { id: 'light', icon: 'sun', label: 'claro' },
  { id: 'dark', icon: 'moon', label: 'oscuro' },
];
function applyTheme(id) {
  const t = THEMES.find((x) => x.id === id) || THEMES[0];
  if (t.id === 'system') document.documentElement.removeAttribute('data-theme');
  else document.documentElement.dataset.theme = t.id;
  const btn = $('theme-btn');
  btn.replaceChildren(icon(t.icon));
  btn.title = `Tema: ${t.label} (clic para cambiar)`;
  try { localStorage.setItem('icpu-theme', t.id); } catch { /* sin almacenamiento */ }
}
$('theme-btn').addEventListener('click', () => {
  const current = document.documentElement.dataset.theme || 'system';
  applyTheme(THEMES[(THEMES.findIndex((t) => t.id === current) + 1) % THEMES.length].id);
});

// ===========================================================================
// Pestanas
// ===========================================================================
const TABS = ['embed', 'extract', 'analyze', 'bench'];
const PRIMARY_INPUT = { embed: 'embed-cover', extract: 'extract-image', analyze: 'analyze-image', bench: 'bench-cover' };
const RUN_BUTTON = { embed: 'embed-run', extract: 'extract-run', analyze: 'analyze-run', bench: 'bench-run' };
const DROP_LABEL = { embed: 'imagen portadora', extract: 'imagen con el mensaje', analyze: 'imagen a analizar', bench: 'portadora del experimento' };
let currentTab = 'embed';

function showTab(name) {
  currentTab = name;
  for (const btn of document.querySelectorAll('.tabs button')) {
    const on = btn.dataset.tab === name;
    btn.setAttribute('aria-selected', String(on));
    btn.tabIndex = on ? 0 : -1;
    $(`tab-${btn.dataset.tab}`).hidden = !on;
  }
  try { sessionStorage.setItem('icpu-tab', name); } catch { /* sin almacenamiento */ }
}
for (const btn of document.querySelectorAll('.tabs button')) btn.addEventListener('click', () => showTab(btn.dataset.tab));
document.querySelector('.tabs').addEventListener('keydown', (e) => {
  if (e.key !== 'ArrowRight' && e.key !== 'ArrowLeft') return;
  const i = TABS.indexOf(currentTab) + (e.key === 'ArrowRight' ? 1 : -1);
  showTab(TABS[(i + TABS.length) % TABS.length]);
  $(`tabbtn-${currentTab}`).focus();
});

// ===========================================================================
// Historial (persistente: solo metadatos; descargas solo en memoria)
// ===========================================================================
const HISTORY_KEY = 'icpu-history';
const HISTORY_MAX = 60;
const downloads = new Map(); // id -> { bytes, name, mime } (solo esta sesion)

function loadHistory() {
  try { return JSON.parse(localStorage.getItem(HISTORY_KEY) || '[]'); } catch { return []; }
}
function saveHistory(list) {
  try { localStorage.setItem(HISTORY_KEY, JSON.stringify(list.slice(0, HISTORY_MAX))); } catch { /* sin almacenamiento */ }
}
function addHistory(entry, download) {
  const id = `${Date.now()}-${Math.random().toString(36).slice(2, 8)}`;
  if (download) downloads.set(id, download);
  saveHistory([{ id, time: Date.now(), ...entry }, ...loadHistory()]);
  renderHistory();
}
const HISTORY_META = {
  embed: { icon: 'lock', label: 'Ocultado' },
  extract: { icon: 'key', label: 'Extraído' },
  analyze: { icon: 'chart', label: 'Analizado' },
  bench: { icon: 'flask', label: 'Experimento' },
};
function renderHistory() {
  const list = loadHistory();
  const count = $('history-count');
  count.hidden = list.length === 0;
  count.textContent = list.length > 99 ? '99+' : String(list.length);
  const ol = $('history-list');
  if (!list.length) {
    ol.replaceChildren(h('li', { class: 'h-empty', text: 'Todavía no hay operaciones.' }));
    return;
  }
  ol.replaceChildren(...list.map((e) => {
    const meta = HISTORY_META[e.type] || HISTORY_META.analyze;
    const dl = downloads.get(e.id);
    return h('li', null,
      h('div', { class: `h-icon ${e.type}` }, icon(meta.icon)),
      h('div', { class: 'h-main' },
        h('div', { class: 'h-title', text: `${meta.label}: ${e.title}` }),
        h('div', { class: 'h-detail', text: `${e.detail} · ${fmtTime(e.time)}` })),
      dl ? h('button', { class: 'icon-btn', title: `Descargar ${dl.name}`, 'aria-label': `Descargar ${dl.name}`,
        onclick: () => downloadBytes(dl.bytes, dl.name, dl.mime) }, icon('download')) : null);
  }));
}
function toggleHistory(open = $('history').hidden) {
  $('history').hidden = !open;
  $('scrim').hidden = !open;
  $('history-btn').classList.toggle('active', open);
  if (open) { renderHistory(); $('history-close').focus(); }
}
$('history-btn').addEventListener('click', () => toggleHistory());
$('history-close').addEventListener('click', () => toggleHistory(false));
$('scrim').addEventListener('click', () => toggleHistory(false));
$('history-clear').addEventListener('click', () => {
  if (!confirm('¿Borrar todo el historial de este navegador?')) return;
  saveHistory([]);
  downloads.clear();
  renderHistory();
  toast('Historial borrado');
});

// ===========================================================================
// Ficheros: zonas de arrastre, soltar en cualquier parte y pegar
// ===========================================================================
const fileHandlers = {}; // id del <input type=file> -> funcion(file)

// Con un fichero ya elegido, la zona se reduce a una franja "Cambiar…".
function compactDrop(drop, text) {
  drop.classList.add('has-file');
  drop.querySelector('.drop-text').textContent = text;
}

function setupDrop(dropId, inputId, onFile, changeText = 'Cambiar: arrastra otra imagen o haz clic') {
  const drop = $(dropId);
  const input = $(inputId);
  const handle = (file) => {
    compactDrop(drop, typeof changeText === 'function' ? changeText(file) : changeText);
    onFile(file);
  };
  fileHandlers[inputId] = handle;
  input.addEventListener('change', () => {
    if (input.files[0]) handle(input.files[0]);
    input.value = '';
  });
  drop.addEventListener('click', (e) => { if (e.target !== input) input.click(); });
  drop.addEventListener('keydown', (e) => {
    if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); input.click(); }
  });
}

function routeFile(file, target) {
  const drop = target && target.closest ? target.closest('.drop') : null;
  const inputId = drop && !drop.hidden && drop.offsetParent ? drop.dataset.input : PRIMARY_INPUT[currentTab];
  fileHandlers[inputId](file);
}

let dragDepth = 0;
window.addEventListener('dragenter', (e) => {
  if (![...(e.dataTransfer?.types || [])].includes('Files')) return;
  dragDepth++;
  $('drop-overlay-text').textContent = `Suelta para usarla como ${DROP_LABEL[currentTab]}`;
  $('drop-overlay').hidden = false;
});
window.addEventListener('dragleave', () => { if (--dragDepth <= 0) { dragDepth = 0; $('drop-overlay').hidden = true; } });
window.addEventListener('dragover', (e) => e.preventDefault());
window.addEventListener('drop', (e) => {
  e.preventDefault();
  dragDepth = 0;
  $('drop-overlay').hidden = true;
  const file = e.dataTransfer?.files?.[0];
  if (file) routeFile(file, document.elementFromPoint(e.clientX, e.clientY));
});
window.addEventListener('paste', (e) => {
  const item = [...(e.clipboardData?.items || [])].find((i) => i.kind === 'file' && i.type.startsWith('image/'));
  if (!item) return; // texto normal: que lo gestione el campo con el foco
  e.preventDefault();
  const blob = item.getAsFile();
  const ext = (blob.type.split('/')[1] || 'png').replace('jpeg', 'jpg');
  routeFile(new File([blob], `pegada-${new Date().toISOString().slice(11, 19).replace(/:/g, '')}.${ext}`, { type: blob.type }));
  toast('Imagen pegada desde el portapapeles');
});

// Vista previa: el navegador muestra PNG/JPEG/GIF/BMP/WebP; el resto lo convierte el servidor.
const BROWSER_IMAGE = /\.(png|jpe?g|gif|bmp|webp)$/i;
const previewCache = new WeakMap();
function displayUrl(file) {
  if (!previewCache.has(file)) {
    previewCache.set(file, BROWSER_IMAGE.test(file.name)
      ? Promise.resolve(URL.createObjectURL(file))
      : (() => { const f = new FormData(); f.append('image', file); return apiBlob('/api/preview', f).then((b) => URL.createObjectURL(b)); })());
  }
  return previewCache.get(file);
}

function describeChips(info) {
  const chips = [info.format, `${info.width} × ${info.height} px`, `${info.channels} canal${info.channels > 1 ? 'es' : ''} · ${info.depth} bits`,
    fmtBytes(info.file_bytes)];
  if (info.frames > 1) chips.push(`${info.frames} fotogramas`);
  if (info.jpeg_quality >= 0) chips.push(`calidad ≈ ${info.jpeg_quality}${info.jpeg_progressive ? ' · progresivo' : ''}`);
  if (info.jpeg_subsampling) chips.push(`croma ${info.jpeg_subsampling}`);
  return chips;
}

// Tarjeta del fichero elegido; se rellena con la vista previa y los datos al llegar.
function fileCard(file, { chips = [], onRemove } = {}) {
  const thumbSlot = h('div', { class: 'thumb-placeholder' }, icon('image'));
  const chipBox = h('div', { class: 'chips' }, chips.map((c) => h('span', { class: 'chip-tag', text: c })));
  const card = h('div', { class: 'file-card' }, thumbSlot,
    h('div', { class: 'file-meta' }, h('div', { class: 'file-name', text: file.name }), chipBox,
      onRemove ? h('div', { class: 'file-actions' },
        h('button', { class: 'btn ghost small', onclick: onRemove }, icon('x'), 'Quitar')) : null));
  displayUrl(file).then((url) => {
    const img = h('img', { class: 'thumb', alt: '', src: url });
    img.addEventListener('error', () => img.replaceWith(h('div', { class: 'thumb-placeholder', text: 'Sin vista previa' })));
    thumbSlot.replaceWith(img);
  }).catch(() => thumbSlot.replaceWith(h('div', { class: 'thumb-placeholder', text: 'Sin vista previa' })));
  card.setChips = (list) => chipBox.replaceChildren(...list.map((c) => h('span', { class: 'chip-tag', text: c })));
  return card;
}

async function loadInfo(file) {
  const form = new FormData();
  form.append('image', file);
  return api('/api/info', form);
}

// ===========================================================================
// Visor/comparador: deslizador antes/despues, diferencias y lupa de pixeles
// ===========================================================================
function loadImage(url) {
  return new Promise((resolve, reject) => {
    const img = new Image();
    img.onload = () => resolve(img);
    img.onerror = () => reject(new Error('No se pudo cargar la imagen'));
    img.src = url;
  });
}

// opts: { before, after?, diff?, beforeLabel, afterLabel, title }
function createViewer(opts) {
  const modes = [];
  if (opts.after) modes.push({ id: 'slider', icon: 'columns', label: 'Antes / después' });
  if (opts.after) modes.push({ id: 'single', icon: 'image', label: opts.afterLabel || 'Con mensaje' });
  if (opts.diff) modes.push({ id: 'diff', icon: 'layers', label: 'Diferencias ×64' });

  const baseImg = h('img', { alt: opts.beforeLabel || 'Imagen', src: opts.before, draggable: 'false' });
  const afterImg = h('img', { alt: opts.afterLabel || '', src: opts.after || opts.before, draggable: 'false' });
  const afterLayer = h('div', { class: 'layer' }, afterImg);
  const handle = h('div', { class: 'handle' });
  const tagL = h('span', { class: 'tag left', text: opts.beforeLabel || 'Original' });
  const tagR = h('span', { class: 'tag right', text: opts.afterLabel || 'Con mensaje' });
  const loupe = h('canvas', { class: 'loupe', width: 168, height: 168, hidden: true });
  const loupeInfo = h('div', { class: 'loupe-info', hidden: true });
  const stage = h('div', { class: 'compare' }, baseImg, afterLayer, handle, tagL, tagR, loupe, loupeInfo);

  let mode = modes.length ? modes[0].id : 'single';
  const hintEl = h('p', { class: 'viewer-hint' });
  const HINTS = {
    slider: 'Arrastra el divisor (o usa las flechas) para comparar · la lupa muestra cada píxel y su color',
    single: 'Activa la lupa para ver píxeles individuales y su color',
    diff: 'Diferencia absoluta ×64: cada punto es una muestra que ha cambiado',
  };
  let split = 50;
  let loupeOn = false;
  const pixelCache = new Map(); // url -> {img, data}

  function render() {
    stage.classList.toggle('single', mode !== 'slider');
    stage.classList.toggle('pixelated', mode === 'diff');
    if (mode === 'slider') {
      baseImg.src = opts.before;
      afterLayer.hidden = false;
      afterLayer.style.clipPath = `inset(0 0 0 ${split}%)`;
      handle.style.left = `${split}%`;
    } else {
      baseImg.src = mode === 'diff' ? opts.diff : (opts.after || opts.before);
      afterLayer.hidden = true;
    }
    for (const b of modeButtons) b.setAttribute('aria-pressed', String(b.dataset.mode === mode));
    hintEl.textContent = HINTS[mode];
  }

  function setSplitFromEvent(e) {
    const r = stage.getBoundingClientRect();
    split = Math.max(0, Math.min(100, ((e.clientX - r.left) / r.width) * 100));
    render();
  }
  let dragging = false;
  stage.addEventListener('pointerdown', (e) => {
    if (mode !== 'slider') return;
    dragging = true;
    stage.setPointerCapture(e.pointerId);
    setSplitFromEvent(e);
  });
  stage.addEventListener('pointerup', () => { dragging = false; });
  stage.addEventListener('pointermove', (e) => {
    if (dragging) setSplitFromEvent(e);
    if (loupeOn) drawLoupe(e);
  });
  stage.addEventListener('pointerleave', () => { loupe.hidden = true; loupeInfo.hidden = true; });
  stage.tabIndex = 0;
  stage.setAttribute('aria-label', 'Comparador de imágenes: flechas izquierda y derecha para mover el divisor');
  stage.addEventListener('keydown', (e) => {
    if (mode !== 'slider' || (e.key !== 'ArrowLeft' && e.key !== 'ArrowRight')) return;
    e.preventDefault();
    split = Math.max(0, Math.min(100, split + (e.key === 'ArrowRight' ? 5 : -5)));
    render();
  });

  async function pixels(url) {
    if (!pixelCache.has(url)) {
      pixelCache.set(url, loadImage(url).then((img) => {
        const c = document.createElement('canvas');
        c.width = img.naturalWidth;
        c.height = img.naturalHeight;
        const ctx = c.getContext('2d', { willReadFrequently: true });
        ctx.drawImage(img, 0, 0);
        return { img, ctx };
      }));
    }
    return pixelCache.get(url);
  }

  async function drawLoupe(e) {
    const r = stage.getBoundingClientRect();
    const fx = (e.clientX - r.left) / r.width;
    const fy = (e.clientY - r.top) / r.height;
    if (fx < 0 || fx > 1 || fy < 0 || fy > 1) return;
    const url = mode === 'slider' ? (fx * 100 < split ? opts.before : opts.after) : (mode === 'diff' ? opts.diff : (opts.after || opts.before));
    const { img, ctx: src } = await pixels(url);
    const px = Math.min(img.naturalWidth - 1, Math.floor(fx * img.naturalWidth));
    const py = Math.min(img.naturalHeight - 1, Math.floor(fy * img.naturalHeight));
    const zoom = 12;
    const size = 168;
    const span = size / zoom;
    const ctx = loupe.getContext('2d');
    ctx.imageSmoothingEnabled = false;
    ctx.fillStyle = '#000';
    ctx.fillRect(0, 0, size, size);
    ctx.drawImage(img, px - span / 2 + 0.5, py - span / 2 + 0.5, span, span, 0, 0, size, size);
    ctx.strokeStyle = 'rgba(255,255,255,.9)';
    ctx.lineWidth = 1.5;
    ctx.strokeRect(size / 2 - zoom / 2, size / 2 - zoom / 2, zoom, zoom);
    const [R, G, B] = src.getImageData(px, py, 1, 1).data;
    loupe.style.left = `${e.clientX - r.left}px`;
    loupe.style.top = `${e.clientY - r.top}px`;
    loupeInfo.style.left = `${e.clientX - r.left}px`;
    loupeInfo.style.top = `${e.clientY - r.top + 96}px`;
    loupeInfo.textContent = `(${px}, ${py})  R${R} G${G} B${B}`;
    loupe.hidden = false;
    loupeInfo.hidden = false;
  }

  const modeButtons = modes.length > 1 ? modes.map((m) => h('button', {
    class: 'btn ghost small', 'data-mode': m.id, 'aria-pressed': 'false', onclick: () => { mode = m.id; render(); },
  }, icon(m.icon), m.label)) : [];
  const loupeBtn = h('button', {
    class: 'btn ghost small', 'aria-pressed': 'false', title: 'Lupa de píxeles: pasa el ratón por la imagen',
    onclick: () => {
      loupeOn = !loupeOn;
      loupeBtn.setAttribute('aria-pressed', String(loupeOn));
      loupeBtn.classList.toggle('secondary', loupeOn);
      stage.style.cursor = loupeOn ? 'none' : '';
      if (!loupeOn) { loupe.hidden = true; loupeInfo.hidden = true; }
    },
  }, icon('zoom'), 'Lupa');

  const el = h('div', { class: 'viewer' },
    h('div', { class: 'viewer-toolbar' }, opts.title ? h('h3', { text: opts.title }) : null, ...modeButtons,
      h('span', { class: 'spacer' }), loupeBtn),
    stage, hintEl);
  render();
  return el;
}

// ===========================================================================
// Graficas SVG interactivas: leyenda que oculta series, tooltip y exportar PNG
// ===========================================================================
function niceTicks(min, max, count = 5) {
  const span = max - min || 1;
  const raw = span / count;
  const mag = 10 ** Math.floor(Math.log10(raw));
  const step = [1, 2, 2.5, 5, 10].map((m) => m * mag).find((st) => span / st <= count) || 10 * mag;
  const ticks = [];
  for (let v = Math.ceil(min / step) * step; v <= max + step * 1e-9; v += step) ticks.push(Number(v.toFixed(10)));
  return ticks;
}

function lineChart({ series, xLabel, yLabel, xDomain, yDomain, xFmt = (v) => v, yFmt = (v) => v, tipFmt, threshold, dots = true }) {
  const W = 600, H = 300, m = { l: 56, r: 16, t: 14, b: 44 };
  const pts = series.flatMap((se) => se.points);
  const [x0, x1] = xDomain || [Math.min(...pts.map((p) => p[0])), Math.max(...pts.map((p) => p[0]))];
  let [y0, y1] = yDomain || [Math.min(...pts.map((p) => p[1])), Math.max(...pts.map((p) => p[1]))];
  if (!yDomain) { const pad = (y1 - y0) * 0.08 || 1; y0 -= pad; y1 += pad; }
  const sx = (v) => m.l + ((v - x0) / (x1 - x0 || 1)) * (W - m.l - m.r);
  const sy = (v) => H - m.b - ((v - y0) / (y1 - y0 || 1)) * (H - m.t - m.b);
  tipFmt = tipFmt || yFmt;

  const svg = s('svg', { class: 'chart', viewBox: `0 0 ${W} ${H}`, role: 'img', 'aria-label': `${yLabel} frente a ${xLabel}` });
  for (const t of niceTicks(y0, y1)) {
    svg.append(s('line', { class: 'gridline', x1: m.l, x2: W - m.r, y1: sy(t), y2: sy(t) }));
    svg.append(s('text', { x: m.l - 8, y: sy(t) + 4, 'text-anchor': 'end' }, yFmt(t)));
  }
  for (const t of niceTicks(x0, x1, 6)) svg.append(s('text', { x: sx(t), y: H - m.b + 18, 'text-anchor': 'middle' }, xFmt(t)));
  svg.append(s('line', { class: 'axis', x1: m.l, x2: W - m.r, y1: H - m.b, y2: H - m.b }));
  if (threshold != null && threshold >= y0 && threshold <= y1) {
    svg.append(s('line', { class: 'threshold', x1: m.l, x2: W - m.r, y1: sy(threshold), y2: sy(threshold) }));
  }
  svg.append(s('text', { class: 'axis-title', x: (m.l + W - m.r) / 2, y: H - 6, 'text-anchor': 'middle' }, xLabel));
  svg.append(s('text', { class: 'axis-title', x: 14, y: (m.t + H - m.b) / 2, 'text-anchor': 'middle',
    transform: `rotate(-90 14 ${(m.t + H - m.b) / 2})` }, yLabel));

  const groups = series.map((se) => {
    const g = s('g', { class: 'series' });
    const d = se.points.map((p, i) => `${i ? 'L' : 'M'}${sx(p[0]).toFixed(1)},${sy(p[1]).toFixed(1)}`).join('');
    g.append(s('path', { class: `line ${se.cls}${se.dashed ? ' dashed' : ''}`, d }));
    if (dots) for (const p of se.points) g.append(s('circle', { class: `dot ${se.cls}`, cx: sx(p[0]), cy: sy(p[1]), r: 3.5 }));
    svg.append(g);
    return { se, g, visible: true };
  });

  // Tooltip: el valor de cada serie visible en la x mas cercana al puntero
  const guide = s('line', { class: 'guide', y1: m.t, y2: H - m.b, visibility: 'hidden' });
  const focus = s('g');
  svg.append(guide, focus);
  const tip = h('div', { class: 'chart-tip', hidden: true });
  const xs = [...new Set(pts.map((p) => p[0]))].sort((a, b) => a - b);
  svg.addEventListener('pointermove', (e) => {
    const r = svg.getBoundingClientRect();
    const vx = ((e.clientX - r.left) / r.width) * W;
    const dataX = x0 + ((vx - m.l) / (W - m.l - m.r)) * (x1 - x0);
    let best = xs[0];
    for (const x of xs) if (Math.abs(x - dataX) < Math.abs(best - dataX)) best = x;
    const rows = groups.filter((g) => g.visible).map((g) => [g.se, g.se.points.find((p) => p[0] === best)]).filter(([, p]) => p);
    if (!rows.length) { tip.hidden = true; return; }
    guide.setAttribute('x1', sx(best));
    guide.setAttribute('x2', sx(best));
    guide.setAttribute('visibility', 'visible');
    focus.replaceChildren(...rows.map(([se, p]) => s('circle', { class: `focus-dot ${se.cls}`, cx: sx(p[0]), cy: sy(p[1]), r: 5 })));
    tip.replaceChildren(h('div', { class: 'tip-title', text: `${xLabel}: ${xFmt(best)}` }),
      ...rows.slice(0, 12).map(([se, p]) => h('div', { class: 'tip-row' }, h('i', { class: se.cls }), se.name, h('b', { text: tipFmt(p[1]) }))));
    tip.hidden = false;
    const left = (sx(best) / W) * r.width;
    tip.style.left = `${left > r.width * 0.6 ? left - 170 : left}px`;
    tip.style.top = `${(e.clientY - r.top)}px`;
  });
  svg.addEventListener('pointerleave', () => { tip.hidden = true; guide.setAttribute('visibility', 'hidden'); focus.replaceChildren(); });

  const legend = series.length > 1 ? h('div', { class: 'legend' }, groups.map((g) => {
    const b = h('button', { 'aria-pressed': 'true', title: 'Mostrar u ocultar la serie',
      onclick: () => {
        g.visible = !g.visible;
        b.setAttribute('aria-pressed', String(g.visible));
        g.g.classList.toggle('off', !g.visible);
      } }, h('i', { class: `${g.se.cls}${g.se.dashed ? ' dashed' : ''}` }), g.se.name);
    return b;
  })) : null;

  const el = h('div', null, h('div', { class: 'chart-wrap' }, svg, tip), legend);
  el.svg = svg;
  return el;
}

// Exporta una grafica a PNG: se copian los estilos calculados (variables CSS
// del tema incluidas) a la copia del SVG antes de rasterizarla.
async function exportChartPng(svg, filename) {
  const clone = svg.cloneNode(true);
  const props = ['fill', 'stroke', 'stroke-width', 'stroke-dasharray', 'opacity', 'font-size', 'font-family', 'font-weight', 'visibility'];
  const src = [svg, ...svg.querySelectorAll('*')];
  const dst = [clone, ...clone.querySelectorAll('*')];
  src.forEach((node, i) => {
    const cs = getComputedStyle(node);
    dst[i].setAttribute('style', props.map((p) => `${p}:${cs.getPropertyValue(p)}`).join(';'));
  });
  const [, , W, H] = svg.getAttribute('viewBox').split(' ').map(Number);
  clone.setAttribute('width', W);
  clone.setAttribute('height', H);
  const bg = s('rect', { x: 0, y: 0, width: W, height: H });
  bg.setAttribute('style', `fill:${getComputedStyle(document.body).backgroundColor}`);
  clone.insertBefore(bg, clone.firstChild);
  const url = URL.createObjectURL(new Blob([new XMLSerializer().serializeToString(clone)], { type: 'image/svg+xml' }));
  try {
    const img = await loadImage(url);
    const canvas = h('canvas', { width: W * 2, height: H * 2 });
    const ctx = canvas.getContext('2d');
    ctx.scale(2, 2);
    ctx.drawImage(img, 0, 0, W, H);
    canvas.toBlob((blob) => downloadBlob(blob, filename), 'image/png');
  } finally {
    URL.revokeObjectURL(url);
  }
}

function chartCard(title, chartOpts, filename) {
  const chart = lineChart(chartOpts);
  return h('section', { class: 'card chart-card' },
    h('div', { class: 'chart-head' }, h('h3', { text: title }),
      h('button', { class: 'icon-btn', title: 'Descargar como PNG', 'aria-label': `Descargar ${title} como PNG`,
        onclick: () => exportChartPng(chart.svg, filename).catch((e) => toast(e.message, 'bad')) }, icon('image-down'))),
    chart);
}

const stat = (label, value, sub) => h('div', { class: 'stat' }, h('dt', { text: label }), h('dd', null, value, sub ? h('small', { text: sub }) : null));

function verdictCard(label, value, verdict, note) {
  const level = /muy probable|insercion probable/.test(verdict) ? 'bad' : /dudoso/.test(verdict) ? 'warn' : 'ok';
  const text = { 'insercion muy probable': 'Inserción muy probable', 'insercion probable': 'Inserción probable',
    dudoso: 'Dudoso', 'sin indicios': 'Sin indicios' }[verdict] || verdict;
  return h('div', { class: 'verdict' }, h('div', { class: 'label', text: label }), h('div', { class: 'value', text: value }),
    h('span', { class: `tag ${level}` }, icon(level === 'ok' ? 'check' : 'alert'), text), note ? h('p', { text: note }) : null);
}

function resultHead(iconName, title, subtitle, kind = 'ok') {
  const badge = h('div', { class: 'badge-icon' }, icon(iconName));
  if (kind !== 'ok') { badge.style.background = `var(--${kind}-soft)`; badge.style.color = `var(--${kind})`; }
  return h('div', { class: 'result-head' }, badge, h('div', null, h('h2', { text: title }), subtitle ? h('p', { text: subtitle }) : null));
}

// ===========================================================================
// OCULTAR
// ===========================================================================
const embedState = { cover: null, info: null, payload: null, card: null };

setupDrop('embed-cover-drop', 'embed-cover', async (file) => {
  embedState.cover = file;
  embedState.info = null;
  const box = $('embed-cover-info');
  const card = fileCard(file, { chips: ['Leyendo…'] });
  box.replaceChildren(card);
  try {
    embedState.info = await loadInfo(file);
    card.setChips(describeChips(embedState.info));
    box.append(capacityTable(embedState.info));
  } catch (e) {
    embedState.cover = null;
    card.setChips([]);
    box.append(h('div', { class: 'callout bad' }, icon('alert'), h('p', { text: e.message })));
  }
  updateEmbedMeter();
});

function capacityTable(info) {
  const rows = info.capacities.map((c) => h('tr', null,
    h('td', { text: c.output }), h('td', { text: c.domain }), h('td', { class: 'num', text: c.bits_per_slot }),
    h('td', { class: 'num', text: fmtBytes(c.message_bytes) })));
  return h('details', { class: 'advanced capacity' }, h('summary', { text: 'Capacidad por formato y bits por ranura' }),
    h('div', { class: 'table-wrap' }, h('table', null,
      h('thead', null, h('tr', null, h('th', { text: 'Salida' }), h('th', { text: 'Dominio' }),
        h('th', { class: 'num', text: 'Bits' }), h('th', { class: 'num', text: 'Mensaje máx.' }))),
      h('tbody', null, rows))));
}

const embedKind = () => document.querySelector('input[name="embed-kind"]:checked').value;
for (const radio of document.querySelectorAll('input[name="embed-kind"]')) {
  radio.addEventListener('change', () => {
    const isText = embedKind() === 'text';
    $('embed-text-box').hidden = !isText;
    $('embed-payload-drop').hidden = isText;
    updateEmbedMeter();
  });
}
for (const id of ['embed-message', 'embed-format', 'embed-bits', 'embed-mode']) {
  $(id).addEventListener('input', updateEmbedMeter);
  $(id).addEventListener('change', updateEmbedMeter);
}

setupDrop('embed-payload-drop', 'embed-payload', (file) => {
  embedState.payload = file;
  updateEmbedMeter();
}, (file) => `${file.name} · ${fmtBytes(file.size)} (clic para cambiar)`);

// Capacidades del formato de salida elegido, por bits por ranura.
function capacitiesFor(info) {
  const caps = info.capacities;
  const fmt = $('embed-format').value;
  let ext = fmt === 'auto' ? caps[0].extension : fmt;
  // inspect() solo lista BMP/TGA/QOI/PNM para su propio formato: comparten el dominio espacial de PNG.
  if (!caps.some((c) => c.extension === ext) && ['bmp', 'tga', 'qoi', 'ppm'].includes(ext)) ext = 'png';
  return caps.filter((c) => c.extension === ext).sort((a, b) => a.bits_per_slot - b.bits_per_slot);
}

// Medidor en vivo: tamano estimado del mensaje (sin compresion) frente a la capacidad.
function updateEmbedMeter() {
  const meter = $('embed-meter');
  const isText = embedKind() === 'text';
  const size = isText ? utf8Length($('embed-message').value) : embedState.payload
    ? embedState.payload.size + utf8Length(embedState.payload.name) : 0;
  if (!embedState.info || !size) { meter.hidden = true; return; }
  meter.hidden = false;
  const fill = $('embed-meter-fill');
  const label = $('embed-meter-label');
  const caps = capacitiesFor(embedState.info);
  if (!caps.length) {
    fill.style.width = '100%';
    fill.className = 'meter-fill bad';
    label.textContent = 'La salida GIF necesita una portadora GIF.';
    return;
  }
  const bits = $('embed-bits').value;
  const match = $('embed-mode').value === 'match';
  const usable = caps.filter((c) => (bits === 'auto' || c.bits_per_slot === Number(bits)) && (!match || c.bits_per_slot === 1));
  const chosen = usable.find((c) => size <= c.message_bytes) || usable[usable.length - 1] || caps[0];
  const ratio = size / chosen.message_bytes;
  fill.style.width = `${Math.min(100, ratio * 100)}%`;
  fill.className = `meter-fill ${ratio > 1 ? 'bad' : ratio > 0.7 ? 'warn' : ''}`;
  label.replaceChildren(h('strong', { text: fmtBytes(size) }), ` de ${fmtBytes(chosen.message_bytes)} (${fmtPct(Math.min(ratio, 9.99), 1)})`);
  if (ratio <= 1) label.append(` · cabe con ${chosen.bits_per_slot} bit${chosen.bits_per_slot > 1 ? 's' : ''} por ranura`);
  else label.append(isText && $('embed-compress').checked ? ' · no cabe sin comprimir (la compresión puede bastar)' : ' · no cabe');
}

// ---- Contrasena: fuerza estimada y generador ----
const COMMON = ['password', 'contrase', '123456', 'qwerty', 'admin', 'hola', 'icpu', 'secreto', 'clave', 'letmein', 'iloveyou', 'abc123', 'welcome'];
const GENERATED_BITS = Math.floor(20 * Math.log2(55)); // 20 caracteres de 55 posibles (los guiones no aportan)
let lastGenerated = null;

function passwordStrength(pw) {
  if (pw === lastGenerated) return { bits: GENERATED_BITS, level: 4, label: 'Muy fuerte (generada)' };
  let pool = 0;
  if (/[a-z]/.test(pw)) pool += 26;
  if (/[A-Z]/.test(pw)) pool += 26;
  if (/\d/.test(pw)) pool += 10;
  if (/[^a-zA-Z0-9]/.test(pw)) pool += 33;
  let bits = pw.length * Math.log2(Math.max(pool, 2));
  const lower = pw.toLowerCase();
  if (/(.)\1{2,}/.test(pw)) bits *= 0.75;
  if (/(0123|1234|2345|3456|4567|5678|6789|abcd|bcde|qwer|asdf|zxcv)/.test(lower)) bits *= 0.7;
  if (COMMON.some((w) => lower.includes(w))) bits = Math.min(bits, 24);
  bits = Math.round(bits);
  const level = bits < 35 ? 1 : bits < 55 ? 2 : bits < 75 ? 3 : 4;
  return { bits, level, label: ['', 'Muy débil', 'Mejorable', 'Buena', 'Muy fuerte'][level] };
}
function generatePassword() {
  const alphabet = 'abcdefghjkmnpqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789';
  const limit = 256 - (256 % alphabet.length); // muestreo por rechazo: sin sesgo
  const out = [];
  while (out.length < 20) {
    for (const b of crypto.getRandomValues(new Uint8Array(32))) {
      if (b < limit && out.length < 20) out.push(alphabet[b % alphabet.length]);
    }
  }
  return out.join('').match(/.{5}/g).join('-');
}
function updatePasswordUi() {
  const pw = $('embed-password').value;
  const box = $('embed-strength');
  box.hidden = !pw;
  if (pw) {
    const st = passwordStrength(pw);
    box.dataset.level = st.level;
    $('embed-strength-label').textContent = `${st.label} · ≈ ${st.bits} bits`;
  }
  const pw2 = $('embed-password2').value;
  const hint = $('embed-match-hint');
  if (!pw2) { hint.textContent = ''; hint.className = 'field-hint'; return; }
  const same = pw === pw2;
  hint.textContent = same ? 'Las contraseñas coinciden' : 'Las contraseñas no coinciden';
  hint.className = `field-hint ${same ? 'ok' : 'bad'}`;
}
$('embed-password').addEventListener('input', updatePasswordUi);
$('embed-password2').addEventListener('input', updatePasswordUi);
$('embed-generate').addEventListener('click', async () => {
  const pw = generatePassword();
  lastGenerated = pw;
  $('embed-password').value = pw;
  $('embed-password2').value = pw;
  for (const id of ['embed-password', 'embed-password2']) $(id).type = 'text';
  updatePasswordUi();
  try {
    await navigator.clipboard.writeText(pw);
    toast('Contraseña generada y copiada. Guárdala: sin ella no se recupera el mensaje.');
  } catch {
    toast('Contraseña generada. Cópiala y guárdala: sin ella no se recupera el mensaje.');
  }
});
for (const btn of document.querySelectorAll('.toggle-pw')) {
  btn.addEventListener('click', () => {
    const ids = btn.dataset.for.split(' ');
    const show = $(ids[0]).type === 'password';
    for (const id of ids) $(id).type = show ? 'text' : 'password';
    btn.replaceChildren(icon(show ? 'eye-off' : 'eye'));
  });
}

$('embed-run').addEventListener('click', () => withBusy($('embed-run'), 'Cifrando y ocultando…', async () => {
  if (!embedState.cover) throw new Error('Elige primero una imagen portadora.');
  const pw = $('embed-password').value;
  if (!pw) { $('embed-password').focus(); throw new Error('Escribe una contraseña.'); }
  if (pw !== $('embed-password2').value) { $('embed-password2').focus(); throw new Error('Las contraseñas no coinciden.'); }
  const form = new FormData();
  form.append('cover', embedState.cover);
  if (embedKind() === 'text') {
    if (!$('embed-message').value) { $('embed-message').focus(); throw new Error('El mensaje está vacío.'); }
    form.append('message', $('embed-message').value);
  } else {
    if (!embedState.payload) throw new Error('Elige el fichero que quieres ocultar.');
    form.append('payload', embedState.payload);
  }
  form.append('password', pw);
  for (const [k, id] of [['format', 'embed-format'], ['bits', 'embed-bits'], ['mode', 'embed-mode'], ['layout', 'embed-layout'], ['quality', 'embed-quality']]) {
    form.append(k, $(id).value);
  }
  form.append('compress', $('embed-compress').checked ? '1' : '0');
  const r = await api('/api/embed', form);
  await showEmbedResult(r);
  toast('Mensaje ocultado y verificado');
}));

async function showEmbedResult(r) {
  const bytes = base64ToBytes(r.image);
  const stegoUrl = r.preview ? pngUrl(r.preview) : blobUrl(bytes, r.mime);
  const coverUrl = r.cover_preview ? pngUrl(r.cover_preview) : await displayUrl(embedState.cover);
  const download = { bytes, name: r.filename, mime: r.mime };
  addHistory({ type: 'embed', title: r.filename,
    detail: `${r.format} · ${embedKind() === 'text' ? 'texto' : 'fichero'} de ${fmtBytes(r.message_bytes)}` }, download);

  const q = r.quality;
  const box = $('embed-result');
  box.replaceChildren(h('section', { class: 'card result-card' },
    resultHead('check', 'Mensaje ocultado y verificado', `${r.filename} · se ha releído la imagen y el mensaje se recupera correctamente`),
    h('dl', { class: 'stats' },
      stat('Formato', r.format, `dominio ${r.domain}`),
      stat('Mensaje', fmtBytes(r.message_bytes), r.compressed ? `comprimido a ${fmtBytes(r.packed_bytes)}` : 'sin comprimir'),
      stat('Capacidad usada', fmtPct(r.fill_ratio), `${r.bits_per_slot} bit${r.bits_per_slot > 1 ? 's' : ''} por ranura`),
      stat('Distorsión', fmtPsnr(q.psnr), `SSIM ${fmtNum(q.ssim, 4)}`),
      stat('Muestras cambiadas', fmtPct(q.changed_ratio, 2), `diferencia máx. ${q.max_abs_diff}`),
      stat('Fichero', fmtBytes(r.image_bytes), `original ${fmtBytes(r.cover_bytes)}`)),
    createViewer({ before: coverUrl, after: stegoUrl, diff: pngUrl(r.difference_map), beforeLabel: 'Original', afterLabel: 'Con mensaje',
      title: 'Comparar' }),
    r.notes.length ? h('div', { class: 'callout warn' }, icon('alert'), h('ul', null, r.notes.map((n) => h('li', { text: n })))) : null,
    h('div', { class: 'callout info' }, icon('alert'),
      h('p', { text: 'Para enviarla, adjúntala como archivo (correo, Telegram "como archivo", nube…). WhatsApp, Instagram y otras redes recomprimen las imágenes y el mensaje se pierde.' })),
    h('div', { class: 'actions' },
      h('button', { class: 'btn primary', onclick: () => downloadBytes(bytes, r.filename, r.mime) }, icon('download'), `Descargar ${r.filename}`),
      h('button', { class: 'btn secondary', onclick: () => sendTo('extract-image', bytes, r.filename, r.mime, 'extract') }, icon('key'), 'Probar a extraer'),
      h('button', { class: 'btn secondary', onclick: () => sendTo('analyze-image', bytes, r.filename, r.mime, 'analyze', embedState.cover) }, icon('chart'), 'Analizar resultado'))));
  box.firstChild.scrollIntoView({ behavior: 'smooth', block: 'start' });
}

// Pasa la imagen generada a otra pestana como si se hubiera elegido alli.
function sendTo(inputId, bytes, name, mime, tab, cover) {
  fileHandlers[inputId](new File([bytes], name, { type: mime }));
  if (cover && tab === 'analyze') fileHandlers['analyze-cover'](cover);
  showTab(tab);
  window.scrollTo({ top: 0, behavior: 'smooth' });
}

// ===========================================================================
// EXTRAER
// ===========================================================================
const extractState = { image: null };

setupDrop('extract-image-drop', 'extract-image', async (file) => {
  extractState.image = file;
  $('extract-result').replaceChildren();
  const card = fileCard(file, { chips: [fmtBytes(file.size)] });
  $('extract-image-info').replaceChildren(card);
  try { card.setChips(describeChips(await loadInfo(file))); } catch (e) { card.setChips([e.message]); }
  $('extract-password').focus();
});
$('extract-password').addEventListener('keydown', (e) => { if (e.key === 'Enter') $('extract-run').click(); });

$('extract-run').addEventListener('click', () => withBusy($('extract-run'), 'Derivando la clave…', async () => {
  if (!extractState.image) throw new Error('Elige primero la imagen.');
  if (!$('extract-password').value) { $('extract-password').focus(); throw new Error('Escribe la contraseña.'); }
  const form = new FormData();
  form.append('image', extractState.image);
  form.append('password', $('extract-password').value);
  form.append('layout', $('extract-layout').value);
  let r;
  try {
    r = await api('/api/extract', form);
  } catch (e) {
    $('extract-result').replaceChildren(h('section', { class: 'card result-card' },
      resultHead('alert', 'No se ha encontrado ningún mensaje', e.message, 'bad'),
      h('ul', { class: 'note' }, h('li', { text: 'Comprueba la contraseña (distingue mayúsculas y minúsculas).' }),
        h('li', { text: 'Si se ocultó con orden secuencial, elígelo en Opciones avanzadas.' }),
        h('li', { text: 'Si la imagen se envió por una red social o se editó, el mensaje se habrá perdido.' }))));
    return;
  }
  const bytes = base64ToBytes(r.data);
  const box = $('extract-result');
  if (r.kind === 'text') {
    addHistory({ type: 'extract', title: extractState.image.name, detail: `texto de ${fmtBytes(r.bytes)}` });
    const area = h('textarea', { class: 'message-out', readonly: true, rows: 8, 'aria-label': 'Mensaje recuperado' });
    area.value = r.text;
    box.replaceChildren(h('section', { class: 'card result-card' },
      resultHead('check', 'Mensaje recuperado', `${fmtBytes(r.bytes)} · autenticado: no ha sido modificado`),
      area,
      h('div', { class: 'actions' },
        h('button', { class: 'btn primary', onclick: async () => {
          await navigator.clipboard.writeText(r.text);
          toast('Copiado al portapapeles');
        } }, icon('copy'), 'Copiar'),
        h('button', { class: 'btn secondary', onclick: () => downloadBytes(bytes, 'mensaje.txt', 'text/plain;charset=utf-8') }, icon('download'), 'Descargar .txt'))));
  } else {
    const name = r.name || 'fichero.bin';
    addHistory({ type: 'extract', title: extractState.image.name, detail: `fichero ${name} (${fmtBytes(r.bytes)})` }, { bytes, name, mime: 'application/octet-stream' });
    box.replaceChildren(h('section', { class: 'card result-card' },
      resultHead('check', 'Fichero recuperado', 'autenticado: no ha sido modificado'),
      h('dl', { class: 'stats' }, stat('Nombre', name), stat('Tamaño', fmtBytes(r.bytes))),
      h('div', { class: 'actions' },
        h('button', { class: 'btn primary', onclick: () => downloadBytes(bytes, name) }, icon('download'), `Descargar ${name}`))));
  }
  toast('Mensaje recuperado');
}));

// ===========================================================================
// ANALIZAR
// ===========================================================================
const analyzeState = { image: null, cover: null };

setupDrop('analyze-image-drop', 'analyze-image', (file) => {
  analyzeState.image = file;
  const card = fileCard(file, { chips: [fmtBytes(file.size)] });
  $('analyze-image-info').replaceChildren(card);
  loadInfo(file).then((i) => card.setChips(describeChips(i))).catch((e) => card.setChips([e.message]));
});
setupDrop('analyze-cover-drop', 'analyze-cover', (file) => {
  analyzeState.cover = file;
  const card = fileCard(file, { chips: [fmtBytes(file.size)], onRemove: () => {
    analyzeState.cover = null;
    $('analyze-cover-info').replaceChildren();
  } });
  $('analyze-cover-info').replaceChildren(card);
  loadInfo(file).then((i) => card.setChips(describeChips(i))).catch((e) => card.setChips([e.message]));
});

$('analyze-run').addEventListener('click', () => withBusy($('analyze-run'), 'Analizando…', async () => {
  if (!analyzeState.image) throw new Error('Elige la imagen que quieres analizar.');
  const form = new FormData();
  form.append('image', analyzeState.image);
  if (analyzeState.cover) form.append('cover', analyzeState.cover);
  const r = await api('/api/analyze', form);
  await showAnalysis(r);
  const verdict = r.rs ? r.rs.verdict : r.chi_square_dct ? r.chi_square_dct.verdict : '';
  addHistory({ type: 'analyze', title: analyzeState.image.name, detail: `${r.format} · ${verdict || 'analizada'}` });
}));

async function showAnalysis(r) {
  const cards = [];
  if (r.chi_square) {
    cards.push(verdictCard('Chi-cuadrado (Westfeld-Pfitzmann)', `p = ${fmtNum(r.chi_square.p)}`, r.chi_square.verdict,
      'Probabilidad de que los pares de valores (2i, 2i+1) estén igualados, como deja la sustitución LSB.'));
  }
  if (r.rs) {
    cards.push(verdictCard('Análisis RS (Fridrich)', fmtPct(r.rs.estimated_rate), r.rs.verdict,
      'Fracción estimada de muestras con mensaje. Fiable con sustitución LSB; LSB matching lo resiste.'));
  }
  if (r.chi_square_dct) {
    cards.push(verdictCard('Chi-cuadrado DCT (JSteg)', `p = ${fmtNum(r.chi_square_dct.p)}`, r.chi_square_dct.verdict,
      'Pares de valores en los coeficientes DCT. En JPEG los tests sobre píxeles no tienen sentido.'));
  }
  if (r.quality) {
    cards.push(h('div', { class: 'verdict' }, h('div', { class: 'label', text: 'Distorsión respecto a la portadora' }),
      h('div', { class: 'value', text: fmtPsnr(r.quality.psnr) }),
      h('span', { class: 'tag ok' }, `SSIM ${fmtNum(r.quality.ssim, 4)}`),
      h('p', { text: `MSE ${fmtNum(r.quality.mse, 4)} · ${fmtPct(r.quality.changed_ratio, 2)} de muestras cambiadas · diferencia máx. ${r.quality.max_abs_diff}` })));
  }
  const parts = [h('div', { class: `grid ${cards.length >= 3 ? 'three' : 'two'}` }, cards)];
  if (r.format === 'GIF') {
    parts.push(h('div', { class: 'callout info' }, icon('alert'),
      h('p', { text: 'En GIF los datos van en los índices de la paleta: los tests LSB sobre colores son solo orientativos.' })));
  }

  const charts = [];
  if (r.chi_square_sweep) {
    charts.push(chartCard('Barrido chi-cuadrado', {
      series: [{ name: 'p', cls: 's1', points: r.chi_square_sweep.map((p) => [p.fraction * 100, p.p]) }],
      xLabel: 'Porción analizada (%)', yLabel: 'p', xDomain: [0, 100], yDomain: [0, 1],
      yFmt: (v) => v.toFixed(1), tipFmt: (v) => v.toFixed(3), xFmt: (v) => Math.round(v), threshold: 0.95,
    }, 'barrido_chi2.png'));
  }
  const channelCls = r.histograms.length === 1 ? ['sk'] : ['sr', 'sg', 'sb'];
  const channelName = r.histograms.length === 1 ? ['Gris'] : ['Rojo', 'Verde', 'Azul'];
  charts.push(chartCard('Histograma', {
    series: r.histograms.map((hist, c) => ({ name: channelName[c], cls: channelCls[c], points: hist.map((v, i) => [i, v]) })),
    xLabel: 'Valor', yLabel: 'Muestras', xDomain: [0, 255], dots: false, yDomain: [0, Math.max(...r.histograms.flat()) * 1.05],
    yFmt: (v) => (v >= 1000 ? `${(v / 1000).toFixed(v >= 10000 ? 0 : 1)}k` : v), tipFmt: fmtInt,
  }, 'histograma.png'));
  parts.push(h('div', { class: `grid ${charts.length > 1 ? 'two' : ''}` }, charts));

  if (r.difference_map && analyzeState.cover) {
    const coverUrl = await displayUrl(analyzeState.cover);
    const imageUrl = await displayUrl(analyzeState.image);
    parts.push(h('section', { class: 'card' }, createViewer({ before: coverUrl, after: imageUrl, diff: pngUrl(r.difference_map),
      beforeLabel: 'Portadora', afterLabel: 'Analizada', title: 'Comparar con la portadora' })));
  }
  parts.push(h('div', { class: 'grid two' },
    h('section', { class: 'card' }, createViewer({ before: pngUrl(r.bit_plane_0), title: 'Plano de bits 0 (LSB)' }),
      h('p', { class: 'note', text: 'En una foto natural conserva algo de estructura; un mensaje cifrado parece ruido puro.' })),
    h('section', { class: 'card' }, createViewer({ before: pngUrl(r.bit_plane_1), title: 'Plano de bits 1' }),
      h('p', { class: 'note', text: 'Referencia: solo cambia si se usan 2 o más bits por ranura.' }))));

  const box = $('analyze-result');
  box.replaceChildren(h('div', { class: 'stack result-card' }, parts));
  box.firstChild.scrollIntoView({ behavior: 'smooth', block: 'start' });
}

// ===========================================================================
// EXPERIMENTO
// ===========================================================================
const benchState = { cover: null };

setupDrop('bench-cover-drop', 'bench-cover', (file) => {
  benchState.cover = file;
  const card = fileCard(file, { chips: [fmtBytes(file.size)] });
  $('bench-cover-info').replaceChildren(card);
  loadInfo(file).then((i) => card.setChips(describeChips(i))).catch((e) => card.setChips([e.message]));
});

const checkedValues = (name) => [...document.querySelectorAll(`input[name="${name}"]:checked`)].map((i) => i.value);
// "0.05, 0.1, 1" (coma como separador) o "0,05 0,1 1" (coma decimal, separados por espacios o ;)
function benchFills() {
  const raw = $('bench-fills').value.trim();
  const parts = raw.includes('.') ? raw.split(/[,;\s]+/) : raw.split(/[;\s]+/);
  return parts.filter(Boolean).map((v) => v.replace(',', '.'));
}

function updateBenchCount() {
  const bits = checkedValues('bench-bits').map(Number);
  const modes = checkedValues('bench-modes');
  const layouts = checkedValues('bench-layouts').length;
  const fills = benchFills().length;
  let combos = 0;
  for (const k of bits) for (const m of modes) if (!(m === 'match' && k > 1)) combos += layouts * fills;
  $('bench-count').textContent = combos ? `${combos} combinaciones` : 'Marca al menos un valor en cada grupo';
}
for (const el of document.querySelectorAll('#tab-bench input')) el.addEventListener('input', updateBenchCount);

$('bench-run').addEventListener('click', () => withBusy($('bench-run'), 'Ejecutando…', async () => {
  if (!benchState.cover) throw new Error('Elige una imagen portadora.');
  const bits = checkedValues('bench-bits');
  const modes = checkedValues('bench-modes');
  const layouts = checkedValues('bench-layouts');
  if (!bits.length || !modes.length || !layouts.length) throw new Error('Marca al menos un valor en cada grupo.');
  const fills = benchFills();
  if (!fills.length || fills.some((v) => !(Number(v) > 0 && Number(v) <= 1))) {
    throw new Error('El llenado debe ser una lista de fracciones entre 0 y 1, por ejemplo: 0.1, 0.5, 1');
  }
  const form = new FormData();
  form.append('cover', benchState.cover);
  form.append('bits', bits.join(','));
  form.append('modes', modes.join(','));
  form.append('layouts', layouts.join(','));
  form.append('fills', fills.join(','));
  form.append('format', $('bench-format').value);

  const progress = $('bench-progress');
  const pfill = $('bench-progress-fill');
  const plabel = $('bench-progress-label');
  progress.hidden = false;
  progress.classList.add('indeterminate');
  plabel.textContent = 'Preparando…';
  const started = performance.now();
  let result = null;
  try {
    await apiStream('/api/bench', form, (ev) => {
      if (ev.type === 'progress') {
        progress.classList.remove('indeterminate');
        pfill.style.width = `${(ev.done / ev.total) * 100}%`;
        const elapsed = (performance.now() - started) / 1000;
        const eta = ev.done ? (elapsed / ev.done) * (ev.total - ev.done) : 0;
        plabel.textContent = `${ev.done} de ${ev.total} combinaciones${ev.done && ev.done < ev.total ? ` · quedan ≈ ${Math.ceil(eta)} s` : ''}`;
      } else if (ev.type === 'error') {
        throw new Error(ev.error);
      } else if (ev.type === 'result') {
        result = ev;
      }
    });
  } finally {
    progress.hidden = true;
  }
  if (!result) throw new Error('El experimento terminó sin resultados.');
  showBench(result);
  addHistory({ type: 'bench', title: benchState.cover.name, detail: `${result.runs.length} combinaciones · ${result.output}` });
  toast(`Experimento terminado en ${((performance.now() - started) / 1000).toFixed(1).replace('.', ',')} s`);
}));

const MODE_ES = { replace: 'sustitución', match: 'matching' };
const LAYOUT_ES = { random: 'aleatorio', sequential: 'secuencial' };

function showBench(r) {
  const ok = r.runs.filter((run) => !run.error);
  const groups = new Map();
  for (const run of ok) {
    const key = `k=${run.bits} · ${MODE_ES[run.mode]} · ${LAYOUT_ES[run.layout]}`;
    if (!groups.has(key)) groups.set(key, { key, runs: [], layout: run.layout });
    groups.get(key).runs.push(run);
  }
  const series = (fn) => [...groups.values()].map((g, i) => ({
    name: g.key, cls: `s${(i % 8) + 1}`, dashed: g.layout === 'sequential',
    points: g.runs.map((run) => [run.fill * 100, fn(run)]).filter((p) => p[1] != null && p[1] >= 0),
  })).filter((se) => se.points.length);

  const isDct = r.domain === 'dct';
  const base = r.baseline;
  const parts = [h('section', { class: 'card' },
    resultHead('flask', `Salida ${r.output} · dominio ${r.domain}`,
      base ? (isDct ? `Portadora sin mensaje: chi² DCT p = ${fmtNum(base.dct_chi2_p)}`
        : `Portadora sin mensaje: chi² p = ${fmtNum(base.chi2_p)} · RS = ${fmtNum(base.rs_rate)}`) : 'Líneas discontinuas: orden secuencial'),
    h('p', { class: 'note', text: 'Líneas discontinuas: orden secuencial. Pulsa en la leyenda para ocultar series y pasa el ratón para ver los valores.' }))];

  if (ok.length) {
    const xOpts = { xLabel: 'Llenado (% de la capacidad)', xFmt: (v) => Math.round(v) };
    const charts = [chartCard('Distorsión (PSNR)', { ...xOpts, series: series((run) => run.psnr), yLabel: 'PSNR (dB)',
      yFmt: (v) => v.toFixed(0), tipFmt: (v) => `${v.toFixed(2)} dB` }, 'psnr.png')];
    const det = isDct ? series((run) => run.dct_chi2_p) : series((run) => run.rs_rate);
    if (det.length) {
      charts.push(chartCard(isDct ? 'Detección: chi² DCT (p)' : 'Detección: tasa estimada por RS', { ...xOpts, series: det,
        yLabel: isDct ? 'p' : 'Tasa RS', yDomain: [0, 1], yFmt: (v) => v.toFixed(1), tipFmt: (v) => v.toFixed(3) }, 'deteccion.png'));
    }
    if (!isDct) {
      const chi = series((run) => run.chi2_p);
      if (chi.length) {
        charts.push(chartCard('Detección: chi-cuadrado (p)', { ...xOpts, series: chi, yLabel: 'p', yDomain: [0, 1],
          yFmt: (v) => v.toFixed(1), tipFmt: (v) => v.toFixed(3), threshold: 0.95 }, 'chi2.png'));
      }
      charts.push(chartCard('Similitud estructural (SSIM)', { ...xOpts, series: series((run) => run.ssim), yLabel: 'SSIM',
        yFmt: (v) => v.toFixed(3), tipFmt: (v) => v.toFixed(5) }, 'ssim.png'));
    }
    parts.push(h('div', { class: 'grid two' }, charts));
  }

  const head = ['k', 'Modo', 'Orden', 'Llenado', 'Mensaje', 'Cambiadas', 'PSNR', 'SSIM', 'chi² p', 'RS', 'DCT p', 'Tiempo'];
  const td = (text, num) => h('td', { class: num ? 'num' : null, text });
  const rows = r.runs.map((run) => run.error
    ? h('tr', { class: 'err' }, td(run.bits), td(MODE_ES[run.mode]), td(LAYOUT_ES[run.layout]), td(fmtPct(run.fill, 0), true),
      h('td', { colspan: 8, text: run.error }))
    : h('tr', null, td(run.bits), td(MODE_ES[run.mode]), td(LAYOUT_ES[run.layout]), td(fmtPct(run.fill, 0), true),
      td(fmtBytes(run.message_bytes), true), td(fmtPct(run.changed_ratio, 2), true), td(fmtPsnr(run.psnr), true),
      td(fmtNum(run.ssim, 4), true), td(fmtNum(run.chi2_p), true), td(fmtNum(run.rs_rate), true), td(fmtNum(run.dct_chi2_p), true),
      td(`${run.seconds.toFixed(2).replace('.', ',')} s`, true)));
  parts.push(h('section', { class: 'card' },
    h('div', { class: 'chart-head' }, h('h3', { text: 'Resultados' }),
      h('button', { class: 'btn secondary small', onclick: () => downloadBenchCsv(r) }, icon('download'), 'CSV')),
    h('div', { class: 'table-wrap' }, h('table', null,
      h('thead', null, h('tr', null, head.map((t, i) => h('th', { class: i >= 3 ? 'num' : null, text: t })))),
      h('tbody', null, rows)))));

  const box = $('bench-result');
  box.replaceChildren(h('div', { class: 'stack result-card' }, parts));
  box.firstChild.scrollIntoView({ behavior: 'smooth', block: 'start' });
}

function downloadBenchCsv(r) {
  const cols = ['bits', 'mode', 'layout', 'fill', 'message_bytes', 'changed_ratio', 'psnr', 'ssim', 'chi2_p', 'rs_rate', 'dct_chi2_p', 'seconds', 'error'];
  const esc = (v) => (v == null ? '' : /[",\n]/.test(String(v)) ? `"${String(v).replace(/"/g, '""')}"` : String(v));
  const csv = [cols.join(','), ...r.runs.map((run) => cols.map((c) => esc(run[c])).join(','))].join('\n');
  downloadBytes(new TextEncoder().encode(`${csv}\n`), 'icpu_experimento.csv', 'text/csv;charset=utf-8');
}

// ===========================================================================
// Atajos de teclado
// ===========================================================================
function isTyping(el) {
  return el && (el.tagName === 'INPUT' || el.tagName === 'TEXTAREA' || el.tagName === 'SELECT' || el.isContentEditable);
}
document.addEventListener('keydown', (e) => {
  if ((e.ctrlKey || e.metaKey) && e.key === 'Enter') {
    e.preventDefault();
    $(RUN_BUTTON[currentTab]).click();
  } else if (e.altKey && !e.ctrlKey && /^[1-4]$/.test(e.key)) {
    e.preventDefault();
    showTab(TABS[Number(e.key) - 1]);
  } else if (e.altKey && (e.key === 'h' || e.key === 'H')) {
    e.preventDefault();
    toggleHistory();
  } else if (e.key === '?' && !isTyping(document.activeElement)) {
    e.preventDefault();
    $('shortcuts').showModal();
  } else if (e.key === 'Escape' && !$('history').hidden) {
    toggleHistory(false);
  }
});
$('shortcuts-btn').addEventListener('click', () => $('shortcuts').showModal());
$('shortcuts-close').addEventListener('click', () => $('shortcuts').close());

// ===========================================================================
// Arranque y cierre
// ===========================================================================
$('shutdown').addEventListener('click', async () => {
  if (!confirm('¿Cerrar I-CPU? El servidor local se detendrá.')) return;
  try { await api('/api/shutdown', new FormData()); } catch { /* ya estaba cerrado */ }
  document.querySelector('main').hidden = true;
  document.querySelector('.topbar').hidden = true;
  $('closed').hidden = false;
});

(async () => {
  hydrateIcons();
  let theme = 'system';
  try { theme = localStorage.getItem('icpu-theme') || 'system'; } catch { /* sin almacenamiento */ }
  applyTheme(theme);
  try {
    const saved = sessionStorage.getItem('icpu-tab');
    if (saved && TABS.includes(saved)) showTab(saved);
  } catch { /* sin almacenamiento */ }
  renderHistory();
  updateBenchCount();
  try {
    $('version').textContent = `v${(await api('/api/version')).version}`;
  } catch (e) {
    toast(TOKEN ? e.message : 'Falta el token de sesión: abre I-CPU desde el enlace que aparece en la consola.', 'bad');
  }
})();
