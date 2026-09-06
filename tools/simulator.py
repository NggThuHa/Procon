"""Simulator để chấm chiến thuật offline (S1 trong docs/plan/KIENNT.md).

Chỉ mô phỏng phần **đã xác minh từ `hexudon-bot-cpp/main.cpp`**:
tính hợp lệ của nước đi, chi phí bước, tiêu hao nhiên liệu.

CỐ Ý KHÔNG mô phỏng: điểm số, brand, stock, traffic giữa các ngày, điều kiện xe
tiếp tế nạp nhiên liệu. Những thứ đó chưa có nguồn từ ban tổ chức (xem bảng trong
docs/PLAN.md). Mô phỏng chúng theo phỏng đoán sẽ khiến arena chọn sai chiến thuật
— tệ hơn là không có arena. Mở khoá sau Giai đoạn 0.

Fixture là setup THẬT chụp từ server (fixtures/default.json), không phải bịa.

Không cần API, không cần mạng.
"""
import copy
import json

from .errors import (
    E_BAD_FORMAT,
    E_NO_FUEL,
    E_NOT_ADJACENT,
    E_POND,
    E_STEP_OVERFLOW,
    MatchError,
)
from .rules import KIND_PATROL, TERRAIN_POND, TRAFFIC_CLEAR, move_cost, neighbor


