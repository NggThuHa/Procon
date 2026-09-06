# PTIT PROCON 2026

Repo này dùng để **hiểu luật, xây dựng chiến thuật và vận hành bot** cho cuộc thi PTIT Procon 2026.

Mục tiêu của team là điều khiển các xe trên bản đồ lục giác qua nhiều ngày để thu thập udon. Người thắng không nhất thiết là người lấy được nhiều phần udon nhất; lợi thế lớn nhất đến từ việc **hiểu đúng luật, lập route hợp lệ và phản ứng đúng với state/traffic thực tế**.

## Đọc tài liệu theo thứ tự

1. Đọc phần **Luật chơi** trong file này.
2. Đọc kế hoạch chung [`PLAN.md`](docs/PLAN.md).
3. KIENNT (tôi) đọc [`docs/plan/KIENNT.md`](docs/plan/KIENNT.md); DATNT đọc [`docs/plan/DATNT.md`](docs/plan/DATNT.md).
4. Khi làm việc với AI Agent, đọc [`AGENTS.md`](AGENTS.md).
5. Luôn kiểm tra dữ liệu thực tế từ `/setup` và `/state`; không coi ví dụ trong tài liệu là dữ liệu của trận hiện tại.

> **Nguồn sự thật:** Nếu tài liệu này khác response server hoặc quy định chính thức của ban tổ chức, ưu tiên quy định chính thức và dữ liệu server.

---

## 1. Luật chơi và cách xếp hạng

### 1.1. Mục tiêu

Đội điều khiển nhiều agent/xe trên bản đồ lục giác. Mỗi ngày, đội gửi một action cho tất cả agent. Xe tuần tra di chuyển tới các spot để thu udon; xe tiếp tế hỗ trợ hậu cần.

### 1.2. Thứ tự xếp hạng

Kết quả được so sánh theo thứ tự sau, từ quan trọng nhất đến ít quan trọng hơn:

1. **Số loại udon (`brand`) khác nhau đã thu được**: nhiều hơn thắng.
2. Nếu bằng nhau, **tổng lũy kế số loại udon theo ngày** cao hơn thắng.
3. Nếu vẫn bằng nhau, **tổng số phần udon** cao hơn thắng.
4. Nếu vẫn bằng nhau, **tổng thời gian phản hồi** thấp hơn thắng.
5. Nếu vẫn hòa, áp dụng luật phụ của ban tổ chức/server.

Server tự tính điểm sau khi action được chấp nhận. Bot không gửi điểm.

**Hệ quả chiến thuật:** Khi phải lựa chọn giữa mở một `brand` mới và lấy thêm phần của `brand` đã có, thường nên ưu tiên `brand` mới. Tuy nhiên, chỉ được chốt route sau khi kiểm tra khoảng cách, bước, traffic, nhiên liệu, stock và khả năng thu thực tế.

---

## 2. Vòng đời một trận

```text
GET  /setup
POST /assignment
GET  /start                 chờ trận bắt đầu
Lặp theo từng ngày:
  GET  /state
  POST /actions
GET  /result
```

Quy tắc:

- Số ngày, số agent, giới hạn bước và giới hạn nhiên liệu phải đọc từ `setup`/`state`, không được đoán.
- `/assignment` chỉ thực hiện một lần cho cả trận. Sau khi thành công không đổi loại xe.
- Mỗi ngày phải lấy state mới nhất và tính lại route.
- Sau khi gửi action, phải lưu response và state mới trước khi xử lý ngày tiếp theo.
- Một action không hợp lệ có thể khiến toàn bộ submission bị từ chối.

---

## 3. Agent và loại xe

`setup` trả về danh sách agent. Action thứ `i` luôn tương ứng với agent thứ `i`; phải giữ nguyên thứ tự này trong mọi payload.

| `kind` | Loại xe | Quyền và giới hạn |
|---:|---|---|
| `0` | Xe tuần tra | Được di chuyển và thu udon tại spot; bị giới hạn nhiên liệu |
| `1` | Xe tiếp tế | Không thu udon; nhiên liệu vô hạn; có thể hỗ trợ xe tuần tra khi cùng ô và đáp ứng điều kiện server |

Không được tự giả định xe tiếp tế có thể nạp từ xa hoặc nạp ở mọi thời điểm. Điều kiện hỗ trợ chính xác phải lấy từ luật/API và state thực tế.

Ví dụ state tối thiểu:

```json
{
  "day": 0,
  "agents": [
    {"kind": 0, "pos": 12, "fuel": 8}
  ],
  "others": [],
  "traffics": []
}
```

---

