# Concept

C++ / 리눅스 저수준 학습 노트와 짧은 실험. 노트는 `(note)` 표기.

## 노트

- [Linux Memory](linux-memory/) — ELF / 가상 메모리 / brk·mmap / glibc malloc·tcache (note)
- [Huge Page](huge-page/) — TLB pressure 해소 메커니즘 + 실측 (note)
- [Cache Alignment](cache-alignment/) — false sharing 실측, plain RMW / LOCK / contention 비용 분해 (note)
- [Atomic Order](atomic-order/) — memory_order별 비용 측정, x86 fetch_add / store / load 실측 (note)
- [Memory Hierarchy](memory-hierarchy/) — CPU 파이프라인(ROB·store buffer), 캐시 계층(MESI·false sharing), TLB/페이징, 하드웨어 구조 용어, NUMA (note)
- [Memory Model](memory-model/) — SC-DRF, happens-before, synchronizes-with, release/acquire, seq_cst(IRIW), relaxed (note)
- [Memory Barrier](memory-barrier/) — 컴파일러 배리어, CPU 배리어(mfence), atomic_signal_fence vs atomic_thread_fence, x86 비용 (note)
