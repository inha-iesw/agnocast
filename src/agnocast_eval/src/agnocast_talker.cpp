// Agnocast (zero-copy) publisher for the memory evaluation.
//
// Publishes one EVAL_BLOCK_BYTES message on EVAL_TOPIC after a startup delay, then
// idles. The published message stays resident in the shared-memory ring (depth 1); all
// subscribers map those same physical pages, so total payload memory is independent of
// subscriber count. Prints READY (before publish) and PUBLISHED so the driver can sample
// a clean pre-publish baseline.

#include "agnocast/agnocast.hpp"
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

class AgnocastTalker : public rclcpp::Node
{
  const size_t elems_;
  agnocast::Publisher<Msg>::SharedPtr pub_;
  rclcpp::TimerBase::SharedPtr once_;

  void publish_once()
  {
    once_->cancel();  // exactly one publish
    agnocast::ipc_shared_ptr<Msg> m = pub_->borrow_loaned_message();
    m->data.resize(elems_);
    int64_t checksum = 0;
    for (size_t i = 0; i < elems_; i++) {
      const int64_t value = static_cast<int64_t>(i) * 2654435761u + 1;
      m->data[i] = value;
      checksum += value;
    }
    m->id = checksum;
    std::printf("PUBLISHED id=%ld bytes=%zu\n", static_cast<long>(checksum), elems_ * sizeof(int64_t));
    std::fflush(stdout);
    pub_->publish(std::move(m));
  }

public:
  AgnocastTalker()
  : Node("agnocast_talker"), elems_(env_size("EVAL_BLOCK_BYTES", 16u << 20) / sizeof(int64_t))
  {
    const char * topic = std::getenv("EVAL_TOPIC");
    pub_ = agnocast::create_publisher<Msg>(this, topic ? topic : "/eval_topic", 1);
    const auto delay = std::chrono::milliseconds(static_cast<long long>(env_size("EVAL_START_DELAY_MS", 2500)));
    std::printf("READY bytes=%zu\n", elems_ * sizeof(int64_t));
    std::fflush(stdout);
    once_ = this->create_wall_timer(delay, std::bind(&AgnocastTalker::publish_once, this));
  }
};

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  agnocast::SingleThreadedAgnocastExecutor executor;
  auto node = std::make_shared<AgnocastTalker>();
  executor.add_node(node);
  executor.spin();
  rclcpp::shutdown();
  return 0;
}