class Simulator:
    def __init__(self, setup: dict):
        """`setup` đúng shape của `GET /setup` — xem fixtures/default.json."""
        self._setup = copy.deepcopy(setup)
        self.reset()

    @classmethod
    def from_file(cls, path: str):
        with open(path, encoding="utf-8") as fh:
            return cls(json.load(fh))

    def reset(self):
        setup = copy.deepcopy(self._setup)
        self.width = setup["map"]["width"]
        self.height = setup["map"]["height"]
        self.cells = [c for row in setup["map"]["cells"] for c in row]
        if len(self.cells) != self.width * self.height:
            raise ValueError("fixture: map.cells không khớp width*height")

        self.day_steps = list(setup["daySteps"])
        self.day_seconds = list(setup.get("daySeconds") or [])
        self.n_days = len(self.day_steps)
        # fuelLimits là MỘT int dùng chung, không tách theo loại xe (observed).
        self.patrol_fuel = setup.get("fuelLimits")
        # agents là mảng int = vị trí xuất phát, không phải mảng object (observed).
        self.positions = list(setup["agents"])
        self.spots = copy.deepcopy(setup["spots"])
        self.n_agents = len(self.positions)
        self.busy_threshold = setup.get("busyThreshold")
        self.jammed_threshold = setup.get("jammedThreshold")
        self.players = setup.get("players")
        self.kinds = [KIND_PATROL] * self.n_agents
        self.fuel = [self.patrol_fuel] * self.n_agents
        self.traffic = {}
        self.day = 0
        self.visited = []   # ô mà mỗi xe đã đi vào, theo ngày — để tầng trên chấm

    def assign(self, kinds):
        if len(kinds) != self.n_agents:
            raise MatchError(E_BAD_FORMAT, f"cần {self.n_agents} kind")
        self.kinds = list(kinds)
        self.fuel = [
            self.patrol_fuel if k == KIND_PATROL else None for k in self.kinds
        ]

    def state(self):
        """Đúng shape của `GET /state` thật — xem docs/observed/probe-state-day0.json.

        CỐ Ý không trả `spots`/`stocks`: server thật KHÔNG gửi chúng trong state.
        Bot phải tự theo dõi tồn kho từ `/setup` cộng với những gì mình đã thu.
        Nếu simulator phát không cho chiến thuật, chiến thuật sẽ chạy được ở nhà
        và gãy ở trận thật.
        """
        return {
            "endsAt": 0,
            "day": self.day,
            "agents": [
                {"kind": self.kinds[i], "pos": self.positions[i], "fuel": self.fuel[i]}
                for i in range(self.n_agents)
            ],
            "others": [],
            "traffics": [
                {"pos": p, "status": s} for p, s in sorted(self.traffic.items())
            ],
        }

    def stocks_snapshot(self):
        """Tồn kho hiện tại — CHỈ dùng để chấm điểm/kiểm thử, không phải state.

        Chiến thuật không được gọi hàm này: server thật không cho biết.
        """
        return copy.deepcopy(self.spots)

    def finished(self):
        return self.day >= self.n_days

    # ------------------------------------------------------------- validate
    def validate(self, payload):
        """Trả [{end, path, steps, fuel_used}] hoặc ném MatchError."""
        if self.finished():
            raise MatchError(E_BAD_FORMAT, "trận đã kết thúc")
        if not isinstance(payload, list):
            raise MatchError(E_BAD_FORMAT, "payload phải là mảng")
        if len(payload) != self.n_agents:
            raise MatchError(
                E_BAD_FORMAT, f"cần {self.n_agents} action, nhận {len(payload)}"
            )
        budget = self.day_steps[self.day]
        return [
            self._validate_agent(i, cmds, budget) for i, cmds in enumerate(payload)
        ]

    def _validate_agent(self, index, commands, budget):
        if not isinstance(commands, list):
            raise MatchError(E_BAD_FORMAT, f"agent {index}: action phải là mảng")
        pos = self.positions[index]
        fuel_left = self.fuel[index] if self.kinds[index] == KIND_PATROL else None
        steps = 0
        fuel_used = 0
        path = []

        for cmd in commands:
            if isinstance(cmd, bool) or not isinstance(cmd, int):
                raise MatchError(E_BAD_FORMAT, f"agent {index}: lệnh phải là số nguyên")
            if cmd > 5:
                raise MatchError(E_BAD_FORMAT, f"agent {index}: mã hướng {cmd} > 5")

            if cmd < 0:
                steps += -cmd
                self._check_budget(index, steps, budget)
                continue

            dest = neighbor(pos, cmd, self.width, self.height)
            if dest is None:
                raise MatchError(
                    E_NOT_ADJACENT,
                    f"agent {index}: hướng {cmd} từ ô {pos} ra ngoài bản đồ",
                )
            if self.cells[dest] == TERRAIN_POND:
                raise MatchError(E_POND, f"agent {index}: ô {dest} là ao")

            cost = move_cost(self.cells[pos], self.traffic.get(pos, TRAFFIC_CLEAR))
            if cost is None:
                raise MatchError(E_POND, f"agent {index}: ô nguồn {pos} không đi được")
            step_cost, fuel_cost = cost

            steps += step_cost
            self._check_budget(index, steps, budget)
            if fuel_left is not None:
                if fuel_left < fuel_cost:
                    raise MatchError(
                        E_NO_FUEL, f"agent {index}: cần {fuel_cost}, còn {fuel_left}"
                    )
                fuel_left -= fuel_cost
                fuel_used += fuel_cost

            pos = dest
            path.append(dest)

        return {"end": pos, "path": path, "steps": steps, "fuel_used": fuel_used}

    @staticmethod
    def _check_budget(index, steps, budget):
        if steps > budget:
            raise MatchError(
                E_STEP_OVERFLOW, f"agent {index}: {steps} > {budget} bước"
            )

    # ----------------------------------------------------------------- step
    def step(self, payload):
        """Validate rồi áp dụng một ngày. Trả về plan của từng agent."""
        plans = self.validate(payload)
        for i, plan in enumerate(plans):
            self.positions[i] = plan["end"]
            if self.fuel[i] is not None:
                self.fuel[i] -= plan["fuel_used"]
        self.visited.append([list(p["path"]) for p in plans])
        self.day += 1
        return plans

    def score(self):
        raise NotImplementedError(
            "Chưa có luật chấm điểm từ ban tổ chức. Xem Giai đoạn 0 trong "
            "docs/PLAN.md — brand, stocks và thứ tự xếp hạng đều chưa có nguồn."
        )
