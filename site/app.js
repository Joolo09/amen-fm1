// SPDX-License-Identifier: GPL-3.0-only
// AMEN-FM1 installer: the AMEN firmware onto an M-VAVE FM-1 with Felucca's own WebMIDI updater (vendor/fm1ota.js,
// unchanged). The firmware's version, identity and digest come from firmware/amen.json (tools/build_amen.sh).
import { Updater } from "./vendor/fm1ota.js";
import { productOf, logicalImage, sha256hex } from "./vendor/fm1pkg.js";

const $ = (id) => document.getElementById(id);
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const logEl = $("log");
function log(...a) {
  logEl.textContent += `[${new Date().toLocaleTimeString()}] ${a.join(" ")}\n`;
  logEl.scrollTop = logEl.scrollHeight;
}
function say(el, text, kind = "") {
  const s = $(el);
  s.textContent = text;
  s.className = "status" + (kind ? " " + kind : "");
}
function bar(id, frac) {
  const b = $(id);
  if (!b.children.length) for (let i = 0; i < 24; i++) b.append(document.createElement("i"));
  const n = Math.round(Math.max(0, Math.min(1, frac)) * 24);
  [...b.children].forEach((c, i) => c.classList.toggle("on", i < n));
}
function chip(id, text, kind = "") {
  const c = $(id);
  c.textContent = text;
  c.className = "chip" + (kind ? " " + kind : "");
}

// the 16 steps of the header, lit one sixteenth after another at 170 BPM (AMEN's first bar: kicks and snares marked)
(() => {
  const steps = $("steps"), K = [0, 2, 10, 11], S = [4, 12];
  for (let i = 0; i < 16; i++) {
    const e = document.createElement("i");
    if (K.includes(i)) e.className = "k";
    if (S.includes(i)) e.className = "s";
    e.style.animationDelay = `${(i * 1.412) / 16}s`;
    steps.append(e);
  }
})();
bar("fwbar", 0);

let midi = null, busy = false, image = null, FW = null;
// ?fw=os: the AMEN OS test build (firmware/amen-os.json), opt-in; the page installs AMEN (Felucca-based) otherwise
const OS = new URLSearchParams(location.search).get("fw") === "os";
const NAME = OS ? "AMEN OS" : "AMEN";
if (OS) {
  $("ostest").hidden = false;
  $("fwname").textContent = $("fwname2").textContent = NAME;
}
const MIDI_TEXT = {
  notfound: "FM-1 не найден. Проверьте кабель (с передачей данных), подключите напрямую, закройте другие MIDI-программы.",
  model: "Подключено не FM-1.",
  stopped: "Устройство остановилось. Переподключите кабель и нажмите снова: если FM-1 уже в режиме обновления, запись продолжится.",
  noloader: "Режим обновления не появился. Переподключите кабель и нажмите снова: FM-1 ждёт в режиме обновления.",
  lost: "Связь по USB пропала. Переподключите кабель и нажмите снова: запись продолжится.",
  noreturn: "FM-1 не вернулся после записи. Выключите и включите его.",
  mismatch: "Записано, но FM-1 сообщает другую версию: ",
  badreq: "Неверный запрос от устройства.",
};

function setBusy(on) {
  busy = on;
  $("connect").disabled = on;
  $("fwgo").disabled = on || !image || !$("fwok").checked;
}
window.addEventListener("beforeunload", (e) => { if (busy) { e.preventDefault(); e.returnValue = ""; } });

// ------------------------------------------------------------------------------------------ the package ---
async function fetchBytes(url) {
  const r = await fetch(url, { cache: "no-cache" });
  if (!r.ok) throw new Error(`${url}: HTTP ${r.status}`);
  return new Uint8Array(await r.arrayBuffer());
}
(async () => {
  try {
    const json = OS ? "firmware/amen-os.json" : "firmware/amen.json";
    const r = await fetch(json, { cache: "no-cache" });
    if (!r.ok) throw new Error(`${json}: HTTP ${r.status}`);
    FW = await r.json();
    $("fwver").textContent = FW.version;
    const pkg = await fetchBytes(FW.file);
    if (productOf(pkg) !== FW.product) throw new Error(`идентификатор пакета ${productOf(pkg)}, ожидался ${FW.product}`);
    if ((await sha256hex(pkg)) !== FW.sha256) throw new Error("контрольная сумма прошивки не совпадает");
    image = logicalImage(pkg);
    log(`прошивка ${NAME} ${FW.version} загружена (${pkg.length} Б, ${FW.product})`);
  } catch (e) {
    say("st1", "Не удалось загрузить прошивку: " + e.message, "err");
    log(String(e.stack || e));
  }
  setBusy(false);
})();

// ---------------------------------------------------------------------------------------------- connect ---
async function access() {
  if (midi) return midi;
  if (!navigator.requestMIDIAccess) throw new Error("Этот браузер не умеет WebMIDI. Откройте страницу в Chrome или Edge на компьютере.");
  try {
    midi = await navigator.requestMIDIAccess({ sysex: true });
  } catch (_) {
    throw new Error("MIDI (SysEx) не разрешён. Разрешите MIDI в настройках сайта (значок слева от адреса) и нажмите снова.");
  }
  return midi;
}

