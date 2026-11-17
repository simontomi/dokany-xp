"""Moves C99-style late declarations to the start of their block (C89).

The WDK 7.1 C compiler (cl 15.00) only accepts declarations at the start of a
block. This tool reads the compiler's error log, and for every reported late
declaration `TYPE name = value;` it puts `TYPE name;` at the start of the
enclosing block and leaves `name = value;` where the declaration was.

Anything it cannot change safely (several declarators, an array or aggregate
initializer, a name that the block already uses before the declaration) is
listed as MANUAL and left alone.

Usage: python c89hoist.py <build .err file> [--apply]
Without --apply it only prints what it would do.
"""

import re
import sys
from collections import defaultdict

ERR_RE = re.compile(
    r"^(?:\d+>)?(?P<path>[A-Za-z]:\\.*?)\((?P<line>\d+)\) : error "
    r"(?P<code>C\d+): (?P<msg>.*)$")
DECL_CODES = {"C2275", "C2143"}  # C2146 also comes from unknown types
IDENT = r"[A-Za-z_]\w*"
TYPE_RE = re.compile(
    r"^(?P<const>const\s+)?(?P<type>(?:(?:struct|enum|unsigned|signed)\s+)?"
    + IDENT + r"(?:\s+const)?(?:\s*\*\s*(?:const\s*)?)*?)\s*(?P<name>" + IDENT
    + r")\s*(?P<array>\[[^\]]*\])?\s*$", re.S)
NOT_TYPES = {"return", "goto", "else", "case", "if", "while", "do", "switch",
             "break", "continue", "sizeof", "__leave", "__try", "__finally"}


def mask_code(text):
    """The text with comments, strings and char literals blanked out."""
    out = list(text)
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            for k in range(i, j):
                out[k] = " "
            i = j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            for k in range(i, j):
                if out[k] != "\n":
                    out[k] = " "
            i = j
        elif c in "\"'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            for k in range(i + 1, min(j, n)):
                if out[k] != "\n":
                    out[k] = " "
            i = j + 1
        else:
            i += 1
    return "".join(out)


def top_level_index(s, chars):
    """Index of the first char of `chars` at paren/bracket/brace depth 0."""
    depth = 0
    for i, c in enumerate(s):
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
        elif depth == 0 and c in chars:
            if c == "=" and (s[i + 1:i + 2] == "=" or
                             (i > 0 and s[i - 1] in "=!<>")):
                continue
            return i
    return -1


