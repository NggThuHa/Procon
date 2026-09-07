# Arena Hungarian

Kết quả được tạo bằng `python3 -m tools.arena` trên simulator offline và mock HTTP server localhost. Không gọi judge thật, không dùng credential.

| Map | Điểm | Số ngày chạy | Action lỗi |
|---|---|---:|---:|
| default | `udon_types=3`, `daily_types_sum=12`, `udon_total=12` | 4/4 | 0 |
| **Trung bình** | **3 / 12 / 12** | **4/4** | **0** |

## Phạm vi đo

- Fixture: `fixtures/default.json`.
- Bot: `hexudon-bot-cpp/bot`.
- Simulator hiện là mô hình observed/offline; traffic động, đối thủ, tiếp tế và một số luật scoring chưa được mô phỏng đầy đủ.
- Chưa dùng kết quả này để khẳng định hiệu quả trên judge thật.
