'use strict';

const content = document.getElementById('content');

const FINGER_LABELS = [
  'Pinky (L)', 'Ring (L)', 'Middle (L)', 'Index (L)', 'Thumb (L)',
  'Pinky (R)', 'Ring (R)', 'Middle (R)', 'Index (R)', 'Thumb (R)',
];

/* ---------------------------------------------------------------- helpers */

function esc(s) {
  return String(s ?? '').replace(/[&<>"']/g, (c) => ({
    '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;',
  }[c]));
}

/**
 * Bumped every time a screen is (re-)rendered. Any long-running polling
 * loop (Reset Device, the enrollment wizard) captures the value current
 * at its start and checks it before every retry/continue - if it no
 * longer matches, the user has navigated to a different screen, so the
 * loop stops silently instead of continuing to poll the sensor and
 * mutate shared device state (the LED, fps mutex) in the background on
 * behalf of a screen that isn't showing anymore.
 */
let navGeneration = 0;

function render(html) {
  navGeneration++;
  content.innerHTML = html;
}

function isCurrentGeneration(gen) {
  return gen === navGeneration;
}

function $(sel) {
  return content.querySelector(sel);
}

function $all(sel) {
  return Array.from(content.querySelectorAll(sel));
}

async function api(method, path, body) {
  const opts = { method };
  if (body !== undefined) {
    opts.headers = { 'Content-Type': 'application/json' };
    opts.body = JSON.stringify(body);
  }
  const res = await fetch(path, opts);
  let data = {};
  try { data = await res.json(); } catch (e) { /* empty/non-JSON body */ }
  if (!res.ok) {
    throw new Error(data.message || data.error || ('HTTP ' + res.status));
  }
  return data;
}

function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

/* ------------------------------------------------------ enrollment wizard */

const WIZARD_TOTAL_STEPS = 6; /* 3 templates x 2 scans */
const WIZARD_ICONS = { waiting: '●', success: '✓', error: '✕' };

/**
 * Builds the flat sequence of 6 capture round trips for one enrollment
 * (3 templates x 2 scans each). There is no separate "lift" step and
 * never was one that actually worked: the sensor cannot reliably report
 * a lift after a scan (confirmed independently via the vendor's own PC
 * test tool), only a fresh touch. The server's own presence-wait for
 * each capture already requires that fresh touch, so it alone enforces
 * "lift and place again" between captures - the wizard just needs to
 * tell the user to do it.
 */
function buildEnrollPlan() {
  const plan = [];
  for (let template = 1; template <= 3; template++) {
    for (let step = 1; step <= 2; step++) {
      plan.push({ template, step });
    }
  }
  return plan;
}

/**
 * Renders the wizard shell (progress dots + label + message box) into
 * containerEl. Call once per wizard run; the wizardSet* helpers below then
 * update pieces of it in place without re-rendering the whole thing, so
 * unrelated parts of the screen don't flicker.
 */
function renderWizardShell(containerEl) {
  containerEl.innerHTML = `
    <div class="wizard">
      <div class="wizard-dots" id="wizard-dots"></div>
      <div class="wizard-label" id="wizard-label"></div>
      <div class="wizard-message" id="wizard-message">
        <span class="wizard-icon"></span>
        <span class="wizard-text"></span>
      </div>
    </div>
  `;
}

function wizardSetProgress(label, completedSteps) {
  document.getElementById('wizard-label').textContent = label;
  const dots = document.getElementById('wizard-dots');
  let html = '';
  for (let i = 0; i < WIZARD_TOTAL_STEPS; i++) {
    const cls = i < completedSteps ? 'done' : (i === completedSteps ? 'current' : 'pending');
    html += `<span class="wizard-dot ${cls}"></span>`;
  }
  dots.innerHTML = html;
}

/**
 * Updates the single message box. Only called on real state transitions
 * (a new step starting, success, error) - never on a routine "still
 * waiting for a finger" retry, so the instruction text doesn't churn.
 * The 'waiting' state instead shows a continuously pulsing icon, giving
 * constant "still listening" feedback without rewriting any text.
 */
