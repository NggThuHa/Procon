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


def sim(kinds=(0, 0, 1)):
    s = Simulator.from_file(FIXTURE)
    s.assign(list(kinds))
    return s


class TestAccepts(unittest.TestCase):
    def test_plan_from_sample_bot_is_legal(self):
        # Đúng chuỗi bot mẫu sinh ra ở ngày 0; nếu simulator từ chối thì một trong
        # hai bên sai, và đó là tín hiệu phải điều tra chứ không phải sửa test.
        s = sim()
        plans = s.step([[2, 2, -9], [1, 2, 2, -6], [-12]])
        self.assertEqual(plans[0]["end"], 16)
        self.assertEqual(plans[1]["end"], 16)
        self.assertEqual(plans[2]["end"], 28)

    def test_supply_vehicle_has_no_fuel_tracking(self):
        s = sim()
        s.step([[-12], [-12], [-12]])
        self.assertIsNone(s.fuel[2])

    def test_day_advances_until_finished(self):
        s = sim()
        for _ in range(s.n_days):
            self.assertFalse(s.finished())
            s.step([[-1], [-1], [-1]])
        self.assertTrue(s.finished())


class TestRejects(unittest.TestCase):
    def assertRejects(self, code, payload, setup=None):
        s = setup or sim()
        with self.assertRaises(MatchError) as ctx:
            s.validate(payload)
        self.assertEqual(ctx.exception.code, code, str(ctx.exception))

    def test_wrong_agent_count(self):
        self.assertRejects(E_BAD_FORMAT, [[0], [0]])

    def test_not_a_list(self):
        self.assertRejects(E_BAD_FORMAT, {"agents": []})

    def test_direction_above_five(self):
        self.assertRejects(E_BAD_FORMAT, [[6], [-1], [-1]])

    def test_non_integer_command(self):
        self.assertRejects(E_BAD_FORMAT, [["2"], [-1], [-1]])

    def test_off_map(self):
        # agent 1 ở pos 21 = (r3, c0); hướng 0 ở hàng lẻ lùi một cột -> ra ngoài.
        self.assertRejects(E_NOT_ADJACENT, [[-1], [0], [-1]])

    def test_pond(self):
        # Đặt agent 0 ở pos 18 = (r2,c4); hướng 0 (trên-trái, hàng chẵn) tới
        # pos 11 = (r1,c4), là ao.
        s = sim()
        s.positions[0] = 18
        self.assertEqual(s.cells[11], 3)
        self.assertRejects(E_POND, [[0], [-1], [-1]], setup=s)

    def test_step_overflow(self):
        # daySteps[0] = 12; chờ 13 bước là vượt.
        self.assertRejects(E_STEP_OVERFLOW, [[-13], [-1], [-1]])

    def test_step_budget_exact_is_allowed(self):
        s = sim()
        s.validate([[-12], [-12], [-12]])  # không được ném

    def test_no_fuel(self):
        s = sim()
        s.fuel[0] = 0
        self.assertRejects(E_NO_FUEL, [[2], [-1], [-1]], setup=s)


class TestScoreIsBlocked(unittest.TestCase):
    def test_score_refuses_until_rules_verified(self):
        with self.assertRaises(NotImplementedError):
            sim().score()


if __name__ == "__main__":
    unittest.main()
