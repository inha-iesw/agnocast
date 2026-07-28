// Standard ROS 2 (rclcpp / DDS) subscriber for the memory evaluation — the "before
// Agnocast" baseline. Holds the received message; each subscriber process keeps its own
// private deserialized copy, so N subscribers cost N payloads of physical memory.

#include "agnocast_sample_interfaces/msg/dynamic_size_array.hpp"
#include "rclcpp/rclcpp.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

using std::placeholders::_1;
using Msg = agnocast_sample_interfaces::msg::DynamicSizeArray;

class StdHoldListener : public rclcpp::Node
{
  rclcpp::Subscription<Msg>::SharedPtr sub_;
  Msg::SharedPtr held_;  // keeps this subscriber's private copy resident

  void callback(const Msg::SharedPtr message)
  {
    held_ = message;
    std::printf(
      "HELD id=%ld bytes=%zu\n", static_cast<long>(message->id),
      message->data.size() * sizeof(int64_t));
    std::fflush(stdout);
  }

public:
  StdHoldListener() : Node("std_hold_listener")
  {
    const char * topic = std::getenv("EVAL_TOPIC");
    const auto qos = rclcpp::QoS(rclcpp::KeepLast(1)).reliable().transient_local();
    sub_ = create_subscription<Msg>(
      topic ? topic : "/eval_topic", qos, std::bind(&StdHoldListener::callback, this, _1));
    std::printf("SUBSCRIBED\n");
    std::fflush(stdout);
  }
};

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<StdHoldListener>());
  rclcpp::shutdown();
  return 0;
}
