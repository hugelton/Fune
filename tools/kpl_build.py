#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Build a plugin for the FM-1: one C file (sdk/kreisel_sdk.h) -> a .kpl image file (src/kreisel_plugin_image.h).

    python3 tools/kpl_build.py plugins/beep/beep.c [-o build/fm1/plugins/beep.kpl]
    python3 tools/kpl_build.py --info build/fm1/plugins/beep.kpl        # what a .kpl holds (no toolchain)

The FM-1's compiler (the JieLi toolchain, as platforms/fm1hw/build.sh: JIELI_TOOLCHAIN, in Docker unless on Linux x86-64;
JIELI_DOCKER=0/1 forces it) compiles it with the firmware's settings (platforms/targets/fm1.toml) and links it three times: for
address 0 (code at 0x40, after the header; RAM at 0), with the code moved, with the RAM moved. The 32-bit fields that differ by
exactly the move are its relocations (any instruction or data that holds an address); anything else that differs is refused.
The file is the first link with the header and the relocations: the sender (platforms/fm1hw/fm1_plugin.py) links it for where
the FM-1 puts it.

Checked here: the record kreisel_plugin() returns (abi, size, version 1..255, kind, id), no section the image cannot hold, no
floating point unless the record says K_NEED_FLOAT, and the CPU erratum (tools/fm1_mul_scan.py, as for the firmware)."""
import argparse
import os
import platform
import re
import shutil
import struct
import subprocess
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MAGIC = 0x4D49504B          # "KPIM"
HDR = 64
FORMAT = 1
DELTA = 0x12345600          # the move: a multiple of 256 (alignments keep), its byte 1 never 0 (a moved field's byte 1 always differs)
RELOC_RAM = 0x80000000
CHECK_BASE, CHECK_RAM = 0x02093000, 0x01C08000   # the FM-1's first image (flash 0x097000 in the XIP window), its RAM pool
REGION = 0x49000            # the FM-1's room for images (0x097000 .. 0x0DFFFF)
KINDS = {0: "synth", 1: "drum", 2: "effect"}
FLOAT_HELPERS = re.compile(r"^__(add|sub|mul|div|neg|cmp|eq|ne|lt|le|gt|ge|un|fix|fixuns|float|floatun|extend|trunc)"
                           r"[sd]f\d|^__(aeabi_[fd]|.*[sd]f2)")
LINK_LD = """MEMORY
{{
    IMG (rx) : ORIGIN = {text:#x}, LENGTH = {room:#x}
    RAM (rw) : ORIGIN = {ram:#x}, LENGTH = 0x10000
}}
SECTIONS
{{
    .text : {{ KEEP(*(.text.kreisel_plugin)) *(.text*) *(.rodata*) . = ALIGN(4); _k_etext = .; }} > IMG
    .data : {{ . = ALIGN(4); _k_data = .; *(.data*) *(.sdata*) . = ALIGN(4); _k_edata = .; }} > RAM AT > IMG
    _k_data_load = LOADADDR(.data);
    .bss (NOLOAD) : {{ _k_bss = .; *(.bss*) *(.sbss*) *(COMMON) . = ALIGN(4); _k_ebss = .; }} > RAM
    /DISCARD/ : {{ *(.comment) *(.note*) *(.eh_frame*) }}
}}
"""


def fail(msg):
    sys.exit(f"kpl_build: {msg}")


def api_version():
    """K_ENGINE_API_VERSION, read from the headers the plugin is built against"""
    abi = re.search(r"#define K_PLUGIN_ABI (\d+)", (ROOT / "src/kreisel_plugin.h").read_text())
    minor = re.search(r"#define K_ENGINE_API_MINOR (\d+)", (ROOT / "src/kreisel_engine_api.h").read_text())
    if not abi or not minor:
        fail("K_PLUGIN_ABI / K_ENGINE_API_MINOR not found in src/")
    return int(abi.group(1)) << 8 | int(minor.group(1))


def hcrc(h):
    """the header's own CRC: bytes 4 .. 59 with flags (byte 7) as 0xFF"""
    b = bytearray(h[:60])
    b[7] = 0xFF
    return zlib.crc32(bytes(b[4:60]))


HDR_FMT = "<IHBBHBBIIIIIIIIIIIII"   # KImageHeader, field by field
HDR_KEYS = ("magic hdr_size format flags api version kind uid base size record data_off data_size bss_size ram nreloc crc "
            "r0 r1 hcrc").split()


def header(**f):
    h = bytearray(struct.pack(HDR_FMT, MAGIC, HDR, FORMAT, 0xFF, f["api"], f["version"], f["kind"], f["uid"], 0,
                              f["size"], f["record"], f["data_off"], f["data_size"], f["bss_size"], 0, f["nreloc"], f["crc"],
                              0, 0, 0))
    assert len(h) == HDR
    struct.pack_into("<I", h, 60, hcrc(h))
    return bytes(h)


def parse(data):
    """a .kpl's header as a dict, or ValueError"""
    if len(data) < HDR:
        raise ValueError("shorter than a header")
    h = dict(zip(HDR_KEYS, struct.unpack_from(HDR_FMT, data)))
    if h["magic"] != MAGIC or h["hdr_size"] != HDR or h["format"] != FORMAT:
        raise ValueError("not a plugin image (KPIM, format 1)")
    if h["hcrc"] != hcrc(data):
        raise ValueError("its header is damaged (CRC)")
    if h["size"] > len(data) or len(data) != h["size"] + 4 * h["nreloc"]:
        raise ValueError("cut short, or not a file as built (its relocations)")
    if zlib.crc32(data[HDR:h["size"]]) != h["crc"]:
        raise ValueError("damaged (CRC)")
    return h


def relocate(image, relocs, base, ram):
    """relocs {offset: 0 | RELOC_RAM} applied in place for base and ram (k_image_link does the same on a device's side)"""
    for o, kind in relocs.items():
        v = struct.unpack_from("<I", image, o)[0] + (ram if kind else base)
        struct.pack_into("<I", image, o, v & 0xFFFFFFFF)


def uid_str(u):
    return "".join(chr((u >> s) & 255) for s in (24, 16, 8, 0))


def info(path):
    data = Path(path).read_bytes()
    try:
        h = parse(data)
    except ValueError as e:
        fail(f"{path}: {e}")
    name = plugin_name(data, h)
    print(f"{path}: {name} '{uid_str(h['uid'])}' v{h['version']} {KINDS.get(h['kind'], h['kind'])}, interface "
          f"{h['api'] >> 8}.{h['api'] & 255}, {h['size']} bytes ({(h['size'] + 4095) // 4096} sectors), RAM "
          f"{h['data_size']} + {h['bss_size']} bytes, {h['nreloc']} relocations"
          + (", linked" if h["base"] else ""))


def plugin_name(body, h):
    """KEngine.name of the record, read from the image linked at 0 (32-bit pointers: KPlugin.desc at 12, KEngine.name at 0)"""
    try:
        desc = struct.unpack_from("<I", body, h["record"] + 12)[0]
        p = struct.unpack_from("<I", body, desc)[0]
        return body[p:body.index(b"\0", p)].decode("ascii")
    except (struct.error, ValueError, UnicodeDecodeError):
        return "?"


class Toolchain:
    def __init__(self, work):
        self.tc = Path(os.environ.get("JIELI_TOOLCHAIN", Path.home() / ".jieli/toolchain"))
        if not (self.tc / "pi32v2/bin/clang").exists():
            fail(f"no JieLi toolchain in {self.tc} (JIELI_TOOLCHAIN)")
        native = platform.system() == "Linux" and platform.machine() == "x86_64"
        self.docker = os.environ.get("JIELI_DOCKER", "0" if native else "1") == "1"
        self.work = work
        if self.docker and not shutil.which("docker"):
            fail("Docker is not there (colima start)")

    def path(self, p):
        """a host path as the toolchain sees it"""
        p = Path(p).resolve()
        if not self.docker:
            return str(p)
        for host, cont in ((ROOT, "/work"), (self.src, "/src")):
            try:
                return f"{cont}/{p.relative_to(host)}"
            except ValueError:
                pass
        fail(f"{p} is outside the repository and the plugin's folder")

    def run(self, tool, *args, out=None):
        if self.docker:
            cmd = ["docker", "run", "--rm", "--platform", "linux/amd64", "-v", f"{ROOT}:/work", "-v", f"{self.src}:/src:ro",
                   "-v", f"{self.tc}:/opt/jieli:ro", "-w", "/work", os.environ.get("JIELI_DOCKER_IMAGE", "debian:bookworm-slim"),
                   f"/opt/jieli/{tool}", *args]
        else:
            cmd = [str(self.tc / tool), *args]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode:
            fail(f"{tool} failed:\n{r.stdout}{r.stderr}")
        if out:
            Path(out).write_text(r.stdout)
        return r.stdout

    def inc(self):
        return "/opt/jieli/pi32v2" if self.docker else str(self.tc / "pi32v2")


def defines():
    r = subprocess.run([sys.executable, "-I", str(ROOT / "tools/kbuild.py"), "fm1", "defines"], capture_output=True, text=True,
                       check=True)
    return r.stdout.split()


def symbols(sym_text):
    out = {}
    for line in sym_text.splitlines():
        p = line.split()
        if len(p) >= 4 and re.fullmatch(r"[0-9a-f]{8}", p[0]):
            out[p[-1]] = (int(p[0], 16), int(p[-2], 16) if re.fullmatch(r"[0-9a-f]{8}", p[-2]) else 0, line)
    return out


def build(src, out):
    src = Path(src).resolve()
    if not src.exists():
        fail(f"{src}: no such file")
    name = src.stem
    work = ROOT / "build/fm1/plugins" / f"{name}.work"
    work.mkdir(parents=True, exist_ok=True)
    tc = Toolchain(work)
    tc.src = src.parent
    obj = work / f"{name}.o"
    tc.run("pi32v2/bin/clang", "-target", "pi32v2", "-Os", "-ffunction-sections", "-fdata-sections", "-fno-builtin", "-Wall",
           "-Wno-unused-function", "-isystem", tc.inc() + "/include", "-I" + tc.path(ROOT / "sdk"), "-I" + tc.path(ROOT / "src"),
           *defines(), "-c", tc.path(src), "-o", tc.path(obj))
    room = REGION - HDR
    # (code's address, RAM's address): at 0, the code moved, the RAM moved; and where the FM-1's first image goes, to check the
    # relocations against (the file linked there must be what the linker gives there)
    links = {"a": (HDR, 0), "t": (HDR + DELTA, 0), "r": (HDR, DELTA), "x": (CHECK_BASE + HDR, CHECK_RAM)}
    res = {}
    for k, (text, ram) in links.items():
        ld, elf = work / f"link_{k}.ld", work / f"link_{k}.elf"
        ld.write_text(LINK_LD.format(text=text, room=room, ram=ram))
        tc.run("pi32v2/bin/ld", "--gc-sections", "--no-check-sections", "-e", "kreisel_plugin", "-T", tc.path(ld), tc.path(obj),
               "-L" + tc.inc() + "/lib", "--start-group", "-lc", "-lcompiler-rt", "--end-group", "-Map", tc.path(work / f"link_{k}.map"),
               "-o", tc.path(elf))
        for sec in ("text", "data"):
            tc.run("common/bin/objcopy", "-O", "binary", "-j", f".{sec}", tc.path(elf), tc.path(work / f"{k}_{sec}.bin"))
        sects = tc.run("common/bin/objdump", "-h", tc.path(elf))
        res[k] = ((work / f"{k}_text.bin").read_bytes(), (work / f"{k}_data.bin").read_bytes(), sects)
    sym_text = tc.run("common/bin/objdump", "-t", tc.path(work / "link_a.elf"), out=work / f"{name}.sym")
    tc.run("common/bin/objdump", "-d", tc.path(work / "link_a.elf"), out=work / f"{name}.dis")
    syms = symbols(sym_text)
    text, data, sects = res["a"]

    # only what an image holds: code and constants, .data, .bss
    for line in sects.splitlines():
        m = re.match(r"\s*\d+\s+(\S+)\s+([0-9a-f]+)\s", line)
        if m and int(m.group(2), 16) and m.group(1) not in (".text", ".data", ".bss") and not m.group(1).startswith(
                (".debug", ".symtab", ".strtab", ".shstrtab", ".rela", ".comment")):
            fail(f"a section an image cannot hold: {m.group(1)}")
    if len(text) % 4:
        text += b"\0" * (-len(text) % 4)
    for k in ("t", "r"):
        if len(res[k][0]) + (-len(res[k][0]) % 4) != len(text) or len(res[k][1]) != len(data):
            fail("the links came out of different sizes (an alignment past 256?)")
    if syms.get("_k_data_load", (0,))[0] != HDR + len(text) and data:
        fail(".data's values are not right after the code")
    bss = syms["_k_ebss"][0] - syms["_k_bss"][0] if "_k_bss" in syms else 0
    body_a = text + data

    def pad(t, d):
        return t + b"\0" * (-len(t) % 4) + d

    relocs = {}
    for k, kind in (("t", 0), ("r", RELOC_RAM)):
        body_x = pad(res[k][0], res[k][1])
        i = 0
        while i < len(body_a):
            if body_a[i] == body_x[i]:
                i += 1
                continue
            f = i - 1   # (the move's byte 0 is 0: a moved field's first byte is the same, its second differs)
            if f < 0 or f + 4 > len(body_a) or \
                    (struct.unpack_from("<I", body_x, f)[0] - struct.unpack_from("<I", body_a, f)[0]) & 0xFFFFFFFF != DELTA:
                fail(f"a difference at image offset {HDR + i:#x} that is not a moved 32-bit address")
            if HDR + f in relocs:
                fail(f"a field at {HDR + f:#x} moves with both the code and the RAM")
            relocs[HDR + f] = kind
            i = f + 4

    # the record: the one address kreisel_plugin() returns
    if "kreisel_plugin" not in syms:
        fail("no kreisel_plugin() (sdk/kreisel_sdk.h: KREISEL_PLUGIN_EXPORT const KPlugin *kreisel_plugin(void))")
    fa, fs, _ = syms["kreisel_plugin"]
    inside = [o for o in relocs if fa <= o and o + 4 <= fa + max(fs, 1) and relocs[o] == 0]
    if len(inside) != 1:
        fail("kreisel_plugin() must only return its record's address")
    image = bytearray(b"\0" * HDR + body_a)
    record = struct.unpack_from("<I", image, inside[0])[0]
    if record < HDR or record + 40 > len(image) or record % 4:
        fail("the record is not in the image")
    size, abi, version, kind = struct.unpack_from("<IHHB", image, record)
    desc, needs = struct.unpack_from("<I", image, record + 12)[0], struct.unpack_from("<H", image, record + 30)[0]
    if abi != api_version() >> 8 or size < 20:
        fail(f"the record's abi {abi} / size {size} are not this interface's")
    if not 1 <= version <= 255:
        fail(f"version {version}: a plugin's version is 1 .. 255 (one byte in a song)")
    if kind not in KINDS:
        fail(f"kind {kind}")
    if not HDR <= desc <= len(image) - 8:
        fail("the descriptor is not in the image")
    uid = struct.unpack_from("<I", image, desc + 4)[0]

    # floating point (the FM-1 has no FPU: the record must say so, and the FM-1 refuses it)
    floats = sorted(n for n in syms if FLOAT_HELPERS.match(n))
    if floats and not needs & 1:
        fail(f"uses floating point ({', '.join(floats[:4])}) but its record does not say K_NEED_FLOAT")
    # the CPU erratum
    r = subprocess.run([sys.executable, "-I", str(ROOT / "tools/fm1_mul_scan.py"), str(work / f"{name}.dis"),
                        str(work / f"{name}.sym")], capture_output=True, text=True)
    if r.returncode:
        fail(f"a 64-bit multiply beside a 16-bit load (src/kreisel_mul.h):\n{r.stdout}{r.stderr}")

    check = bytearray(image)
    relocate(check, relocs, CHECK_BASE, CHECK_RAM)
    if bytes(check[HDR:]) != pad(res["x"][0], res["x"][1]):
        fail(f"the relocations do not give what a link at {CHECK_BASE:#x} gives")
    size_img = len(image)
    if size_img > REGION:
        fail(f"{size_img} bytes: more than the FM-1's room for plugins ({REGION})")
    h = header(api=api_version(), version=version, kind=kind, uid=uid, size=size_img, record=record, data_off=HDR + len(text),
               data_size=len(data), bss_size=bss, nreloc=len(relocs), crc=zlib.crc32(bytes(image[HDR:])))
    image[:HDR] = h
    rel = b"".join(struct.pack("<I", o | relocs[o]) for o in sorted(relocs))
    out = Path(out) if out else ROOT / "build/fm1/plugins" / f"{name}.kpl"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(bytes(image) + rel)
    info(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("source", help="the plugin's C file, or with --info a .kpl")
    ap.add_argument("-o", "--out", help="the .kpl (default build/fm1/plugins/NAME.kpl)")
    ap.add_argument("--info", action="store_true", help="say what a .kpl holds")
    a = ap.parse_args()
    if a.info:
        info(a.source)
    else:
        build(a.source, a.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
