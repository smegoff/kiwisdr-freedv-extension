var freedv = {
   ext_name: 'FreeDV',
   first_time: true,
   running: false,
   testing: false,
   test_available: false,
   test_synced: false,
   test_audio: false,
   test_armed: false,
   test_arm_timer: null,
   test_progress_timer: null,
   last_test_result: '',
   reporter_enabled: false,
   rade_enabled: false,
   mode: 'RADEV1',
   calling_index: 0,
   calling_frequencies: [
      { label: 'Select a calling frequency', kHz: 0, sideband: '' },
      { label: '160 m - 1.870 MHz LSB', kHz: 1870, sideband: 'lsb' },
      { label: '80 m - 3.625 MHz LSB', kHz: 3625, sideband: 'lsb' },
      { label: '80 m - 3.643 MHz LSB', kHz: 3643, sideband: 'lsb' },
      { label: '80 m - 3.693 MHz LSB', kHz: 3693, sideband: 'lsb' },
      { label: '80 m - 3.697 MHz LSB', kHz: 3697, sideband: 'lsb' },
      { label: '80 m - 3.803 MHz LSB', kHz: 3803, sideband: 'lsb' },
      { label: '60 m - 5.4035 MHz USB', kHz: 5403.5, sideband: 'usb' },
      { label: '60 m - 5.3685 MHz USB', kHz: 5368.5, sideband: 'usb' },
      { label: '40 m - 7.177 MHz LSB', kHz: 7177, sideband: 'lsb' },
      { label: '40 m - 7.197 MHz LSB', kHz: 7197, sideband: 'lsb' },
      { label: '20 m - 14.236 MHz USB (most common)', kHz: 14236, sideband: 'usb' },
      { label: '20 m - 14.240 MHz USB', kHz: 14240, sideband: 'usb' },
      { label: '17 m - 18.118 MHz USB', kHz: 18118, sideband: 'usb' },
      { label: '15 m - 21.313 MHz USB', kHz: 21313, sideband: 'usb' },
      { label: '12 m - 24.933 MHz USB', kHz: 24933, sideband: 'usb' },
      { label: '10 m - 28.330 MHz USB', kHz: 28330, sideband: 'usb' },
      { label: '10 m - 28.720 MHz USB', kHz: 28720, sideband: 'usb' },
      { label: '10 GHz QO-100 - 10489.640 MHz USB', kHz: 10489640, sideband: 'usb' }
   ],
   legacy_modes: ['1600', '700C', '700D', '700E', '2400A', '2400B', '800XA'],
   modes: ['1600', '700C', '700D', '700E', '2400A', '2400B', '800XA'],
   filter_index: 0,
   filter_modes: ['Auto (lock on sync)', 'Tight', 'Normal', 'Wide'],
   filter_keys: ['auto', 'tight', 'normal', 'wide'],
   filter_locked: false,
   saved_setup: null,
   saved_passband: null,
   receiver_profile: null,
   saved_noise_filter: null,
   noise_filter_forced: false,
   saved_audio_comp: false,
   audio_comp_forced: false,
   generation: 0
};

// FreeDV HF waveforms are centred at 1500 Hz. These are the documented
// occupied RF bandwidths. Automatic mode adds 200 Hz per edge for acquisition,
// tightens to 50 Hz per edge on first sync and rounds outward to a 25 Hz
// boundary. John uses the same
// amateur voice convention: below 10 MHz is LSB except for the 60 metre
// allocation, which uses USB. Frequencies at 10 MHz and above use USB.
function freedv_sideband_for_frequency(freq_kHz)
{
   freq_kHz = +freq_kHz;
   if (freq_kHz >= 5250 && freq_kHz < 5450) return 'usb';
   return (freq_kHz < 10000)? 'lsb':'usb';
}

function freedv_filter_key()
{
   return freedv.filter_keys[freedv.filter_index] || 'auto';
}

function freedv_filter_guard_hz()
{
   var key = freedv_filter_key();
   if (key == 'tight') return 50;
   if (key == 'wide') return 350;
   if (key == 'auto' && freedv.filter_locked) return 50;
   return 200;
}

