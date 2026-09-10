#!/usr/bin/env python3
"""contract_tests.py — schema-level contract validation (Phase 0 gate).

Validates:
  1. Canonical schema files exist and are valid JSON with expected $id.
  2. Evidence JSON (host capability record) against structural invariants of
     dc.host-capability/1 (fields present, honest unmeasured defaults).
  3. Canon files are present.

Exit code 0 = all contract tests pass. Non-zero names the failing gate.
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FAILURES: list[str] = []


def check(name: str, condition: bool, detail: str = "") -> None:
    if condition:
        print(f"  PASS  {name}")
    else:
        print(f"  FAIL  {name}  {detail}")
        FAILURES.append(name)


def load_json(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as f:
        return json.load(f)


def test_schemas() -> None:
    print("[schemas]")
    expected = {
        "host-capability.schema.json": "https://11vated.tech/schemas/dc.host-capability/2",
        "host-capability-v1.schema.json": "https://11vated.tech/schemas/dc.host-capability/1",
        "profile.schema.json": "https://11vated.tech/schemas/dc.profile/1",
        "game.schema.json": "https://11vated.tech/schemas/dc.game/1",
        "fidelity.schema.json": "https://11vated.tech/schemas/dc.fidelity/1",
        "lifecycle.schema.json": "https://11vated.tech/schemas/dc.lifecycle/1",
    }
    for fname, schema_id in expected.items():
        p = ROOT / "formats" / "schemas" / fname
        if not p.exists():
            check(f"schema {fname} exists", False, str(p))
            continue
        try:
            data = load_json(p)
        except json.JSONDecodeError as e:
            check(f"schema {fname} parses", False, str(e))
            continue
        check(f"schema {fname} $id", data.get("$id") == schema_id, f"got {data.get('$id')}")
        check(f"schema {fname} draft", "$schema" in data)


def test_profiles() -> None:
    print("[profiles: DCP registry]")
    pdir = ROOT / "formats" / "profiles"
    files = sorted(pdir.glob("DCP-*.json")) if pdir.exists() else []
    check("8 provisional DCP profiles", len(files) >= 8, f"found {len(files)}")
    valid_paths = {
        "cpu.game_thread_score", "cpu.sustained_score", "cpu.single_thread_score", "cpu.worker_score",
        "gpu.raster_score", "gpu.compute_score", "gpu.rt_score", "gpu.sustained_score",
        "gpu.copy_gbps", "gpu.matrix_score",
        "memory.bandwidth_gbps", "memory.vram_bytes", "memory.ram_bytes",
        "storage.read_seq_mbps", "storage.latency_us_p99",
        "display.min_refresh_hz", "display.hdr_active", "display.vrr_proven",
    }
    for f in files:
        try:
            data = load_json(f)
        except json.JSONDecodeError as e:
            check(f"{f.name} parses", False, str(e))
            continue
        ok_schema = data.get("schema") == "dc.profile/1"
        ok_id = str(data.get("id", "")).startswith("DCP-")
        check(f"{f.name} schema+id", ok_schema and ok_id)
        reqs = data.get("requires", [])
        ok_paths = all(r.get("path") in valid_paths for r in reqs) and len(reqs) >= 1
        check(f"{f.name} requirement paths", ok_paths)


def test_canon() -> None:
    print("[canon]")
    for p in [ROOT / "docs" / "canon" / "DC-CANON-001.md",
              ROOT / "docs" / "canon" / "DC-BLUEPRINT-001.md",
              ROOT / "docs" / "research" / "RESEARCH_LEDGER.md"]:
        check(f"{p.relative_to(ROOT)} exists", p.exists() and p.stat().st_size > 1000)
    adr_dir = ROOT / "docs" / "adr"
    adr_files = sorted(adr_dir.glob("ADR-*.md")) if adr_dir.exists() else []
    check("20 ADRs present", len(adr_files) >= 20, f"found {len(adr_files)}")


def test_host_capability_evidence() -> None:
    print("[evidence: host-capability]")
    evidence_candidates = sorted((ROOT / "evidence").glob("host-capability*.json")) if (ROOT / "evidence").exists() else []
    if not evidence_candidates:
        print("  SKIP  no host-capability evidence yet (run dc-hostprof --out evidence/host-capability.json)")
        return
    data = load_json(evidence_candidates[0])
    schema = data.get("schema", "")
    check("schema field is /1 or /2", schema in ("dc.host-capability/1", "dc.host-capability/2"), schema)
    for section in ["os", "cpu", "gpu", "memory", "storage", "display", "audio", "input", "thermal", "trust"]:
        check(f"section {section}", section in data)

    if schema == "dc.host-capability/2":
        # ADR-0021 invariants.
        qual = data.get("qualification", {})
        check("qualification section", "measured" in qual and "mode" in qual)
        measured = bool(qual.get("measured"))
        if measured:
            # C5: measured records must have a documented mode.
            check("measured mode documented", qual.get("mode") in ("quick", "standard", "certification"))
            gpu = data.get("gpu", {})
            if gpu.get("raster_score", 0) and gpu.get("compute_score", 0):
                # Scores without derivation output is a C10 violation.
                check("scores imply derivation output",
                      bool(data.get("profiles_claimed")) or bool(data.get("profiles_rejected")))
        else:
            # Discovery-only: no scores may be claimed, no profiles claimed.
            check("discovery-only record claims no profiles", not data.get("profiles_claimed"))
        check("vrr three-state fields", all(
            k in (data.get("display") or [{}])[0].get("vrr", {})
            for k in ("supported", "path_compatible", "actively_proven")))
    else:
        # Legacy /1 invariants.
        gpu = data.get("gpu", {})
        if "raster_score" in gpu and gpu["raster_score"]:
            check("gpu raster_score measured implies profiles claimed",
                  bool(data.get("profiles")), "scores without claimed profiles is a C10 violation")
        thermal = data.get("thermal", {})
        if thermal.get("sustained_profile_valid"):
            check("sustained validity implies profiles", bool(data.get("profiles")))
        for prof in data.get("profiles", []):
            check(f"profile {prof} versioned", str(prof).startswith("DCP-"))

    # Qualification evidence files (when a --qualify pass ran).
    qdir = ROOT / "evidence" / "qualification"
    if qdir.exists():
        for f in sorted(qdir.glob("*.json")):
            try:
                qdata = load_json(f)
            except json.JSONDecodeError as e:
                check(f"{f.name} parses", False, str(e))
                continue
            if f.name == "benchmarks.json":
                check("benchmarks.json schema", qdata.get("schema") == "dc.qualification/1")
                benches = qdata.get("benchmarks", [])
                check("benchmarks non-empty", len(benches) >= 1)
                for b in benches:
                    check(f"bench {b.get('benchmark_id')} has methodology",
                          bool(b.get("methodology_id")))
                    # C10: non-finite scores must never appear.
                    check(f"bench {b.get('benchmark_id')} score finite",
                          isinstance(b.get("score"), (int, float)))


def test_dependency_rules() -> None:
    print("[dependency rules (canon 39.1)]")
    core_dir = ROOT / "runtime" / "core"
    violations: list[str] = []
    if core_dir.exists():
        for f in core_dir.rglob("*"):
            if f.suffix not in {".hpp", ".cpp", ".h"}:
                continue
            text = f.read_text(encoding="utf-8", errors="replace")
            for forbidden in ["<windows.h>", "<d3d12", "<dxgi", "<vulkan>", "X11/"]:
                if forbidden in text:
                    violations.append(f"{f.name} includes {forbidden}")
    check("runtime/core has no host/OS includes", not violations, "; ".join(violations))


def main() -> int:
    print("=" * 60)
    print("11vated Digital Console — Phase 0 contract tests")
    print("=" * 60)
    test_schemas()
    test_canon()
    test_profiles()
    test_host_capability_evidence()
    test_dependency_rules()
    print("=" * 60)
    if FAILURES:
        print(f"RESULT: {len(FAILURES)} contract test(s) FAILED: {FAILURES}")
        return 1
    print("RESULT: all contract tests passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
