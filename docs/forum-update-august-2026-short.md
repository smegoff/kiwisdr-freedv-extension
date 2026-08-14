# FreeDV extension update: v0.1.33 to v0.1.38

My previous KiwiSDR FreeDV update covered **v0.1.33**, including the separate
Test 700D and Test RADE reference paths. Since then the Kiwi extension has
reached **v0.1.38** and the external decoder **v0.1.26**.

This remains a normal AM335x KiwiSDR, with Codec2 and RADEV1 processing
offloaded to a private Debian guest under Proxmox. It is not an AI-64 build.

Changes since v0.1.33:

- **Public proxy validation:** Test 700D and Test RADE both synchronized and
  returned decoded audio for an ordinary listener through John's
  `proxy.kiwisdr.com` service. The private decoder remains unexposed.
- **Stale-control recovery:** A public test exposed a decoder socket that
  remained open after authenticated Kiwi polling stopped. Decoder v0.1.25 now
  measures response age, declares the connection stale after ten seconds and
  reconnects automatically. Health and dashboard counters expose this state.
- **Manual tuning:** v0.1.34 added a compact FreeDV frequency field accepting
  MHz, kHz or Hz. It applies the normal amateur sideband convention
  automatically, including USB on 60 metres.
- **Live Reporter frequencies:** Decoder v0.1.26 relays the currently
  advertised numeric frequencies to the Kiwi over its authenticated private
  connection. The menu marks them **[Reporter live]** and retains its static
  entries during outages. No callsigns, station names, messages or listener
  identities are relayed.
- **Panel layout:** v0.1.37 made the manual field and Tune button share one row,
  reduced excess vertical spacing and kept Reporter, decoder state and dropped
  frames visible inside the modal. The build gate now also rejects stale
  optimized FreeDV CSS.
- **Local diagnostics link:** v0.1.38 adds **Decoder diagnostics (LAN)** beside
  Dropped frames when the Kiwi page is opened from a private/local address. It
  is deliberately absent for public proxy listeners. The dashboard itself
  remains read-only and restricted by the management-LAN firewall.
- Help and installation/rollback documentation have also been updated.

The current 700D and RADEV1 references pass end to end with synchronization,
returned PCM and zero dropped frames. RADEV1 remains experimental, and live-RF
validation is still being recorded mode by mode.

FreeDV v0.1.38 running Test 700D:

![FreeDV extension](https://raw.githubusercontent.com/smegoff/kiwisdr-freedv-extension/main/docs/images/freedv-extension-v0.1.38.png)

The synchronized decoder dashboard:

![Decoder diagnostics](https://raw.githubusercontent.com/smegoff/kiwisdr-freedv-extension/main/docs/images/decoder-dashboard-v0.1.26.png)

Code, installation notes and full test evidence:
https://github.com/smegoff/kiwisdr-freedv-extension