function freedv_filter_label()
{
   var key = freedv_filter_key();
   if (key == 'auto') return freedv.filter_locked? 'auto locked':'auto acquiring';
   return key;
}

function freedv_receiver_profile(mode, freq_kHz, guard_hz)
{
   guard_hz = isFinite(+guard_hz)? +guard_hz:200;
   var p = {
      mode: mode,
      sideband: freedv_sideband_for_frequency(freq_kHz),
      nominal_hz: 0,
      low: 300,
      high: 3000,
      guard_hz: guard_hz,
      filter_label: freedv_filter_label(),
      note: ''
   };

   var widths = {
      '1600': 1125,
      '700C': 1500,
      '700D': 1000,
      '700E': 1500,
      '800XA': 2000,
      'RADEV1': 1500
   };
   p.nominal_hz = widths[mode] || 0;

   if (p.nominal_hz) {
      p.low = Math.max(300,
         Math.floor((1500 - p.nominal_hz/2 - guard_hz) / 25) * 25);
      p.high = Math.min(5700,
         Math.ceil((1500 + p.nominal_hz/2 + guard_hz) / 25) * 25);
   } else if (mode == '2400A') {
      // 2400A is a 5 kHz VHF SDR waveform. The Kiwi's 12 kHz SSB path can
      // expose a wide integration filter, but still lacks the required
      // 48 kHz modem path and is not claimed as live-RF ready.
      p.nominal_hz = 5000;
      p.low = 300;
      p.high = 5700;
      p.note = 'integration only';
   } else if (mode == '2400B') {
      // Upstream tests 2400B through a 300-3000 Hz audio path, after an FM
      // receiver. The current Kiwi transport has no FM/48 kHz modem path.
      p.low = 300;
      p.high = 3000;
      p.note = 'analog FM audio (integration only)';
   }
   return p;
}

function freedv_receiver_profile_text(p)
{
   var low = p.sideband == 'lsb'? -p.high : p.low;
   var high = p.sideband == 'lsb'? -p.low : p.high;
   var occupied = p.nominal_hz?
      (p.nominal_hz / 1000).toFixed(3).replace(/0+$/, '').replace(/\.$/, '') +
      ' kHz signal; ' : '';
   return p.sideband.toUpperCase() +'; '+ occupied +'filter '+ low +' to '+ high +' Hz' +
      (p.nominal_hz? '; '+ p.filter_label:'') +
      (p.note? '; '+ p.note:'');
}

function freedv_apply_receiver_profile()
{
   var p = freedv_receiver_profile(freedv.mode, +ext_get_freq_kHz(),
      freedv_filter_guard_hz());
   freedv.receiver_profile = p;
   if (ext_get_mode() != p.sideband) ext_set_mode(p.sideband);
   // ext_set_passband() mirrors positive audio limits into negative
   // frequencies automatically when the selected demodulator is LSB.
   ext_set_passband(p.low, p.high);
   var el = w3_el('id-freedv-radio');
   if (el) w3_innerHTML('id-freedv-radio', freedv_receiver_profile_text(p));
}

function freedv_filter_reset()
{
   freedv.filter_locked = false;
   freedv_apply_receiver_profile();
}

function freedv_filter_sync(synced)
{
   if (!synced || !freedv.running || freedv.testing ||
       freedv_filter_key() != 'auto' || freedv.filter_locked) return;
   freedv.filter_locked = true;
   freedv_apply_receiver_profile();
}

function freedv_force_noise_filter_off()
{
   if (freedv.noise_filter_forced || typeof noise_filter == 'undefined') return;
   freedv.saved_noise_filter = {
      algo: +noise_filter.algo,
      denoise: +noise_filter.denoise,
      autonotch: +noise_filter.autonotch,
      stored_algo: kiwi_storeRead('last_nr_algo')
   };
   noise_filter.algo = noise_filter.NR_OFF;
   snd_send('SET nr algo='+ noise_filter.NR_OFF);
   w3_select_value('nr_algo', noise_filter.NR_OFF, { all:1 });
   if (typeof noise_filter_controls_refresh == 'function')
      noise_filter_controls_refresh();
   freedv.noise_filter_forced = true;
   if (w3_el('id-freedv-clean'))
      w3_innerHTML('id-freedv-clean', 'noise filter off (saved)');
}