function wizardSetMessage(kind, text) {
  const msgEl = document.getElementById('wizard-message');
  msgEl.className = `wizard-message ${kind}`;
  msgEl.querySelector('.wizard-icon').textContent = WIZARD_ICONS[kind] || '';
  msgEl.querySelector('.wizard-text').textContent = text;
}

/**
 * Runs the full enrollment wizard (buildEnrollPlan()'s flat capture
 * sequence) against the given scan endpoint, rendering progress into
 * containerEl. Silently retries a capture on "timeout" (the presence-wait
 * window on the server elapsed without a finger being placed) since
 * that's expected, routine behaviour, not an error - the pulsing icon
 * already conveys it, so no message text changes on each retry. There is
 * no automatic give-up: isCancelled() (if given) is polled between
 * requests so the caller can offer the user an explicit way to stop a
 * stalled attempt (e.g. a Cancel button).
 */
async function runEnrollWizard(scanUrl, containerEl, isCancelled) {
  const myGen = navGeneration;
  renderWizardShell(containerEl);

  let completedCaptures = 0;

  for (const { template, step } of buildEnrollPlan()) {
    if (!isCurrentGeneration(myGen) || (isCancelled && isCancelled())) { return; }
    wizardSetProgress(`Template ${template} of 3`, completedCaptures);

    wizardSetMessage('waiting', (template === 1 && step === 1)
      ? 'Place your finger on the sensor'
      : 'Lift your finger and place it again');

    for (;;) {
      const result = await api('POST', scanUrl, { template, step });
      if (!isCurrentGeneration(myGen) || (isCancelled && isCancelled())) { return; }
      if (result.status === 'success') {
        completedCaptures++;
        if (result.rebooting) {
          wizardSetProgress('Enrollment complete', WIZARD_TOTAL_STEPS);
          wizardSetMessage('success', 'Admin fingerprint enrolled. Rebooting...');
          return result;
        }
        break;
      }
      if (result.status === 'timeout') {
        continue;
      }
      /* 'error' (e.g. a commit failure) - terminal, with a specific
       * message from the server explaining what happened. */
      const msg = result.message || ('Enrollment failed: ' + result.status);
      wizardSetMessage('error', msg);
      throw new Error(msg);
    }
  }

  wizardSetProgress('Enrollment complete', WIZARD_TOTAL_STEPS);
  wizardSetMessage('success', 'Fingerprint enrolled successfully.');
}

/* -------------------------------------------------------------------- app */

async function boot() {
  render('<p class="muted">Loading...</p>');
  try {
    const mode = await api('GET', '/api/mode');
    if (mode.mode === 'first_run') {
      renderFirstRun();
    } else {
      renderMenu();
    }
  } catch (err) {
    render(`<div class="status-box error">Failed to load: ${esc(err.message)}</div>`);
  }
}

/* --------------------------------------------------------- First-Run-Mode */

function renderFirstRun() {
  render(`
    <div class="card">
      <h2>Welcome</h2>
      <p>No admin fingerprint is configured yet. Enroll one now to continue
      setting up this device. You will place your finger on the sensor six
      times (three templates, two scans each) to build a reliable admin
      fingerprint.</p>
      <div class="actions">
        <button class="primary" id="start-firstrun">Start enrollment</button>
      </div>
    </div>
    <div id="firstrun-wizard"></div>
  `);

  $('#start-firstrun').addEventListener('click', async () => {
    $('#start-firstrun').disabled = true;
    try {
      await runEnrollWizard('/api/firstrun/enroll/scan', $('#firstrun-wizard'));
    } catch (err) {
      wizardSetMessage('error', err.message);
      $('#start-firstrun').disabled = false;
    }
  });
}

/* -------------------------------------------------------- Main menu (WP101) */

