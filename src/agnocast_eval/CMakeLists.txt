cmake_minimum_required(VERSION 3.14)
project(agnocast_eval)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(agnocastlib REQUIRED)
find_package(agnocast_sample_interfaces REQUIRED)

foreach(target agnocast_talker agnocast_hold_listener std_talker std_hold_listener)
  add_executable(${target} src/${target}.cpp)
  ament_target_dependencies(${target} rclcpp agnocastlib agnocast_sample_interfaces)
  target_include_directories(${target} PRIVATE ${agnocastlib_INCLUDE_DIRS})
endforeach()

# Size probe needs no agnocastlib (pure rclcpp serialization + the message type).
add_executable(msg_size_probe src/msg_size_probe.cpp)
ament_target_dependencies(msg_size_probe rclcpp agnocast_sample_interfaces)

install(TARGETS agnocast_talker agnocast_hold_listener std_talker std_hold_listener msg_size_probe
  DESTINATION lib/${PROJECT_NAME})

ament_package()
