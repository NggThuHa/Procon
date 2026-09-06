"""Lỗi khi một chuỗi lệnh không hợp lệ.

CẢNH BÁO: các mã `E_*` dưới đây là **tên do team đặt**, KHÔNG phải mã lỗi của
ban tổ chức. Grep code mẫu BTC: không có mã nào trong số này xuất hiện. Bot mẫu
chỉ phân biệt HTTP 200/425/429. Khi biết mã thật, đổi ở đây một chỗ.
"""


class MatchError(Exception):
    def __init__(self, code: str, detail: str = ""):
        super().__init__(f"{code}: {detail}" if detail else code)
        self.code = code
        self.detail = detail


E_BAD_FORMAT = "E_BAD_FORMAT"
E_NOT_ADJACENT = "E_NOT_ADJACENT"
E_POND = "E_POND"
E_STEP_OVERFLOW = "E_STEP_OVERFLOW"
E_NO_FUEL = "E_NO_FUEL"
