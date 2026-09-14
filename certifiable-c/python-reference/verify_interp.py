"""End-to-end differential test: the C engine's logic against the real
Smarsh runtime, on whole programs.

This is the check the whole port has been building toward. The lexer and
parser were verified structurally; this runs actual programs and compares
what they PRINT, which is the only thing a user ever sees.

Transliterates smarsh_interp.c on top of the already-transliterated lexer
and parser, then diffs stdout against `node bin/smarsh.mjs`. Verifies the
ALGORITHM, not the C -- unchanged from every other verify_*.py here.
"""

import json
import subprocess

from verify_lexer import LexError
from verify_parser import c_parse, ParseError

MAX_DEPTH = 64
DEFAULT_STEPS = 1000000

NIL = ('nil',)
GROUNDLESS = ('groundless',)


class RunError(Exception):
    def __init__(self, status):
        self.status = status


class Flow:
    NORMAL, RETURN, BREAK, CONTINUE = range(4)


class Interp:
    def __init__(self, steps=DEFAULT_STEPS):
        self.out = []
        self.steps = steps
        self.flow = Flow.NORMAL

    def spend(self):
        if self.steps <= 0:
            raise RunError('SI_ERR_STEPS_EXHAUSTED')
        self.steps -= 1

    # --- rendering, matching the JS engine's str() -------------------
    def render(self, v):
        if v is NIL or v == NIL:
            return 'nil'
        if v == GROUNDLESS:
            return 'groundless'
        if isinstance(v, bool):
            return 'true' if v else 'false'
        if isinstance(v, float):
            if v == int(v) and abs(v) < 1e15:
                return str(int(v))
            return f'{v:.12g}'
        if isinstance(v, str):
            return v
        if isinstance(v, list):
            return '[' + ', '.join(self.render(x) for x in v) + ']'
        return '?'

    def truthy(self, v):
        if v == NIL or v == GROUNDLESS:
            return False
        if isinstance(v, bool):
            return v
        if isinstance(v, float):
            return v != 0.0
        return True

    def equal(self, a, b):
        # groundless != nil: if these compared equal the distinction
        # would be unobservable from inside a program.
        if (a == GROUNDLESS) != (b == GROUNDLESS):
            return False
        if (a == NIL) != (b == NIL):
            return False
        if isinstance(a, bool) != isinstance(b, bool):
            return False
        return a == b

    def binary(self, op, l, r):
        if l == GROUNDLESS or r == GROUNDLESS:
            if op == '==':
                return l == GROUNDLESS and r == GROUNDLESS
            if op == '!=':
                return not (l == GROUNDLESS and r == GROUNDLESS)
            return GROUNDLESS
        if op == '==':
            return self.equal(l, r)
        if op == '!=':
            return not self.equal(l, r)
        if op == '+' and isinstance(l, str) and isinstance(r, str):
            return l + r
        if not isinstance(l, float) or not isinstance(r, float):
            raise RunError('SI_ERR_TYPE')
        if op == '+':
            return l + r
        if op == '-':
            return l - r
        if op == '*':
            return l * r
        if op == '**':
            return l ** r
        if op == '<':
            return l < r
        if op == '>':
            return l > r
        if op == '<=':
            return l <= r
        if op == '>=':
            return l >= r
        if op == '/':
            if r == 0.0:
                raise RunError('SI_ERR_DIVIDE_BY_ZERO')
            return l / r
        if op == '%':
            if r == 0.0:
                raise RunError('SI_ERR_DIVIDE_BY_ZERO')
            return l - r * int(l / r)
        raise RunError('SI_ERR_UNSUPPORTED')

    def builtin(self, name, args):
        if name == 'print':
            if len(args) != 1:
                raise RunError('SI_ERR_TYPE')
            self.out.append(self.render(args[0]))
            return NIL
        if name == 'str':
            if len(args) != 1:
                raise RunError('SI_ERR_TYPE')
            return self.render(args[0])
        if name == 'len':
            if len(args) != 1:
                raise RunError('SI_ERR_TYPE')
            if isinstance(args[0], (list, str)):
                return float(len(args[0]))
            raise RunError('SI_ERR_TYPE')
        raise RunError('SI_ERR_UNSUPPORTED')

    # --- evaluation ---------------------------------------------------
    def eval(self, n, scope, depth=0):
        if depth >= MAX_DEPTH:
            raise RunError('SI_ERR_DEPTH_EXCEEDED')
        self.spend()
        t = n['t']

        if t == 'Num':
            return float(n['x'])
        if t == 'DecLit':
            raise RunError('SI_ERR_UNSUPPORTED')
        if t == 'Str':
            return n['x']
        if t == 'Bool':
            return n['x'] == 'true'
        if t == 'Nil':
            return NIL
        if t == 'Ident':
            s = scope
            while s is not None:
                if n['x'] in s[0]:
                    return s[0][n['x']][0]
                s = s[1]
            raise RunError('SI_ERR_UNKNOWN_NAME')
        if t == 'ListLit':
            return [self.eval(c, scope, depth + 1) for c in n['k']]

        if t == 'Unary':
            v = self.eval(n['k'][0], scope, depth + 1)
            if v == GROUNDLESS:
                if n['x'] in ('not', '!'):
                    return True
                return GROUNDLESS
            if n['x'] in ('not', '!'):
                return not self.truthy(v)
            if n['x'] == '-':
                if not isinstance(v, float):
                    raise RunError('SI_ERR_TYPE')
                return -v
            raise RunError('SI_ERR_UNSUPPORTED')

        if t == 'Binary':
            l = self.eval(n['k'][0], scope, depth + 1)
            r = self.eval(n['k'][1], scope, depth + 1)
            return self.binary(n['x'], l, r)

        if t == 'Logical':
            l = self.eval(n['k'][0], scope, depth + 1)
            if n['x'] == 'and':
                return l if not self.truthy(l) else self.eval(n['k'][1], scope, depth + 1)
            return l if self.truthy(l) else self.eval(n['k'][1], scope, depth + 1)

        if t == 'Index':
            obj = self.eval(n['k'][0], scope, depth + 1)
            idx = self.eval(n['k'][1], scope, depth + 1)
            if not isinstance(obj, list) or not isinstance(idx, float):
                raise RunError('SI_ERR_TYPE')
            i = int(idx)
            if idx < 0 or i >= len(obj):
                raise RunError('SI_ERR_INDEX_OUT_OF_RANGE')
            return obj[i]

        if t == 'Call':
            callee = n['k'][0]
            if callee['t'] != 'Ident':
                raise RunError('SI_ERR_UNSUPPORTED')
            args = [self.eval(c, scope, depth + 1) for c in n['k'][1:]]
            return self.builtin(callee['x'], args)

        if t in ('Program', 'Block'):
            inner = ({}, scope) if t == 'Block' else scope
            for c in n['k']:
                self.eval(c, inner, depth + 1)
                if self.flow != Flow.NORMAL:
                    break
            return NIL

        if t == 'ExprStmt':
            return self.eval(n['k'][0], scope, depth + 1)

        if t == 'Declare':
            v = self.eval(n['k'][0], scope, depth + 1)
            scope[0][n['x']] = (v, True)   # mutability not diffed here
            return NIL

        if t == 'Assign':
            target = n['k'][0]
            if target['t'] != 'Ident':
                raise RunError('SI_ERR_UNSUPPORTED')
            v = self.eval(n['k'][1], scope, depth + 1)
            s = scope
            while s is not None:
                if target['x'] in s[0]:
                    s[0][target['x']] = (v, True)
                    return v
                s = s[1]
            raise RunError('SI_ERR_UNKNOWN_NAME')

        if t == 'If':
            if self.truthy(self.eval(n['k'][0], scope, depth + 1)):
                return self.eval(n['k'][1], scope, depth + 1)
            if len(n['k']) > 2:
                return self.eval(n['k'][2], scope, depth + 1)
            return NIL

        if t == 'While':
            while True:
                if not self.truthy(self.eval(n['k'][0], scope, depth + 1)):
                    break
                self.eval(n['k'][1], scope, depth + 1)
                if self.flow == Flow.BREAK:
                    self.flow = Flow.NORMAL
                    break
                if self.flow == Flow.CONTINUE:
                    self.flow = Flow.NORMAL
                if self.flow == Flow.RETURN:
                    break
            return NIL

        if t == 'Return':
            self.flow = Flow.RETURN
            return self.eval(n['k'][0], scope, depth + 1) if n['k'] else NIL
        if t == 'Break':
            self.flow = Flow.BREAK
            return NIL
        if t == 'Continue':
            self.flow = Flow.CONTINUE
            return NIL

        raise RunError('SI_ERR_UNSUPPORTED')


