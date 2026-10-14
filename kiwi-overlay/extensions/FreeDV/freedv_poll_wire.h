#pragma once
#include <stdio.h>

/* Shared by Kiwi and the wire conformance test. Six fields means trailing or
 * forbidden input and must be rejected by the authenticated poll handler. */
static inline int freedv_parse_poll_arguments(const char *arguments,
    int *protocol, long long *timestamp, char nonce[17], char hmac[65],
    char frequencies[768], char *extra)
{
    int prefix_end = 0;
    int fields = sscanf(arguments,
        "%d,%lld,%16[0-9a-fA-F],%64[0-9a-fA-F]%n,%767[0-9A-Z/:;-]%c",
        protocol, timestamp, nonce, hmac, &prefix_end, frequencies, extra);
    if (fields == 4 && arguments[prefix_end]) return 6;
    return fields;
}
