import unittest

from tools import rules


class TestHexGeometry(unittest.TestCase):
    W, H = 7, 5

    def n(self, pos, d):
        return rules.neighbor(pos, d, self.W, self.H)

    def test_even_row_matches_cpp_bot(self):
        # pos 17 = (r2, c3), hàng chẵn. Giá trị đối chiếu với DE[] trong main.cpp.
        self.assertEqual([self.n(17, d) for d in range(6)], [10, 11, 18, 25, 24, 16])

    def test_odd_row_matches_cpp_bot(self):
        # pos 24 = (r3, c3), hàng lẻ. Đối chiếu với DO[] trong main.cpp.
        self.assertEqual([self.n(24, d) for d in range(6)], [16, 17, 25, 31, 30, 23])

    def test_parity_shifts_opposite_ways(self):
        # Hướng 0 ở hàng chẵn giữ nguyên cột, ở hàng lẻ lùi một cột.
        self.assertEqual(self.n(17, 0), 10)
        self.assertEqual(self.n(24, 0), 16)

    def test_edges_return_none(self):
        self.assertIsNone(self.n(0, 0))
        self.assertIsNone(self.n(0, 5))
        self.assertEqual(self.n(0, 2), 1)
        self.assertIsNone(self.n(34, 3))  # góc dưới-phải

    def test_move_is_reversible(self):
        opposite = {0: 3, 1: 4, 2: 5, 3: 0, 4: 1, 5: 2}
        for pos in range(self.W * self.H):
            for d in range(6):
                nb = self.n(pos, d)
                if nb is not None:
                    self.assertEqual(self.n(nb, opposite[d]), pos,
                                     f"pos={pos} d={d} không đối xứng")

    def test_invalid_direction(self):
        self.assertIsNone(self.n(17, 6))
        self.assertIsNone(self.n(17, -1))


class TestMoveCost(unittest.TestCase):
    def test_costs_match_readme_table(self):
        self.assertEqual(rules.move_cost(rules.TERRAIN_PLAIN), (2, 1))
        self.assertEqual(rules.move_cost(rules.TERRAIN_MOUNTAIN), (3, 2))
        self.assertEqual(rules.move_cost(rules.TERRAIN_ROAD, rules.TRAFFIC_CLEAR), (1, 2))
        self.assertEqual(rules.move_cost(rules.TERRAIN_ROAD, rules.TRAFFIC_CROWDED), (2, 2))
        self.assertEqual(rules.move_cost(rules.TERRAIN_ROAD, rules.TRAFFIC_JAM), (4, 2))

    def test_pond_is_impassable(self):
        self.assertIsNone(rules.move_cost(rules.TERRAIN_POND))


if __name__ == "__main__":
    unittest.main()
