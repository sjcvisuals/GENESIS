#!/usr/bin/env python3
"""Headless ExtGenesisRemover preflight (Linux/cloud). No PIXERA required.

Gates:
  1) Static MSVC-breakers (broken '\\n', Unicode in string literals)
  2) Stamp / RVM / entrypoint presence
  3) Build + run genesis_matting_smoke --require-rvm
"""
from __future__ import annotations

import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REPORT = Path(os.environ.get("TMPDIR", "/tmp")) / "ExtGenesisRemover.preflight.txt"

pass_n = 0
fail_n = 0
warn_n = 0
lines: list[str] = []


def step(msg: str, level: str = "INFO") -> None:
    line = f"[{level}] {msg}"
    lines.append(line)
    print(line)


def ok(msg: str) -> None:
    global pass_n
    pass_n += 1
    step(msg, "PASS")


def fail(msg: str) -> None:
    global fail_n
    fail_n += 1
    step(msg, "FAIL")


def warn(msg: str) -> None:
    global warn_n
    warn_n += 1
    step(msg, "WARN")


_UNICODE_BAD = {
    "\u2014": "em-dash",
    "\u2013": "en-dash",
    "\u2192": "arrow",
    "\u2018": "smart-quote",
    "\u2019": "smart-quote",
    "\u201c": "smart-quote",
    "\u201d": "smart-quote",
}


def scan_unicode_in_strings(path: Path) -> list[str]:
    """Flag Unicode punctuation inside double-quoted string literals (MSVC risk)."""
    text = path.read_text(encoding="utf-8", errors="replace")
    hits: list[str] = []
    for i, line in enumerate(text.splitlines(), 1):
        stripped = line.strip()
        if stripped.startswith("//"):
            continue
        for m in re.finditer(r'"([^"\\]|\\.)*"', line):
            s = m.group(0)
            for ch, name in _UNICODE_BAD.items():
                if ch in s:
                    hits.append(f"{path.name}:{i} {name} in string")
    return hits


