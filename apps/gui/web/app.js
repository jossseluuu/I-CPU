// I-CPU - interfaz web. Habla con el servidor local (/api/*) y dibuja los
// resultados. Todo texto que viene del usuario o del servidor se inserta con
// textContent (nunca innerHTML).
'use strict';

// ---------------------------------------------------------------------------
// Sesion: el token llega en el fragmento de la URL (#t=...) y se guarda en
// sessionStorage para sobrevivir a una recarga de la pestana.
// ---------------------------------------------------------------------------
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

async function api(path, form) {
  let res;
  try {
    res = await fetch(path, {
      method: form ? 'POST' : 'GET',
      headers: { 'X-ICPU-Token': TOKEN },
      body: form,
    });
  } catch {
    throw new Error('No hay conexión con I-CPU. ¿Se ha cerrado el programa?');
  }
  let data;
  try {
    data = await res.json();
  } catch {
    throw new Error(`Respuesta inesperada del servidor (HTTP ${res.status})`);
  }
  if (!res.ok) throw new Error(data.error || `Error HTTP ${res.status}`);
  return data;
}

// ---------------------------------------------------------------------------
// Utilidades de DOM y formato
// ---------------------------------------------------------------------------
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
  if (n < 1024 * 1024) return `${(n / 1024).toFixed(1)} KiB`;
  return `${(n / 1024 / 1024).toFixed(2)} MiB`;
}
const fmtPct = (r, d = 1) => `${(r * 100).toFixed(d)} %`;
const fmtNum = (v, d = 3) => (v == null || v < 0 ? '–' : Number(v).toFixed(d));
const fmtPsnr = (v) => (v == null ? '∞' : `${v.toFixed(2)} dB`);  // JSON null = infinito (imagenes identicas)

function base64ToBytes(b64) {
  const bin = atob(b64);
  const out = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i);
  return out;
}

function downloadBytes(bytes, filename, mime = 'application/octet-stream') {
  const url = URL.createObjectURL(new Blob([bytes], { type: mime }));
  const a = h('a', { href: url, download: filename });
  document.body.append(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 10000);
}

function setStatus(id, kind, text, busy = false) {
  const el = $(id);
  el.className = `status ${kind || ''}`;
  el.replaceChildren();
  if (!text) return;
  if (busy) el.append(h('span', { class: 'spinner', 'aria-hidden': 'true' }), ' ');
  el.append(text);
}

async function withBusy(button, statusId, busyText, fn) {
  button.disabled = true;
  setStatus(statusId, 'info', busyText, true);
  try {
    await fn();
    setStatus(statusId, '', '');
  } catch (e) {
    setStatus(statusId, 'error', e.message);
  } finally {
    button.disabled = false;
  }
}

// Formatos que el navegador sabe mostrar (los demas se previsualizan sin miniatura).
const BROWSER_IMAGE = /\.(png|jpe?g|gif|bmp|webp)$/i;

function fileCard(file, facts) {
  const thumb = BROWSER_IMAGE.test(file.name)
    ? h('img', { class: 'thumb', alt: '', src: URL.createObjectURL(file) })
    : h('div', { class: 'thumb-placeholder', text: 'Sin vista previa' });
  if (thumb.tagName === 'IMG') {
    thumb.addEventListener('error', () => thumb.replaceWith(h('div', { class: 'thumb-placeholder', text: 'Sin vista previa' })));
  }
  return h('div', { class: 'file-card' }, thumb,
    h('div', null, h('div', { class: 'file-name', text: file.name }), h('div', { class: 'facts' }, facts)));
}

function describeInfo(info) {
  const parts = [`${info.format} · ${info.width} × ${info.height} px`,
    `${info.channels} canal(es), ${info.depth} bits`, fmtBytes(info.file_bytes)];
  if (info.frames > 1) parts.push(`${info.frames} fotogramas`);
  if (info.jpeg_quality >= 0) {
    parts.push(`JPEG calidad ≈ ${info.jpeg_quality}${info.jpeg_progressive ? ', progresivo' : ''}` +
      (info.jpeg_subsampling ? `, croma ${info.jpeg_subsampling}` : ''));
  }
  return parts.join(' · ');
}

