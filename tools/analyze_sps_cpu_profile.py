"""Summarize sampled native CPU costs. This cannot establish an SPS speedup."""
import argparse
import json
import math
from pathlib import Path
import re

# Diagnostic follow-up policy, not a statistical confidence or overhead estimate.
CONTROL_GAP_WARNING_PERCENT = 10


def fields(line):
    result = {}
    for key, raw in re.findall(r"([a-z][a-z0-9_]*)=([^\s;]+)", line):
        try:
            value = float(raw) if "." in raw else int(raw)
        except ValueError:
            value = raw
        if isinstance(value, float) and not math.isfinite(value):
            raise ValueError("Non-finite profile value")
        result[key] = value
    return result


def summarize(text):
    captures, current = [], None
    for line in text.splitlines():
        if "sps_cpu_profile start " in line:
            current = {"summary": None, "groups": {}, "work": {}}
        elif current is not None:
            if "sps_cpu_profile summary " in line:
                current["summary"] = fields(line)
            elif "sps_cpu_profile group " in line:
                group = fields(line)
                current["groups"][(group["phase"], group["slot"])] = group
            elif "sps_cpu_profile work " in line:
                work = fields(line)
                current["work"].setdefault(work["slot"], {})[work["kind"]] = work
            elif "sps_cpu_profile complete;" in line:
                captures.append(current)
                current = None
    if not captures:
        raise ValueError("No completed SPS CPU profile; start/finish Ctrl+Alt+F10 on track")
    results = []
    for capture in captures:
        summary = capture["summary"]
        if summary is None:
            raise ValueError("Missing capture summary")
        warnings, attribution_warnings, comparisons = [], [], []
        for name in ("invalid_ranges", "cycle_failures"):
            if summary[name]:
                warnings.append(f"{name}={summary[name]}")
        for slot in ("outer_left", "outer_right", "focus_left", "focus_right"):
            coarse = capture["groups"].get(("coarse", slot))
            detailed = capture["groups"].get(("detailed", slot))
            if coarse is None or detailed is None:
                warnings.append(f"{slot}: missing coarse/detailed coverage")
                continue
            for group in (coarse, detailed):
                if group["samples"] < 30 or group["dropped"]:
                    warnings.append(f"{slot} {group['phase']}: samples={group['samples']} dropped={group['dropped']}")
            work = capture["work"].get(slot, {})
            if len(work) != 7:
                raise ValueError(f"{slot}: incomplete work categories")
            partition = sum(w["exclusive_mean_ms"] for w in work.values()) + detailed["remainder_mean_ms"]
            if abs(partition - detailed["wall_mean_ms"]) > 0.00001:
                raise ValueError(f"{slot}: invalid nested accounting")
            for value in work.values():
                value["calls_per_recorded_group"] = value["calls"] / detailed["samples"]
            gap = (
                100 * (detailed["wall_mean_ms"] / coarse["wall_mean_ms"] - 1)
                if coarse["wall_mean_ms"] > 0 else None
            )
            if gap is None or abs(gap) >= CONTROL_GAP_WARNING_PERCENT:
                attribution_warnings.append(
                    f"{slot}: large/undefined coarse-detailed wall difference; "
                    "individual timings need lower-overhead follow-up; "
                    "different frames prevent automatic overhead subtraction"
                )
            comparisons.append({
                "slot": slot, "coarse": coarse, "detailed": detailed,
                "detailed_minus_coarse_wall_percent": gap, "work": work,
            })
        results.append({
            "summary": summary, "slots": comparisons, "coverage_warnings": warnings,
            "attribution_warnings": attribution_warnings,
            "requires_timing_followup": bool(warnings or attribution_warnings),
            "control_gap_warning_threshold_percent": CONTROL_GAP_WARNING_PERCENT,
        })
    return {
        "rendering": "legacy", "native_work_removed": False, "speedup_established": False,
        "unit": "wall milliseconds per recorded group invocation; not per frame",
        "captures": results,
        "limitations": [
            "Coarse/detailed use different frames and retain the same hooks; neither is an unhooked baseline.",
            "Wall time includes waits and instrumentation. Thread cycles are not milliseconds.",
            "Inclusive categories overlap. Only exclusive categories plus remainder partition group wall.",
            "No category is automatically shareable/removable; transform mode is not full numeric eligibility.",
        ],
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    args = parser.parse_args()
    print(json.dumps(summarize(args.log.read_text(encoding="utf-8-sig")), indent=2))