function renderMenu() {
  render(`
    <ul class="menu-list">
      <li><button data-nav="system">System Settings</button></li>
      <li><button data-nav="users">User Settings</button></li>
      <li><button data-nav="fingerprints">Fingerprint Library</button></li>
      <li><button class="danger" data-nav="exit">Exit</button></li>
    </ul>
  `);
  $all('[data-nav]').forEach((btn) => {
    btn.addEventListener('click', () => navigate(btn.dataset.nav));
  });
}

function navigate(screen) {
  const screens = {
    menu: renderMenu,
    system: renderSystemMenu,
    'system-general': renderGeneral,
    'system-wifi': renderWifi,
    'system-mqtt': renderMqtt,
    'system-reset': renderReset,
    users: renderUsers,
    fingerprints: renderFingerprints,
    'fingerprints-add': renderAddFingerprint,
    exit: renderExit,
  };
  (screens[screen] || renderMenu)();
}

function backButton(target) {
  return `<button class="link back-btn" data-nav="${target}">&laquo; Back</button>`;
}

function bindBack(target) {
  /* Selects by the dedicated .back-btn class, not the generic [data-nav]
   * attribute - a screen can have other data-nav elements with different
   * targets (e.g. Fingerprint Library's "Add fingerprint" button), and
   * $('[data-nav]') would grab whichever comes first in the DOM
   * regardless of which button was actually meant. */
  $('.back-btn')?.addEventListener('click', () => navigate(target));
}

/* -------------------------------------------------------- System Settings */

function renderSystemMenu() {
  render(`
    ${backButton('menu')}
    <ul class="menu-list">
      <li><button data-nav="system-general">Scheduled Reboot</button></li>
      <li><button data-nav="system-wifi">Wifi Setup</button></li>
      <li><button data-nav="system-mqtt">Mqtt Setup</button></li>
      <li><button class="danger" data-nav="system-reset">Reset Device</button></li>
    </ul>
  `);
  bindBack('menu');
  $all('[data-nav]').forEach((btn) => {
    btn.addEventListener('click', () => navigate(btn.dataset.nav));
  });
}

async function renderGeneral() {
  render(`${backButton('system')}<p class="muted">Loading...</p>`);
  const data = await api('GET', '/api/system/general');
  render(`
    ${backButton('system')}
    <div class="card">
      <h2>Scheduled Reboot</h2>
      <form id="general-form">
        <label>Reboot interval (minutes, 0 = off)</label>
        <input type="number" min="0" id="reboot-minutes" value="${esc(data.reboot_minutes)}">
        <div class="actions"><button class="primary" type="submit">Save</button></div>
      </form>
      <div id="general-status"></div>
    </div>
  `);
  bindBack('system');
  $('#general-form').addEventListener('submit', async (e) => {
    e.preventDefault();
    try {
      await api('POST', '/api/system/general', {
        reboot_minutes: Number($('#reboot-minutes').value) || 0,
      });
      $('#general-status').innerHTML = '<div class="status-box success">Saved.</div>';
    } catch (err) {
      $('#general-status').innerHTML = `<div class="status-box error">${esc(err.message)}</div>`;
    }
  });
}

async function renderWifi() {
  render(`${backButton('system')}<p class="muted">Loading...</p>`);
  const data = await api('GET', '/api/system/wifi');
  render(`
    ${backButton('system')}
    <div class="card">
      <h2>Wifi Setup</h2>
      <form id="wifi-form">
        <label>SSID</label>
        <input type="text" id="wifi-ssid" value="${esc(data.ssid)}" required>
        <label>Password ${data.has_password ? '(leave blank to keep current)' : ''}</label>
        <input type="password" id="wifi-pass" placeholder="${data.has_password ? '********' : ''}">
        <label>AP fallback timeout (seconds)</label>
        <input type="number" min="0" id="wifi-fallback" value="${esc(data.ap_fallback_timeout_s)}">
        <div class="actions"><button class="primary" type="submit">Save</button></div>
      </form>
      <div id="wifi-status"></div>
    </div>
  `);
  bindBack('system');
  $('#wifi-form').addEventListener('submit', async (e) => {
    e.preventDefault();
    const body = {
      ssid: $('#wifi-ssid').value,
      ap_fallback_timeout_s: Number($('#wifi-fallback').value) || 0,
    };
    if ($('#wifi-pass').value) { body.password = $('#wifi-pass').value; }
    try {
      await api('POST', '/api/system/wifi', body);
      $('#wifi-status').innerHTML = '<div class="status-box success">Saved.</div>';
    } catch (err) {
      $('#wifi-status').innerHTML = `<div class="status-box error">${esc(err.message)}</div>`;
    }
  });
}