def main() -> int:
    print("=== GENESIS Remover PREFLIGHT (Linux/cloud) ===")
    print(f"Repo: {ROOT}")
    print()

    step("--- Static source gates ---")
    host = ROOT / "extensions/ExtGenesisRemover/matte_host/main.cpp"
    stamp_cpp = ROOT / "extensions/ExtGenesisRemover/src/BuildStamp.cpp"
    rvm = ROOT / "assets/matting/rvm_mobilenetv3_fp16.onnx"
    src_root = ROOT / "extensions/ExtGenesisRemover"

    if not host.is_file():
        fail(f"missing {host}")
    else:
        raw = host.read_text(encoding="utf-8", errors="replace")
        if re.search(r"<<\s*'\s*[\r\n]+\s*';", raw):
            fail("matte_host/main.cpp has broken newline char literal (MSVC C2001/C2137)")
        elif not re.search(r"<<\s*'\\n'\s*;", raw):
            warn("matte_host log newline pattern not found")
        else:
            ok("matte_host log newline is a single '\\n' char literal")
        if "--smoke" in raw and "runHostSmoke" in raw:
            ok("matte_host supports --smoke / runHostSmoke")
        else:
            fail("matte_host smoke entry incomplete (--smoke / runHostSmoke)")

    stamp = None
    if not stamp_cpp.is_file():
        fail("BuildStamp.cpp missing")
    else:
        text = stamp_cpp.read_text(encoding="utf-8", errors="replace")
        m = re.search(r'(kBuildStamp)\[\]\s*=\s*"([^"]+)"', text)
        if m:
            stamp = m.group(2)
            ok(f"BuildStamp.cpp stamp = {stamp} ({m.group(1)})")
        else:
            fail("Could not parse build stamp from BuildStamp.cpp")

    if not rvm.is_file():
        fail(f"RVM model missing: {rvm}")
    else:
        size = rvm.stat().st_size
        head = rvm.read_bytes()[:80]
        if size < 1_000_000 or b"git-lfs" in head or head.startswith(b"version https://git-lfs"):
            fail(f"RVM model is LFS pointer or too small ({size} bytes). Run: git lfs pull")
        else:
            ok(f"RVM model OK ({size / 1e6:.1f} MB)")

    unicode_hits: list[str] = []
    for p in list(src_root.rglob("*.cpp")) + list(src_root.rglob("*.h")):
        unicode_hits.extend(scan_unicode_in_strings(p))
    if unicode_hits:
        for h in unicode_hits[:12]:
            fail(f"Unicode in string literal: {h}")
        if len(unicode_hits) > 12:
            fail(f"... and {len(unicode_hits) - 12} more Unicode string hits")
    else:
        ok("No Unicode punctuation inside ExtGenesisRemover string literals")

    bats_ok = (ROOT / "scripts/preflight-remover.ps1").is_file() and (
        ROOT / "PREFLIGHT-REMOVER.bat"
    ).is_file()
    if bats_ok:
        ok("Windows PREFLIGHT entrypoints present")
    else:
        fail("PREFLIGHT-REMOVER.bat / scripts/preflight-remover.ps1 missing")

    health = ROOT / "scripts/health-check-remover.ps1"
    if health.is_file():
        ok("health-check-remover.ps1 present")
    else:
        fail("scripts/health-check-remover.ps1 missing")

    step("--- Build/run genesis_matting_smoke ---")
    build_dir = ROOT / "build"
    try:
        cmake_ok = subprocess.run(["cmake", "--version"], capture_output=True).returncode == 0
        ninja_ok = subprocess.run(["ninja", "--version"], capture_output=True).returncode == 0
    except FileNotFoundError:
        cmake_ok = ninja_ok = False

    if not cmake_ok or not ninja_ok:
        fail("cmake/ninja unavailable - cannot run native smoke on this host")
    else:
        cfg = subprocess.run(
            [
                "cmake",
                "-S",
                str(ROOT),
                "-B",
                str(build_dir),
                "-G",
                "Ninja",
                "-DCMAKE_BUILD_TYPE=Debug",
            ],
            cwd=ROOT,
            capture_output=True,
            text=True,
        )
        if cfg.returncode != 0:
            fail("cmake configure failed")
            lines.append(cfg.stderr[-2500:])
        else:
            ok("cmake configure ok")
            bld = subprocess.run(
                ["cmake", "--build", str(build_dir), "--target", "genesis_matting_smoke", "-j"],
                cwd=ROOT,
                capture_output=True,
                text=True,
            )
            if bld.returncode != 0:
                fail("genesis_matting_smoke build failed")
                lines.append(bld.stderr[-3000:] or bld.stdout[-3000:])
            else:
                ok("genesis_matting_smoke built")
                found = [p for p in build_dir.rglob("genesis_matting_smoke") if p.is_file()]
                exe = next((p for p in found if p.suffix == ""), None) or (found[0] if found else None)
                if not exe:
                    fail("genesis_matting_smoke binary not found")
                else:
                    run = subprocess.run(
                        [str(exe), "--require-rvm"],
                        cwd=str(exe.parent),
                        capture_output=True,
                        text=True,
                    )
                    if run.stdout:
                        lines.append(run.stdout[-2000:])
                    if run.stderr:
                        lines.append(run.stderr[-2000:])
                    if run.returncode == 0 and "ALL PASS" in (run.stdout or ""):
                        ok("matting smoke PASS (--require-rvm)")
                    else:
                        fail(f"matting smoke FAILED exit={run.returncode}")

    summary = f"PREFLIGHT RESULT pass={pass_n} warn={warn_n} fail={fail_n} stamp={stamp}"
    lines.append(summary)
    REPORT.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print()
    print(summary)
    print(f"Report: {REPORT}")
    return 1 if fail_n else 0


if __name__ == "__main__":
    raise SystemExit(main())
