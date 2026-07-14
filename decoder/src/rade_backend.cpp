#include "freedv/backend.hpp"

#include <stdexcept>

namespace kfd {

std::unique_ptr<DecoderBackend> make_rade_backend(bool enabled) {
  if (!enabled) throw std::runtime_error("RADE backend is disabled");
  throw std::runtime_error("RADE adapter not compiled; build pinned radae_decoder tools first");
}

} // namespace kfd
