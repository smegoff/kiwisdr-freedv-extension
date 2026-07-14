#pragma once

#include <cstdint>
#include <vector>

struct SRC_STATE_tag;

namespace kfd {

class StreamingResampler {
 public:
  StreamingResampler() = default;
  ~StreamingResampler();

  StreamingResampler(const StreamingResampler&) = delete;
  StreamingResampler& operator=(const StreamingResampler&) = delete;

  std::vector<int16_t> process(const std::vector<int16_t>& input,
                               uint32_t input_rate, uint32_t output_rate);
  void reset();

 private:
  void clear();

  SRC_STATE_tag* state_ = nullptr;
  uint32_t input_rate_ = 0;
  uint32_t output_rate_ = 0;
};

}  // namespace kfd
