# Project

| 프로젝트                        | 버전 | 설명                                                                  |
| :------------------------------ | :--- | :-------------------------------------------------------------------- |
| [linear-buffer](linear-buffer/) | v02  | 고정 용량 선형 바이트 버퍼. 메시지 단위 직렬화/역직렬화               |
| [ring-buffer](ring-buffer/)     | v04  | 고정 용량 원형 바이트 버퍼. 네트워크 스트림 수신/송신 버퍼            |
| [object-pool](object-pool/)     | v05  | 고정/가변 크기 객체 풀. new/delete 대체, RAII 자원 관리               |
| [memory-pool](memory-pool/)     | v03  | raw 바이트 메모리 풀. arena bump allocator, mmap 백엔드               |
| [spsc-queue](spsc-queue/)       | v05  | 단일 생산자 단일 소비자 락-프리 큐. 스레드 간 메시지 전달 저지연 구현 |
