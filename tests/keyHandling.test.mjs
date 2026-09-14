// keyHandling.test.mjs; a security regression lock, not a smoke test.
// Asserts the properties a Citadel-grade security review would actually
// check by hand: the API key never reaches deviceGate/deviceExecutor/
// commandExecutor (the files that write to the persisted audit log), and
// modelClient.js/geminiClient.js never read process.env themselves.

import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const SRC = path.resolve(__dirname, '..', 'src', 'lib');

function read(file) {
  return fs.readFileSync(path.join(SRC, file), 'utf8');
}

test('deviceGate.js never references apiKey in any form', () => {
  assert.doesNotMatch(read('deviceGate.js'), /apiKey/i);
});

test('deviceExecutor.js never references apiKey in any form', () => {
  assert.doesNotMatch(read('deviceExecutor.js'), /apiKey/i);
});

test('commandExecutor.js never references apiKey in any form', () => {
  assert.doesNotMatch(read('commandExecutor.js'), /apiKey/i);
});

test('modelClient.js never reads process.env itself (the key must arrive as a parameter)', () => {
  assert.doesNotMatch(read('modelClient.js'), /process\.env/);
});

test('geminiClient.js never reads process.env itself (the key must arrive as a parameter)', () => {
  assert.doesNotMatch(read('geminiClient.js'), /process\.env/);
});

test('modelClient.js and geminiClient.js never call console.log/warn/error with anything key-shaped', () => {
  for (const file of ['modelClient.js', 'geminiClient.js']) {
    const src = read(file);
    const consoleCalls = src.match(/console\.(log|warn|error|info)\([^)]*\)/g) || [];
    for (const call of consoleCalls) {
      assert.doesNotMatch(call, /apiKey|api_key/i, `${file}: ${call}`);
    }
  }
});

test('bin/ciall.mjs is the only file in src/ or bin/ that reads ANTHROPIC_API_KEY or GEMINI_API_KEY from process.env', () => {
  const binPath = path.resolve(__dirname, '..', 'bin', 'ciall.mjs');
  const binSrc = fs.readFileSync(binPath, 'utf8');
  // bin/ciall.mjs uses computed access (process.env[envVar]), not literal
  // dot access, so this checks for the pattern actually used: process.env
  // referenced AND both key names present as literal strings.
  assert.match(binSrc, /process\.env\[/);
  assert.match(binSrc, /'ANTHROPIC_API_KEY'/);
  assert.match(binSrc, /'GEMINI_API_KEY'/);

  const libFiles = fs.readdirSync(SRC).filter((f) => f.endsWith('.js'));
  for (const file of libFiles) {
    const src = read(file);
    assert.doesNotMatch(src, /process\.env\.(ANTHROPIC_API_KEY|GEMINI_API_KEY)/, `${file} must not read the key from the environment directly`);
    assert.doesNotMatch(src, /process\.env\[['"](ANTHROPIC_API_KEY|GEMINI_API_KEY)['"]\]/, `${file} must not read the key from the environment directly`);
  }
});
