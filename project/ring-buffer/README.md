# ring-buffer

고정 용량 원형 바이트 버퍼. 네트워크 스트림 수신/송신 버퍼용.

- **ring (wrap-around)**: `read_pos`/`write_pos` 둘이 고정 크기 배열 위를 순환
- **contiguous region 개념**: wrap 때문에 연속 읽기/쓰기 가능 바이트는 `readable_size()`/`writable_size()`로 따로 노출
- **C++20**
- `linear-buffer`와 달리 메시지 단위가 아닌 **스트림**이 주 용도

## 버전 히스토리

|        버전 | 주요 변화                                                               | 결과                                                                                         |
| ----------: | :---------------------------------------------------------------------- | :------------------------------------------------------------------------------------------- |
| [v01](v01/) | `unique_ptr<byte[]>` 기반 runtime capacity, raw I/O bool all-or-nothing | 베이스라인                                                                                   |
| [v02](v02/) | `template<std::size_t N>` + `& (N-1)` bitmask                           | 작은 chunk 3~4× 빠름. **단, compile-time 크기로 `rep movsq` 인라인되어 4KB에서 1.7× 느려짐** |
| [v03](v03/) | `[[gnu::noinline, gnu::noclone]]` 래퍼로 `rep movsq` 회피               | 4KB에서 v02 대비 **2× 빠름**, v01보다도 빠름. IPC 0.06 → 2.96 회복                           |

## API (공통)

- 상태: `capacity()`, `size()`, `available()`, `empty()`, `full()`, `clear()`
- zero-copy: `read_ptr()`, `write_ptr()`, `readable_size()`, `writable_size()`, `move_read_pos()`, `move_write_pos()`
- raw I/O (bool): `read()`, `write()`, `peek()`

v02~v03은 `template<N>`이라 생성 시 용량을 타입 인자로 지정: `RingBuffer<1024> rb;`

## 빌드/테스트

```bash
cmake --workflow --preset debug
ctest --preset test

# 벤치 (CPU 고정 + 반복)
./project/ring-buffer/script/run_bench_v01.sh
./project/ring-buffer/script/run_bench_v02.sh
./project/ring-buffer/script/run_bench_v03.sh
```
