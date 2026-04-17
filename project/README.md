# Project

C++ 저수준 컴포넌트 학습 프로젝트. 각 디렉터리는 `v01`~`vNN` 단계로 진행.

## 프로젝트

- [linear-buffer](linear-buffer/) — 고정 용량 선형 바이트 버퍼. 메시지 단위 직렬화/역직렬화
- [ring-buffer](ring-buffer/) — 고정 용량 원형 바이트 버퍼. 네트워크 스트림 수신/송신

## 빌드

```bash
cmake --workflow --preset debug
ctest --preset test
```
