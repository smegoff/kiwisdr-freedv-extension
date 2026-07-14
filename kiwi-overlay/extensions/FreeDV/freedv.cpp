// Copyright (c) 2025-2026 John Seamons, ZL4VO/KF6VO
// FreeDV external camper transport additions copyright (c) 2026.

#include "ext.h"
#include "freedv.h"

#include "kiwi.h"
#include "misc.h"
#include "mem.h"
#include "rx.h"
#include "conn.h"
#include "cfg.h"
#include "str.h"
#include "sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

#define FREEDV_PROTOCOL 2
#define FREEDV_RELEASE "0.1.6"
#define FREEDV_STATUS_TIMEOUT 5
#define FREEDV_NONCES 64

typedef struct {
    int rx_chan;
    bool setup;
    bool running;
    bool decoder_online;
    char mode[16];
    u64_t frequency_hz;
    u4_t generation;
    u4_t last_status;
} freedv_t;

typedef struct {
    char value[17];
    time_t seen;
} freedv_nonce_t;

static freedv_t freedv[MAX_RX_CHANS];
static int active_rx = -1;
static u4_t next_generation = 1;
static freedv_nonce_t recent_nonces[FREEDV_NONCES];
static int nonce_position;
static char shared_secret[192];
static bool secret_loaded;

static bool freedv_mode_valid(const char *mode)
{
    static const char *modes[] = { "1600", "700C", "700D", "700E", "2400A", "2400B", "800XA", "RADEV1" };
    for (unsigned i = 0; i < ARRAY_LEN(modes); i++) {
        if (strcmp(mode, modes[i]) == 0) return true;
    }
    return false;
}

static void freedv_json_escape(char *out, size_t out_size, const char *in)
{
    size_t j = 0;
    if (!in) in = "";
    for (size_t i = 0; in[i] && j + 2 < out_size; i++) {
        unsigned char c = in[i];
        if (c == '"' || c == '\\') {
            out[j++] = '\\';
            out[j++] = c;
        } else if (c >= 0x20 && c < 0x7f) {
            out[j++] = c;
        }
    }
    out[j] = 0;
}

static bool freedv_encoded_status_valid(const char *value)
{
    if (!value) return false;
    size_t length = strlen(value);
    if (length < 6 || length > 2048) return false;
    if (!(value[0] == '{' ||
        (value[0] == '%' && value[1] == '7' && tolower(value[2]) == 'b'))) return false;
    if (!(value[length-1] == '}' ||
        (length >= 3 && value[length-3] == '%' && value[length-2] == '7' &&
         tolower(value[length-1]) == 'd'))) return false;
    for (size_t i = 0; i < length; i++) {
        unsigned char c = value[i];
        if (c <= 0x20 || c >= 0x7f) return false;
        if (c == '%') {
            if (i + 2 >= length || !isxdigit(value[i+1]) || !isxdigit(value[i+2])) return false;
            i += 2;
        }
    }
    return true;
}

static bool freedv_load_secret()
{
    if (secret_loaded) return strlen(shared_secret) >= 32;
    secret_loaded = true;
    FILE *file = fopen("/root/decoder.env", "r");
    if (!file) return false;
    char line[256];
    while (fgets(line, sizeof(line), file)) {
        static const char key[] = "FREEDV_SHARED_SECRET=";
        if (strncmp(line, key, sizeof(key)-1) != 0) continue;
        char *value = &line[sizeof(key)-1];
        value[strcspn(value, "\r\n")] = 0;
        kiwi_strncpy(shared_secret, value, sizeof(shared_secret));
        break;
    }
    fclose(file);
    return strlen(shared_secret) >= 32;
}

