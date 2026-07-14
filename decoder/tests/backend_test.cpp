#include "freedv/backend.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

int main() {
  constexpr std::array<const char*, 7> modes = {
      "1600", "700C", "700D", "700E", "2400A", "2400B", "800XA"};
  std::vector<int16_t> silence(8000 / 2);
  for (const char* mode : modes) {
    auto backend = kfd::make_codec2_backend(mode);
    assert(std::string(backend->name()) == "codec2");
    assert(backend->modem_sample_rate() == 8000);
    const auto result = backend->push(silence.data(), silence.size());
    assert(result.sample_rate == 8000);
    backend->reset();
    std::cout << mode << ": ok\n";
  }
  bool rade_rejected = false;
  try {
    (void) kfd::make_rade_backend(false);
  } catch (const std::runtime_error&) {
    rade_rejected = true;
  }
  assert(rade_rejected);
  std::cout << "RADE disabled: ok\n";
  return 0;
}
