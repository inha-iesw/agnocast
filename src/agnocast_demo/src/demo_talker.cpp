// Demo publisher showing Agnocast working normally (zero-copy IPC happy path).
//
// Each tick it borrows a loaned message directly in shared memory, fills it with a
// deterministic pattern, folds a checksum into `id`, and prints the payload's
// shared-memory address before publishing. Pair with demo_listener, which reads the
// same shared-memory bytes in another process and verifies the checksum without a copy.

#include "agnocast/agnocast.hpp"
#include "agnocast_sample_interfaces/msg/dynamic_size_array.hpp"
#include "rclcpp/rclcpp.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

using namespace std::chrono_literals;
using Msg = agnocast_sample_interfaces::msg::DynamicSizeArray;

namespace
{
size_t env_size(const char * name, size_t fallback)
{
  const char * v = std::getenv(name);
  if (v == nullptr || *v == '\0') return fallback;
  char * end = nullptr;
  const unsigned long long parsed = std::strtoull(v, &end, 10);
  return (end == v) ? fallback : static_cast<size_t>(parsed);
}
}  // namespace

class DemoTalker : public rclcpp::Node
{
  const size_t elems_;
  int64_t seq_ = 0;
  rclcpp::TimerBase::SharedPtr timer_;
  agnocast::Publisher<Msg>::SharedPtr pub_;

  void tick()
  {
    agnocast::ipc_shared_ptr<Msg> m = pub_->borrow_loaned_message();

    m->data.resize(elems_);
    int64_t checksum = 0;
    for (size_t i = 0; i < elems_; i++) {
      const int64_t value = seq_ * 1000003 + static_cast<int64_t>(i);  // deterministic pattern
      m->data[i] = value;
      checksum += value;
    }
    m->id = checksum;  // subscriber re-folds the payload and compares against this

    std::printf(
      "[talker]   seq=%ld payload_addr=%p size=%zuB checksum=%ld\n", static_cast<long>(seq_),
      static_cast<const void *>(m->data.data()), elems_ * sizeof(int64_t), static_cast<long>(checksum));
    std::fflush(stdout);

    pub_->publish(std::move(m));
    seq_++;
  }

public:
  DemoTalker() : Node("agnocast_demo_talker"), elems_(env_size("DEMO_BLOCK_BYTES", 1u << 20) / sizeof(int64_t))
  {
    pub_ = agnocast::create_publisher<Msg>(this, "/agnocast_demo", 1);
    timer_ = this->create_wall_timer(1s, std::bind(&DemoTalker::tick, this));
    RCLCPP_INFO(get_logger(), "publishing %zuB messages on /agnocast_demo every 1s", elems_ * sizeof(int64_t));
  }
};

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  agnocast::SingleThreadedAgnocastExecutor executor;
  auto node = std::make_shared<DemoTalker>();
  executor.add_node(node);
  executor.spin();
  rclcpp::shutdown();
  return 0;
}