static void freedv_hmac_sha256(const char *key, const char *message, BYTE digest[SHA256_BLOCK_SIZE])
{
    BYTE key_block[64] = {0};
    size_t key_len = strlen(key);
    if (key_len > sizeof(key_block)) {
        SHA256_CTX key_ctx;
        sha256_init(&key_ctx);
        sha256_update(&key_ctx, (const BYTE *) key, key_len);
        sha256_final(&key_ctx, key_block);
        key_len = SHA256_BLOCK_SIZE;
    } else {
        memcpy(key_block, key, key_len);
    }

    BYTE inner_pad[64], outer_pad[64];
    for (unsigned i = 0; i < sizeof(key_block); i++) {
        inner_pad[i] = key_block[i] ^ 0x36;
        outer_pad[i] = key_block[i] ^ 0x5c;
    }
    BYTE inner[SHA256_BLOCK_SIZE];
    SHA256_CTX ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, inner_pad, sizeof(inner_pad));
    sha256_update(&ctx, (const BYTE *) message, strlen(message));
    sha256_final(&ctx, inner);
    sha256_init(&ctx);
    sha256_update(&ctx, outer_pad, sizeof(outer_pad));
    sha256_update(&ctx, inner, sizeof(inner));
    sha256_final(&ctx, digest);
}

static bool freedv_constant_hex_equal(const BYTE digest[SHA256_BLOCK_SIZE], const char *hex)
{
    static const char digits[] = "0123456789abcdef";
    unsigned difference = 0;
    if (strlen(hex) != SHA256_BLOCK_SIZE * 2) return false;
    for (unsigned i = 0; i < SHA256_BLOCK_SIZE; i++) {
        difference |= (unsigned) (digits[digest[i] >> 4] ^ hex[i*2]);
        difference |= (unsigned) (digits[digest[i] & 0xf] ^ hex[i*2+1]);
    }
    return difference == 0;
}

static bool freedv_nonce_seen(const char *nonce, time_t now)
{
    for (unsigned i = 0; i < FREEDV_NONCES; i++) {
        if (recent_nonces[i].seen && now - recent_nonces[i].seen <= 60 &&
            strcmp(recent_nonces[i].value, nonce) == 0) return true;
    }
    kiwi_strncpy(recent_nonces[nonce_position].value, nonce, sizeof(recent_nonces[nonce_position].value));
    recent_nonces[nonce_position].seen = now;
    nonce_position = (nonce_position + 1) % FREEDV_NONCES;
    return false;
}

static bool freedv_source_allowed(conn_t *conn_mon)
{
    if (!conn_mon || !conn_mon->isLocal_ip) return false;
    char *configured = (char *) cfg_string("freedv.decoder_ip", NULL, CFG_OPTIONAL);
    const char *expected = configured && *configured? configured : "192.168.10.145";
    const char *actual = conn_mon->remote_ip;
    if (strncmp(actual, "::ffff:", 7) == 0) actual += 7;
    bool allowed = strcmp(actual, expected) == 0;
    if (configured) cfg_string_free(configured);
    return allowed;
}

static void freedv_set_return_audio(int rx_chan, bool enabled)
{
    if (rx_chan < 0 || rx_chan >= rx_chans) return;
    freedv_return_audio(rx_chan, enabled);
}

static void freedv_stop(int rx_chan)
{
    if (rx_chan < 0 || rx_chan >= rx_chans) return;
    freedv_t *e = &freedv[rx_chan];
    if (e->running) {
        e->generation = next_generation++;
        e->running = false;
    }
    e->decoder_online = false;
    e->last_status = 0;
    freedv_set_return_audio(rx_chan, false);
    if (active_rx == rx_chan) active_rx = -1;
}

void freedv_close(int rx_chan)
{
    freedv_stop(rx_chan);
    ext_unregister_receive_cmds(rx_chan);
    freedv[rx_chan].setup = false;
}

// Called in the sound/waterfall/monitor task context.
bool freedv_receive_cmds(u2_t key, char *cmd, int rx_chan)
{
    (void) key;
    if (strncmp(cmd, "SET rev_txt=", 12) != 0) return false;
    freedv_t *e = &freedv[rx_chan];
    char *end;
    unsigned long generation = strtoul(&cmd[12], &end, 10);
    if (*end != ',' || !e->running || generation != e->generation) return true;
    if (!freedv_encoded_status_valid(end + 1)) {
        ext_send_msg_encoded(rx_chan, false, "EXT", "error", "invalid decoder status");
        return true;
    }
    e->last_status = timer_sec();
    e->decoder_online = true;
    freedv_set_return_audio(rx_chan, true);
    // The JSON is already URL encoded by the external decoder.
    ext_send_msg(rx_chan, false, "EXT status_json=%s", end + 1);
    return true;
}

