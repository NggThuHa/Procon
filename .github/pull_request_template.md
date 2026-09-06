## Mô tả

<!-- PR này thay đổi điều gì và vì sao? -->

## Người phụ trách

- [ ] KIENNT — local/mock server, API, snapshot, log, retry
- [ ] DATNT — map, route, validator, simulator
- [ ] Phối hợp hai phần

## Phạm vi

- [ ] Local-only/mock server
- [ ] Không yêu cầu deploy public
- [ ] Có thay đổi tài liệu
- [ ] Có thay đổi fixture/snapshot (đã kiểm tra không chứa secret)

## Kiểm tra đã chạy

```text
# Ghi lệnh và kết quả ở đây
```

## Checklist

- [ ] Đã đọc `AGENTS.md` và tài liệu phân công liên quan.
- [ ] Không sửa ngoài phạm vi yêu cầu.
- [ ] Validator đã kiểm tra adjacency, ao, step, fuel, traffic và format (nếu có action).
- [ ] Test lỗi `E_NOT_ADJACENT`, `E_POND`, `E_STEP_OVERFLOW`, `E_NO_FUEL`, `E_BAD_FORMAT`/rate limit (nếu liên quan).
- [ ] Đã chạy `git diff --check`.
- [ ] Không có `.env`, token, credentials hoặc Authorization header trong diff.
- [ ] Đã cập nhật `README.md`, `PLAN.md` hoặc file plan riêng nếu cần.

## Giả định / câu hỏi còn mở

<!-- Ghi ASSUMPTION rõ ràng; không tự khẳng định luật chưa xác minh. -->
