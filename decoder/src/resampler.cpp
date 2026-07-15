#include "freedv/resampler.hpp"

#include <samplerate.h>

#include <cmath>
#include <stdexcept>

namespace kfd {

StreamingResampler::~StreamingResampler() { clear(); }

std::vector<int16_t> StreamingResampler::process(
    const std::vector<int16_t>& input, uint32_t input_rate, uint32_t output_rate) {
  if (input.empty() || input_rate == 0 || output_rate == 0) return {};
  if (input_rate == output_rate) return input;
  if (!state_ || input_rate != input_rate_ || output_rate != output_rate_) {
    clear();
    int error = 0;
    state_ = src_new(SRC_SINC_FASTEST, 1, &error);
    if (!state_) throw std::runtime_error(src_strerror(error));
    input_rate_ = input_rate;
    output_rate_ = output_rate;
  }
  std::vector<float> source(input.size());
  src_short_to_float_array(input.data(), source.data(), static_cast<int>(input.size()));
  const double ratio = static_cast<double>(output_rate) / input_rate;
  std::vector<float> destination(
      static_cast<std::size_t>(std::ceil(input.size() * ratio)) + 256);
  SRC_DATA data{};
  data.data_in = source.data();
  data.input_frames = static_cast<long>(source.size());
  data.data_out = destination.data();
  data.output_frames = static_cast<long>(destination.size());
  data.src_ratio = ratio;
  const int error = src_process(state_, &data);
  if (error) throw std::runtime_error(src_strerror(error));
  if (data.input_frames_used != data.input_frames)
    throw std::runtime_error("resampler output bound exhausted");
  std::vector<int16_t> output(static_cast<std::size_t>(data.output_frames_gen));
  src_float_to_short_array(destination.data(), output.data(), static_cast<int>(output.size()));
  return output;
}

void StreamingResampler::reset() {
  if (state_) src_reset(state_);
}

void StreamingResampler::clear() {
  if (state_) src_delete(state_);
  state_ = nullptr;
  input_rate_ = output_rate_ = 0;
}

}  // namespace kfd
