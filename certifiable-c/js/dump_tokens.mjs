// The real Smarsh lexer's token stream for one source file, as JSON. The C
// test (test_frontend.c) prints its own stream in exactly this shape and
// compares the two strings byte for byte. Reads the source from a file, not
// argv, because test programs contain < and > and a shell would redirect.
import { readFileSync } from 'node:fs';
import { tokenize } from 'file:///C:/Users/USER/smarsh/src/lexer.js';
const src = readFileSync(process.argv[2], 'utf8');
try {
  const toks = tokenize(src);
  const out = toks.map((t) => ({
    kind: t.type, text: t.type === 'eof' ? '' : String(t.value), nl: !!t.nlBefore,
  }));
  console.log(JSON.stringify(out));
} catch (e) {
  console.log(JSON.stringify({ error: e.kind || 'error' }));
}
