// SPDX-License-Identifier: GPL-3.0-only
// AMEN-FM1 installer: Felucca's firmware (its own WebMIDI updater, vendor/fm1ota.js) and the AMEN plugins
// (plugins.js) onto an M-VAVE FM-1.
import { Updater } from "./vendor/fm1ota.js";
import { productOf, logicalImage, sha256hex } from "./vendor/fm1pkg.js";
import * as P from "./plugins.js";

// the firmware this site installs (CI puts it in firmware/; its digest pinned here)
const FW = {
  version: "1.9.0",
  product: "FM-1_919",
  file: "firmware/felucca-1.9.0.fwsc",
  sha256: "08c1408019d01a3edc6e2f37bb61a24d3028dcfa5ed9570d6228a1eb6e75e0a4",
};
const PLUGINS = [
  { file: "fep/jungle.s0.fep", name: "JUNGLE" },
  { file: "fep/reese.s1.fep", name: "REESE" },
];

const $ = (id) => document.getElementById(id);
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const logEl = $("log");
function log(...a) {
  const line = `[${new Date().toLocaleTimeString()}] ${a.join(" ")}`;
  logEl.textContent += line + "\n";
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

// the 16 steps of the header, lit one sixteenth after another (AMEN's first bar: kicks and snares marked)
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
bar("plbar", 0);

let midi = null, busy = false, image = null, blobs = null;
const MIDI_TEXT = {
  notfound: "FM-1 не найден. Проверьте кабель (с передачей данных), подключите напрямую, закройте другие MIDI-программы.",
  model: "Подключено не FM-1.",
  stopped: "Устройство остановилось. Ничего не записано: переподключите и нажмите снова.",
  noloader: "Режим обновления не появился. Переподключите кабель и нажмите снова: FM-1 ждёт в режиме обновления.",
  lost: "Связь по USB пропала. Переподключите кабель и нажмите снова: запись продолжится.",
  noreturn: "FM-1 не вернулся после записи. Выключите и включите его.",
  mismatch: "Записано, но FM-1 сообщает другую версию: ",
  badreq: "Неверный запрос от устройства.",
};

function setBusy(on) {
  busy = on;
  $("connect").disabled = on;
  $("fwgo").disabled = on || !midi || !image || !$("fwok").checked;
  for (const id of ["plgo", "plinfo", "er0", "er1"]) $(id).disabled = on || !midi;
  $("fepgo").disabled = on || !midi || !$("fepfile").files.length;
  if (!on && !blobs) $("plgo").disabled = true;
}
window.addEventListener("beforeunload", (e) => { if (busy) { e.preventDefault(); e.returnValue = ""; } });

// ------------------------------------------------------------------------------------------- files ---
async function fetchBytes(url) {
  const r = await fetch(url, { cache: "no-cache" });
  if (!r.ok) throw new Error(`${url}: HTTP ${r.status}`);
  return new Uint8Array(await r.arrayBuffer());
}
(async () => {
  try {
    const pkg = await fetchBytes(FW.file);
    if (productOf(pkg) !== FW.product) throw new Error(`идентификатор пакета ${productOf(pkg)}, ожидался ${FW.product}`);
    const digest = await sha256hex(pkg);
    if (digest !== FW.sha256) throw new Error("контрольная сумма прошивки не совпадает");
    image = logicalImage(pkg);
    $("fwver").textContent = FW.version;
    log(`прошивка Felucca ${FW.version} загружена (${pkg.length} Б, ${FW.product})`);
  } catch (e) {
    say("st1", "Не удалось загрузить прошивку: " + e.message, "err");
    log(String(e.stack || e));
  }
  try {
    blobs = [];
    for (const p of PLUGINS) {
      const b = await fetchBytes(p.file);
      const h = P.parseFep(b);
      blobs.push({ ...p, bytes: b, h });
      log(`${h.name} v${h.version}: слот ${h.slot}, ${b.length} Б`);
    }
  } catch (e) {
    blobs = null;
    say("st2", "Не удалось загрузить плагины: " + e.message, "err");
    log(String(e.stack || e));
  }
  setBusy(false);
})();

// ----------------------------------------------------------------------------------------- connect ---
async function access() {
  if (midi) return midi;
  if (!navigator.requestMIDIAccess) throw new Error("Этот браузер не умеет WebMIDI. Откройте страницу в Chrome или Edge на компьютере.");
  try {
    midi = await navigator.requestMIDIAccess({ sysex: true });
  } catch (_) {
    throw new Error("MIDI (SysEx) не разрешён. Разрешите MIDI в настройках сайта (значок слева от адреса) и нажмите снова.");
  }
  midi.onstatechange = () => {};
  return midi;
}

async function identify() {
  const up = new Updater(midi, (...a) => log(...a));
  const dev = await up.find();
  if (!dev) return null;
  dev.link.close();
  return dev.id;
}

function describe(id) {
  if (!id) return { text: "НЕТ УСТРОЙСТВА", kind: "err", felucca: false };
  if (/^ota-/i.test(id.model)) return { text: "РЕЖИМ ОБНОВЛЕНИЯ", kind: "warn", felucca: false, ota: true };
  const v = id.version;                                   // Felucca packages are FM-1_9xx (stock V15: FM-1_015)
  if (v >= 900 && v <= 999) return { text: `FELUCCA (${id.text})`, kind: "ok", felucca: true };
  return { text: `ЗАВОДСКАЯ (${id.text})`, kind: "warn", felucca: false };
}

async function connect() {
  setBusy(true);
  say("st0", "Ищу FM-1…", "busy");
  try {
    await access();
    const id = await identify();
    const d = describe(id);
    chip("devchip", d.text, d.kind);
    if (!id) {
      say("st0", MIDI_TEXT.notfound, "err");
    } else if (d.ota) {
      say("st0", "FM-1 в режиме обновления: запись прервалась. Нажмите «ПРОШИТЬ FELUCCA», чтобы продолжить.", "busy");
      $("s1").classList.add("hot");
    } else if (d.felucca) {
      say("st0", `Найден FM-1 с Felucca (${id.text}).`, "ok");
      await refreshSlots(true);
    } else {
      say("st0", `Найден FM-1 с заводской прошивкой (${id.text}). Сначала шаг 01: прошивка Felucca.`, "ok");
      $("s1").classList.add("hot");
    }
    log(`устройство: ${id ? id.text : "нет"}`);
  } catch (e) {
    say("st0", e.message, "err");
    log(String(e.stack || e));
  }
  setBusy(false);
}
$("connect").addEventListener("click", connect);
$("fwok").addEventListener("change", () => setBusy(busy));

// ---------------------------------------------------------------------------------------- firmware ---
$("fwgo").addEventListener("click", async () => {
  if (busy || !image) return;
  setBusy(true);
  bar("fwbar", 0);
  try {
    await access();
    const up = new Updater(midi, (...a) => log(...a));
    const total = 74 + 1167;      // the requests of a whole install (Felucca's installer counts them the same way)
    const step = (k, a) => {
      log(`прошивка: ${k} ${a ?? ""}`);
      if (k === "start") say("st1", `Начинаю (${a})…`, "busy");
      if (k === "verify") { say("st1", "Проверка…", "busy"); bar("fwbar", a / total); }
      if (k === "loader") say("st1", "Перехожу в режим записи…", "busy");
      if (k === "write") { say("st1", "Запись… не отключайте кабель", "busy"); bar("fwbar", (74 + a) / total); }
      if (k === "reboot") say("st1", "Перезагрузка…", "busy");
      if (k === "done") { bar("fwbar", 1); }
    };
    const ota = await up.find((id) => /^ota-/i.test(id.model));
    if (ota) {
      ota.link.close();
      log("найден режим обновления: продолжаю запись");
      const ok = await up.resume(image, step, FW.product);
      if (!ok) throw Object.assign(new Error("stopped"), { code: "stopped" });
    } else {
      await up.install(image, FW.product, step);
    }
    say("st1", `Felucca ${FW.version} установлена. Переходите к шагу 02.`, "ok");
    say("st0", `FM-1 с Felucca (${FW.product}).`, "ok");
    chip("devchip", `FELUCCA (${FW.product})`, "ok");
    $("s1").classList.remove("hot");
    $("s2").classList.add("hot");
    await sleep(1500);
    await refreshSlots(true);
  } catch (e) {
    const text = e && e.code && MIDI_TEXT[e.code] ? MIDI_TEXT[e.code] + (e.detail || "") : "Ошибка: " + (e && e.message);
    say("st1", text, "err");
    log(String(e && e.stack || e));
  }
  setBusy(false);
});

// ----------------------------------------------------------------------------------------- plugins ---
async function withFelucca(fn) {
  await access();
  const link = await P.findFelucca(midi);
  if (!link) {
    throw new Error("FM-1 с Felucca 1.9+ не отвечает на команды плагинов. Если на нём заводская прошивка или Felucca старше 1.9, сначала шаг 01.");
  }
  try { return await fn(link); } finally { link.close(); }
}

function showSlots(inf) {
  for (const s of inf.slots) {
    const id = `slot${s.slot}`;
    if (!document.getElementById(id)) continue;
    if (s.state === 0) chip(id, "ПУСТО");
    else chip(id, `${s.name || "?"} · ${s.stateText.toUpperCase()}`, s.state === 1 ? "ok" : "err");
  }
}

async function refreshSlots(quiet = false) {
  try {
    const inf = await withFelucca((l) => P.info(l));
    showSlots(inf);
    log("слоты: " + inf.slots.map((s) => `${s.slot}=${s.name || "-"}/${s.stateText}`).join(", "));
    if (!quiet) say("st2", "Слоты прочитаны.", "ok");
    return inf;
  } catch (e) {
    if (!quiet) say("st2", e.message, "err");
    log(String(e.stack || e));
    return null;
  }
}
$("plinfo").addEventListener("click", async () => {
  if (busy) return;
  setBusy(true);
  await refreshSlots(false);
  setBusy(false);
});

async function installBlobs(list) {
  setBusy(true);
  bar("plbar", 0);
  try {
    const totalChunks = list.reduce((n, b) => n + Math.ceil(b.bytes.length / 256), 0);
    let done = 0;
    await withFelucca(async (link) => {
      for (const b of list) {
        say("st2", `Записываю ${b.h.name} в слот ${b.h.slot}…`, "busy");
        const before = done;
        const h = await P.install(link, b.bytes, (n) => { done = before + n; bar("plbar", done / totalChunks); });
        log(`${h.name}: записан в слот ${h.slot}` + (h.number ? `, номер движка ${h.number}` : ""));
      }
      say("st2", "Перезапускаю FM-1…", "busy");
      await P.restart(link);
    });
    say("st2", "Жду FM-1 после перезапуска…", "busy");
    let inf = null;
    for (let t = 0; t < 20 && !inf; t++) {
      await sleep(1500);
      try { inf = await withFelucca((l) => P.info(l)); } catch (_) { inf = null; }
    }
    if (!inf) {
      say("st2", "Записано. FM-1 не ответил после перезапуска: проверьте его и нажмите «ПРОВЕРИТЬ СЛОТЫ».", "busy");
    } else {
      showSlots(inf);
      const bad = list.filter((b) => inf.slots[b.h.slot] && inf.slots[b.h.slot].state !== 1);
      if (bad.length) {
        say("st2", "Записано, но " + bad.map((b) => `${b.h.name}: ${inf.slots[b.h.slot].stateText}`).join("; "), "err");
      } else {
        bar("plbar", 1);
        say("st2", "Готово! Ищите " + list.map((b) => b.h.name).join(" и ") + " на ручке ENG.", "ok");
        $("s2").classList.remove("hot");
        $("s3").classList.add("hot");
      }
    }
  } catch (e) {
    say("st2", e.message, "err");
    log(String(e.stack || e));
  }
  setBusy(false);
}
$("plgo").addEventListener("click", () => { if (!busy && blobs) installBlobs(blobs); });

$("fepfile").addEventListener("change", () => setBusy(busy));
$("fepgo").addEventListener("click", async () => {
  const f = $("fepfile").files[0];
  if (busy || !f) return;
  try {
    const bytes = new Uint8Array(await f.arrayBuffer());
    const h = P.parseFep(bytes);
    if (!confirm(`Записать ${h.name} v${h.version} в слот ${h.slot}? То, что там стоит, будет стёрто.`)) return;
    await installBlobs([{ file: f.name, name: h.name, bytes, h }]);
  } catch (e) {
    say("st2", e.message, "err");
  }
});

for (const slot of [0, 1]) {
  $(`er${slot}`).addEventListener("click", async () => {
    if (busy || !confirm(`Очистить слот ${slot}? Движок из него пропадёт после перезапуска.`)) return;
    setBusy(true);
    try {
      await withFelucca(async (link) => { await P.erase(link, slot); await P.restart(link); });
      say("st2", `Слот ${slot} очищен, FM-1 перезапускается.`, "ok");
      chip(`slot${slot}`, "ПУСТО");
    } catch (e) {
      say("st2", e.message, "err");
      log(String(e.stack || e));
    }
    setBusy(false);
  });
}

if (!navigator.requestMIDIAccess) {
  say("st0", "Этот браузер не умеет WebMIDI. Откройте страницу в Chrome или Edge на компьютере.", "err");
}

// the demo: a pixel player over an <audio>
(() => {
  const a = $("demo"), btn = $("play"), pb = $("pbar"), tm = $("ptime");
  const fmt = (t) => `${Math.floor(t / 60)}:${String(Math.floor(t % 60)).padStart(2, "0")}`;
  bar("pbar", 0);
  btn.addEventListener("click", () => { if (a.paused) a.play(); else a.pause(); });
  a.addEventListener("play", () => { btn.textContent = "❚❚"; btn.setAttribute("aria-label", "Пауза"); });
  a.addEventListener("pause", () => { btn.textContent = "▶"; btn.setAttribute("aria-label", "Играть демо"); });
  a.addEventListener("timeupdate", () => {
    const f = a.duration ? a.currentTime / a.duration : 0;
    bar("pbar", f); pb.setAttribute("aria-valuenow", String(Math.round(f * 100))); tm.textContent = fmt(a.currentTime);
  });
  const seek = (f) => { if (a.duration) a.currentTime = Math.max(0, Math.min(1, f)) * a.duration; };
  pb.addEventListener("click", (e) => { const r = pb.getBoundingClientRect(); seek((e.clientX - r.left) / r.width); });
  pb.addEventListener("keydown", (e) => {
    if (!a.duration) return;
    if (e.key === "ArrowRight") seek(a.currentTime / a.duration + 0.05);
    if (e.key === "ArrowLeft") seek(a.currentTime / a.duration - 0.05);
  });
})();
