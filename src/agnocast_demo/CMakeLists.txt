cmake_minimum_required(VERSION 3.14)
project(agnocast_demo)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(agnocastlib REQUIRED)
find_package(agnocast_sample_interfaces REQUIRED)

add_executable(demo_talker src/demo_talker.cpp)
ament_target_dependencies(demo_talker rclcpp agnocastlib agnocast_sample_interfaces)
target_include_directories(demo_talker PRIVATE ${agnocastlib_INCLUDE_DIRS})

add_executable(demo_listener src/demo_listener.cpp)
ament_target_dependencies(demo_listener rclcpp agnocastlib agnocast_sample_interfaces)
target_include_directories(demo_listener PRIVATE ${agnocastlib_INCLUDE_DIRS})

install(TARGETS demo_talker demo_listener
  DESTINATION lib/${PROJECT_NAME})

install(DIRECTORY launch
  DESTINATION share/${PROJECT_NAME}/)

ament_package()
