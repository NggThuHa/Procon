# fixtures/

Dữ liệu mẫu để chạy local/mock server và test deterministic. Đây là điểm mà
`FIXTURE` trong [`.env.example`](../.env.example) trỏ tới.

## `default.json`

Map 7×5, 3 agent (2 tuần tra + 1 tiếp tế), 4 spot với 3 brand, 5 ngày.
Có sẵn ao (`terrain = 3`), đường (`1`) và núi (`2`) để kiểm thử adjacency,
`E_POND`, chi phí bước và fuel.

- `setup` — payload mẫu cho `GET /setup`.
- `state` — payload mẫu cho `GET /state` ở ngày 0.

## ASSUMPTION

Cấu trúc file này được suy ra từ [`README.md`](../README.md) của repo, **không**
phải từ response thật của ban tổ chức. Trước khi tin vào một field:

- Đối chiếu với `/setup` và `/state` thật khi có quyền truy cập.
- Nếu server trả khác, sửa fixture theo server — server là nguồn sự thật.

Các điểm chưa xác minh: hình dạng của `fuelLimits` (hiện là object `patrol`/`supply`), cách `brand` được mã hóa
(số hay chuỗi), và schema đầy đủ của `traffics` và `others`.

## Quy tắc

- Không đưa `API_TOKEN`, Authorization header hay dữ liệu thật vào fixture.
- Snapshot runtime thuộc `.snapshots/` và không được commit.
