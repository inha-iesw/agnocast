// Burst node for the Agnocast shared-memory physical-page reclaim regression test.
//
// Freed blocks return their physical pages to the kernel via madvise(MADV_REMOVE) in
// the heaphook TLSF allocator (agnocast_heaphook/src/reclaim.rs). With the stock talker
// that is invisible because same-size messages are recycled from the free list. This
// node forces a high-water mark of concurrently-live large messages, then frees them
// all at once, so an external sampler can observe whether the shared-memory pool's Rss
// drops back to baseline (reclaim working) or stays at the peak (regression).
//
// Phases are announced on stdout as `MARKER <name> <epoch_ms>` so scripts/run_repro.sh
// can align them with scripts/sample_smaps.py output. All RSS measurement is done by
// that external sampler to avoid perturbing the pool with this process's own reads.

#include "agnocast/agnocast.hpp"
#include "agnocast_sample_interfaces/msg/dynamic_size_array.hpp"
#include "rclcpp/rclcpp.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

using Msg = agnocast_sample_interfaces::msg::DynamicSizeArray;

namespace
{
// Reads an unsigned parameter from the environment, falling back to `fallback`.
size_t env_size(const char * name, size_t fallback)
{
  const char * v = std::getenv(name);
  if (v == nullptr || *v == '\0') return fallback;
  char * end = nullptr;
  const unsigned long long parsed = std::strtoull(v, &end, 10);
  return (end == v) ? fallback : static_cast<size_t>(parsed);
}

// Prints a phase marker with a wall-clock timestamp and flushes immediately so the
// runner never blocks on stdio buffering while correlating phases with the sampler.
void marker(const char * name)
{
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
  std::printf("MARKER %s %lld\n", name, static_cast<long long>(ms));
  std::fflush(stdout);
}
}  // namespace

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);

  const size_t block_bytes = env_size("REPRO_BLOCK_BYTES", 4ull * 1024 * 1024);  // 4 MiB
  const size_t num_msgs = env_size("REPRO_NUM_MSGS", 256);                       // ~1 GiB peak
  const size_t phase_secs = env_size("REPRO_PHASE_SECS", 5);
  const size_t elems = block_bytes / sizeof(int64_t);

  std::printf("REPRO_PID %d\n", static_cast<int>(getpid()));
  std::printf(
    "REPRO_CONFIG block_bytes=%zu num_msgs=%zu peak_mib=%zu phase_secs=%zu\n", block_bytes,
    num_msgs, (block_bytes * num_msgs) / (1024 * 1024), phase_secs);
  std::fflush(stdout);

  auto node = std::make_shared<rclcpp::Node>("burst_leak_repro");
  auto publisher = agnocast::create_publisher<Msg>(node.get(), "/repro_topic", 1);

  const auto dwell = std::chrono::seconds(static_cast<long long>(phase_secs));

  // Phase 1 — baseline: nothing borrowed, pool Rss should sit near zero.
  marker("baseline");
  std::this_thread::sleep_for(dwell);

  // Phase 2 — peak: borrow num_msgs messages and touch every page of each so the
  // shared-memory pool faults in ~peak_mib of physical pages held simultaneously.
  std::vector<agnocast::ipc_shared_ptr<Msg>> held;
  held.reserve(num_msgs);  // allocated on the real heap (nothing borrowed yet)
  marker("peak_begin");
  for (size_t i = 0; i < num_msgs; i++) {
    agnocast::ipc_shared_ptr<Msg> m = publisher->borrow_loaned_message();
    m->id = static_cast<int64_t>(i);
    m->data.resize(elems, static_cast<int64_t>(i));  // resize zero/value-fills -> faults pages
    held.push_back(std::move(m));
  }
  marker("peak_hold");
  std::this_thread::sleep_for(dwell);

  // Phase 3 — drain: drop every borrowed (unpublished) message at once. Each destructor
  // frees its buffer through the heaphook -> TLSF deallocate, which punches out the
  // block's pages, so the sampler should see Rss fall back toward the baseline.
  marker("drain_begin");
  held.clear();
  marker("drained_hold");
  std::this_thread::sleep_for(dwell);

  marker("done");
  rclcpp::shutdown();
  return 0;
}