// Zona de arrastre asociada a un <input type=file>.
function setupDrop(dropId, inputId, onFile) {
  const drop = $(dropId);
  const input = $(inputId);
  input.addEventListener('change', () => input.files[0] && onFile(input.files[0]));
  drop.addEventListener('dragover', (e) => { e.preventDefault(); drop.classList.add('over'); });
  drop.addEventListener('dragleave', () => drop.classList.remove('over'));
  drop.addEventListener('drop', (e) => {
    e.preventDefault();
    drop.classList.remove('over');
    const f = e.dataTransfer.files[0];
    if (f) onFile(f);
  });
}

// ---------------------------------------------------------------------------
// Graficas SVG sencillas (lineas con puntos)
// ---------------------------------------------------------------------------
function niceTicks(min, max, count = 5) {
  const span = max - min || 1;
  const raw = span / count;
  const mag = 10 ** Math.floor(Math.log10(raw));
  const step = [1, 2, 2.5, 5, 10].map((m) => m * mag).find((st) => span / st <= count) || 10 * mag;
  const ticks = [];
  for (let v = Math.ceil(min / step) * step; v <= max + step * 1e-9; v += step) ticks.push(Number(v.toFixed(10)));
  return ticks;
}

function lineChart({ series, xLabel, yLabel, xDomain, yDomain, xFmt = (v) => v, yFmt = (v) => v, threshold, dots = true }) {
  const W = 560, H = 280, m = { l: 52, r: 14, t: 12, b: 42 };
  const pts = series.flatMap((se) => se.points);
  const [x0, x1] = xDomain || [Math.min(...pts.map((p) => p[0])), Math.max(...pts.map((p) => p[0]))];
  let [y0, y1] = yDomain || [Math.min(...pts.map((p) => p[1])), Math.max(...pts.map((p) => p[1]))];
  if (y0 === y1) { y0 -= 1; y1 += 1; }
  const sx = (v) => m.l + ((v - x0) / (x1 - x0 || 1)) * (W - m.l - m.r);
  const sy = (v) => H - m.b - ((v - y0) / (y1 - y0)) * (H - m.t - m.b);

  const svg = s('svg', { class: 'chart', viewBox: `0 0 ${W} ${H}`, role: 'img', 'aria-label': `${yLabel} frente a ${xLabel}` });
  for (const t of niceTicks(y0, y1)) {
    svg.append(s('line', { class: 'gridline', x1: m.l, x2: W - m.r, y1: sy(t), y2: sy(t) }));
    svg.append(s('text', { x: m.l - 6, y: sy(t) + 4, 'text-anchor': 'end' }, yFmt(t)));
  }
  for (const t of niceTicks(x0, x1, 6)) {
    svg.append(s('text', { x: sx(t), y: H - m.b + 16, 'text-anchor': 'middle' }, xFmt(t)));
  }
  svg.append(s('line', { class: 'axis', x1: m.l, x2: W - m.r, y1: H - m.b, y2: H - m.b }));
  if (threshold != null && threshold >= y0 && threshold <= y1) {
    svg.append(s('line', { class: 'threshold', x1: m.l, x2: W - m.r, y1: sy(threshold), y2: sy(threshold) }));
  }
  svg.append(s('text', { class: 'axis-title', x: (m.l + W - m.r) / 2, y: H - 6, 'text-anchor': 'middle' }, xLabel));
  svg.append(s('text', { class: 'axis-title', x: 12, y: (m.t + H - m.b) / 2, 'text-anchor': 'middle',
    transform: `rotate(-90 12 ${(m.t + H - m.b) / 2})` }, yLabel));

  for (const se of series) {
    const d = se.points.map((p, i) => `${i ? 'L' : 'M'}${sx(p[0]).toFixed(1)},${sy(p[1]).toFixed(1)}`).join('');
    svg.append(s('path', { class: `line ${se.cls}${se.dashed ? ' dashed' : ''}`, d }));
    if (dots) {
      for (const p of se.points) {
        svg.append(s('circle', { class: `dot ${se.cls}`, cx: sx(p[0]), cy: sy(p[1]), r: 3.5 },
          s('title', null, `${se.name}: ${xFmt(p[0])} → ${yFmt(p[1])}`)));
      }
    }
  }
  const legend = h('div', { class: 'legend' },
    series.map((se) => h('span', null, h('i', { class: se.cls }), se.name)));
  return h('div', null, svg, series.length > 1 ? legend : null);
}

