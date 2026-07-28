# 메모리 평가: Agnocast zero-copy vs 표준 ROS 2

Agnocast의 공유메모리 zero-copy IPC가 표준 ROS 2(rclcpp/DDS) 대비 메모리를 얼마나
아끼는지, **subscriber 수**와 **메시지 크기**를 늘리며 "메시지 payload에 기인한 물리 메모리"를
측정해 정량화한다.

![결과](scripts/out/zero_copy_memory.png)

## 측정 지표 & 방식

각 케이스마다 메시지를 보유하는 subscriber K개와 publisher 1개를 띄우고 모든 참여 프로세스의
`smaps_rollup` **Pss**를 두 시점에 측정한다:

- **baseline** — subscriber가 모두 준비됐지만 **발행 전**
- **loaded** — 모든 subscriber가 수신·**보유한 후**

보고값은 전 프로세스에 대한 `Σ(loaded − baseline)` — payload가 유발한 물리 메모리다.
**PSS(Proportional Set Size)** 를 쓰는 이유: 공유 페이지를 매핑한 프로세스 수로 나눠
배분하므로 Agnocast가 공유하는 사본 하나와 표준 ROS 2가 프로세스마다 갖는 사본을
**한 지표로 공정하게 비교**할 수 있다.
프로세스 고정 오버헤드는 전/후 델타로 상쇄되고 각 케이스는 2회 평균한다.

두 경로 모두 같은 메시지(`agnocast_sample_interfaces/DynamicSizeArray`)와 같은 "메시지 보유"
패턴을 쓰며 전송 계층만 다르다(`agnocast_*` vs `std_*` 노드, [agnocast_eval](.) 패키지).

## 결과

**subscriber 수 스윕** (16 MiB 메시지):

| subscriber | 표준 ROS 2 | Agnocast | 절감 |
|---|---|---|---|
| 1 | 64.1 MiB | 16.1 MiB | 4.0× |
| 2 | 96.2 MiB | 16.0 MiB | 6.0× |
| 4 | 160.2 MiB | 16.0 MiB | 10.0× |
| 8 | 288.2 MiB | 16.0 MiB | **18.0×** |

**메시지 크기 스윕** (subscriber 1개):

| 메시지 | 표준 ROS 2 | Agnocast | 절감 |
|---|---|---|---|
| 1 MiB | 4.2 MiB | 1.0 MiB | 4.1× |
| 4 MiB | 16.2 MiB | 4.0 MiB | 4.0× |
| 16 MiB | 64.1 MiB | 16.1 MiB | 4.0× |
| 64 MiB | 256.2 MiB | 64.1 MiB | 4.0× |

## 해석

- **Agnocast의 payload 메모리는 subscriber 수와 무관하게 일정**(K=1…8 모두 ≈16 MiB 사본 하나):
  publisher가 공유메모리에 한 번 쓰고 모든 subscriber가 같은 물리 페이지를 매핑한다. subscriber가
  늘수록 이점이 계속 커진다(subscriber 8개에서 18×).
- **표준 ROS 2는 선형 증가**(여기선 subscriber당 ≈ +32 MiB): 참여자마다 DDS history 캐시
  복사본과 애플리케이션 복사본을 모두 보유하는데, Agnocast는 둘 다 필요 없다.
- **Agnocast의 payload 메모리는 크기와 무관하게 정확히 메시지 사본 하나**(1×)인 반면, 표준
  ROS 2는 subscriber 1개에서도 payload의 ≈4×가 든다.

## 교차검증: 내부 메시지 크기 ↔ 실측 메모리

ROS 2 코드는 `/proc` 없이도 메시지 크기를 안다.
[msg_size_probe](src/msg_size_probe.cpp)는 in-memory payload
(`data.size() * sizeof(int64_t)`)와 직렬화 CDR 크기
(`rclcpp::Serialization<Msg>` → `SerializedMessage::size()`)를 함께 출력한다:

```
payload      in-memory (data*8)     serialized CDR
 1 MiB          1048576 bytes            1048596 bytes
16 MiB         16777216 bytes           16777236 bytes
64 MiB         67108864 bytes           67108884 bytes
```

직렬화 크기 = payload + 20바이트 CDR 프레이밍(배열 길이 + `id` + 정렬)이므로 논리적
메시지 크기 ≈ payload N이다. **실측 PSS ÷ 이 내부 크기 = 유효 물리 복사본 수**이며
그래프의 `(N×)` 라벨이 이 값이다:

| 케이스 | 실측 PSS ÷ 메시지 크기 | = 유효 복사본 |
|---|---|---|
| Agnocast (모든 K/N) | N ÷ N | **1× (공유 사본 하나)** |
| 표준, subscriber 1 | ≈4N ÷ N | 4× (DDS 캐시 + 앱 복사, 양끝 ×2) |
| 표준, subscriber 8 | 288 ÷ 16 | 18× |

외부 PSS 측정과 내부 메시지 크기가 일치한다: Agnocast 총 물리 메모리는 딱 메시지 사본 하나이고
표준 ROS 2는 복사본 수만큼이다. probe를 별도 도구로 둔 이유 — zero-copy publisher 안에서
직렬화하면 회수되지 않는 CDR 버퍼가 공유메모리에 잡혀 측정을 오염시키기 때문.

## 구성

| 파일 | 역할 |
|------|------|
| `scripts/harness.py` | 재사용 측정 harness — 노드 실행/마커 대기/PSS 샘플링/`measure_case` (실험과 분리) |
| `scripts/run_eval.py` | 스윕 정의 + `harness` 호출 + `results.csv` 기록 |
| `scripts/plot_eval.py` | `results.csv` → 그래프 `scripts/out/zero_copy_memory.png` |
| `src/` | 측정 노드 4종 + `msg_size_probe` (colcon 패키지) |

## 재현

저장소 루트(= colcon workspace 루트)에서:

```bash
source /opt/ros/humble/setup.bash
colcon build --symlink-install --packages-select agnocast_eval
source install/setup.bash
EVAL=src/agnocast_eval/scripts
python3 $EVAL/run_eval.py    # scripts/out/results.csv 생성 (~4분)
python3 $EVAL/plot_eval.py   # scripts/out/zero_copy_memory.png 생성
ros2 run agnocast_eval msg_size_probe   # 내부 메시지 크기(in-memory vs 직렬화)
```

전제: Agnocast 커널 모듈 로드 + `libagnocast_heaphook.so` 빌드
(저장소 루트에서 `bash scripts/dev/build_all.bash`). `run_eval.py`는 소싱된 셸에서 실행해야
한다(노드 바이너리를 직접 spawn해 heaphook가 프로세스를 정확히 한 번 등록하도록).
