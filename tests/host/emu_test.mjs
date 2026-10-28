// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Charles Vestal
// The browser build in Node: boot it on the boardwalk (the sea and the attract tune sound), step up to Bells with
// PLAY, ring, and expect sound and screen frames throughout.
import fs from "fs";
const wasm = fs.readFileSync(process.argv[2]);
let mem;
const wasi = new Proxy({ clock_time_get: (id, p, out) => { new DataView(mem.buffer).setBigUint64(out, 0n, true); return 0; },
  fd_write: (fd, iov, n, out) => { new DataView(mem.buffer).setUint32(out, 0, true); return 0; } }, { get: (t, k) => k in t ? t[k] : () => 0 });
const { instance } = await WebAssembly.instantiate(wasm, { wasi_snapshot_preview1: wasi, env: new Proxy({}, { get: () => () => 0 }) });
const ex = instance.exports; mem = ex.memory;
if (ex._initialize) ex._initialize();
ex.web_master(2800); ex.web_boot();
const B_PLAY = 10;                       // plat.h's button order
function run(blocks) {                   // the peak over these blocks
  let peak = 0;
  for (let i = 0; i < blocks; i++) {
    ex.web_render(128);
    const l = new Float32Array(mem.buffer, ex.web_out_l(), 128);
    for (const v of l) peak = Math.max(peak, Math.abs(v));
  }
  return peak;
}
const walk = run(1400);                  // ~4 s on the boardwalk
ex.web_buttons(1 << B_PLAY); run(20); ex.web_buttons(0); run(40);   // into Bells
ex.web_buttons(1 << B_PLAY); run(20); ex.web_buttons(0);            // ring
const ring = run(1400);
console.log("boardwalk peak", walk.toFixed(3), "bells peak", ring.toFixed(3), "blits", ex.web_blits());
process.exit(walk > 0.005 && walk < 1 && ring > 0.05 && ring < 1 && ex.web_blits() > 20 ? 0 : 1);