function freedv_restore_noise_filter()
{
   if (!freedv.noise_filter_forced || !freedv.saved_noise_filter ||
       typeof noise_filter == 'undefined') return;
   var saved = freedv.saved_noise_filter;
   noise_filter.algo = saved.algo;
   noise_filter.denoise = saved.denoise;
   noise_filter.autonotch = saved.autonotch;
   snd_send('SET nr algo='+ noise_filter.algo);
   w3_select_value('nr_algo', noise_filter.algo, { all:1 });
   if (noise_filter.algo != noise_filter.NR_OFF &&
       typeof noise_filter_send == 'function') {
      noise_filter_send(noise_filter.NR_DENOISE);
      noise_filter_send(noise_filter.NR_AUTONOTCH);
   }
   if (saved.stored_algo == null) kiwi_storeDelete('last_nr_algo');
   else kiwi_storeWrite('last_nr_algo', saved.stored_algo);
   if (typeof noise_filter_controls_refresh == 'function')
      noise_filter_controls_refresh();
   freedv.saved_noise_filter = null;
   freedv.noise_filter_forced = false;
}

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
         case 'rade_enabled':
            freedv.rade_enabled = (+value != 0);
            freedv.modes = freedv.legacy_modes.slice();
            if (freedv.rade_enabled) freedv.modes.push('RADEV1');
            if (freedv.modes.indexOf(freedv.mode) < 0) freedv.mode = '700D';
            break;
         case 'reporter_enabled':
            freedv.reporter_enabled = (+value != 0);
            freedv_update_reporter_state();
            break;
         case 'ready':
            freedv_controls_setup();
            break;
         case 'test_available':
            freedv.test_available = (+value != 0);
            w3_disable('id-freedv-test', !freedv.test_available);
            break;
         case 'state':
            if (freedv.testing && value == 'test-signal-running')
               freedv_test_mark_armed();
            w3_innerHTML('id-freedv-state',
               value == 'stopped' && freedv.last_test_result? freedv.last_test_result : value);
            break;
         case 'have_rtn_snd':
            if (freedv.testing) freedv.test_audio = true;
            w3_innerHTML('id-freedv-state', 'decoded audio');
            break;
         case 'test_pct':
            if (+value > 0) freedv_test_mark_progress();
            w3_innerHTML('id-freedv-test-progress', value +'%');
            break;
         case 'test_done':
            freedv_clear_test_timers();
            var passed = freedv.test_synced && freedv.test_audio;
            freedv.last_test_result = passed? 'test passed':'test failed';
            freedv_stop_ui(true);
            w3_innerHTML('id-freedv-state', freedv.last_test_result);
            w3_innerHTML('id-freedv-error', passed? '':
               'The reference signal did not produce synchronized decoded audio.');
            break;
         case 'status_json':
            var status = kiwi_JSON_parse('FreeDV status', decodeURIComponent(value));
            if (!status || status.type != 'status') break;
            freedv.generation = status.generation || 0;
            if (freedv.testing && status.state == 'running')
               freedv_test_mark_armed();
            w3_innerHTML('id-freedv-state', status.state || 'running');
            w3_innerHTML('id-freedv-backend', status.backend || 'external');
            w3_innerHTML('id-freedv-sync', status.sync? 'yes':'no');
            freedv_filter_sync(status.sync);
            if (freedv.testing && +status.decoded_frames > 0) {
               // Returned PCM is emitted only while Codec2 reports sync. Use
               // the per-session counter so a short sync interval cannot fall
               // between the 250 ms status updates and produce a false fail.
               freedv.test_synced = true;
               freedv.test_audio = true;
            }
            w3_innerHTML('id-freedv-snr', isFinite(+status.snr)? (+status.snr).toFixed(1) +' dB':'-- dB');
            w3_innerHTML('id-freedv-foff', isFinite(+status.frequency_offset)?
               (+status.frequency_offset).toFixed(1) +' Hz':'-- Hz');
            w3_innerHTML('id-freedv-text', ((status.callsign || '') +' '+ (status.text || '')).trim());
            w3_innerHTML('id-freedv-dropped', status.dropped || 0);
            freedv_update_reporter_state(status.reporter);
            if (status.error) w3_innerHTML('id-freedv-error', status.error);
            break;
         case 'error':
            freedv_stop_ui(true);
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
         w3_div('w3-medium w3-text-aqua', '<b>FreeDV v0.1.30 receive decoder</b>'),
         w3_div('w3-margin-T-8 w3-text-red', 'FreeDV requires a Kiwi configured for 12 kHz audio channels.'));
      ext_panel_show(unsupported, null, null);
      ext_set_controls_width_height(420, 120);
      return;
   }
   if (!freedv.saved_setup) {
      freedv.saved_setup = ext_save_setup();
      freedv.saved_passband = ext_get_passband();
   }
   var initial_profile = freedv_receiver_profile(freedv.mode, +ext_get_freq_kHz(),
      freedv_filter_guard_hz());
   var calling_labels = freedv.calling_frequencies.map(function(entry) { return entry.label; });
   var controls = w3_div('id-freedv-controls w3-text-white',
      w3_div('id-freedv-intro',
         w3_div('w3-medium w3-text-aqua', '<b>FreeDV v0.1.30 receive decoder</b>'),
         w3_div('w3-small', 'External decoder via Kiwi camper return-audio transport'),
         w3_div('w3-small w3-text-light-grey', 'Built with ',
            w3_link('', 'https://freedv.org/', 'FreeDV'),
            ' open-source digital voice, Codec2 and RADE.')),
      w3_inline('id-freedv-actions/w3-margin-right',
         w3_select('w3-text-red', 'Mode', '', 'freedv.mode', freedv.modes.indexOf(freedv.mode),
            freedv.modes, 'freedv_mode_cb'),
         w3_button('id-freedv-start w3-green w3-margin-T-8', 'Start', 'freedv_start_cb'),
         w3_button('id-freedv-test w3-aqua w3-margin-T-8', 'Test', 'freedv_test_cb')),
      w3_select('id-freedv-calling w3-text-red', 'Calling frequency', '',
         'freedv.calling_index', freedv.calling_index, calling_labels,
         'freedv_calling_frequency_cb'),
      w3_select('id-freedv-filter w3-text-red', 'Receiver filter', '',
         'freedv.filter_index', freedv.filter_index, freedv.filter_modes,
         'freedv_filter_cb'),
      w3_div('id-freedv-radio-info',
         w3_div('w3-small', 'Test: ', w3_div('id-freedv-test-progress w3-show-inline', 'ready')),
         w3_div('w3-small', 'DSP: ',
            w3_div('id-freedv-clean w3-show-inline', 'noise filter off (saved)')),
         w3_div('w3-small', 'Receiver: ',
            w3_div('id-freedv-radio w3-show-inline', freedv_receiver_profile_text(initial_profile)))),
      w3_div('id-freedv-status',
         w3_div('', 'State: ', w3_div('id-freedv-state w3-show-inline', 'stopped')),
         w3_div('', 'Backend: ', w3_div('id-freedv-backend w3-show-inline', 'external')),
         w3_div('', 'Sync: ', w3_div('id-freedv-sync w3-show-inline', 'no')),
         w3_div('', 'SNR: ', w3_div('id-freedv-snr w3-show-inline', '-- dB')),
         w3_div('', 'Offset: ', w3_div('id-freedv-foff w3-show-inline', '-- Hz')),
         w3_div('', 'Callsign/text: ', w3_div('id-freedv-text w3-show-inline', '')),
         w3_div('', 'Reporter: ', w3_div('id-freedv-reporter w3-show-inline',
            freedv.reporter_enabled? 'enabled (idle)':'disabled'))),
      w3_div('id-freedv-footer',
         w3_div('w3-small', 'Dropped frames: ', w3_div('id-freedv-dropped w3-show-inline', '0')),
         w3_div('id-freedv-error w3-small w3-text-red'),
         w3_link('w3-small', 'https://qso.freedv.org/', 'FreeDV Reporter')));
   ext_panel_show(controls, null, null);
   ext_set_controls_width_height(470, 480);
   w3_disable('id-freedv-test', !freedv.test_available);
   freedv_force_noise_filter_off();
   freedv_apply_receiver_profile();
   ext_send('SET freedv_setup');
}

