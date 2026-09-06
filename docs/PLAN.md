# PLAN — PTIT PROCON 2026

## Tại sao plan này khác bản cũ

Bản cũ chia việc theo tầng: một người làm hạ tầng (mock server, API client,
snapshot), một người làm chiến thuật. Chia như vậy sai, vì:

1. **Hạ tầng đã được BTC giải sẵn.** `hexudon-bot-cpp/` nối server, parse JSON,
   chạy đủ vòng đời. README của BTC nói thẳng: *"Chỉ cần sửa hàm `planActions()`
   để cải tiến chiến thuật."* Người làm hạ tầng sẽ ngồi không.
2. **Điểm chỉ đến từ `planActions()`.** Không có điểm nào cho mock server.

Nên cả hai người đều làm chiến thuật, chia theo **hướng chuyên môn khác nhau**
để phủ được nhiều dạng đề, rồi để simulator chấm xem hướng nào thắng.

## Cảnh báo phải đọc trước

Mục "Luật chơi" trong [`README.md`](../README.md) **phần lớn không có nguồn từ
BTC**. Lịch sử Git cho thấy nó do AI viết ra ở commit `8d98369`, không phải
trích từ đặc tả. Đối chiếu với code mẫu BTC:

| Đã xác minh (có trong `main.cpp`) | Chưa có nguồn |
|---|---|
| Vòng đời 6 endpoint | `brand`, `stocks` — 0 lần xuất hiện |
| `pos = row*width + col` | Thứ tự xếp hạng, cách tính điểm |
| 6 mã hướng, parity EVEN-R | Mã lỗi `E_*` — 0 lần xuất hiện |
| Bảng chi phí bước/nhiên liệu | Điều kiện tiếp tế, cách nạp stock |
| Lệnh chờ `-N`, status 425/429 | Công thức traffic giữa các ngày |

**Không tối ưu chiến thuật dựa trên cột phải cho tới khi Giai đoạn 0 xong.**
Tối ưu theo luật bịa là cách chắc chắn nhất để thua.

---

## Giai đoạn 0 — Xác minh luật (chặn mọi thứ khác, cả hai cùng làm)

Không code chiến thuật cho tới khi xong. Việc cụ thể:

1. Xin BTC bản đặc tả chính thức: cách tính điểm, ý nghĩa `brand`/`stocks`,
   điều kiện xe tiếp tế, mã lỗi thật.
2. Trả lời câu quyết định: **BTC có judge để tập không?** Nếu có, bao nhiêu trận
   thử, giới hạn request bao nhiêu. Câu trả lời quyết định Giai đoạn 1 làm gì.
3. Chạy bot mẫu một trận, **dump nguyên văn** response `/setup` và `/state` vào
   `docs/observed/`. Đó mới là schema thật.
4. Sửa `README.md`: xoá phần bịa, mỗi mục còn lại ghi rõ nguồn
   (`[BTC spec]`, `[main.cpp]`, `[quan sát từ server]`).

**Xong khi:** mỗi dòng luật trong README đều truy được về một nguồn.

---

## Giai đoạn 1 — Nền chung (cả hai thống nhất, không ai được đi trước)

Đây là phần cả hai chiến thuật đều dùng. Làm chung để không có hai bản map model
lệch nhau.

| Việc | Vì sao | Chủ trì |
|---|---|---|
| Thay BFS bằng Dijkstra | `bfs()` hiện tìm đường **ít ô nhất**, không phải **rẻ nhất**. Đất=2, đường=1, núi=3 → đường đi hiện tại sai | DATNT |
| Validator trước khi gửi | Một action sai có thể hỏng cả submission | DATNT |
| Simulator + scorer offline | Cách duy nhất so hai chiến thuật mà không phải đợi trận thật | KIENNT |
| Bộ map thử nghiệm | Cần nhiều dạng đề để test, không chỉ một fixture | KIENNT |

**Xong khi:** chạy được `simulate(map, strategy) -> điểm`, và hai chiến thuật
rỗng cho ra cùng kết quả.

---

## Giai đoạn 2 — Chia hướng

Mỗi người sở hữu một tầng quyết định và các bậc thang chiến thuật thuộc tầng đó.
Danh mục đầy đủ ở [`strategies.md`](strategies.md) — bài này là **Team Orienteering
Problem**, có sẵn thang thuật toán từ dễ đến khó, không cần tự nghĩ ra.

Mỗi chiến thuật một nhánh, một release. Quy ước ở
[`strategies.md`](strategies.md#mỗi-chiến-thuật-một-nhánh-một-release).

| | KIENNT | DATNT |
|---|---|---|
| Tầng sở hữu | Chọn mục tiêu, chấm điểm | Bản đồ, đường đi, điều phối xe |
| Câu hỏi trả lời | *Đi đâu thì đáng?* | *Đi thế nào cho kịp?* |
| Chiến thuật | Bậc 2, 4 của thang | Bậc 1, 3, 5 của thang |
| Mạnh ở dạng đề | Trận ngắn, ngân sách bước chặt, spot rải rác | Trận dài, nhiên liệu chặt, cần xe tiếp tế |
| Nhánh đầu | `strategy/hungarian` | `strategy/dijkstra` |
| File | [`docs/plan/KIENNT.md`](plan/KIENNT.md) | [`docs/plan/DATNT.md`](plan/DATNT.md) |

### Rủi ro của cách chia này, và cách chặn

Chia theo hướng dễ dẫn tới **hai con bot làm dở dang thay vì một con tử tế**.
Ba chốt chặn:

1. **Cùng một interface.** Cả hai implement đúng một signature, đặt ở file riêng
   để không dẫm chân nhau trong `planActions()`:

   ```
   hexudon-bot-cpp/
     strategy/common.hpp      # Giai đoạn 1: map, Dijkstra, validator (chung)
     strategy/greedy_day.hpp  # KIENNT
     strategy/multi_day.hpp   # DATNT
     main.cpp                 # planActions() chỉ còn là bộ chọn
   ```

2. **Simulator phân xử, không phải tranh luận.** Mỗi tuần chạy cả hai chiến thuật
   trên toàn bộ map thử nghiệm, đăng bảng điểm. Ai thắng nhiều dạng hơn thì đó là
   mặc định.

3. **Có hạn chót hợp nhất.** Trước ngày thi một tuần, dừng phát triển song song.
   Chọn chiến thuật thắng làm chính, chiến thuật kia chỉ còn là fallback cho dạng
   đề mà nó thắng rõ rệt. Không mang hai bot dở vào phòng thi.

---

## Quy trình làm việc

- Mỗi người làm trên nhánh của mình, mở PR vào `main`.
- File `strategy/common.hpp` là **của chung**: sửa nó thì phải có review của
  người kia, vì nó ảnh hưởng cả hai chiến thuật.
- Phát hiện luật mới (từ BTC hoặc từ response server) → cập nhật `README.md`
  trước, code sau. Báo cho người kia ngay, vì nó có thể phá giả định của họ.
- Không commit `.env`, token hay Authorization header.

## Đo tiến độ

Chỉ một con số: **điểm trung bình trên bộ map thử nghiệm**. Mọi thay đổi chiến
thuật phải kèm số trước/sau. Không có số thì không merge.