async function renderMqtt() {
  render(`${backButton('system')}<p class="muted">Loading...</p>`);
  const data = await api('GET', '/api/system/mqtt');
  const fcRows = data.function_codes.map((fc) => `
    <div class="item-row">
      <span>FC ${fc.fc}: ${esc(fc.topic)}</span>
      <button class="link" data-edit-fc="${fc.fc}">Edit</button>
    </div>`).join('') || '<p class="muted">No function codes configured yet.</p>';

  render(`
    ${backButton('system')}
    <div class="card">
      <h2>Mqtt Setup</h2>
      <form id="mqtt-form">
        <label>Broker IP</label>
        <input type="text" id="mqtt-broker" value="${esc(data.broker)}">
        <label>Topic</label>
        <input type="text" id="mqtt-topic" value="${esc(data.topic)}">
        <label>Username</label>
        <input type="text" id="mqtt-user" value="${esc(data.user)}">
        <label>Password ${data.has_password ? '(leave blank to keep current)' : ''}</label>
        <input type="password" id="mqtt-pass">
        <label>Client ID</label>
        <input type="text" id="mqtt-client-id" value="${esc(data.client_id)}">

        <h3>Heartbeat</h3>
        <div class="checkbox-row">
          <input type="checkbox" id="hb-enabled" ${data.heartbeat.enabled ? 'checked' : ''}>
          <label style="margin:0">Enabled</label>
        </div>
        <label>Topic</label>
        <input type="text" id="hb-topic" value="${esc(data.heartbeat.topic)}">
        <label>Message</label>
        <input type="text" id="hb-message" value="${esc(data.heartbeat.message)}">
        <label>Interval (seconds)</label>
        <input type="number" min="1" id="hb-interval" value="${esc(data.heartbeat.interval_s)}">

        <h3>Last Will</h3>
        <div class="checkbox-row">
          <input type="checkbox" id="lw-enabled" ${data.last_will.enabled ? 'checked' : ''}>
          <label style="margin:0">Enabled</label>
        </div>
        <label>Topic</label>
        <input type="text" id="lw-topic" value="${esc(data.last_will.topic)}">
        <label>Message</label>
        <input type="text" id="lw-message" value="${esc(data.last_will.message)}">

        <div class="actions"><button class="primary" type="submit">Save</button></div>
      </form>
      <div id="mqtt-status"></div>
    </div>

    <div class="card">
      <h3>Function Codes (1-31)</h3>
      <div class="item-list">${fcRows}</div>
      <div class="actions"><button id="mqtt-add-fc">Add / Edit Function Code</button></div>
      <div id="fc-editor"></div>
    </div>
  `);
  bindBack('system');

  $('#mqtt-form').addEventListener('submit', async (e) => {
    e.preventDefault();
    const body = {
      broker: $('#mqtt-broker').value,
      topic: $('#mqtt-topic').value,
      user: $('#mqtt-user').value,
      client_id: $('#mqtt-client-id').value,
      heartbeat: {
        enabled: $('#hb-enabled').checked,
        topic: $('#hb-topic').value,
        message: $('#hb-message').value,
        interval_s: Number($('#hb-interval').value) || 60,
      },
      last_will: {
        enabled: $('#lw-enabled').checked,
        topic: $('#lw-topic').value,
        message: $('#lw-message').value,
      },
    };
    if ($('#mqtt-pass').value) { body.password = $('#mqtt-pass').value; }
    try {
      await api('POST', '/api/system/mqtt', body);
      $('#mqtt-status').innerHTML = '<div class="status-box success">Saved.</div>';
    } catch (err) {
      $('#mqtt-status').innerHTML = `<div class="status-box error">${esc(err.message)}</div>`;
    }
  });

  const openFcEditor = (fc, topic, message) => {
    $('#fc-editor').innerHTML = `
      <label>Function Code (1-31)</label>
      <input type="number" min="1" max="31" id="fc-num" value="${esc(fc)}">
      <label>Topic</label>
      <input type="text" id="fc-topic" value="${esc(topic)}">
      <label>Message</label>
      <input type="text" id="fc-message" value="${esc(message)}">
      <div class="actions"><button class="primary" id="fc-save">Save function code</button></div>
    `;
    $('#fc-save').addEventListener('click', async () => {
      try {
        await api('POST', '/api/system/mqtt', {
          function_codes: [{
            fc: Number($('#fc-num').value),
            topic: $('#fc-topic').value,
            message: $('#fc-message').value,
          }],
        });
        renderMqtt();
      } catch (err) {
        $('#mqtt-status').innerHTML = `<div class="status-box error">${esc(err.message)}</div>`;
      }
    });
  };

  $('#mqtt-add-fc').addEventListener('click', () => openFcEditor('', '', ''));
  $all('[data-edit-fc]').forEach((btn) => {
    btn.addEventListener('click', () => {
      const fc = data.function_codes.find((x) => String(x.fc) === btn.dataset.editFc);
      openFcEditor(fc.fc, fc.topic, fc.message);
    });
  });
}

