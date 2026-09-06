"""Lỗi khi một chuỗi lệnh không hợp lệ.

CẢNH BÁO: các mã `E_*` dưới đây là **tên do team đặt**, KHÔNG phải mã lỗi của
ban tổ chức.

`[observed]` Server thật KHÔNG dùng mã lỗi. `POST /actions` luôn trả 200 kèm:

    {"protocol_version": "v0.1-draft", "type": "action_result",
     "match_id": "m-11542", "day": 1, "valid": true, "reason": "",
     "submission_id": "team-A-d1-2", "response_ms": 95}

Hợp lệ hay không nằm ở `valid` (bool) và `reason` (chuỗi tự do), không phải
ở HTTP status hay mã lỗi. Nghĩa là:

- Không được coi `POST /actions` trả 200 là action đã được chấp nhận —
  phải đọc `valid`.
- `response_ms` chính là thứ cộng dồn thành `response_ms_total`, tiêu chí
  xếp hạng thứ 4.

Các mã dưới đây chỉ dùng nội bộ trong simulator để nói rõ *vì sao* một chuỗi
lệnh bị từ chối. Đừng so chúng với chuỗi `reason` của server.
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
