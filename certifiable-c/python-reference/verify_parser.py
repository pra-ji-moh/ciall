"""Differential test: the C parser's logic against the real JS parser.

smarsh_parser.c cannot be compiled here, so its algorithm is
transliterated below -- same shunting-yard, same precedence table, same
associativity rule, same nl_before cutoff -- and the tree it produces is
compared structurally against ../../smarsh/src/parser.js.

Verifies the ALGORITHM, not the C. What it does catch is the failure that
actually matters for a parser port: a tree shaped differently from the
reference. Precedence and associativity errors are silent and change
results, which makes them exactly the thing not to take on trust.
"""

import json
import subprocess

from verify_lexer import c_tokenize, LexError

MAX_DEPTH = 64

KW = {'or', 'and', 'not', 'true', 'false', 'nil', 'let', 'var', 'if', 'else',
      'while', 'return', 'break', 'continue'}

UNSUPPORTED_KW = {'fn', 'agent', 'for', 'match', 'attempt', 'maybe', 'choose',
                  'fork', 'import', 'redefine', 'using', 'spawn', 'tensor'}


class ParseError(Exception):
    def __init__(self, status):
        self.status = status


def precedence_of(t):
    if t['kind'] == 'kw':
        return {'or': 1, 'and': 2}.get(t['text'], 0)
    if t['kind'] != 'op':
        return 0
    x = t['text']
    if x in ('==', '!='):
        return 3
    if x in ('<', '>', '<=', '>='):
        return 4
    if x in ('+', '-'):
        return 5
    if x in ('*', '/', '%', '@'):
        return 6
    if x == '**':
        return 7
    return 0


def node(t, x='', k=None):
    return {'t': t, 'x': x, 'k': k or []}


class P:
    def __init__(self, toks):
        self.toks, self.pos = toks, 0

    def peek(self):
        return self.toks[min(self.pos, len(self.toks) - 1)]

    def at_op(self, o):
        t = self.peek()
        return t['kind'] == 'op' and t['text'] == o

    def at_kw(self, k):
        t = self.peek()
        return t['kind'] == 'kw' and t['text'] == k

    def at_eof(self):
        return self.peek()['kind'] == 'eof'

    def advance(self):
        if self.pos < len(self.toks):
            self.pos += 1

    def accept_op(self, o):
        if self.at_op(o):
            self.advance()
            return True
        return False

    def expect_op(self, o):
        if not self.accept_op(o):
            raise ParseError('SP_ERR_UNEXPECTED_TOKEN')

    # --- operands, with postfix ---------------------------------------
    def operand(self, depth):
        if depth >= MAX_DEPTH:
            raise ParseError('SP_ERR_DEPTH_EXCEEDED')
        t = self.peek()

        if self.at_kw('not') or self.at_op('-') or self.at_op('!'):
            op = t['text']
            self.advance()
            inner = self.operand(depth + 1)
            # power binds tighter than unary, matching the JS chain
            while self.at_op('**'):
                self.advance()
                rhs = self.operand(depth + 1)
                inner = node('Binary', '**', [inner, rhs])
            return node('Unary', op, [inner])

        if t['kind'] in ('num', 'dec', 'str'):
            kind = {'num': 'Num', 'dec': 'DecLit', 'str': 'Str'}[t['kind']]
            self.advance()
            n = node(kind, t['text'])
        elif t['kind'] == 'kw' and t['text'] in ('true', 'false'):
            self.advance()
            n = node('Bool', t['text'])
        elif self.at_kw('nil'):
            self.advance()
            n = node('Nil', '')
        elif t['kind'] == 'ident':
            self.advance()
            n = node('Ident', t['text'])
        elif self.at_op('('):
            self.advance()
            n = self.expression(depth + 1)
            self.expect_op(')')
        elif self.at_op('['):
            self.advance()
            kids = []
            if not self.at_op(']'):
                while True:
                    kids.append(self.expression(depth + 1))
                    if not self.accept_op(','):
                        break
            self.expect_op(']')
            n = node('ListLit', '', kids)
        else:
            raise ParseError('SP_ERR_UNEXPECTED_TOKEN')

        steps = 0
        while steps < MAX_DEPTH:
            steps += 1
            if self.at_op('('):
                self.advance()
                args = []
                if not self.at_op(')'):
                    while True:
                        args.append(self.expression(depth + 1))
                        if not self.accept_op(','):
                            break
                self.expect_op(')')
                n = node('Call', '', [n] + args)
            elif self.at_op('['):
                self.advance()
                idx = []
                while True:
                    idx.append(self.expression(depth + 1))
                    if not self.accept_op(','):
                        break
                self.expect_op(']')
                n = node('Index', '', [n] + idx)
            elif self.at_op('.'):
                self.advance()
                if self.peek()['kind'] != 'ident':
                    raise ParseError('SP_ERR_UNEXPECTED_TOKEN')
                name = self.peek()['text']
                self.advance()
                n = node('Member', name, [n])
            else:
                break
        return n

    # --- shunting-yard -------------------------------------------------
    def expression(self, depth):
        if depth >= MAX_DEPTH:
            raise ParseError('SP_ERR_DEPTH_EXCEEDED')
        operands = [self.operand(depth)]
        ops = []

        def reduce_one(entry):
            if len(operands) < 2:
                raise ParseError('SP_ERR_UNEXPECTED_TOKEN')
            rhs = operands.pop()
            lhs = operands.pop()
            kind = 'Logical' if entry['op'] in ('and', 'or') else 'Binary'
            operands.append(node(kind, entry['op'], [lhs, rhs]))

        while True:
            t = self.peek()
            prec = precedence_of(t)
            if prec == 0 or t['nl']:
                break
            entry = {'op': t['text'], 'prec': prec}
            while ops and (ops[-1]['prec'] > prec
                           or (ops[-1]['prec'] == prec and entry['op'] != '**')):
                reduce_one(ops.pop())
            ops.append(entry)
            self.advance()
            operands.append(self.operand(depth + 1))

        while ops:
            reduce_one(ops.pop())
        if len(operands) != 1:
            raise ParseError('SP_ERR_UNEXPECTED_TOKEN')
        return operands[0]

    # --- statements ------------------------------------------------------
    def block(self, depth):
        self.expect_op('{')
        kids = []
        while not self.at_op('}') and not self.at_eof():
            kids.append(self.statement(depth + 1))
        self.expect_op('}')
        return node('Block', '', kids)

    def statement(self, depth):
        if depth >= MAX_DEPTH:
            raise ParseError('SP_ERR_DEPTH_EXCEEDED')
        t = self.peek()

        if t['kind'] == 'kw' and t['text'] in UNSUPPORTED_KW:
            raise ParseError('SP_ERR_UNSUPPORTED')

        if self.at_kw('let') or self.at_kw('var'):
            self.advance()
            if self.peek()['kind'] != 'ident':
                raise ParseError('SP_ERR_UNEXPECTED_TOKEN')
            name = self.peek()['text']
            self.advance()
            self.expect_op('=')
            return node('Declare', name, [self.expression(depth + 1)])

        if self.at_kw('if'):
            self.advance()
            test = self.expression(depth + 1)
            then = self.block(depth + 1)
            kids = [test, then]
            if self.at_kw('else'):
                self.advance()
                kids.append(self.statement(depth + 1) if self.at_kw('if')
                            else self.block(depth + 1))
            return node('If', '', kids)

        if self.at_kw('while'):
            self.advance()
            test = self.expression(depth + 1)
            return node('While', '', [test, self.block(depth + 1)])

        if self.at_kw('return'):
            self.advance()
            kids = []
            if not self.at_op('}') and not self.at_eof() and not self.peek()['nl']:
                kids.append(self.expression(depth + 1))
            return node('Return', '', kids)

        if self.at_kw('break'):
            self.advance()
            return node('Break', '')
        if self.at_kw('continue'):
            self.advance()
            return node('Continue', '')

        if self.at_op('{'):
            return self.block(depth)

        expr = self.expression(depth + 1)
        if self.at_op('='):
            self.advance()
            expr = node('Assign', '', [expr, self.expression(depth + 1)])
        return node('ExprStmt', '', [expr])


