# AGENTS.md — Hướng dẫn cho AI Agent

## Mục tiêu repo

Repo dùng để phát triển bot/engine Procon và kiểm thử luật trên localhost/mock server. Không yêu cầu deploy public.

## Tài liệu bắt buộc đọc

Theo thứ tự:

1. [`README.md`](README.md) — giới thiệu và luật chơi.
2. [`PLAN.md`](PLAN.md) — phân công và quy trình phối hợp.
3. [`docs/plan/KIENNT.md`](docs/plan/KIENNT.md) — vai trò KIENNT (tôi): API, localhost, vận hành.
4. [`docs/plan/DATNT.md`](docs/plan/DATNT.md) — vai trò DATNT: map, route, validator.

## Vai trò hiện tại

- **KIENNT (tôi):** local/mock server, API client, fixtures, snapshot, log, retry, rate limit và integration flow.
- **DATNT:** luật, map lục giác, mục tiêu brand/spot, route, simulator và validator.

Không được tự đổi vai trò hoặc phân công nếu chưa có yêu cầu mới.

## Quy tắc làm việc

- Ưu tiên localhost: `http://localhost:<PORT>`.
- Không hard-code URL production và không yêu cầu deploy public.
- Không gọi Internet/server thi đấu thật trong test mặc định.
- Không đọc, in, commit hoặc đưa secret vào fixture/log/payload.
- Không đoán luật khi thiếu dữ liệu; ghi rõ `ASSUMPTION` và hỏi người phụ trách.
- Không sửa rộng ngoài phạm vi yêu cầu.
- Không đổi encoding, normalize Unicode hoặc sửa mojibake ngoài phạm vi task.
- Trước khi sửa file, đọc vùng liên quan; sau khi sửa, kiểm tra diff.
- Không sửa file generated, lock, vendor hoặc binary nếu không được yêu cầu.

## Quy tắc Procon bắt buộc

- Giữ đúng thứ tự agent trong assignment/action.
- Validate adjacency, ao, step, fuel, traffic và format trước khi gửi action.
- State mới nhất có quyền ưu tiên hơn route cũ.
- Một lỗi trong action có thể làm cả submission bị từ chối.
- Timeout không chứng minh request chưa được nhận; không retry action mù quáng.
- Mock server phải có test cho `E_NOT_ADJACENT`, `E_POND`, `E_STEP_OVERFLOW`, `E_NO_FUEL`, `E_BAD_FORMAT` và rate limit.

## Quy ước thay đổi

### Nếu làm phần KIENNT

- Dùng nhánh `kien/infra-local`.
- Ưu tiên file local server, API client, fixture, snapshot và runtime.
- Cập nhật `docs/plan/KIENNT.md` khi hoàn thành milestone hoặc phát hiện giả định mới.

### Nếu làm phần DATNT

- Dùng nhánh `dat/route-planner`.
- Ưu tiên map model, target selection, route planner, validator và simulator.
- Cập nhật `docs/plan/DATNT.md` khi hoàn thành milestone hoặc phát hiện giả định mới.

### Khi tích hợp hai phần

1. KIENNT cung cấp fixture/state local rõ schema.
2. DATNT tạo action và ghi `VALIDATION: PASS`.
3. KIENNT kiểm tra tích hợp rồi mới gọi `/actions` local.
4. Lưu response/state sau action.
5. Chạy test và kiểm tra secret leak.

## Cách báo cáo

Mọi báo cáo nên gồm:

- Files changed.
- Why the change is needed.
- Tests/checks run.
- Assumptions or unresolved questions.
- Whether the change is local-only or affects an external integration.

## Không được làm

- Không commit `.env`, token, credentials hoặc Authorization header.
- Không tạo production deployment chỉ để hoàn thành test.
- Không tự khẳng định luật chưa được xác minh là đúng.
- Không bỏ qua validator để “thử gửi xem sao”.
- Không dùng state cũ khi đã có snapshot mới.
