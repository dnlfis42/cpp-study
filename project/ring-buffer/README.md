# ring-buffer

고정 용량 원형 바이트 버퍼. 네트워크 스트림 수신/송신 버퍼용.

- **ring (wrap-around)**: `read_pos`/`write_pos` 둘이 고정 크기 배열 위를 순환
- **contiguous region 개념**: wrap 때문에 연속 읽기/쓰기 가능 바이트는 `readable_size()`/`writable_size()`로 따로 노출
- **C++20**
- `linear-buffer`와 달리 메시지 단위가 아닌 **스트림**이 주 용도

## 버전 히스토리

| 버전 | 주요 변화                                                         |
| ---: | :---------------------------------------------------------------- |
|  v01 | 기본 구현. `unique_ptr<byte[]>` 기반, raw I/O bool all-or-nothing |

## API 요약 (v01 기준)

- 상태: `capacity()`, `size()`, `available()`, `empty()`, `full()`, `clear()`
- zero-copy: `read_ptr()`, `write_ptr()`, `readable_size()`, `writable_size()`, `move_read_pos()`, `move_write_pos()`
- raw I/O (bool): `read()`, `write()`, `peek()`

## 빌드/테스트

```bash
cmake --workflow --preset debug
ctest --preset test
./project/ring-buffer/script/run_bench_v01.sh
```
