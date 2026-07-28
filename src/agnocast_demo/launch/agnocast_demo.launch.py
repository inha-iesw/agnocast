"""Zero-copy demo launch (ROS 2 Python launch).

Starts one discovery agent (required for cross-process topic discovery), one
subscriber, and one publisher. Every Agnocast process must preload
libagnocast_heaphook.so so message payloads are allocated in shared memory;
AGNOCAST_BRIDGE_MODE=off silences the optional ROS 2 <-> Agnocast bridge.
"""

import os

from launch import LaunchDescription
from launch_ros.actions import Node


def _agnocast_env():
    preload = "libagnocast_heaphook.so"
    existing = os.environ.get("LD_PRELOAD", "")
    if existing:
        preload = f"{preload}:{existing}"
    return {"LD_PRELOAD": preload, "AGNOCAST_BRIDGE_MODE": "off"}


def generate_launch_description():
    env = _agnocast_env()
    return LaunchDescription(
        [
            # The discovery agent is a metadata daemon that allocates no message
            # payloads, so it must NOT preload the heaphook (doing so double-registers
            # the process and fails AGNOCAST_ADD_PROCESS_CMD). Matches the stock
            # discovery_agent.launch.xml, which sets no LD_PRELOAD.
            Node(
                package="ros2agnocast_discovery_agent",
                executable="discovery_agent",
                output="screen",
            ),
            Node(
                package="agnocast_demo",
                executable="demo_listener",
                name="demo_listener",
                output="screen",
                additional_env=env,
            ),
            Node(
                package="agnocast_demo",
                executable="demo_talker",
                name="demo_talker",
                output="screen",
                additional_env=env,
            ),
        ]
    )
