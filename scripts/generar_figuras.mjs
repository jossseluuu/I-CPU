// Genera las figuras de la documentacion (docs/img/experimentos y docs/img/gui) a
// partir de experimentos reales con I-CPU. Asi cada grafica de la documentacion se
// puede reproducir y comprobar.
//
//   node scripts/generar_figuras.mjs              graficas SVG y figuras PNG
//   node scripts/generar_figuras.mjs --capturas   ademas, capturas de la GUI (tema claro y oscuro)
//
// Requisitos: I-CPU compilado (scripts\build.bat), Node.js 22 o posterior y Microsoft
// Edge o Google Chrome (se usan en modo headless para componer las figuras PNG y para
// las capturas). Sin dependencias de npm.
//
// Las imagenes llevan un mensaje aleatorio y sal y nonce nuevos en cada ejecucion: las
// cifras cambian un poco de una vez a otra, pero no las conclusiones. El resumen
// numerico queda en docs/img/experimentos/resultados.json.

import { spawn, spawnSync } from 'node:child_process';
import { existsSync, mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { randomBytes } from 'node:crypto';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const EXE = process.platform === 'win32' ? '.exe' : '';
const BUILD = ['build/msvc-release', 'build/release'].map((b) => path.join(ROOT, b)).find((b) => existsSync(b));
if (!BUILD) fail('No hay ninguna compilacion. Ejecuta antes scripts\\build.bat');
const ICPU = path.join(BUILD, 'apps', 'cli', `icpu${EXE}`);
const ICPU_GUI = path.join(BUILD, 'apps', 'gui', `icpu-gui${EXE}`);
const WORK = path.join(BUILD, 'figuras');
const OUT_EXP = path.join(ROOT, 'docs', 'img', 'experimentos');
const OUT_GUI = path.join(ROOT, 'docs', 'img', 'gui');
const COVER = path.join(ROOT, 'samples', 'paisaje.png');
const COVER_GIF = path.join(ROOT, 'samples', 'paisaje.gif');
const PASSWORD = 'icpu-figuras';
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

function fail(msg) { console.error(`[figuras] ${msg}`); process.exit(1); }
function log(msg) { console.log(`[figuras] ${msg}`); }

function icpu(...args) {
  const r = spawnSync(ICPU, args, { env: { ...process.env, ICPU_PASSWORD: PASSWORD }, encoding: 'utf8', maxBuffer: 1 << 28 });
  if (r.status !== 0) fail(`icpu ${args.join(' ')}\n${r.stdout}\n${r.stderr}`);
  return r.stdout;
}

function findBrowser() {
  const candidates = [
    process.env.ICPU_BROWSER,
    'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',
    'C:/Program Files/Microsoft/Edge/Application/msedge.exe',
    'C:/Program Files/Google/Chrome/Application/chrome.exe',
    '/usr/bin/microsoft-edge', '/usr/bin/google-chrome', '/usr/bin/chromium', '/usr/bin/chromium-browser',
  ];
  return candidates.find((c) => c && existsSync(c));
}

// ---------------------------------------------------------------------------
// CSV
// ---------------------------------------------------------------------------
function readCsv(file) {
  const [head, ...rows] = readFileSync(file, 'utf8').trim().split(/\r?\n/);
  const keys = head.split(',');
  return rows.map((line) => {
    const cells = line.split(',');
    const o = {};
    keys.forEach((k, i) => { const v = cells[i]?.replace(/^"|"$/g, ''); o[k] = v !== '' && !Number.isNaN(Number(v)) ? Number(v) : v; });
    return o;
  });
}

// ---------------------------------------------------------------------------
// Graficas SVG (con tema claro y oscuro: GitHub las muestra en <img>, que respeta
// prefers-color-scheme)
// ---------------------------------------------------------------------------
const PALETTE = {
  adaptive: '#4f46e5', match: '#0891b2', replace: '#ea580c', replace2: '#c026d3', sequential: '#dc2626',
  cumulative: '#94a3b8', window: '#dc2626', cover: '#16a34a',
};
const esc = (s) => String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
const fmt = (v, d = 1) => v.toFixed(d).replace('.', ',');

function svgFrame(w, h, body, title) {
  return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ${w} ${h}" width="${w}" height="${h}" role="img" aria-label="${esc(title)}">
<style>
  .bg { fill: #ffffff; stroke: #e6e7eb; }
  .t { fill: #111318; font: 700 17px 'Segoe UI', Inter, system-ui, sans-serif; }
  .st { fill: #6b7180; font: 400 12.5px 'Segoe UI', Inter, system-ui, sans-serif; }
  .ax { fill: #6b7180; font: 400 11.5px 'Segoe UI', Inter, system-ui, sans-serif; }
  .axl { fill: #3c4049; font: 600 12px 'Segoe UI', Inter, system-ui, sans-serif; }
  .lg { fill: #3c4049; font: 500 12px 'Segoe UI', Inter, system-ui, sans-serif; }
  .lbl { fill: #111318; font: 600 12px 'Segoe UI', Inter, system-ui, sans-serif; }
  .grid { stroke: #eef0f3; }
  .axis { stroke: #d4d6dc; }
  .thr { stroke: #c62828; stroke-dasharray: 4 4; opacity: .7; }
  .thrt { fill: #c62828; font: 500 11px 'Segoe UI', Inter, system-ui, sans-serif; }
  .band { fill: #fdecec; }
  @media (prefers-color-scheme: dark) {
    .bg { fill: #15181e; stroke: #262b35; }
    .t, .lbl { fill: #eceef2; } .st, .ax { fill: #8f97a6; } .axl, .lg { fill: #c7cbd3; }
    .grid { stroke: #222731; } .axis { stroke: #343a46; }
    .thr { stroke: #f87171; } .thrt { fill: #f87171; } .band { fill: #2c1517; }
  }
</style>
<rect class="bg" x=".5" y=".5" width="${w - 1}" height="${h - 1}" rx="14"/>
${body}
</svg>
`;
}

function lineChart({ title, subtitle, xLabel, yLabel, series, xDomain, yDomain, xTicks, yTicks, xFmt = (v) => fmt(v, 0), yFmt = (v) => fmt(v, 1), threshold, bands = [] }) {
  const W = 760, H = 440, L = 70, R = 24, T = 78, B = 96;
  const pw = W - L - R, ph = H - T - B;
  const x = (v) => L + ((v - xDomain[0]) / (xDomain[1] - xDomain[0])) * pw;
  const y = (v) => T + ph - ((v - yDomain[0]) / (yDomain[1] - yDomain[0])) * ph;
  let b = `<text class="t" x="24" y="36">${esc(title)}</text>`;
  if (subtitle) b += `<text class="st" x="24" y="56">${esc(subtitle)}</text>`;
  for (const [a, z, label] of bands) {
    b += `<rect class="band" x="${x(a)}" y="${T}" width="${x(z) - x(a)}" height="${ph}"/>`;
    if (label) b += `<text class="thrt" x="${(x(a) + x(z)) / 2}" y="${T + ph / 2}" text-anchor="middle">${esc(label)}</text>`;
  }
  for (const t of yTicks) b += `<line class="grid" x1="${L}" x2="${W - R}" y1="${y(t)}" y2="${y(t)}"/><text class="ax" x="${L - 8}" y="${y(t) + 4}" text-anchor="end">${esc(yFmt(t))}</text>`;
  for (const t of xTicks) b += `<line class="grid" x1="${x(t)}" x2="${x(t)}" y1="${T}" y2="${T + ph}"/><text class="ax" x="${x(t)}" y="${T + ph + 18}" text-anchor="middle">${esc(xFmt(t))}</text>`;
  b += `<line class="axis" x1="${L}" x2="${W - R}" y1="${T + ph}" y2="${T + ph}"/><line class="axis" x1="${L}" x2="${L}" y1="${T}" y2="${T + ph}"/>`;
  if (threshold != null) b += `<line class="thr" x1="${L}" x2="${W - R}" y1="${y(threshold.value)}" y2="${y(threshold.value)}"/><text class="thrt" x="${W - R - 4}" y="${y(threshold.value) - 6}" text-anchor="end">${esc(threshold.label)}</text>`;
  b += `<text class="axl" x="${L + pw / 2}" y="${T + ph + 40}" text-anchor="middle">${esc(xLabel)}</text>`;
  b += `<text class="axl" transform="translate(20 ${T + ph / 2}) rotate(-90)" text-anchor="middle">${esc(yLabel)}</text>`;
  for (const s of series) {
    const pts = s.points.filter(([px, py]) => Number.isFinite(px) && Number.isFinite(py));
    const d = pts.map(([px, py], i) => `${i ? 'L' : 'M'}${x(px).toFixed(1)},${y(Math.min(Math.max(py, yDomain[0]), yDomain[1])).toFixed(1)}`).join(' ');
    b += `<path d="${d}" fill="none" stroke="${s.color}" stroke-width="2.4" stroke-linejoin="round" stroke-linecap="round"${s.dash ? ' stroke-dasharray="6 5"' : ''}/>`;
    for (const [px, py] of pts) b += `<circle cx="${x(px).toFixed(1)}" cy="${y(Math.min(Math.max(py, yDomain[0]), yDomain[1])).toFixed(1)}" r="3.4" fill="${s.color}"/>`;
  }
  // Leyenda en una o dos filas
  let lx = L, ly = H - 26;
  for (const s of series) {
    const w = 34 + s.name.length * 6.6;
    if (lx + w > W - R) { lx = L; ly += 20; }
    b += `<line x1="${lx}" x2="${lx + 22}" y1="${ly - 4}" y2="${ly - 4}" stroke="${s.color}" stroke-width="3"${s.dash ? ' stroke-dasharray="5 4"' : ''}/><text class="lg" x="${lx + 28}" y="${ly}">${esc(s.name)}</text>`;
    lx += w + 14;
  }
  return svgFrame(W, H + (ly - (H - 26)), b, title);
}

function barChart({ title, subtitle, groups, unit }) {
  // groups: [{ label, bars: [{ name, value, color }] }]
  const W = 760, L = 140, R = 90, T = 84, rowH = 26, gap = 14, head = 24;
  const max = Math.max(...groups.flatMap((g) => g.bars.map((b) => b.value)));
  const H = T + groups.reduce((a, g) => a + head + g.bars.length * rowH + gap, 0) + 10;
  const x = (v) => L + (v / max) * (W - L - R);
  let b = `<text class="t" x="24" y="36">${esc(title)}</text>`;
  if (subtitle) b += `<text class="st" x="24" y="56">${esc(subtitle)}</text>`;
  let yy = T;
  for (const g of groups) {
    b += `<text class="lbl" x="24" y="${yy + 14}">${esc(g.label)}</text><line class="grid" x1="24" x2="${W - 24}" y1="${yy + 21}" y2="${yy + 21}"/>`;
    yy += head;
    for (const bar of g.bars) {
      b += `<rect x="${L}" y="${yy + 4}" width="${Math.max(2, x(bar.value) - L)}" height="${rowH - 8}" rx="4" fill="${bar.color}"/>`;
      b += `<text class="ax" x="${x(bar.value) + 8}" y="${yy + rowH / 2 + 4}">${esc(`${fmt(bar.value, 1)} ${unit}`)}</text>`;
      b += `<text class="lg" x="${L - 10}" y="${yy + rowH / 2 + 4}" text-anchor="end">${esc(bar.name)}</text>`;
      yy += rowH;
    }
    yy += gap;
  }
  return svgFrame(W, H, b, title);
}

// ---------------------------------------------------------------------------
// Figuras PNG compuestas en un navegador headless (canvas)
// ---------------------------------------------------------------------------
const dataUri = (file) => `data:image/png;base64,${readFileSync(file).toString('base64')}`;

// Abre la pagina, espera a que marque window.ready y captura solo la tarjeta, con fondo transparente.
async function renderHtml(browser, html, out, width) {
  const page = path.join(WORK, `${path.basename(out, '.png')}.html`);
  writeFileSync(page, html);
  const b = await cdpLaunch(browser, { width, height: 800, port: 9611 });
  await b.send('Emulation.setDefaultBackgroundColorOverride', { color: { r: 0, g: 0, b: 0, a: 0 } });
  await b.goto(pathToFileURL(page).href);
  for (let i = 0; i < 100 && !(await b.evaluate('window.ready === true')); i++) await sleep(100);
  const h = await b.evaluate(`Math.ceil(document.querySelector('.card').getBoundingClientRect().height)`);
  await b.metrics(h);
  await sleep(300);
  await b.shot(out, { x: 0, y: 0, width, height: h });
  b.close();
  await sleep(500);
}

const FIGURE_CSS = `
  body { margin: 0; font-family: 'Segoe UI', Inter, system-ui, sans-serif; background: transparent; }
  .card { margin: 0; padding: 22px 24px; background: #fff; border: 1px solid #e6e7eb; border-radius: 16px; box-sizing: border-box; }
  h1 { margin: 0; font-size: 19px; color: #111318; } .sub { margin: 4px 0 16px; font-size: 13px; color: #6b7180; }
  .row { display: grid; gap: 14px; }
  figure { margin: 0; } canvas, img { display: block; width: 100%; border-radius: 10px; image-rendering: pixelated; }
  figcaption { margin-top: 8px; font-size: 13px; color: #3c4049; line-height: 1.45; }
  figcaption b { color: #111318; font-size: 14px; display: block; }
  .tag { display: inline-block; padding: 1px 7px; border-radius: 99px; font: 600 11.5px/18px ui-monospace, Consolas, monospace; background: #f3f4f6; color: #3c4049; margin: 2px 4px 0 0; }
  .bad { background: #fdecec; color: #c62828; } .ok { background: #e9f7ee; color: #15803d; }
`;

// Script comun: carga imagenes (data URI: el canvas no queda "contaminado") y
// pinta mascaras de cambios o ampliaciones.
const FIGURE_JS = `
  const load = (src) => new Promise((ok) => { const i = new Image(); i.onload = () => ok(i); i.src = src; });
  function pixels(img) { const c = document.createElement('canvas'); c.width = img.width; c.height = img.height; const g = c.getContext('2d'); g.drawImage(img, 0, 0); return g.getImageData(0, 0, c.width, c.height); }
  // Portadora en gris tenue y, encima, los pixeles cambiados en color
  function changeMask(canvas, cover, stego, color) {
    const a = pixels(cover), b = pixels(stego); canvas.width = a.width; canvas.height = a.height;
    const g = canvas.getContext('2d'); const out = g.createImageData(a.width, a.height);
    for (let i = 0; i < a.data.length; i += 4) {
      const changed = a.data[i] !== b.data[i] || a.data[i + 1] !== b.data[i + 1] || a.data[i + 2] !== b.data[i + 2];
      const l = (a.data[i] * .3 + a.data[i + 1] * .59 + a.data[i + 2] * .11) * .25 + 12;
      out.data[i] = changed ? color[0] : l; out.data[i + 1] = changed ? color[1] : l; out.data[i + 2] = changed ? color[2] : l + 4; out.data[i + 3] = 255;
    }
    g.putImageData(out, 0, 0);
  }
  // Diferencia absoluta amplificada x64
  function diff64(canvas, cover, stego) {
    const a = pixels(cover), b = pixels(stego); canvas.width = a.width; canvas.height = a.height;
    const g = canvas.getContext('2d'); const out = g.createImageData(a.width, a.height);
    for (let i = 0; i < a.data.length; i += 4) { for (let c = 0; c < 3; c++) out.data[i + c] = Math.min(255, Math.abs(a.data[i + c] - b.data[i + c]) * 64); out.data[i + 3] = 255; }
    g.putImageData(out, 0, 0);
  }
  // Recorte ampliado (vecino mas proximo) con rejilla
  function zoom(canvas, img, sx, sy, n, scale) {
    canvas.width = n * scale; canvas.height = n * scale; const g = canvas.getContext('2d'); g.imageSmoothingEnabled = false;
    g.drawImage(img, sx, sy, n, n, 0, 0, n * scale, n * scale); g.strokeStyle = 'rgba(0,0,0,.18)';
    for (let k = 0; k <= n; k++) { g.beginPath(); g.moveTo(k * scale + .5, 0); g.lineTo(k * scale + .5, n * scale); g.stroke(); g.beginPath(); g.moveTo(0, k * scale + .5); g.lineTo(n * scale, k * scale + .5); g.stroke(); }
  }
`;

// ---------------------------------------------------------------------------
// Capturas de la GUI (CDP)
// ---------------------------------------------------------------------------
async function cdpLaunch(browser, { width, height, port, dark }) {
  const profile = path.join(WORK, `perfil-cdp-${port}`);
  rmSync(profile, { recursive: true, force: true });
  const proc = spawn(browser, ['--headless=new', '--disable-gpu', `--remote-debugging-port=${port}`, `--user-data-dir=${profile}`,
    `--window-size=${width},${height}`, '--hide-scrollbars', '--no-first-run', 'about:blank'], { stdio: 'ignore' });
  let targets;
  for (let i = 0; i < 80 && !targets; i++) { try { targets = await (await fetch(`http://127.0.0.1:${port}/json`)).json(); } catch { await sleep(150); } }
  const ws = new WebSocket(targets.find((t) => t.type === 'page').webSocketDebuggerUrl);
  await new Promise((r) => ws.addEventListener('open', r));
  let id = 0; const pending = new Map();
  ws.addEventListener('message', (ev) => { const m = JSON.parse(ev.data); if (m.id && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); } });
  const send = (method, params = {}) => new Promise((ok) => { const i = ++id; pending.set(i, ok); ws.send(JSON.stringify({ id: i, method, params })); });
  await send('Page.enable');
  const metrics = (h) => send('Emulation.setDeviceMetricsOverride', { width, height: h, deviceScaleFactor: 1, mobile: false });
  await metrics(height);
  if (dark) await send('Emulation.setEmulatedMedia', { features: [{ name: 'prefers-color-scheme', value: 'dark' }] });
  const evaluate = async (expr) => (await send('Runtime.evaluate', { expression: expr, awaitPromise: true, returnByValue: true })).result?.result?.value;
  return {
    send, evaluate, metrics,
    goto: async (url) => { await send('Page.navigate', { url }); await sleep(1500); },
    shot: async (file, clip) => { const r = await send('Page.captureScreenshot', { format: 'png', ...(clip ? { clip: { ...clip, scale: 1 } } : {}) }); writeFileSync(file, Buffer.from(r.result.data, 'base64')); },
    close: () => { try { ws.close(); } catch { /* */ } proc.kill(); },
  };
}

async function captureGui(browser, theme, port) {
  const W = 1280, H = 800;
  const gui = spawn(ICPU_GUI, ['--no-browser', '--port', String(port)]);
  let out = ''; gui.stdout.on('data', (d) => { out += d; });
  for (let i = 0; i < 80 && !/#t=\w+/.test(out); i++) await sleep(100);
  const url = out.match(/http:\/\/127\.0\.0\.1:\d+\/#t=\w+/)?.[0];
  if (!url) fail(`icpu-gui no arranco:\n${out}`);
  const b = await cdpLaunch(browser, { width: W, height: H, port: port + 1000, dark: theme === 'dark' });
  await b.goto(url);
  await b.evaluate(`localStorage.setItem('icpu-theme', '${theme}')`);
  await b.goto(url);
  const file = (n) => path.join(OUT_GUI, `${n}-${theme === 'dark' ? 'oscuro' : 'claro'}.png`);
  const waitFor = async (expr, ms = 600000) => { const t0 = Date.now(); while (Date.now() - t0 < ms) { if (await b.evaluate(expr)) return; await sleep(300); } fail(`tiempo agotado: ${expr}`); };
  const setFile = async (id, f) => {
    const doc = await b.send('DOM.getDocument', { depth: 1 });
    const q = await b.send('DOM.querySelector', { nodeId: doc.result.root.nodeId, selector: `#${id}` });
    await b.send('DOM.setFileInputFiles', { nodeId: q.result.nodeId, files: [f] });
    await sleep(900);
  };
  const setVal = (id, v) => b.evaluate(`(() => { const e = document.getElementById('${id}'); e.value = ${JSON.stringify(v)}; e.dispatchEvent(new Event('input', { bubbles: true })); })()`);
  const click = (id) => b.evaluate(`document.getElementById('${id}').click()`);
  const done = (id) => `document.getElementById('${id}-result').children.length > 0 && !document.getElementById('${id}-run').disabled`;
  const full = async (name) => {
    await b.evaluate('window.scrollTo(0, 0)');
    await b.metrics(await b.evaluate('Math.ceil(document.documentElement.scrollHeight)'));
    await sleep(600); await b.shot(file(name)); await b.metrics(H); await sleep(200);
  };
  const element = async (name, selector) => {
    const r = await b.evaluate(`(() => { const e = document.querySelector('${selector}'); e.scrollIntoView(); const r = e.getBoundingClientRect(); return { x: r.x, y: r.y + scrollY, width: r.width, height: r.height }; })()`);
    await b.evaluate('window.scrollTo(0, 0)');
    await b.metrics(await b.evaluate('Math.ceil(document.documentElement.scrollHeight)'));
    await sleep(500); await b.shot(file(name), { x: Math.max(0, r.x - 16), y: Math.max(0, r.y - 16), width: r.width + 32, height: r.height + 32 });
    await b.metrics(H); await sleep(200);
  };

  await click('tabbtn-embed');
  await setFile('embed-cover', COVER);
  await setVal('embed-message', 'Nos vemos el jueves a las 20:00 en la biblioteca. Trae el USB.');
  await setVal('embed-password', 'correcto-caballo-bateria-grapa');
  await setVal('embed-password2', 'correcto-caballo-bateria-grapa');
  await sleep(400);
  await b.shot(file('ocultar'));
  await click('embed-run'); await waitFor(done('embed')); await sleep(1200);
  await element('ocultar-resultado', '#embed-result');
  await click('tabbtn-extract');
  await setFile('extract-image', path.join(ROOT, 'samples', 'demo_mensaje.png'));
  await setVal('extract-password', 'icpu-demo');
  await click('extract-run'); await waitFor(done('extract')); await sleep(600);
  await b.shot(file('extraer'));
  await click('tabbtn-analyze');
  await setFile('analyze-image', path.join(ROOT, 'samples', 'demo_secuencial.png'));
  await setFile('analyze-cover', COVER);
  await click('analyze-run'); await waitFor(done('analyze')); await sleep(1000);
  await full('analizar');
  await click('tabbtn-bench');
  await setFile('bench-cover', COVER);
  await click('bench-run'); await sleep(2500);
  await b.shot(file('experimento-progreso'));
  await waitFor(done('bench')); await sleep(1200);
  await element('experimento-graficas', '#bench-result');
  await click('tabbtn-embed');
  await click('about-btn'); await sleep(400); await b.shot(file('acerca-de'));
  await b.evaluate(`document.getElementById('about').close()`);
  await click('history-btn'); await sleep(500); await b.shot(file('historial'));
  await click('history-close'); await click('shutdown'); await sleep(400); await b.shot(file('apagado'));
  await click('shutdown-confirm'); await sleep(1500); await b.shot(file('cerrado'));
  b.close();
  gui.kill();
}

// ---------------------------------------------------------------------------
// Programa
// ---------------------------------------------------------------------------
rmSync(WORK, { recursive: true, force: true });
for (const d of [WORK, OUT_EXP, OUT_GUI]) mkdirSync(d, { recursive: true });
const browser = findBrowser();
if (!browser) fail('No se encuentra Edge ni Chrome (puedes indicarlo con la variable ICPU_BROWSER)');

// 1) Experimentos sistematicos en los tres dominios
log('Experimento PNG (dominio espacial)...');
icpu('bench', COVER, '--bits', '1,2', '--csv', path.join(WORK, 'bench_png.csv'));
log('Experimento JPEG (dominio DCT)...');
icpu('bench', COVER, '--format', 'jpg', '--bits', '1', '--modes', 'adaptive,replace', '--layouts', 'random', '--csv', path.join(WORK, 'bench_jpg.csv'));
log('Experimento GIF (dominio de paleta)...');
icpu('bench', COVER_GIF, '--bits', '1', '--modes', 'adaptive,replace', '--layouts', 'random', '--csv', path.join(WORK, 'bench_gif.csv'));

// 2) El mismo mensaje (12 KB aleatorios) con cada modo, y su analisis
log('Mismo mensaje con cada modo...');
const msg = path.join(WORK, 'mensaje.bin');
writeFileSync(msg, randomBytes(12000));
const cases = {
  adaptive: ['--mode', 'adaptive'],
  match: ['--mode', 'match'],
  replace: ['--mode', 'replace', '--bits', '1'],
  sequential: ['--mode', 'replace', '--bits', '1', '--layout', 'sequential'],
};
const analysis = { cover: JSON.parse(icpu('analyze', COVER, '--json')) };
for (const [name, args] of Object.entries(cases)) {
  const out = path.join(WORK, `${name}.png`);
  icpu('embed', COVER, '-o', out, '-f', msg, '--no-compress', '--force', ...args);
  analysis[name] = JSON.parse(icpu('analyze', out, '--cover', COVER, '--json'));
}
const info = { png: JSON.parse(icpu('info', COVER, '--json')), gif: JSON.parse(icpu('info', COVER_GIF, '--json')) };

// 3) Graficas SVG
log('Graficas SVG...');
const png = readCsv(path.join(WORK, 'bench_png.csv')).filter((r) => !r.error);
const pick = (rows, mode, layout, bits = 1) => rows.filter((r) => r.modo === mode && r.orden === layout && r.bits === bits).sort((a, b) => a.bytes - b.bytes);
const kib = (r) => r.bytes / 1024;
const sizeSeries = (metric) => [
  { name: 'Adaptativo (STC)', color: PALETTE.adaptive, points: pick(png, 'adaptive', 'random').map((r) => [kib(r), metric(r)]) },
  { name: 'LSB matching', color: PALETTE.match, points: pick(png, 'match', 'random').map((r) => [kib(r), metric(r)]) },
  { name: 'Sustitución LSB, k = 1', color: PALETTE.replace, points: pick(png, 'replace', 'random').map((r) => [kib(r), metric(r)]) },
  { name: 'Sustitución LSB, k = 2', color: PALETTE.replace2, dash: true, points: pick(png, 'replace', 'random', 2).map((r) => [kib(r), metric(r)]) },
];
const xMax = Math.ceil(Math.max(...png.filter((r) => r.bits <= 2).map(kib)) / 10) * 10;
const xTicks = Array.from({ length: xMax / 10 + 1 }, (_, i) => i * 10).filter((v) => v <= xMax && (xMax <= 60 || v % 20 === 0));

writeFileSync(path.join(OUT_EXP, 'psnr-por-tamano.svg'), lineChart({
  title: 'Distorsión según el tamaño del mensaje',
  subtitle: 'PSNR respecto a la portadora (más alto = menos cambios) · matching y sustitución con k = 1 se superponen',
  xLabel: 'Mensaje oculto (KiB)', yLabel: 'PSNR (dB)', series: sizeSeries((r) => r.psnr),
  xDomain: [0, xMax], yDomain: [40, 80], xTicks, yTicks: [40, 50, 60, 70, 80], yFmt: (v) => fmt(v, 0),
}));
writeFileSync(path.join(OUT_EXP, 'rs-por-tamano.svg'), lineChart({
  title: 'Detección: tasa estimada por el análisis RS',
  subtitle: 'Fracción de muestras que RS cree modificadas (0 = parece limpia) · con la imagen casi llena, RS puede volverse inestable',
  xLabel: 'Mensaje oculto (KiB)', yLabel: 'Tasa RS estimada', series: sizeSeries((r) => r.rs_tasa),
  xDomain: [0, xMax], yDomain: [0, 1], xTicks, yTicks: [0, 0.2, 0.4, 0.6, 0.8, 1], yFmt: (v) => fmt(v, 1),
  threshold: { value: 0.1, label: 'umbral orientativo (10 %)' },
}));

const seq = analysis.sequential.chi_square_sweep;
// La zona con mensaje va desde el inicio de la primera ventana con p > 0,9 hasta el final de la ultima
const hot = seq.map((s, i) => [i, s.window_p > 0.9]).filter(([, h]) => h).map(([i]) => i);
const seqFrom = hot.length ? (hot[0] > 0 ? seq[hot[0] - 1].fraction : 0) : 0, seqTo = hot.length ? seq[hot.at(-1)].fraction : 0;
writeFileSync(path.join(OUT_EXP, 'barrido-chi2.svg'), lineChart({
  title: 'Barrido chi-cuadrado de una inserción secuencial',
  subtitle: '12 KB con sustitución LSB en orden secuencial · el acumulado no ve nada; las ventanas marcan dónde está el mensaje',
  xLabel: 'Porción de la imagen, de arriba abajo (%)', yLabel: 'p (probabilidad de inserción)',
  series: [
    { name: 'Acumulado (Westfeld-Pfitzmann)', color: PALETTE.cumulative, dash: true, points: seq.map((s) => [s.fraction * 100, s.p]) },
    { name: 'Por ventanas (cada 5 % por separado)', color: PALETTE.window, points: seq.map((s) => [s.fraction * 100, s.window_p]) },
    { name: 'Portadora sin mensaje, por ventanas', color: PALETTE.cover, points: analysis.cover.chi_square_sweep.map((s) => [s.fraction * 100, s.window_p]) },
  ],
  xDomain: [0, 100], yDomain: [0, 1], xTicks: [0, 20, 40, 60, 80, 100], yTicks: [0, 0.2, 0.4, 0.6, 0.8, 1], yFmt: (v) => fmt(v, 1),
  threshold: { value: 0.95, label: 'p = 0,95' }, bands: [[seqFrom * 100, seqTo * 100, 'zona con mensaje']],
}));

const cap = (inf, out, field, k = 1) => inf.capacities.find((c) => c.output === out && c.bits_per_slot === k)[field] / 1024;
writeFileSync(path.join(OUT_EXP, 'capacidad.svg'), barChart({
  title: 'Cuánto cabe en una imagen de 480 × 320',
  subtitle: 'Mensaje máximo sin comprimir · el modo adaptativo limita la carga a propósito para ser menos detectable',
  unit: 'KiB',
  groups: [
    { label: 'PNG (espacial)', bars: [
      { name: 'adaptativo', value: cap(info.png, 'PNG', 'adaptive_bytes'), color: PALETTE.adaptive },
      { name: 'replace, k = 1', value: cap(info.png, 'PNG', 'message_bytes', 1), color: PALETTE.replace },
      { name: 'replace, k = 2', value: cap(info.png, 'PNG', 'message_bytes', 2), color: PALETTE.replace2 },
      { name: 'replace, k = 4', value: cap(info.png, 'PNG', 'message_bytes', 4), color: '#9333ea' }] },
    { label: 'JPEG (DCT)', bars: [
      { name: 'adaptativo', value: cap(info.png, 'JPEG', 'adaptive_bytes'), color: PALETTE.adaptive },
      { name: 'replace', value: cap(info.png, 'JPEG', 'message_bytes'), color: PALETTE.replace }] },
    { label: 'GIF (paleta)', bars: [
      { name: 'adaptativo', value: cap(info.gif, 'GIF', 'adaptive_bytes'), color: PALETTE.adaptive },
      { name: 'replace', value: cap(info.gif, 'GIF', 'message_bytes'), color: PALETTE.replace }] },
  ],
}));

// 4) Figuras PNG
log('Figuras PNG...');
const coverUri = dataUri(COVER);
const tag = (cls, text) => `<span class="tag ${cls}">${text}</span>`;
const pct = (v) => `${fmt(v * 100, 1)} %`;
const caption = (name, a, extra) => {
  const rs = a.rs.estimated_rate;
  return `<b>${name}</b>${tag('', `${pct(a.quality.changed_ratio)} muestras`)}${tag('', `PSNR ${fmt(a.quality.psnr, 1)} dB`)}${tag(rs > 0.1 ? 'bad' : 'ok', `RS ${fmt(rs, 2)}`)}<br>${extra}`;
};
const masks = [
  ['adaptive', 'Adaptativo (STC, por defecto)', 'Los cambios se concentran en la hierba y las montañas, donde hay textura. El cielo casi no se toca.', [99, 102, 241]],
  ['match', 'LSB matching (±1)', 'Reparte los cambios por toda la imagen, sin mirar si la zona es lisa.', [6, 182, 212]],
  ['replace', 'Sustitución LSB', 'Mismo reparto que matching, pero iguala los pares de valores: RS lo detecta.', [249, 115, 22]],
  ['sequential', 'Sustitución en orden secuencial', 'Todo el mensaje en una franja: se ve a simple vista y el barrido chi-cuadrado lo localiza.', [239, 68, 68]],
];
await renderHtml(browser, `<!doctype html><meta charset="utf-8"><style>${FIGURE_CSS} .row{grid-template-columns:repeat(2,1fr)}</style>
<div class="card"><h1>¿Dónde cambia cada modo la imagen?</h1><p class="sub">El mismo mensaje de 12 KB oculto en samples/paisaje.png de cuatro formas. En color, los píxeles modificados; en gris, la imagen original.</p>
<div class="row">${masks.map(([k, t, d]) => `<figure><canvas id="${k}"></canvas><figcaption>${caption(t, analysis[k], d)}</figcaption></figure>`).join('')}</div></div>
<script>${FIGURE_JS}
(async () => { const cover = await load('${coverUri}');
${masks.map(([k, , , c]) => `changeMask(document.getElementById('${k}'), cover, await load('${dataUri(path.join(WORK, `${k}.png`))}'), [${c}]);`).join('\n')}
window.ready = true; })();</script>`, path.join(OUT_EXP, 'donde-cambia-cada-modo.png'), 1000);

await renderHtml(browser, `<!doctype html><meta charset="utf-8"><style>${FIGURE_CSS} .row{grid-template-columns:1fr 1fr 1fr} .zoom{grid-template-columns:1fr 1fr; margin-top:14px}</style>
<div class="card"><h1>A simple vista, la misma imagen</h1><p class="sub">12 KB ocultos con el modo adaptativo. Solo cambia el bit menos significativo de algunas muestras: como mucho ±1 sobre 255.</p>
<div class="row">
 <figure><img src="${coverUri}"><figcaption><b>Original</b>samples/paisaje.png</figcaption></figure>
 <figure><img id="st" src="${dataUri(path.join(WORK, 'adaptive.png'))}"><figcaption><b>Con el mensaje</b>PSNR ${fmt(analysis.adaptive.quality.psnr, 1)} dB · SSIM ${fmt(analysis.adaptive.quality.ssim, 4)}</figcaption></figure>
 <figure><canvas id="d"></canvas><figcaption><b>Diferencia ×64</b>Lo único que distingue a las dos: ruido en las zonas con textura</figcaption></figure>
</div>
<div class="row zoom">
 <figure><canvas id="z1"></canvas><figcaption><b>Original, ampliada ×20</b>un recorte de 24 × 24 píxeles de la ladera</figcaption></figure>
 <figure><canvas id="z2"></canvas><figcaption><b>Con el mensaje, ampliada ×20</b>el mismo recorte: las diferencias no se aprecian ni así</figcaption></figure>
</div></div>
<script>${FIGURE_JS}
(async () => { const a = await load('${coverUri}'), b = await load(document.getElementById('st').src);
diff64(document.getElementById('d'), a, b); zoom(document.getElementById('z1'), a, 300, 250, 24, 20); zoom(document.getElementById('z2'), b, 300, 250, 24, 20); window.ready = true; })();</script>`,
path.join(OUT_EXP, 'original-frente-a-resultado.png'), 1000);

// 5) Resumen numerico (lo citan las guias)
const jpg = readCsv(path.join(WORK, 'bench_jpg.csv')).filter((r) => !r.error);
const gif = readCsv(path.join(WORK, 'bench_gif.csv')).filter((r) => !r.error);
const summary = {
  generado: new Date().toISOString().slice(0, 10),
  version: icpu('version').trim(),
  mismo_mensaje_12kb: Object.fromEntries(Object.keys(analysis).map((k) => [k, {
    chi2_p: analysis[k].chi_square?.p, rs: analysis[k].rs?.estimated_rate,
    psnr: analysis[k].quality?.psnr ?? null, ssim: analysis[k].quality?.ssim ?? null, muestras_cambiadas: analysis[k].quality?.changed_ratio ?? 0,
  }])),
  capacidad_kib: { png_adaptativo: cap(info.png, 'PNG', 'adaptive_bytes'), png_replace_k1: cap(info.png, 'PNG', 'message_bytes'), jpeg_adaptativo: cap(info.png, 'JPEG', 'adaptive_bytes'), jpeg_replace: cap(info.png, 'JPEG', 'message_bytes'), gif_adaptativo: cap(info.gif, 'GIF', 'adaptive_bytes'), gif_replace: cap(info.gif, 'GIF', 'message_bytes') },
  bench: { png, jpg, gif },
};
writeFileSync(path.join(OUT_EXP, 'resultados.json'), `${JSON.stringify(summary, null, 2)}\n`);

// 6) Capturas de la GUI (opcional)
if (process.argv.includes('--capturas')) {
  log('Capturas de la GUI (tema claro)...');
  await captureGui(browser, 'light', 18871);
  log('Capturas de la GUI (tema oscuro)...');
  await captureGui(browser, 'dark', 18872);
}
log(`Listo: ${path.relative(ROOT, OUT_EXP)}${process.argv.includes('--capturas') ? ` y ${path.relative(ROOT, OUT_GUI)}` : ''}`);
process.exit(0);