function freedv_calling_frequency_cb(path, index, first)
{
   if (first || freedv.testing) return;
   index = +index;
   var entry = freedv.calling_frequencies[index];
   if (!entry || !entry.kHz) return;

   var range = ext_get_freq_range();
   if (entry.kHz < range.lo_kHz || entry.kHz > range.hi_kHz) {
      w3_innerHTML('id-freedv-error',
         entry.label +' is outside this Kiwi\'s configured frequency range. ' +
         'QO-100 requires a suitable downconverter/transverter frequency offset in Kiwi Admin.');
      freedv.calling_index = 0;
      w3_select_value(path, 0);
      return;
   }

   freedv.calling_index = index;
   freedv.filter_locked = false;
   w3_innerHTML('id-freedv-error', '');
   // Calling frequencies are displayed RF frequencies. Kiwi tuning uses the
   // receiver frequency after subtracting any configured transverter offset.
   ext_tune(entry.kHz - range.offset_kHz, entry.sideband, ext_zoom.CUR);
   freedv_apply_receiver_profile();
}

function freedv_mode_cb(path, index, first)
{
   if (first || freedv.testing) return;
   freedv.mode = freedv.modes[+index];
   freedv_filter_reset();
   if (freedv.running) ext_send('SET freedv_start=1 mode='+ freedv.mode);
}

