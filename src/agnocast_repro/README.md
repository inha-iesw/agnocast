# 재현 / 회귀 테스트: 공유 메모리 물리 페이지 회수

원래는 heaphook TLSF allocator
([`agnocast_heaphook/src/tlsf.rs`](../../agnocast_heaphook/src/tlsf.rs))의 결함을 재현하던 패키지다. 
해제된 공유 메모리 블록이 물리 페이지를 커널에 **돌려주지 않아서** 오래 도는 프로세스의 메모리가 high-water mark까지 늘기만 하고 줄지 않았다.

**이 결함은 지금은 고쳐졌다** — `deallocate()`가 해제된 블록의 내부 페이지에
`madvise(MADV_REMOVE)`를 호출한다([`src/reclaim.rs`](../../agnocast_heaphook/src/reclaim.rs) 참고).
그래서 이 harness는 이제 **회귀 테스트** 역할을 한다. 지금은 *pages reclaimed*를 보고한다.
수정을 되돌리면 다시 *LEAK REPRODUCED*로 뒤집힌다.

## 기본 샘플 앱으로는 왜 드러나지 않는가

기본 `talker`는 QoS depth 1로 100 ms마다 1 MiB 메시지를 발행한다. TLSF가 매번 같은 블록을
재활용하니 pool의 resident 크기는 몇 MiB에서 평평해지고 누수가 눈에 띄지 않는다. 이를 드러내려고
[`burst_leak_repro`](src/burst_leak_repro.cpp)는 **큰 메시지를 동시에 여럿 살려 둔 채 high-water
mark를 만든 뒤 한꺼번에 해제**한다:

1. **baseline** — 아무것도 borrow하지 않은 상태. pool Rss ≈ 0.
2. **peak** — `REPRO_NUM_MSGS`개 메시지를 borrow해 각각의 모든 페이지를 건드린다(기본
   256 × 4 MiB = 1 GiB). 전부 동시에 살려 둔다.
3. **drain** — borrow한 메시지를 발행하지 않은 채 전부 버린다. 소멸자마다 heaphook을 거쳐
   버퍼를 해제하고 TLSF `deallocate()`로 간다.
4. **observe** — pool의 Rss가 peak 근처에 머물면 해제된 페이지가 회수되지 않은 것이다.

해제는 실제 heaphook을 통과한다. 발행하지 않은 borrow 상태의 `ipc_shared_ptr`를 버리면
`delete ptr_`가 호출되고(`agnocast_smart_pointer.hpp`의 `reset()` 참고) 그 버퍼 해제를
`lib.rs`의 `free()`가 가로채 `TLSFAllocator::deallocate`로 넘긴다.

## 전제 조건

- Agnocast 커널 모듈 로드 (`/dev/agnocast` 존재, `/sys/module/agnocast`).
- `libagnocast_heaphook.so` 빌드·설치. 저장소 루트에서:
  ```bash
  bash scripts/dev/build_all.bash   # colcon + kmod + heaphook -> install/agnocastlib/lib
  ```
- repro 노드 빌드:
  ```bash
  source /opt/ros/humble/setup.bash
  colcon build --symlink-install --packages-select agnocast_repro
  ```

## 실행

```bash
scripts/run_repro.sh
```

조절 값(노드가 읽는 환경 변수):

| 변수 | 기본값 | 의미 |
|-----|---------|---------|
| `REPRO_BLOCK_BYTES` | `4194304` (4 MiB) | 메시지당 건드리는 바이트 |
| `REPRO_NUM_MSGS`    | `256`             | peak에서 동시에 살려 두는 메시지 수 (peak ≈ block × num) |
| `REPRO_PHASE_SECS`  | `5`               | 단계별 유지 시간 (sampler 수집 구간) |

산출물은 `scripts/out/`에 쌓인다(git 추적 제외). `node.log`는 단계 마커이고 `smaps.csv`는
`/dev/shm/agnocast@<pid>` pool 매핑의 `epoch_ms,rss_kb,pss_kb` 시계열이다.

## 결과 읽기

러너는 baseline / peak / drained Rss와 판정을 출력한다. 회수 수정이 들어간 현재 출력은 이렇다:

```
  baseline Rss :         16 kB (    0.0 MiB)
  peak     Rss :    1048656 kB ( 1024.1 MiB)
  drained  Rss :      ~1600 kB (   ~1.5 MiB)  <- after freeing every message
  >>> pages reclaimed: only 0% stayed resident after free
```

`drained`가 baseline 수준으로 돌아왔다는 것은 해제된 물리 페이지가 커널로 반환됐다는 뜻이다.
**수정 전에는** 같은 실행이 `drained ≈ peak`와 `>>> LEAK REPRODUCED: 100% ...`를 냈다.
`TLSFAllocator::deallocate`의 `madvise(MADV_REMOVE)`를 되돌리면 그 상태가 그대로 돌아오고
이 점이 이 패키지를 회귀 테스트로 만든다.

## 파일

| 파일 | 역할 |
|------|------|
| `scripts/run_repro.sh`      | preflight → heaphook 아래에서 노드 실행 → 샘플링 → 판정 |
| `scripts/sample_smaps.py`   | pid가 끝날 때까지 pool 매핑의 Rss/Pss를 CSV로 수집 |
| `scripts/summarize_repro.py`| 노드 마커와 CSV를 읽어 누수/회수 판정을 출력 |
| `src/burst_leak_repro.cpp`  | burst 노드 (`DynamicSizeArray` 재사용) |