## 4. Bản đồ lục giác và di chuyển

### 4.1. Vị trí

Bản đồ có chiều rộng `map.width`, chiều cao `map.height`. Vị trí phẳng được biểu diễn bởi:

```text
pos = row * map.width + column
```

Không được coi `pos + 1` hoặc `pos - 1` luôn là một bước hợp lệ. Phải tính neighbor theo hình học lục giác và parity của hàng/cột theo map thực tế.

### 4.2. Mã hướng

| Mã | Hướng |
|---:|---|
| `0` | trên-trái |
| `1` | trên-phải |
| `2` | phải |
| `3` | dưới-phải |
| `4` | dưới-trái |
| `5` | trái |

Một bước chỉ hợp lệ khi ô đích thực sự kề ô nguồn theo hướng đó. Không được đi ra ngoài map, vào ô không tồn tại hoặc vào ao.

### 4.3. Địa hình, chi phí và traffic

Chi phí được tính khi xe **rời ô nguồn**:

| Địa hình | Điều kiện | Chi phí bước | Nhiên liệu |
|---|---|---:|---:|
| Đất/đồng bằng (`terrain = 0`) | Có thể đi | `2` | `1` |
| Đường (`terrain = 1`) | Thông thoáng | `1` | `2` |
| Đường (`terrain = 1`) | Đông | `2` | `2` |
| Đường (`terrain = 1`) | Ùn tắc | `4` | `2` |
| Núi (`terrain = 2`) | Có thể đi | `3` | `2` |
| Ao (`terrain = 3`) | Không thể đi | Không áp dụng | Không áp dụng |

Luật bắt buộc:

- Tổng chi phí bước của mỗi agent không vượt `daySteps[day]`.
- Xe tuần tra không được vượt giới hạn nhiên liệu.
- Xe tiếp tế có nhiên liệu vô hạn nhưng vẫn phải tuân thủ giới hạn bước nếu server áp dụng.
- Ngày đầu đường bắt đầu thông thoáng; ngày sau traffic phụ thuộc dữ liệu trước đó và lưu lượng di chuyển.
- Traffic thay đổi chi phí bước, nên route ngày hôm nay không được dùng mù quáng cho ngày mai.
- Nếu chưa chắc chi phí, dùng route bảo thủ; không chạy sát giới hạn dựa trên phỏng đoán.

---

## 5. Spot, brand, stock và thu udon

Mỗi `spot` thường có:

- `pos`: vị trí spot.
- `brand`: loại udon.
- `stocks`: số lượng tồn kho.

Quy tắc:

- Xe tuần tra đến đúng spot sẽ tự động thu udon theo luật server.
- Mỗi xe chỉ thu tại spot đầu tiên nó ghé trong một ngày.
- Stock được bổ sung vào đầu ngày.
- Stock của các đội độc lập với nhau.
- Đi ngang qua gần spot không đồng nghĩa với đã thu; xe phải đáp ứng điều kiện ghé/đứng tại spot của server.

---

## 6. Action và format payload

Payload `/actions` là một mảng; mỗi phần tử tương ứng với một agent:

```json
[
  [2, 2, 3, -4],
  [-10],
  [5, 5, 0]
]
```

- Số phần tử ngoài phải đúng bằng số agent.
- `0..5`: đi một bước theo mã hướng tương ứng.
- `-N`: đứng yên `N` bước; dùng giá trị âm cho lệnh chờ.
- Lệnh đi/chờ đều phải được tính vào ngân sách bước theo quy tắc server.
- Phải validate toàn bộ mảng trước khi gửi.
- Khi route chính không an toàn, dùng action fallback hợp lệ như đứng yên thay vì gửi payload có nguy cơ làm hỏng toàn submission.

Payload `/assignment` là mảng `kind`, độ dài bằng số agent:

```json
[0, 0, 1, 0]
```

Không thay đổi assignment sau khi server chấp nhận.

---

## 7. Môi trường chạy

Phát triển và kiểm thử mặc định trên **localhost hoặc mock server**. Không yêu cầu deploy public, domain thật hay gọi server thi đấu từ Internet.

- Base URL mặc định: `http://localhost:<PORT>`.
- Có thể dùng fixture/snapshot giả lập để test deterministic.
- Không hard-code production URL; lấy base URL từ environment/config.
- Chỉ kết nối server thi đấu khi team chủ động xác nhận và có cấu hình riêng.

### 7.1. Chạy bot mẫu

```bash
cd hexudon-bot-cpp
make                                   # Linux/macOS
make LDLIBS=-lws2_32                   # Windows (MinGW)
./bot "$BASE_URL" "$MATCH_ID" "$API_TOKEN"
```

