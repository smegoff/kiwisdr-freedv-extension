var freedv = {
   ext_name: 'FreeDV',
   first_time: true,
   running: false,
   mode: '700D',
   modes: ['1600', '700C', '700D', '700E', '2400A', '2400B', '800XA'],
   saved_setup: null,
   saved_audio_comp: false,
   audio_comp_forced: false,
   generation: 0
};

function freedv_force_uncompressed_audio()
{
   if (freedv.audio_comp_forced) return;
   freedv.saved_audio_comp = ext_get_audio_comp();
   // John’s return-audio path requires linear PCM, not browser ADPCM state.
   ext_set_audio_comp(false, true);
   freedv.audio_comp_forced = true;
}

function freedv_restore_audio_compression()
{
   if (!freedv.audio_comp_forced) return;
   ext_set_audio_comp(freedv.saved_audio_comp, true);
   freedv.audio_comp_forced = false;
}

function FreeDV_main()
{
   ext_switch_to_client(freedv.ext_name, freedv.first_time, freedv_recv);
   if (!freedv.first_time) freedv_controls_setup();
   freedv.first_time = false;
}

function freedv_recv(data)
{
   var firstChars = arrayBufferToStringLen(data, 3);
   if (firstChars == 'DAT') return;
   var stringData = arrayBufferToString(data).substring(4);
   stringData.split(' ').forEach(function(token) {
      if (!token) return;
      var equals = token.indexOf('=');
      var name = equals < 0? token : token.substring(0, equals);
      var value = equals < 0? '' : token.substring(equals + 1);
      switch (name) {
         case 'ready':
            freedv_controls_setup();
            break;
         case 'state':
            w3_innerHTML('id-freedv-state', value);
            break;
         case 'have_rtn_snd':
            w3_innerHTML('id-freedv-state', 'decoded audio');
            break;
         case 'status_json':
            var status = kiwi_JSON_parse('FreeDV status', decodeURIComponent(value));
            if (!status || status.type != 'status') break;
            freedv.generation = status.generation || 0;
            w3_innerHTML('id-freedv-state', status.state || 'running');
            w3_innerHTML('id-freedv-backend', status.backend || 'external');
            w3_innerHTML('id-freedv-sync', status.sync? 'yes':'no');
            w3_innerHTML('id-freedv-snr', isFinite(+status.snr)? (+status.snr).toFixed(1) +' dB':'-- dB');
            w3_innerHTML('id-freedv-foff', isFinite(+status.frequency_offset)?
               (+status.frequency_offset).toFixed(1) +' Hz':'-- Hz');
            w3_innerHTML('id-freedv-text', ((status.callsign || '') +' '+ (status.text || '')).trim());
            w3_innerHTML('id-freedv-dropped', status.dropped || 0);
            w3_innerHTML('id-freedv-reporter', status.reporter || 'disabled');
            if (status.error) w3_innerHTML('id-freedv-error', status.error);
            break;
         case 'error':
            freedv.running = false;
            w3_button_text('id-freedv-start', 'Start');
            w3_innerHTML('id-freedv-state', 'error');
            w3_innerHTML('id-freedv-error', decodeURIComponent(value));
            break;
      }
   });
}

function freedv_controls_setup()
{
   if (ext_nom_sample_rate() != 12000) {
      var unsupported = w3_div('id-freedv-controls w3-text-white',
         w3_div('w3-medium w3-text-aqua', '<b>FreeDV v0.1.6 receive decoder</b>'),
         w3_div('w3-margin-T-8 w3-text-red', 'FreeDV requires a Kiwi configured for 12 kHz audio channels.'));
      ext_panel_show(unsupported, null, null);
      ext_set_controls_width_height(420, 120);
      return;
   }
   if (!freedv.saved_setup) freedv.saved_setup = ext_save_setup();
   ext_set_mode('usb');
   ext_set_passband(300, 3000);
   var controls = w3_div('id-freedv-controls w3-text-white',
      w3_div('w3-medium w3-text-aqua', '<b>FreeDV v0.1.6 receive decoder</b>'),
      w3_div('w3-small', 'External decoder via Kiwi camper return-audio transport'),
      w3_inline('w3-margin-T-8/w3-margin-right',
         w3_select('w3-text-red', 'Mode', '', 'freedv.mode', freedv.modes.indexOf(freedv.mode),
            freedv.modes, 'freedv_mode_cb'),
         w3_button('id-freedv-start w3-green w3-margin-T-8', 'Start', 'freedv_start_cb')),
      w3_div('w3-margin-T-8', 'State: ', w3_div('id-freedv-state w3-show-inline', 'stopped')),
      w3_div('', 'Backend: ', w3_div('id-freedv-backend w3-show-inline', 'external')),
      w3_div('', 'Sync: ', w3_div('id-freedv-sync w3-show-inline', 'no')),
      w3_div('', 'SNR: ', w3_div('id-freedv-snr w3-show-inline', '-- dB')),
      w3_div('', 'Offset: ', w3_div('id-freedv-foff w3-show-inline', '-- Hz')),
      w3_div('', 'Callsign/text: ', w3_div('id-freedv-text w3-show-inline', '')),
      w3_div('', 'Reporter: ', w3_div('id-freedv-reporter w3-show-inline', 'disabled')),
      w3_div('w3-small', 'Dropped frames: ', w3_div('id-freedv-dropped w3-show-inline', '0')),
      w3_div('id-freedv-error w3-small w3-text-red'),
      w3_link('w3-small', 'https://qso.freedv.org/', 'FreeDV Reporter'));
   ext_panel_show(controls, null, null);
   ext_set_controls_width_height(410, 350);
   ext_send('SET freedv_setup');
}

