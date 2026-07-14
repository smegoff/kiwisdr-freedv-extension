# FreeDV mode support

The production milestone is scoped to seven libcodec2 modes. Here,
"supported" means the mode is present in the Kiwi selector, accepted by the
authenticated job protocol, mapped to a libcodec2 `FREEDV_MODE_*` identifier,
and opened by the decoder smoke test. It does not mean every mode has completed
a live-RF speech test on this Kiwi installation.

## Mode comparison

| Mode | Intended channel | Voice codec | Modem and protection | Nominal RF bandwidth | Raw modem rate | Approx. minimum SNR | Practical character |
| --- | --- | --- | --- | ---: | ---: | ---: | --- |
| 1600 | HF SSB | Codec2 1300 | 14 DQPSK carriers, DBPSK pilot, Golay (23,12) FEC | 1,125 Hz | 1,600 bit/s | 4 dB | Low latency and easy to tune, but the least robust of these HF modes under multipath fading. |
| 700C | HF SSB | Codec2 700C | 14-carrier coherent QPSK with transmit diversity; no FEC | 1,500 Hz | 1,400 bit/s | 2 dB | Fast 40 ms frames and quick sync. Useful at medium-to-high SNR on fast-fading channels, but not the weak-signal choice. |
| 700D | HF SSB | Codec2 700C | 17-carrier OFDM/QPSK, LDPC (224,112) FEC | 1,000 Hz | 1,900 bit/s | -2 dB | Best low-SNR option in this set. Strong FEC helps with static and interference, at the cost of latency and sensitivity to tuning and fast fading. |
| 700E | HF SSB | Codec2 700C | 21-carrier OFDM/QPSK, LDPC (112,56) FEC | 1,500 Hz | 3,000 bit/s | 1 dB | Shorter frames and faster sync than 700D; needs about 3 dB more signal but handles fast fading better. |
| 2400A | VHF/UHF SDR path | Codec2 1300 | 4FSK, Golay (23,12) FEC | 5 kHz | 2,400 bit/s | Not specified | Constant-envelope waveform intended for an SDR radio and a wider VHF channel. Requires 48 kHz modem samples. |
| 2400B | VHF/UHF analog-FM path | Codec2 1300 | Baseband through analog FM, Golay (23,12) FEC | Set by FM radio/channel | 2,400 bit/s | Not specified | Intended to pass through commodity FM radios. Requires an FM receive path and 48 kHz modem samples. |
| 800XA | HF or VHF through SSB | Codec2 700C | 4FSK; no FEC | 2,000 Hz | 800 bit/s | Not specified | Constant-envelope, low raw-rate mode. It has no text side channel, so a decoded callsign cannot be assumed. |

The SNR figures are approximate additive-white-Gaussian-noise thresholds from
upstream documentation, not Kiwi S-meter targets. Real HF performance also
depends on multipath, fading rate, interference, sound-card/radio filtering and
tuning error. The raw modem rate is the upstream accounting of channel payload,
including speech-codec data, protection, text and synchronization rather than
the speech-codec rate alone.

These are the classic all-Codec2 modes selected for a small, predictable,
headless decoder. They are not the entire FreeDV catalogue. The LPCNet-based
2020 variants are outside this milestone, and RADE is a separate experimental
backend that remains disabled.

## Why 700C, 700D and 700E sound related

The names refer to complete FreeDV waveforms, not three different speech
codecs. All three compress speech with **Codec2 700C**, then place those bits
into different modems:

- 700C uses a short parallel-carrier waveform with transmit diversity and no
  forward-error-correction code.
- 700D uses narrow OFDM and strong LDPC protection to work at lower SNR.
- 700E trades about 3 dB of weak-signal performance for faster synchronization,
  lower latency and better fast-fading behavior.

This is why changing between 700C, 700D and 700E changes bandwidth, latency,
tuning tolerance and fading performance even though the speech codec is the
same.

## Current project readiness

| Capability | Current evidence |
| --- | --- |
| Mode exposed in the ordinary Kiwi menu | All seven modes |
| Protocol and backend allow-list | All seven modes |
| libcodec2 backend open/reset smoke test | All seven modes |
| Browser-to-Kiwi-to-decoder transport acceptance | 700D bundled reference test plus normal no-signal session |
| Live RF decoded speech | Pending for every mode |
| Reporter | Verified RX-only online presence during a normal session; callsign reports depend on valid decoded metadata |
| RADE | Compiled as an optional experimental adapter; disabled and outside this milestone |

There is an important integration gap for **2400A and 2400B**. Upstream
libcodec2 uses a 48 kHz modem sample rate for both modes, while the current
adapter advertises a fixed 8 kHz modem rate and the Kiwi panel forces a
300-3,000 Hz USB passband. 2400B additionally needs an analog-FM demodulation
path. These two modes can be selected and their libcodec2 contexts open, but
they should not be described as live-RF production-ready until the adapter
uses `freedv_get_modem_sample_rate()`, the Kiwi selects a suitable passband and
demodulator, and reference recordings decode end to end.

The HF SSB modes 1600, 700C, 700D, 700E and 800XA fit the existing 12 kHz Kiwi
transport architecture. They still need mode-by-mode reference-recording and
live-transmission acceptance before the project can claim verified speech
decoding in every advertised mode.

## Mode selection guidance

- Start with **700D** for weak HF signals or when you do not know which legacy
  mode is in use. Tune carefully; upstream guidance notes roughly a plus or
  minus 60 Hz acquisition range.
- Try **700E** when the channel is fading quickly and the signal is strong
  enough to give up roughly 3 dB relative to 700D.
- Try **700C** when fast synchronization and low latency matter and the signal
  is already comfortably above threshold.
- Use **1600** only when the transmitting station is using it; it is a common
  early FreeDV waveform but needs more SNR and handles fading less well.
- Treat **2400A/2400B** as integration targets for a VHF-capable receive chain,
  not as alternatives for an ordinary HF SSB transmission.
- Use **800XA** only for a matching 4FSK transmission; it is not interchangeable
  with the 700-series waveforms despite sharing the Codec2 700C speech codec.

## Upstream references

- [Codec2 FreeDV mode tables and design notes](https://github.com/drowe67/codec2/blob/310777b1c6f1af0bc7c72f5b32f80f6fd9136962/README_freedv.md)
- [FreeDV GUI user manual and mode guidance](https://github.com/drowe67/freedv-gui/blob/master/USER_MANUAL.md)
- [FreeDV 1600 specification](https://freedv.org/freedv-specification/)
