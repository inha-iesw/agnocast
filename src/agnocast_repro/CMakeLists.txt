cmake_minimum_required(VERSION 3.14)
project(agnocast_repro)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(agnocastlib REQUIRED)
find_package(agnocast_sample_interfaces REQUIRED)

add_executable(burst_leak_repro src/burst_leak_repro.cpp)
ament_target_dependencies(burst_leak_repro rclcpp agnocastlib agnocast_sample_interfaces)
target_include_directories(burst_leak_repro PRIVATE ${agnocastlib_INCLUDE_DIRS})

install(TARGETS burst_leak_repro
  DESTINATION lib/${PROJECT_NAME})

ament_package()