function freedv_filter_cb(path, index, first)
{
   if (first) return;
   freedv.filter_index = +index;
   freedv.filter_locked = false;
   freedv_apply_receiver_profile();
}

function freedv_start_cb()
{
   if (freedv.running) freedv_stop_ui(true);
   else freedv_start_ui(false);
}

function freedv_start_ui(testing)
{
   freedv.running = true;
   freedv.testing = testing;
   freedv.test_synced = false;
   freedv.test_audio = false;
   freedv.test_armed = false;
   freedv.last_test_result = '';
   freedv.filter_locked = false;
   w3_button_text('id-freedv-start', 'Stop');
   w3_button_text('id-freedv-test', testing? 'Stop test':'Test',
      testing? 'w3-red':'w3-aqua', testing? 'w3-aqua':'w3-red');
   w3_disable('id-freedv.mode', testing);
   w3_disable('id-freedv.calling_index', testing);
   w3_disable('id-freedv.filter_index', testing);
   w3_innerHTML('id-freedv-error', '');
   w3_innerHTML('id-freedv-test-progress', testing? '0%':'ready');
   freedv_update_reporter_state();
   freedv_force_uncompressed_audio();
   freedv_apply_receiver_profile();
   ext_send(testing? 'SET freedv_test=1 mode='+ freedv.mode :
      'SET freedv_start=1 mode='+ freedv.mode);
   if (testing) {
      freedv_clear_test_timers();
      freedv.test_arm_timer = setTimeout(function() {
         if (!freedv.testing) return;
         freedv.last_test_result = 'test failed';
         freedv_stop_ui(true);
         w3_innerHTML('id-freedv-state', freedv.last_test_result);
         w3_innerHTML('id-freedv-error',
            'The decoder did not arm the reference signal within 15 seconds. Please retry; if it repeats, check decoder health.');
      }, 15000);
   }
}