function renderReset() {
  render(`
    ${backButton('system')}
    <div class="card">
      <h2>Reset Device</h2>
      <p>This erases all settings, users and fingerprints. It requires
      scanning the admin fingerprint to confirm.</p>
      <div class="actions"><button class="danger" id="reset-start">Start Reset</button></div>
      <div id="reset-wizard"></div>
    </div>
  `);
  bindBack('system');

  $('#reset-start').addEventListener('click', async () => {
    const myGen = navGeneration;
    $('#reset-start').disabled = true;
    renderWizardShell($('#reset-wizard'));

    const fail = (msg) => {
      wizardSetMessage('error', msg);
      $('#reset-start').disabled = false;
    };

    try {
      await api('POST', '/api/system/reset/start');

      /* One attempt per request: place finger, scan, hold the result LED
       * server-side, then either erase (admin matched) or come back here
       * for another try. No separate "clear"/lift step - the sensor can't
       * reliably report a lift after a scan, so the next attempt's own
       * presence-wait is what actually requires a fresh touch. */
      for (;;) {
        if (!isCurrentGeneration(myGen)) { return; } /* navigated away - stop polling */
        wizardSetMessage('waiting', 'Place the admin finger on the sensor');
        const result = await api('POST', '/api/system/reset/scan', {});
        if (!isCurrentGeneration(myGen)) { return; }

        if (result.status === 'success') {
          wizardSetMessage('success', 'Device reset. Rebooting...');
          return;
        }
        if (result.status === 'no_match' || result.status === 'not_admin') {
          wizardSetMessage('error', 'Scan failed - try again');
          continue;
        }
        fail(result.message || ('Reset failed: ' + result.status));
        return;
      }
    } catch (err) {
      if (isCurrentGeneration(myGen)) { fail(err.message); }
    }
  });
}

/* ------------------------------------------------------------ Exit (WP105) */

