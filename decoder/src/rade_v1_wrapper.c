#include "freedv/rade_v1_wrapper.h"

#include <rade_api.h>
#include <fargan.h>
#include <lpcnet.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct kfd_rade_decoder {
  struct rade* modem;
  FARGANState fargan;
  float* features;
  float* eoo_bits;
  int feature_count;
  int eoo_bit_count;
  int fargan_ready;
  int continuation_frames;
  int last_sync;
  float continuation[5 * NB_TOTAL_FEATURES];
};

_Static_assert(sizeof(kfd_rade_complex) == sizeof(RADE_COMP),
               "RADE complex sample layout changed");

static void set_error(char* error, size_t error_size, const char* message) {
  if (error && error_size) snprintf(error, error_size, "%s", message ? message : "RADE error");
}

static void reset_vocoder(kfd_rade_decoder* decoder) {
  fargan_init(&decoder->fargan);
  decoder->fargan_ready = 0;
  decoder->continuation_frames = 0;
  memset(decoder->continuation, 0, sizeof(decoder->continuation));
}

static int open_modem(kfd_rade_decoder* decoder, char* error, size_t error_size) {
  char model_name[] = "";
  decoder->modem = rade_open(model_name, RADE_VERBOSE_0);
  if (!decoder->modem) {
    set_error(error, error_size, "rade_open failed");
    return -1;
  }
  if (rade_version() != 2) {
    rade_close(decoder->modem);
    decoder->modem = NULL;
    set_error(error, error_size, "unsupported RADE C API version");
    return -1;
  }
  decoder->feature_count = rade_n_features_in_out(decoder->modem);
  decoder->eoo_bit_count = rade_n_eoo_bits(decoder->modem);
  if (decoder->feature_count <= 0 || decoder->feature_count % NB_TOTAL_FEATURES != 0 ||
      decoder->eoo_bit_count <= 0) {
    rade_close(decoder->modem);
    decoder->modem = NULL;
    set_error(error, error_size, "invalid RADE buffer dimensions");
    return -1;
  }
  decoder->features = calloc((size_t)decoder->feature_count, sizeof(float));
  decoder->eoo_bits = calloc((size_t)decoder->eoo_bit_count, sizeof(float));
  if (!decoder->features || !decoder->eoo_bits) {
    free(decoder->features);
    free(decoder->eoo_bits);
    decoder->features = NULL;
    decoder->eoo_bits = NULL;
    rade_close(decoder->modem);
    decoder->modem = NULL;
    set_error(error, error_size, "unable to allocate RADE buffers");
    return -1;
  }
  reset_vocoder(decoder);
  decoder->last_sync = 0;
  return 0;
}

kfd_rade_decoder* kfd_rade_create(char* error, size_t error_size) {
  kfd_rade_decoder* decoder = calloc(1, sizeof(*decoder));
  if (!decoder) {
    set_error(error, error_size, "unable to allocate RADE decoder");
    return NULL;
  }
  rade_initialize();
  if (open_modem(decoder, error, error_size) != 0) {
    rade_finalize();
    free(decoder);
    return NULL;
  }
  return decoder;
}

void kfd_rade_destroy(kfd_rade_decoder* decoder) {
  if (!decoder) return;
  if (decoder->modem) rade_close(decoder->modem);
  free(decoder->features);
  free(decoder->eoo_bits);
  rade_finalize();
  free(decoder);
}

int kfd_rade_reset(kfd_rade_decoder* decoder, char* error, size_t error_size) {
  if (!decoder) {
    set_error(error, error_size, "RADE decoder is not open");
    return -1;
  }
  if (decoder->modem) rade_close(decoder->modem);
  decoder->modem = NULL;
  free(decoder->features);
  free(decoder->eoo_bits);
  decoder->features = NULL;
  decoder->eoo_bits = NULL;
  return open_modem(decoder, error, error_size);
}

