# Experimental RADEv2

The decoder uses the official [FreeDV C implementation](https://github.com/freedv/rade_c)
at `c8a3dc156045cae2cd251e1a4be0c304c9ddf2f9`, with the same pinned
FARGAN/Opus revision as the existing V1 service. The upstream project describes
V2 as a development waveform whose models and API can change. V2 is therefore
off by default, independently of the V1 switch.

Enable `FREEDV_ENABLE_RADEV2=1` in the private decoder environment and enable
the experimental RADEV2 switch in Kiwi Admin only after reference acceptance.
Processing stays on the external Debian decoder; this is not an AI-64 change.
The adapter selects `RADE_MODE_V2`, uses the reported input/feature dimensions,
and permits zero EOO bits because V2 does not supply V1 callsign metadata.

Generate a V2 recording with the pinned `rade_tx_wav --v2` and decode it through
the candidate backend. Set `FREEDV_TEST_RADEV2=1` when running
`freedv-rade-reference-test reference-v2.wav decoded-v2.wav`. The same test
checks synchronization, returned PCM, bounded overflow/reset, real-time factor
and the Kiwi 12 kHz packet cadence. A V1 waveform cannot validate V2.
The browser uses a separate bundled V2 waveform and never substitutes the
700D or V1 sample. `FreeDV.rade2.au` was generated from the pinned library's
`wav/david_vk5dgr.wav`; its SHA-256 is
`b62acd5ef5cb628429762db355aff234e52aeb76999ee099f6901d5a2ed09ff1`.

The station selector accepts public Reporter TX state, callsign, codec and
frequency. TX rows appear first and choose the codec when locally available.
Locators, messages and Reporter session IDs are not relayed. Updates are
authenticated with the existing Kiwi control HMAC, character/size limited,
and expire when the feed becomes stale. Reporter TX state is advisory and does
not establish that a signal is receivable at this Kiwi.