function stat(label, value, sub) {
  return h('div', { class: 'stat' }, h('dt', { text: label }), h('dd', null, value, sub ? h('small', null, ' ', sub) : null));
}

function verdictCard(label, value, verdict, note) {
  const level = /muy probable|insercion probable/.test(verdict) ? 'bad' : /dudoso/.test(verdict) ? 'warn' : 'ok';
  const text = { 'insercion muy probable': 'Inserción muy probable', 'insercion probable': 'Inserción probable',
    dudoso: 'Dudoso', 'sin indicios': 'Sin indicios' }[verdict] || verdict;
  return h('div', { class: 'verdict' }, h('div', { class: 'label', text: label }), h('div', { class: 'value', text: value }),
    h('span', { class: `tag ${level}`, text }), note ? h('p', { text: note }) : null);
}

// ---------------------------------------------------------------------------
// Pestanas
// ---------------------------------------------------------------------------
function showTab(name) {
  for (const btn of document.querySelectorAll('.tabs button')) {
    const on = btn.dataset.tab === name;
    btn.setAttribute('aria-selected', String(on));
    $(`tab-${btn.dataset.tab}`).hidden = !on;
  }
  try { sessionStorage.setItem('icpu-tab', name); } catch { /* sin almacenamiento: no pasa nada */ }
}
for (const btn of document.querySelectorAll('.tabs button')) btn.addEventListener('click', () => showTab(btn.dataset.tab));

for (const btn of document.querySelectorAll('.toggle-pw')) {
  btn.addEventListener('click', () => {
    const ids = btn.dataset.for.split(' ');
    const show = $(ids[0]).type === 'password';
    for (const id of ids) $(id).type = show ? 'text' : 'password';
    btn.textContent = show ? 'Ocultar' : 'Mostrar';
  });
}

// ---------------------------------------------------------------------------
// OCULTAR
// ---------------------------------------------------------------------------
const embed = { cover: null, info: null, payload: null };

setupDrop('embed-cover-drop', 'embed-cover', async (file) => {
  embed.cover = file;
  embed.info = null;
  const box = $('embed-cover-info');
  box.replaceChildren(fileCard(file, 'Leyendo…'));
  try {
    const form = new FormData();
    form.append('image', file);
    embed.info = await api('/api/info', form);
    box.replaceChildren(fileCard(file, describeInfo(embed.info)), capacityTable(embed.info));
    // GIF solo se puede escribir con portadora GIF y viceversa: se ajusta el formato por defecto
    $('embed-format').value = 'auto';
  } catch (e) {
    embed.cover = null;
    box.replaceChildren(fileCard(file, ''), h('div', { class: 'status error', text: e.message }));
  }
  updateEmbedSize();
});

function capacityTable(info) {
  const rows = info.capacities.map((c) => h('tr', null,
    h('td', { text: c.output }), h('td', { text: c.domain }), h('td', { class: 'num', text: c.bits_per_slot }),
    h('td', { class: 'num', text: fmtBytes(c.message_bytes) })));
  return h('div', { class: 'capacity table-wrap' },
    h('table', null,
      h('thead', null, h('tr', null, h('th', { text: 'Salida' }), h('th', { text: 'Dominio' }),
        h('th', { class: 'num', text: 'Bits/ranura' }), h('th', { class: 'num', text: 'Mensaje máx.' }))),
      h('tbody', null, rows)),
    h('p', { class: 'hint', text: 'Capacidad aproximada sin compresión. Más bits por ranura = más capacidad pero más distorsión.' }));
}

function embedKind() { return document.querySelector('input[name="embed-kind"]:checked').value; }

for (const radio of document.querySelectorAll('input[name="embed-kind"]')) {
  radio.addEventListener('change', () => {
    const isText = embedKind() === 'text';
    $('embed-message').hidden = !isText;
    $('embed-payload-drop').hidden = isText;
    updateEmbedSize();
  });
}
$('embed-message').addEventListener('input', updateEmbedSize);
$('embed-format').addEventListener('change', updateEmbedSize);
$('embed-bits').addEventListener('change', updateEmbedSize);

setupDrop('embed-payload-drop', 'embed-payload', (file) => {
  embed.payload = file;
  $('embed-payload-drop').querySelector('.drop-text').textContent = `${file.name} (${fmtBytes(file.size)})`;
  updateEmbedSize();
});