size_t kfd_rade_input_needed(kfd_rade_decoder* decoder) {
  return decoder && decoder->modem ? (size_t)rade_nin(decoder->modem) : 0;
}

size_t kfd_rade_input_max(kfd_rade_decoder* decoder) {
  return decoder && decoder->modem ? (size_t)rade_nin_max(decoder->modem) : 0;
}

size_t kfd_rade_pcm_max(kfd_rade_decoder* decoder) {
  if (!decoder || decoder->feature_count <= 0) return 0;
  return (size_t)(decoder->feature_count / NB_TOTAL_FEATURES) * LPCNET_FRAME_SIZE;
}

int kfd_rade_decode(kfd_rade_decoder* decoder,
                    const kfd_rade_complex* input,
                    size_t input_count,
                    int16_t* pcm,
                    size_t pcm_capacity,
                    kfd_rade_output* output,
                    char* error,
                    size_t error_size) {
  if (!decoder || !decoder->modem || !input || !pcm || !output) {
    set_error(error, error_size, "invalid RADE decode arguments");
    return -1;
  }
  memset(output, 0, sizeof(*output));
  const size_t needed = kfd_rade_input_needed(decoder);
  if (input_count != needed) {
    set_error(error, error_size, "incorrect RADE input frame size");
    return -1;
  }

  int has_eoo = 0;
  const int feature_samples = rade_rx(decoder->modem, decoder->features, &has_eoo,
                                      decoder->eoo_bits, (RADE_COMP*)(void*)input);
  const int modem_sync = rade_sync(decoder->modem) != 0;
  output->snr_db = modem_sync ? (float)rade_snrdB_3k_est(decoder->modem) : 0.0f;
  output->frequency_offset_hz = modem_sync ? rade_freq_offset(decoder->modem) : 0.0f;

  if (!modem_sync && decoder->last_sync) reset_vocoder(decoder);
  decoder->last_sync = modem_sync;

  if (has_eoo) {
    char callsign[RADE_EOO_CALLSIGN_MAX + 1] = {0};
    const int length = rade_rx_get_eoo_callsign(decoder->eoo_bits,
                                                decoder->eoo_bit_count, callsign);
    if (length > 0) snprintf(output->eoo_callsign, sizeof(output->eoo_callsign), "%s", callsign);
  }

  if (feature_samples < 0 || feature_samples % NB_TOTAL_FEATURES != 0) {
    set_error(error, error_size, "invalid RADE feature output");
    return -1;
  }
  for (int offset = 0; offset < feature_samples; offset += NB_TOTAL_FEATURES) {
    const float* feature = &decoder->features[offset];
    if (!decoder->fargan_ready) {
      memcpy(&decoder->continuation[decoder->continuation_frames * NB_TOTAL_FEATURES],
             feature, NB_TOTAL_FEATURES * sizeof(float));
      decoder->continuation_frames++;
      if (decoder->continuation_frames == 5) {
        float packed[5 * NB_FEATURES];
        float zeros[FARGAN_CONT_SAMPLES] = {0};
        for (int frame = 0; frame < 5; frame++) {
          memcpy(&packed[frame * NB_FEATURES],
                 &decoder->continuation[frame * NB_TOTAL_FEATURES],
                 NB_FEATURES * sizeof(float));
        }
        fargan_cont(&decoder->fargan, zeros, packed);
        decoder->fargan_ready = 1;
      }
      continue;
    }
    if (output->pcm_samples + LPCNET_FRAME_SIZE > pcm_capacity) {
      set_error(error, error_size, "RADE PCM output buffer exhausted");
      return -1;
    }
    fargan_synthesize_int(&decoder->fargan,
                          (opus_int16*)&pcm[output->pcm_samples], feature);
    output->pcm_samples += LPCNET_FRAME_SIZE;
  }
  output->synced = modem_sync || output->pcm_samples > 0;
  return 0;
}