function describe(id) {
  if (!id) return { text: "НЕТ УСТРОЙСТВА", kind: "err" };
  if (/^ota-/i.test(id.model)) return { text: "РЕЖИМ ОБНОВЛЕНИЯ", kind: "warn", ota: true };
  if (FW && id.text === FW.product) return { text: `${NAME} ${FW.version}`, kind: "ok", current: true };
  if (id.version >= 700 && id.version <= 799) return { text: `AMEN OS (${id.text})`, kind: "warn" };
  if (id.version >= 900 && id.version <= 999) return { text: `FELUCCA/AMEN (${id.text})`, kind: "warn" };
  return { text: `ЗАВОДСКАЯ (${id.text})`, kind: "warn" };
}

$("connect").addEventListener("click", async () => {
  setBusy(true);
  say("st0", "Ищу FM-1…", "busy");
  try {
    await access();
    const up = new Updater(midi, (...a) => log(...a));
    const dev = await up.find();
    if (dev) dev.link.close();
    const id = dev && dev.id, d = describe(id);
    chip("devchip", d.text, d.kind);
    if (!id) say("st0", MIDI_TEXT.notfound, "err");
    else if (d.ota) say("st0", `FM-1 в режиме обновления: запись прервалась. Нажмите «ПРОШИТЬ ${NAME}», чтобы продолжить.`, "busy");
    else if (d.current) say("st0", `На FM-1 уже стоит ${NAME} ${FW.version}. Прошить заново можно в шаге 01.`, "ok");
    else say("st0", `Найден FM-1 (${id.text}). Дальше шаг 01.`, "ok");
    if (id && !d.current) $("s1").classList.add("hot");
    log(`устройство: ${id ? id.text : "нет"}`);
  } catch (e) {
    say("st0", e.message, "err");
    log(String(e.stack || e));
  }
  setBusy(false);
});
$("fwok").addEventListener("change", () => setBusy(busy));

// --------------------------------------------------------------------------------------------- firmware ---
$("fwgo").addEventListener("click", async () => {
  if (busy || !image) return;
  setBusy(true);
  bar("fwbar", 0);
  try {
    await access();
    const up = new Updater(midi, (...a) => log(...a));
    const total = 74 + Math.ceil(image.length / 512);   // step 1's reads, then the whole image in 512-byte reads
    const step = (k, a) => {
      log(`прошивка: ${k} ${a ?? ""}`);
      if (k === "start") say("st1", `Начинаю (${a})…`, "busy");
      if (k === "verify") { say("st1", "Проверка…", "busy"); bar("fwbar", a / total); }
      if (k === "loader") say("st1", "Перехожу в режим записи…", "busy");
      if (k === "write") { say("st1", "Запись… не отключайте кабель", "busy"); bar("fwbar", (74 + a) / total); }
      if (k === "reboot") say("st1", "Перезагрузка…", "busy");
      if (k === "done") bar("fwbar", 1);
    };
    const ota = await up.find((id) => /^ota-/i.test(id.model));
    if (ota) {
      ota.link.close();
      log("найден режим обновления: продолжаю запись");
      if (!(await up.resume(image, step, FW.product))) throw Object.assign(new Error("stopped"), { code: "stopped" });
    } else {
      await up.install(image, FW.product, step);
    }
    say("st1", `${NAME} ${FW.version} установлена! FM-1 перезапустился — жмите PLAY.`, "ok");
    say("st0", `На FM-1 стоит ${NAME} ${FW.version}.`, "ok");
    chip("devchip", `${NAME} ${FW.version}`, "ok");
    $("s1").classList.remove("hot");
    $("s3").classList.add("hot");
  } catch (e) {
    const text = e && e.code && MIDI_TEXT[e.code] ? MIDI_TEXT[e.code] + (e.detail || "") : "Ошибка: " + (e && e.message);
    say("st1", text, "err");
    log(String(e && e.stack || e));
  }
  setBusy(false);
});

// ------------------------------------------------------------------------------------------------- demo ---
(() => {
  const a = $("demo"), btn = $("play"), pb = $("pbar"), tm = $("ptime");
  const fmt = (t) => `${Math.floor(t / 60)}:${String(Math.floor(t % 60)).padStart(2, "0")}`;
  bar("pbar", 0);
  btn.addEventListener("click", () => { if (a.paused) a.play(); else a.pause(); });
  a.addEventListener("play", () => { btn.textContent = "❚❚"; btn.setAttribute("aria-label", "Пауза"); });
  a.addEventListener("pause", () => { btn.textContent = "▶"; btn.setAttribute("aria-label", "Играть демо"); });
  a.addEventListener("timeupdate", () => {
    const f = a.duration ? a.currentTime / a.duration : 0;
    bar("pbar", f);
    pb.setAttribute("aria-valuenow", String(Math.round(f * 100)));
    tm.textContent = fmt(a.currentTime);
  });
  const seek = (f) => { if (a.duration) a.currentTime = Math.max(0, Math.min(1, f)) * a.duration; };
  pb.addEventListener("click", (e) => { const r = pb.getBoundingClientRect(); seek((e.clientX - r.left) / r.width); });
  pb.addEventListener("keydown", (e) => {
    if (!a.duration) return;
    if (e.key === "ArrowRight") seek(a.currentTime / a.duration + 0.05);
    if (e.key === "ArrowLeft") seek(a.currentTime / a.duration - 0.05);
  });
})();

if (!navigator.requestMIDIAccess) {
  say("st0", "Этот браузер не умеет WebMIDI. Откройте страницу в Chrome или Edge на компьютере.", "err");
}