// Estimacion orientativa: tamano del mensaje frente a la capacidad del formato elegido.
function updateEmbedSize() {
  const size = embedKind() === 'text' ? new TextEncoder().encode($('embed-message').value).length
    : (embed.payload ? embed.payload.size : 0);
  const hint = $('embed-size');
  if (!size) { hint.textContent = ''; return; }
  let text = `Tamaño del mensaje: ${fmtBytes(size)}`;
  if (embed.info) {
    const caps = embed.info.capacities;
    const fmt = $('embed-format').value;
    const bits = $('embed-bits').value;
    // La primera entrada es la del formato de la portadora (o PNG si no se puede escribir).
    // inspect() no lista BMP/TGA/QOI/PNM salvo para su propio formato: comparten el dominio espacial de PNG.
    let ext = fmt === 'auto' ? caps[0].extension : fmt;
    if (!caps.some((c) => c.extension === ext) && ['bmp', 'tga', 'qoi', 'ppm'].includes(ext)) ext = 'png';
    const matching = caps.filter((c) => c.extension === ext && (bits === 'auto' || c.bits_per_slot === Number(bits)));
    const max = Math.max(0, ...matching.map((c) => c.message_bytes));
    if (max > 0) {
      text += ` de ${fmtBytes(max)} disponibles`;
      if (size > max) text += ' — probablemente no cabe (la compresión puede ayudar con texto)';
    }
  }
  hint.textContent = text;
}

$('embed-run').addEventListener('click', () => withBusy($('embed-run'), 'embed-status', 'Cifrando y ocultando… (Argon2id tarda un momento)', async () => {
  if (!embed.cover) throw new Error('Elige primero una imagen portadora.');
  const pw = $('embed-password').value;
  if (!pw) throw new Error('Escribe una contraseña.');
  if (pw !== $('embed-password2').value) throw new Error('Las contraseñas no coinciden.');
  const form = new FormData();
  form.append('cover', embed.cover);
  if (embedKind() === 'text') {
    if (!$('embed-message').value) throw new Error('El mensaje está vacío.');
    form.append('message', $('embed-message').value);
  } else {
    if (!embed.payload) throw new Error('Elige el fichero que quieres ocultar.');
    form.append('payload', embed.payload);
  }
  form.append('password', pw);
  form.append('format', $('embed-format').value);
  form.append('bits', $('embed-bits').value);
  form.append('mode', $('embed-mode').value);
  form.append('layout', $('embed-layout').value);
  form.append('quality', $('embed-quality').value);
  form.append('compress', $('embed-compress').checked ? '1' : '0');
  const r = await api('/api/embed', form);
  showEmbedResult(r);
}));

function showEmbedResult(r) {
  const bytes = base64ToBytes(r.image);
  const box = $('embed-result');
  const preview = /^image\/(png|jpeg|gif|bmp)$/.test(r.mime)
    ? h('img', { class: 'preview-img', alt: 'Imagen resultante', src: URL.createObjectURL(new Blob([bytes], { type: r.mime })) })
    : null;
  box.replaceChildren(
    h('h2', null, 'Mensaje ocultado ', h('span', { class: 'tag ok', text: r.verified ? 'verificado' : 'sin verificar' })),
    h('dl', { class: 'stats' },
      stat('Formato / dominio', `${r.format}`, r.domain),
      stat('Bits por ranura', r.bits_per_slot),
      stat('Mensaje', fmtBytes(r.message_bytes), r.compressed ? `→ ${fmtBytes(r.packed_bytes)} comprimido` : ''),
      stat('Capacidad usada', fmtPct(r.fill_ratio), `de ${fmtBytes(r.capacity_bytes)}`),
      stat('Ranuras cambiadas', r.slots_changed.toLocaleString('es'), `de ${r.slots_used.toLocaleString('es')} escritas`),
      stat('Tamaño del fichero', fmtBytes(r.image_bytes), `antes ${fmtBytes(r.cover_bytes)}`)),
    r.notes.length ? h('ul', { class: 'notes' }, r.notes.map((n) => h('li', { text: n }))) : null,
    h('div', { class: 'actions' },
      h('button', { class: 'primary', onclick: () => downloadBytes(bytes, r.filename, r.mime) }, `Descargar ${r.filename}`),
      h('button', { class: 'secondary', onclick: () => sendTo('extract-image', bytes, r.filename, r.mime, 'extract') }, 'Probar a extraer'),
      h('button', { class: 'secondary', onclick: () => sendTo('analyze-image', bytes, r.filename, r.mime, 'analyze', embed.cover) }, 'Analizar resultado')),
    preview);
  box.hidden = false;
  box.scrollIntoView({ behavior: 'smooth', block: 'nearest' });
}