static void freedv_poll(int rx_chan)
{
    freedv_t *e = &freedv[rx_chan];
    if (!e->running || !e->decoder_online || !e->last_status) return;
    if (timer_sec() - e->last_status <= FREEDV_STATUS_TIMEOUT) return;
    e->decoder_online = false;
    // Keep ownership of the receiver audio stream while FreeDV is running.
    // The return-audio gate supplies silence until the decoder reconnects.
    freedv_set_return_audio(rx_chan, true);
    ext_send_msg(rx_chan, false, "EXT state=decoder-offline");
}

bool freedv_msgs(char *msg, int rx_chan)
{
    freedv_t *e = &freedv[rx_chan];
    e->rx_chan = rx_chan;

    if (strcmp(msg, "SET ext_server_init") == 0) {
        ext_send_msg(rx_chan, false, "EXT ready protocol=%d backend=external release=%s",
            FREEDV_PROTOCOL, FREEDV_RELEASE);
        return true;
    }
    if (strcmp(msg, "SET freedv_setup") == 0) {
        if (!e->setup) {
            ext_register_receive_cmds(freedv_receive_cmds, rx_chan);
            e->setup = true;
        }
        return true;
    }

    int start;
    char mode[16];
    if (sscanf(msg, "SET freedv_start=%d mode=%15s", &start, mode) == 2) {
        if (!start) {
            freedv_stop(rx_chan);
            ext_send_msg(rx_chan, false, "EXT state=stopped");
            return true;
        }
        if (!freedv_mode_valid(mode)) {
            ext_send_msg_encoded(rx_chan, false, "EXT", "error", "unsupported FreeDV mode");
            return true;
        }
        if (active_rx != -1 && active_rx != rx_chan && freedv[active_rx].running) {
            ext_send_msg_encoded(rx_chan, false, "EXT", "error", "FreeDV decoder is already in use");
            return true;
        }
        active_rx = rx_chan;
        e->running = true;
        e->decoder_online = false;
        e->last_status = 0;
        e->generation = next_generation++;
        kiwi_strncpy(e->mode, mode, sizeof(e->mode));
        e->frequency_hz = (u64_t) (ext_get_displayed_freq_kHz(rx_chan) * 1000.0 + 0.5);
        freedv_set_return_audio(rx_chan, true);
        ext_send_msg(rx_chan, false, "EXT state=waiting-for-decoder generation=%u", e->generation);
        return true;
    }
    if (strcmp(msg, "SET freedv_stop") == 0 || strcmp(msg, "SET freedv_close") == 0) {
        freedv_stop(rx_chan);
        ext_send_msg(rx_chan, false, "EXT state=stopped");
        return true;
    }
    return false;
}

