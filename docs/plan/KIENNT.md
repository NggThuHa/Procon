# KIENNT — Kế hoạch phụ trách API, localhost và vận hành

## Vai trò

KIENNT (tôi) phụ trách **API client, mock/localhost server, snapshot, log, retry và vận hành an toàn**. DATNT phụ trách map, chiến thuật, route và validator.

- Nhánh làm việc: `kien/infra-local`
- File cập nhật kết quả: `docs/plan/KIENNT.md`
- Luật bắt buộc: [`README.md`](../../README.md)
- Quy ước phối hợp: [`PLAN.md`](../../PLAN.md)
- Môi trường mặc định: `http://localhost:<PORT>` hoặc mock server

## Nguyên tắc vận hành

1. Phát triển và kiểm thử trên localhost/mock server; không yêu cầu deploy public.
2. Không hard-code production URL; base URL phải lấy từ environment/config.
3. Không chạy action nếu chưa kiểm tra `MATCH_ID`, `DAY`, schema và `VALIDATION: PASS` từ DATNT.
4. Timeout không đồng nghĩa request chưa được local/mock server nhận; không retry mù quáng.
5. Snapshot phải đủ để khôi phục client sau restart.
6. Mock server phải giữ hành vi gần luật thật và mô phỏng lỗi để DATNT kiểm thử.
7. Không ghi secret vào log, fixture, snapshot hoặc file bàn giao.

## Công việc cụ thể

### K1 — Local/mock server

- [ ] Chọn cách chạy server local đơn giản, có lệnh start rõ ràng.
- [ ] Cho phép cấu hình `HOST`, `PORT`, fixture và delay qua environment.
- [ ] Implement các endpoint local: `/setup`, `/assignment`, `/start`, `/state`, `/actions`, `/result`.
- [ ] Ưu tiên bind `localhost`; không mặc định mở public interface.
- [ ] Cung cấp fixture map, agents, spots, traffic, daySteps và fuelLimits.
- [ ] Có cơ chế reset trận để chạy lại test deterministic.
- [ ] Mô phỏng lỗi adjacency, pond, step, fuel, format và rate limit.

**Đầu ra:** local server chạy được bằng một lệnh và fixture tái lập được.

### K2 — API client

- [ ] Đọc `BASE_URL` từ environment; mặc định localhost.
- [ ] Tạo wrapper cho `/setup`, `/assignment`, `/start`, `/state`, `/actions`, `/result`.
- [ ] Kiểm tra status code và schema response.
- [ ] Đo request duration nhưng không log token/header.
- [ ] Phân loại validation error, server error, timeout và rate limit.
- [ ] Có mock adapter để test mà không cần mạng.

### K3 — Snapshot và log

- [ ] Lưu setup snapshot bất biến.
- [ ] Lưu state trước action, response action và state sau action.
- [ ] Mỗi snapshot có match, day, timestamp, endpoint, status và duration.
- [ ] Sanitize Authorization/API token trước khi ghi hoặc gửi cho DATNT.
- [ ] Có thể khôi phục trạng thái làm việc sau khi client restart.
- [ ] Không đưa snapshot runtime chứa dữ liệu nhạy cảm vào Git.

### K4 — Assignment và vòng đời trận

- [ ] Validate assignment đúng số agent và `kind` hợp lệ.
- [ ] Gọi assignment một lần, không đổi kind sau khi accepted.
- [ ] Poll `/start` với delay cấu hình được.
- [ ] Điều phối state/action/result đúng thứ tự.
- [ ] Chặn action nếu day hoặc state không khớp.
- [ ] Chạy toàn bộ flow bằng local fixture trước khi tích hợp thật.

### K5 — Retry, rate limit và lỗi

- [ ] Mô phỏng `429`/`E_RATE_LIMIT` và backoff có giới hạn.
- [ ] Không retry `/actions` khi request trước ở trạng thái unknown.
- [ ] Sau timeout, lấy lại state/response trước khi quyết định.
- [ ] Ghi correlation id nếu có.
- [ ] Không gửi duplicate action trong cùng day.
- [ ] Giữ timeout connect và response riêng biệt.

### K6 — Kiểm thử tích hợp với DATNT

- [ ] Setup fixture thiếu field.
- [ ] Assignment/action sai số lượng agent.
- [ ] Action bị DATNT validator từ chối.
- [ ] Action hợp lệ được áp dụng và state thay đổi đúng.
- [ ] Các lỗi `E_NOT_ADJACENT`, `E_POND`, `E_STEP_OVERFLOW`, `E_NO_FUEL`, `E_BAD_FORMAT`.
- [ ] Timeout, rate limit và restart từ snapshot.
- [ ] Kiểm tra log không chứa secret.

## Giao diện nhận action từ DATNT

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

KIENNT không gửi nếu thiếu `VALIDATION: PASS`, sai match/day hoặc state đã cũ.

## Giao diện gửi state cho DATNT

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

## Báo cáo sau mỗi ngày mô phỏng

```text
DAY: <day>
SETUP_STATUS: <ok/error>
STATE_STATUS: <ok/error>
ACTION_STATUS: <accepted/rejected/unknown>
RESPONSE_TIME_MS: <number>
RETRY_COUNT: <number>
SNAPSHOT: <path/reference>
ERRORS: <...>
NEXT_OPERATION: <...>
```

## Checklist trước khi gửi action local

- [ ] Local/mock server đang chạy.
- [ ] Fixture/state mới nhất đã được lưu.
- [ ] Đúng match và day.
- [ ] Đủ action cho mọi agent, đúng thứ tự.
- [ ] DATNT đã trả `VALIDATION: PASS`.
- [ ] Validator tích hợp pass.
- [ ] Không có request trước ở trạng thái unknown.
- [ ] Rate limit đủ an toàn.
- [ ] Response/state sau action được lưu.
- [ ] Log đã sanitize và không chứa token.

## Giả định cần xác minh

- [ ] Chu kỳ chính xác của `/start`, `/state` và `/actions` trong mock server.
- [ ] State nào chứng minh action đã được áp dụng.
- [ ] Chính sách retry/idempotency.
- [ ] Schema đầy đủ của traffic và result.
- [ ] Điều kiện chuyển sang ngày tiếp theo.
- [ ] Cách tính thời gian phản hồi.