// Pasa la imagen generada a otra pestana como si se hubiera elegido alli.
function sendTo(inputId, bytes, name, mime, tab, cover) {
  const file = new File([bytes], name, { type: mime });
  const assign = (id, f) => {
    const dt = new DataTransfer();
    dt.items.add(f);
    $(id).files = dt.files;
    $(id).dispatchEvent(new Event('change'));
  };
  assign(inputId, file);
  if (cover && tab === 'analyze') assign('analyze-cover', cover);
  showTab(tab);
}

// ---------------------------------------------------------------------------
// EXTRAER
// ---------------------------------------------------------------------------
const extractState = { image: null };

setupDrop('extract-image-drop', 'extract-image', async (file) => {
  extractState.image = file;
  $('extract-result').hidden = true;
  const box = $('extract-image-info');
  box.replaceChildren(fileCard(file, fmtBytes(file.size)));
  try {
    const form = new FormData();
    form.append('image', file);
    box.replaceChildren(fileCard(file, describeInfo(await api('/api/info', form))));
  } catch (e) {
    box.replaceChildren(fileCard(file, ''), h('div', { class: 'status error', text: e.message }));
  }
});

$('extract-password').addEventListener('keydown', (e) => { if (e.key === 'Enter') $('extract-run').click(); });

$('extract-run').addEventListener('click', () => withBusy($('extract-run'), 'extract-status', 'Derivando la clave y buscando el mensaje…', async () => {
  if (!extractState.image) throw new Error('Elige primero la imagen.');
  if (!$('extract-password').value) throw new Error('Escribe la contraseña.');
  const form = new FormData();
  form.append('image', extractState.image);
  form.append('password', $('extract-password').value);
  form.append('layout', $('extract-layout').value);
  const r = await api('/api/extract', form);
  const bytes = base64ToBytes(r.data);
  const box = $('extract-result');
  if (r.kind === 'text') {
    const area = h('textarea', { class: 'message-out', readonly: true, rows: 8 });
    area.value = r.text;
    box.replaceChildren(h('h2', { text: `Mensaje recuperado (${fmtBytes(r.bytes)})` }), area,
      h('div', { class: 'actions' },
        h('button', { class: 'secondary', onclick: async (e) => {
          await navigator.clipboard.writeText(r.text);
          e.target.textContent = 'Copiado';
        } }, 'Copiar'),
        h('button', { class: 'secondary', onclick: () => downloadBytes(bytes, 'mensaje.txt', 'text/plain;charset=utf-8') }, 'Descargar .txt')));
  } else {
    box.replaceChildren(h('h2', { text: 'Fichero recuperado' }),
      h('dl', { class: 'stats' }, stat('Nombre', r.name || 'fichero.bin'), stat('Tamaño', fmtBytes(r.bytes))),
      h('div', { class: 'actions' },
        h('button', { class: 'primary', onclick: () => downloadBytes(bytes, r.name || 'fichero.bin') }, `Descargar ${r.name || 'fichero.bin'}`)));
  }
  box.hidden = false;
}));

// ---------------------------------------------------------------------------
// ANALIZAR
// ---------------------------------------------------------------------------
const analyzeState = { image: null, cover: null };

setupDrop('analyze-image-drop', 'analyze-image', (file) => {
  analyzeState.image = file;
  $('analyze-image-info').replaceChildren(fileCard(file, fmtBytes(file.size)));
});
setupDrop('analyze-cover-drop', 'analyze-cover', (file) => {
  analyzeState.cover = file;
  $('analyze-cover-info').replaceChildren(fileCard(file, fmtBytes(file.size)),
    h('button', { class: 'ghost small', onclick: () => {
      analyzeState.cover = null;
      $('analyze-cover').value = '';
      $('analyze-cover-info').replaceChildren();
    } }, 'Quitar portadora'));
});