bool freedv_monitor_poll(struct conn_st *conn_st, const char *arguments)
{
    conn_t *conn_mon = (conn_t *) conn_st;
    if (!freedv_source_allowed(conn_mon)) {
        send_msg(conn_mon, false, "MSG freedv_auth=source");
        return true;
    }
    if (!freedv_load_secret()) {
        send_msg(conn_mon, false, "MSG freedv_auth=secret");
        return true;
    }

    int protocol = 0;
    long long timestamp = 0;
    char nonce[17] = {0};
    char supplied_hmac[65] = {0};
    char extra = 0;
    if (sscanf(arguments, "%d,%lld,%16[0-9a-fA-F],%64[0-9a-fA-F]%c",
            &protocol, &timestamp, nonce, supplied_hmac, &extra) != 4 || protocol != FREEDV_PROTOCOL) {
        send_msg(conn_mon, false, "MSG freedv_auth=format");
        return true;
    }
    for (char *p = nonce; *p; p++) *p = tolower(*p);
    for (char *p = supplied_hmac; *p; p++) *p = tolower(*p);
    time_t now = time(NULL);
    if (llabs((long long) now - timestamp) > 30) {
        send_msg(conn_mon, false, "MSG freedv_auth=timestamp");
        return true;
    }
    if (freedv_nonce_seen(nonce, now)) {
        send_msg(conn_mon, false, "MSG freedv_auth=replay");
        return true;
    }

    char signed_value[96];
    kiwi_snprintf_buf(signed_value, "%d|%lld|%s", protocol, timestamp, nonce);
    BYTE digest[SHA256_BLOCK_SIZE];
    freedv_hmac_sha256(shared_secret, signed_value, digest);
    if (!freedv_constant_hex_equal(digest, supplied_hmac)) {
        send_msg(conn_mon, false, "MSG freedv_auth=hmac");
        return true;
    }

    char job[1024];
    if (active_rx >= 0 && active_rx < rx_chans && freedv[active_rx].running) {
        freedv_t *e = &freedv[active_rx];
        u64_t current_frequency = (u64_t) (ext_get_displayed_freq_kHz(active_rx) * 1000.0 + 0.5);
        if (current_frequency != e->frequency_hz) {
            e->frequency_hz = current_frequency;
            e->generation = next_generation++;
            e->decoder_online = false;
        }
        char *call = (char *) cfg_string("freedv.reporter_callsign", NULL, CFG_OPTIONAL);
        char *grid = (char *) cfg_string("freedv.reporter_grid", NULL, CFG_OPTIONAL);
        char *message = (char *) cfg_string("freedv.reporter_message", NULL, CFG_OPTIONAL);
        char call_j[64], grid_j[32], message_j[256];
        freedv_json_escape(call_j, sizeof(call_j), call);
        freedv_json_escape(grid_j, sizeof(grid_j), grid);
        freedv_json_escape(message_j, sizeof(message_j), message);
        kiwi_snprintf_buf(job,
            "{\"protocol\":%d,\"generation\":%u,\"running\":true,\"rx_chan\":%d,"
            "\"mode\":\"%s\",\"input_rate\":%u,\"frequency_hz\":%llu,"
            "\"reporter\":{\"enabled\":%s,\"callsign\":\"%s\",\"grid\":\"%s\",\"message\":\"%s\"}}",
            FREEDV_PROTOCOL, e->generation, active_rx, e->mode,
            (u4_t) ext_update_get_sample_rateHz(active_rx), (unsigned long long) e->frequency_hz,
            cfg_true("freedv.reporter_enabled")? "true":"false", call_j, grid_j, message_j);
        if (call) cfg_string_free(call);
        if (grid) cfg_string_free(grid);
        if (message) cfg_string_free(message);
    } else {
        kiwi_snprintf_buf(job, "{\"protocol\":%d,\"generation\":%u,\"running\":false}",
            FREEDV_PROTOCOL, next_generation? next_generation - 1 : 0);
    }
    char *encoded = kiwi_str_encode(job);
    send_msg(conn_mon, false, "MSG freedv_job=%s", encoded);
    kiwi_ifree(encoded, "freedv_job");
    return true;
}

bool FreeDV_vars()
{
    bool update = false;
    cfg_default_object("freedv", "{}", &update);
    cfg_default_string("freedv.decoder_ip", "192.168.10.145", &update);
    cfg_default_bool("freedv.reporter_enabled", false, &update);
    cfg_default_string("freedv.reporter_callsign", "", &update);
    cfg_default_string("freedv.reporter_grid", "", &update);
    cfg_default_string("freedv.reporter_message", "", &update);
    return update;
}

void FreeDV_main();

static ext_t freedv_ext = {
    "FreeDV",
    FreeDV_main,
    freedv_close,
    freedv_msgs,
    EXT_NEW_VERSION,
    EXT_NO_FLAGS,
    freedv_poll
};

void FreeDV_main()
{
    ext_register(&freedv_ext);
}
