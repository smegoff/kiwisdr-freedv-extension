#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  float real;
  float imag;
} kfd_rade_complex;

typedef struct {
  int synced;
  float snr_db;
  float frequency_offset_hz;
  size_t pcm_samples;
  char eoo_callsign[9];
} kfd_rade_output;

typedef struct kfd_rade_decoder kfd_rade_decoder;

kfd_rade_decoder* kfd_rade_create(char* error, size_t error_size);
void kfd_rade_destroy(kfd_rade_decoder* decoder);
int kfd_rade_reset(kfd_rade_decoder* decoder, char* error, size_t error_size);
size_t kfd_rade_input_needed(kfd_rade_decoder* decoder);
size_t kfd_rade_input_max(kfd_rade_decoder* decoder);
size_t kfd_rade_pcm_max(kfd_rade_decoder* decoder);
int kfd_rade_decode(kfd_rade_decoder* decoder,
                    const kfd_rade_complex* input,
                    size_t input_count,
                    int16_t* pcm,
                    size_t pcm_capacity,
                    kfd_rade_output* output,
                    char* error,
                    size_t error_size);

#ifdef __cplusplus
}
#endif