function renderExit() {
  render(`
    ${backButton('menu')}
    <div class="card">
      <h2>Exit Setup</h2>
      <p>The device will reboot and attempt to start normally.</p>
      <div class="actions"><button class="primary" id="exit-confirm">Reboot now</button></div>
      <div id="exit-status"></div>
    </div>
  `);
  bindBack('menu');
  $('#exit-confirm').addEventListener('click', async () => {
    $('#exit-confirm').disabled = true;
    try {
      await api('POST', '/api/system/exit');
      $('#exit-status').innerHTML = '<div class="status-box success">Rebooting...</div>';
    } catch (err) {
      $('#exit-status').innerHTML = `<div class="status-box error">${esc(err.message)}</div>`;
      $('#exit-confirm').disabled = false;
    }
  });
}

/* -------------------------------------------------------- User Settings */

async function renderUsers() {
  render(`${backButton('menu')}<p class="muted">Loading...</p>`);
  const data = await api('GET', '/api/users');
  const rows = data.users.map((u) => `
    <div class="item-row">
      <span>#${u.uuid} - ${esc(u.name)}</span>
      <span>
        <button class="link" data-edit-user="${u.uuid}" data-name="${esc(u.name)}">Edit</button>
        <button class="link" data-del-user="${u.uuid}">Delete</button>
      </span>
    </div>`).join('') || '<p class="muted">No users yet.</p>';

  render(`
    ${backButton('menu')}
    <div class="card">
      <h2>Users</h2>
      <div class="item-list">${rows}</div>
      <div class="actions"><button id="user-add">Add user</button></div>
      <div id="user-editor"></div>
      <div id="user-status"></div>
    </div>
  `);
  bindBack('menu');

  const openEditor = (uuid, name) => {
    $('#user-editor').innerHTML = `
      <label>UUID (1-127)</label>
      <input type="number" min="1" max="127" id="edit-uuid" value="${esc(uuid)}" ${uuid ? 'readonly' : ''}>
      <label>Name</label>
      <input type="text" id="edit-name" value="${esc(name)}">
      <div class="actions"><button class="primary" id="user-save">Save</button></div>
    `;
    $('#user-save').addEventListener('click', async () => {
      const targetUuid = Number($('#edit-uuid').value);
      try {
        await api('POST', `/api/users?uuid=${targetUuid}`, { name: $('#edit-name').value });
        renderUsers();
      } catch (err) {
        $('#user-status').innerHTML = `<div class="status-box error">${esc(err.message)}</div>`;
      }
    });
  };

  $('#user-add').addEventListener('click', () => openEditor('', ''));
  $all('[data-edit-user]').forEach((btn) => {
    btn.addEventListener('click', () => openEditor(btn.dataset.editUser, btn.dataset.name));
  });
  $all('[data-del-user]').forEach((btn) => {
    btn.addEventListener('click', async () => {
      if (!confirm('Delete this user and all their fingerprints?')) { return; }
      try {
        await api('DELETE', `/api/users?uuid=${btn.dataset.delUser}`);
        renderUsers();
      } catch (err) {
        $('#user-status').innerHTML = `<div class="status-box error">${esc(err.message)}</div>`;
      }
    });
  });
}

/* --------------------------------------------------------- Fingerprint Library */

async function renderFingerprints() {
  render(`${backButton('menu')}<p class="muted">Loading...</p>`);
  const data = await api('GET', '/api/fingerprints');
  const rows = data.fingerprints.map((fp) => `
    <div class="item-row">
      <span>${esc(fp.name)} - ${FINGER_LABELS[fp.finger_id] ?? ('finger ' + fp.finger_id)} (FC ${fp.function_code})</span>
      <button class="link" data-del-fp="${fp.id}">Delete</button>
    </div>`).join('') || '<p class="muted">No fingerprints enrolled yet.</p>';

  render(`
    ${backButton('menu')}
    <div class="card">
      <h2>Fingerprint Library</h2>
      <div class="item-list">${rows}</div>
      <div class="actions"><button class="primary" id="fp-add">Add fingerprint</button></div>
      <div id="fp-status"></div>
    </div>
  `);
  bindBack('menu');
  $('#fp-add').addEventListener('click', () => navigate('fingerprints-add'));

  $all('[data-del-fp]').forEach((btn) => {
    btn.addEventListener('click', async () => {
      if (!confirm('Delete this fingerprint template?')) { return; }
      try {
        await api('DELETE', `/api/fingerprints?id=${btn.dataset.delFp}`);
        renderFingerprints();
      } catch (err) {
        $('#fp-status').innerHTML = `<div class="status-box error">${esc(err.message)}</div>`;
      }
    });
  });
}

