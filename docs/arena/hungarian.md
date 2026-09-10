# Arena Hungarian

Kết quả được tạo bằng `python3 -m tools.arena` trên simulator offline và mock HTTP server localhost. Không gọi judge thật, không dùng credential.

| Map | Điểm | Số ngày chạy | Action lỗi |
|---|---|---:|---:|
| default | `udon_types=4`, `daily_types_sum=16`, `udon_total=16` | 4/4 | 0 |
| **Trung bình** | **4 / 16 / 16** | **4/4** | **0** |

## Phạm vi đo

- Fixture: `fixtures/default.json`.
- Bot: `hexudon-bot-cpp/bot`.
- Simulator hiện là mô hình observed/offline; traffic động, đối thủ, tiếp tế và một số luật scoring chưa được mô phỏng đầy đủ.
- Baseline bot BTC trên cùng fixture đạt `4 / 15 / 12`; Hungarian đạt `4 / 16 / 16` trong arena này.
- Chưa dùng kết quả này để khẳng định hiệu quả trên judge thật.