Đọc `BASE_URL`, `MATCH_ID` và `API_TOKEN` từ environment (xem `.env.example`);
không truyền token thật trực tiếp trên command line khi có thể tránh, vì nó
lọt vào shell history và process list. Chi tiết ở
[`hexudon-bot-cpp/README.md`](hexudon-bot-cpp/README.md).

## 8. API

### 8.1. Endpoint

```text
GET  /api/v1/matches/{MATCH_ID}/setup
POST /api/v1/matches/{MATCH_ID}/assignment
GET  /api/v1/matches/{MATCH_ID}/start
GET  /api/v1/matches/{MATCH_ID}/state
POST /api/v1/matches/{MATCH_ID}/actions
GET  /api/v1/matches/{MATCH_ID}/result
```

### 8.2. Header

```http
Authorization: Bearer <API_TOKEN>
Content-Type: application/json
```

Không commit hoặc in `API_TOKEN` vào source, log, README, shell history hay Git. Dùng biến môi trường và file `.env` đã được ignore.

### 8.3. Giới hạn request

- Polling quá nhanh có thể bị `429` hoặc `E_RATE_LIMIT`; mặc định giữ khoảng cách tối thiểu khoảng 200 ms hoặc theo giới hạn server.
- Timeout mạng không chứng minh request chưa được server nhận.
- Trước khi retry `/actions`, phải kiểm tra state/response để tránh gửi trùng hoặc mất đồng bộ.

### 8.4. Lỗi thường gặp

| Mã lỗi | Ý nghĩa | Cách xử lý |
|---|---|---|
| `E_NOT_ADJACENT` | Ô đích không kề ô hiện tại | Tính lại neighbor/route |
| `E_POND` | Đi vào ao | Loại ô khỏi graph |
| `E_STEP_OVERFLOW` | Vượt ngân sách bước | Rút ngắn route/đổi mục tiêu |
| `E_NO_FUEL` | Thiếu nhiên liệu | Ghé tiếp tế/chọn route khác |
| `E_BAD_FORMAT` | Sai JSON/action | Kiểm tra schema và thứ tự agent |
| `E_RATE_LIMIT` / `429` | Gửi quá nhanh | Tăng delay, giảm polling |

---

## 9. Luật ưu tiên khi có mâu thuẫn

Khi hai nguồn thông tin mâu thuẫn, xử lý theo thứ tự:

1. Quy định chính thức của ban tổ chức.
2. Response và validation thực tế từ server.
3. Dữ liệu `/setup` và `/state` của trận hiện tại.
4. Tài liệu repo và ví dụ minh họa.
5. Suy luận/giả định của agent.

AI Agent không được tự bịa schema, luật điểm, chi phí, điều kiện tiếp tế hoặc hành vi server. Nếu chưa đủ dữ liệu, phải đánh dấu giả định và hỏi người phụ trách.

---

## 10. Checklist trước khi gửi action

- [ ] Đúng `MATCH_ID` và đúng `day`.
- [ ] Đủ action cho từng agent, đúng thứ tự.
- [ ] Mọi bước đi đều tới ô kề hợp lệ.
- [ ] Không ra ngoài map hoặc vào ao.
- [ ] Không vượt `daySteps[day]`.
- [ ] Xe tuần tra đủ nhiên liệu.
- [ ] Đã tính traffic hiện tại.
- [ ] Đã kiểm tra spot/brand/stock.
- [ ] Ưu tiên brand còn thiếu khi điều kiện cho phép.
- [ ] Validator trả `PASS`.
- [ ] JSON hợp lệ.
- [ ] Không polling/gửi request quá nhanh.
- [ ] Không retry khi chưa biết trạng thái request trước.
- [ ] Không lộ `API_TOKEN`.

## 11. Tài liệu trong repo

| File | Mục đích |
|---|---|
| `README.md` | Giới thiệu và luật chơi |
| `docs/PLAN.md` | Quy ước chung và cách phối hợp |
| `docs/plan/KIENNT.md` | Kế hoạch chi tiết của KIENNT (tôi) |
| `docs/plan/DATNT.md` | Kế hoạch chi tiết của DATNT |
| `AGENTS.md` | Quy tắc để AI Agent làm việc trong repo |
| `hexudon-bot-cpp/` | Bot mẫu C++17, transport HTTP raw socket |
| `fixtures/` | Dữ liệu mẫu cho local/mock server |
| `.env.example` | Mẫu biến môi trường, không chứa secret |
| `.github/` | CI, issue template và PR template |