function freedv_mode_cb(path, index, first)
{
   if (first) return;
   freedv.mode = freedv.modes[+index];
   if (freedv.running) ext_send('SET freedv_start=1 mode='+ freedv.mode);
}

function freedv_start_cb()
{
   freedv.running = !freedv.running;
   w3_button_text('id-freedv-start', freedv.running? 'Stop':'Start');
   w3_innerHTML('id-freedv-error', '');
   if (freedv.running) {
      freedv_force_uncompressed_audio();
      ext_set_mode('usb');
      ext_set_passband(300, 3000);
      ext_send('SET freedv_start=1 mode='+ freedv.mode);
   } else {
      ext_send('SET freedv_stop');
      freedv_restore_audio_compression();
   }
}

function FreeDV_environment_changed(changed)
{
   if (freedv.running && (changed.freq || changed.mode))
      w3_innerHTML('id-freedv-state', 'retuning');
}

function FreeDV_blur()
{
   ext_send('SET freedv_close');
   freedv.running = false;
   freedv.generation = 0;
   freedv_restore_audio_compression();
   if (freedv.saved_setup) ext_restore_setup(freedv.saved_setup);
   freedv.saved_setup = null;
}

function FreeDV_help(show)
{
   if (show) {
      var s =
         '<b>Choose the same mode as the transmitting station.</b> The modes are not ' +
         'automatically interchangeable. If the mode is unknown, 700D is the usual ' +
         'starting point for weak-signal HF operation.<br><br>' +

         '<b>1600 - legacy HF voice</b><br>' +
         'About 1.1 kHz wide. Low latency and comparatively easy to tune, but it needs ' +
         'a stronger signal and handles fading less well than the 700D/700E modes.<br><br>' +

         '<b>700C - fast HF voice</b><br>' +
         'About 1.5 kHz wide with quick 40 ms frames and no forward error correction. ' +
         'Good for a solid signal when fast synchronization and low latency matter; ' +
         'not the best weak-signal choice.<br><br>' +

         '<b>700D - weak-signal HF voice (recommended starting mode)</b><br>' +
         'About 1.0 kHz wide with strong LDPC error correction. It works at the lowest ' +
         'SNR of these legacy modes, but has more latency and should be tuned carefully ' +
         '(roughly within +/-60 Hz for acquisition).<br><br>' +

         '<b>700E - faster-fading HF voice</b><br>' +
         'About 1.5 kHz wide. Synchronizes faster and copes with faster fading better ' +
         'than 700D, at the cost of needing roughly 3 dB more signal.<br><br>' +

         '<b>2400A - VHF/UHF SDR 4FSK</b><br>' +
         'A roughly 5 kHz-wide constant-envelope waveform requiring a 48 kHz modem ' +
         'sample path. It is selectable for integration testing, but is <b>not yet ' +
         'live-RF ready</b> in this Kiwi 12 kHz HF/USB receive path.<br><br>' +

         '<b>2400B - VHF/UHF through analog FM</b><br>' +
         'Designed to pass through ordinary FM radios and also requires a 48 kHz modem ' +
         'path plus FM demodulation. It is selectable for integration testing, but is ' +
         '<b>not yet live-RF ready</b> in the current Kiwi path.<br><br>' +

         '<b>800XA - low-rate 4FSK</b><br>' +
         'About 2.0 kHz wide, constant envelope and without forward error correction. ' +
         'Use it only for a matching 800XA transmission. This mode has no text side ' +
         'channel, so callsign text may remain blank.<br><br>' +

         '<b>Listening</b><br>' +
         'The extension selects USB and a 300-3000 Hz passband for the current HF modes. ' +
         'Press Start and wait for <i>Sync: yes</i>. While FreeDV is running, ordinary ' +
         'receiver noise is silenced and audio is heard only from synchronized FreeDV ' +
         'decoding. Press Stop or close the extension to restore normal Kiwi audio.<br><br>' +

         'Mode specifications and operating information: ' +
         '<a href="https://freedv.org/" target="_blank">freedv.org</a>';
      confirmation_show_scrolling_content('FreeDV mode guide', s, 720, 500);
   }
   return true;
}

function FreeDV_config_html()
{
   var decoder_ip = ext_get_cfg_param('freedv.decoder_ip', '192.168.10.145');
   var callsign = ext_get_cfg_param('freedv.reporter_callsign', '');
   var grid = ext_get_cfg_param('freedv.reporter_grid', '');
   var message = ext_get_cfg_param('freedv.reporter_message', '');
   var body = w3_divs('w3-container/w3-tspace-8',
      w3_input('', 'Decoder LAN address', 'freedv.decoder_ip', decoder_ip, 'w3_string_set_cfg_cb'),
      w3_switch_get_param('', 'Reporter off', 'Reporter on', 'freedv.reporter_enabled', 0, 0,
         'w3_bool_set_cfg_cb'),
      w3_input('', 'Station callsign', 'freedv.reporter_callsign', callsign, 'w3_string_set_cfg_cb'),
      w3_input('', 'Maidenhead locator', 'freedv.reporter_grid', grid, 'w3_string_set_cfg_cb'),
      w3_input('', 'Reporter message', 'freedv.reporter_message', message, 'w3_string_set_cfg_cb'),
      w3_div('w3-small', 'The shared decoder secret is stored root-only and is never sent to browsers.'));
   ext_config_html(freedv, 'freedv', 'FreeDV', 'External FreeDV decoder', body);
}
