// SPDX-License-Identifier: GPL-3.0-only
// A fake M-VAVE FM-1 behind WebMIDI: the update protocol (identity, read requests) and Felucca's plugin commands.
(() => {
  const S = window.__fm1 = { fw: "FM-1_015", mode: "app", got: [], slots: [{st:0,name:"",id:0,ver:0},{st:0,name:"",id:0,ver:0}],
    blobs: {0: [], 1: []}, regs: [], log: [], otaBytes: 0, otaReqs: 0, plugins: true, restarted: 0, chunkSum: 0 };
  const pack7 = (data) => { const out = []; let acc = 0, nb = 0; for (const b of data) { acc |= b << nb; nb += 8; while (nb >= 7) { out.push(acc & 0x7F); acc >>>= 7; nb -= 7; } } if (nb) out.push(acc & 0x7F); return out; };
  const unpack7 = (s) => { const out = []; let acc = 0, nb = 0; for (const b of s) { acc |= (b & 0x7F) << nb; nb += 7; while (nb >= 8) { out.push(acc & 0xFF); acc >>>= 8; nb -= 8; } } return out; };
  const g32 = (b, i) => b[i] + b[i+1]*128 + b[i+2]*16384 + b[i+3]*2097152 + b[i+4]*268435456;
  const u32 = (v) => [0,1,2,3,4].map((i) => Math.floor(v / 2 ** (7*i)) & 0x7F);
  const listeners = [];
  // ids as Chrome gives them; names settable (S.inName / S.outName: another firmware's, Windows' MIDIIN2 (...))
  const input = { id: "input-1", get name() { return S.inName || "FM-1"; }, manufacturer: "", state: "connected", onmidimessage: null, open: async () => {}, };
  const other = { id: "input-0", name: "Keystation 49", manufacturer: "M-Audio", state: "connected", onmidimessage: null, open: async () => {} };
  const emit = (bytes) => setTimeout(() => input.onmidimessage && input.onmidimessage({ data: new Uint8Array(bytes) }), 2);
  function identity() {
    const txt = S.mode === "ota" ? "ota-" + S.fw.replace(/_.*/, "") + "_1" : S.fw;
    const d = new Array(34).fill(0); d[0] = 0; d[1] = 0x59; d[2] = 0x11;
    for (let i = 0; i < txt.length; i++) d[6 + i] = txt.charCodeAt(i);
    return [0xF0, ...pack7(d), 0xF7];
  }
  function request(addr, len, fl = 0) {
    const u = [0, 0x59, 0x30, 0, 0, 0, fl, addr & 0xFF, (addr >>> 8) & 0xFF, (addr >>> 16) & 0xFF, (addr >>> 24) & 0xFF, len & 0xFF, (len >> 8) & 0xFF, 0];
    let s = 0; for (let i = 6; i < 14; i++) s += u[i]; u.push(~s & 0xFF);
    return [0xF0, ...pack7(u), 0xF7];
  }
  // the update: the device asks for parts of the image
  let plan = [], planAt = 0;
  function nextReq() {
    if (planAt < plan.length) { const [a, l] = plan[planAt++]; S.otaReqs++; emit(request(a, l)); }
  }
  function startUpgrade() {
    const size = S.imageSize;
    if (S.mode === "app") {          // step 1: a few reads, then the loader
      plan = []; for (let i = 0; i < 74; i++) plan.push([(i * 4096) % (size - 512), 256]); plan.push([0xE0000000, 0]);
    } else {                         // step 2: the whole image
      plan = []; for (let a = 0; a < size; a += 512) plan.push([a, Math.min(512, size - a)]); plan.push([0xF0000000, 0]);
    }
    planAt = 0; setTimeout(nextReq, 50);
  }
  function onResponse(u) {
    const addr = (u[7] | (u[8] << 8) | (u[9] << 16) | (u[10] << 24)) >>> 0, len = u[11] | (u[12] << 8);
    if (addr === 0xE0000000) { S.mode = "ota"; S.fw = S.target.replace(/_.*/, "") + "_0"; S.log.push("loader"); return; }   // (the package's loader)
    if (addr === 0xF0000000) { S.mode = "app"; S.fw = S.target; S.log.push("written"); return; }
    if (S.mode === "ota") { S.otaBytes += len; for (let i = 0; i < len; i++) S.chunkSum = (S.chunkSum + u[14 + i] * ((addr + i) % 251 + 1)) % 1000000007; }
    nextReq();
  }
  function plugin(cmd, a) {
    const H = [0xF0, 0x7D, 0x46, 0x4C, cmd];
    if (!S.plugins || !/^FM-1_9/.test(S.fw) || S.mode !== "app") return;
    if (cmd === 82) {
      const out = [1, 2];
      S.slots.forEach((s) => { out.push(s.st, ...u32(s.id), ...u32(s.ver), s.st ? 33 : 0, ...Array.from(s.name, (c) => c.charCodeAt(0)), 0); });
      out.push(S.regs.length); S.regs.forEach((r, i) => out.push(i, ...u32(r)));
      emit([...H, ...out, 0xF7]);
    } else if (cmd === 85) { const id = g32(a, 0); if (!S.regs.includes(id)) S.regs.push(id); emit([...H, S.regs.indexOf(id), 0, 0xF7]); }
    else if (cmd === 83) { S.blobs[a[0]] = []; S.slots[a[0]] = {st:0,name:"",id:0,ver:0}; emit([...H, a[0], 0, 0xF7]); }
    else if (cmd === 84) {
      const slot = a[0], off = a[1] | (a[2] << 7) | (a[3] << 14), p = a.slice(4), data = [];
      for (let i = 0; i < p.length; i += 8) { const top = p[i]; for (let j = 1; j < 8 && i + j < p.length; j++) data.push(p[i + j] | (((top >> (j - 1)) & 1) << 7)); }
      data.forEach((b, i) => { S.blobs[slot][off + i] = b; });
      emit([...H, slot, a[1], a[2], a[3], 0, 0xF7]);
    } else if (cmd === 86) {
      emit([...H, 0, 0xF7]); S.restarted++;
      for (const k of [0, 1]) { const b = S.blobs[k]; if (b.length > 96) {
        const name = String.fromCharCode(...b.slice(64, 80)).split("\0")[0]; S.slots[k] = { st: 1, name, id: b[16] | (b[17]<<8) | (b[18]<<16) | (b[19]<<24) >>> 0, ver: b[20] }; } }
    }
  }
  const output = { id: "output-1", get name() { return S.outName || S.inName || "FM-1"; }, manufacturer: "", state: "connected", open: async () => {},
    send(bytes) {
      bytes = Array.from(bytes);
      if (bytes.join() === [0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7].join()) { emit(identity()); return; }
      if (bytes.join() === [0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7].join()) { startUpgrade(); return; }
      if (bytes[1] === 0x7D && bytes[2] === 0x46 && bytes[3] === 0x4C) { plugin(bytes[4], bytes.slice(5, -1)); return; }
      const u = unpack7(bytes.slice(1, -1));
      if (u[1] === 0x59 && u[2] === 0x30) onResponse(u);
    } };
  const otherOut = { id: "output-0", name: "Keystation 49", state: "connected", open: async () => {}, send() {} };
  const access = { inputs: new Map([["input-0", other], ["input-1", input]]), outputs: new Map([["output-0", otherOut], ["output-1", output]]),
    onstatechange: null, sysexEnabled: true };
  navigator.requestMIDIAccess = async (o) => { if (!o || !o.sysex) throw new Error("no sysex"); return access; };
})();
