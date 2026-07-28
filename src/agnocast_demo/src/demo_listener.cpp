// Demo subscriber that verifies Agnocast delivered a message zero-copy.
//
// On each receipt it (1) re-folds the payload checksum and compares it to `id` written
// by the publisher, and (2) checks the payload's address lies inside a mapped
// `/dev/shm/agnocast@<pid>` pool. An address in the pool means the subscriber is reading
// the publisher's shared-memory bytes directly — no deserialization, no copy. The
// publisher's pool is mapped into this process on first receipt, so the maps are read
// fresh each callback (this process never publishes, so its reads use the real heap).

#include "agnocast/agnocast.hpp"
#include "agnocast_sample_interfaces/msg/dynamic_size_array.hpp"
#include "rclcpp/rclcpp.hpp"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>

using std::placeholders::_1;
using Msg = agnocast_sample_interfaces::msg::DynamicSizeArray;

namespace
{
// True if `addr` falls within any mapped Agnocast shared-memory pool of this process.
bool addr_in_agnocast_pool(uintptr_t addr)
{
  std::ifstream maps("/proc/self/maps");
  std::string line;
  while (std::getline(maps, line)) {
    if (line.find("/dev/shm/agnocast@") == std::string::npos) continue;
    const auto dash = line.find('-');
    const auto space = line.find(' ');
    if (dash == std::string::npos || space == std::string::npos) continue;
    const uintptr_t start = std::stoull(line.substr(0, dash), nullptr, 16);
    const uintptr_t end = std::stoull(line.substr(dash + 1, space - dash - 1), nullptr, 16);
    if (addr >= start && addr < end) return true;
  }
  return false;
}
}  // namespace

class DemoListener : public rclcpp::Node
{
  agnocast::Subscription<Msg>::SharedPtr sub_;

  void callback(const agnocast::ipc_shared_ptr<Msg> & message)
  {
    const int64_t * data = message->data.data();
    int64_t checksum = 0;
    for (const int64_t v : message->data) checksum += v;

    const bool checksum_ok = (checksum == message->id);
    const bool in_pool = addr_in_agnocast_pool(reinterpret_cast<uintptr_t>(data));

    std::printf(
      "[listener] payload_addr=%p size=%zuB checksum=%ld expected=%ld  %s  %s\n",
      static_cast<const void *>(data), message->data.size() * sizeof(int64_t),
      static_cast<long>(checksum), static_cast<long>(message->id),
      checksum_ok ? "CHECKSUM-MATCH" : "CHECKSUM-MISMATCH",
      in_pool ? "ADDR-IN-SHM-POOL => ZERO-COPY" : "ADDR-NOT-IN-POOL => COPIED!");
    std::fflush(stdout);
  }

public:
  DemoListener() : Node("agnocast_demo_listener")
  {
    rclcpp::CallbackGroup::SharedPtr group =
      create_callback_group(rclcpp::CallbackGroupType::MutuallyExclusive);
    agnocast::SubscriptionOptions options;
    options.callback_group = group;
    sub_ = agnocast::create_subscription<Msg>(
      this, "/agnocast_demo", 1, std::bind(&DemoListener::callback, this, _1), options);
    RCLCPP_INFO(get_logger(), "subscribed to /agnocast_demo");
  }
};

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  agnocast::SingleThreadedAgnocastExecutor executor;
  auto node = std::make_shared<DemoListener>();
  executor.add_node(node);
  executor.spin();
  rclcpp::shutdown();
  return 0;
}