$('analyze-run').addEventListener('click', () => withBusy($('analyze-run'), 'analyze-status', 'Analizando…', async () => {
  if (!analyzeState.image) throw new Error('Elige la imagen que quieres analizar.');
  const form = new FormData();
  form.append('image', analyzeState.image);
  if (analyzeState.cover) form.append('cover', analyzeState.cover);
  showAnalysis(await api('/api/analyze', form));
}));

function pngUrl(b64) { return URL.createObjectURL(new Blob([base64ToBytes(b64)], { type: 'image/png' })); }

function showAnalysis(r) {
  const box = $('analyze-result');
  const cards = [];
  if (r.chi_square) {
    cards.push(verdictCard('Chi-cuadrado (Westfeld-Pfitzmann)', `p = ${r.chi_square.p.toFixed(3)}`, r.chi_square.verdict,
      'Probabilidad de que los pares de valores (2i, 2i+1) estén igualados, como deja la sustitución LSB.'));
  }
  if (r.rs) {
    cards.push(verdictCard('Análisis RS (Fridrich)', fmtPct(r.rs.estimated_rate), r.rs.verdict,
      'Fracción estimada de muestras con mensaje. Fiable con sustitución LSB; LSB matching lo resiste.'));
  }
  if (r.chi_square_dct) {
    cards.push(verdictCard('Chi-cuadrado DCT (JSteg)', `p = ${r.chi_square_dct.p.toFixed(3)}`, r.chi_square_dct.verdict,
      'Pares de valores en los coeficientes DCT. En JPEG los tests sobre píxeles no tienen sentido.'));
  }
  if (r.quality) {
    cards.push(h('div', { class: 'verdict' }, h('div', { class: 'label', text: 'Distorsión respecto a la portadora' }),
      h('div', { class: 'value', text: fmtPsnr(r.quality.psnr) }),
      h('p', { text: `SSIM ${r.quality.ssim.toFixed(5)} · MSE ${r.quality.mse.toFixed(4)} · ` +
        `${fmtPct(r.quality.changed_ratio, 2)} de muestras cambiadas · diferencia máx. ${r.quality.max_abs_diff}` })));
  }
  const parts = [h('div', { class: 'grid three' }, cards)];
  if (r.format === 'GIF') {
    parts.push(h('div', { class: 'status info', text: 'En GIF los datos van en los índices de la paleta: los tests LSB sobre colores son solo orientativos.' }));
  }

  const charts = [];
  if (r.chi_square_sweep) {
    charts.push(h('div', { class: 'card' }, h('h3', { text: 'Barrido chi-cuadrado' }),
      lineChart({
        series: [{ name: 'p', cls: 's1', points: r.chi_square_sweep.map((p) => [p.fraction * 100, p.p]) }],
        xLabel: 'Porción de la imagen analizada (%)', yLabel: 'p', xDomain: [0, 100], yDomain: [0, 1],
        yFmt: (v) => v.toFixed(1), threshold: 0.95,
      }),
      h('p', { class: 'hint', text: 'Con inserción secuencial, p es alto al principio y cae donde termina el mensaje.' })));
  }
  const channelCls = r.histograms.length === 1 ? ['sk'] : ['sr', 'sg', 'sb'];
  const channelName = r.histograms.length === 1 ? ['Gris'] : ['Rojo', 'Verde', 'Azul'];
  charts.push(h('div', { class: 'card' }, h('h3', { text: 'Histograma' }),
    lineChart({
      series: r.histograms.map((hist, c) => ({ name: channelName[c], cls: channelCls[c], points: hist.map((v, i) => [i, v]) })),
      xLabel: 'Valor', yLabel: 'Muestras', xDomain: [0, 255], dots: false,
      yFmt: (v) => (v >= 1000 ? `${(v / 1000).toFixed(v >= 10000 ? 0 : 1)}k` : v),
    })));
  parts.push(h('div', { class: 'grid two' }, charts));

  const figs = [
    h('figure', { class: 'figure' }, h('img', { src: pngUrl(r.bit_plane_0), alt: 'Plano de bits 0' }),
      h('figcaption', { text: 'Plano de bits 0 (LSB): en una foto natural conserva algo de estructura; un mensaje cifrado parece ruido puro.' })),
    h('figure', { class: 'figure' }, h('img', { src: pngUrl(r.bit_plane_1), alt: 'Plano de bits 1' }),
      h('figcaption', { text: 'Plano de bits 1: referencia (solo cambia si se usan 2 o más bits por ranura).' })),
  ];
  if (r.difference_map) {
    figs.push(h('figure', { class: 'figure' }, h('img', { src: pngUrl(r.difference_map), alt: 'Mapa de diferencias' }),
      h('figcaption', { text: 'Mapa de diferencias |portadora − imagen| ×64: dónde se ha escrito.' })));
  }
  parts.push(h('div', { class: 'card' }, h('h3', { text: `Visualizaciones (${r.width} × ${r.height})` }),
    h('div', { class: 'grid three' }, figs)));

  box.replaceChildren(...parts);
  box.hidden = false;
  box.scrollIntoView({ behavior: 'smooth', block: 'start' });
}

