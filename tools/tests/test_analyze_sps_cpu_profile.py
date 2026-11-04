import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("profile", Path(__file__).parents[1] / "analyze_sps_cpu_profile.py")
profile = importlib.util.module_from_spec(spec)
spec.loader.exec_module(profile)


def capture():
    lines = ["sps_cpu_profile start rendering=legacy",
             "sps_cpu_profile summary reason=duration invalid_ranges=0 cycle_failures=0"]
    for slot in ("outer_left", "outer_right", "focus_left", "focus_right"):
        for phase in ("coarse", "detailed"):
            lines.append(f"sps_cpu_profile group phase={phase} slot={slot} samples=100 dropped=0 wall_mean_ms=1.0 remainder_mean_ms=0.3")
        for kind in ("preparation", "transform", "other_transform", "upload", "draw", "map", "unmap"):
            lines.append(f"sps_cpu_profile work slot={slot} kind={kind} calls=200 inclusive_mean_ms=0.2 exclusive_mean_ms=0.1")
    return "\n".join(lines + ["sps_cpu_profile complete;"])


class AnalysisTest(unittest.TestCase):
    def test_controls_and_units(self):
        result = profile.summarize(capture())
        self.assertFalse(result["speedup_established"])
        self.assertFalse(result["native_work_removed"])
        report = result["captures"][0]
        self.assertEqual(report["coverage_warnings"], [])
        self.assertEqual(report["attribution_warnings"], [])
        self.assertFalse(report["requires_timing_followup"])
        self.assertEqual(report["slots"][2]["work"]["draw"]["calls_per_recorded_group"], 2)
        self.assertEqual(report["slots"][2]["detailed_minus_coarse_wall_percent"], 0)

    def test_missing_or_partial(self):
        for text in ("", capture().replace("sps_cpu_profile complete;", "")):
            with self.assertRaises(ValueError):
                profile.summarize(text)
        text = "\n".join(line for line in capture().splitlines() if "slot=focus_right" not in line)
        self.assertTrue(profile.summarize(text)["captures"][0]["coverage_warnings"])

    def test_invalid_accounting(self):
        with self.assertRaises(ValueError):
            profile.summarize(capture().replace("exclusive_mean_ms=0.1", "exclusive_mean_ms=0.2"))

    def test_multiple_captures(self):
        self.assertEqual(len(profile.summarize(capture() + "\n" + capture())["captures"]), 2)

    def test_large_control_gap_is_not_a_clean_timing_result(self):
        lines = [
            line.replace("wall_mean_ms=1.0", "wall_mean_ms=1.8").replace(
                "remainder_mean_ms=0.3", "remainder_mean_ms=1.1")
            if "phase=detailed" in line else line
            for line in capture().splitlines()
        ]
        report = profile.summarize("\n".join(lines))["captures"][0]
        self.assertEqual(report["coverage_warnings"], [])
        self.assertEqual(len(report["attribution_warnings"]), 4)
        self.assertTrue(report["requires_timing_followup"])
        self.assertAlmostEqual(report["slots"][0]["detailed_minus_coarse_wall_percent"], 80)


if __name__ == "__main__":
    unittest.main()
