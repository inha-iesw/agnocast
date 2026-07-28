// Standalone probe: how ROS 2 internal code knows a message's size.
//
// For each payload the evaluation uses, builds a DynamicSizeArray and reports two sizes:
//   * in-memory payload  — data.size() * sizeof(int64_t), the resident C++ buffer that
//     Agnocast shares zero-copy and that each standard-ROS 2 subscriber copies.
//   * serialized CDR size — rclcpp::Serialization<Msg> -> SerializedMessage::size(), the
//     exact byte count DDS transports per subscriber.
//
// Kept out of the measured pub/sub nodes on purpose: serializing a large message there
// would allocate a throwaway CDR buffer in the publisher's shared-memory heap while the
// message is borrowed, perturbing the very PSS the evaluation measures.

#include "agnocast_sample_interfaces/msg/dynamic_size_array.hpp"
#include "rclcpp/serialization.hpp"
#include "rclcpp/serialized_message.hpp"

#include <cstdint>
#include <cstdio>

using Msg = agnocast_sample_interfaces::msg::DynamicSizeArray;

int main()
{
  std::printf("%-12s %-22s %-22s\n", "payload", "in-memory (data*8)", "serialized CDR");
  for (const size_t mib : {1u, 4u, 16u, 64u}) {
    Msg m;
    m.id = 0;
    m.data.resize((mib << 20) / sizeof(int64_t));

    const size_t in_memory = m.data.size() * sizeof(int64_t);

    rclcpp::Serialization<Msg> serializer;
    rclcpp::SerializedMessage serialized;
    serializer.serialize_message(&m, &serialized);
    const size_t cdr = serialized.size();

    std::printf(
      "%2zu MiB       %10zu bytes         %10zu bytes\n", mib, in_memory, cdr);
  }
  return 0;
}