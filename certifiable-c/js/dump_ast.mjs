// The real Smarsh parser's tree for one source file, normalised to the
// shape the C arena can produce (kind, text, ordered children) and printed
// as JSON for a byte-for-byte comparison. Reads from a file, as above.
import { readFileSync } from 'node:fs';
import { parse } from 'file:///C:/Users/USER/smarsh/src/parser.js';
function norm(n) {
  if (n === null || n === undefined) return null;
  if (Array.isArray(n)) return n.map(norm);
  if (typeof n !== 'object' || !n.type) return null;
  const kids = [];
  for (const k of ['expr','left','right','operand','callee','object','test','target','value','iterable']) {
    if (n[k] && typeof n[k] === 'object') kids.push(norm(n[k]));
  }
  for (const k of ['args','indices','elements','body']) {
    if (Array.isArray(n[k])) for (const c of n[k]) kids.push(norm(c));
    else if (n[k] && typeof n[k] === 'object') kids.push(norm(n[k]));
  }
  for (const k of ['then','alt']) if (n[k]) kids.push(norm(n[k]));
  const text = n.op ?? n.name ?? (n.value !== undefined && typeof n.value !== 'object' ? String(n.value) : '');
  return { t: n.type, x: String(text), k: kids.filter(Boolean) };
}
try {
  const ast = parse(readFileSync(process.argv[2], 'utf8'), 'x.smarsh');
  console.log(JSON.stringify(norm(ast)));
} catch (e) {
  console.log(JSON.stringify({ error: e.kind || 'error' }));
}
