#include "freedv_poll_wire.h"
#include <cassert>
#include <cstring>
#include <string>

int main() {
    const std::string prefix = "2,1791968000,0123456789abcdef," + std::string(64, 'a');
    auto scan = [&](const std::string& suffix, int expected) {
        int protocol = 0;
        long long timestamp = 0;
        char nonce[17] = {}, hmac[65] = {}, frequencies[768] = {}, extra = 0;
        const int fields = freedv_parse_poll_arguments((prefix + suffix).c_str(),
            &protocol, &timestamp, nonce, hmac, frequencies, &extra);
        assert(fields == expected);
        assert(protocol == 2 && timestamp == 1791968000);
        if (expected == 5) assert(std::string(frequencies) == suffix.substr(1));
    };
    scan("", 4);
    scan(",-", 5);
    scan(",7177000;14236000", 5);
    scan(",7177000:ZL2ABC/P:RADEV2;14236000:JA0AQX:RADEV1", 5);
    scan(",7177000:<script>:RADEV2", 6);
    scan(",7177000,extra", 6);
    scan(",7177000 bad", 6);
    scan(",", 6);
    scan(",bad", 6);
    scan(" unexpected", 6);
}
