# AGENTS.md — Hướng dẫn cho AI Agent

## Mục tiêu repo

Repo dùng để cải tiến chiến thuật cho bot Procon. Tầng transport (HTTP, JSON,
vòng đời trận) do ban tổ chức cung cấp sẵn ở `hexudon-bot-cpp/` và đã chạy được —
**không xây lại**. Điểm chỉ đến từ `planActions()`.

## Cảnh báo: phần lớn "luật" trong README chưa có nguồn

Mục Luật chơi trong `README.md` do AI viết ở commit `8d98369`, không phải trích
từ đặc tả ban tổ chức. `brand`, `stocks`, thứ tự xếp hạng và toàn bộ mã lỗi `E_*`
**không xuất hiện một lần nào** trong code mẫu BTC.

Không khẳng định những thứ đó là luật. Không tối ưu chiến thuật dựa trên chúng
cho tới khi Giai đoạn 0 trong `docs/PLAN.md` hoàn tất.

## Tài liệu bắt buộc đọc

Theo thứ tự:

1. [`README.md`](README.md) — giới thiệu và luật chơi.
2. [`PLAN.md`](docs/PLAN.md) — phân công và quy trình phối hợp.
3. [`docs/plan/KIENNT.md`](docs/plan/KIENNT.md) — vai trò KIENNT (tôi): API, localhost, vận hành.
4. [`docs/plan/DATNT.md`](docs/plan/DATNT.md) — vai trò DATNT: map, route, validator.

## Vai trò hiện tại

- **KIENNT (tôi):** chọn mục tiêu và chấm điểm — *đi đâu thì đáng*. Sở hữu
  simulator và arena. Bậc 2 (gán Hungarian) và bậc 4 (beam search).
- **DATNT:** bản đồ, đường đi, điều phối xe — *đi thế nào cho kịp*. Sở hữu
  `strategy/common.hpp`. Bậc 1 (Dijkstra), 3 (rolling horizon), 5 (ALNS).

Thang bậc đầy đủ ở `docs/strategies.md`. Các bậc **xếp chồng lên nhau**, không
phải hai bot cạnh tranh.

Không được tự đổi vai trò hoặc phân công nếu chưa có yêu cầu mới.

## Quy tắc làm việc

- Không hard-code URL production; lấy base URL từ tham số dòng lệnh/environment.
- Không gọi server thi đấu thật trong test mặc định. Simulator chạy offline,
  không cần mạng và không cần token.
- Không tự tạo thêm tài khoản/token trên judge để tập nếu chưa hỏi ban tổ chức.
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
- Simulator chỉ mô phỏng phần đã xác minh từ `main.cpp`: tính hợp lệ nước đi,
  chi phí bước, nhiên liệu. Không mô phỏng điểm số theo phỏng đoán.
- Mọi thay đổi chiến thuật phải kèm số đo arena trước/sau. Không có số, không merge.

## Quy ước thay đổi

### Mỗi chiến thuật một nhánh, một release

- Nhánh: `strategy/<tên>` · Tag: `bot-<tên>-v<N>` · File: `strategy/<tên>.hpp`
- Nhánh chiến thuật chỉ chạm file của nó + một dòng đăng ký trong dispatcher.
- Sửa `strategy/common.hpp` phải tách PR riêng — nó ảnh hưởng mọi chiến thuật.
- Cập nhật file plan của người phụ trách khi xong milestone hoặc phát hiện giả định mới.

### Khi tích hợp

1. Chạy validator trên action trước khi gửi.
2. Chạy arena trên toàn bộ bộ map thử nghiệm, ghi lại điểm.
3. So với bản trước; kém hơn thì không merge.
4. Kiểm tra không lộ secret.

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
