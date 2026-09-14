"""Differential test: the C lexer's logic against the real JS lexer.

smarsh_lexer.c cannot be compiled here, so its logic is transliterated
below -- same branch order, same tables, same escape handling -- and the
token stream it produces is compared against ../../smarsh/src/lexer.js,
which is covered by 985 tests and a 3,065-program differential oracle.

This verifies the ALGORITHM, not the C. Compilation, undefined behaviour
and integer promotion remain unchecked until a compiler exists. But a
token-stream divergence from the tested reference is the failure mode
that actually matters for a lexer port, and this catches it.
"""

import json
import subprocess

SL_MAX_TOKEN_TEXT = 256

KEYWORDS = {
    "let", "var", "fn", "return", "if", "else", "while", "for", "in",
    "true", "false", "nil", "and", "or", "not", "break", "continue",
    "maybe", "choose", "fork", "tensor", "needs", "requires", "ensures",
    "attempt", "rescue",
    "agent", "on", "spawn", "redefine", "import", "as",
    "invariant", "variant", "using",
    "match", "when",
}

PUNCT = [
    "**", "==", "!=", "<=", ">=", "=>", "&&", "||",
    "+", "-", "*", "/", "%", "@", "<", ">", "=",
    "(", ")", "{", "}", "[", "]", ",", ";", ".", ":", "!",
]


class LexError(Exception):
    def __init__(self, status):
        self.status = status


def is_digit(c):
    return "0" <= c <= "9"


def is_ident_start(c):
    return c.isascii() and (c.isalpha() or c == "_")


def is_ident_part(c):
    return is_ident_start(c) or is_digit(c)


def c_tokenize(source):
    """Transliteration of sl_tokenize(), branch for branch."""
    out = []
    i = 0
    n = len(source)
    line = 1
    pending_newline = False

    while i < n:
        c = source[i]

        if c == "\n":
            line += 1
            pending_newline = True
            i += 1
            continue
        if c in " \t\r":
            i += 1
            continue
        if c == "/" and i + 1 < n and source[i + 1] == "/":
            while i < n and source[i] != "\n":
                i += 1
            continue

        if is_digit(c):
            start = i
            while i < n and is_digit(source[i]):
                i += 1
            if i < n and source[i] == "." and i + 1 < n and is_digit(source[i + 1]):
                i += 1
                while i < n and is_digit(source[i]):
                    i += 1
            is_dec = i < n and source[i] == "d"
            text = source[start:i]
            if len(text) >= SL_MAX_TOKEN_TEXT:
                raise LexError("SL_ERR_TOKEN_TOO_LONG")
            if is_dec:
                i += 1
            out.append({"kind": "dec" if is_dec else "num", "text": text,
                        "nl": pending_newline})
            pending_newline = False
            continue

        if is_ident_start(c):
            start = i
            while i < n and is_ident_part(source[i]):
                i += 1
            text = source[start:i]
            if len(text) >= SL_MAX_TOKEN_TEXT:
                raise LexError("SL_ERR_TOKEN_TOO_LONG")
            out.append({"kind": "kw" if text in KEYWORDS else "ident",
                        "text": text, "nl": pending_newline})
            pending_newline = False
            continue

        if c == '"':
            i += 1
            wrote = []
            while i < n and source[i] != '"':
                ch = source[i]
                if ch == "$" and i + 1 < n and source[i + 1] == "{":
                    raise LexError("SL_ERR_UNSUPPORTED")
                if ch == "\\" and i + 1 < n:
                    esc = source[i + 1]
                    i += 2
                    ch = {"n": "\n", "t": "\t", "r": "\r"}.get(esc, esc)
                else:
                    if ch == "\n":
                        line += 1
                    i += 1
                if len(wrote) + 1 >= SL_MAX_TOKEN_TEXT:
                    raise LexError("SL_ERR_TOKEN_TOO_LONG")
                wrote.append(ch)
            if i >= n:
                raise LexError("SL_ERR_UNTERMINATED_STRING")
            i += 1
            out.append({"kind": "str", "text": "".join(wrote), "nl": pending_newline})
            pending_newline = False
            continue

        matched = False
        for op in PUNCT:
            if source.startswith(op, i):
                out.append({"kind": "op", "text": op, "nl": pending_newline})
                pending_newline = False
                i += len(op)
                matched = True
                break
        if matched:
            continue

        raise LexError("SL_ERR_UNEXPECTED_CHAR")

    out.append({"kind": "eof", "text": "", "nl": pending_newline})
    return out


def js_tokenize(source):
    r = subprocess.run(
        ["node", "C:/Users/USER/AppData/Local/Temp/dump_tokens.mjs", source],
        capture_output=True, text=True, timeout=60,
    )
    return json.loads(r.stdout.strip())


CASES = [
    "let x = 42",
    "var y = 3.5",
    "let m = 1.50d",
    "fn f(a) needs fs { return a ** 2 }",
    "if a == b { } else { }",
    'let s = "hello"',
    'let s = "tab\\there"',
    "while i < 10 { i = i + 1 }",
    "let xs = [1, 2, 3]",
    "record Point(x, y)",          # contextual: must lex as ident, not kw
    "let region = 5",              # contextual used as a plain name
    "attempt { f() } rescue e { }",
    "a.b.c",
    "x <= y and y >= z or not w",
    "maybe 0.5 { }",
    "match v { when 1 => 2 }",
    "// a comment\nlet z = 1",
    "let a = 1\nlet b = 2",        # nl_before must be set on the second let
    "fn g() requires x > 0 ensures result != nil { }",
    "agent A { on msg(m) { } }",
]

FAILURES = []
print("C lexer logic vs the real JS lexer, token stream by token stream")
print()

for src in CASES:
    want = js_tokenize(src)
    try:
        got = c_tokenize(src)
        err = None
    except LexError as e:
        got, err = None, e.status

    label = src.replace("\n", "\\n")
    if len(label) > 46:
        label = label[:43] + "..."

    if isinstance(want, dict) and "error" in want:
        ok = err is not None
        detail = f"JS errored, C {'errored too' if ok else 'did not'}"
    elif err is not None:
        ok = False
        detail = f"C raised {err}, JS produced {len(want)} tokens"
    else:
        ok = got == want
        if not ok:
            for k, (g, w) in enumerate(zip(got, want)):
                if g != w:
                    detail = f"token {k}: C={g} JS={w}"
                    break
            else:
                detail = f"length: C={len(got)} JS={len(want)}"
        else:
            detail = ""

    print(f"  {'ok  ' if ok else 'FAIL'}  {label}" + ("" if ok else f"\n          {detail}"))
    if not ok:
        FAILURES.append(src)

print()
if FAILURES:
    print(f"{len(FAILURES)} of {len(CASES)} cases diverge from the tested reference")
    raise SystemExit(1)
print(f"all {len(CASES)} cases produce a token stream identical to the JS lexer")
