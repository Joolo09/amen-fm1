// SPDX-License-Identifier: GPL-3.0-only
// The installer site against a fake FM-1 (mock_midi.js) in Chromium: stock V15 -> the AMEN firmware, a resumed
// write, a Felucca already on it, no device, a phone's width.
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
function firmware(json) {
  const meta = JSON.parse(fs.readFileSync(path.join(root, json), "utf8"));
  const pkg = fs.readFileSync(path.join(root, meta.file));
  const img = Buffer.alloc(pkg.length - 20);
  for (let i = 0; i < 20; i++) pkg.copy(img, i * 0x2F, i * 0x30, i * 0x30 + 0x2F);
  pkg.copy(img, 20 * 0x2F, 20 * 0x30);
  let want = 0;
  for (let i = 0; i < img.length; i++) want = (want + img[i] * (i % 251 + 1)) % 1000000007;
  return { meta, img, want };
}
const { meta, img, want } = firmware("firmware/amen.json");
const FW_DONE = /установлена|Ошибка|остановил|вернулся|появился|пропала|Подключено не/;

async function open(b, setup, viewport = { width: 900, height: 1400 }, fw = { meta, img }, query = "") {
  const page = await b.newPage({ viewport });
  page.errors = [];
  page.on("pageerror", (e) => page.errors.push(String(e)));
  page.on("console", (m) => m.type() === "error" && !/favicon|fonts\.g/.test(m.text()) && page.errors.push(m.text()));
  await page.addInitScript({ path: path.join(__dirname, "mock_midi.js") });
  await page.addInitScript(`window.addEventListener("DOMContentLoaded", () => { Object.assign(window.__fm1, { imageSize: ${fw.img.length}, target: "${fw.meta.product}" }, ${setup}); window.confirm = () => true; })`);
  await page.goto(URL + query);
  await page.waitForFunction(() => document.getElementById("log").textContent.includes("загружена"), null, { timeout: 15000 });
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
  check(st.fw === meta.product && st.bytes === img.length && st.sum === want, `the whole image written (${st.bytes} B, ${st.fw})`);
  check(/AMEN/.test(await text(p, "#devchip")), "the device shows AMEN " + await text(p, "#devchip"));
  check(!p.errors.length, "no page errors " + p.errors.join("; "));
  if (shots) await p.screenshot({ path: path.join(shots, "installer.png"), fullPage: true });
  await p.close();

  // an interrupted write: the FM-1 waits in its update loader
  p = await open(b, `{ mode: "ota", fw: "${meta.product}" }`);
  await p.click("#connect");
  await p.waitForFunction(() => /ОБНОВЛЕНИЯ/.test(document.getElementById("devchip").textContent));
  await p.check("#fwok");
  await p.click("#fwgo");
  await p.waitForFunction((re) => new RegExp(re).test(document.getElementById("st1").textContent), FW_DONE.source, { timeout: 180000 });
  check(/установлена/.test(await text(p, "#st1")), "resume: " + await text(p, "#st1"));
  await p.close();

  // Felucca on the FM-1: offered the AMEN firmware
  p = await open(b, `{ fw: "${meta.product === "FM-1_918" ? "FM-1_917" : "FM-1_918"}" }`);
  await p.click("#connect");
  await p.waitForFunction(() => /FELUCCA/.test(document.getElementById("devchip").textContent));
  check(/шаг 01/.test(await text(p, "#st0")), "Felucca found: " + await text(p, "#st0"));
  await p.close();

  // AMEN already on it
  p = await open(b, `{ fw: "${meta.product}" }`);
  await p.click("#connect");
  await p.waitForFunction(() => /AMEN/.test(document.getElementById("devchip").textContent));
  check(/уже стоит/.test(await text(p, "#st0")), "AMEN found: " + await text(p, "#st0"));
  await p.close();

  // another firmware, its own name and Windows' port names: found by probing every port, installed after the question
  p = await open(b, `{ fw: "X0X_12", inName: "MIDIIN2 (Groovebox)", outName: "MIDIOUT2 (Groovebox)" }`);
  await p.click("#connect");
  await p.waitForFunction(() => /ДРУГАЯ/.test(document.getElementById("devchip").textContent));
  check(true, "another firmware: " + await text(p, "#devchip"));
  check(/Keystation/.test(await text(p, "#ports")) && /X0X_12/.test(await text(p, "#ports")), "the probed ports listed");
  check((await p.$$eval("#port option", (o) => o.map((x) => x.value))).includes("input-1"), "the port list has the ids");
  await p.check("#fwok");
  await p.click("#fwgo");
  await p.waitForFunction((re) => new RegExp(re).test(document.getElementById("st1").textContent), FW_DONE.source, { timeout: 180000 });
  const sx = await p.evaluate(() => ({ fw: window.__fm1.fw, bytes: window.__fm1.otaBytes }));
  check(/установлена/.test(await text(p, "#st1")) && sx.fw === meta.product && sx.bytes === img.length, "over another firmware: " + await text(p, "#st1"));
  check(!p.errors.length, "no page errors " + p.errors.join("; "));
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

  // ?fw=os: the AMEN OS test build, opt-in (the page without it never offers it)
  if (fs.existsSync(path.join(root, "firmware/amen-os.json"))) {
    const os = firmware("firmware/amen-os.json");
    p = await open(b, "{}", undefined, os, "?fw=os");
    check(await p.isVisible("#ostest"), "?fw=os: the test build's warning shows");
    await p.click("#connect");
    await p.waitForFunction(() => /ЗАВОДСКАЯ/.test(document.getElementById("devchip").textContent));
    await p.check("#fwok");
    await p.click("#fwgo");
    await p.waitForFunction((re) => new RegExp(re).test(document.getElementById("st1").textContent), FW_DONE.source, { timeout: 180000 });
    const so = await p.evaluate(() => ({ fw: window.__fm1.fw, bytes: window.__fm1.otaBytes, sum: window.__fm1.chunkSum }));
    check(/AMEN OS .* установлена/.test(await text(p, "#st1")), "AMEN OS: " + await text(p, "#st1"));
    check(so.fw === os.meta.product && so.bytes === os.img.length && so.sum === os.want, `AMEN OS: the whole image written (${so.bytes} B, ${so.fw})`);
    check(!p.errors.length, "no page errors " + p.errors.join("; "));
    await p.close();
    p = await open(b, "{}");
    check(!(await p.isVisible("#ostest")), "without ?fw=os: no AMEN OS");
    await p.close();
  }

  await b.close();
  srv.close();
  console.log(fails ? `${fails} FAILED` : "all passed");
  process.exit(fails ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(1); });
