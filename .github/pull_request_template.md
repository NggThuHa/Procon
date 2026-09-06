## Chiến thuật này làm gì

<!--
BẮT BUỘC. Viết để người kia đọc là hiểu, không cần mở code.
Nếu PR không đổi chiến thuật (sửa tài liệu, CI, hạ tầng), ghi "Không đổi chiến thuật".
-->

**Issue:** #

**Ý tưởng:**

**Khác gì bản trước:**

## Số đo arena

<!-- Không có số thì không merge. Dán bảng từ docs/arena/<tên>.md. -->

| Map | Trước | Sau | Chênh |
|---|---:|---:|---:|
| dense-road | | | |
| tight-fuel | | | |
| long-match | | | |
| **Trung bình** | | | |

**Kết luận:** <!-- thắng ở đâu, thua ở đâu, có nên làm mặc định không -->

## Người phụ trách

- [ ] KIENNT — chọn mục tiêu, chấm điểm, simulator/arena
- [ ] DATNT — bản đồ, đường đi, điều phối xe
- [ ] Cả hai

## Phạm vi

- [ ] Chỉ chạm `strategy/<tên>.hpp` của mình
- [ ] Có sửa `strategy/common.hpp` — **cần review của người kia**
- [ ] Có thay đổi tài liệu
- [ ] Có thay đổi fixture (đã kiểm tra không chứa secret)

## Giả định luật

<!--
Chiến thuật này dựa vào luật nào? Mỗi luật ghi nguồn:
[observed] docs/observed/... · [main.cpp] · [BTC spec] · [CHƯA XÁC MINH]
Nếu có mục CHƯA XÁC MINH, nói rõ nó sai thì hỏng đến đâu.
-->

## Kiểm tra đã chạy

```text
# Ghi lệnh và kết quả ở đây
```

## Checklist

- [ ] Đã đọc `AGENTS.md` và `docs/strategies.md`.
- [ ] Validator không báo lỗi trên toàn bộ map thử nghiệm.
- [ ] Không sửa ngoài phạm vi.
- [ ] Đã chạy `git diff --check`.
- [ ] Không có `.env`, token hay Authorization header trong diff.
- [ ] Đã cập nhật file plan của người phụ trách nếu có milestone mới.
