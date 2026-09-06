# PLAN — PTIT PROCON 2026

## Mục đích

Tài liệu này là bản điều phối chung. Luật thi đấu nằm ở [`README.md`](README.md); công việc chi tiết được tách theo người:

- **KIENNT (tôi):** [`docs/plan/KIENNT.md`](docs/plan/KIENNT.md) — API client, mock/localhost server, vận hành, snapshot và reliability.
- **DATNT:** [`docs/plan/DATNT.md`](docs/plan/DATNT.md) — map, chiến thuật, route, validator và simulator.

## Môi trường chạy

- Phát triển và kiểm thử mặc định trên **localhost** hoặc mock server.
- Không yêu cầu deploy public, domain thật hay gọi server thi đấu từ Internet trong giai đoạn phát triển.
- Base URL mặc định khi test local: `http://localhost:<PORT>`.
- Dữ liệu test phải là fixture/snapshot giả lập, không chứa token thật.
- Chỉ chuyển sang server thi đấu khi team chủ động xác nhận và có cấu hình riêng; không hard-code URL production.

## Quy tắc làm việc song song

1. KIENNT làm trên nhánh `kien/infra-local`.
2. DATNT làm trên nhánh `dat/route-planner`.
3. Không cùng sửa một file nếu không cần thiết.
4. DATNT chỉ bàn giao action sau khi validator trả `PASS` trên fixture/local state.
5. KIENNT không chạy action nếu thiếu `VALIDATION: PASS`, sai `MATCH_ID/day` hoặc state đã cũ.
6. Sau mỗi ngày mô phỏng, KIENNT lưu snapshot và gửi state mới cho DATNT; DATNT không dùng route cũ một cách mù quáng.
7. Mọi giả định chưa xác minh phải ghi rõ trong file của người phụ trách.
8. Không commit/in log `API_TOKEN`, Authorization header hoặc dữ liệu bí mật.
9. Mock server phải mô phỏng lỗi quan trọng: adjacency, pond, step, fuel, format và rate limit.

## Ranh giới trách nhiệm

| Việc | KIENNT (tôi) | DATNT |
|---|---|---|
| HTTP/WebSocket client | Chính | Dùng để tích hợp |
| Mock/local server và fixture | Chính | Cung cấp case luật cần mô phỏng |
| Setup/assignment/start/state/result | Chính | Kiểm tra dữ liệu đầu vào |
| Snapshot, log, retry, rate limit | Chính | Gửi yêu cầu edge case |
| Hiểu luật, map, neighbor | Hỗ trợ môi trường test | Chính |
| Chọn brand/spot và tính route | Cung cấp state/fixture | Chính |
| Validator/simulator | Chạy tích hợp | Chính |
| Gửi `/actions` trong local | Chính sau khi validator pass | Tạo payload |
| Phân tích sau ngày/trận | Chính về log/runtime | Chính về chiến thuật/route |

## Quy trình mỗi ngày mô phỏng

```text
KIENNT: khởi động localhost/mock server
KIENNT: GET /state → lưu snapshot
KIENNT: gửi state/traffic/fuel/position cho DATNT
DATNT: chọn mục tiêu brand/spot
DATNT: tính route và chạy validator
DATNT: bàn giao action + chi phí + rủi ro
KIENNT: kiểm tra match/day/schema/validator
KIENNT: POST /actions lên localhost một lần
KIENNT: lưu response/state sau action
Cả hai: đánh giá kết quả và cập nhật fixture/route cho ngày sau
```

## Format bàn giao tối thiểu

### DATNT → KIENNT

```text
MATCH_ID: <id>
DAY: <day>
ACTION: <JSON array>
VALIDATION: PASS
TOTAL_STEPS: <per-agent>
FUEL_REQUIRED: <per-agent>
TARGETS: <spot/brand>
FALLBACK: <action dự phòng>
ASSUMPTIONS: <giả định>
RISKS: <rủi ro>
```

### KIENNT → DATNT

```text
MATCH_ID: <id>
DAY: <day>
STATE_SNAPSHOT: <reference>
AGENTS: <position/fuel/kind>
TRAFFIC: <summary>
SERVER_STATUS: LOCAL_READY|ERROR|UNKNOWN
LAST_RESPONSE: <sanitized response>
CONSTRAINTS: <step/fuel/rate-limit>
ASSUMPTIONS: <giả định>
```

## Definition of Done cho action local

- [ ] Local/mock server đang chạy và endpoint đã được kiểm tra.
- [ ] Fixture state mới nhất đã được lưu.
- [ ] Đúng match và day.
- [ ] Đủ action cho mọi agent, đúng thứ tự.
- [ ] Neighbor, ao, bước, traffic và fuel đã được kiểm tra.
- [ ] DATNT đã trả `VALIDATION: PASS`.
- [ ] KIENNT kiểm tra schema và gửi đúng một lần.
- [ ] Response/state sau action được lưu.
- [ ] Có test cho lỗi dự kiến.
- [ ] Không chứa secret hoặc yêu cầu deploy public.
