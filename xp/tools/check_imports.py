"""Checks that every function a built binary imports is exported by the
system files of a target Windows (for example extracted from its install
media).

Usage:
  python check_imports.py <system files folder> <binary> [<binary> ...]

The folder holds the target's ntoskrnl.exe, hal.dll, kernel32.dll, ...
(any letter case). Prints every missing import and ends with
"IMPORTS OK" or "IMPORTS MISSING <n>"; the exit code is 0 only when nothing
is missing.
"""
import os
import struct
import subprocess
import sys

LINK = r"C:\WinDDK\7600.16385.1\bin\x86\x86\link.exe"


def rva_to_offset(sections, rva):
    for va, vsize, raw, rsize in sections:
        if va <= rva < va + max(vsize, rsize):
            return rva - va + raw
    raise ValueError("RVA 0x%x outside the sections" % rva)


def exports(path):
    data = open(path, "rb").read()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    nsec, = struct.unpack_from("<H", data, pe + 6)
    optsize, = struct.unpack_from("<H", data, pe + 20)
    opt = pe + 24
    magic, = struct.unpack_from("<H", data, opt)
    dd = opt + (112 if magic == 0x20B else 96)
    exp_rva, = struct.unpack_from("<I", data, dd)
    sections = []
    for i in range(nsec):
        s = opt + optsize + 40 * i
        vsize, va, rsize, raw = struct.unpack_from("<IIII", data, s + 8)
        sections.append((va, vsize, raw, rsize))
    names = set()
    if not exp_rva:
        return names
    e = rva_to_offset(sections, exp_rva)
    count, = struct.unpack_from("<I", data, e + 24)
    names_rva, = struct.unpack_from("<I", data, e + 32)
    n = rva_to_offset(sections, names_rva)
    for i in range(count):
        p = rva_to_offset(sections, struct.unpack_from("<I", data, n + 4 * i)[0])
        end = data.index(b"\0", p)
        names.add(data[p:end].decode("ascii"))
    return names


def imports(binary):
    out = subprocess.run([LINK, "/dump", "/imports", binary], capture_output=True,
                         text=True, errors="replace").stdout
    result = []
    module = None
    for line in out.splitlines():
        if "Summary" in line:
            break
        s = line.strip()
        if s.lower().endswith((".dll", ".exe", ".sys")) and " " not in s:
            module = s.lower()
            continue
        parts = s.split()
        if module and len(parts) == 2:
            try:
                int(parts[0], 16)
            except ValueError:
                continue
            result.append((module, parts[1]))
    return result


def main():
    folder = sys.argv[1]
    files = {f.lower(): os.path.join(folder, f) for f in os.listdir(folder)}
    cache = {}
    missing = 0
    for binary in sys.argv[2:]:
        imps = imports(binary)
        for module, name in imps:
            if module not in cache:
                cache[module] = exports(files[module]) if module in files else None
                if cache[module] is None:
                    print("NO MODULE %s (needed by %s)" % (module, binary))
            if cache[module] is None or name not in cache[module]:
                print("MISSING %s: %s!%s" % (os.path.basename(binary), module, name))
                missing += 1
        print("checked %s: %d imports" % (os.path.basename(binary), len(imps)))
    print("IMPORTS OK" if missing == 0 else "IMPORTS MISSING %d" % missing)
    return 0 if missing == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