// ---------------------------------------------------------------------------
// EXPERIMENTO
// ---------------------------------------------------------------------------
const benchState = { cover: null };

setupDrop('bench-cover-drop', 'bench-cover', async (file) => {
  benchState.cover = file;
  const box = $('bench-cover-info');
  box.replaceChildren(fileCard(file, fmtBytes(file.size)));
  try {
    const form = new FormData();
    form.append('image', file);
    box.replaceChildren(fileCard(file, describeInfo(await api('/api/info', form))));
  } catch (e) {
    box.replaceChildren(fileCard(file, ''), h('div', { class: 'status error', text: e.message }));
  }
});

const checkedValues = (name) => [...document.querySelectorAll(`input[name="${name}"]:checked`)].map((i) => i.value);

$('bench-run').addEventListener('click', () => withBusy($('bench-run'), 'bench-status', 'Ejecutando el experimento… puede tardar con imágenes grandes', async () => {
  if (!benchState.cover) throw new Error('Elige una imagen portadora.');
  const bits = checkedValues('bench-bits');
  const modes = checkedValues('bench-modes');
  const layouts = checkedValues('bench-layouts');
  if (!bits.length || !modes.length || !layouts.length) throw new Error('Marca al menos un valor en cada grupo.');
  const fills = $('bench-fills').value.split(/[,;\s]+/).filter(Boolean).map((v) => v.replace(',', '.'));
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
  showBench(await api('/api/bench', form));
}));

const MODE_ES = { replace: 'sustitución', match: 'matching' };
const LAYOUT_ES = { random: 'aleatorio', sequential: 'secuencial' };

