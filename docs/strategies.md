# Kho chiến thuật

## Bài này tên là gì

Bài HEXUDON là một biến thể của **Team Orienteering Problem (TOP)**: có nhiều xe,
mỗi điểm mang một phần thưởng, mỗi xe có giới hạn thời gian/quãng đường, mục tiêu
là chọn tập điểm nào để đi sao cho tổng thưởng lớn nhất. Thêm hai lớp nữa:

- **Green VRP / EVRP** — xe có bình nhiên liệu hữu hạn, phải ghé trạm nạp. Đúng
  với xe tuần tra + xe tiếp tế.
- **Chi phí thay đổi theo thời gian** — traffic làm chi phí ngày mai khác ngày nay.

Nghĩa là không phải phát minh gì. TOP là bài NP-hard đã được nghiên cứu 30 năm,
có sẵn cả một thang thuật toán từ dễ đến khó.

---

## Thang chiến thuật

Đây **không phải** các lựa chọn thay thế nhau. Chúng **xếp chồng**: tầng dưới
sinh lời giải ban đầu, tầng trên cải thiện nó. Một bot mạnh dùng cả ba tầng.

### Tầng 1 — Dựng lời giải ban đầu

| Cách | Ý tưởng | Chi phí cài | Ghi chú |
|---|---|---|---|
| **Greedy tỉ lệ** | Chọn spot có `giá trị / chi phí đến nơi` cao nhất | Thấp | Điểm xuất phát. Bot mẫu còn chưa làm được cái này |
| **Gán Hungarian** | Gán xe ↔ spot tối ưu toàn cục bằng thuật toán Hungary, `O(n³)` | Thấp | **Thắng greedy gần như luôn**, vì greedy để hai xe cùng nhắm một spot — đúng lỗi bot mẫu đang mắc |
| **Insertion heuristic** | Chèn dần spot vào tuyến đang có, chọn chỗ chèn rẻ nhất | Trung bình | Chuẩn của VRP |

### Tầng 2 — Cải thiện cục bộ

| Cách | Ý tưởng | Chi phí cài |
|---|---|---|
| **2-opt / Or-opt** | Đảo một đoạn tuyến, hoặc dời một spot sang chỗ khác | Thấp |
| **Simulated Annealing** | Nhận cả thay đổi xấu với xác suất giảm dần để thoát cực trị địa phương | Trung bình |
| **Tabu Search** | Cấm quay lại nước vừa đi để không lặp | Trung bình |
| **ALNS** (Adaptive Large Neighborhood Search) | Phá một phần lời giải rồi dựng lại, tự học toán tử nào hiệu quả | Cao |

ALNS là thứ mạnh nhất trong tài liệu TOP hiện nay.

### Tầng 3 — Tìm kiếm trên chuỗi hành động

| Cách | Ý tưởng | Hợp với bài này? |
|---|---|---|
| **Beam search** | Giữ `W` phương án tốt nhất ở mỗi độ sâu, mở rộng tiếp | **Rất hợp.** Bài có ngân sách bước rời rạc, đúng dạng beam search giỏi |
| **MCTS** | Mô phỏng ngẫu nhiên rồi ưu tiên nhánh tốt | Chỉ đáng nếu có đối kháng thật. Chưa xác minh là có |
| **Giải thuật di truyền** | Tiến hoá quần thể chuỗi lệnh | Phổ biến ở contest, nhưng tốn thời gian cài hơn beam search |

### Tầng 4 — Đặc thù bài này

- **Rolling horizon** — lập kế hoạch `N` ngày, chỉ thực thi ngày đầu, hôm sau lập
  lại từ state mới. Đây là cách đúng để đối phó traffic thay đổi, thay vì lập kế
  hoạch cứng cả trận.
- **Lịch tiếp nhiên liệu (Green VRP)** — coi xe tiếp tế như trạm nạp di động, lên
  lịch điểm hẹn.
- **Tránh xung đột (MAPF)** — hai xe cùng đội không dẫm lên tuyến nhau, và không
  tự làm tắc đường của chính mình.

---

## Kinh nghiệm từ contest heuristic

Meta lặp đi lặp lại ở các giải AI contest: **dùng Greedy hoặc Beam Search để ra
kế hoạch tổng thể, rồi Simulated Annealing để tinh chỉnh chi tiết.** Có người
chạy beam search sâu 60, rộng 1000, có loại trùng.

Điều kiện tiên quyết của mọi thứ trên: **một hàm chấm điểm nhanh và đúng.**
Beam search rộng 1000 nghĩa là chấm điểm cả nghìn lời giải mỗi lượt. Không có
simulator nhanh thì không tầng nào chạy được.