function freedv_test_mark_armed()
{
   if (!freedv.testing || freedv.test_armed) return;
   freedv.test_armed = true;
   if (freedv.test_arm_timer) clearTimeout(freedv.test_arm_timer);
   freedv.test_arm_timer = null;
   w3_innerHTML('id-freedv-test-progress', 'armed');
   freedv.test_progress_timer = setTimeout(function() {
      if (!freedv.testing) return;
      freedv.last_test_result = 'test failed';
      freedv_stop_ui(true);
      w3_innerHTML('id-freedv-state', freedv.last_test_result);
      w3_innerHTML('id-freedv-error',
         'The decoder armed, but Kiwi reference audio did not start within 5 seconds. Please retry; if it repeats, check Kiwi audio health.');
   }, 5000);
}

function freedv_test_mark_progress()
{
   if (freedv.test_progress_timer) clearTimeout(freedv.test_progress_timer);
   freedv.test_progress_timer = null;
}

function freedv_clear_test_timers()
{
   if (freedv.test_arm_timer) clearTimeout(freedv.test_arm_timer);
   if (freedv.test_progress_timer) clearTimeout(freedv.test_progress_timer);
   freedv.test_arm_timer = null;
   freedv.test_progress_timer = null;
}

function freedv_update_reporter_state(status)
{
   var value = 'disabled';
   if (freedv.reporter_enabled) {
      if (freedv.testing) value = 'enabled (test excluded)';
      else if (!freedv.running) value = 'enabled (idle)';
      else value = (!status || status == 'disabled')? 'connecting':status;
   }
   if (w3_el('id-freedv-reporter')) w3_innerHTML('id-freedv-reporter', value);
}

function freedv_stop_ui(send_stop)
{
   freedv_clear_test_timers();
   if (send_stop) ext_send('SET freedv_stop');
   freedv.running = false;
   freedv.testing = false;
   w3_button_text('id-freedv-start', 'Start');
   w3_button_text('id-freedv-test', 'Test', 'w3-aqua', 'w3-red');
   w3_disable('id-freedv.mode', false);
   w3_disable('id-freedv.calling_index', false);
   w3_disable('id-freedv.filter_index', false);
   freedv.filter_locked = false;
   freedv_apply_receiver_profile();
   freedv_update_reporter_state();
   freedv_restore_audio_compression();
}

function freedv_test_cb()
{
   if (!freedv.test_available) return;
   if (freedv.testing) {
      freedv_stop_ui(true);
      w3_innerHTML('id-freedv-state', 'stopped');
      return;
   }
   if (freedv.running) ext_send('SET freedv_stop');
   // John\'s bundled reference recording is a 700D signal. Keeping the test
   // mode fixed makes a pass/fail result comparable between deployments.
   freedv.mode = '700D';
   w3_select_value('freedv.mode', freedv.modes.indexOf(freedv.mode));
   freedv_start_ui(true);
}

function FreeDV_environment_changed(changed)
{
   if (!freedv.saved_setup) return;
   if (changed.freq || changed.mode) {
      freedv_filter_reset();
      if (freedv.running) w3_innerHTML('id-freedv-state', 'retuning');
   }
}

function FreeDV_blur()
{
   ext_send('SET freedv_close');
   freedv_stop_ui(false);
   freedv.generation = 0;
   freedv_restore_noise_filter();
   if (freedv.saved_setup) ext_restore_setup(freedv.saved_setup);
   if (freedv.saved_passband)
      ext_set_passband(freedv.saved_passband.low, freedv.saved_passband.high);
   freedv.saved_setup = null;
   freedv.saved_passband = null;
}

