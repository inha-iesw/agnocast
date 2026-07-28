# agnocast_demo

Agnocast의 **zero-copy publisher/subscriber 예제**를 최소 구성으로 담은 패키지다. 표준 ROS 2 예제 패키지 구조를 따른다. 한 프로세스에서 발행한 메시지를 다른 프로세스의 subscriber가 **공유 메모리에서 복사 없이 그대로** 읽는다는 것을 보여준다.

- `demo_talker`는 loaned 메시지를 공유 메모리에서 직접 빌려 1 MiB를 결정적 패턴으로 채우고 checksum을 `id`에 접어 넣은 뒤, payload의 공유 메모리 주소를 출력하고 `/agnocast_demo`로 발행한다.
- `demo_listener`는 별도 프로세스에서 이를 받아 payload 주소를 출력하고 checksum을 다시 접어 맞춰 보고 그 주소가 매핑된 `/dev/shm/agnocast@<pid>` pool 안에 들어 있는지 확인한다.

publisher와 subscriber는 **같은 payload 주소**를 출력한다. pool이 두 프로세스에서 같은 가상 주소에 매핑되므로, subscriber는 publisher가 쓴 바이트를 그대로 읽는다.

```
[talker]   seq=0 payload_addr=0x406000000e0 size=1048576B checksum=8589869056
[listener] payload_addr=0x406000000e0 size=1048576B checksum=8589869056 expected=8589869056  CHECKSUM-MATCH  ADDR-IN-SHM-POOL => ZERO-COPY
```

## 빌드

```bash
source /opt/ros/humble/setup.bash
colcon build --symlink-install --packages-select agnocast_demo   # 저장소 루트에서
source install/setup.bash
```

Agnocast 커널 모듈이 로드돼 있어야 하고 `libagnocast_heaphook.so`가 빌드돼 있어야 한다 (저장소 루트에서 `bash scripts/dev/build_all.bash`).

## 실행

discovery agent, subscriber, publisher를 한 번에 띄운다.

```bash
ros2 launch agnocast_demo agnocast_demo.launch.py
```

메시지 크기는 `DEMO_BLOCK_BYTES`로 정한다 (기본값 `1048576`).

### 노드를 따로 실행하기

**pub/sub** 프로세스는 payload가 공유 메모리에 잡히도록 heaphook을 반드시 preload해야 한다. 반면 discovery agent는 **preload하면 안 된다** — payload를 할당하지 않는 데다, preload하면 프로세스가 중복 등록된다. workspace를 source한 뒤 터미널 세 개에서:

```bash
# 터미널 1 — discovery agent (LD_PRELOAD 없이)
ros2 run ros2agnocast_discovery_agent discovery_agent

# 터미널 2 — subscriber
AGNOCAST_BRIDGE_MODE=off LD_PRELOAD=libagnocast_heaphook.so ros2 run agnocast_demo demo_listener

# 터미널 3 — publisher
AGNOCAST_BRIDGE_MODE=off LD_PRELOAD=libagnocast_heaphook.so ros2 run agnocast_demo demo_talker
```

앞선 실행이 깔끔하게 끝나지 않았다면 남아 있는 discovery lock부터 지운다: `rm -f /dev/shm/agnocast_discovery_agent_*.lock`