Đây là lý do simulator ở [`plan/KIENNT.md`](plan/KIENNT.md) phải làm trước.

---

## Lộ trình đề xuất

Leo thang, mỗi bậc đo bằng arena trước khi lên bậc sau:

1. **Dijkstra** thay `bfs()` — sửa bug, không phải cải tiến.
2. **Gán Hungarian** thay greedy — hết cảnh hai xe cùng nhắm một spot.
3. **Rolling horizon** — lập kế hoạch nhiều ngày, thực thi một ngày.
4. **Beam search** trên chuỗi lệnh trong ngày.
5. **SA hoặc ALNS** tinh chỉnh tuyến.

Bậc 1–2 rẻ và thắng nhiều nhất. Đừng nhảy thẳng lên bậc 5.

## Nguồn

- [A survey of the orienteering problem](https://arxiv.org/html/2512.16865v1)
- [Metaheuristics for the team orienteering problem](https://link.springer.com/article/10.1007/s10732-006-9004-0)
- [Hybrid ALNS for the team orienteering problem](https://www.sciencedirect.com/science/article/abs/pii/S0305054820301519)
- [Green VRP: heuristic based exact solution approach](https://www.sciencedirect.com/science/article/abs/pii/S1568494615007085)
- [Beam Monte-Carlo Tree Search](https://dke.maastrichtuniversity.nl/m.winands/documents/CIG2012_paper_32.pdf)
- [A Survey of Monte Carlo Tree Search Methods](http://www.incompleteideas.net/609%20dropbox/other%20readings%20and%20resources/MCTS-survey.pdf)
- [CodinGame — Exploring the Possibles with a Stochastic Algorithm](https://www.codingame.com/blog/stochastic-algorithm-smash-the-code/)
- [Sakana AI wins AtCoder Heuristic Contest](https://sakana.ai/ahc058/)

---

## Mỗi chiến thuật một nhánh, một release

### Quy ước

| | Mẫu | Ví dụ |
|---|---|---|
| Nhánh | `strategy/<tên>` | `strategy/beam-search` |
| Tag | `bot-<tên>-v<N>` | `bot-beam-search-v1` |
| File | `strategy/<tên>.hpp` | `main.cpp` |

```bash
git switch -c strategy/beam-search
# ... code, đo bằng arena ...
git push -u origin strategy/beam-search
git tag bot-beam-search-v1
git push origin bot-beam-search-v1     # -> workflow tự build và tạo release
```

[`.github/workflows/release.yml`](../.github/workflows/release.yml) sẽ build nhị
phân Linux + Windows, chạy smoke test, rồi tạo GitHub Release đính kèm cả hai.

### Nhánh chiến thuật chỉ được đụng vào file của nó

Nếu hai nhánh cùng sửa `main.cpp` thì merge sẽ đau. Nhánh chiến thuật chỉ nên
chạm:

- `hexudon-bot-cpp/strategy/<tên>.hpp` — chiến thuật của nó
- một dòng đăng ký trong bộ chọn của `planActions()`
- `docs/arena/<tên>.md` — điểm arena

Sửa `strategy/common.hpp` phải tách PR riêng, vì nó ảnh hưởng mọi chiến thuật.

### Release không có điểm arena thì vô nghĩa

Workflow đọc `docs/arena/<tên>.md` và nhét vào ghi chú phát hành. Thiếu file đó
thì release vẫn tạo nhưng bị đánh dấu cảnh báo — vì không có số thì không biết
nó hơn hay kém bản trước.

Định dạng tối thiểu của `docs/arena/<tên>.md`:

```markdown
| Map | Điểm | So với `bot-v1` |
|---|---|---|
| dense-road   | 42 | +6 |
| tight-fuel   | 31 | -2 |
| long-match   | 58 | +11 |
| **Trung bình** | **43.7** | **+5.0** |
```

### Bảng nhánh theo lộ trình

| Bậc | Nhánh | Tag đầu tiên | Chủ trì |
|---:|---|---|---|
| 1 | `strategy/dijkstra` | `bot-dijkstra-v1` | DATNT |
| 2 | `strategy/beam-search` | `bot-beam-search-v1` | KIENNT |
| 3 | `strategy/rolling-horizon` | `bot-rolling-horizon-v1` | DATNT |
| 4 | `strategy/beam-search` | `bot-beam-search-v1` | KIENNT |
| 5 | `strategy/alns` | `bot-alns-v1` | DATNT |

---

## Bằng chứng từ trận thật đầu tiên có điểm

`m-11538` — bot mẫu (BFS, chưa đọc `brand`) đấu bot AI mức `hard`.
Dữ liệu đầy đủ: [`docs/observed/first-scoring-match.json`](observed/first-scoring-match.json).

| Chỉ số (theo thứ tự ưu tiên) | Mình | AI | |
|---|---:|---:|---|
| 1. `udon_types` | **4** | **4** | hòa — cả hai mở hết 4 brand |
| 2. `daily_types_sum` | 9 | **16** | **thua ở đây** |
| 3. `udon_total` | 12 | **61** | thua đậm |
| 4. `response_ms_total` | **870** | 1018 | mình nhanh hơn |

Ba điều rút ra, đều có số làm bằng chứng:

### Trận thắng thua ở tiêu chí 2, không phải tiêu chí 1

Mở hết brand là chuyện dễ — bot ngây thơ cũng làm được. `daily_types_sum` là
**tổng lũy kế số loại theo từng ngày**, nên mở sớm ăn nhiều hơn mở muộn rất
nhiều. Mở đủ 4 loại ngay ngày 1 của trận 4 ngày cho `4+4+4+4 = 16`. Mình được
`9`, tức mở dần (khoảng `1+2+3+3`).

**Hệ quả:** hàm mục tiêu phải ưu tiên **mở brand mới càng sớm càng tốt**, chứ
không phải "cuối trận có đủ brand". Đây là thứ dễ làm sai nhất khi viết hàm
chấm điểm.

### Ngân sách tính toán còn thừa rất nhiều

`870ms` cho 4 ngày ≈ **218ms mỗi ngày**, trong khi `daySeconds` cho phép
**60 000ms**. Mới dùng hết 0,4%.

Poll `/state` 595 lần trong trận cũng không làm `response_ms_total` xấu đi —
mình vẫn nhanh hơn AI. Vậy chỉ số này đo thời gian trả lời mỗi ngày, không phải
lưu lượng request. Beam search rộng hoàn toàn nằm trong khả năng.

### Khoảng cách `udon_total` cho thấy chỗ mất mát lớn nhất

12 so với 61, gấp 5 lần. Bot mẫu chỉ thu ở spot đầu tiên gặp mỗi ngày và để hai
xe cùng nhắm một spot. Bậc 2 (gán Hungarian) nhắm thẳng vào chỗ này.

---

## Luật cộng điểm, đọc ngược từ trận có kiểm soát

`m-11542` — cho **đúng một** xe tuần tra đi vào spot `pos 0` (`brand 0`,
`stocks 3`) ở ngày 0 rồi **đứng yên** ba ngày còn lại. Ba xe kia đứng im cả trận.
Dữ liệu: [`docs/observed/probe-result.json`](observed/probe-result.json).

| | Kết quả |
|---|---|
| `udon_types` | 1 |
| `daily_types_sum` | 4 |
| `udon_total` | **4** |

Suy ra:

1. **Thu 1 phần mỗi ngày, kể cả khi đứng yên.** Không cần đi lại; ở trên spot là
   đủ. Bốn ngày cho bốn phần.
2. **Tồn kho có nạp lại.** Spot chỉ có `stocks: 3` nhưng thu được `4`, nên `stocks`
   không phải trần cộng dồn cả trận.
3. **`daily_types_sum` = tổng theo ngày của số brand đang có.** Giữ 1 loại suốt 4
   ngày cho `1+1+1+1 = 4`. Khớp với bot AI: mở đủ 4 brand từ sớm cho
   `4×4 = 16`.

### Hệ quả chiến thuật

Điểm 1 và 2 cộng lại rất đáng chú ý: **đỗ một xe trên spot là nguồn thu ổn định**.
Không phải lúc nào cũng đáng chạy vòng quanh — với trận ngắn, đưa mỗi xe tới một
brand *khác nhau* rồi đỗ lại có thể ăn hơn là đi gom nhiều spot cùng brand.

Điểm 3 nói cách thắng: **mở đủ brand càng sớm càng tốt**, vì mỗi ngày sau đó đều
được cộng lại. Mở đủ 4 brand ngày 0 hơn hẳn mở đủ vào ngày cuối.

### Còn chưa xác minh

- Đi **ngang** qua spot rồi dừng chỗ khác có thu không? Simulator hiện dùng luật
  hẹp nhất khớp bằng chứng: chỉ thu tại ô xe **dừng** cuối ngày.
- Một xe thu được nhiều spot trong một ngày không?
- Tồn kho nạp lại bao nhiêu mỗi ngày?
- Hai xe cùng đỗ trên một spot thì sao?

Mỗi câu trên chạy thêm một trận probe là trả lời được.
