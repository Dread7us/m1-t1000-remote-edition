"""Verify labeled analysis refuses overlap, drift, and sparse measurements."""
import unittest
import test_device_check  # Installs the serial test double before importing the runner.
from motion_trial import analyze


def records(quiet_a=(48, 50, 52), walking_a=(70, 72, 74),
            quiet_b=(49, 51, 53), walking_b=(71, 73, 75)):
    return [dict(phase=phase, state=2, samples=50, score=value)
            for phase, values in zip(("quiet_a", "walking_a", "quiet_b", "walking_b"),
                                     (quiet_a, walking_a, quiet_b, walking_b)) for value in values]


class MotionTrialTests(unittest.TestCase):
    def test_separation_with_held_out_block(self):
        result = analyze(records())
        self.assertEqual(result["verdict"], "Separated in this trial")
        self.assertEqual(result["holdout_false_alarms"], 0)
        self.assertEqual(result["holdout_walking_detected"], 3)

    def test_observed_overlapping_scores_remain_inconclusive(self):
        result = analyze(records((50, 54, 56), (52, 53, 54)))
        self.assertEqual(result["verdict"], "Inconclusive")
        self.assertNotIn("candidate_threshold", result)

    def test_drift_cannot_be_called_success(self):
        result = analyze(records(quiet_b=(72, 73, 74)))
        self.assertEqual(result["verdict"], "Inconclusive")
        self.assertEqual(result["holdout_false_alarms"], 3)

    def test_decreased_score_requires_independent_confirmation(self):
        result = analyze(records((70, 72, 74), (48, 50, 52), (71, 73, 75), (49, 51, 53)))
        self.assertEqual(result["direction"], "lower")
        self.assertEqual(result["verdict"], "Separated in this trial")

    def test_sparse_or_missing_data_cannot_pass(self):
        data = records()
        data[0]["samples"] = 0
        self.assertEqual(analyze(data)["verdict"], "Inconclusive")
        self.assertEqual(analyze(records()[:-1])["verdict"], "Inconclusive")


if __name__ == "__main__":
    unittest.main()
