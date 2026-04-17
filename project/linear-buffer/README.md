# Linear Buffer

고정 용량 선형 바이트 버퍼. 메시지 단위 직렬화/역직렬화에 사용.

- **linear (non-ring)**: `read_pos` / `write_pos` 단방향 전진, `clear()`로 리셋
- **C++20** (char8_t, 이후 버전에서 std::span)
- 네트워크 프로토콜 메시지 한 단위를 담는 용도

## 버전 히스토리

| 버전 | 주요 변화                                              |
| ---: | :----------------------------------------------------- |
|  v01 | 기본 구현. raw 바이트 I/O + primitive operator<</>>    |
|  v02 | zero-copy read API — `std::span<const std::byte>` 반환 |

각 버전 세부 내용은 해당 디렉터리 README 참고.

## API 요약 (v01 기준)

- 상태: `capacity()`, `size()`, `available()`, `empty()`, `clear()`
- zero-copy: `read_ptr()`, `write_ptr()`, `move_read_pos()`, `move_write_pos()`
- raw I/O (all-or-nothing, bool): `read()`, `write()`, `peek()`
- primitive 직렬화 (throw on 실패): `operator<<`, `operator>>`

## 사용자 타입 확장 패턴

비멤버 오버로드로 확장:

```cpp
LinearBuffer& operator<<(LinearBuffer& lb, const MyType& v) {
    return lb << v.field1 << v.field2;
}
LinearBuffer& operator>>(LinearBuffer& lb, MyType& v) {
    return lb >> v.field1 >> v.field2;
}
```

## 빌드/테스트

```bash
cd <cpp-study 루트>
cmake --workflow --preset debug
ctest --preset test
./build/release/bin/bench_linbuf_v01
```
