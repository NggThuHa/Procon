# Do lai bot tren map lon / ngay dai

Bot cu (`49875f8`) cho ket qua rat te khi map lon, ngay dai va nhieu doi thu.
File nay ghi lai **nguyen nhan da do duoc** va **so do sau khi sua**, de lan sau
khong ai phai chan doan lai.

Cach do: arena offline (`python3 -m tools.arena`), 30 fixture sinh san trai deu
theo co map (8x8 -> 64x64), mat do spot (1 spot/8 o -> 1 spot/50 o), so ngay
(4 -> 12) va do khan nhien lieu. Ngan sach suy nghi ep ve `PROCON_PLAN_MS=1500`
cho ca hai ben de so cong bang.

## Bon nguyen nhan, theo do nang dan

### 1. Tim duong tren khong gian `(o, so buoc)` — het gio truoc khi kip tim

Dijkstra cu chay tren khong gian `o x (daySteps+1)` de toi uu nhien lieu duoi
tran buoc, va chay MOT lan cho moi nguon (moi spot + moi xe). Do phuc tap nhan
ba theo co map, ngan sach buoc va so spot cung mot luc:

| Map | Buoc/ngay | Spot | Thoi gian lap ke hoach MOI NGAY (bot cu) |
|---|---:|---:|---:|
| 50x50 | 80 | 70 | 1.8 giay |
| 60x60 | 120 | 100 | 5.8 giay |
| 80x80 | 200 | 150 | **30 giay** |

`daySeconds` that la 60. Tren map 80x80 bot cu tieu nua ngay cho mot ngay, va
`buildRouteCache()` khong he kiem tra dong ho nen ngan sach `PROCON_PLAN_MS`
khong chan duoc no. Map to hon mot chut nua la mat trang ca ngay.

Sua: Dijkstra chay tren DUNG o, nhieu lan voi cac to hop trong so
`(buoc, nhien lieu)` khac nhau — moi lan mot diem tren bien Pareto. Cung ba map
tren gio lan luot con 0.2 / 0.3 / 0.6 giay.

### 2. Xe dung yen ca ngay khi khong spot nao trong tam

Beam cu chi nhan chang **ket thuc dung tai mot spot**: `if (gain.portions <= 0)
continue`. Khong toi duoc spot nao trong ngan sach hom nay thi xe khong co
phuong an nao, va no dung yen — dot sach ngan sach buoc doi lay khong gi. Do tren
fixture 40x40 thua spot: **562 / 2400 buoc (23%) la lenh dung yen**.

Sua: chang qua dai bi CAT lay doan di duoc, xe tien ve phia spot de ngay mai
xuat phat gan hon. Cong voi `endValue[]` — uoc luong so phan udon gom duoc NGAY
MAI neu ket thuc ngay hom nay tai o do.

### 3. Trong so cham diem la hang so chinh tay tren map 8x8

`beamExpansionScore()` cu tru `route.steps * 1100000` va `route.fuel * 180000`.
Tren map 8x8 mot chang dai 5 buoc; tren map 80x80 no dai 40 buoc, nen cung hang
so do bien moi chang thanh diem am — va "khong lam gi" thanh phuong an diem cao
nhat. Day la ly do thu hai khien xe dung yen.

Sua: moi trong so suy tu mat do spot va chi phi dia hinh THAT cua ngay hom nay
(`g_stepsPerPortion`, `g_fuelScarcity`), nen cung mot bo tham so chay duoc ca
map 8x8 lan 100x100.

### 4. Diem de TIM lan voi diem de CHON

Phat buoc/nhien lieu can co de huong tim kiem, nhung khong duoc co trong thang
do chon ke hoach cuoi cung — vi mot buoc khong dung hom nay thi mat luon, no
khong chuyen sang ngay mai duoc. Bot cu dung cung mot thang do cho ca hai viec.

Sua: `BeamState::score` (co phat) de xep hang va cat nhanh; `BeamState::objective`
(khong phat, dung bon tieu chi that cua `standings`) de chon. Rieng viec tach hai
thang do nay dua map nho tu **thua** bot cu len **bang hoac hon**.

## Ket qua

30 fixture, so theo dung thu tu tieu chi cua `standings`:

| | Bot cu | Bot moi |
|---|---:|---:|
| Thang / thua / hoa | 0 / 27 / 3 | **27 / 0 / 3** |
| Tong `udon_total` | 7542 | **9666 (+28.2%)** |
| Tong `udon_types` | 174 | 174 |
| Tong `daily_types_sum` | 1524 | 1524 |
| Thoi gian chay ca bo | 341 giay | **126 giay** |

Chenh lech tap trung o dung cho de gay ra van de nay:

| Fixture | Bot cu | Bot moi | |
|---|---:|---:|---|
| `s08` (8x8, 4 ngay) | 64 / 59 / 66 | 64 / 59 / 67 | bang |
| `s24d` (24x24, 8 ngay, day spot) | 265 / 302 / 299 | 337 / 363 / 347 | +18% |
| `s48` (48x48, 12 ngay) | 593 / 649 / 551 | 693 / 725 / 694 | +19% |
| `s64` (64x64, 12 ngay) | 462 / 464 / 481 | 767 / 868 / 846 | **+75%** |
| `mega80` (80x80, 200 buoc/ngay) | 152 | 476 | **+213%** |

`udon_types` va `daily_types_sum` bang nhau vi ca hai bot deu mo het brand tu
ngay dau tren moi fixture — tren bo fixture nay tran thang thua o tieu chi 3.

## Kiem tra dong ho

Map 100x100, 250 spot, 12 xe, 240 buoc/ngay, `daySeconds = 60`:

| `PROCON_PLAN_MS` | Thoi gian that | Phan udon | Loai udon |
|---|---:|---:|---:|
| `300` | 198 ms | 272 | 12/12 |
| `1000` | 689 ms | 662 | 12/12 |
| mac dinh (30000) | 17.4 giay | 672 | 12/12 |

Han thoi gian duoc ton trong o moi muc, va ngan sach chat den 300 ms van ra ke
hoach dung duoc — vi `buildRouteBook()` bi chan o 60% ngan sach va khong bao gio
duoc cat truoc khi moi xe co duong di cua rieng no.

## Da do va BO

Ghi lai de khong ai thu lai:

- **Tinh gia nhien lieu de danh cho ngay sau vao thang do chon** (`PROCON_FUEL_FUTURE`,
  mac dinh `0`): muc 50% lam 27-0-3 xuong 27-1-2; muc 100% xuong 18-11-1. Viec
  ban phat nhien lieu qua ngay DA do `fuelBudgetToday()` lam, tinh them lan nua
  la ham hai lan cung mot thu.
- **Pha "tieu not ngan sach buoc con lai"**: cho xe di ve o co khoi luong spot
  cao nhat con voi toi. Lam te di o moi co map (`udon_total` 9666 -> khoang 8900):
  no keo xe ra khoi spot no dang dung — noi ma ngay mai xe thu duoc mot phan.
- **Nam diem Pareto khi tim duong** thay vi ba: cham hon ma khong tot hon
  (2128 so voi 2151 tren nhom fixture nho/day).
- **Bao hoa mem cho `endValue[]`** (`cap*r/(cap+r)`) thay vi cat cut o dung mot
  ngay duong: te hon o moi co map.
