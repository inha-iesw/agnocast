// Standard ROS 2 (rclcpp / DDS) publisher for the memory evaluation — the "before
// Agnocast" baseline. Each subscriber receives its own deserialized copy, so total
// payload memory grows with subscriber count. QoS is transient_local + KeepLast(1) so a
// single publish reliably reaches every already-running subscriber. Mirrors
// agnocast_talker's timing (READY -> one publish -> idle) for an apples-to-apples run.

#include "agnocast_sample_interfaces/msg/dynamic_size_array.hpp"
#include "rclcpp/rclcpp.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

using Msg = agnocast_sample_interfaces::msg::DynamicSizeArray;

static size_t env_size(const char * name, size_t fallback)
{
  const char * v = std::getenv(name);
  if (v == nullptr || *v == '\0') return fallback;
  char * end = nullptr;
  const unsigned long long p = std::strtoull(v, &end, 10);
  return (end == v) ? fallback : static_cast<size_t>(p);
}

class StdTalker : public rclcpp::Node
{
  const size_t elems_;
  rclcpp::Publisher<Msg>::SharedPtr pub_;
  Msg::SharedPtr held_;  // keeps the publisher's own copy resident
  rclcpp::TimerBase::SharedPtr once_;

  void publish_once()
  {
    once_->cancel();  // exactly one publish
    auto m = std::make_shared<Msg>();
    m->data.resize(elems_);
    int64_t checksum = 0;
    for (size_t i = 0; i < elems_; i++) {
      const int64_t value = static_cast<int64_t>(i) * 2654435761u + 1;
      m->data[i] = value;
      checksum += value;
    }
    m->id = checksum;
    pub_->publish(*m);
    held_ = m;
    std::printf("PUBLISHED id=%ld bytes=%zu\n", static_cast<long>(checksum), elems_ * sizeof(int64_t));
    std::fflush(stdout);
  }

public:
  StdTalker() : Node("std_talker"), elems_(env_size("EVAL_BLOCK_BYTES", 16u << 20) / sizeof(int64_t))
  {
    const char * topic = std::getenv("EVAL_TOPIC");
    const auto qos = rclcpp::QoS(rclcpp::KeepLast(1)).reliable().transient_local();
    pub_ = create_publisher<Msg>(topic ? topic : "/eval_topic", qos);
    const auto delay = std::chrono::milliseconds(static_cast<long long>(env_size("EVAL_START_DELAY_MS", 2500)));
    std::printf("READY bytes=%zu\n", elems_ * sizeof(int64_t));
    std::fflush(stdout);
    once_ = this->create_wall_timer(delay, std::bind(&StdTalker::publish_once, this));
  }
};

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<StdTalker>());
  rclcpp::shutdown();
  return 0;
}
