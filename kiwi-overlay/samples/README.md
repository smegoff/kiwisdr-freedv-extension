# FreeDV reference sample

`FreeDV.clean.au` is a generated build artifact used by the Kiwi extension's
700D Test button. It is produced with `freedv-codec2-reference-generator` from
the pinned `freedv/rade_c` `input_sample.wav` speech sample, which is distributed
under the BSD 2-Clause licence.

The generated file is mono, 16-bit big-endian AU at 12 kHz. It contains a clean
continuous FreeDV 700D waveform with encoded-silence acquisition frames before
and after the speech. Regenerate and validate it on the decoder guest with:

```sh
freedv-codec2-reference-generator input_sample.wav FreeDV.clean.au
freedv-reference-test 700D FreeDV.clean.au
```

The expected SHA-256 for the current artifact is:

```text
426dcc677932903d863fa266fc3acfc0bbcafc63906e231a9b16fc4429e6d37a
```