def plan_file(path, lines_wanted):
    with open(path, "r", encoding="utf-8", newline="") as f:
        text = f.read()
    lines = text.splitlines(keepends=True)
    masked = mask_code(text)
    mlines = masked.splitlines(keepends=True)
    offsets = []
    pos = 0
    for l in lines:
        offsets.append(pos)
        pos += len(l)
    nl = "\r\n" if "\r\n" in text else "\n"

    edits = []      # (start_idx, end_idx, new_lines)
    inserts = defaultdict(list)  # after_idx -> [lines]
    report = []
    for line_no in sorted(set(lines_wanted)):
        idx = line_no - 1
        m = mlines[idx]
        stripped = m.strip()
        indent = re.match(r"\s*", lines[idx]).group(0)
        first_word = re.match(IDENT, stripped)
        if not first_word or first_word.group(0) in NOT_TYPES:
            report.append(("SKIP", line_no, lines[idx].strip()))
            continue
        # The whole statement: up to the first ';' at depth 0.
        end = idx
        stmt = m
        while top_level_index(stmt, ";") < 0 and end + 1 < len(mlines):
            end += 1
            stmt += mlines[end]
        semi = top_level_index(stmt, ";")
        if semi < 0:
            report.append(("MANUAL", line_no, "no end of statement"))
            continue
        for_m = re.match(r"\s*for\s*\(\s*", stmt)
        if for_m:
            body = stmt[for_m.end():]
            eq = top_level_index(body, "=;")
            lhs = body[:eq]
            tm = TYPE_RE.match(lhs.strip())
            if not tm or eq < 0 or body[eq] != "=":
                report.append(("MANUAL", line_no, lines[idx].strip()))
                continue
            decl_type = tm.group("type")
            name = tm.group("name")
            orig = lines[idx]
            new_first = re.sub(r"for\s*\(\s*" + re.escape(lhs.strip()),
                               "for (" + name, orig, count=1)
            if new_first == orig:
                report.append(("MANUAL", line_no, "for: cannot rewrite"))
                continue
            stmt_start_off = offsets[idx] + len(re.match(r"\s*", lines[idx]).group(0))
            new_lines = [new_first]
            end = idx
            name_decl = name
        else:
            decl = stmt[:semi]
            if top_level_index(decl, ",") >= 0:
                report.append(("MANUAL", line_no, "several declarators"))
                continue
            eq = top_level_index(decl, "=")
            lhs = decl if eq < 0 else decl[:eq]
            rhs = None if eq < 0 else decl[eq + 1:]
            tm = TYPE_RE.match(lhs.strip())
            if not tm:
                report.append(("MANUAL", line_no, lines[idx].strip()))
                continue
            name = tm.group("name")
            decl_type = tm.group("type")
            if tm.group("array") and rhs is not None:
                report.append(("MANUAL", line_no, "array initializer"))
                continue
            if rhs is not None and rhs.strip().startswith("{"):
                report.append(("MANUAL", line_no, "aggregate initializer"))
                continue
            if tm.group("const") and "*" not in decl_type:
                pass  # a const object: the hoisted declaration drops const
            elif tm.group("const"):
                decl_type = "const " + decl_type
            if re.search(r"\*\s*const", decl_type):
                report.append(("MANUAL", line_no, "const pointer"))
                continue
            stmt_start_off = offsets[idx] + len(indent)
            if rhs is None:
                new_lines = []
            else:
                # Replace "TYPE name =" by "name =" in the original text.
                orig_stmt = "".join(lines[idx:end + 1])
                eq_off = len(indent) + (len(m) - len(m.lstrip())) * 0
                eq_pos = orig_stmt.index("=", len(indent) + len(lhs.strip()) - 1)
                new_stmt = indent + name + " " + orig_stmt[eq_pos:]
                new_lines = new_stmt.splitlines(keepends=True)
            if tm.group("array"):
                name_decl = name + tm.group("array").strip()
            else:
                name_decl = name
        # The enclosing block: the first unmatched '{' before the statement.
        depth = 0
        p = stmt_start_off - 1
        while p >= 0:
            c = masked[p]
            if c == "}":
                depth += 1
            elif c == "{":
                if depth == 0:
                    break
                depth -= 1
            p -= 1
        if p < 0:
            report.append(("MANUAL", line_no, "no enclosing block"))
            continue
        brace_line = masked.count("\n", 0, p)
        after_brace = mlines[brace_line][p - offsets[brace_line] + 1:]
        if after_brace.strip():
            report.append(("MANUAL", line_no, "code after the block's '{'"))
            continue
        region = masked[p + 1:stmt_start_off]
        if re.search(r"\b" + re.escape(name) + r"\b", region):
            report.append(("MANUAL", line_no, "'%s' used earlier in the block" % name))
            continue
        base = decl_type.strip()
        stars = ""
        while base.endswith("*"):
            stars += "*"
            base = base[:-1].rstrip()
        hoisted = "%s%s %s%s;%s" % (indent, base, stars, name_decl, nl)
        inserts[brace_line].append(hoisted)
        edits.append((idx, end, new_lines))
        report.append(("OK", line_no, "%s %s" % (decl_type.strip(), name)))

    # Apply bottom-up: replacements and insertions by line index.
    ops = [(s, "R", e, nlines) for (s, e, nlines) in edits]
    ops += [(k + 1, "I", None, ins) for k, ins in inserts.items()]
    ops.sort(key=lambda o: (o[0], o[1] == "R"), reverse=True)
    new = list(lines)
    for start, kind, end, payload in ops:
        if kind == "R":
            new[start:end + 1] = payload
        else:
            new[start:start] = payload
    return "".join(new), report


def main():
    err_path = sys.argv[1]
    apply = "--apply" in sys.argv
    wanted = defaultdict(list)
    with open(err_path, "r", encoding="mbcs", errors="replace") as f:
        for raw in f:
            m = ERR_RE.match(raw.strip())
            if not m or m.group("code") not in DECL_CODES:
                continue
            msg = m.group("msg")
            if m.group("code") == "C2143" and "before 'type'" not in msg \
                    and "before '<cv-qualifer>'" not in msg:
                continue
            if m.group("code") == "C2146" and "before identifier" not in msg:
                continue
            wanted[m.group("path").lower()].append(int(m.group("line")))
    for path, lines_wanted in sorted(wanted.items()):
        new_text, report = plan_file(path, lines_wanted)
        for kind, line_no, what in report:
            print("%-6s %s(%d): %s" % (kind, path, line_no, what))
        if apply:
            with open(path, "w", encoding="utf-8", newline="") as f:
                f.write(new_text)


if __name__ == "__main__":
    main()
