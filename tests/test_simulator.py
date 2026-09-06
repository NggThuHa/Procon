"""Test simulator trên setup THẬT (fixtures/default.json, chụp từ server).

Map 8x8, 4 xe xuất phát ở [3, 63, 45, 42], daySteps 32, fuelLimits 64.
Ao ở pos 0, 9, 37, 46.
"""
import unittest

from tools.errors import (
    E_BAD_FORMAT,
    E_NO_FUEL,
    E_NOT_ADJACENT,
    E_POND,
    E_STEP_OVERFLOW,
    MatchError,
)
from tools.simulator import Simulator

FIXTURE = "fixtures/default.json"
WAIT = [-1, -1, -1, -1]


def sim(kinds=(0, 0, 0, 1)):
    s = Simulator.from_file(FIXTURE)
    s.assign(list(kinds))
    return s


class TestReadsRealSetup(unittest.TestCase):
    def test_agents_is_flat_position_array(self):
        # Shape thật: mảng int, KHÔNG phải mảng object {"pos": ...}.
        self.assertEqual(sim().positions, [3, 63, 45, 42])

    def test_fuel_limit_is_a_single_int(self):
        # fuelLimits = 64 dùng chung, không tách patrol/supply.
        s = sim()
        self.assertEqual(s.patrol_fuel, 64)
        self.assertEqual(s.fuel[:3], [64, 64, 64])
        self.assertIsNone(s.fuel[3], "xe tiếp tế không theo dõi nhiên liệu")

    def test_traffic_thresholds_come_from_setup(self):
        # Không hardcode: server nói 5 và 10.
        s = sim()
        self.assertEqual((s.busy_threshold, s.jammed_threshold), (5, 10))

    def test_day_budget(self):
        s = sim()
        self.assertEqual(s.day_steps, [32] * 4)
        self.assertEqual(s.day_seconds, [60] * 4)


class TestAccepts(unittest.TestCase):
    def test_two_moves_east_then_wait(self):
        s = sim()
        plans = s.step([[2, 2, -28], WAIT[0:1] * 1, [-1], [-1]])
        self.assertEqual(plans[0]["end"], 5)
        self.assertEqual(plans[0]["steps"], 32)
        self.assertEqual(s.fuel[0], 64 - 2)

    def test_exact_step_budget_allowed(self):
        sim().validate([[-32], [-1], [-1], [-1]])

    def test_match_ends_after_last_day(self):
        s = sim()
        for _ in range(s.n_days):
            self.assertFalse(s.finished())
            s.step([list(WAIT[:1]) for _ in range(4)])
        self.assertTrue(s.finished())


class TestRejects(unittest.TestCase):
    def assertRejects(self, code, payload, setup=None):
        s = setup or sim()
        with self.assertRaises(MatchError) as ctx:
            s.validate(payload)
        self.assertEqual(ctx.exception.code, code, str(ctx.exception))

    def test_wrong_agent_count(self):
        self.assertRejects(E_BAD_FORMAT, [[-1], [-1], [-1]])

    def test_not_a_list(self):
        self.assertRejects(E_BAD_FORMAT, {"agents": []})

    def test_direction_above_five(self):
        self.assertRejects(E_BAD_FORMAT, [[6], [-1], [-1], [-1]])

    def test_non_integer_command(self):
        self.assertRejects(E_BAD_FORMAT, [["2"], [-1], [-1], [-1]])

    def test_off_map(self):
        # Xe 0 ở pos 3 = (r0,c3); hướng 0 là trên-trái -> vượt cạnh trên.
        self.assertRejects(E_NOT_ADJACENT, [[0], [-1], [-1], [-1]])

    def test_pond(self):
        # pos 1 = (r0,c1) núi; hướng 4 (dưới-trái, hàng chẵn) -> pos 9 là ao.
        s = sim()
        s.positions[0] = 1
        self.assertEqual(s.cells[9], 3)
        self.assertRejects(E_POND, [[4], [-1], [-1], [-1]], setup=s)

    def test_step_overflow(self):
        self.assertRejects(E_STEP_OVERFLOW, [[-33], [-1], [-1], [-1]])

    def test_no_fuel(self):
        s = sim()
        s.fuel[0] = 0
        self.assertRejects(E_NO_FUEL, [[2], [-1], [-1], [-1]], setup=s)


class TestScoreIsBlocked(unittest.TestCase):
    def test_score_refuses_until_scoring_verified(self):
        # Thứ tự xếp hạng đã xác minh qua standings, nhưng cách CỘNG điểm
        # (thu udon khi nào, bao nhiêu) thì chưa.
        with self.assertRaises(NotImplementedError):
            sim().score()


if __name__ == "__main__":
    unittest.main()
