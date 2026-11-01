// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Yoann
// ZINZIN in the browser, checked in a headless Chromium at a phone's size: the page switches on, ZOU plays, the
// screen moves, there is sound, nothing throws. Serves DIR itself (the emulator: fm1-base's tools/build.py --web, or
// make web's build/web).
//
//   node tools/web_smoke.js DIR [shots_dir]      (needs playwright: npm i playwright && npx playwright install chromium)
const http = require("http"), fs = require("fs"), path = require("path");
const { chromium, devices } = require("playwright");
const dir = path.resolve(process.argv[2] || "build/web"), shots = process.argv[3];
const TYPES = { ".html": "text/html", ".js": "text/javascript", ".wasm": "application/wasm", ".json": "application/json" };

const server = http.createServer((req, res) => {
  const f = path.join(dir, decodeURIComponent(req.url.split("?")[0]).replace(/\/$/, "/index.html"));
  if (!f.startsWith(dir) || !fs.existsSync(f)) { res.writeHead(404); return res.end(); }
  res.writeHead(200, { "content-type": TYPES[path.extname(f)] || "application/octet-stream" });
  fs.createReadStream(f).pipe(res);
});

(async () => {
  await new Promise((r) => server.listen(0, r));
  const url = `http://localhost:${server.address().port}/`;
  const browser = await chromium.launch({ args: ["--autoplay-policy=no-user-gesture-required"] });
  const ctx = await browser.newContext({ ...devices["iPhone 13"], viewport: { width: 390, height: 664 } });
  const page = await ctx.newPage();
  const errors = [];
  page.on("pageerror", (e) => errors.push(e.message));
  page.on("console", (m) => { if (m.type() === "error") errors.push(m.text()); });
  const r = {};
  try {
    await page.goto(url);
    await page.tap("#go");
    await page.waitForFunction(() => document.getElementById("start").hidden || document.getElementById("err").textContent, null, { timeout: 20000 });
    r.error = await page.evaluate(() => document.getElementById("err").textContent);
    r.audio = await page.evaluate(() => ac && `${ac.state} ${ac.sampleRate} Hz`);
    await page.evaluate(() => {
      window.an = ac.createAnalyser(); an.fftSize = 2048; gain.connect(an);
      window.rms = () => { const a = new Float32Array(2048); an.getFloatTimeDomainData(a); let s = 0; for (const v of a) s += v * v; return Math.sqrt(s / a.length); };
    });
    await page.tap("text=ZOU");
    await page.waitForTimeout(3000);
    let peak = 0;
    for (let i = 0; i < 10; i++) { peak = Math.max(peak, await page.evaluate(() => rms())); await page.waitForTimeout(100); }
    r.rms = +peak.toFixed(4);
    const shot = () => page.evaluate(() => document.getElementById("screen").toDataURL());
    const a = await shot();
    await page.waitForTimeout(400);
    r.screenMoves = a !== (await shot());
    r.state = await page.evaluate(() => st && { track: st[23], playing: st[28], mood: st[31], bpm: Math.round(st[32]) });
    r.load = await page.evaluate(() => document.getElementById("load").textContent);
    if (shots) await page.screenshot({ path: path.join(shots, "smoke.png") });
  } catch (e) {
    r.error = (r.error ? r.error + "; " : "") + e.message;
  }
  r.errors = errors;
  await browser.close();
  server.close();
  const ok = !r.error && !errors.length && r.rms > 0.005 && r.screenMoves && r.state && r.state.playing === 1;
  console.log(JSON.stringify(r));
  console.log(ok ? "OK" : "FAIL");
  process.exit(ok ? 0 : 1);
})();
