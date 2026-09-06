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


class TestScoring(unittest.TestCase):
    """Tái hiện đúng thí nghiệm m-11542 (docs/observed/probe-result.json)."""

    def _park_one_agent_on_a_spot(self):
        s = sim(kinds=(0, 0, 0, 0))
        spot = s.spots[0]
        s.positions[0] = spot["pos"]
        return s, spot

    def test_standing_on_spot_collects_every_day(self):
        # m-11542: xe vào spot ngày 0 rồi đứng yên -> udon_total = 4 = số ngày.
        s, spot = self._park_one_agent_on_a_spot()
        for _ in range(s.n_days):
            s.step([[-32]] * 4)
        score = s.score()
        self.assertEqual(score["udon_types"], 1)
        self.assertEqual(score["udon_total"], s.n_days)
        self.assertEqual(score["by_brand"], {spot["brand"]: s.n_days})

    def test_daily_types_sum_rewards_opening_early(self):
        # Giữ 1 loại suốt 4 ngày -> 1+1+1+1 = 4, đúng như m-11542.
        s, _ = self._park_one_agent_on_a_spot()
        for _ in range(s.n_days):
            s.step([[-32]] * 4)
        self.assertEqual(s.score()["daily_types_sum"], s.n_days)

    def test_no_collection_when_parked_off_spot(self):
        s = sim(kinds=(0, 0, 0, 0))
        for _ in range(s.n_days):
            s.step([[-32]] * 4)
        self.assertEqual(s.score()["udon_total"], 0)

    def test_supply_vehicle_does_not_collect(self):
        s = sim(kinds=(1, 0, 0, 0))
        s.positions[0] = s.spots[0]["pos"]
        s.step([[-32]] * 4)
        self.assertEqual(s.score()["udon_total"], 0)

    def test_better_follows_priority_order(self):
        low = {"udon_types": 1, "daily_types_sum": 99, "udon_total": 99}
        high = {"udon_types": 2, "daily_types_sum": 1, "udon_total": 1}
        self.assertTrue(Simulator.better(high, low), "udon_types phải thắng trước")
        a = {"udon_types": 2, "daily_types_sum": 9, "udon_total": 99}
        b = {"udon_types": 2, "daily_types_sum": 16, "udon_total": 1}
        self.assertTrue(Simulator.better(b, a), "hoà types thì daily_types_sum quyết")


if __name__ == "__main__":
    unittest.main()
