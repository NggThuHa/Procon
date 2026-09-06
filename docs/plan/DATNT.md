# DATNT — Kế hoạch phụ trách map, chiến thuật, route và validator

## Vai trò

DATNT phụ trách **hiểu luật, mô hình map, chọn mục tiêu, tính route, validator và simulator**. KIENNT (tôi) phụ trách localhost/API và vận hành.

- Nhánh làm việc: `dat/route-planner`
- File cập nhật kết quả: `docs/plan/DATNT.md`
- Luật bắt buộc: [`README.md`](../../README.md)
- Quy ước phối hợp: [`PLAN.md`](../../PLAN.md)
- Môi trường kiểm thử: state/fixture từ localhost/mock server

## Nguyên tắc chiến thuật

1. Ưu tiên mở `brand` chưa có trước khi tối ưu thêm số phần của brand đã có.
2. Mọi route phải dựa trên state, traffic, vị trí, fuel và stock mới nhất.
3. Không tự đoán neighbor, cost, điều kiện tiếp tế hoặc cách thu spot.
4. Luôn tạo fallback đứng yên hoặc route an toàn.
5. Chạy validator trước khi bàn giao action cho KIENNT.
6. Chỉ dùng fixture/state đã được KIENNT đánh dấu `LOCAL_READY` hoặc snapshot hợp lệ.

## Công việc cụ thể

### D1 — Phân tích luật và setup

- [ ] Đọc `README.md` và ghi rõ phần nào là luật bắt buộc, phần nào là giả định.
- [ ] Đọc `map.width`, `map.height`, `map.cells`, agents, spots, daySteps và fuelLimits từ fixture/state.
- [ ] Xác định brand đã có, brand còn thiếu và stock.
- [ ] Ghi các điểm chưa xác minh vào mục cuối file.

### D2 — Mô hình bản đồ lục giác

- [ ] Xác nhận `pos = row * width + column`.
- [ ] Xây dựng bảng neighbor cho 6 hướng `0..5` theo parity.
- [ ] Test ô giữa, ô biên, ô ngoài map và ao.
- [ ] Không coi `pos + 1`/`pos - 1` mặc định là neighbor.
- [ ] Bàn giao bảng neighbor và case test cho KIENNT.

### D3 — Chọn mục tiêu và xếp hạng route

Xếp route theo thứ tự:

1. Mở được nhiều brand mới hơn.
2. Đóng góp tốt hơn vào brand tích lũy theo ngày.
3. Thu nhiều phần hơn nếu hai lựa chọn trên tương đương.
4. Có biên an toàn bước/fuel lớn hơn.
5. Ít phụ thuộc vào timing hỗ trợ hơn.

Với mỗi route phải tính vị trí sau từng lệnh, chi phí bước theo ô nguồn, fuel, spot đầu tiên ghé và vị trí kết thúc.

### D4 — Validator/simulator

Validator bắt buộc kiểm tra:

- [ ] JSON hợp lệ.
- [ ] Số action ngoài đúng số agent và đúng thứ tự.
- [ ] Lệnh đi thuộc `0..5`; lệnh chờ có format `-N`.
- [ ] Mỗi bước tới ô kề hợp lệ.
- [ ] Không vào ao/ra ngoài map.
- [ ] Không vượt `daySteps[day]`.
- [ ] Xe tuần tra đủ fuel.
- [ ] Điều kiện xe tiếp tế không bị vi phạm.
- [ ] Route kết thúc tại vị trí dự kiến.

Lỗi tối thiểu: `E_BAD_FORMAT`, `E_NOT_ADJACENT`, `E_POND`, `E_STEP_OVERFLOW`, `E_NO_FUEL`.

### D5 — Bộ test route

- [ ] Cả 6 hướng ở ô giữa và ô biên.
- [ ] Ao chắn đường.
- [ ] Route vừa đủ/vượt bước một đơn vị.
- [ ] Route vừa đủ/vượt fuel một đơn vị.
- [ ] Lệnh đứng yên chiếm ngân sách theo luật fixture.
- [ ] Nhiều agent cùng đến một spot.
- [ ] Xe tiếp tế đứng yên/chờ.
- [ ] Traffic ngày sau làm route cũ không còn hợp lệ.
- [ ] Fallback đứng yên serialize được.
- [ ] Chạy validator với fixture lỗi do KIENNT cung cấp.

## Giao diện bàn giao cho KIENNT

DATNT gửi action theo mẫu:

```text
MATCH_ID: <id>
DAY: <day>
ACTION: <JSON array>
VALIDATION: PASS
TOTAL_STEPS: <per-agent>
FUEL_REQUIRED: <per-agent>
TARGETS: <spot/brand>
PREDICTED_END_POS: <per-agent>
FALLBACK: <action dự phòng>
ASSUMPTIONS: <giả định>
RISKS: <rủi ro>
```

Nếu validator fail, phải sửa route và gửi lại payload. Không ghi token hoặc dữ liệu bí mật vào file bàn giao.

## Giao diện nhận state từ KIENNT

```text
MATCH_ID: <id>
DAY: <day>
STATE_SNAPSHOT: <reference>
AGENTS: <position/fuel/kind>
TRAFFIC: <summary>
SPOTS_AND_STOCKS: <summary>
SERVER_STATUS: LOCAL_READY|ERROR|UNKNOWN
LAST_RESPONSE: <sanitized response>
CONSTRAINTS: <step/fuel/rate-limit>
ASSUMPTIONS: <giả định>
```

Không tính route từ state có `ERROR`, `UNKNOWN` hoặc day không khớp.

## Báo cáo sau mỗi ngày mô phỏng

```text
DAY: <day>
BRANDS_BEFORE: <...>
BRANDS_TARGETED: <...>
BRANDS_COLLECTED: <...>
ACTION_STATUS: <accepted/rejected/unknown>
ROUTE_RESULT: <summary>
ASSUMPTIONS_CONFIRMED: <...>
ISSUES: <...>
NEXT_CHANGE: <...>
```

## Giả định cần xác minh

- [ ] Công thức neighbor chính xác theo parity của fixture.
- [ ] Cách mock server tính lệnh `-N` vào `daySteps`.
- [ ] Điều kiện chính xác để xe tiếp tế nạp fuel.
- [ ] Điều kiện “ghé spot” và cách xử lý nhiều xe cùng spot.
- [ ] Công thức traffic giữa các ngày.
- [ ] Tie-break đầy đủ sau thời gian phản hồi.
