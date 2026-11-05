// make_manual_pdf.mjs - the manual's PDFs and screenshots, from docs/ served locally, with Playwright.
//
//   node tools/make_manual_pdf.mjs          docs/manual/clatter12-manual.pdf and clatter12-crib-sheets.pdf
//   node tools/make_manual_pdf.mjs --shots  docs/manual/img/*.png: each page of the running panel
//
// The screenshots are the panel itself (docs/assets/c12panel.wasm, driven through window.C12PAGE),
// so they show what the firmware draws. Run tools/make_manual.py first (`make manual` does both).
// Copyright (C) 2026 The CLATTER contributors. SPDX-License-Identifier: GPL-3.0-only
import { createServer } from 'node:http';
import { readFile, stat } from 'node:fs/promises';
import { createRequire } from 'node:module';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const require = createRequire(import.meta.url);
const { chromium } = require('playwright');
const ROOT = path.join(path.dirname(fileURLToPath(import.meta.url)), '..', 'docs');
const TYPES = { '.html': 'text/html', '.css': 'text/css', '.js': 'text/javascript', '.mjs': 'text/javascript', '.wasm': 'application/wasm',
  '.woff2': 'font/woff2', '.png': 'image/png', '.svg': 'image/svg+xml', '.json': 'application/json', '.txt': 'text/plain', '.pdf': 'application/pdf' };

const server = createServer(async (req, res) => {
  let p = decodeURIComponent(new URL(req.url, 'http://x').pathname);
  if (p.endsWith('/')) p += 'index.html';
  const f = path.join(ROOT, path.normalize(p));
  if (!f.startsWith(ROOT)) { res.writeHead(403).end(); return; }
  try {
    await stat(f);
    res.writeHead(200, { 'content-type': TYPES[path.extname(f)] || 'application/octet-stream' }).end(await readFile(f));
  } catch { res.writeHead(404).end(); }
});
await new Promise((r) => server.listen(0, '127.0.0.1', r));
const BASE = `http://127.0.0.1:${server.address().port}/`;
const browser = await chromium.launch();

async function shots() {
  const OUT = path.join(ROOT, 'manual', 'img');
  const pg = await browser.newPage({ viewport: { width: 1500, height: 1400 }, deviceScaleFactor: 1.5 });
  await pg.goto(BASE);
  await pg.waitForFunction(() => window.C12PAGE && window.C12PAGE.P.state === 'off');
  const T = (x, y, p) => pg.evaluate(([x, y, p]) => window.C12PAGE.panel.touch(x, y, p), [x, y, p]);
  const tap = async (x, y) => { await T(x, y, 150); await pg.waitForTimeout(60); await T(x, y, 0); };
  const page = (n) => pg.evaluate((n) => window.C12PAGE.panel.setPage(n), n);
  const shot = async (name) => {
    await pg.waitForTimeout(500);
    const el = pg.locator('#plinky');
    await el.scrollIntoViewIfNeeded();
    const r = await el.boundingBox();
    await pg.screenshot({ path: path.join(OUT, name + '.png'), clip: { x: r.x - 160, y: r.y - 6, width: r.width + 166, height: r.height + 12 } });
    console.log('img/' + name + '.png');
  };
  // SEQ, with a snare step held, given a 3x ratchet, and left selected
  await page(0); await pg.waitForTimeout(200);
  const leds = await pg.evaluate(() => Array.from(window.C12PAGE.P.leds));
  let sx = -1, sy = -1;
  for (let y = 3; y < 7 && sx < 0; y++) for (let x = 0; x < 16; x++) if (leds[y * 16 + x]) { sx = x; sy = y; break; }
  if (sx >= 0) { await T(sx, sy, 160); await pg.waitForTimeout(450); await tap(10, 8); await T(sx, sy, 0); }
  await shot('seq');
  await page(1); await shot('sound');
  await page(2); await shot('gen');
  await page(3); await shot('mix');
  await page(4); await tap(8, 0); await shot('fx-space');
  await tap(9, 0); await shot('fx-colour'); await tap(8, 0);
  await page(5); await shot('perform');
  await page(6); await shot('repeat');
  await page(7); await shot('save');
  await page(0); await pg.waitForTimeout(200); await T(8, 15, 150); await pg.waitForTimeout(700); await shot('tempo'); await T(8, 15, 0);
}

async function pdfs() {
  const pg = await browser.newPage({ viewport: { width: 1200, height: 1600 } });
  await pg.emulateMedia({ media: 'print' });
  for (const [file, cribs] of [['clatter12-manual.pdf', false], ['clatter12-crib-sheets.pdf', true]]) {
    await pg.goto(BASE + 'manual/', { waitUntil: 'networkidle' });
    await pg.evaluate(async (cribs) => {
      document.body.classList.toggle('cribs-only', cribs);
      const all = document.querySelectorAll('.crib'); /* no blank page after the last sheet */
      if (all.length) all[all.length - 1].style.breakAfter = cribs ? 'auto' : '';
      for (const img of document.images) img.loading = 'eager';
      await document.fonts.ready;
      await Promise.all(Array.from(document.images, (i) => i.complete ? 0 : new Promise((r) => { i.onload = i.onerror = r; })));
    }, cribs);
    await pg.pdf({ path: path.join(ROOT, 'manual', file), format: 'A4', printBackground: true, preferCSSPageSize: true,
      displayHeaderFooter: false, tagged: true, outline: true });
    console.log('manual/' + file);
  }
}

try {
  if (process.argv.includes('--shots')) await shots(); else await pdfs();
} finally {
  await browser.close();
  server.close();
}
