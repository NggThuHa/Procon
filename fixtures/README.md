# fixtures/

Dữ liệu để chạy simulator offline.

## `default.json`

**Đây là setup THẬT**, chụp nguyên văn từ `GET /setup` của một trận luyện tập
8×8 (`docs/observed/setup-practice-8x8.json`). Không phải dữ liệu bịa.

| Field | Giá trị | Ý nghĩa |
|---|---|---|
| `map` | 8×8, terrain `0..3` | 0 đất, 1 đường, 2 núi, 3 ao |
| `agents` | `[3, 63, 45, 42]` | **mảng int = vị trí xuất phát**, không phải object |
| `fuelLimits` | `64` | **một int duy nhất**, không tách theo loại xe |
| `daySteps` | `[32,32,32,32]` | ngân sách bước mỗi ngày |
| `daySeconds` | `[60,60,60,60]` | giới hạn giây để trả lời mỗi ngày |
| `spots` | 8 spot, `brand` 0..3 | `{brand, pos, stocks}` |
| `players` | `2` | số đội trong trận |
| `busyThreshold` / `jammedThreshold` | `5` / `10` | ngưỡng chuyển trạng thái traffic |

## Lấy fixture mới

```bash
set -a; . ./.env; set +a
TOKEN=$(curl -s -X POST https://procon.ptit.edu.vn/auth/login \
  -H 'Content-Type: application/json' \
  -d "{\"username\":\"$USERNAME\",\"password\":\"$PASSWORD\"}" | jq -r .token)
M=$(curl -s -X POST https://procon.ptit.edu.vn/practice \
  -H "Authorization: Bearer $TOKEN" -H 'Content-Type: application/json' -d '{}')
MID=$(echo "$M" | jq -r .match_id); MT=$(echo "$M" | jq -r .your_token)
curl -s "https://procon.ptit.edu.vn/api/v1/matches/$MID/setup" \
  -H "Authorization: Bearer $MT" | jq . > fixtures/default.json
```

## Còn thiếu

Chưa chụp được `GET /state` (cần bot nối vào trận đang chạy). Shape của
`state`, `traffics` và `others` vẫn chưa xác minh.

## Quy tắc

- Không đưa token vào fixture. `your_token` của trận và token đăng nhập không
  bao giờ được commit.
- Snapshot runtime thuộc `.snapshots/`, không commit.
