#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Koen De Vleeschauwer
"""
cstrings.py: the characters of the string literals in C sources, as a UTF-8
text file: the input of fontconv.py.

Only what is between double quotes counts. Comments, character constants
and the file name of #include "..." are skipped, so a note in a comment adds
no glyphs to the font. Escapes are decoded; control characters are left
out. A \\x or octal escape counts only when it is printable ASCII: a byte of
a UTF-8 sequence is not a character by itself.

Output: each character once, in code point order, 64 to a line.

usage: cstrings.py [-v] [-o chars.txt] file.c ...
"""

import argparse
import re
import sys

ESCAPES = {"a": 7, "b": 8, "f": 12, "n": 10, "r": 13, "t": 9, "v": 11,
           "\\": 92, "'": 39, '"': 34, "?": 63}
INCLUDE = re.compile(r'#[ \t]*include[ \t]*"[^"\n]*"')


def string(text, i):
    """the string literal that starts with the quote at text[i]: its code
    points and the index after the closing quote"""
    out = []
    i += 1
    n = len(text)
    while i < n and text[i] != '"':
        c = text[i]
        if c == "\n":                       # unterminated: stop at the line end
            return out, i
        if c != "\\" or i + 1 >= n:
            out.append(ord(c))
            i += 1
            continue
        e = text[i + 1]
        i += 2
        if e == "\n":                       # line continuation
            continue
        if e in ESCAPES:
            out.append(ESCAPES[e])
        elif e == "x":
            m = re.match(r"[0-9a-fA-F]+", text[i:])
            if m:
                out.append(byte(int(m.group(), 16)))
                i += m.end()
        elif e in "01234567":
            m = re.match(r"[0-7]{0,2}", text[i:])
            out.append(byte(int(e + m.group(), 8)))
            i += m.end()
        elif e in "uU":
            k = 4 if e == "u" else 8
            m = re.match("[0-9a-fA-F]{%d}" % k, text[i:])
            if m:
                out.append(int(m.group(), 16))
                i += k
        else:
            out.append(ord(e))
    return out, i + 1


def byte(v):
    """a \\x or octal escape: a character only when printable ASCII"""
    return v if 0x20 <= v < 0x7F else 0


def literals(text):
    """the code points of all string literals in C source text"""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == "/" and text.startswith("/*", i):
            j = text.find("*/", i + 2)
            i = n if j < 0 else j + 2
        elif c == "/" and text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif c == "'":                      # character constant
            i += 1
            while i < n and text[i] not in "'\n":
                i += 2 if text[i] == "\\" else 1
            i += 1
        elif c == "#":
            m = INCLUDE.match(text, i)
            i = m.end() if m else i + 1
        elif c == '"':
            s, i = string(text, i)
            out.extend(s)
        else:
            i += 1
    return out


def printable(cp):
    return cp >= 0x20 and cp != 0x7F and not 0xD800 <= cp < 0xE000


def main():
    ap = argparse.ArgumentParser(description="characters of the C string literals, as UTF-8 text")
    ap.add_argument("-o", metavar="FILE", help="output file (default: standard output)")
    ap.add_argument("-v", action="store_true", help="per file: the number of characters, to standard error")
    ap.add_argument("files", nargs="+", metavar="file.c")
    a = ap.parse_args()

    chars = set()
    for name in a.files:
        with open(name, encoding="utf-8") as f:
            found = {cp for cp in literals(f.read()) if printable(cp)}
        if a.v:
            print(f"{name}: {len(found)} characters, {sum(cp >= 0x80 for cp in found)} not ASCII",
                  file=sys.stderr)
        chars |= found

    s = "".join(chr(cp) for cp in sorted(chars))
    lines = "".join(s[k:k + 64] + "\n" for k in range(0, len(s), 64))
    if a.v:
        print(f"total: {len(chars)} characters, {sum(cp >= 0x80 for cp in chars)} not ASCII",
              file=sys.stderr)
    if a.o:
        with open(a.o, "w", encoding="utf-8") as f:
            f.write(lines)
    else:
        sys.stdout.write(lines)


if __name__ == "__main__":
    main()
