# KIENNT — Chọn mục tiêu, chấm điểm và công cụ đo

## Vai trò

Trả lời câu hỏi **"đi đâu thì đáng"**: nhìn state, quyết định mỗi xe nên nhắm
spot nào. DATNT trả lời "đi thế nào cho kịp".

Ngoài ra sở hữu **công cụ đo** — simulator và arena — vì mọi quyết định chiến
thuật của cả hai người đều phải được chấm bằng công cụ này.

- Chiến thuật hiện tại: `planActions()` trong `hexudon-bot-cpp/main.cpp`
- Luật: [`README.md`](../../README.md) · Phối hợp: [`PLAN.md`](../PLAN.md)

> Vai trò cũ (mock server, API client, snapshot, retry) đã bị cắt. Lý do trong
> [`PLAN.md`](../PLAN.md): BTC đã giải xong tầng transport.

---

## Giai đoạn 0 — Phần việc của KIENNT

- [ ] Hỏi BTC: **cách tính điểm chính xác**. Đây là thứ chặn toàn bộ tầng chọn
      mục tiêu — không biết chấm điểm thế nào thì không biết "đáng" nghĩa là gì.
- [ ] Hỏi BTC: `brand` và `stocks` có thật không, kiểu dữ liệu gì, nạp lại ra sao.
- [ ] Hỏi BTC: có judge để tập không, giới hạn bao nhiêu.
- [ ] Chạy bot mẫu một trận, lưu `/setup` và `/state` nguyên văn vào
      `docs/observed/`.

---

## Giai đoạn 1 — Simulator và arena

Đây là phần hạ tầng **duy nhất** còn sống, và nó sống vì một lý do cụ thể: không
có cách nào khác để biết chiến thuật A có hơn chiến thuật B không.

Phân biệt rõ với thứ đã cắt:

| | Mock server (đã cắt) | Simulator (giữ) |
|---|---|---|
| Giả lập | Đường truyền HTTP | Luật trò chơi |
| Thay thế | `http.hpp` — BTC đã làm xong | Không có gì thay được |
| Dùng để | Không gì cả | Chấm điểm chiến thuật offline |

### S1 — Simulator

- [ ] Nhận `(map, setup, chiến thuật)` → chạy hết số ngày → trả điểm.
- [ ] Chỉ mô phỏng phần **đã xác minh**: tính hợp lệ của nước đi, chi phí bước,
      nhiên liệu. Phần chấm điểm để sau Giai đoạn 0.
- [ ] Mỗi giả định chưa xác minh phải là một hằng số đặt riêng một chỗ, không rải
      khắp code — để sửa một lần khi có luật thật.
- [ ] Deterministic: cùng input phải ra cùng output.

### S2 — Bộ map thử nghiệm

Không tối ưu trên một fixture duy nhất. Cần ít nhất các dạng:

- [ ] Map nhiều đường vs map nhiều núi/ao.
- [ ] Ngân sách bước chặt vs rộng.
- [ ] Nhiên liệu chặt (bắt buộc dùng xe tiếp tế) vs rộng.
- [ ] Trận ngắn (3–5 ngày) vs dài (15+ ngày).
- [ ] Spot tập trung một góc vs rải đều.

### S3 — Arena

- [ ] Chạy mọi chiến thuật × mọi map, in bảng điểm.
- [ ] **Không cần API, không cần mạng** — chạy hoàn toàn offline.
- [ ] Xuất bảng so sánh dạng markdown để dán vào PR.

---

## Giai đoạn 2 — Bậc 2 và 4 của thang

Không phải "chiến thuật riêng của KIENNT đấu với chiến thuật của DATNT". Cả hai
xây **một bot chung**, mỗi người sở hữu vài bậc trên thang ở
[`strategies.md`](../strategies.md). Các bậc xếp chồng lên nhau, không thay thế nhau.

### Bậc 2 — Gán mục tiêu toàn cục

Hungarian đã bỏ. `planActions()` hiện dùng beam search trực tiếp để phân bổ route
cho các xe tuần tra, tránh phụ thuộc vào `strategy/hungarian.hpp`.

### Bậc 4 — Beam search (`main.cpp`)

Giữ `W` phương án tốt nhất ở mỗi độ sâu, mở rộng tiếp. Hợp với bài này vì ngân
sách bước rời rạc.

- [ ] Trạng thái: vị trí xe, bước đã dùng, nhiên liệu, spot đã thu.
- [ ] Loại trạng thái trùng, nếu không beam sẽ đầy bản sao.
- [ ] Chiều rộng và độ sâu chỉnh được, đo bằng arena để chọn.

Chỉ làm sau khi bậc 1–3 xong và có số đo.

---

## Giao diện

`planActions()` trong `main.cpp` đọc state mới nhất, lập beam plan, validate trước
khi gửi. Phần tìm đường hiện vẫn nằm trong `main.cpp`; cần tách sang
`strategy/common.hpp` khi DATNT hoàn tất nền chung.

Mỗi bậc một nhánh, một release: xem
[quy ước](../strategies.md#mỗi-chiến-thuật-một-nhánh-một-release).

---

## Giả định cần xác minh

- [ ] `brand` có tồn tại và có tính điểm không.
- [ ] Thứ tự tie-break khi hai đội bằng điểm.
- [ ] Stock nạp lại theo ngày với số lượng bao nhiêu.
- [ ] Xe đi ngang spot có thu được không, hay phải dừng lại.
- [ ] Một xe thu được tối đa mấy spot một ngày.
