# FreeDV reference samples

`FreeDV.clean.au` is a generated build artifact used by the Kiwi extension's
**Test 700D** button. It is produced with
`freedv-codec2-reference-generator` from the pinned `freedv/rade_c`
`input_sample.wav` speech sample, which is distributed under the BSD 2-Clause
licence.

`FreeDV.rade.au` is used by **Test RADE**. The pinned portable RADEV1
`rade_tx_wav` utility modulates the same speech sample at 8 kHz, then SoX
resamples and packages it for the Kiwi.

`FreeDV.rade2.au` is used by **Test RADEv2**. It was generated with
`rade_tx_wav --v2` from `wav/david_vk5dgr.wav` in official
`freedv/rade_c@c8a3dc156045cae2cd251e1a4be0c304c9ddf2f9`, then converted
with SoX to the Kiwi format. The upstream BSD 2-Clause notice is retained in
[RADE-LICENSE.txt](RADE-LICENSE.txt).

All three files are mono, 16-bit big-endian AU at 12 kHz. The 700D artifact contains
encoded-silence acquisition frames before and after the speech. Regenerate and
validate them on the decoder guest with:

```sh
freedv-codec2-reference-generator input_sample.wav FreeDV.clean.au
freedv-reference-test 700D FreeDV.clean.au
./tools/generate-radev1-test-sample.sh input_sample.wav FreeDV.rade.au

# Verify the RADEV1 source waveform before AU packaging.
rade_tx_wav -v 0 input_sample.wav /tmp/radev1-reference.wav
freedv-rade-reference-test /tmp/radev1-reference.wav
```

The expected SHA-256 values are:

```text
FreeDV.clean.au  426dcc677932903d863fa266fc3acfc0bbcafc63906e231a9b16fc4429e6d37a
FreeDV.rade.au   85ff073a2cc67c348b5ac956bea9b6bd028dacab00e8e7333dc29a73788e8c86
FreeDV.rade2.au  b62acd5ef5cb628429762db355aff234e52aeb76999ee099f6901d5a2ed09ff1
```