async function renderAddFingerprint() {
  render(`${backButton('fingerprints')}<p class="muted">Loading users...</p>`);
  const users = await api('GET', '/api/users');
  if (users.users.length === 0) {
    render(`
      ${backButton('fingerprints')}
      <div class="status-box error">Add a user first before enrolling a fingerprint.</div>
    `);
    bindBack('fingerprints');
    return;
  }

  const userOptions = users.users.map((u) => `<option value="${u.uuid}">#${u.uuid} - ${esc(u.name)}</option>`).join('');
  const fingerButtons = FINGER_LABELS.map((label, id) =>
    `<button type="button" data-finger="${id}">${esc(label)}</button>`).join('');

  render(`
    ${backButton('fingerprints')}
    <div class="card">
      <h2>Add Fingerprint</h2>
      <form id="prepare-form">
        <label>User</label>
        <select id="fp-uuid">${userOptions}</select>
        <label>Finger</label>
        <div class="finger-svg-grid">${fingerButtons}</div>
        <input type="hidden" id="fp-finger-id" value="0">
        <label>Function Code (1-31)</label>
        <input type="number" min="1" max="31" id="fp-fc" value="1" required>
        <div class="actions"><button class="primary" type="submit">Start scan</button></div>
      </form>
      <div id="prepare-status"></div>
    </div>
  `);
  bindBack('fingerprints');

  $all('[data-finger]').forEach((btn) => {
    btn.addEventListener('click', () => {
      $all('[data-finger]').forEach((b) => b.classList.remove('selected'));
      btn.classList.add('selected');
      $('#fp-finger-id').value = btn.dataset.finger;
    });
  });
  $('[data-finger="0"]').classList.add('selected');

  $('#prepare-form').addEventListener('submit', async (e) => {
    e.preventDefault();
    try {
      await api('POST', '/api/fingerprints/enroll/prepare', {
        uuid: Number($('#fp-uuid').value),
        finger_id: Number($('#fp-finger-id').value),
        function_code: Number($('#fp-fc').value),
      });
    } catch (err) {
      $('#prepare-status').innerHTML = `<div class="status-box error">${esc(err.message)}</div>`;
      return;
    }

    render(`
      <div class="card">
        <h2>Scanning</h2>
        <div id="enroll-wizard"></div>
        <div class="actions"><button id="enroll-cancel" type="button">Cancel</button></div>
      </div>
    `);
    const myGen = navGeneration;
    let cancelled = false;
    $('#enroll-cancel').addEventListener('click', async () => {
      cancelled = true;
      $('#enroll-cancel').disabled = true;
      wizardSetMessage('waiting', 'Cancelling...');
      try {
        await api('POST', '/api/fingerprints/enroll/cancel', {});
      } catch (err) {
        /* best-effort - the wizard loop below is about to stop regardless */
      }
      if (isCurrentGeneration(myGen)) {
        navigate('fingerprints');
      }
    });

    try {
      await runEnrollWizard('/api/fingerprints/enroll/scan', $('#enroll-wizard'), () => cancelled);
      if (!isCurrentGeneration(myGen) || cancelled) { return; } /* navigated away or cancelled */
      await sleep(1200);
      navigate('fingerprints');
    } catch (err) {
      if (isCurrentGeneration(myGen) && !cancelled) {
        wizardSetMessage('error', err.message);
      }
    }
  });
}

boot();