function FreeDV_help(show)
{
   if (show) {
      var calling_help = freedv.calling_frequencies.slice(1).map(function(entry) {
         return entry.label;
      }).join('<br>');
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

         '<b>RADEV1 - neural HF voice codec (experimental)</b><br>' +
         'RADE v1 is a receive-only neural speech waveform decoded on the external ' +
         'decoder guest using the portable RADE implementation and FARGAN speech ' +
         'synthesis. It is about 1.5 kHz wide, accepts 8 kHz modem audio and produces ' +
         '16 kHz decoded speech. It targets intelligible speech around -2 dB SNR and ' +
         'has no conventional SNR squelch, so audio is passed only while the RADE modem ' +
         'reports synchronization. RADEV1 appears in the selector only when both the ' +
         'decoder service and the Kiwi administrator have enabled it. Treat it as ' +
         'experimental until live-RF interoperability testing is complete.<br><br>' +

         '<b>Listening</b><br>' +
         'The extension follows the usual amateur voice convention: 160, 80 and 40 ' +
         'metres use LSB, 60 metres is the USB exception, and 10 MHz and above use USB. It changes ' +
         'sideband automatically when you retune. Each HF mode gets a filter based on ' +
         'its documented occupied bandwidth, centred at 1500 Hz. Automatic filter mode ' +
         'starts with 200 Hz of acquisition room on each edge, tightens to 50 Hz on ' +
         'first sync and stays locked until a retune, mode change or restart. Tight, ' +
         'Normal and Wide are manual overrides. The active sideband and filter are shown in ' +
         'the main panel. 2400A and 2400B remain integration-only because they require ' +
         'a VHF/FM 48 kHz receive path.<br><br>' +
         'Opening FreeDV temporarily turns off the Kiwi noise filter, denoiser and ' +
         'automatic notch path so analogue-speech processing cannot distort the modem ' +
         'waveform. The previous noise-filter selection is restored when FreeDV closes. ' +
         'The noise blanker is not changed.<br><br>' +
         'Press Start and wait for <i>Sync: yes</i>. While FreeDV is running, ordinary ' +
         'receiver noise is silenced and audio is heard only from synchronized FreeDV ' +
         'decoding. Press Stop or close the extension to restore normal Kiwi audio.<br><br>' +

         '<b>Calling frequencies</b><br>' +
         calling_help + '<br><br>' +
         'The Calling frequency list tunes common FreeDV activity frequencies and sets ' +
         'the listed sideband. The 14.236 MHz 20 metre entry is marked as the most common. ' +
         'Selecting a frequency does not start decoding; choose the transmitted FreeDV ' +
         'mode and press Start. The QO-100 entry is available only when the Kiwi has a ' +
         'suitable downconverter/transverter frequency offset configured in Admin.<br><br>' +

         '<b>Test</b><br>' +
         'The Test button feeds John\'s bundled 700D reference recording through the ' +
         'normal Kiwi sound channel, the external Codec2 decoder and the standard Kiwi ' +
         'return-audio path. A pass requires both modem synchronization and returned ' +
         'decoded PCM. Test sessions are never sent to FreeDV Reporter.<br><br>' +

         '<b>More information</b><br>' +
         'Installation, architecture, mode notes, rollback guidance and current test ' +
         'status are in the ' +
         '<a href="https://github.com/smegoff/kiwisdr-freedv-extension" target="_blank">' +
         'KiwiSDR FreeDV Extension GitHub repository</a>.<br>' +
         'FreeDV mode specifications and operating information: ' +
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
      w3_switch_get_param('', 'RADEV1 off', 'RADEV1 on', 'freedv.rade_enabled', 0, 0,
         'w3_bool_set_cfg_cb'),
      w3_switch_get_param('', 'Reporter off', 'Reporter on', 'freedv.reporter_enabled', 0, 0,
         'w3_bool_set_cfg_cb'),
      w3_input('', 'Station callsign', 'freedv.reporter_callsign', callsign, 'w3_string_set_cfg_cb'),
      w3_input('', 'Maidenhead locator', 'freedv.reporter_grid', grid, 'w3_string_set_cfg_cb'),
      w3_input('', 'Reporter message', 'freedv.reporter_message', message, 'w3_string_set_cfg_cb'),
      w3_div('w3-small', 'The shared decoder secret is stored root-only and is never sent to browsers.'));
   ext_config_html(freedv, 'freedv', 'FreeDV', 'External FreeDV decoder', body);
}
