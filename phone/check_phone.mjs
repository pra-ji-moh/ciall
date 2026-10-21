// check_phone.mjs -- the brain inside phone/index.html lives exactly the life
// the native brain does (brain/ol_same.c), act for act.
//   node check_phone.mjs "<what ol_same printed>"
import { readFileSync } from "node:fs";
const page = readFileSync(new URL("./index.html", import.meta.url), "utf8");
const b64 = page.match(/var WASM = "([A-Za-z0-9+/=]+)"/)[1];
const { instance } = await WebAssembly.instantiate(Buffer.from(b64, "base64"));
const e = instance.exports, N = 85;
e.ol_begin(42);
const input = new Float32Array(e.memory.buffer, e.ol_input(), N);
let h = 0;
for (let t = 0; t < 20000; t++) {
  for (let k = 0; k < N; k++) input[k] = Math.fround(((t * 31 + k * 17 + ((t * k) % 7)) % 101) / 100);
  h = (Math.imul(h, 31) + e.ol_step()) >>> 0;
}
const expect = new Float32Array(e.memory.buffer, e.ol_expect(), 83);
const got = `${h} ${expect[3].toPrecision(9).replace(/0+$/, "")}`;
const want = (process.argv[2] || "").trim();
console.log(got === want ? `same life, act for act (${got})` : `DIFFERENT: phone ${got}, native ${want}`);
process.exit(got === want ? 0 : 1);
