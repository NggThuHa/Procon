# DATNT — Bản đồ, đường đi và điều phối xe

## Vai trò

Trả lời câu hỏi **"đi thế nào cho kịp"**: từ danh sách mục tiêu, sinh ra chuỗi
lệnh hợp lệ, rẻ nhất, không vượt bước và nhiên liệu. KIENNT trả lời "đi đâu thì
đáng".

Sở hữu `strategy/common.hpp` — nền chung mà cả hai chiến thuật đều dùng. Sửa file
này phải có review của KIENNT.

- Nhánh: `dat/strategy-multiday`
- Chiến thuật sở hữu: `strategy/multi_day.hpp`
- Luật: [`README.md`](../../README.md) · Phối hợp: [`PLAN.md`](../PLAN.md)

---

## Giai đoạn 0 — Phần việc của DATNT

- [ ] Hỏi BTC: **điều kiện chính xác để xe tiếp tế nạp nhiên liệu** — cùng ô? cùng
      lúc? có tốn lượt không? Đây là thứ chặn toàn bộ chiến thuật `multi_day`.
- [ ] Hỏi BTC: công thức traffic giữa các ngày, và mã lỗi thật khi action sai.
- [ ] Xác nhận parity lục giác bằng dữ liệu thật: gửi một nước đi, đối chiếu `pos`
      trong `/state` kế tiếp với dự đoán.

---

## Giai đoạn 1 — Nền chung (`strategy/common.hpp`)

### C1 — Mô hình bản đồ

- [ ] Neighbor 6 hướng theo parity EVEN-R.
      Đã có bản tham chiếu đã test ở [`tools/rules.py`](../../tools/rules.py) —
      đối chiếu với nó, và test cả 6 hướng × mọi ô, kiểm tra tính đối xứng
      (đi hướng `d` rồi đi hướng ngược lại phải về chỗ cũ).
- [ ] Chi phí bước và nhiên liệu tính theo **ô nguồn**, không phải ô đích.
- [ ] Ao và ô ngoài map bị loại khỏi đồ thị.

### C2 — Tìm đường (ưu tiên cao nhất trong Giai đoạn 1)

- [ ] **Thay `bfs()` bằng Dijkstra.** Hàm hiện tại tìm đường ít ô nhất, không phải
      rẻ nhất. Với đất=2, đường=1, núi=3, đường đi nó chọn đang sai — đây là bug
      thật, không phải cải tiến.
- [ ] Trọng số lấy từ traffic của state hiện tại, không dùng lại của ngày trước.
- [ ] Trả về cả chi phí bước và chi phí nhiên liệu, để tầng chọn mục tiêu cân nhắc.

### C3 — Validator

Chạy trước mọi lần gửi. Một action sai có thể làm hỏng cả submission.

- [ ] Đúng số agent, đúng thứ tự.
- [ ] Lệnh đi thuộc `0..5`, lệnh chờ là số âm.
- [ ] Mỗi bước tới ô thực sự kề, không vào ao, không ra ngoài map.
- [ ] Tổng bước không vượt `daySteps[day]`.
- [ ] Xe tuần tra đủ nhiên liệu suốt lộ trình.
- [ ] Vị trí kết thúc khớp với dự đoán.

Validator này phải **độc lập với simulator của KIENNT**. Hai bên cùng bug thì mất
tác dụng kiểm tra chéo — đó là điểm mạnh duy nhất của việc có hai bản.

---

## Giai đoạn 2 — Bậc 1, 3 và 5 của thang

Cả hai người xây **một bot chung**, mỗi người sở hữu vài bậc trên thang ở
[`strategies.md`](../strategies.md). Các bậc xếp chồng, không thay thế nhau.

### Bậc 1 — Dijkstra (đã nằm trong C2)

Bậc đầu tiên và là bậc bắt buộc: đây là **sửa bug**, không phải cải tiến.

### Bậc 3 — Rolling horizon (`strategy/rolling_horizon.hpp`)

Lập kế hoạch `N` ngày, chỉ thực thi ngày đầu, hôm sau lập lại từ state mới.

- [ ] Đây là cách đúng để đối phó traffic thay đổi — kế hoạch cứng cả trận sẽ sai
      ngay từ ngày thứ hai.
- [ ] Lồng lịch tiếp nhiên liệu vào: xe tiếp tế đón xe tuần tra ở đâu, ngày nào.
- [ ] Tính traffic do **chính mình** tạo ra — dồn nhiều xe qua một ô đường sẽ làm
      chính mình chậm ngày hôm sau.
- [ ] `N` chỉnh được, đo bằng arena.

### Bậc 5 — ALNS (`strategy/alns.hpp`)

Adaptive Large Neighborhood Search: phá một phần lời giải rồi dựng lại, tự học
toán tử nào hiệu quả. Đây là thứ mạnh nhất trong tài liệu Team Orienteering
Problem hiện nay.

- [ ] Toán tử phá: bỏ ngẫu nhiên, bỏ theo cụm địa lý, bỏ spot đắt nhất.
- [ ] Toán tử dựng: chèn tham lam, chèn hối tiếc.
- [ ] Chấp nhận lời giải xấu hơn theo kiểu Simulated Annealing.

Chỉ làm khi bậc 1–4 đã xong và còn thời gian. Đắt nhất trên thang.

---

## Giao diện

```cpp
// strategy/rolling_horizon.hpp, strategy/alns.hpp
Plan planRollingHorizon(const Setup&, const State&);
Plan planAlns(const Setup&, const State&);
```

Mỗi bậc một nhánh, một release: xem
[quy ước](../strategies.md#mỗi-chiến-thuật-một-nhánh-một-release).

---

## Giả định cần xác minh

- [ ] Lệnh chờ `-N` có tiêu tốn đúng `N` bước trong ngân sách không.
- [ ] Xe tiếp tế có bị giới hạn bước không.
- [ ] Điều kiện nạp nhiên liệu từ xe tiếp tế.
- [ ] Công thức traffic giữa các ngày.
- [ ] Chuyện gì xảy ra khi hai xe cùng đội vào cùng một ô.