def c_run(src):
    tree = c_parse(src)
    it = Interp()
    it.eval(tree, ({}, None))
    return it.out


def js_run(src):
    import os
    import tempfile
    fd, path = tempfile.mkstemp(suffix='.smarsh')
    with os.fdopen(fd, 'w', newline='\n') as f:
        f.write(src)
    try:
        r = subprocess.run(['node', 'C:/Users/USER/smarsh/bin/smarsh.mjs', 'run', path],
                           capture_output=True, text=True, timeout=90)
        if r.returncode != 0:
            return {'error': (r.stderr or r.stdout).strip()[:60]}
        return [l for l in r.stdout.replace('\r\n', '\n').split('\n') if l != '']
    finally:
        os.unlink(path)


CASES = [
    'print(1 + 2)',
    'print(2 * 3 + 4)',
    'print(2 + 3 * 4)',
    'print(10 - 3 - 2)',
    'print(2 ** 3 ** 2)',
    'print(7 / 2)',
    'print(7 % 3)',
    'print(1 < 2)',
    'print(2 == 2)',
    'print(1 != 2)',
    'print(true and false)',
    'print(true or false)',
    'print(not true)',
    'print("hi")',
    'print("a" + "b")',
    'let x = 5\nprint(x)',
    'let x = 5\nlet y = x * 2\nprint(y)',
    'var i = 0\nwhile i < 3 { i = i + 1 }\nprint(i)',
    'if 1 < 2 { print("yes") } else { print("no") }',
    'if 2 < 1 { print("yes") } else { print("no") }',
    'print([1, 2, 3])',
    'print(len([1, 2, 3]))',
    'let xs = [10, 20]\nprint(xs[1])',
    'print(str(42))',
    'var t = 0\nvar i = 0\nwhile i < 5 { t = t + i\n i = i + 1 }\nprint(t)',
]

FAILURES = []
print('C engine logic vs the real Smarsh runtime, whole programs')
print()

for src in CASES:
    want = js_run(src)
    try:
        got = c_run(src)
        err = None
    except (RunError, ParseError, LexError) as e:
        got, err = None, e.status

    label = src.replace('\n', '; ')
    if len(label) > 44:
        label = label[:41] + '...'

    if isinstance(want, dict):
        ok = err is not None
        detail = f"JS errored, C {'errored too' if ok else 'did not'}"
    elif err is not None:
        ok, detail = False, f'C raised {err}, JS printed {want}'
    else:
        ok = got == want
        detail = f'C={got} JS={want}'

    print(f"  {'ok  ' if ok else 'FAIL'}  {label}" + ('' if ok else f'\n          {detail}'))
    if not ok:
        FAILURES.append(src)

print()
if FAILURES:
    print(f'{len(FAILURES)} of {len(CASES)} diverge from the real runtime')
    raise SystemExit(1)
print(f'all {len(CASES)} programs print exactly what the real Smarsh runtime prints')
