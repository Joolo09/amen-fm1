// SPDX-License-Identifier: GPL-3.0-only
// The installer site against a fake FM-1 (mock_midi.js) in Chromium: stock V15 -> Felucca -> the AMEN plugins, a
// resumed write, a Felucca without plugins, no device, a phone's width.
//   node tests/site/e2e.js build/site [SCREENSHOT_DIR]      (needs playwright; exit 1 on a failure)
const { chromium } = require("playwright");
const fs = require("fs"), path = require("path"), http = require("http");
const root = path.resolve(process.argv[2] || "build/site"), shots = process.argv[3];
const mime = { ".html": "text/html", ".js": "text/javascript", ".mp3": "audio/mpeg" };
let fails = 0;
const check = (ok, what) => { console.log(`${ok ? "ok  " : "FAIL"} ${what}`); if (!ok) fails++; };

const srv = http.createServer((q, r) => {
  const f = path.join(root, decodeURIComponent(q.url.split("?")[0]).replace(/\/$/, "/index.html"));
  if (!f.startsWith(root) || !fs.existsSync(f)) { r.writeHead(404); return r.end(); }
  r.writeHead(200, { "content-type": mime[path.extname(f)] || "application/octet-stream" });
  fs.createReadStream(f).pipe(r);
});
const URL = "http://localhost:8765/";

// the image the device reads (the .fwsc without its 20 marker bytes) and the mock's weighted sum of it
const pkg = fs.readFileSync(path.join(root, "firmware/felucca-1.9.0.fwsc"));
const img = Buffer.alloc(pkg.length - 20);
for (let i = 0; i < 20; i++) pkg.copy(img, i * 0x2F, i * 0x30, i * 0x30 + 0x2F);
pkg.copy(img, 20 * 0x2F, 20 * 0x30);
let want = 0;
for (let i = 0; i < img.length; i++) want = (want + img[i] * (i % 251 + 1)) % 1000000007;
const FW_DONE = /установлена|Ошибка|остановил|вернулся|появился|пропала|Подключено не/;

async function open(b, setup, viewport = { width: 900, height: 1400 }) {
  const page = await b.newPage({ viewport });
  page.errors = [];
  page.on("pageerror", (e) => page.errors.push(String(e)));
  page.on("console", (m) => m.type() === "error" && !/favicon|fonts\.g/.test(m.text()) && page.errors.push(m.text()));
  await page.addInitScript({ path: path.join(__dirname, "mock_midi.js") });
  await page.addInitScript(`window.addEventListener("DOMContentLoaded", () => { Object.assign(window.__fm1, { imageSize: ${img.length}, target: "FM-1_919" }, ${setup}); window.confirm = () => true; })`);
  await page.goto(URL);
  await page.waitForFunction(() => document.getElementById("log").textContent.includes("REESE"), null, { timeout: 15000 });
  return page;
}
const text = (p, id) => p.textContent(id);

(async () => {
  await new Promise((r) => srv.listen(8765, r));
  const b = await chromium.launch();

  // stock V15 -> Felucca 1.9.0 -> both plugins
  let p = await open(b, "{}");
  await p.click("#connect");
  await p.waitForFunction(() => /ЗАВОДСКАЯ/.test(document.getElementById("devchip").textContent));
  check(await p.isDisabled("#fwgo"), "the firmware button waits for the consent box");
  await p.check("#fwok");
  await p.click("#fwgo");
  await p.waitForFunction((re) => new RegExp(re).test(document.getElementById("st1").textContent), FW_DONE.source, { timeout: 180000 });
  const st = await p.evaluate(() => ({ fw: window.__fm1.fw, bytes: window.__fm1.otaBytes, sum: window.__fm1.chunkSum }));
  check(/установлена/.test(await text(p, "#st1")), "firmware: " + await text(p, "#st1"));
  check(st.fw === "FM-1_919" && st.bytes === img.length && st.sum === want, `the whole image written (${st.bytes} B, ${st.fw})`);
  await p.waitForTimeout(2500);
  await p.click("#plgo");
  await p.waitForFunction(() => /Готово|Ошибка|не отвечает|но /.test(document.getElementById("st2").textContent), null, { timeout: 60000 });
  check(/Готово/.test(await text(p, "#st2")), "plugins: " + await text(p, "#st2"));
  const same = await p.evaluate(async () => {
    const res = [];
    for (const [k, f] of [[0, "fep/jungle.s0.fep"], [1, "fep/reese.s1.fep"]]) {
      const w = new Uint8Array(await (await fetch(f)).arrayBuffer()), got = window.__fm1.blobs[k];
      res.push(got.length === w.length && w.every((x, i) => got[i] === x));
    }
    return res;
  });
  check(same[0] && same[1], "both .fep written byte for byte into their slots");
  check(/ИГРАЕТ/.test(await text(p, "#slot0")) && /ИГРАЕТ/.test(await text(p, "#slot1")), "both slots play after the restart");
  check(!p.errors.length, "no page errors " + p.errors.join("; "));
  if (shots) await p.screenshot({ path: path.join(shots, "installer.png"), fullPage: true });
  await p.close();

  // an interrupted write: the FM-1 waits in its update loader
  p = await open(b, `{ mode: "ota", fw: "FM-1_919" }`);
  await p.click("#connect");
  await p.waitForFunction(() => /ОБНОВЛЕНИЯ/.test(document.getElementById("devchip").textContent));
  await p.check("#fwok");
  await p.click("#fwgo");
  await p.waitForFunction((re) => new RegExp(re).test(document.getElementById("st1").textContent), FW_DONE.source, { timeout: 180000 });
  check(/установлена/.test(await text(p, "#st1")), "resume: " + await text(p, "#st1"));
  await p.close();

  // a Felucca without plugins
  p = await open(b, `{ fw: "FM-1_918", plugins: false }`);
  await p.click("#connect");
  await p.waitForFunction(() => /FELUCCA/.test(document.getElementById("devchip").textContent));
  await p.click("#plgo");
  await p.waitForFunction(() => /не отвечает|Готово/.test(document.getElementById("st2").textContent), null, { timeout: 20000 });
  check(/не отвечает/.test(await text(p, "#st2")), "old Felucca: asked to update first");
  await p.close();

  // no FM-1
  p = await open(b, "{}");
  await p.evaluate(() => { navigator.requestMIDIAccess = async () => ({ inputs: new Map(), outputs: new Map() }); });
  await p.click("#connect");
  await p.waitForFunction(() => /не найден/.test(document.getElementById("st0").textContent));
  check(true, "no device: " + await text(p, "#st0"));
  await p.close();

  // a phone
  p = await open(b, "{}", { width: 375, height: 800 });
  check(!(await p.evaluate(() => document.documentElement.scrollWidth > window.innerWidth)), "375 px: no horizontal scroll");
  await p.close();

  await b.close();
  srv.close();
  console.log(fails ? `${fails} FAILED` : "all passed");
  process.exit(fails ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(1); });
