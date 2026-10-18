"""The rules check_routes.py holds a row to: python3 -m unittest test_check_routes, from tools/."""
import unittest

from check_routes import SIZED, sized_rules

FAR = ["--language", "fr", "--view", "far", "--screen", "1920x1080"]


class SizedRules(unittest.TestCase):
    def test_a_row_of_the_normal_size_holds_none(self):
        self.assertEqual(sized_rules("enhanced", ["--language", "fr"], [], "sameboy"), [])

    def test_a_sized_row_holds_them_all(self):
        self.assertEqual(sized_rules("enhanced", FAR, ["language.code=fr"], "mgba"), SIZED["enhanced"])

    def test_its_own_ceiling_replaces_the_rule_on_every_core(self):
        for core in ("sameboy", "mgba"):
            self.assertEqual(sized_rules("enhanced", FAR, ["enhanced.ghost_wrong_room<=11"], core), ["enhanced.cold_fill_frames>=0"])

    def test_a_ceiling_for_one_core_leaves_the_rule_on_the_other(self):
        expect = ["sameboy:enhanced.ghost_wrong_room<=11"]
        self.assertEqual(sized_rules("enhanced", FAR, expect, "sameboy"), ["enhanced.cold_fill_frames>=0"])
        self.assertEqual(sized_rules("enhanced", FAR, expect, "mgba"), SIZED["enhanced"])

    def test_only_a_ceiling_replaces_it(self):
        for rule in ("enhanced.ghost_wrong_room>=0", "enhanced.ghost_wrong_room=3", "mgba:enhanced.ghost_wrong_room>=0"):
            self.assertEqual(sized_rules("enhanced", FAR, [rule], "mgba"), SIZED["enhanced"])

    def test_a_threaded_row_has_no_sized_rules(self):
        self.assertEqual(sized_rules("enhanced-threaded", FAR, [], "sameboy"), [])


if __name__ == "__main__":
    unittest.main()