def c_parse(src):
    toks = c_tokenize(src)
    p = P(toks)
    kids = []
    while not p.at_eof():
        kids.append(p.statement(0))
    return node('Program', '', kids)


def js_parse(src):
    r = subprocess.run(['node', 'C:/Users/USER/AppData/Local/Temp/dump_ast.mjs', src],
                       capture_output=True, text=True, timeout=60)
    return json.loads(r.stdout.strip())


def shape(n):
    """Compare structure and operators, ignoring how each side wraps
    statements -- the C arena has no ExprStmt-vs-expression distinction
    worth diffing here, only the tree shape and the operator nesting."""
    if n is None:
        return None
    return (n['t'], n['x'], tuple(shape(c) for c in n['k']))


CASES = [
    'let x = 1 + 2 * 3',            # precedence
    'let x = 1 * 2 + 3',
    'let x = 1 - 2 - 3',            # left associative
    'let x = 2 ** 3 ** 2',          # RIGHT associative
    'let x = 0 - 2 ** 2',           # power binds tighter than unary
    'let x = a == b and c != d',
    'let x = a < b or c >= d',
    'let x = not a and b',
    'let x = (1 + 2) * 3',          # parens override
    'let x = f(1, 2)',
    'let x = a.b.c',
    'let x = xs[0]',
    'let x = f(a)(b)',              # chained call
    'let x = [1, 2, 3]',
    'var y = 1',
    'if a { } else { }',
    'while a < 10 { }',
    'return',
    'x = 5',
]

FAILURES = []
print('C parser logic vs the real JS parser, tree by tree')
print()

for src in CASES:
    want = js_parse(src)
    try:
        got = c_parse(src)
        err = None
    except (ParseError, LexError) as e:
        got, err = None, e.status

    label = src if len(src) <= 40 else src[:37] + '...'

    if isinstance(want, dict) and 'error' in want:
        ok = err is not None
    elif err is not None:
        ok = False
    else:
        ok = shape(got) == shape(want)

    print(f"  {'ok  ' if ok else 'FAIL'}  {label}")
    if not ok:
        FAILURES.append(src)
        print(f'          C  : {shape(got) if got else err}')
        print(f'          JS : {shape(want)}')

print()
if FAILURES:
    print(f'{len(FAILURES)} of {len(CASES)} diverge from the reference')
    raise SystemExit(1)
print(f'all {len(CASES)} produce a tree structurally identical to the JS parser')
