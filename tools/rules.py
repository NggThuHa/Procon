"""Luật bản đồ và chi phí di chuyển, dùng cho mock server.

ASSUMPTION: parity EVEN-R (hàng chẵn lệch phải) và bảng chi phí ở đây lấy từ
`README.md` §4 và khớp với `hexudon-bot-cpp/main.cpp`. CHƯA đối chiếu với
server thi đấu. Nếu server trả khác, sửa file này trước, không sửa validator.

Đây là validator phía *server giả lập* (K1). Validator phía bot là D4 của DATNT
và phải độc lập — hai bên trùng bug thì mất tác dụng kiểm tra chéo.
"""

TERRAIN_PLAIN = 0
TERRAIN_ROAD = 1
TERRAIN_MOUNTAIN = 2
TERRAIN_POND = 3

TRAFFIC_CLEAR = 0
TRAFFIC_CROWDED = 1
TRAFFIC_JAM = 2

KIND_PATROL = 0
KIND_SUPPLY = 1

# (dc, dr) theo mã hướng 0..5: trên-trái, trên-phải, phải, dưới-phải, dưới-trái, trái.
_DELTA_EVEN_ROW = ((0, -1), (1, -1), (1, 0), (1, 1), (0, 1), (-1, 0))
_DELTA_ODD_ROW = ((-1, -1), (0, -1), (1, 0), (0, 1), (-1, 1), (-1, 0))

# terrain -> (step_cost, fuel_cost); đường phụ thuộc traffic nên tra riêng.
_COST_BY_TERRAIN = {
    TERRAIN_PLAIN: (2, 1),
    TERRAIN_MOUNTAIN: (3, 2),
}
_ROAD_STEP_BY_TRAFFIC = {
    TRAFFIC_CLEAR: 1,
    TRAFFIC_CROWDED: 2,
    TRAFFIC_JAM: 4,
}


def to_pos(row: int, col: int, width: int) -> int:
    return row * width + col


def to_rowcol(pos: int, width: int):
    return divmod(pos, width)


def neighbor(pos: int, direction: int, width: int, height: int):
    """Ô kề theo mã hướng, hoặc None nếu ra ngoài bản đồ."""
    if not 0 <= direction <= 5:
        return None
    row, col = to_rowcol(pos, width)
    dc, dr = (_DELTA_ODD_ROW if row % 2 else _DELTA_EVEN_ROW)[direction]
    ncol, nrow = col + dc, row + dr
    if not (0 <= ncol < width and 0 <= nrow < height):
        return None
    return to_pos(nrow, ncol, width)


def move_cost(terrain: int, traffic: int = TRAFFIC_CLEAR):
    """(step_cost, fuel_cost) khi RỜI một ô, hoặc None nếu ô không đi được.

    Chi phí tính theo ô nguồn, không phải ô đích (README §4.3).
    """
    if terrain == TERRAIN_ROAD:
        step = _ROAD_STEP_BY_TRAFFIC.get(traffic)
        return None if step is None else (step, 2)
    return _COST_BY_TERRAIN.get(terrain)