function showBench(r) {
  const box = $('bench-result');
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
  const parts = [];
  const base = r.baseline;
  parts.push(h('div', { class: 'card' }, h('h3', { text: `Salida ${r.output} · dominio ${r.domain}` }),
    base ? h('p', { class: 'hint', text: isDct
      ? `Portadora sin mensaje: chi² DCT p = ${fmtNum(base.dct_chi2_p)}`
      : `Portadora sin mensaje: chi² p = ${fmtNum(base.chi2_p)} · RS = ${fmtNum(base.rs_rate)}` }) : null,
    h('p', { class: 'hint', text: 'Líneas discontinuas: orden secuencial. Pasa el ratón por los puntos para ver los valores.' })));

  if (ok.length) {
    const psnrSeries = series((run) => run.psnr);
    const charts = [];
    if (psnrSeries.length) {
      charts.push(h('div', { class: 'card' }, h('h3', { text: 'Distorsión (PSNR)' }),
        lineChart({ series: psnrSeries, xLabel: 'Llenado (% de la capacidad)', yLabel: 'PSNR (dB)', yFmt: (v) => v.toFixed(0) })));
    }
    const detSeries = isDct ? series((run) => run.dct_chi2_p) : series((run) => run.rs_rate);
    if (detSeries.length) {
      charts.push(h('div', { class: 'card' }, h('h3', { text: isDct ? 'Detección: chi² DCT (p)' : 'Detección: tasa estimada por RS' }),
        lineChart({ series: detSeries, xLabel: 'Llenado (% de la capacidad)', yLabel: isDct ? 'p' : 'Tasa RS',
          yDomain: [0, 1], yFmt: (v) => v.toFixed(1) })));
    }
    if (!isDct) {
      const chiSeries = series((run) => run.chi2_p);
      if (chiSeries.length) {
        charts.push(h('div', { class: 'card' }, h('h3', { text: 'Detección: chi-cuadrado (p)' }),
          lineChart({ series: chiSeries, xLabel: 'Llenado (% de la capacidad)', yLabel: 'p', yDomain: [0, 1],
            yFmt: (v) => v.toFixed(1), threshold: 0.95 })));
      }
    }
    parts.push(h('div', { class: 'grid two' }, charts));
  }

  const head = ['k', 'Modo', 'Orden', 'Llenado', 'Mensaje', 'Cambiadas', 'PSNR', 'SSIM', 'chi² p', 'RS', 'DCT p', 'Tiempo'];
  const rows = r.runs.map((run) => run.error
    ? h('tr', { class: 'err' }, h('td', { text: run.bits }), h('td', { text: MODE_ES[run.mode] }), h('td', { text: LAYOUT_ES[run.layout] }),
      h('td', { class: 'num', text: fmtPct(run.fill, 0) }), h('td', { colspan: 8, text: run.error }))
    : h('tr', null, h('td', { text: run.bits }), h('td', { text: MODE_ES[run.mode] }), h('td', { text: LAYOUT_ES[run.layout] }),
      h('td', { class: 'num', text: fmtPct(run.fill, 0) }), h('td', { class: 'num', text: fmtBytes(run.message_bytes) }),
      h('td', { class: 'num', text: fmtPct(run.changed_ratio, 2) }), h('td', { class: 'num', text: fmtPsnr(run.psnr) }),
      h('td', { class: 'num', text: run.ssim.toFixed(4) }), h('td', { class: 'num', text: fmtNum(run.chi2_p) }),
      h('td', { class: 'num', text: fmtNum(run.rs_rate) }), h('td', { class: 'num', text: fmtNum(run.dct_chi2_p) }),
      h('td', { class: 'num', text: `${run.seconds.toFixed(2)} s` })));
  parts.push(h('div', { class: 'card' },
    h('h3', { text: 'Resultados' }),
    h('div', { class: 'table-wrap' }, h('table', null,
      h('thead', null, h('tr', null, head.map((t, i) => h('th', { class: i >= 3 ? 'num' : null, text: t })))),
      h('tbody', null, rows))),
    h('div', { class: 'actions' }, h('button', { class: 'secondary', onclick: () => downloadBenchCsv(r) }, 'Descargar CSV'))));

  box.replaceChildren(...parts);
  box.hidden = false;
  box.scrollIntoView({ behavior: 'smooth', block: 'start' });
}

function downloadBenchCsv(r) {
  const cols = ['bits', 'mode', 'layout', 'fill', 'message_bytes', 'changed_ratio', 'psnr', 'ssim', 'chi2_p', 'rs_rate', 'dct_chi2_p', 'seconds', 'error'];
  const esc = (v) => (v == null ? '' : /[",\n]/.test(String(v)) ? `"${String(v).replace(/"/g, '""')}"` : String(v));
  const csv = [cols.join(','), ...r.runs.map((run) => cols.map((c) => esc(run[c])).join(','))].join('\n');
  downloadBytes(new TextEncoder().encode(csv + '\n'), 'icpu_experimento.csv', 'text/csv;charset=utf-8');
}

// ---------------------------------------------------------------------------
// Arranque y cierre
// ---------------------------------------------------------------------------
$('shutdown').addEventListener('click', async () => {
  if (!confirm('¿Cerrar I-CPU? El servidor local se detendrá.')) return;
  try { await api('/api/shutdown', new FormData()); } catch { /* ya estaba cerrado */ }
  document.querySelector('main').hidden = true;
  document.querySelector('.topbar').hidden = true;
  $('closed').hidden = false;
});

(async () => {
  try {
    const saved = sessionStorage.getItem('icpu-tab');
    if (saved && $(`tab-${saved}`)) showTab(saved);
  } catch { /* sin almacenamiento */ }
  try {
    $('version').textContent = `v${(await api('/api/version')).version}`;
  } catch (e) {
    setStatus('embed-status', 'error', TOKEN ? e.message
      : 'Falta el token de sesión: abre I-CPU desde el enlace que aparece en la consola.');
  }
})();
