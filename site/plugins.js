// SPDX-License-Identifier: GPL-3.0-only
// AMEN-FM1: Felucca's plugin commands over WebMIDI (the same steps as Felucca's tools/fm1_plugin.py; the protocol is
// Felucca's web/EDITOR_PROTOCOL.md, "Plugin engines (82-87; 1.9)").

const HDR = [0xF0, 0x7D, 0x46, 0x4C];
export const CMD = { INFO: 82, ERASE: 83, WRITE: 84, REG: 85, RESTART: 86, ENGINES: 87 };
export const STATES = ["пусто", "играет", "битый файл", "отклонён", "пропущен после сбоя", "нет номера"];
const RC = ["готово", "неверные аргументы", "ошибка флеша", "слот занят (играет)", "список номеров полон",
  "метка обновления"];
const FM1_MAGIC = 0x31504546, HDR_SIZE = 96, FE_NEW = 0xFF, ENGI_PLUG0 = 32;
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

function fail(msg) { const e = new Error(msg); e.plugin = true; return e; }

// a 32-bit value as five 7-bit bytes, LSB first
export const u32 = (v) => [0, 1, 2, 3, 4].map((i) => Math.floor(v / 2 ** (7 * i)) & 0x7F);
const g32 = (b, i) => b[i] + b[i + 1] * 128 + b[i + 2] * 16384 + b[i + 3] * 2097152 + b[i + 4] * 268435456;

// groups of 7 bytes: a byte of their top bits, then the 7 low parts (fm1_plugin.py pack7)
export function pack7(data) {
  const out = [];
  for (let i = 0; i < data.length; i += 7) {
    const g = data.slice(i, i + 7);
    let top = 0;
    g.forEach((b, j) => { top |= ((b >> 7) & 1) << j; });
    out.push(top, ...Array.from(g, (b) => b & 0x7F));
  }
  return out;
}

// a .fep blob's header (Felucca-Engines src/felucca_plugin_fm1.h, fe_fm1_hdr_t)
export function parseFep(bytes) {
  if (bytes.length < HDR_SIZE) throw fail("это не плагин FM-1 (.fep): файл слишком короткий");
  const dv = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  if (dv.getUint32(0, true) !== FM1_MAGIC) throw fail("это не плагин FM-1 (.fep)");
  const name = new TextDecoder().decode(bytes.subarray(64, 80)).split("\0")[0];
  return {
    id: dv.getUint32(16, true), version: dv.getUint32(20, true), engine: dv.getUint32(24, true),
    name, slot: dv.getUint32(80, true), size: bytes.length,
  };
}

// one in/out pair of a Felucca FM-1
export class PluginLink {
  constructor(input, output) {
    this.input = input; this.output = output; this.q = []; this.waiter = null;
    input.onmidimessage = (ev) => {
      if (ev.data[0] !== 0xF0) return;
      this.q.push(Array.from(ev.data));
      if (this.waiter) { const w = this.waiter; this.waiter = null; w(); }
    };
  }
  close() { this.input.onmidimessage = null; }
  async read(ms) {
    if (!this.q.length) {
      let tm;
      await Promise.race([new Promise((r) => { this.waiter = r; }), new Promise((r) => { tm = setTimeout(r, ms); })]);
      clearTimeout(tm);
      this.waiter = null;
    }
    return this.q.shift() || null;
  }
  // a command and its answer (the bytes after the command's number), or null on no answer
  async ask(cmd, args = [], ms = 3000) {
    this.q.length = 0;
    try { this.output.send([...HDR, cmd, ...args, 0xF7]); } catch (_) { return null; }
    const end = Date.now() + ms;
    while (Date.now() < end) {
      const p = await this.read(end - Date.now());
      if (!p) break;
      if (p[1] === HDR[1] && p[2] === HDR[2] && p[3] === HDR[3] && p[4] === cmd) return p.slice(5, -1);
    }
    return null;
  }
}

// the first port pair that answers PLUGIN_INFO: a Felucca 1.9+ with plugins
export async function findFelucca(access) {
  const outs = [...access.outputs.values()];
  for (const input of access.inputs.values()) {
    if (input.state === "disconnected") continue;
    if (!/fm-1|felucca|composite|usb-midi/i.test(input.name || "")) continue;   // never probe other gear
    const output = outs.find((o) => o.name === input.name && o.state !== "disconnected");
    if (!output) continue;
    try { await input.open(); await output.open(); } catch (_) { continue; }
    const link = new PluginLink(input, output);
    const r = await link.ask(CMD.INFO, [], 1500);
    if (r && r.length >= 3) return link;
    link.close();
  }
  return null;
}

export async function info(link) {
  const r = await link.ask(CMD.INFO);
  if (!r) throw fail("FM-1 не ответил на запрос слотов");
  if (r.length < 3) throw fail("флеш плагинов недоступен");
  const abi = r[0], ns = r[1], slots = [];
  let i = 2;
  for (let k = 0; k < ns; k++) {
    const st = r[i], id = g32(r, i + 1), version = g32(r, i + 6), no = r[i + 11];
    i += 12;
    const j = r.indexOf(0, i);
    const name = String.fromCharCode(...r.slice(i, j));
    i = j + 1;
    slots.push({ slot: k, state: st, stateText: STATES[st] ?? `состояние ${st}`, id, version, engine: no, name });
  }
  const regs = [];
  const n = r[i++] ?? 0;
  for (let k = 0; k < n; k++, i += 6) regs.push({ engine: r[i] + ENGI_PLUG0, id: g32(r, i + 1) });
  return { abi, slots, regs };
}

function check(rc, what) {
  if (rc) throw fail(`${what}: ${RC[rc] ?? "код " + rc}`);
}

// install a blob into the slot it is linked for; onProgress(done, total)
export async function install(link, blob, onProgress = () => {}) {
  const h = parseFep(blob);
  if (h.engine === FE_NEW) {
    const r = await link.ask(CMD.REG, u32(h.id));
    if (!r) throw fail("нет ответа на регистрацию номера движка");
    check(r[1], "номер движка");
    h.number = r[0] + ENGI_PLUG0;
  }
  const e = await link.ask(CMD.ERASE, [h.slot], 5000);
  if (!e) throw fail("нет ответа на очистку слота");
  check(e[1], "очистка слота");
  const total = Math.ceil(blob.length / 256);
  for (let off = 0, n = 0; off < blob.length; off += 256, n++) {
    const args = [h.slot, off & 0x7F, (off >> 7) & 0x7F, (off >> 14) & 0x7F, ...pack7(blob.subarray(off, off + 256))];
    const r = await link.ask(CMD.WRITE, args);
    if (!r) throw fail(`нет ответа при записи (смещение ${off})`);
    check(r[4], `запись (смещение ${off})`);
    onProgress(n + 1, total);
  }
  return h;
}

export async function erase(link, slot) {
  const r = await link.ask(CMD.ERASE, [slot], 5000);
  if (!r) throw fail("нет ответа на очистку слота");
  check(r[1], "очистка слота");
}

// the FM-1 restarts (plugins load at power-on); it may go before it answers
export async function restart(link) {
  await link.ask(CMD.RESTART, [], 1500);
  link.close();
  await sleep(500);
}
