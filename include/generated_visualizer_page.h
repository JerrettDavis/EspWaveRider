#pragma once

#include <pgmspace.h>

static const char kEmbeddedVisualizerPage[] PROGMEM = R"LBHTML(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <title>Lonely ESP32-S3 mmWave Monitor</title>
  <meta name="viewport" content="width=device-width, initial-scale=1" />

  <style>
    :root {
      color-scheme: light dark;
      --bg: #0f1115;
      --panel: #171b22;
      --panel-2: #1f2530;
      --text: #e8edf5;
      --muted: #94a3b8;
      --border: #2d3748;
      --good: #22c55e;
      --bad: #ef4444;
      --warn: #f59e0b;
      --accent: #60a5fa;
      --code: #0b1020;
    }

    @media (prefers-color-scheme: light) {
      :root {
        --bg: #f7f8fb;
        --panel: #ffffff;
        --panel-2: #f1f5f9;
        --text: #111827;
        --muted: #64748b;
        --border: #d8dee9;
        --code: #f8fafc;
      }
    }

    * { box-sizing: border-box; }

    body {
      margin: 0;
      font-family: ui-sans-serif, system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
      background: radial-gradient(circle at top, var(--panel-2), var(--bg) 45%);
      color: var(--text);
    }

    header {
      padding: 20px;
      border-bottom: 1px solid var(--border);
      background: color-mix(in srgb, var(--panel) 88%, transparent);
      backdrop-filter: blur(12px);
      position: sticky;
      top: 0;
      z-index: 5;
    }

    h1 {
      margin: 0 0 8px;
      font-size: 22px;
      letter-spacing: -0.02em;
    }

    .subtle {
      color: var(--muted);
      font-size: 14px;
    }

    main {
      padding: 20px;
      display: grid;
      gap: 16px;
      grid-template-columns: repeat(12, 1fr);
    }

    section {
      background: color-mix(in srgb, var(--panel) 94%, transparent);
      border: 1px solid var(--border);
      border-radius: 18px;
      padding: 16px;
      box-shadow: 0 12px 40px rgba(0, 0, 0, 0.18);
    }

    .span-12 { grid-column: span 12; }
    .span-8 { grid-column: span 8; }
    .span-6 { grid-column: span 6; }
    .span-4 { grid-column: span 4; }

    @media (max-width: 1000px) {
      .span-8, .span-6, .span-4 { grid-column: span 12; }
    }

    .toolbar {
      display: flex;
      flex-wrap: wrap;
      gap: 10px;
      align-items: center;
    }

    .workspace-shell {
      display: grid;
      gap: 16px;
      background:
        radial-gradient(circle at top right, color-mix(in srgb, var(--accent) 22%, transparent), transparent 34%),
        color-mix(in srgb, var(--panel) 94%, transparent);
    }

    .workspace-shell .section-title {
      margin-bottom: 0;
    }

    .view-tabs {
      display: flex;
      flex-wrap: wrap;
      gap: 10px;
    }

    .view-tab {
      border-radius: 999px;
      padding: 10px 16px;
    }

    .view-tab.active {
      background: var(--accent);
      border-color: color-mix(in srgb, var(--accent) 70%, black);
      color: white;
    }

    .view-panel {
      display: none;
      gap: 14px;
    }

    .view-panel.active {
      display: grid;
    }

    .shell-grid {
      display: grid;
      grid-template-columns: minmax(0, 1.35fr) minmax(280px, 0.65fr);
      gap: 14px;
    }

    @media (max-width: 960px) {
      .shell-grid {
        grid-template-columns: 1fr;
      }
    }

    .helper-grid {
      display: grid;
      grid-template-columns: repeat(2, minmax(0, 1fr));
      gap: 12px;
    }

    @media (max-width: 760px) {
      .helper-grid {
        grid-template-columns: 1fr;
      }
    }

    .helper-card,
    .wizard-card,
    .topic-card {
      background: var(--panel-2);
      border: 1px solid var(--border);
      border-radius: 16px;
      padding: 16px;
      display: grid;
      gap: 10px;
    }

    .helper-card h3,
    .wizard-card h3,
    .topic-card h3 {
      margin: 0;
      font-size: 16px;
      letter-spacing: -0.02em;
    }

    .helper-card p,
    .wizard-card p,
    .topic-card p {
      margin: 0;
      color: var(--muted);
      font-size: 13px;
      line-height: 1.5;
    }

    .mini-status {
      display: inline-flex;
      align-items: center;
      gap: 8px;
      width: fit-content;
      border: 1px solid var(--border);
      border-radius: 999px;
      background: color-mix(in srgb, var(--panel) 88%, transparent);
      padding: 6px 10px;
      color: var(--muted);
      font-size: 12px;
      font-weight: 700;
      letter-spacing: 0.04em;
      text-transform: uppercase;
    }

    .mini-dot {
      width: 8px;
      height: 8px;
      border-radius: 999px;
      background: var(--warn);
    }

    .mini-dot.good { background: var(--good); }
    .mini-dot.bad { background: var(--bad); }

    .helper-actions {
      display: flex;
      flex-wrap: wrap;
      gap: 10px;
    }

    .topic-value {
      font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, "Liberation Mono", monospace;
      font-size: 13px;
      padding: 10px 12px;
      border-radius: 12px;
      background: var(--code);
      border: 1px solid var(--border);
      word-break: break-all;
    }

    .view-hidden {
      display: none;
    }

    button {
      appearance: none;
      border: 1px solid var(--border);
      background: var(--panel-2);
      color: var(--text);
      border-radius: 12px;
      padding: 10px 14px;
      cursor: pointer;
      font-weight: 650;
    }

    button:hover { border-color: var(--accent); }

    button.primary {
      background: var(--accent);
      color: white;
      border-color: color-mix(in srgb, var(--accent) 85%, black);
    }

    button.danger {
      background: color-mix(in srgb, var(--bad) 18%, var(--panel-2));
      border-color: color-mix(in srgb, var(--bad) 55%, var(--border));
    }

    input,
    select {
      border: 1px solid var(--border);
      background: var(--panel-2);
      color: var(--text);
      border-radius: 12px;
      padding: 10px 12px;
      min-width: 260px;
    }

    .status-pill {
      display: inline-flex;
      align-items: center;
      gap: 8px;
      border: 1px solid var(--border);
      background: var(--panel-2);
      border-radius: 999px;
      padding: 8px 12px;
      color: var(--muted);
      font-size: 14px;
    }

    .status-pill.warn {
      border-color: color-mix(in srgb, var(--warn) 55%, var(--border));
      background: color-mix(in srgb, var(--warn) 14%, var(--panel-2));
      color: var(--text);
    }

    .version-banner {
      display: none;
      gap: 10px;
      align-items: flex-start;
      border: 1px solid color-mix(in srgb, var(--warn) 55%, var(--border));
      background: color-mix(in srgb, var(--warn) 12%, var(--panel));
      border-radius: 16px;
      padding: 14px 16px;
    }

    .version-banner.visible {
      display: grid;
    }

    .version-banner-title {
      font-size: 12px;
      letter-spacing: 0.08em;
      text-transform: uppercase;
      color: var(--warn);
      font-weight: 800;
    }

    .version-banner-body {
      color: var(--text);
      line-height: 1.5;
    }

    .dot {
      width: 10px;
      height: 10px;
      border-radius: 999px;
      background: var(--bad);
      box-shadow: 0 0 0 4px color-mix(in srgb, var(--bad) 18%, transparent);
    }

    .dot.good {
      background: var(--good);
      box-shadow: 0 0 0 4px color-mix(in srgb, var(--good) 18%, transparent);
    }

    .dot.warn {
      background: var(--warn);
      box-shadow: 0 0 0 4px color-mix(in srgb, var(--warn) 18%, transparent);
    }

    .metric-grid {
      display: grid;
      grid-template-columns: repeat(3, minmax(0, 1fr));
      gap: 12px;
    }

    @media (max-width: 700px) {
      .metric-grid { grid-template-columns: repeat(2, minmax(0, 1fr)); }
    }

    .metric {
      background: var(--panel-2);
      border: 1px solid var(--border);
      border-radius: 14px;
      padding: 14px;
    }

    .metric .label {
      color: var(--muted);
      font-size: 12px;
      text-transform: uppercase;
      letter-spacing: 0.08em;
    }

    .metric .value {
      margin-top: 6px;
      font-size: 24px;
      font-weight: 800;
      letter-spacing: -0.03em;
      word-break: break-word;
    }

    .presence-card {
      min-height: 170px;
      display: grid;
      place-items: center;
      text-align: center;
      border-radius: 16px;
      border: 1px solid var(--border);
      background: radial-gradient(circle at center, color-mix(in srgb, var(--bad) 18%, transparent), transparent 58%), var(--panel-2);
      transition: 180ms ease;
    }

    .presence-card.active {
      background: radial-gradient(circle at center, color-mix(in srgb, var(--good) 28%, transparent), transparent 58%), var(--panel-2);
      border-color: color-mix(in srgb, var(--good) 60%, var(--border));
    }

    .presence-card .big {
      font-size: 44px;
      font-weight: 900;
      letter-spacing: -0.05em;
    }

    .presence-card .small {
      color: var(--muted);
      margin-top: 6px;
    }

    canvas {
      width: 100%;
      border-radius: 14px;
      background: var(--code);
      border: 1px solid var(--border);
      display: block;
    }

    #activityCanvas { height: 260px; }
    #surfaceCanvas { height: 470px; }

    pre {
      margin: 0;
      overflow: auto;
      max-height: 360px;
      background: var(--code);
      border: 1px solid var(--border);
      border-radius: 14px;
      padding: 12px;
      font-size: 12px;
      line-height: 1.45;
      white-space: pre-wrap;
      word-break: break-word;
    }

    .event-list {
      display: flex;
      flex-direction: column;
      gap: 8px;
      max-height: 420px;
      overflow: auto;
    }

    .event {
      background: var(--panel-2);
      border: 1px solid var(--border);
      border-radius: 12px;
      padding: 10px;
      font-size: 13px;
    }

    .event-top {
      display: flex;
      justify-content: space-between;
      gap: 12px;
      margin-bottom: 6px;
    }

    .event-type {
      font-weight: 800;
      color: var(--accent);
    }

    .event-time {
      color: var(--muted);
      font-variant-numeric: tabular-nums;
    }

    .event-body {
      color: var(--muted);
      font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, "Liberation Mono", monospace;
      word-break: break-word;
    }

    .section-title {
      display: flex;
      align-items: baseline;
      justify-content: space-between;
      gap: 12px;
      margin-bottom: 12px;
    }

    .section-title h2 {
      margin: 0;
      font-size: 16px;
      letter-spacing: -0.02em;
    }

    .section-title span {
      color: var(--muted);
      font-size: 13px;
      text-align: right;
    }

    .hint {
      color: var(--muted);
      font-size: 13px;
      line-height: 1.45;
      margin-top: 10px;
    }

    .config-grid {
      display: grid;
      grid-template-columns: repeat(3, minmax(0, 1fr));
      gap: 12px;
    }

    @media (max-width: 900px) {
      .config-grid { grid-template-columns: 1fr; }
    }

    .field {
      display: flex;
      flex-direction: column;
      gap: 8px;
    }

    .field label {
      font-size: 12px;
      letter-spacing: 0.08em;
      text-transform: uppercase;
      color: var(--muted);
    }

    .range-header {
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 10px;
    }

    .range-value {
      min-width: 58px;
      padding: 4px 8px;
      border: 1px solid var(--border);
      border-radius: 999px;
      background: var(--panel-2);
      color: var(--text);
      font-size: 12px;
      text-align: center;
      font-variant-numeric: tabular-nums;
    }

    .setup-actions {
      margin-top: 14px;
    }

    .setup-status {
      color: var(--muted);
      font-size: 14px;
    }

    .diagnostics-grid {
      display: grid;
      grid-template-columns: repeat(4, minmax(0, 1fr));
      gap: 12px;
    }

    .icon-button {
      min-width: 46px;
      padding: 10px 12px;
      font-size: 18px;
      line-height: 1;
    }

    .settings-hidden {
      display: none;
    }

    .settings-stack {
      display: grid;
      gap: 18px;
    }

    .settings-stack.settings-hidden {
      display: none;
    }

    .subsection {
      display: grid;
      gap: 12px;
      padding-top: 6px;
      border-top: 1px solid var(--border);
    }

    .subsection:first-of-type {
      padding-top: 0;
      border-top: 0;
    }

    .checkbox-field {
      justify-content: end;
    }

    .checkbox-row {
      display: flex;
      align-items: center;
      gap: 10px;
      min-height: 44px;
      padding: 10px 12px;
      border: 1px solid var(--border);
      border-radius: 12px;
      background: var(--panel-2);
    }

    .checkbox-row input {
      min-width: auto;
      margin: 0;
    }

    .room-editor-toolbar {
      display: grid;
      grid-template-columns: repeat(4, minmax(0, 1fr));
      gap: 12px;
      margin-top: 12px;
    }

    @media (max-width: 900px) {
      .room-editor-toolbar { grid-template-columns: repeat(2, minmax(0, 1fr)); }
    }

    @media (max-width: 560px) {
      .room-editor-toolbar { grid-template-columns: 1fr; }
    }

    .room-editor-note {
      margin-top: 10px;
      color: var(--muted);
      font-size: 13px;
      line-height: 1.45;
    }

    .room-editor-status {
      margin-top: 8px;
      color: var(--accent);
      font-size: 13px;
      line-height: 1.45;
      min-height: 20px;
    }

    .peer-link-list,
    .calibration-sample-list {
      display: grid;
      gap: 10px;
    }

    .peer-link-card,
    .calibration-sample-card {
      background: var(--panel-2);
      border: 1px solid var(--border);
      border-radius: 14px;
      padding: 12px;
      display: grid;
      gap: 8px;
    }

    .peer-link-top,
    .calibration-sample-top {
      display: flex;
      justify-content: space-between;
      gap: 10px;
      align-items: baseline;
      flex-wrap: wrap;
    }

    .peer-link-name,
    .calibration-sample-title {
      font-weight: 800;
      letter-spacing: -0.02em;
    }

    .peer-link-meta,
    .calibration-sample-meta {
      color: var(--muted);
      font-size: 12px;
      font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, "Liberation Mono", monospace;
    }

    .peer-link-actions,
    .calibration-actions {
      display: flex;
      flex-wrap: wrap;
      gap: 10px;
    }

    .peer-link-actions a {
      display: inline-flex;
      align-items: center;
      justify-content: center;
      min-height: 42px;
      padding: 10px 14px;
      border-radius: 12px;
      border: 1px solid var(--border);
      background: var(--panel);
      color: var(--text);
      text-decoration: none;
      font-weight: 700;
    }

    .peer-link-actions a:hover {
      border-color: var(--accent);
    }

    .calibration-summary-grid {
      display: grid;
      grid-template-columns: repeat(2, minmax(0, 1fr));
      gap: 12px;
    }

    @media (max-width: 560px) {
      .calibration-summary-grid {
        grid-template-columns: 1fr;
      }
    }

    .calibration-status {
      color: var(--accent);
      font-size: 13px;
      line-height: 1.45;
      min-height: 20px;
    }

    .sensor-grid {
      display: grid;
      grid-template-columns: repeat(4, minmax(0, 1fr));
      gap: 12px;
    }

    @media (max-width: 900px) {
      .sensor-grid { grid-template-columns: repeat(2, minmax(0, 1fr)); }
    }

    @media (max-width: 560px) {
      .sensor-grid { grid-template-columns: 1fr; }
    }

    .led-chip {
      display: inline-flex;
      align-items: center;
      gap: 8px;
    }

    .led-swatch {
      width: 16px;
      height: 16px;
      border-radius: 999px;
      border: 1px solid rgba(255,255,255,0.2);
      background: #000;
      box-shadow: 0 0 0 3px rgba(255,255,255,0.06);
    }

    @media (max-width: 900px) {
      .diagnostics-grid { grid-template-columns: repeat(2, minmax(0, 1fr)); }
    }

    @media (max-width: 560px) {
      .diagnostics-grid { grid-template-columns: 1fr; }
    }
  </style>
</head>

<body>
  <header>
    <h1>Lonely ESP32-S3 mmWave Monitor</h1>
    <div class="subtle">Reads newline-delimited JSON from the ESP32-S3 firmware over Web Serial. Supports raw UART frames, LD2420 text range mode, and LD2420 energy gate mode.</div>
  </header>

  <main>
    <section class="span-12">
      <div class="toolbar">
        <button id="connectButton" class="primary">Connect Serial</button>
        <button id="disconnectButton">Disconnect</button>
        <button id="clearButton">Clear</button>
        <button id="energyButton">Send energy</button>
        <button id="statusButton">Send status</button>
        <button id="unitsToggleButton">Units: Metric</button>
        <button id="settingsToggleButton" class="icon-button" title="Toggle settings" aria-label="Toggle settings">&#9881;</button>

        <span class="status-pill">
          <span id="connectionDot" class="dot"></span>
          <span id="connectionText">Disconnected</span>
        </span>

        <span class="status-pill">
          <span id="firmwareVersionText">Firmware: waiting</span>
        </span>

        <span id="peerVersionPill" class="status-pill warn" hidden>
          <span class="dot warn"></span>
          <span id="peerVersionPillText">Peer version mismatch</span>
        </span>

        <input id="commandInput" placeholder="serial command: ping, status, energy" />
        <button id="sendButton">Send</button>
      </div>
    </section>

    <section class="span-12 workspace-shell">
      <div class="section-title">
        <h2>Control Center</h2>
        <span>choose a view, follow guided steps, and use the compact observation feed</span>
      </div>

      <div class="view-tabs" role="tablist" aria-label="Workspace views">
        <button id="dashboardViewButton" class="view-tab active" type="button" role="tab" aria-selected="true">Dashboard</button>
        <button id="setupViewButton" class="view-tab" type="button" role="tab" aria-selected="false">Setup Wizard</button>
        <button id="sensorsViewButton" class="view-tab" type="button" role="tab" aria-selected="false">Sensor Lab</button>
        <button id="consoleViewButton" class="view-tab" type="button" role="tab" aria-selected="false">Console</button>
      </div>

      <div id="dashboardPanel" class="view-panel active">
        <div id="versionBanner" class="version-banner" role="status" aria-live="polite">
          <div class="version-banner-title">Version Warning</div>
          <div id="versionBannerText" class="version-banner-body">Waiting for version telemetry.</div>
        </div>

        <div class="shell-grid">
          <div class="helper-grid">
            <article class="helper-card">
              <div class="mini-status"><span id="wizardDeviceDot" class="mini-dot bad"></span><span id="wizardDeviceStatus">Waiting for device telemetry</span></div>
              <h3>Use Dashboard For Fast Triage</h3>
              <p>Keep this view up during installs and walk-tests. It surfaces connection state, presence, room fusion, and BLE sightings without exposing the full configuration surface.</p>
              <div class="helper-actions">
                <button id="dashboardOpenSetupButton" type="button">Open Setup Wizard</button>
                <button id="dashboardOpenSensorsButton" type="button">Open Sensor Lab</button>
              </div>
            </article>

            <article class="helper-card">
              <div class="mini-status"><span id="wizardRoomDot" class="mini-dot"></span><span id="wizardRoomStatus">Room layout needs review</span></div>
              <h3>Keep Geometry Deliberate</h3>
              <p>Room editor changes matter more than raw threshold fiddling. Use Setup Wizard when placing sensors, then jump to Sensor Lab only if the room view and clutter filtering need adjustment.</p>
              <div class="helper-actions">
                <button id="dashboardOpenConsoleButton" type="button">Open Console</button>
              </div>
            </article>

            <article class="helper-card">
              <div class="mini-status"><span class="mini-dot"></span><span>Firmware Sync</span></div>
              <h3>Converge To Highest Peer Release</h3>
              <p>Older nodes can self-update to the highest release version visible on the room mesh. The update is downloaded from GitHub for this node's board target.</p>
              <div id="firmwareSyncCandidate" class="topic-value">No higher peer release visible.</div>
              <div class="helper-actions">
                <button id="firmwareSyncButton" type="button" disabled>Sync To Highest Peer</button>
              </div>
              <p id="firmwareSyncStatus" class="hint">Waiting for firmware sync telemetry.</p>
            </article>
          </div>

          <aside class="topic-card">
            <div class="mini-status"><span id="wizardFeedDot" class="mini-dot"></span><span id="wizardFeedStatus">Observation feed waiting on MQTT</span></div>
            <h3>Temporal Observation Feed</h3>
            <p>Interested parties can subscribe to one compact JSON stream per node instead of depending on a growing set of Home Assistant entities.</p>
            <div id="observationTopicValue" class="topic-value">—</div>
            <div class="helper-actions">
              <button id="copyObservationTopicButton" type="button">Copy Topic</button>
            </div>
            <p id="observationTopicHint">Once MQTT is connected, subscribe to this topic to capture temporal radar, BLE, room, and Wi-Fi observations.</p>
          </aside>
        </div>
      </div>

      <div id="setupPanel" class="view-panel">
        <div class="helper-grid">
          <article class="wizard-card">
            <div class="mini-status"><span class="mini-dot good"></span><span>Step 1</span></div>
            <h3>Attach To The Node</h3>
            <p>Use the hosted page over Wi-Fi for normal setup. If the node has never been provisioned or is unreachable, attach Web Serial first.</p>
            <div class="helper-actions">
              <button id="wizardConnectButton" type="button">Connect Serial</button>
              <button id="wizardStatusButton" type="button">Refresh Status</button>
            </div>
          </article>

          <article class="wizard-card">
            <div class="mini-status"><span id="wizardConfigDot" class="mini-dot"></span><span id="wizardConfigStatus">Needs Wi-Fi + MQTT setup</span></div>
            <h3>Provision Wi-Fi And MQTT</h3>
            <p>Scan nearby networks if needed, then save the provisioning form. The main button opens Home Assistant once the node has the basics.</p>
            <div class="helper-actions">
              <button id="wizardScanWifiButton" type="button">Scan Wi-Fi</button>
              <button id="wizardSaveSetupButton" type="button">Save Setup</button>
              <button id="wizardOpenHaButton" class="primary" type="button">Open In Home Assistant</button>
            </div>
          </article>

          <article class="wizard-card">
            <div class="mini-status"><span class="mini-dot"></span><span>Step 3</span></div>
            <h3>Map The Room</h3>
            <p>Set room identity, place the node on the floor plan, then save tuning only after the geometry is reasonable. The room editor is the first stop for de-dupe quality.</p>
            <div class="helper-actions">
              <button id="wizardSaveTuningButton" type="button">Save Room + Tuning</button>
              <button id="wizardOpenSensorsButton" type="button">Review Sensor Lab</button>
            </div>
          </article>

          <article class="wizard-card">
            <div class="mini-status"><span class="mini-dot good"></span><span>Step 4</span></div>
            <h3>Use The Observation Feed</h3>
            <p>For raw history and temporal analytics, subscribe to the observation topic instead of creating more point-in-time entities.</p>
            <div class="topic-value" id="setupObservationTopicValue">—</div>
            <div class="helper-actions">
              <button id="wizardCopyObservationTopicButton" type="button">Copy Topic</button>
              <button id="wizardOpenConsoleButton" type="button">Open Console</button>
            </div>
          </article>
        </div>
      </div>

      <div id="sensorsPanel" class="view-panel">
        <div class="helper-grid">
          <article class="helper-card">
            <h3>Sensor Lab Focus</h3>
            <p>Use this view when tuning clutter rejection, checking room geometry, or comparing BLE activity against presence decisions. It keeps the sensor-heavy panels together.</p>
          </article>
          <article class="helper-card">
            <h3>Interpretation Notes</h3>
            <p>Static radar surface is a bucketed field, not a camera. Fused room view is layout guidance. BLE sightings are passive observations, not identity proof on their own.</p>
          </article>
        </div>
      </div>

      <div id="consolePanel" class="view-panel">
        <div class="helper-grid">
          <article class="helper-card">
            <h3>Console View</h3>
            <p>Use events and raw NDJSON when you need to see the exact telemetry stream, validate command responses, or capture behavior that the dashboard smooths over.</p>
          </article>
          <article class="helper-card">
            <h3>When To Use It</h3>
            <p>Reach for Console when debugging provisioning, MQTT state transitions, or raw radar frame behavior. Stay on Dashboard for normal installation and operations.</p>
          </article>
        </div>
      </div>
    </section>

    <section id="settingsSection" class="span-12 settings-stack" data-view="setup">
      <div class="section-title">
        <h2>Settings</h2>
        <span>network, tuning, mesh, and sensor reporting</span>
      </div>

      <div class="subsection">
        <div class="section-title">
          <h2>Home Assistant Setup</h2>
          <span>serial provisioning + MQTT discovery</span>
        </div>

        <div class="config-grid">
          <div class="field">
            <label for="wifiSelect">Scanned Wi-Fi</label>
            <select id="wifiSelect">
              <option value="">Scan over serial</option>
            </select>
          </div>

          <div class="field">
            <label for="wifiSsidInput">Manual SSID</label>
            <input id="wifiSsidInput" placeholder="optional override or hidden network" />
          </div>

          <div class="field">
            <label for="wifiPasswordInput">Wi-Fi Password</label>
            <input id="wifiPasswordInput" type="password" placeholder="network password" />
          </div>

          <div class="field">
            <label for="mqttHostInput">MQTT Host</label>
            <input id="mqttHostInput" value="homeassistant.local" placeholder="defaults to homeassistant.local" />
          </div>

          <div class="field">
            <label for="mqttPortInput">MQTT Port</label>
            <input id="mqttPortInput" value="1883" inputmode="numeric" />
          </div>

          <div class="field">
            <label for="mqttUserInput">MQTT Username</label>
            <input id="mqttUserInput" placeholder="optional" />
          </div>

          <div class="field">
            <label for="mqttPasswordInput">MQTT Password</label>
            <input id="mqttPasswordInput" type="password" placeholder="optional" />
          </div>

          <div class="field">
            <label for="nodeIdInput">Node ID</label>
            <input id="nodeIdInput" value="lb_mmwave_presence" />
          </div>

          <div class="field">
            <label for="friendlyNameInput">Friendly Name</label>
            <input id="friendlyNameInput" value="LB mmWave Presence" />
          </div>
        </div>

        <div class="toolbar setup-actions">
          <button id="scanWifiButton">Scan Wi-Fi</button>
          <button id="saveHaButton">Save HA Setup</button>
          <button id="addToHaButton" class="primary">Open Device In Home Assistant</button>
          <button id="openMosquittoButton">Open Mosquitto Setup</button>
          <span id="haSetupStatus" class="setup-status">Connect over serial, scan Wi-Fi if needed, save settings, then open Home Assistant. The device should appear automatically under MQTT.</span>
        </div>

        <div class="hint">This provisions Wi-Fi and MQTT over Web Serial so Home Assistant can discover the controller automatically through MQTT. Leave MQTT Host alone for the usual Home Assistant setup. If MQTT is already configured in Home Assistant, use the main button to open Devices &amp; Services and look for the discovered device under MQTT. This does not emulate the native ESPHome API.</div>
      </div>

      <div class="subsection">
        <div class="section-title">
          <h2>Detection Tuning</h2>
          <span>presence filtering, mesh identity, and RGB behavior</span>
        </div>

        <div class="config-grid">
          <div class="field">
            <label for="roomIdInput">Room ID</label>
            <input id="roomIdInput" placeholder="room-default" />
          </div>

          <div class="field">
            <label for="roomEditorTargetInput">Editor Target</label>
            <select id="roomEditorTargetInput">
              <option value="__local__">Local node</option>
            </select>
          </div>

          <div class="field">
            <label for="sensorRoleInput">Sensor Role</label>
            <select id="sensorRoleInput">
              <option value="auto">Auto</option>
              <option value="primary">Primary</option>
              <option value="secondary">Secondary</option>
              <option value="doorway">Doorway</option>
              <option value="perimeter">Perimeter</option>
            </select>
          </div>

          <div class="field">
            <label for="poseXInput">Sensor X Position (cm)</label>
            <input id="poseXInput" type="number" min="-2000" max="2000" step="5" value="0" />
          </div>

          <div class="field">
            <label for="poseYInput">Sensor Y Position (cm)</label>
            <input id="poseYInput" type="number" min="-2000" max="2000" step="5" value="0" />
          </div>

          <div class="field">
            <div class="range-header">
              <label for="headingInput">Sensor Heading</label>
              <span id="headingValue" class="range-value">-90 deg</span>
            </div>
            <input id="headingInput" type="range" min="-180" max="180" step="5" value="-90" />
          </div>

          <div class="field">
            <label for="roomWidthInput">Room Width (cm)</label>
            <input id="roomWidthInput" type="number" min="100" max="4000" step="5" value="600" />
          </div>

          <div class="field">
            <label for="roomHeightInput">Room Height (cm)</label>
            <input id="roomHeightInput" type="number" min="100" max="4000" step="5" value="400" />
          </div>

          <div class="field">
            <div class="range-header">
              <label for="maxRangeInput">Max Detection Range (cm)</label>
              <span id="maxRangeValue" class="range-value">1120</span>
            </div>
            <input id="maxRangeInput" type="range" min="70" max="1120" step="70" value="1120" />
          </div>

          <div class="field">
            <div class="range-header">
              <label for="minGateEnergyInput">Min Gate Energy</label>
              <span id="minGateEnergyValue" class="range-value">25</span>
            </div>
            <input id="minGateEnergyInput" type="range" min="0" max="1000" step="5" value="25" />
          </div>

          <div class="field">
            <div class="range-header">
              <label for="sensitivityInput">Sensitivity (%)</label>
              <span id="sensitivityValue" class="range-value">55%</span>
            </div>
            <input id="sensitivityInput" type="range" min="0" max="100" step="1" value="55" />
          </div>

          <div class="field">
            <div class="range-header">
              <label for="presenceHoldInput">Presence Decay (ms)</label>
              <span id="presenceHoldValue" class="range-value">4000</span>
            </div>
            <input id="presenceHoldInput" type="range" min="500" max="15000" step="100" value="4000" />
          </div>

          <div class="field">
            <div class="range-header">
              <label for="minActiveGatesInput">Min Active Gates</label>
              <span id="minActiveGatesValue" class="range-value">1</span>
            </div>
            <input id="minActiveGatesInput" type="range" min="1" max="16" step="1" value="1" />
          </div>

          <div class="field">
            <div class="range-header">
              <label for="minActivityScoreInput">Min Activity Score</label>
              <span id="minActivityScoreValue" class="range-value">10</span>
            </div>
            <input id="minActivityScoreInput" type="range" min="0" max="100" step="1" value="10" />
          </div>

          <div class="field checkbox-field">
            <label for="ledEnabledInput">Status RGB LED</label>
            <div class="checkbox-row">
              <input id="ledEnabledInput" type="checkbox" />
              <span>Enable onboard LED feedback</span>
            </div>
          </div>

          <div class="field">
            <div class="range-header">
              <label for="ledBrightnessInput">LED Brightness</label>
              <span id="ledBrightnessValue" class="range-value">32</span>
            </div>
            <input id="ledBrightnessInput" type="range" min="0" max="255" step="1" value="32" />
          </div>
        </div>

        <div class="toolbar setup-actions">
          <button id="saveTuningButton">Save Tuning</button>
          <span class="setup-status">Higher sensitivity lowers internal thresholds. Presence Decay controls how long the device keeps occupancy active and how long the RGB LED fades from green to red after motion drops out.</span>
        </div>

        <div class="room-editor-toolbar">
          <div class="field checkbox-field">
            <label for="roomSnapEnabledInput">Snap To Grid</label>
            <div class="checkbox-row">
              <input id="roomSnapEnabledInput" type="checkbox" checked />
              <span>Round drags before saving</span>
            </div>
          </div>

          <div class="field">
            <label for="roomGridSizeInput">Grid Spacing</label>
            <select id="roomGridSizeInput">
              <option value="5">5 cm</option>
              <option value="10">10 cm</option>
              <option value="25" selected>25 cm</option>
              <option value="50">50 cm</option>
            </select>
          </div>
        </div>

        <div class="room-editor-note">The selected node is editable directly on the room canvas. Local edits save straight to this controller; peer edits are published over the room MQTT mesh and settle into the fused view as summaries refresh.</div>
        <div id="roomEditorStatus" class="room-editor-status"></div>
      </div>

      <div class="subsection">
        <div class="section-title">
          <h2>Sensor Reporting</h2>
          <span>live local + room level state</span>
        </div>

        <div class="sensor-grid">
          <div class="metric"><div class="label">Filtered Presence</div><div id="filteredPresenceMetric" class="value">—</div></div>
          <div class="metric"><div class="label">GPIO Presence</div><div id="gpioPresenceMetric" class="value">—</div></div>
          <div class="metric"><div class="label">Detection Candidate</div><div id="candidateMetric" class="value">—</div></div>
          <div class="metric"><div class="label">Presence Decay</div><div id="presenceDecayMetric" class="value">—</div></div>
          <div class="metric"><div class="label">People Estimate</div><div id="peopleEstimateMetric" class="value">—</div></div>
          <div class="metric"><div class="label">Active Gates</div><div id="activeGateMetric" class="value">—</div></div>
          <div class="metric"><div class="label">Activity Score</div><div id="activityScoreMetric" class="value">—</div></div>
          <div class="metric"><div class="label">Dominant Gate</div><div id="dominantGateMetric" class="value">—</div></div>
          <div class="metric"><div class="label">Dominant Distance</div><div id="dominantDistanceMetric" class="value">—</div></div>
          <div class="metric"><div class="label">Dominant Energy</div><div id="dominantEnergyMetric" class="value">—</div></div>
          <div class="metric"><div class="label">Total Gate Energy</div><div id="totalGateEnergyMetric" class="value">—</div></div>
          <div class="metric"><div class="label">Room People</div><div id="roomPeopleMetric" class="value">—</div></div>
          <div class="metric"><div class="label">Room Active Nodes</div><div id="roomActiveNodesMetric" class="value">—</div></div>
          <div class="metric"><div class="label">Room Peer Nodes</div><div id="roomPeerNodesMetric" class="value">—</div></div>
          <div class="metric"><div class="label">Room Activity</div><div id="roomActivityMetric" class="value">—</div></div>
          <div class="metric"><div class="label">Status LED</div><div id="statusLedMetric" class="value led-chip"><span id="statusLedSwatch" class="led-swatch"></span><span id="statusLedText">—</span></div></div>
        </div>
      </div>
    </section>

    <section class="span-12" data-view="dashboard">
      <div class="section-title">
        <h2>Connection Diagnostics</h2>
        <span>live HA / Wi-Fi / MQTT state</span>
      </div>

      <div class="diagnostics-grid">
        <div class="metric"><div class="label">Wi-Fi</div><div id="wifiStateMetric" class="value">—</div></div>
        <div class="metric"><div class="label">MQTT</div><div id="mqttStateMetric" class="value">—</div></div>
        <div class="metric"><div class="label">IP Address</div><div id="ipAddressMetric" class="value">—</div></div>
        <div class="metric"><div class="label">Topic Prefix</div><div id="topicPrefixMetric" class="value">—</div></div>
        <div class="metric"><div class="label">Wi-Fi RSSI</div><div id="wifiRssiMetric" class="value">—</div></div>
        <div class="metric"><div class="label">Wi-Fi Channel</div><div id="wifiChannelMetric" class="value">—</div></div>
        <div class="metric"><div class="label">Wi-Fi BSSID</div><div id="wifiBssidMetric" class="value">—</div></div>
      </div>

      <div class="hint" id="wifiReasonText">No connection diagnostics received yet.</div>
    </section>

    <section class="span-12" data-view="dashboard sensors">
      <div class="section-title">
        <h2>Peer Navigation</h2>
        <span id="peerLinkSummary">no network peers announced yet</span>
      </div>
      <div id="peerLinkList" class="peer-link-list">
        <div class="hint">Nodes discovered on the LAN will appear here with direct dashboard links.</div>
      </div>
    </section>

    <section class="span-4" data-view="dashboard">
      <div class="section-title">
        <h2>Presence</h2>
        <span>GPIO / radar mode</span>
      </div>

      <div id="presenceCard" class="presence-card">
        <div>
          <div id="presenceText" class="big">UNKNOWN</div>
          <div id="presenceSubtext" class="small">Waiting for samples</div>
        </div>
      </div>
    </section>

    <section class="span-8" data-view="dashboard">
      <div class="section-title">
        <h2>Device Metrics</h2>
        <span>heartbeat / radar totals</span>
      </div>

      <div class="metric-grid">
        <div class="metric"><div class="label">Uptime</div><div id="uptimeMetric" class="value">—</div></div>
        <div class="metric"><div class="label">Free Heap</div><div id="heapMetric" class="value">—</div></div>
        <div class="metric"><div class="label">Radar Bytes</div><div id="radarBytesMetric" class="value">0</div></div>
        <div class="metric"><div class="label">Radar Frames</div><div id="radarFramesMetric" class="value">0</div></div>
        <div class="metric"><div class="label">Last Range</div><div id="rangeMetric" class="value">—</div></div>
        <div class="metric"><div class="label">Mode</div><div id="modeMetric" class="value">—</div></div>
      </div>
    </section>

    <section class="span-8" data-view="sensors">
      <div class="section-title">
        <h2>Static Radar Surface</h2>
        <span>energy gates when available, text range fallback otherwise</span>
      </div>
      <canvas id="surfaceCanvas" width="1200" height="600"></canvas>
      <div class="hint">This is not a camera or lidar map. LD2420 energy mode gives distance buckets. Text mode gives presence plus a rough range. The surface view projects either one into a stable cone so it is visually useful.</div>
    </section>

    <section class="span-8" data-view="sensors">
      <div class="section-title">
        <h2>Fused Room View</h2>
        <span>local sensor + peer pose hints when room nodes are active</span>
      </div>
      <canvas id="roomFusionCanvas" width="1200" height="520"></canvas>
      <div class="hint">This is now a floor-plan editor. Pick a node in Settings, then drag its marker to move it or drag the white handle to rotate it. The grid and axis guides are for room layout only; peer headings still originate from manual pose data plus live range fusion.</div>
    </section>

    <section class="span-4" data-view="sensors">
      <div class="section-title">
        <h2>Calibration Enrollment</h2>
        <span>capture one moving target, then refine a peer pose</span>
      </div>

      <div class="field">
        <label for="calibrationTargetSelect">Peer Target</label>
        <select id="calibrationTargetSelect">
          <option value="">Select a room peer</option>
        </select>
      </div>

      <div class="calibration-actions" style="margin-top: 12px;">
        <button id="calibrationStartButton" type="button">Start Capture</button>
        <button id="calibrationStopButton" type="button">Stop</button>
        <button id="calibrationClearButton" type="button">Clear</button>
        <button id="calibrationLoadButton" type="button">Load Suggestion</button>
        <button id="calibrationPublishButton" class="primary" type="button">Publish Suggestion</button>
      </div>

      <div class="hint">Walk or slide one object so both sensors keep seeing a single dominant target. The browser captures paired distance samples and searches for the peer pose correction that minimizes fused-point mismatch.</div>
      <div id="calibrationStatus" class="calibration-status"></div>

      <div class="calibration-summary-grid" style="margin-top: 12px;">
        <div class="metric"><div class="label">Captured Samples</div><div id="calibrationSamplesMetric" class="value">0</div></div>
        <div class="metric"><div class="label">Fit Error</div><div id="calibrationErrorMetric" class="value">—</div></div>
        <div class="metric"><div class="label">Improvement</div><div id="calibrationImprovementMetric" class="value">—</div></div>
        <div class="metric"><div class="label">Suggested Pose</div><div id="calibrationSuggestionMetric" class="value">—</div></div>
      </div>

      <div id="calibrationSampleList" class="calibration-sample-list" style="margin-top: 12px;">
        <div class="hint">No calibration samples captured yet.</div>
      </div>
    </section>

    <section class="span-4" data-view="dashboard sensors">
      <div class="section-title">
        <h2>BLE Sightings</h2>
        <span>nearby beacons and device identities</span>
      </div>
      <div class="metric-grid">
        <div class="metric"><div class="label">Beacon Count</div><div id="bleBeaconCountMetric" class="value">0</div></div>
      </div>
      <div id="bleBeaconList" class="hint">No BLE beacons seen yet.</div>
    </section>

    <section class="span-4" data-view="sensors console">
      <div class="section-title">
        <h2>Latest Radar Frame</h2>
        <span>parsed + raw</span>
      </div>
      <pre id="latestFramePre">No radar frame received yet.</pre>
    </section>

    <section class="span-12" data-view="sensors">
      <div class="section-title">
        <h2>Live Activity Strip</h2>
        <span>updates for raw, text range, and energy frames</span>
      </div>
      <canvas id="activityCanvas" width="1200" height="320"></canvas>
    </section>

    <section class="span-6" data-view="console">
      <div class="section-title">
        <h2>Events</h2>
        <span>latest first</span>
      </div>
      <div id="eventList" class="event-list"></div>
    </section>

    <section class="span-6" data-view="console">
      <div class="section-title">
        <h2>Raw Lines</h2>
        <span>serial NDJSON</span>
      </div>
      <pre id="rawPre"></pre>
    </section>
  </main>

  <script>
    const BAUD_RATE = 115200;
    const MAX_EVENTS = 80;
    const MAX_RAW_LINES = 200;
    const ACTIVITY_HISTORY = 140;
    const GATE_COUNT = 16;
    const GATE_SIZE_CM = 70;
    const DEFAULT_MQTT_HOST = "10.0.107.46";
    const DEFAULT_MQTT_PORT = "1883";
    const DEVICE_SNAPSHOT_ENDPOINT = "./api/snapshot";
    const DEVICE_COMMAND_ENDPOINT = "./api/command";
    const DEVICE_WEBSOCKET_PORT = 81;
    const SNAPSHOT_POLL_MS = 150;
    const SNAPSHOT_POLL_MAX_MS = 4000;
    const RECONNECT_BASE_MS = 500;
    const RECONNECT_MAX_MS = 8000;
    const CONNECTION_LOSS_DEBOUNCE_MS = 1200;
    const MY_HOME_ASSISTANT_INTEGRATIONS_URL = "https://my.home-assistant.io/redirect/integrations";
    const MY_HOME_ASSISTANT_MOSQUITTO_URL = "https://my.home-assistant.io/redirect/supervisor_addon/?addon=core_mosquitto";
    const LOCAL_STORAGE_KEY = "lb-mmwave-ha-setup";
    const LOCAL_STORAGE_UNITS_KEY = "lb-mmwave-units";
    const LOCAL_STORAGE_VIEW_KEY = "lb-mmwave-view";
    const ROOM_EDITOR_LOCAL_TARGET = "__local__";

    let port = null;
    let reader = null;
    let keepReading = false;
    let snapshotRequestInFlight = false;
    let deviceSocket = null;
    let deviceSocketReconnectTimer = null;
    let deviceSocketLive = false;
    let deviceSocketReconnectAttempt = 0;
    let snapshotPollFailureCount = 0;
    let connectionLossTimer = null;

    let rawLineBuffer = "";
    let rawLines = [];
    let events = [];
    let activityFrames = [];

    let latestEnergyFrame = null;
    let latestTextRangeFrame = null;
    let latestGenericFrame = null;
    let lastPresenceValue = undefined;
    let lastMode = "waiting";
    let pendingHaConfigSaved = null;
    let snapshotPollTimer = null;
    let lastSnapshotEnergyFrameId = -1;
    let lastSnapshotTextFrameId = -1;
    let lastSnapshotGenericFrameId = -1;

    let gatePeaks = new Array(GATE_COUNT).fill(1);
    let gateHold = new Array(GATE_COUNT).fill(0);
    let syntheticHold = new Array(GATE_COUNT).fill(0);
    let settingsOpen = true;
    let settingsUserToggled = false;
    let unitSystem = "metric";
    let currentView = "dashboard";
    let lastUiSnapshot = null;
    let lastUiEvent = null;
    let latestRoomPeers = [];
    let latestUdpDiscoveryPeers = [];
    let latestBleBeacons = [];
    let lastRoomEditorTargetOptionsSignature = "";
    let lastWifiNetworkOptionsSignature = "";
    let pendingRawRender = false;
    let pendingEventsRender = false;
    let pendingActivityMapDraw = false;
    let pendingSurfaceDraw = false;
    let pendingRoomFusionDraw = false;
    let roomFusionLayout = null;
    let roomEditorState = { mode: null, nodeId: null };
    let roomPoseSaveTimer = null;
    let pendingRoomEditorAck = null;
    let localRoomConfigDraft = {
      roomId: "room-default",
      sensorRole: "auto",
      poseX: "0",
      poseY: "0",
      heading: "-90",
      roomWidth: "600",
      roomHeight: "400"
    };
    let calibrationSession = {
      active: false,
      targetNodeId: "",
      samples: [],
      suggestion: null,
      lastSampleAt: 0
    };

    const els = {
      connectButton: document.getElementById("connectButton"),
      disconnectButton: document.getElementById("disconnectButton"),
      clearButton: document.getElementById("clearButton"),
      energyButton: document.getElementById("energyButton"),
      statusButton: document.getElementById("statusButton"),
      dashboardViewButton: document.getElementById("dashboardViewButton"),
      setupViewButton: document.getElementById("setupViewButton"),
      sensorsViewButton: document.getElementById("sensorsViewButton"),
      consoleViewButton: document.getElementById("consoleViewButton"),
      dashboardPanel: document.getElementById("dashboardPanel"),
      setupPanel: document.getElementById("setupPanel"),
      sensorsPanel: document.getElementById("sensorsPanel"),
      consolePanel: document.getElementById("consolePanel"),
      dashboardOpenSetupButton: document.getElementById("dashboardOpenSetupButton"),
      dashboardOpenSensorsButton: document.getElementById("dashboardOpenSensorsButton"),
      dashboardOpenConsoleButton: document.getElementById("dashboardOpenConsoleButton"),
      firmwareSyncButton: document.getElementById("firmwareSyncButton"),
      firmwareSyncCandidate: document.getElementById("firmwareSyncCandidate"),
      firmwareSyncStatus: document.getElementById("firmwareSyncStatus"),
      wizardConnectButton: document.getElementById("wizardConnectButton"),
      wizardStatusButton: document.getElementById("wizardStatusButton"),
      wizardScanWifiButton: document.getElementById("wizardScanWifiButton"),
      wizardSaveSetupButton: document.getElementById("wizardSaveSetupButton"),
      wizardOpenHaButton: document.getElementById("wizardOpenHaButton"),
      wizardSaveTuningButton: document.getElementById("wizardSaveTuningButton"),
      wizardOpenSensorsButton: document.getElementById("wizardOpenSensorsButton"),
      wizardOpenConsoleButton: document.getElementById("wizardOpenConsoleButton"),
      copyObservationTopicButton: document.getElementById("copyObservationTopicButton"),
      wizardCopyObservationTopicButton: document.getElementById("wizardCopyObservationTopicButton"),
      observationTopicValue: document.getElementById("observationTopicValue"),
      observationTopicHint: document.getElementById("observationTopicHint"),
      setupObservationTopicValue: document.getElementById("setupObservationTopicValue"),
      wizardDeviceDot: document.getElementById("wizardDeviceDot"),
      wizardDeviceStatus: document.getElementById("wizardDeviceStatus"),
      wizardConfigDot: document.getElementById("wizardConfigDot"),
      wizardConfigStatus: document.getElementById("wizardConfigStatus"),
      wizardRoomDot: document.getElementById("wizardRoomDot"),
      wizardRoomStatus: document.getElementById("wizardRoomStatus"),
      wizardFeedDot: document.getElementById("wizardFeedDot"),
      wizardFeedStatus: document.getElementById("wizardFeedStatus"),
      unitsToggleButton: document.getElementById("unitsToggleButton"),
      settingsToggleButton: document.getElementById("settingsToggleButton"),
      scanWifiButton: document.getElementById("scanWifiButton"),
      saveHaButton: document.getElementById("saveHaButton"),
      saveTuningButton: document.getElementById("saveTuningButton"),
      addToHaButton: document.getElementById("addToHaButton"),
      openMosquittoButton: document.getElementById("openMosquittoButton"),
      sendButton: document.getElementById("sendButton"),
      settingsSection: document.getElementById("settingsSection"),
      commandInput: document.getElementById("commandInput"),
      wifiSelect: document.getElementById("wifiSelect"),
      wifiSsidInput: document.getElementById("wifiSsidInput"),
      wifiPasswordInput: document.getElementById("wifiPasswordInput"),
      mqttHostInput: document.getElementById("mqttHostInput"),
      mqttPortInput: document.getElementById("mqttPortInput"),
      mqttUserInput: document.getElementById("mqttUserInput"),
      mqttPasswordInput: document.getElementById("mqttPasswordInput"),
      nodeIdInput: document.getElementById("nodeIdInput"),
      friendlyNameInput: document.getElementById("friendlyNameInput"),
      roomIdInput: document.getElementById("roomIdInput"),
      roomEditorTargetInput: document.getElementById("roomEditorTargetInput"),
      sensorRoleInput: document.getElementById("sensorRoleInput"),
      poseXInput: document.getElementById("poseXInput"),
      poseYInput: document.getElementById("poseYInput"),
      headingInput: document.getElementById("headingInput"),
      headingValue: document.getElementById("headingValue"),
      roomWidthInput: document.getElementById("roomWidthInput"),
      roomHeightInput: document.getElementById("roomHeightInput"),
      roomSnapEnabledInput: document.getElementById("roomSnapEnabledInput"),
      roomGridSizeInput: document.getElementById("roomGridSizeInput"),
      roomEditorStatus: document.getElementById("roomEditorStatus"),
      maxRangeInput: document.getElementById("maxRangeInput"),
      maxRangeValue: document.getElementById("maxRangeValue"),
      minGateEnergyInput: document.getElementById("minGateEnergyInput"),
      minGateEnergyValue: document.getElementById("minGateEnergyValue"),
      sensitivityInput: document.getElementById("sensitivityInput"),
      sensitivityValue: document.getElementById("sensitivityValue"),
      presenceHoldInput: document.getElementById("presenceHoldInput"),
      presenceHoldValue: document.getElementById("presenceHoldValue"),
      minActiveGatesInput: document.getElementById("minActiveGatesInput"),
      minActiveGatesValue: document.getElementById("minActiveGatesValue"),
      minActivityScoreInput: document.getElementById("minActivityScoreInput"),
      minActivityScoreValue: document.getElementById("minActivityScoreValue"),
      ledEnabledInput: document.getElementById("ledEnabledInput"),
      ledBrightnessInput: document.getElementById("ledBrightnessInput"),
      ledBrightnessValue: document.getElementById("ledBrightnessValue"),
      haSetupStatus: document.getElementById("haSetupStatus"),
      wifiStateMetric: document.getElementById("wifiStateMetric"),
      mqttStateMetric: document.getElementById("mqttStateMetric"),
      ipAddressMetric: document.getElementById("ipAddressMetric"),
      topicPrefixMetric: document.getElementById("topicPrefixMetric"),
      wifiRssiMetric: document.getElementById("wifiRssiMetric"),
      wifiChannelMetric: document.getElementById("wifiChannelMetric"),
      wifiBssidMetric: document.getElementById("wifiBssidMetric"),
      wifiReasonText: document.getElementById("wifiReasonText"),
      connectionDot: document.getElementById("connectionDot"),
      connectionText: document.getElementById("connectionText"),
      firmwareVersionText: document.getElementById("firmwareVersionText"),
      peerVersionPill: document.getElementById("peerVersionPill"),
      peerVersionPillText: document.getElementById("peerVersionPillText"),
      versionBanner: document.getElementById("versionBanner"),
      versionBannerText: document.getElementById("versionBannerText"),
      presenceCard: document.getElementById("presenceCard"),
      presenceText: document.getElementById("presenceText"),
      presenceSubtext: document.getElementById("presenceSubtext"),
      uptimeMetric: document.getElementById("uptimeMetric"),
      heapMetric: document.getElementById("heapMetric"),
      radarBytesMetric: document.getElementById("radarBytesMetric"),
      radarFramesMetric: document.getElementById("radarFramesMetric"),
      rangeMetric: document.getElementById("rangeMetric"),
      modeMetric: document.getElementById("modeMetric"),
      filteredPresenceMetric: document.getElementById("filteredPresenceMetric"),
      gpioPresenceMetric: document.getElementById("gpioPresenceMetric"),
      candidateMetric: document.getElementById("candidateMetric"),
      presenceDecayMetric: document.getElementById("presenceDecayMetric"),
      peopleEstimateMetric: document.getElementById("peopleEstimateMetric"),
      activeGateMetric: document.getElementById("activeGateMetric"),
      activityScoreMetric: document.getElementById("activityScoreMetric"),
      dominantGateMetric: document.getElementById("dominantGateMetric"),
      dominantDistanceMetric: document.getElementById("dominantDistanceMetric"),
      dominantEnergyMetric: document.getElementById("dominantEnergyMetric"),
      totalGateEnergyMetric: document.getElementById("totalGateEnergyMetric"),
      roomPeopleMetric: document.getElementById("roomPeopleMetric"),
      roomActiveNodesMetric: document.getElementById("roomActiveNodesMetric"),
      roomPeerNodesMetric: document.getElementById("roomPeerNodesMetric"),
      roomActivityMetric: document.getElementById("roomActivityMetric"),
      bleBeaconCountMetric: document.getElementById("bleBeaconCountMetric"),
      bleBeaconList: document.getElementById("bleBeaconList"),
      statusLedSwatch: document.getElementById("statusLedSwatch"),
      statusLedText: document.getElementById("statusLedText"),
      activityCanvas: document.getElementById("activityCanvas"),
      surfaceCanvas: document.getElementById("surfaceCanvas"),
      roomFusionCanvas: document.getElementById("roomFusionCanvas"),
      peerLinkSummary: document.getElementById("peerLinkSummary"),
      peerLinkList: document.getElementById("peerLinkList"),
      calibrationTargetSelect: document.getElementById("calibrationTargetSelect"),
      calibrationStartButton: document.getElementById("calibrationStartButton"),
      calibrationStopButton: document.getElementById("calibrationStopButton"),
      calibrationClearButton: document.getElementById("calibrationClearButton"),
      calibrationLoadButton: document.getElementById("calibrationLoadButton"),
      calibrationPublishButton: document.getElementById("calibrationPublishButton"),
      calibrationStatus: document.getElementById("calibrationStatus"),
      calibrationSamplesMetric: document.getElementById("calibrationSamplesMetric"),
      calibrationErrorMetric: document.getElementById("calibrationErrorMetric"),
      calibrationImprovementMetric: document.getElementById("calibrationImprovementMetric"),
      calibrationSuggestionMetric: document.getElementById("calibrationSuggestionMetric"),
      calibrationSampleList: document.getElementById("calibrationSampleList"),
      latestFramePre: document.getElementById("latestFramePre"),
      eventList: document.getElementById("eventList"),
      rawPre: document.getElementById("rawPre")
    };

    function setConnectionState(state, text) {
      els.connectionDot.className = "dot";
      if (state === "connected") els.connectionDot.classList.add("good");
      if (state === "connecting") els.connectionDot.classList.add("warn");
      els.connectionText.textContent = text;
    }

    function setHaSetupStatus(text) {
      els.haSetupStatus.textContent = text;
    }

    function setRoomEditorStatus(text) {
      els.roomEditorStatus.textContent = text || "";
    }

    function setCalibrationStatus(text) {
      els.calibrationStatus.textContent = text || "";
    }

    function updateFirmwareSyncStatus(snapshot = lastUiSnapshot) {
      const sync = snapshot?.firmware_sync || {};
      const highestPeerVersion = String(sync.highest_peer_version || "").trim();
      const highestPeerNodeId = String(sync.highest_peer_node_id || "").trim();
      const localCore = String(sync.local_version_core || "").trim();
      const syncAvailable = !!sync.sync_available;
      const inProgress = !!sync.in_progress;
      const pending = !!sync.pending;

      if (highestPeerVersion && highestPeerNodeId) {
        setTextContentIfChanged(
          els.firmwareSyncCandidate,
          `Highest peer release: ${highestPeerVersion} from ${highestPeerNodeId}${sync.highest_peer_source ? ` via ${sync.highest_peer_source}` : ""}`
        );
      } else if (localCore) {
        setTextContentIfChanged(els.firmwareSyncCandidate, `Local release core: ${localCore}. No peer release candidate is visible.`);
      } else {
        setTextContentIfChanged(els.firmwareSyncCandidate, "Local build is not on a tagged release. No higher peer release visible.");
      }

      els.firmwareSyncButton.disabled = inProgress || pending || !syncAvailable;

      if (inProgress) {
        setTextContentIfChanged(els.firmwareSyncStatus, sync.status || "Downloading firmware update from GitHub...");
        return;
      }

      if (pending) {
        setTextContentIfChanged(els.firmwareSyncStatus, sync.status || "Firmware sync queued.");
        return;
      }

      if (syncAvailable) {
        setTextContentIfChanged(els.firmwareSyncStatus, sync.status || `A newer peer release (${highestPeerVersion}) is available for this node.`);
        return;
      }

      if (sync.last_error) {
        setTextContentIfChanged(els.firmwareSyncStatus, `Last sync result: ${sync.last_error}`);
        return;
      }

      if (sync.last_success) {
        setTextContentIfChanged(els.firmwareSyncStatus, sync.status || "Already aligned with the highest visible peer release.");
        return;
      }

      setTextContentIfChanged(els.firmwareSyncStatus, sync.status || "No higher peer release is available to sync from.");
    }

    function findUdpDiscoveryPeer(nodeId) {
      return latestUdpDiscoveryPeers.find(peer => (peer.node_id || "") === nodeId) || null;
    }

    function normalizeDashboardUrl(hostname, ipAddress) {
      const trimmedHostname = String(hostname || "").trim();
      if (trimmedHostname) {
        const host = trimmedHostname.includes(".") ? trimmedHostname : `${trimmedHostname}.local`;
        return `http://${host}/`;
      }

      const trimmedIp = String(ipAddress || "").trim();
      if (trimmedIp && trimmedIp !== "0.0.0.0") {
        return `http://${trimmedIp}/`;
      }

      return "";
    }

    function hostnameFromNodeId(nodeId) {
      const normalized = String(nodeId || "")
        .trim()
        .toLowerCase()
        .replace(/[^a-z0-9]+/g, "-")
        .replace(/^-+|-+$/g, "");
      return normalized;
    }

    function localFirmwareVersion() {
      return String(lastUiSnapshot?.firmware_version || "").trim();
    }

    function normalizePeerVersion(peer) {
      const version = String(peer?.firmware_version || "").trim();
      return version || "unknown";
    }

    function peerVersionEntries() {
      const entries = new Map();

      latestRoomPeers.forEach(peer => {
        const nodeId = String(peer.node_id || "").trim();
        if (!nodeId) return;
        entries.set(nodeId, {
          node_id: nodeId,
          friendly_name: peer.friendly_name || nodeId,
          firmware_version: String(peer.firmware_version || "").trim(),
          room_id: peer.room_id || lastUiSnapshot?.room_id || "room-default",
          sensor_role: peer.sensor_role || "peer",
          hostname: peer.hostname || hostnameFromNodeId(nodeId),
          ip_address: peer.ip_address || ""
        });
      });

      latestUdpDiscoveryPeers.forEach(peer => {
        const nodeId = String(peer.node_id || "").trim();
        if (!nodeId) return;
        const existing = entries.get(nodeId) || {};
        entries.set(nodeId, {
          ...existing,
          ...peer,
          node_id: nodeId,
          hostname: peer.hostname || existing.hostname || hostnameFromNodeId(nodeId),
          firmware_version: String(peer.firmware_version || existing.firmware_version || "").trim()
        });
      });

      return Array.from(entries.values());
    }

    function versionMismatchPeers() {
      const localVersion = localFirmwareVersion();
      if (!localVersion) return [];

      return peerVersionEntries()
        .filter(peer => peer.node_id !== currentLocalNodeId())
        .map(peer => ({
          nodeId: peer.node_id,
          version: normalizePeerVersion(peer)
        }))
        .filter(peer => peer.version !== localVersion);
    }

    function updateVersionStatus(snapshot = lastUiSnapshot) {
      const version = String(snapshot?.firmware_version || "").trim();
      const buildTarget = String(snapshot?.build_target || "").trim();
      setTextContentIfChanged(
        els.firmwareVersionText,
        version ? `Firmware: ${version}${buildTarget ? ` (${buildTarget})` : ""}` : "Firmware: waiting"
      );

      const mismatches = versionMismatchPeers();
      const hasMismatches = mismatches.length > 0;
      els.peerVersionPill.hidden = !hasMismatches;
      els.versionBanner.classList.toggle("visible", hasMismatches);

      if (!hasMismatches) {
        setTextContentIfChanged(els.peerVersionPillText, "Peer versions aligned");
        setTextContentIfChanged(els.versionBannerText, "All visible peers match this node's firmware version.");
        return;
      }

      const mismatchText = mismatches.map(peer => `${peer.nodeId} on ${peer.version}`).join("; ");
      setTextContentIfChanged(els.peerVersionPillText, `${mismatches.length} peer version mismatch${mismatches.length === 1 ? "" : "es"}`);
      setTextContentIfChanged(
        els.versionBannerText,
        `Local firmware is ${version || "unknown"}. Detected peers on different or unknown versions: ${mismatchText}. Update mixed nodes before trusting cross-node behavior.`
      );
    }

    function peerNavigationEntries() {
      const entries = new Map();

      latestRoomPeers.forEach(peer => {
        const nodeId = String(peer.node_id || "").trim();
        if (!nodeId) return;
        entries.set(nodeId, {
          node_id: nodeId,
          friendly_name: peer.friendly_name || nodeId,
          room_id: peer.room_id || lastUiSnapshot?.room_id || "room-default",
          sensor_role: peer.sensor_role || "peer",
          hostname: hostnameFromNodeId(nodeId),
          ip_address: "",
          age_ms: Number(peer.freshness_ms ?? 0),
          source: "room"
        });
      });

      latestUdpDiscoveryPeers.forEach(peer => {
        const nodeId = String(peer.node_id || "").trim();
        if (!nodeId) return;
        const existing = entries.get(nodeId) || {};
        entries.set(nodeId, {
          ...existing,
          ...peer,
          node_id: nodeId,
          hostname: peer.hostname || existing.hostname || hostnameFromNodeId(nodeId),
          source: "udp"
        });
      });

      return Array.from(entries.values());
    }

    function renderPeerLinks() {
      const peers = peerNavigationEntries();
      setTextContentIfChanged(els.peerLinkSummary, peers.length > 0 ? `${peers.length} peer node${peers.length === 1 ? "" : "s"} on the network` : "no network peers announced yet");

      if (peers.length === 0) {
        els.peerLinkList.innerHTML = "<div class=\"hint\">Nodes discovered on the LAN will appear here with direct dashboard links.</div>";
        return;
      }

      const fragment = document.createDocumentFragment();
      peers.forEach(peer => {
        const card = document.createElement("article");
        card.className = "peer-link-card";

        const top = document.createElement("div");
        top.className = "peer-link-top";

        const name = document.createElement("div");
        name.className = "peer-link-name";
        name.textContent = peer.friendly_name || peer.node_id || "Peer node";

        const meta = document.createElement("div");
        meta.className = "peer-link-meta";
        const peerVersion = normalizePeerVersion(peer);
        const mismatch = !!localFirmwareVersion() && peerVersion !== localFirmwareVersion();
        meta.textContent = `${peer.node_id || "unknown"} • ${peer.room_id || "room-default"} • ${peer.sensor_role || "peer"} • fw ${peerVersion}${mismatch ? " • mismatch" : ""}`;

        top.appendChild(name);
        top.appendChild(meta);

        const details = document.createElement("div");
        details.className = "hint";
  const hostnameLabel = peer.hostname ? `${peer.hostname}.local` : "hostname unavailable";
        const ipLabel = peer.ip_address || "IP unavailable";
  const sourceLabel = peer.source === "udp" ? "network discovery" : "room mesh";
  details.textContent = `${hostnameLabel} • ${ipLabel} • seen ${Math.round(Number(peer.age_ms || 0) / 1000)}s ago • ${sourceLabel}`;

        const actions = document.createElement("div");
        actions.className = "peer-link-actions";

        const url = normalizeDashboardUrl(peer.hostname, peer.ip_address);
        if (url) {
          const openLink = document.createElement("a");
          openLink.href = url;
          openLink.target = "_blank";
          openLink.rel = "noopener noreferrer";
          openLink.textContent = "Open Dashboard";
          actions.appendChild(openLink);
        }

        if (peer.ip_address && peer.ip_address !== "0.0.0.0") {
          const ipLink = document.createElement("a");
          ipLink.href = `http://${peer.ip_address}/`;
          ipLink.target = "_blank";
          ipLink.rel = "noopener noreferrer";
          ipLink.textContent = `Open ${peer.ip_address}`;
          actions.appendChild(ipLink);
        }

        card.appendChild(top);
        card.appendChild(details);
        card.appendChild(actions);
        fragment.appendChild(card);
      });

      els.peerLinkList.innerHTML = "";
      els.peerLinkList.appendChild(fragment);
    }

    function populateCalibrationTargetOptions() {
      const previousValue = els.calibrationTargetSelect.value || calibrationSession.targetNodeId || "";
      const peers = latestRoomPeers
        .filter(peer => (peer.node_id || "").trim().length > 0)
        .map(peer => ({
          value: peer.node_id,
          label: `${peer.node_id} (${peer.sensor_role || "peer"})`
        }));

      els.calibrationTargetSelect.innerHTML = "";
      const placeholder = document.createElement("option");
      placeholder.value = "";
      placeholder.textContent = peers.length > 0 ? "Select a room peer" : "No room peers available";
      els.calibrationTargetSelect.appendChild(placeholder);

      peers.forEach(optionData => {
        const option = document.createElement("option");
        option.value = optionData.value;
        option.textContent = optionData.label;
        els.calibrationTargetSelect.appendChild(option);
      });

      els.calibrationTargetSelect.value = peers.some(option => option.value === previousValue) ? previousValue : "";
      if (calibrationSession.active && calibrationSession.targetNodeId !== els.calibrationTargetSelect.value) {
        calibrationSession.active = false;
        calibrationSession.targetNodeId = els.calibrationTargetSelect.value;
      }
    }

    function calibrationTargetPeer() {
      const nodeId = calibrationSession.active ? calibrationSession.targetNodeId : (els.calibrationTargetSelect.value || "");
      return nodeId ? findRoomPeer(nodeId) : null;
    }

    function projectCalibrationPoint(poseX, poseY, headingDeg, distanceCm) {
      const headingRad = Number(headingDeg || 0) * Math.PI / 180;
      return {
        x: Number(poseX || 0) + Math.cos(headingRad) * Number(distanceCm || 0),
        y: Number(poseY || 0) + Math.sin(headingRad) * Number(distanceCm || 0)
      };
    }

    function evaluateCalibrationFit(samples, localPose, peerPose) {
      let totalError = 0;
      for (const sample of samples) {
        const localPoint = projectCalibrationPoint(localPose.x, localPose.y, localPose.headingDeg, sample.localDistanceCm);
        const peerPoint = projectCalibrationPoint(peerPose.x, peerPose.y, peerPose.headingDeg, sample.peerDistanceCm);
        totalError += Math.hypot(localPoint.x - peerPoint.x, localPoint.y - peerPoint.y);
      }
      return samples.length > 0 ? totalError / samples.length : Number.POSITIVE_INFINITY;
    }

    function computeCalibrationSuggestion(samples, peer) {
      if (!peer || samples.length < 6 || !lastUiSnapshot) {
        return null;
      }

      const localPose = {
        x: Number(lastUiSnapshot.pose_x_cm ?? 0),
        y: Number(lastUiSnapshot.pose_y_cm ?? 0),
        headingDeg: Number(lastUiSnapshot.heading_deg ?? -90)
      };
      const basePose = {
        x: Number(peer.pose_x_cm ?? 0),
        y: Number(peer.pose_y_cm ?? 0),
        headingDeg: Number(peer.heading_deg ?? -90)
      };

      const baselineErrorCm = evaluateCalibrationFit(samples, localPose, basePose);
      let best = {
        x: basePose.x,
        y: basePose.y,
        headingDeg: basePose.headingDeg,
        errorCm: baselineErrorCm
      };

      for (let deltaX = -80; deltaX <= 80; deltaX += 5) {
        for (let deltaY = -80; deltaY <= 80; deltaY += 5) {
          for (let deltaHeading = -45; deltaHeading <= 45; deltaHeading += 5) {
            const candidate = {
              x: basePose.x + deltaX,
              y: basePose.y + deltaY,
              headingDeg: basePose.headingDeg + deltaHeading
            };
            const errorCm = evaluateCalibrationFit(samples, localPose, candidate);
            if (errorCm < best.errorCm) {
              best = { ...candidate, errorCm };
            }
          }
        }
      }

      const coarseBest = { ...best };
      for (let deltaX = -6; deltaX <= 6; deltaX += 1) {
        for (let deltaY = -6; deltaY <= 6; deltaY += 1) {
          for (let deltaHeading = -6; deltaHeading <= 6; deltaHeading += 1) {
            const candidate = {
              x: coarseBest.x + deltaX,
              y: coarseBest.y + deltaY,
              headingDeg: coarseBest.headingDeg + deltaHeading
            };
            const errorCm = evaluateCalibrationFit(samples, localPose, candidate);
            if (errorCm < best.errorCm) {
              best = { ...candidate, errorCm };
            }
          }
        }
      }

      return {
        poseX: Math.round(best.x / 5) * 5,
        poseY: Math.round(best.y / 5) * 5,
        headingDeg: Math.round(best.headingDeg / 5) * 5,
        errorCm: best.errorCm,
        baselineErrorCm,
        improvementCm: Math.max(0, baselineErrorCm - best.errorCm)
      };
    }

    function renderCalibrationState() {
      const sampleCount = calibrationSession.samples.length;
      setTextContentIfChanged(els.calibrationSamplesMetric, String(sampleCount));

      const suggestion = calibrationSession.suggestion;
      setTextContentIfChanged(els.calibrationErrorMetric, suggestion ? `${Math.round(suggestion.errorCm)} cm` : "—");
      setTextContentIfChanged(els.calibrationImprovementMetric, suggestion ? `${Math.round(suggestion.improvementCm)} cm` : "—");
      setTextContentIfChanged(
        els.calibrationSuggestionMetric,
        suggestion ? `${suggestion.poseX}, ${suggestion.poseY} @ ${suggestion.headingDeg} deg` : "—"
      );

      els.calibrationStartButton.disabled = !(els.calibrationTargetSelect.value || calibrationSession.targetNodeId);
      els.calibrationStopButton.disabled = !calibrationSession.active;
      els.calibrationClearButton.disabled = sampleCount === 0 && !calibrationSession.active;
      els.calibrationLoadButton.disabled = !suggestion;
      els.calibrationPublishButton.disabled = !suggestion;

      const peer = calibrationTargetPeer();
      if (!calibrationSession.active && sampleCount === 0 && !suggestion) {
        setCalibrationStatus(peer
          ? `Ready to capture against ${peer.node_id}. Keep one moving target visible to both sensors.`
          : "Choose a room peer to begin calibration enrollment.");
      } else if (calibrationSession.active) {
        setCalibrationStatus(`Capturing samples for ${calibrationSession.targetNodeId}. Samples only count when both nodes report one clear dominant target.`);
      } else if (sampleCount > 0 && !suggestion) {
        setCalibrationStatus(`Captured ${sampleCount} sample${sampleCount === 1 ? "" : "s"} for ${calibrationSession.targetNodeId}. Move the target more and restart capture to refine the fit.`);
      } else if (suggestion) {
        setCalibrationStatus(`Suggested pose for ${calibrationSession.targetNodeId}: x ${suggestion.poseX} cm, y ${suggestion.poseY} cm, heading ${suggestion.headingDeg} deg.`);
      }

      if (sampleCount === 0) {
        els.calibrationSampleList.innerHTML = "<div class=\"hint\">No calibration samples captured yet.</div>";
        return;
      }

      const fragment = document.createDocumentFragment();
      calibrationSession.samples.slice(-6).reverse().forEach((sample, index) => {
        const card = document.createElement("article");
        card.className = "calibration-sample-card";
        const top = document.createElement("div");
        top.className = "calibration-sample-top";

        const title = document.createElement("div");
        title.className = "calibration-sample-title";
        title.textContent = `Sample ${sampleCount - index}`;

        const meta = document.createElement("div");
        meta.className = "calibration-sample-meta";
        meta.textContent = `${Math.round(sample.timestampMs / 100) / 10}s`;

        top.appendChild(title);
        top.appendChild(meta);

        const body = document.createElement("div");
        body.className = "hint";
        body.textContent = `Local ${Math.round(sample.localDistanceCm)} cm • Peer ${Math.round(sample.peerDistanceCm)} cm • freshness ${Math.round(sample.peerFreshnessMs)} ms`;

        card.appendChild(top);
        card.appendChild(body);
        fragment.appendChild(card);
      });

      els.calibrationSampleList.innerHTML = "";
      els.calibrationSampleList.appendChild(fragment);
    }

    function captureCalibrationSample() {
      if (!calibrationSession.active) {
        return;
      }

      const peer = findRoomPeer(calibrationSession.targetNodeId);
      const localDistanceCm = Number(lastUiEvent?.dominant_gate_distance_cm ?? lastUiSnapshot?.dominant_gate_distance_cm ?? -1);
      const peerDistanceCm = Number(peer?.dominant_gate_distance_cm ?? -1);
      const localCandidate = (lastUiEvent?.detection_candidate ?? lastUiSnapshot?.detection_candidate) === true;
      const peerCandidate = peer?.detection_candidate === true;
      const localPeople = Number(lastUiEvent?.people_estimate ?? lastUiSnapshot?.people_estimate ?? 0);
      const peerPeople = Number(peer?.people_estimate ?? 0);
      const peerFreshnessMs = Number(peer?.freshness_ms ?? Number.POSITIVE_INFINITY);
      const nowMs = performance.now();

      if (!peer || !localCandidate || !peerCandidate || localDistanceCm < 0 || peerDistanceCm < 0 || localPeople > 1 || peerPeople > 1 || peerFreshnessMs > 2500) {
        renderCalibrationState();
        return;
      }

      const previousSample = calibrationSession.samples.length > 0 ? calibrationSession.samples[calibrationSession.samples.length - 1] : null;
      const localMoved = !previousSample || Math.abs(localDistanceCm - previousSample.localDistanceCm) >= 8;
      const peerMoved = !previousSample || Math.abs(peerDistanceCm - previousSample.peerDistanceCm) >= 8;
      if (!localMoved && !peerMoved) {
        return;
      }

      if (previousSample && (nowMs - calibrationSession.lastSampleAt) < 220) {
        return;
      }

      calibrationSession.samples.push({
        timestampMs: nowMs,
        localDistanceCm,
        peerDistanceCm,
        peerFreshnessMs
      });
      if (calibrationSession.samples.length > 80) {
        calibrationSession.samples.shift();
      }

      calibrationSession.lastSampleAt = nowMs;
      calibrationSession.suggestion = computeCalibrationSuggestion(calibrationSession.samples, peer);
      renderCalibrationState();
    }

    function startCalibrationCapture() {
      const nodeId = els.calibrationTargetSelect.value;
      if (!nodeId) {
        setCalibrationStatus("Choose a room peer before starting calibration capture.");
        return;
      }

      calibrationSession = {
        active: true,
        targetNodeId: nodeId,
        samples: [],
        suggestion: null,
        lastSampleAt: 0
      };
      renderCalibrationState();
    }

    function stopCalibrationCapture() {
      calibrationSession.active = false;
      renderCalibrationState();
    }

    function clearCalibrationCapture() {
      calibrationSession = {
        active: false,
        targetNodeId: els.calibrationTargetSelect.value || "",
        samples: [],
        suggestion: null,
        lastSampleAt: 0
      };
      renderCalibrationState();
    }

    function loadCalibrationSuggestion() {
      const suggestion = calibrationSession.suggestion;
      if (!suggestion || !calibrationSession.targetNodeId) {
        return;
      }

      els.roomEditorTargetInput.value = calibrationSession.targetNodeId;
      setDisplayedRoomConfig({
        roomId: calibrationTargetPeer()?.room_id || els.roomIdInput.value || "room-default",
        sensorRole: calibrationTargetPeer()?.sensor_role || els.sensorRoleInput.value || "peer",
        poseX: String(suggestion.poseX),
        poseY: String(suggestion.poseY),
        heading: String(suggestion.headingDeg),
        roomWidth: String(calibrationTargetPeer()?.room_width_cm ?? els.roomWidthInput.value ?? 600),
        roomHeight: String(calibrationTargetPeer()?.room_height_cm ?? els.roomHeightInput.value ?? 400)
      });
      persistSetupFormState();
      setRoomEditorStatus(`Loaded calibration suggestion for ${editorTargetLabel(calibrationSession.targetNodeId)}.`);
      setCalibrationStatus(`Loaded suggested pose for ${calibrationSession.targetNodeId} into the room editor.`);
    }

    async function publishCalibrationSuggestion() {
      loadCalibrationSuggestion();
      await saveRoomPoseConfig();
      calibrationSession.active = false;
      renderCalibrationState();
    }

    function shouldPreserveSelectedRoomDraft() {
      return !isEditingLocalNode() && (!!roomEditorState.mode || !!roomPoseSaveTimer || !!pendingRoomEditorAck);
    }

    function formatDistanceCm(cm) {
      const numeric = Number(cm);
      if (!Number.isFinite(numeric) || numeric < 0) return "—";
      if (unitSystem === "imperial") {
        const inches = numeric / 2.54;
        if (inches >= 36) return `${(inches / 12).toFixed(1)} ft`;
        return `${Math.round(inches)} in`;
      }
      return `${numeric} cm`;
    }

    function updateRangeDisplays() {
      els.maxRangeValue.textContent = formatDistanceCm(els.maxRangeInput.value);
      els.minGateEnergyValue.textContent = els.minGateEnergyInput.value;
      els.sensitivityValue.textContent = `${els.sensitivityInput.value}%`;
      els.presenceHoldValue.textContent = `${els.presenceHoldInput.value} ms`;
      els.minActiveGatesValue.textContent = els.minActiveGatesInput.value;
      els.minActivityScoreValue.textContent = els.minActivityScoreInput.value;
      els.ledBrightnessValue.textContent = els.ledBrightnessInput.value;
      els.headingValue.textContent = `${els.headingInput.value} deg`;
    }

    function currentLocalNodeId() {
      return (lastUiSnapshot?.node_id || els.nodeIdInput.value || "local").trim() || "local";
    }

    function getSelectedRoomEditorTarget() {
      return els.roomEditorTargetInput.value || ROOM_EDITOR_LOCAL_TARGET;
    }

    function isEditingLocalNode() {
      const target = getSelectedRoomEditorTarget();
      return target === ROOM_EDITOR_LOCAL_TARGET || target === currentLocalNodeId();
    }

    function roundRoomValue(value, step) {
      const numeric = Number(value);
      const safeStep = Math.max(1, Number(step) || 1);
      if (!Number.isFinite(numeric)) return 0;
      return Math.round(numeric / safeStep) * safeStep;
    }

    function roomEditorStep() {
      return els.roomSnapEnabledInput.checked ? Math.max(5, Number(els.roomGridSizeInput.value) || 25) : 5;
    }

    function readDisplayedRoomConfig() {
      return {
        roomId: els.roomIdInput.value.trim() || "room-default",
        sensorRole: els.sensorRoleInput.value.trim() || "auto",
        poseX: els.poseXInput.value.trim() || "0",
        poseY: els.poseYInput.value.trim() || "0",
        heading: els.headingInput.value.trim() || "-90",
        roomWidth: els.roomWidthInput.value.trim() || "600",
        roomHeight: els.roomHeightInput.value.trim() || "400"
      };
    }

    function getLocalRoomConfig() {
      return {
        roomId: localRoomConfigDraft.roomId || lastUiSnapshot?.room_id || "room-default",
        sensorRole: localRoomConfigDraft.sensorRole || lastUiSnapshot?.sensor_role || "auto",
        poseX: localRoomConfigDraft.poseX ?? String(lastUiSnapshot?.pose_x_cm ?? 0),
        poseY: localRoomConfigDraft.poseY ?? String(lastUiSnapshot?.pose_y_cm ?? 0),
        heading: localRoomConfigDraft.heading ?? String(lastUiSnapshot?.heading_deg ?? -90),
        roomWidth: localRoomConfigDraft.roomWidth ?? String(lastUiSnapshot?.room_width_cm ?? 600),
        roomHeight: localRoomConfigDraft.roomHeight ?? String(lastUiSnapshot?.room_height_cm ?? 400)
      };
    }

    function syncLocalRoomDraftFromInputs() {
      if (!isEditingLocalNode()) return;
      localRoomConfigDraft = { ...readDisplayedRoomConfig() };
    }

    function setDisplayedRoomConfig(config) {
      if (!config) return;
      els.roomIdInput.value = config.roomId ?? "room-default";
      els.sensorRoleInput.value = config.sensorRole ?? "auto";
      els.poseXInput.value = String(config.poseX ?? 0);
      els.poseYInput.value = String(config.poseY ?? 0);
      els.headingInput.value = String(config.heading ?? -90);
      els.roomWidthInput.value = String(config.roomWidth ?? 600);
      els.roomHeightInput.value = String(config.roomHeight ?? 400);
      updateRangeDisplays();
      drawRoomFusionView();
    }

    function roomConfigMatches(config, candidate) {
      if (!config || !candidate) return false;
      return String(config.roomId || "room-default") === String(candidate.room_id ?? candidate.roomId ?? "room-default") &&
        String(config.sensorRole || "auto") === String(candidate.sensor_role ?? candidate.sensorRole ?? "auto") &&
        Number(config.poseX || 0) === Number(candidate.pose_x_cm ?? candidate.poseX ?? 0) &&
        Number(config.poseY || 0) === Number(candidate.pose_y_cm ?? candidate.poseY ?? 0) &&
        Number(config.heading || -90) === Number(candidate.heading_deg ?? candidate.headingDeg ?? -90) &&
        Number(config.roomWidth || 600) === Number(candidate.room_width_cm ?? candidate.roomWidth ?? 600) &&
        Number(config.roomHeight || 400) === Number(candidate.room_height_cm ?? candidate.roomHeight ?? 400);
    }

    function resolvePendingRoomEditorAck() {
      if (!pendingRoomEditorAck) return;
      if (pendingRoomEditorAck.local) {
        if (roomConfigMatches(pendingRoomEditorAck.config, lastUiSnapshot || {})) {
          setRoomEditorStatus(`Saved and applied on ${editorTargetLabel(pendingRoomEditorAck.targetNodeId)}.`);
          pendingRoomEditorAck = null;
        }
        return;
      }

      const peer = findRoomPeer(pendingRoomEditorAck.targetNodeId);
      if (peer && roomConfigMatches(pendingRoomEditorAck.config, peer)) {
        setRoomEditorStatus(`Published and applied on ${editorTargetLabel(pendingRoomEditorAck.targetNodeId)}.`);
        pendingRoomEditorAck = null;
      }
    }

    function findRoomPeer(nodeId) {
      return latestRoomPeers.find(peer => (peer.node_id || "") === nodeId) || null;
    }

    function editorTargetLabel(nodeId) {
      if (!nodeId || nodeId === ROOM_EDITOR_LOCAL_TARGET || nodeId === currentLocalNodeId()) {
        return `Local node (${currentLocalNodeId()})`;
      }
      return nodeId;
    }

    function setTextContentIfChanged(element, value) {
      const nextValue = String(value ?? "");
      if (element.textContent !== nextValue) {
        element.textContent = nextValue;
      }
    }

    function setInputValueIfChanged(element, value) {
      const nextValue = String(value ?? "");
      if (element.value !== nextValue) {
        element.value = nextValue;
      }
    }

    function setCheckedIfChanged(element, checked) {
      const nextChecked = Boolean(checked);
      if (element.checked !== nextChecked) {
        element.checked = nextChecked;
      }
    }

    function scheduleRawRender() {
      if (pendingRawRender) return;
      pendingRawRender = true;
      window.requestAnimationFrame(() => {
        pendingRawRender = false;
        setTextContentIfChanged(els.rawPre, rawLines.join("\n"));
        els.rawPre.scrollTop = els.rawPre.scrollHeight;
      });
    }

    function populateRoomEditorTargetOptions() {
      const previousValue = els.roomEditorTargetInput.value || ROOM_EDITOR_LOCAL_TARGET;
      const options = [{ value: ROOM_EDITOR_LOCAL_TARGET, label: `Local node (${currentLocalNodeId()})` }];
      latestRoomPeers.forEach(peer => {
        const nodeId = (peer.node_id || "").trim();
        if (!nodeId) return;
        options.push({
          value: nodeId,
          label: `${nodeId} (${peer.sensor_role || "peer"})`
        });
      });

      const signature = options.map(option => `${option.value}:${option.label}`).join("|");
      if (signature === lastRoomEditorTargetOptionsSignature) {
        const selectedValue = options.some(option => option.value === previousValue) ? previousValue : ROOM_EDITOR_LOCAL_TARGET;
        if (els.roomEditorTargetInput.value !== selectedValue) {
          els.roomEditorTargetInput.value = selectedValue;
        }
        return;
      }

      lastRoomEditorTargetOptionsSignature = signature;

      els.roomEditorTargetInput.innerHTML = "";
      options.forEach(optionData => {
        const option = document.createElement("option");
        option.value = optionData.value;
        option.textContent = optionData.label;
        els.roomEditorTargetInput.appendChild(option);
      });

      const selectedValue = options.some(option => option.value === previousValue) ? previousValue : ROOM_EDITOR_LOCAL_TARGET;
      els.roomEditorTargetInput.value = selectedValue;
    }

    function applySelectedRoomEditorTarget() {
      const target = getSelectedRoomEditorTarget();
      if (target === ROOM_EDITOR_LOCAL_TARGET || target === currentLocalNodeId()) {
        setDisplayedRoomConfig(getLocalRoomConfig());
        return;
      }

      const peer = findRoomPeer(target);
      if (!peer) {
        els.roomEditorTargetInput.value = ROOM_EDITOR_LOCAL_TARGET;
        setDisplayedRoomConfig(getLocalRoomConfig());
        return;
      }

      setDisplayedRoomConfig({
        roomId: peer.room_id || els.roomIdInput.value || "room-default",
        sensorRole: peer.sensor_role || "peer",
        poseX: String(peer.pose_x_cm ?? 0),
        poseY: String(peer.pose_y_cm ?? 0),
        heading: String(peer.heading_deg ?? -90),
        roomWidth: String(peer.room_width_cm ?? 600),
        roomHeight: String(peer.room_height_cm ?? 400)
      });
    }

    function applyRoomEditorConfigToPeerCache(targetNodeId, config) {
      latestRoomPeers = latestRoomPeers.map(peer => {
        if ((peer.node_id || "") !== targetNodeId) return peer;
        return {
          ...peer,
          room_id: config.roomId,
          sensor_role: config.sensorRole,
          pose_x_cm: Number(config.poseX),
          pose_y_cm: Number(config.poseY),
          heading_deg: Number(config.heading),
          room_width_cm: Number(config.roomWidth),
          room_height_cm: Number(config.roomHeight)
        };
      });
    }

    function setUnitSystem(nextUnitSystem, persist = true) {
      unitSystem = nextUnitSystem === "imperial" ? "imperial" : "metric";
      els.unitsToggleButton.textContent = unitSystem === "imperial" ? "Units: Imperial" : "Units: Metric";
      updateRangeDisplays();

      if (lastUiEvent) {
        updateMetrics(lastUiEvent);
        updatePresence(lastPresenceValue, lastUiEvent);
      } else if (lastUiSnapshot) {
        updateMetrics(lastUiSnapshot);
        if (typeof lastUiSnapshot.presence === "boolean") {
          updatePresence(lastUiSnapshot.presence, { event: "heartbeat", ...lastUiSnapshot });
        }
      }

      drawRoomFusionView();

      if (persist) {
        try {
          window.localStorage.setItem(LOCAL_STORAGE_UNITS_KEY, unitSystem);
        } catch (error) {
          console.warn("Failed to persist unit system", error);
        }
      }
    }

    function setSettingsOpen(open) {
      settingsOpen = !!open;
      els.settingsSection.classList.toggle("settings-hidden", !settingsOpen);
      els.settingsToggleButton.setAttribute("aria-pressed", settingsOpen ? "true" : "false");
    }

    function normalizeView(nextView) {
      return ["dashboard", "setup", "sensors", "console"].includes(nextView) ? nextView : "dashboard";
    }

    function setMiniStatus(element, dot, text, state) {
      if (element) element.textContent = text;
      if (!dot) return;
      dot.classList.remove("good", "bad");
      if (state === "good") dot.classList.add("good");
      if (state === "bad") dot.classList.add("bad");
    }

    function observationTopicFor(event = lastUiEvent || lastUiSnapshot) {
      if (typeof event?.topic_prefix === "string" && event.topic_prefix) return `${event.topic_prefix}/observations`;
      if (typeof event?.node_id === "string" && event.node_id) return `lb_mmwave/${event.node_id}/observations`;
      return "—";
    }

    function updateObservationTopic(event = lastUiEvent || lastUiSnapshot) {
      const topic = observationTopicFor(event);
      setTextContentIfChanged(els.observationTopicValue, topic);
      setTextContentIfChanged(els.setupObservationTopicValue, topic);
      const mqttConnected = event?.mqtt_connected === true;
      setMiniStatus(
        els.wizardFeedStatus,
        els.wizardFeedDot,
        mqttConnected ? "Observation feed ready" : "Observation feed waiting on MQTT",
        mqttConnected ? "good" : "bad"
      );
      setTextContentIfChanged(
        els.observationTopicHint,
        mqttConnected
          ? "MQTT is connected. Subscribe here to capture temporal node observations without adding more entities."
          : "The topic path is ready. The node will publish once MQTT is connected."
      );
    }

    function updateWizardState(event = lastUiEvent || lastUiSnapshot) {
      const hasTelemetry = !!event;
      const deviceText = port
        ? "Serial attached"
        : hasTelemetry
          ? "Device UI connected"
          : "Waiting for device telemetry";
      setMiniStatus(els.wizardDeviceStatus, els.wizardDeviceDot, deviceText, hasTelemetry || port ? "good" : "bad");

      const configured = event?.configured === true;
      const mqttConnected = event?.mqtt_connected === true;
      const configText = configured
        ? (mqttConnected ? "Provisioned and on MQTT" : "Provisioned, waiting on MQTT")
        : "Needs Wi-Fi + MQTT setup";
      setMiniStatus(els.wizardConfigStatus, els.wizardConfigDot, configText, configured ? "good" : "bad");

      const roomText = typeof event?.room_id === "string" && event.room_id
        ? `${event.room_id} • ${event.sensor_role || "auto"}`
        : "Room layout needs review";
      const roomReady = typeof event?.room_id === "string" && event.room_id && typeof event?.sensor_role === "string";
      setMiniStatus(els.wizardRoomStatus, els.wizardRoomDot, roomText, roomReady ? "good" : null);

      updateObservationTopic(event);
    }

    function setCurrentView(nextView, persist = true) {
      currentView = normalizeView(nextView);

      const panels = {
        dashboard: els.dashboardPanel,
        setup: els.setupPanel,
        sensors: els.sensorsPanel,
        console: els.consolePanel
      };
      const buttons = {
        dashboard: els.dashboardViewButton,
        setup: els.setupViewButton,
        sensors: els.sensorsViewButton,
        console: els.consoleViewButton
      };

      Object.entries(panels).forEach(([key, panel]) => panel?.classList.toggle("active", key === currentView));
      Object.entries(buttons).forEach(([key, button]) => {
        button?.classList.toggle("active", key === currentView);
        button?.setAttribute("aria-selected", key === currentView ? "true" : "false");
      });

      document.querySelectorAll("section[data-view]").forEach(section => {
        const views = (section.dataset.view || "").split(/\s+/).filter(Boolean);
        section.classList.toggle("view-hidden", views.length > 0 && !views.includes(currentView));
      });

      if (currentView === "setup") {
        setSettingsOpen(true);
      }

      if (persist) {
        try {
          window.localStorage.setItem(LOCAL_STORAGE_VIEW_KEY, currentView);
        } catch (error) {
          console.warn("Failed to persist workspace view", error);
        }
      }
    }

    async function copyObservationTopic() {
      const topic = observationTopicFor();
      if (!topic || topic === "—") {
        setHaSetupStatus("Observation topic is not available yet. Refresh status after the node reports its topic prefix.");
        return;
      }

      try {
        await navigator.clipboard.writeText(topic);
        setHaSetupStatus(`Copied observation topic: ${topic}`);
      } catch (error) {
        setHaSetupStatus(`Observation topic: ${topic}`);
      }
    }

    function formatBoolean(value) {
      if (value === true) return "YES";
      if (value === false) return "NO";
      return "—";
    }

    function supportsNetworkTransport() {
      return window.location.protocol === "http:" || window.location.protocol === "https:";
    }

    function supportsDeviceWebSocket() {
      return supportsNetworkTransport();
    }

    function deviceWebSocketUrl() {
      const protocol = window.location.protocol === "https:" ? "wss" : "ws";
      return `${protocol}://${window.location.hostname}:${DEVICE_WEBSOCKET_PORT}/`;
    }

    function resetSnapshotTracking() {
      lastSnapshotEnergyFrameId = -1;
      lastSnapshotTextFrameId = -1;
      lastSnapshotGenericFrameId = -1;
    }

    function nextRetryDelay(baseMs, maxMs, attempt) {
      const exponent = Math.max(0, attempt - 1);
      return Math.min(maxMs, baseMs * (2 ** exponent));
    }

    function clearConnectionLossTimer() {
      if (!connectionLossTimer) return;
      window.clearTimeout(connectionLossTimer);
      connectionLossTimer = null;
    }

    function scheduleConnectionLossState(text) {
      if (port || connectionLossTimer) return;

      connectionLossTimer = window.setTimeout(() => {
        connectionLossTimer = null;
        if (!port && !deviceSocketLive) {
          setConnectionState("disconnected", text);
        }
      }, CONNECTION_LOSS_DEBOUNCE_MS);
    }

    function markNetworkTransportHealthy() {
      clearConnectionLossTimer();
      deviceSocketReconnectAttempt = 0;
      snapshotPollFailureCount = 0;
    }

    function clearDeviceSocketReconnect() {
      if (!deviceSocketReconnectTimer) return;
      window.clearTimeout(deviceSocketReconnectTimer);
      deviceSocketReconnectTimer = null;
    }

    function scheduleDeviceSocketReconnect() {
      if (!supportsDeviceWebSocket() || port || deviceSocketReconnectTimer) return;

      deviceSocketReconnectAttempt += 1;
      const reconnectDelayMs = nextRetryDelay(RECONNECT_BASE_MS, RECONNECT_MAX_MS, deviceSocketReconnectAttempt);
      setConnectionState("connecting", `Reconnecting to device in ${Math.round(reconnectDelayMs / 100) / 10}s`);

      deviceSocketReconnectTimer = window.setTimeout(() => {
        deviceSocketReconnectTimer = null;
        connectDeviceSocket();
      }, reconnectDelayMs);
    }

    function disconnectDeviceSocket() {
      clearDeviceSocketReconnect();
      if (!deviceSocket) return;

      const socket = deviceSocket;
      deviceSocket = null;
      deviceSocketLive = false;
      socket.onopen = null;
      socket.onmessage = null;
      socket.onerror = null;
      socket.onclose = null;

      if (socket.readyState === WebSocket.OPEN || socket.readyState === WebSocket.CONNECTING) {
        socket.close();
      }
    }

    function connectDeviceSocket() {
      if (!supportsDeviceWebSocket() || port || (deviceSocket && (deviceSocket.readyState === WebSocket.OPEN || deviceSocket.readyState === WebSocket.CONNECTING))) {
        return;
      }

      try {
        const socket = new WebSocket(deviceWebSocketUrl());
        deviceSocket = socket;

        socket.onopen = () => {
          deviceSocketLive = true;
          markNetworkTransportHealthy();
          clearDeviceSocketReconnect();
          stopSnapshotPolling();
          setConnectionState("connected", `Live WebSocket @ ${window.location.host}`);
        };

        socket.onmessage = message => {
          try {
            applyDeviceSnapshot(JSON.parse(message.data));
          } catch (error) {
            addEvent({ event: "ws_parse_error", ms: performance.now(), error: String(error) });
          }
        };

        socket.onerror = () => {
          if (!port) {
            scheduleConnectionLossState(`Connection lost @ ${window.location.host}`);
          }
        };

        socket.onclose = () => {
          if (deviceSocket === socket) {
            deviceSocket = null;
          }
          deviceSocketLive = false;

          if (!port) {
            scheduleConnectionLossState(`Connection lost @ ${window.location.host}`);
            startSnapshotPolling();
            scheduleDeviceSocketReconnect();
          }
        };
      } catch (error) {
        addEvent({ event: "ws_connect_error", ms: performance.now(), error: String(error) });
        scheduleConnectionLossState(`Connection lost @ ${window.location.host}`);
        startSnapshotPolling();
        scheduleDeviceSocketReconnect();
      }
    }

    function describeMqttState(state) {
      switch (state) {
        case -4: return "Connection timeout";
        case -3: return "Connection lost";
        case -2: return "Connect failed";
        case -1: return "Disconnected";
        case 0: return "Connected";
        case 1: return "Bad protocol";
        case 2: return "Bad client ID";
        case 3: return "Broker unavailable";
        case 4: return "Bad credentials";
        case 5: return "Not authorized";
        default: return "Unknown";
      }
    }

    function shouldUseWebSockets() {
      const rawHost = (els.mqttHostInput.value || "").trim();
      return /^wss?:\/\//i.test(rawHost) || /^https?:\/\//i.test(rawHost);
    }

    function applyDeviceSnapshot(snapshot) {
      if (!snapshot || typeof snapshot !== "object") return;

      lastUiSnapshot = snapshot;
      latestRoomPeers = Array.isArray(snapshot.room_peers) ? snapshot.room_peers : [];
      latestUdpDiscoveryPeers = Array.isArray(snapshot.udp_discovery?.peers) ? snapshot.udp_discovery.peers : [];
      latestBleBeacons = Array.isArray(snapshot.ble_beacons) ? snapshot.ble_beacons : [];
      populateRoomEditorTargetOptions();
      populateCalibrationTargetOptions();
      renderPeerLinks();
      updateVersionStatus(snapshot);
      updateFirmwareSyncStatus(snapshot);

      applyHaConfig(snapshot);
      if (!shouldPreserveSelectedRoomDraft()) {
        applySelectedRoomEditorTarget();
      }
      resolvePendingRoomEditorAck();
      if (!shouldPreserveSelectedRoomDraft()) {
        applySelectedRoomEditorTarget();
      }
      updateConnectionDiagnostics(snapshot);
      updateMetrics(snapshot);

      if (typeof snapshot.presence === "boolean") {
        updatePresence(snapshot.presence, { event: "heartbeat", ...snapshot });
      }

      appendNetworkEvent({
        event: "heartbeat",
        ms: snapshot.uptime_ms ?? performance.now(),
        ...snapshot,
        latest_energy_frame: undefined,
        latest_text_frame: undefined,
        latest_generic_frame: undefined
      });

      if (snapshot.latest_energy_frame && snapshot.latest_energy_frame.frames_total !== lastSnapshotEnergyFrameId) {
        lastSnapshotEnergyFrameId = snapshot.latest_energy_frame.frames_total;
        appendNetworkEvent({ event: "ld2420_energy", ...snapshot.latest_energy_frame });
      }

      if (snapshot.latest_text_frame && snapshot.latest_text_frame.frames_total !== lastSnapshotTextFrameId) {
        lastSnapshotTextFrameId = snapshot.latest_text_frame.frames_total;
        appendNetworkEvent({ event: "ld2420_text_range", ...snapshot.latest_text_frame });
      }

      if (snapshot.latest_generic_frame && snapshot.latest_generic_frame.frames_total !== lastSnapshotGenericFrameId) {
        lastSnapshotGenericFrameId = snapshot.latest_generic_frame.frames_total;
        appendNetworkEvent({ event: "radar_uart_frame", ...snapshot.latest_generic_frame });
      }

      if (!port) {
        const hostLabel = snapshot.device_hostname ? `${snapshot.device_hostname}.local` : window.location.host;
        setConnectionState("connected", `${deviceSocketLive ? "Live WebSocket" : "Device HTTP"} @ ${hostLabel}`);
      }

      captureCalibrationSample();
    }

    async function fetchDeviceSnapshot() {
      if (!supportsNetworkTransport()) return;

      const params = new URLSearchParams({
        energy_since: String(lastSnapshotEnergyFrameId),
        text_since: String(lastSnapshotTextFrameId),
        generic_since: String(lastSnapshotGenericFrameId)
      });

      const response = await fetch(`${DEVICE_SNAPSHOT_ENDPOINT}?${params.toString()}`, { cache: "no-store" });
      if (!response.ok) {
        throw new Error(`Snapshot request failed (${response.status})`);
      }

      markNetworkTransportHealthy();
      applyDeviceSnapshot(await response.json());
    }

    async function pollDeviceSnapshot() {
      if (!supportsNetworkTransport() || port || snapshotRequestInFlight) return;

      snapshotRequestInFlight = true;
      let nextDelayMs = SNAPSHOT_POLL_MS;
      try {
        await fetchDeviceSnapshot();
        snapshotPollFailureCount = 0;
        nextDelayMs = SNAPSHOT_POLL_MS;
      } catch (error) {
        snapshotPollFailureCount += 1;
        nextDelayMs = nextRetryDelay(SNAPSHOT_POLL_MS, SNAPSHOT_POLL_MAX_MS, snapshotPollFailureCount + 1);

        if (!deviceSocketLive) {
          scheduleConnectionLossState("HTTP snapshot temporarily unavailable");
          setConnectionState("connecting", `Retrying device HTTP in ${Math.round(nextDelayMs / 100) / 10}s`);
        }

        addEvent({ event: "http_error", ms: performance.now(), error: String(error), retry_in_ms: nextDelayMs });
      } finally {
        snapshotRequestInFlight = false;
      }

      if (!snapshotPollTimer || port) return;
      snapshotPollTimer = window.setTimeout(() => {
        pollDeviceSnapshot();
      }, nextDelayMs);
    }

    function startSnapshotPolling() {
      if (!supportsNetworkTransport() || snapshotPollTimer || port || (deviceSocket && deviceSocket.readyState === WebSocket.OPEN)) return;

      snapshotPollTimer = window.setTimeout(() => {}, SNAPSHOT_POLL_MS);
      pollDeviceSnapshot();
    }

    function stopSnapshotPolling() {
      if (!snapshotPollTimer) return;
      window.clearTimeout(snapshotPollTimer);
      snapshotPollTimer = null;
      snapshotRequestInFlight = false;
      snapshotPollFailureCount = 0;
    }

    function updateConnectionDiagnostics(event) {
      if (typeof event.wifi_connected === "boolean") {
        els.wifiStateMetric.textContent = event.wifi_connected ? "Connected" : "Disconnected";
      }

      const wifiLink = event && typeof event.wifi_link === "object" ? event.wifi_link : null;
      if (typeof (wifiLink?.rssi_dbm) === "number") {
        setTextContentIfChanged(els.wifiRssiMetric, `${wifiLink.rssi_dbm} dBm`);
      }
      if (typeof (wifiLink?.channel) === "number" && wifiLink.channel > 0) {
        setTextContentIfChanged(els.wifiChannelMetric, String(wifiLink.channel));
      }
      if (typeof (wifiLink?.bssid) === "string" && wifiLink.bssid) {
        setTextContentIfChanged(els.wifiBssidMetric, wifiLink.bssid);
      }

      if (typeof event.mqtt_connected === "boolean") {
        if (typeof event.mqtt_state === "number") {
          els.mqttStateMetric.textContent = `${describeMqttState(event.mqtt_state)} (${event.mqtt_state})`;
        } else {
          els.mqttStateMetric.textContent = event.mqtt_connected ? "Connected" : "Disconnected";
        }
      }

      if (typeof event.ip_address === "string" && event.ip_address) {
        els.ipAddressMetric.textContent = event.ip_address;
      }

      if (typeof event.topic_prefix === "string" && event.topic_prefix) {
        els.topicPrefixMetric.textContent = event.topic_prefix;
      }

      if (typeof event.wifi_disconnect_reason_text === "string") {
        const reasonCode = typeof event.wifi_disconnect_reason === "number" ? ` (${event.wifi_disconnect_reason})` : "";
        const extras = [];
        if (typeof event.device_hostname === "string" && event.device_hostname) {
          extras.push(`hostname: ${event.device_hostname}.local`);
        }
        if (typeof event.ap_ssid === "string" && event.ap_ssid) {
          extras.push(`AP: ${event.ap_ssid}`);
        }
        if (typeof event.ap_ip === "string" && event.ap_ip) {
          extras.push(`AP IP: ${event.ap_ip}`);
        }
        els.wifiReasonText.textContent = `Wi-Fi disconnect reason: ${event.wifi_disconnect_reason_text}${reasonCode}${extras.length ? ` • ${extras.join(" • ")}` : ""}`;
      }

      if (typeof event.mqtt_state === "number" && event.mqtt_state === 5) {
        setHaSetupStatus("MQTT broker rejected the connection as not authorized. Enter MQTT Username and MQTT Password, then save again.");
      } else if (typeof event.mqtt_state === "number" && event.mqtt_state === -2) {
        setHaSetupStatus("MQTT connect failed. Use the direct broker endpoint 10.0.107.46:1883 for this Home Assistant setup.");
      }
    }

    function normalizeHost(value) {
      const trimmed = (value || "").trim();
      if (!trimmed) return "";

      let normalized = trimmed.replace(/^mqtts?:\/\//i, "").replace(/^https?:\/\//i, "");
      normalized = normalized.replace(/\/.*$/, "");
      return normalized;
    }

    function inferMqttHost() {
      const explicit = normalizeHost(els.mqttHostInput.value);
      if (explicit) return explicit;

      const currentHost = normalizeHost(window.location.hostname);
      if (currentHost && currentHost !== "localhost" && currentHost !== "127.0.0.1") {
        return currentHost;
      }

      return DEFAULT_MQTT_HOST;
    }

    function readSetupFormState() {
      const localRoomState = getLocalRoomConfig();
      return {
        wifiSsid: els.wifiSsidInput.value,
        mqttHost: inferMqttHost(),
        mqttPort: els.mqttPortInput.value,
        mqttUser: els.mqttUserInput.value,
        mqttPassword: els.mqttPasswordInput.value,
        nodeId: els.nodeIdInput.value,
        friendlyName: els.friendlyNameInput.value,
        roomId: localRoomState.roomId,
        sensorRole: localRoomState.sensorRole,
        poseX: localRoomState.poseX,
        poseY: localRoomState.poseY,
        heading: localRoomState.heading,
        roomWidth: localRoomState.roomWidth,
        roomHeight: localRoomState.roomHeight,
        roomEditorTarget: els.roomEditorTargetInput.value,
        roomSnapEnabled: els.roomSnapEnabledInput.checked,
        roomGridSize: els.roomGridSizeInput.value,
        maxRange: els.maxRangeInput.value,
        minGateEnergy: els.minGateEnergyInput.value,
        sensitivity: els.sensitivityInput.value,
        presenceHold: els.presenceHoldInput.value,
        minActiveGates: els.minActiveGatesInput.value,
        minActivityScore: els.minActivityScoreInput.value,
        ledEnabled: els.ledEnabledInput.checked,
        ledBrightness: els.ledBrightnessInput.value
      };
    }

    function persistSetupFormState() {
      try {
        window.localStorage.setItem(LOCAL_STORAGE_KEY, JSON.stringify(readSetupFormState()));
      } catch (error) {
        console.warn("Failed to persist setup state", error);
      }
    }

    function restoreSetupFormState() {
      try {
        const raw = window.localStorage.getItem(LOCAL_STORAGE_KEY);
        if (!raw) return;

        const saved = JSON.parse(raw);
        if (typeof saved.wifiSsid === "string" && !els.wifiSsidInput.value) els.wifiSsidInput.value = saved.wifiSsid;
        if (typeof saved.mqttHost === "string" && !els.mqttHostInput.value) els.mqttHostInput.value = saved.mqttHost;
        if (typeof saved.mqttPort === "string" && !els.mqttPortInput.value) els.mqttPortInput.value = saved.mqttPort;
        if (typeof saved.mqttUser === "string") els.mqttUserInput.value = saved.mqttUser;
        if (typeof saved.mqttPassword === "string") els.mqttPasswordInput.value = saved.mqttPassword;
        if (typeof saved.nodeId === "string" && !els.nodeIdInput.value) els.nodeIdInput.value = saved.nodeId;
        if (typeof saved.friendlyName === "string" && !els.friendlyNameInput.value) els.friendlyNameInput.value = saved.friendlyName;
        if (typeof saved.roomId === "string") els.roomIdInput.value = saved.roomId;
        if (typeof saved.sensorRole === "string") els.sensorRoleInput.value = saved.sensorRole;
        if (typeof saved.poseX === "string") els.poseXInput.value = saved.poseX;
        if (typeof saved.poseY === "string") els.poseYInput.value = saved.poseY;
        if (typeof saved.heading === "string") els.headingInput.value = saved.heading;
        if (typeof saved.roomWidth === "string") els.roomWidthInput.value = saved.roomWidth;
        if (typeof saved.roomHeight === "string") els.roomHeightInput.value = saved.roomHeight;
        if (typeof saved.roomEditorTarget === "string") els.roomEditorTargetInput.value = saved.roomEditorTarget;
        if (typeof saved.roomSnapEnabled === "boolean") els.roomSnapEnabledInput.checked = saved.roomSnapEnabled;
        if (typeof saved.roomGridSize === "string") els.roomGridSizeInput.value = saved.roomGridSize;
        if (typeof saved.maxRange === "string") els.maxRangeInput.value = saved.maxRange;
        if (typeof saved.minGateEnergy === "string") els.minGateEnergyInput.value = saved.minGateEnergy;
        if (typeof saved.sensitivity === "string") els.sensitivityInput.value = saved.sensitivity;
        if (typeof saved.presenceHold === "string") els.presenceHoldInput.value = saved.presenceHold;
        if (typeof saved.minActiveGates === "string") els.minActiveGatesInput.value = saved.minActiveGates;
        if (typeof saved.minActivityScore === "string") els.minActivityScoreInput.value = saved.minActivityScore;
        if (typeof saved.ledEnabled === "boolean") els.ledEnabledInput.checked = saved.ledEnabled;
        if (typeof saved.ledBrightness === "string") els.ledBrightnessInput.value = saved.ledBrightness;
        localRoomConfigDraft = {
          roomId: typeof saved.roomId === "string" ? saved.roomId : els.roomIdInput.value,
          sensorRole: typeof saved.sensorRole === "string" ? saved.sensorRole : els.sensorRoleInput.value,
          poseX: typeof saved.poseX === "string" ? saved.poseX : els.poseXInput.value,
          poseY: typeof saved.poseY === "string" ? saved.poseY : els.poseYInput.value,
          heading: typeof saved.heading === "string" ? saved.heading : els.headingInput.value,
          roomWidth: typeof saved.roomWidth === "string" ? saved.roomWidth : els.roomWidthInput.value,
          roomHeight: typeof saved.roomHeight === "string" ? saved.roomHeight : els.roomHeightInput.value
        };
      } catch (error) {
        console.warn("Failed to restore setup state", error);
      }

      updateRangeDisplays();
    }

    function formatDuration(ms) {
      if (!Number.isFinite(ms)) return "—";
      const totalSeconds = Math.floor(ms / 1000);
      const hours = Math.floor(totalSeconds / 3600);
      const minutes = Math.floor((totalSeconds % 3600) / 60);
      const seconds = totalSeconds % 60;
      if (hours > 0) return `${hours}h ${minutes}m ${seconds}s`;
      if (minutes > 0) return `${minutes}m ${seconds}s`;
      return `${seconds}s`;
    }

    function formatBytes(bytes) {
      if (!Number.isFinite(bytes)) return "—";
      if (bytes < 1024) return `${bytes} B`;
      if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
      return `${(bytes / 1024 / 1024).toFixed(1)} MB`;
    }

    function rangeToGate(range) {
      const n = Number(range);
      if (!Number.isFinite(n) || n < 0) return -1;
      // Firmware text mode was displaying values like Range 22. Treat small values as decimeters-ish units for visualization.
      const cm = n <= 120 ? n * 10 : n;
      return Math.max(0, Math.min(GATE_COUNT - 1, Math.floor(cm / GATE_SIZE_CM)));
    }

    function appendRawLine(line) {
      rawLines.push(line);
      while (rawLines.length > MAX_RAW_LINES) rawLines.shift();
      scheduleRawRender();
    }

    function addEvent(event) {
      events.unshift(event);
      while (events.length > MAX_EVENTS) events.pop();
      renderEvents();
    }

    function appendNetworkEvent(event) {
      if (!event || typeof event !== "object") return;
      appendRawLine(JSON.stringify(event));
      handleEvent(event);
    }

    function populateWifiSelect(networks) {
      const previousValue = els.wifiSelect.value;
      const sortedNetworks = Array.isArray(networks)
        ? [...networks].sort((left, right) => Number(right.rssi ?? -999) - Number(left.rssi ?? -999))
        : [];

      const signature = sortedNetworks
        .map(network => `${network.ssid ?? ""}:${network.bssid ?? ""}:${network.rssi ?? ""}:${network.auth_mode ?? network.authMode ?? ""}:${network.open ? 1 : 0}`)
        .join("|");
      if (signature === lastWifiNetworkOptionsSignature) {
        const selectedValue = previousValue && [...els.wifiSelect.options].some(option => option.value === previousValue)
          ? previousValue
          : "";
        if (els.wifiSelect.value !== selectedValue) {
          els.wifiSelect.value = selectedValue;
        }
        return;
      }

      lastWifiNetworkOptionsSignature = signature;
      els.wifiSelect.innerHTML = "";

      const placeholder = document.createElement("option");
      placeholder.value = "";
      placeholder.textContent = "Scan over serial";
      els.wifiSelect.appendChild(placeholder);

      for (const network of sortedNetworks) {
        const option = document.createElement("option");
        option.value = network.ssid ?? "";
        const authMode = network.auth_mode ?? network.authMode ?? (network.open ? "OPEN" : "secured");
        const bssid = network.bssid ? ` • ${network.bssid}` : "";
        option.textContent = `${network.ssid ?? "unknown"} (${network.rssi ?? "?"} dBm, ch ${network.channel ?? "?"}, ${authMode}${bssid})`;
        els.wifiSelect.appendChild(option);
      }

      els.wifiSelect.value = previousValue && [...els.wifiSelect.options].some(option => option.value === previousValue)
        ? previousValue
        : "";
    }

    function applyHaConfig(event) {
      if (typeof event.wifi_ssid === "string") {
        const hasOption = [...els.wifiSelect.options].some(option => option.value === event.wifi_ssid);
        if (hasOption) {
          setInputValueIfChanged(els.wifiSelect, event.wifi_ssid);
          if (els.wifiSsidInput.value.trim() === "") setInputValueIfChanged(els.wifiSsidInput, "");
        } else {
          setInputValueIfChanged(els.wifiSsidInput, event.wifi_ssid);
        }
      }

      if (typeof event.mqtt_host === "string" && event.mqtt_host.trim()) setInputValueIfChanged(els.mqttHostInput, event.mqtt_host);
      if (typeof event.mqtt_port === "number") setInputValueIfChanged(els.mqttPortInput, event.mqtt_port);
      if (typeof event.node_id === "string") setInputValueIfChanged(els.nodeIdInput, event.node_id);
      if (typeof event.friendly_name === "string") setInputValueIfChanged(els.friendlyNameInput, event.friendly_name);
      if (typeof event.room_id === "string") localRoomConfigDraft.roomId = event.room_id;
      if (typeof event.sensor_role === "string") localRoomConfigDraft.sensorRole = event.sensor_role;
      if (typeof event.pose_x_cm === "number") localRoomConfigDraft.poseX = String(event.pose_x_cm);
      if (typeof event.pose_y_cm === "number") localRoomConfigDraft.poseY = String(event.pose_y_cm);
      if (typeof event.heading_deg === "number") localRoomConfigDraft.heading = String(event.heading_deg);
      if (typeof event.room_width_cm === "number") localRoomConfigDraft.roomWidth = String(event.room_width_cm);
      if (typeof event.room_height_cm === "number") localRoomConfigDraft.roomHeight = String(event.room_height_cm);
      if (isEditingLocalNode()) {
        setDisplayedRoomConfig(getLocalRoomConfig());
      }
      if (typeof event.max_detection_range_cm === "number") setInputValueIfChanged(els.maxRangeInput, event.max_detection_range_cm);
      if (typeof event.min_gate_energy === "number") setInputValueIfChanged(els.minGateEnergyInput, event.min_gate_energy);
      if (typeof event.sensitivity_percent === "number") setInputValueIfChanged(els.sensitivityInput, event.sensitivity_percent);
      if (typeof event.presence_hold_ms === "number") setInputValueIfChanged(els.presenceHoldInput, event.presence_hold_ms);
      if (typeof event.min_active_gates === "number") setInputValueIfChanged(els.minActiveGatesInput, event.min_active_gates);
      if (typeof event.min_activity_score === "number") setInputValueIfChanged(els.minActivityScoreInput, event.min_activity_score);
      if (typeof event.led_enabled === "boolean") setCheckedIfChanged(els.ledEnabledInput, event.led_enabled);
      if (typeof event.led_brightness === "number") setInputValueIfChanged(els.ledBrightnessInput, event.led_brightness);
      if (typeof event.mqtt_username_set === "boolean" && !event.mqtt_username_set) setInputValueIfChanged(els.mqttUserInput, "");
      updateRangeDisplays();

      const statusParts = [];
      statusParts.push(event.configured ? "HA config present" : "HA config incomplete");
      statusParts.push(event.wifi_connected ? "Wi-Fi connected" : "Wi-Fi idle");
      statusParts.push(event.mqtt_connected ? "MQTT connected" : "MQTT idle");
      setHaSetupStatus(statusParts.join(" • "));

      if (!settingsUserToggled) {
        if (event.configured && event.wifi_connected && event.mqtt_connected) {
          setSettingsOpen(false);
        } else if (!event.configured) {
          setSettingsOpen(true);
        }
      }

      updateConnectionDiagnostics(event);
      populateRoomEditorTargetOptions();
      updateWizardState(event);
      persistSetupFormState();
    }

    function getProvisioningSsid() {
      return els.wifiSsidInput.value.trim() || els.wifiSelect.value.trim();
    }

    function buildHomeAssistantProvisioningCommand() {
      const ssid = getProvisioningSsid();
      const mqttHost = inferMqttHost();
      const mqttPort = els.mqttPortInput.value.trim() || DEFAULT_MQTT_PORT;
      const nodeId = els.nodeIdInput.value.trim() || "lb_mmwave_presence";
      const friendlyName = els.friendlyNameInput.value.trim() || "LB mmWave Presence";

      if (!ssid) {
        alert("Select or enter an SSID first.");
        return null;
      }

      els.mqttHostInput.value = mqttHost;
      persistSetupFormState();

      const payload = [
        ssid,
        els.wifiPasswordInput.value,
        mqttHost,
        mqttPort,
        els.mqttUserInput.value,
        els.mqttPasswordInput.value,
        nodeId,
        friendlyName
      ].map(value => encodeURIComponent(value)).join("|");

      return `ha_config:${payload}`;
    }

    function buildMqttEndpointCommand() {
      const mqttHost = (els.mqttHostInput.value || "").trim() || DEFAULT_MQTT_HOST;
      const mqttPort = els.mqttPortInput.value.trim() || DEFAULT_MQTT_PORT;
      const payload = [
        mqttHost,
        mqttPort,
        shouldUseWebSockets() ? "true" : "false",
        "/mqtt",
        shouldUseWebSockets() ? "homeassistant.jerrettdavis.com" : ""
      ].map(value => encodeURIComponent(value)).join("|");

      return `ha_mqtt_endpoint:${payload}`;
    }

    function buildRoomConfigCommand(config = readDisplayedRoomConfig()) {
      const payload = [
        config.roomId || "room-default",
        config.sensorRole || "auto",
        config.poseX || "0",
        config.poseY || "0",
        config.heading || "-90",
        config.roomWidth || "600",
        config.roomHeight || "400"
      ].map(value => encodeURIComponent(value)).join("|");

      return `ha_room_config:${payload}`;
    }

    function buildRoomPosePublishCommand(targetNodeId, config = readDisplayedRoomConfig()) {
      const payload = [
        targetNodeId,
        config.roomId || "room-default",
        config.sensorRole || "auto",
        config.poseX || "0",
        config.poseY || "0",
        config.heading || "-90",
        config.roomWidth || "600",
        config.roomHeight || "400"
      ].map(value => encodeURIComponent(value)).join("|");

      return `ha_room_pose_publish:${payload}`;
    }

    function buildTuningCommand() {
      const payload = [
        els.maxRangeInput.value.trim() || "1120",
        els.minGateEnergyInput.value.trim() || "25",
        els.sensitivityInput.value.trim() || "55",
        els.presenceHoldInput.value.trim() || "4000",
        els.minActiveGatesInput.value.trim() || "1",
        els.minActivityScoreInput.value.trim() || "10",
        els.ledEnabledInput.checked ? "true" : "false",
        els.ledBrightnessInput.value.trim() || "32"
      ].map(value => encodeURIComponent(value)).join("|");

      return `tuning_config:${payload}`;
    }

    function waitForHaConfigSaved(timeoutMs = 8000) {
      return new Promise((resolve, reject) => {
        const timeoutId = window.setTimeout(() => {
          if (pendingHaConfigSaved?.timeoutId === timeoutId) {
            pendingHaConfigSaved = null;
          }
          reject(new Error("Timed out waiting for the controller to save Home Assistant settings."));
        }, timeoutMs);

        pendingHaConfigSaved = {
          timeoutId,
          resolve: event => {
            window.clearTimeout(timeoutId);
            pendingHaConfigSaved = null;
            resolve(event);
          }
        };
      });
    }

    async function scanWifiNetworks() {
      setHaSetupStatus("Scanning Wi-Fi networks from the controller...");
      await sendCommandText("wifi_scan");
      if (supportsNetworkTransport() && !port) {
        await fetchDeviceSnapshot();
      }
    }

    async function saveHomeAssistantSetup() {
      const command = buildHomeAssistantProvisioningCommand();
      if (!command) return false;

      setHaSetupStatus("Saving Home Assistant settings to the controller...");

      if (supportsNetworkTransport() && !port) {
        await sendCommandText(command);
        await sendCommandText(buildMqttEndpointCommand());
        await fetchDeviceSnapshot();
        return true;
      }

      const savedPromise = waitForHaConfigSaved();
      await sendCommandText(command);
      await savedPromise;
      await sendCommandText(buildMqttEndpointCommand());
      return true;
    }

    async function oneClickAddToHomeAssistant() {
      const popup = window.open("", "_blank");

      if (popup) {
        popup.document.title = "Opening Home Assistant";
        popup.document.body.innerHTML = "<p style=\"font-family: system-ui, sans-serif; padding: 24px;\">Saving controller settings, then redirecting to Home Assistant...</p>";
      }

      try {
        const saved = await saveHomeAssistantSetup();
        if (!saved) {
          popup?.close();
          return;
        }

        setHaSetupStatus("Settings saved. Opening Home Assistant integrations. The device should appear automatically under MQTT...");

        if (popup) {
          popup.location.href = MY_HOME_ASSISTANT_INTEGRATIONS_URL;
        } else {
          window.open(MY_HOME_ASSISTANT_INTEGRATIONS_URL, "_blank", "noopener");
        }
      } catch (error) {
        popup?.close();
        setHaSetupStatus(error.message || "Failed to save Home Assistant settings.");
        throw error;
      }
    }

    async function saveAdvancedSettings() {
      setHaSetupStatus("Saving room and tuning settings to the controller...");
      syncLocalRoomDraftFromInputs();
      persistSetupFormState();

      await sendCommandText(buildRoomConfigCommand(getLocalRoomConfig()));
      await sendCommandText(buildTuningCommand());

      if (supportsNetworkTransport() && !port) {
        await fetchDeviceSnapshot();
      }

      setHaSetupStatus("Detection tuning saved.");
    }

    async function saveRoomPoseConfig() {
      const config = readDisplayedRoomConfig();
      const targetNodeId = getSelectedRoomEditorTarget();
      pendingRoomEditorAck = {
        targetNodeId,
        local: isEditingLocalNode(),
        config: { ...config }
      };
      setRoomEditorStatus(isEditingLocalNode()
        ? `Saving room layout for ${editorTargetLabel(targetNodeId)}...`
        : `Publishing room layout to ${editorTargetLabel(targetNodeId)}...`);
      if (isEditingLocalNode()) {
        localRoomConfigDraft = { ...config };
      } else {
        applyRoomEditorConfigToPeerCache(targetNodeId, config);
      }

      persistSetupFormState();
      await sendCommandText(isEditingLocalNode() ? buildRoomConfigCommand(config) : buildRoomPosePublishCommand(targetNodeId, config));

      if (supportsNetworkTransport() && !port) {
        if (!isEditingLocalNode()) {
          await new Promise(resolve => window.setTimeout(resolve, 650));
        }
        await fetchDeviceSnapshot();
      }

      setHaSetupStatus(isEditingLocalNode()
        ? `Saved pose for ${editorTargetLabel(targetNodeId)}.`
        : `Published pose update for ${editorTargetLabel(targetNodeId)}.`);
      if (pendingRoomEditorAck) {
        setRoomEditorStatus(isEditingLocalNode()
          ? `Saved room layout for ${editorTargetLabel(targetNodeId)}.`
          : `Published room layout to ${editorTargetLabel(targetNodeId)}. Waiting for peer summary...`);
      }
    }

    function scheduleRoomPoseSave() {
      if (roomPoseSaveTimer) {
        window.clearTimeout(roomPoseSaveTimer);
      }

      roomPoseSaveTimer = window.setTimeout(() => {
        roomPoseSaveTimer = null;
        saveRoomPoseConfig().catch(error => addEvent({ event: "write_error", ms: performance.now(), error: String(error) }));
      }, 450);
    }

    function openMosquittoSetup() {
      window.open(MY_HOME_ASSISTANT_MOSQUITTO_URL, "_blank", "noopener");
      setHaSetupStatus("Opened the Home Assistant Mosquitto setup page. Use it if your broker is not ready yet.");
    }

    function renderEvents() {
      if (pendingEventsRender) return;
      pendingEventsRender = true;
      window.requestAnimationFrame(() => {
        pendingEventsRender = false;
        const fragment = document.createDocumentFragment();
        for (const event of events) {
          const item = document.createElement("div");
          item.className = "event";

          const top = document.createElement("div");
          top.className = "event-top";

          const type = document.createElement("div");
          type.className = "event-type";
          type.textContent = event.event ?? "unknown";

          const time = document.createElement("div");
          time.className = "event-time";
          time.textContent = typeof event.ms === "number" ? `${event.ms} ms` : "—";

          top.appendChild(type);
          top.appendChild(time);

          const body = document.createElement("div");
          body.className = "event-body";
          body.textContent = JSON.stringify(event);

          item.appendChild(top);
          item.appendChild(body);
          fragment.appendChild(item);
        }
        els.eventList.replaceChildren(fragment);
      });
    }

    function updatePresence(presence, event) {
      if (event && typeof event === "object") lastUiEvent = event;
      const known = typeof presence === "boolean";
      if (known) lastPresenceValue = presence;

      els.presenceCard.classList.toggle("active", presence === true);
      setTextContentIfChanged(els.presenceText, known ? (presence ? "PRESENT" : "CLEAR") : "UNKNOWN");

      if (known) {
        const source = event?.event ?? "sample";
        const raw = event?.raw;
        const range = event?.range ?? event?.distance_cm;
        setTextContentIfChanged(els.presenceSubtext, `source: ${source}${raw === undefined ? "" : `, raw: ${raw}`}${range === undefined ? "" : `, range: ${formatDistanceCm(range)}`}`);
      } else {
        setTextContentIfChanged(els.presenceSubtext, "Waiting for samples");
      }
    }

    function updateMetrics(event) {
      if (typeof event.uptime_ms === "number") setTextContentIfChanged(els.uptimeMetric, formatDuration(event.uptime_ms));
      if (typeof event.free_heap === "number") setTextContentIfChanged(els.heapMetric, formatBytes(event.free_heap));
      if (typeof event.radar_bytes_total === "number") setTextContentIfChanged(els.radarBytesMetric, event.radar_bytes_total.toLocaleString());
      if (typeof event.bytes_total === "number") setTextContentIfChanged(els.radarBytesMetric, event.bytes_total.toLocaleString());
      if (typeof event.radar_frames_total === "number") setTextContentIfChanged(els.radarFramesMetric, event.radar_frames_total.toLocaleString());
      if (typeof event.frames_total === "number") setTextContentIfChanged(els.radarFramesMetric, event.frames_total.toLocaleString());

      const range = event.distance_cm ?? event.range;
      if (typeof range === "number") {
        setTextContentIfChanged(els.rangeMetric, formatDistanceCm(range));
      }

      setTextContentIfChanged(els.modeMetric, lastMode);
      updateSensorReporting(event);
    }

    function updateSensorReporting(event) {
      if (typeof event.presence === "boolean") setTextContentIfChanged(els.filteredPresenceMetric, formatBoolean(event.presence));
      if (typeof event.gpio_presence === "boolean") setTextContentIfChanged(els.gpioPresenceMetric, formatBoolean(event.gpio_presence));
      if (typeof event.detection_candidate === "boolean") setTextContentIfChanged(els.candidateMetric, formatBoolean(event.detection_candidate));
      if (typeof event.presence_decay_remaining_ms === "number") setTextContentIfChanged(els.presenceDecayMetric, formatDuration(event.presence_decay_remaining_ms));
      if (typeof event.people_estimate === "number") setTextContentIfChanged(els.peopleEstimateMetric, String(event.people_estimate));
      if (typeof event.active_gate_count === "number") setTextContentIfChanged(els.activeGateMetric, String(event.active_gate_count));
      if (typeof event.activity_score === "number") setTextContentIfChanged(els.activityScoreMetric, String(event.activity_score));
      if (typeof event.dominant_gate_index === "number") setTextContentIfChanged(els.dominantGateMetric, String(event.dominant_gate_index));
      if (typeof event.dominant_gate_distance_cm === "number") setTextContentIfChanged(els.dominantDistanceMetric, event.dominant_gate_distance_cm >= 0 ? formatDistanceCm(event.dominant_gate_distance_cm) : "—");
      if (typeof event.dominant_gate_energy === "number") setTextContentIfChanged(els.dominantEnergyMetric, String(event.dominant_gate_energy));
      if (typeof event.total_gate_energy === "number") setTextContentIfChanged(els.totalGateEnergyMetric, String(event.total_gate_energy));
      if (typeof event.room_people_estimate === "number") setTextContentIfChanged(els.roomPeopleMetric, String(event.room_people_estimate));
      if (typeof event.room_active_nodes === "number") setTextContentIfChanged(els.roomActiveNodesMetric, String(event.room_active_nodes));
      if (typeof event.room_peer_nodes === "number") setTextContentIfChanged(els.roomPeerNodesMetric, String(event.room_peer_nodes));
      if (typeof event.room_activity_score === "number") setTextContentIfChanged(els.roomActivityMetric, String(event.room_activity_score));
      if (typeof event.ble_beacon_count === "number") setTextContentIfChanged(els.bleBeaconCountMetric, String(event.ble_beacon_count));
      if (Array.isArray(event.room_peers)) latestRoomPeers = event.room_peers;
      if (Array.isArray(event.udp_discovery?.peers)) latestUdpDiscoveryPeers = event.udp_discovery.peers;
      if (Array.isArray(event.ble_beacons)) latestBleBeacons = event.ble_beacons;

      populateCalibrationTargetOptions();
      renderPeerLinks();
      updateVersionStatus(lastUiSnapshot || event);
      updateFirmwareSyncStatus(lastUiSnapshot || event);

      if (typeof event.status_led_hex === "string" && event.status_led_hex.length === 6) {
        els.statusLedSwatch.style.background = `#${event.status_led_hex}`;
        setTextContentIfChanged(els.statusLedText, `#${event.status_led_hex}`);
      }

      if (latestBleBeacons.length === 0) {
        setTextContentIfChanged(els.bleBeaconList, "No BLE beacons seen yet.");
      } else {
        setTextContentIfChanged(
          els.bleBeaconList,
          latestBleBeacons
            .map(beacon => {
              const name = beacon.name || beacon.address || "unknown";
              const service = beacon.service_uuid ? ` (${beacon.service_uuid})` : "";
              const rssi = typeof beacon.rssi === "number" ? ` RSSI ${beacon.rssi}` : "";
              return `${name}${service}${rssi}`;
            })
            .join(" • ")
        );
      }

      drawRoomFusionView();
      captureCalibrationSample();
    }

    function hexToBytes(hex) {
      if (!hex || typeof hex !== "string") return [];
      const clean = hex.replace(/[^a-fA-F0-9]/g, "");
      const bytes = [];
      for (let i = 0; i + 1 < clean.length; i += 2) bytes.push(parseInt(clean.slice(i, i + 2), 16));
      return bytes;
    }

    function updateLatestFrame(event) {
      const lines = [
        `event: ${event.event ?? "?"}`,
        `length: ${event.length ?? "?"}`,
        `bytes_total: ${event.bytes_total ?? "?"}`,
        `frames_total: ${event.frames_total ?? "?"}`,
        `presence: ${event.presence ?? "?"}`,
        `range: ${event.distance_cm ?? event.range ?? "?"}`,
        ""
      ];

      if (Array.isArray(event.gates)) {
        lines.push("gates:", JSON.stringify(event.gates), "");
      }

      lines.push("hex:", event.hex ?? "", "", "ascii:", event.ascii ?? "");
      els.latestFramePre.textContent = lines.join("\n");
    }

    function pushActivityFrame(kind, values, event) {
      activityFrames.push({
        kind,
        ms: event?.ms ?? performance.now(),
        values: values.slice(0, 64),
        presence: event?.presence,
        range: event?.distance_cm ?? event?.range
      });

      while (activityFrames.length > ACTIVITY_HISTORY) activityFrames.shift();
      drawActivityMap();
    }

    function pushRawActivityFrame(event) {
      const bytes = hexToBytes(event.hex);
      if (bytes.length === 0) return;
      pushActivityFrame("raw", bytes, event);
    }

    function pushTextRangeActivityFrame(event) {
      const values = new Array(64).fill(0);
      const gate = rangeToGate(event.range);
      for (let i = 0; i < values.length; i++) values[i] = event.presence ? 12 : 2;
      if (gate >= 0) {
        const center = Math.floor((gate / (GATE_COUNT - 1)) * 63);
        for (let i = Math.max(0, center - 3); i <= Math.min(63, center + 3); i++) {
          values[i] = 220 - Math.abs(i - center) * 35;
        }
      }
      pushActivityFrame("text", values, event);
    }

    function pushEnergyActivityFrame(event) {
      if (!Array.isArray(event.gates)) return;
      const max = Math.max(...event.gates, 1);
      const values = [];
      for (const gate of event.gates) {
        const scaled = Math.round((Number(gate) / max) * 255);
        values.push(scaled, scaled, scaled, scaled);
      }
      pushActivityFrame("energy", values, event);
    }

    function colorForIntensity(intensity, alpha = 1) {
      const v = Math.max(0, Math.min(1, intensity));
      const red = Math.floor(30 + v * 220);
      const green = Math.floor(90 + v * 120);
      const blue = Math.floor(140 - v * 90);
      return `rgba(${red}, ${green}, ${blue}, ${alpha})`;
    }

    function drawActivityMap() {
      if (pendingActivityMapDraw) return;
      pendingActivityMapDraw = true;
      window.requestAnimationFrame(() => {
        pendingActivityMapDraw = false;
        renderActivityMap();
      });
    }

    function renderActivityMap() {
      const canvas = els.activityCanvas;
      const ctx = canvas.getContext("2d");
      const width = canvas.width;
      const height = canvas.height;

      ctx.clearRect(0, 0, width, height);
      ctx.fillStyle = "#050816";
      ctx.fillRect(0, 0, width, height);

      const rows = ACTIVITY_HISTORY;
      const cols = 64;
      const cellW = width / cols;
      const cellH = height / rows;

      for (let row = 0; row < activityFrames.length; row++) {
        const frame = activityFrames[activityFrames.length - 1 - row];
        const values = frame.values.length ? frame.values : [0];

        for (let col = 0; col < cols; col++) {
          const value = values[col % values.length] ?? 0;
          const intensity = Math.max(0.05, value / 255);
          ctx.fillStyle = colorForIntensity(intensity, 0.92);
          ctx.fillRect(col * cellW, row * cellH, Math.max(1, cellW - 1), Math.max(1, cellH - 1));
        }
      }

      ctx.strokeStyle = "rgba(255,255,255,0.18)";
      ctx.lineWidth = 2;
      ctx.strokeRect(1, 1, width - 2, height - 2);

      ctx.fillStyle = "rgba(255,255,255,0.75)";
      ctx.font = "24px ui-monospace, monospace";
      ctx.fillText("newest frames at top", 24, 36);
    }

    function normalizeGate(value, gateIndex) {
      const peak = Math.max(gatePeaks[gateIndex], 1);
      return Math.max(0, Math.min(1, value / peak));
    }

    function handleLd2420Energy(event) {
      if (!Array.isArray(event.gates)) return;

      latestEnergyFrame = event;
      latestTextRangeFrame = null;
      lastMode = "energy";

      for (let i = 0; i < GATE_COUNT; i++) {
        const value = Number(event.gates[i] ?? 0);
        gatePeaks[i] = Math.max(gatePeaks[i] * 0.995, value, 1);
        gateHold[i] = Math.max(gateHold[i] * 0.88, value);
      }

      updatePresence(event.presence, event);
      updateLatestFrame(event);
      pushEnergyActivityFrame(event);
      drawStaticRadarSurface();
    }

    function handleLd2420TextRange(event) {
      latestTextRangeFrame = event;
      lastMode = "text range";

      const gate = rangeToGate(event.range);
      for (let i = 0; i < GATE_COUNT; i++) {
        syntheticHold[i] *= 0.82;
      }
      if (gate >= 0) {
        syntheticHold[gate] = Math.max(syntheticHold[gate], event.presence ? 1 : 0.25);
        if (gate > 0) syntheticHold[gate - 1] = Math.max(syntheticHold[gate - 1], event.presence ? 0.45 : 0.15);
        if (gate < GATE_COUNT - 1) syntheticHold[gate + 1] = Math.max(syntheticHold[gate + 1], event.presence ? 0.45 : 0.15);
      }

      updatePresence(event.presence, event);
      updateLatestFrame(event);
      pushTextRangeActivityFrame(event);
      drawStaticRadarSurface();
    }

    function drawStaticRadarSurface() {
      if (pendingSurfaceDraw) return;
      pendingSurfaceDraw = true;
      window.requestAnimationFrame(() => {
        pendingSurfaceDraw = false;
        renderStaticRadarSurface();
      });
    }

    function renderStaticRadarSurface() {
      const canvas = els.surfaceCanvas;
      const ctx = canvas.getContext("2d");
      const w = canvas.width;
      const h = canvas.height;

      ctx.clearRect(0, 0, w, h);
      ctx.fillStyle = "#050816";
      ctx.fillRect(0, 0, w, h);

      const cx = w / 2;
      const cy = h - 42;
      const maxRadius = Math.min(w * 0.46, h * 0.88);
      const angleStart = -60 * Math.PI / 180;
      const angleEnd = -120 * Math.PI / 180;

      const hasEnergy = latestEnergyFrame && Array.isArray(latestEnergyFrame.gates);
      const hasText = latestTextRangeFrame && typeof latestTextRangeFrame.range === "number";

      for (let gate = GATE_COUNT - 1; gate >= 0; gate--) {
        const innerR = (gate / GATE_COUNT) * maxRadius;
        const outerR = ((gate + 1) / GATE_COUNT) * maxRadius;

        let normalized = 0;
        if (hasEnergy) {
          const current = Number(latestEnergyFrame.gates[gate] ?? 0);
          const held = gateHold[gate] ?? 0;
          normalized = normalizeGate(Math.max(current, held), gate);
        } else if (hasText) {
          normalized = syntheticHold[gate] ?? 0;
        }

        const alpha = 0.07 + normalized * 0.84;

        ctx.beginPath();
        ctx.arc(cx, cy, outerR, angleEnd, angleStart);
        ctx.arc(cx, cy, innerR, angleStart, angleEnd, true);
        ctx.closePath();
        ctx.fillStyle = colorForIntensity(normalized, alpha);
        ctx.fill();
        ctx.strokeStyle = "rgba(255,255,255,0.11)";
        ctx.lineWidth = 2;
        ctx.stroke();

        const labelAngle = -90 * Math.PI / 180;
        const labelR = (innerR + outerR) / 2;
        const lx = cx + Math.cos(labelAngle) * labelR;
        const ly = cy + Math.sin(labelAngle) * labelR;

        ctx.fillStyle = "rgba(255,255,255,0.84)";
        ctx.font = "18px ui-monospace, monospace";
        ctx.textAlign = "center";
        ctx.fillText(`${gate}`, lx, ly);

        ctx.fillStyle = "rgba(255,255,255,0.55)";
        ctx.font = "13px ui-monospace, monospace";
        ctx.fillText(`${((gate + 1) * GATE_SIZE_CM / 100).toFixed(1)}m`, lx, ly + 18);
      }

      ctx.beginPath();
      ctx.arc(cx, cy, 12, 0, Math.PI * 2);
      ctx.fillStyle = "#ffffff";
      ctx.fill();

      const source = hasEnergy ? latestEnergyFrame : latestTextRangeFrame;
      const presenceText = source?.presence === true ? "PRESENT" : source?.presence === false ? "CLEAR" : "UNKNOWN";
      const distanceText = hasEnergy && source?.distance_cm != null
        ? `${source.distance_cm} cm`
        : hasText
          ? `Range ${source.range}`
          : "unknown";

      ctx.fillStyle = "rgba(255,255,255,0.88)";
      ctx.font = "22px ui-sans-serif, system-ui";
      ctx.textAlign = "left";
      ctx.fillText(`Mode: ${lastMode}`, 24, 40);
      ctx.fillText(`Presence: ${presenceText}`, 24, 72);
      ctx.fillText(`Distance: ${distanceText}`, 24, 104);

      if (hasEnergy) {
        const maxGateValue = Math.max(...latestEnergyFrame.gates);
        const maxGateIndex = latestEnergyFrame.gates.indexOf(maxGateValue);
        ctx.fillText(`Strongest gate: ${maxGateIndex} (${maxGateValue})`, 24, 136);
      } else if (hasText) {
        const gate = rangeToGate(latestTextRangeFrame.range);
        ctx.fillText(`Projected gate: ${gate >= 0 ? gate : "unknown"}`, 24, 136);
      } else {
        ctx.fillText("Waiting for radar frames", 24, 136);
      }
    }

    function drawRoomFusionView() {
      if (pendingRoomFusionDraw) return;
      pendingRoomFusionDraw = true;
      window.requestAnimationFrame(() => {
        pendingRoomFusionDraw = false;
        renderRoomFusionView();
      });
    }

    function renderRoomFusionView() {
      const canvas = els.roomFusionCanvas;
      const ctx = canvas.getContext("2d");
      const w = canvas.width;
      const h = canvas.height;

      ctx.clearRect(0, 0, w, h);
      ctx.fillStyle = "#06111f";
      ctx.fillRect(0, 0, w, h);

      const maxRangeCm = 450;
      const localDistanceCm = Number(lastUiEvent?.dominant_gate_distance_cm ?? lastUiSnapshot?.dominant_gate_distance_cm ?? -1);
      const roomPeople = Number(lastUiEvent?.room_people_estimate ?? lastUiSnapshot?.room_people_estimate ?? 0);
      const roomPeers = Array.isArray(latestRoomPeers) ? latestRoomPeers : [];
      const selectedTargetNodeId = getSelectedRoomEditorTarget();
      const localNodeId = currentLocalNodeId();
      const localConfig = getLocalRoomConfig();
      const selectedConfig = readDisplayedRoomConfig();
      const roomWidthCm = Math.max(100, Number(selectedConfig.roomWidth || 600));
      const roomHeightCm = Math.max(100, Number(selectedConfig.roomHeight || 400));
      const sensors = [
        {
          nodeId: localNodeId,
          label: "Local node",
          role: localConfig.sensorRole || lastUiSnapshot?.sensor_role || "local",
          editable: selectedTargetNodeId === ROOM_EDITOR_LOCAL_TARGET || selectedTargetNodeId === localNodeId,
          poseX: Number(localConfig.poseX || lastUiSnapshot?.pose_x_cm || 0),
          poseY: Number(localConfig.poseY || lastUiSnapshot?.pose_y_cm || 0),
          headingDeg: Number(localConfig.heading || lastUiSnapshot?.heading_deg || -90),
          distanceCm: localDistanceCm,
          color: "#53e3a6",
          coneColor: "rgba(61, 214, 140, 0.18)"
        },
        ...roomPeers.map((peer, index) => ({
          nodeId: peer.node_id || `peer-${index + 1}`,
          label: peer.node_id || `Peer ${index + 1}`,
          role: (peer.node_id || "") === selectedTargetNodeId ? (els.sensorRoleInput.value || peer.sensor_role || "peer") : (peer.sensor_role || "peer"),
          editable: (peer.node_id || "") === selectedTargetNodeId,
          poseX: (peer.node_id || "") === selectedTargetNodeId ? Number(els.poseXInput.value || peer.pose_x_cm || 0) : Number(peer.pose_x_cm ?? 0),
          poseY: (peer.node_id || "") === selectedTargetNodeId ? Number(els.poseYInput.value || peer.pose_y_cm || 0) : Number(peer.pose_y_cm ?? 0),
          headingDeg: (peer.node_id || "") === selectedTargetNodeId ? Number(els.headingInput.value || peer.heading_deg || -90) : Number(peer.heading_deg ?? 0),
          distanceCm: Number(peer.dominant_gate_distance_cm ?? -1),
          color: "#ffc46a",
          coneColor: "rgba(255, 196, 87, 0.16)",
          distanceDeltaCm: Number(peer.distance_delta_cm ?? -1),
          angleHint: Number(peer.relative_angle_guess_deg ?? 0),
          angleConfidence: Number(peer.relative_angle_confidence_percent ?? 0)
        }))
      ];

      function projectPoint(sensor, distanceCm) {
        const headingRad = Number(sensor.headingDeg ?? 0) * Math.PI / 180;
        return {
          x: Number(sensor.poseX ?? 0) + Math.cos(headingRad) * distanceCm,
          y: Number(sensor.poseY ?? 0) + Math.sin(headingRad) * distanceCm
        };
      }

      const detections = sensors
        .filter(sensor => Number.isFinite(sensor.distanceCm) && sensor.distanceCm >= 0)
        .map(sensor => ({ ...projectPoint(sensor, sensor.distanceCm), sensor }));

      const clusters = [];
      detections.forEach(detection => {
        const cluster = clusters.find(candidate => Math.hypot(candidate.x - detection.x, candidate.y - detection.y) <= 60);
        if (cluster) {
          cluster.members.push(detection);
          cluster.x = cluster.members.reduce((sum, item) => sum + item.x, 0) / cluster.members.length;
          cluster.y = cluster.members.reduce((sum, item) => sum + item.y, 0) / cluster.members.length;
        } else {
          clusters.push({ x: detection.x, y: detection.y, members: [detection] });
        }
      });

      const extents = [];
      extents.push({ x: 0, y: 0 }, { x: roomWidthCm, y: roomHeightCm });
      sensors.forEach(sensor => extents.push({ x: sensor.poseX, y: sensor.poseY }));
      detections.forEach(detection => extents.push({ x: detection.x, y: detection.y }));
      if (extents.length === 0) {
        extents.push({ x: -150, y: -150 }, { x: 150, y: 150 });
      }

      let minX = Math.min(...extents.map(point => point.x));
      let maxX = Math.max(...extents.map(point => point.x));
      let minY = Math.min(...extents.map(point => point.y));
      let maxY = Math.max(...extents.map(point => point.y));
      const paddingCm = 120;
      minX -= paddingCm;
      maxX += paddingCm;
      minY -= paddingCm;
      maxY += paddingCm;
      if (maxX - minX < 300) {
        const centerX = (maxX + minX) / 2;
        minX = centerX - 150;
        maxX = centerX + 150;
      }
      if (maxY - minY < 300) {
        const centerY = (maxY + minY) / 2;
        minY = centerY - 150;
        maxY = centerY + 150;
      }

      const inner = { left: 70, top: 45, right: w - 40, bottom: h - 55 };
      const scaleX = (inner.right - inner.left) / (maxX - minX || 1);
      const scaleY = (inner.bottom - inner.top) / (maxY - minY || 1);
      const scale = Math.min(scaleX, scaleY);
      const interactiveNodes = [];
      const gridSpacingCm = Math.max(5, Number(els.roomGridSizeInput.value) || 25);

      function mapPoint(point) {
        return {
          x: inner.left + (point.x - minX) * scale,
          y: inner.bottom - (point.y - minY) * scale
        };
      }

      function unmapPoint(point) {
        return {
          x: minX + ((point.x - inner.left) / scale),
          y: minY + ((inner.bottom - point.y) / scale)
        };
      }

      for (let gridX = Math.floor(minX / gridSpacingCm) * gridSpacingCm; gridX <= maxX; gridX += gridSpacingCm) {
        const mapped = mapPoint({ x: gridX, y: minY });
        ctx.beginPath();
        ctx.moveTo(mapped.x, inner.top);
        ctx.lineTo(mapped.x, inner.bottom);
        ctx.strokeStyle = gridX === 0 ? "rgba(255,255,255,0.28)" : "rgba(255,255,255,0.08)";
        ctx.lineWidth = gridX === 0 ? 2 : 1;
        ctx.stroke();
        ctx.fillStyle = "rgba(255,255,255,0.55)";
        ctx.font = "12px ui-monospace, monospace";
        ctx.textAlign = "center";
        ctx.fillText(`${Math.round(gridX)} cm`, mapped.x, inner.bottom + 18);
      }

      for (let gridY = Math.floor(minY / gridSpacingCm) * gridSpacingCm; gridY <= maxY; gridY += gridSpacingCm) {
        const mapped = mapPoint({ x: minX, y: gridY });
        ctx.beginPath();
        ctx.moveTo(inner.left, mapped.y);
        ctx.lineTo(inner.right, mapped.y);
        ctx.strokeStyle = gridY === 0 ? "rgba(255,255,255,0.28)" : "rgba(255,255,255,0.08)";
        ctx.lineWidth = gridY === 0 ? 2 : 1;
        ctx.stroke();
        ctx.fillStyle = "rgba(255,255,255,0.55)";
        ctx.font = "12px ui-monospace, monospace";
        ctx.textAlign = "right";
        ctx.fillText(`${Math.round(gridY)} cm`, inner.left - 10, mapped.y + 4);
      }

      ctx.strokeStyle = "rgba(255,255,255,0.12)";
      ctx.lineWidth = 2;
      ctx.strokeRect(inner.left, inner.top, inner.right - inner.left, inner.bottom - inner.top);

      const roomTopLeft = mapPoint({ x: 0, y: roomHeightCm });
      const roomBottomRight = mapPoint({ x: roomWidthCm, y: 0 });
      const roomRectWidth = roomBottomRight.x - roomTopLeft.x;
      const roomRectHeight = roomBottomRight.y - roomTopLeft.y;
      ctx.fillStyle = "rgba(96, 165, 250, 0.08)";
      ctx.fillRect(roomTopLeft.x, roomTopLeft.y, roomRectWidth, roomRectHeight);
      ctx.strokeStyle = "rgba(96, 165, 250, 0.85)";
      ctx.lineWidth = 3;
      ctx.strokeRect(roomTopLeft.x, roomTopLeft.y, roomRectWidth, roomRectHeight);

      const eastWallHandle = mapPoint({ x: roomWidthCm, y: roomHeightCm / 2 });
      const northWallHandle = mapPoint({ x: roomWidthCm / 2, y: roomHeightCm });
      [eastWallHandle, northWallHandle].forEach(handle => {
        ctx.beginPath();
        ctx.rect(handle.x - 8, handle.y - 8, 16, 16);
        ctx.fillStyle = "rgba(255,255,255,0.94)";
        ctx.fill();
        ctx.strokeStyle = "rgba(96, 165, 250, 0.95)";
        ctx.lineWidth = 2;
        ctx.stroke();
      });

      ctx.fillStyle = "rgba(255,255,255,0.72)";
      ctx.font = "13px ui-monospace, monospace";
      ctx.textAlign = "left";
      ctx.fillText(`Room frame x ${Math.round(minX)}..${Math.round(maxX)} cm`, inner.left, 24);
      ctx.fillText(`y ${Math.round(minY)}..${Math.round(maxY)} cm`, inner.left + 290, 24);
      ctx.fillText(`editing ${editorTargetLabel(selectedTargetNodeId)}`, inner.left + 520, 24);
      ctx.fillText(`room people ${Number.isFinite(roomPeople) ? roomPeople : 0}`, inner.right - 150, 24);
      ctx.fillText(`room ${Math.round(roomWidthCm)} x ${Math.round(roomHeightCm)} cm`, inner.left, h - 18);

      ctx.font = "13px ui-monospace, monospace";
      ctx.fillStyle = "rgba(255,255,255,0.82)";
      ctx.fillText("drag wall handles to resize room envelope", roomTopLeft.x + 12, roomTopLeft.y + 22);

      interactiveNodes.push({ nodeId: selectedTargetNodeId || ROOM_EDITOR_LOCAL_TARGET, mode: "room-width", center: eastWallHandle, radius: 16 });
      interactiveNodes.push({ nodeId: selectedTargetNodeId || ROOM_EDITOR_LOCAL_TARGET, mode: "room-height", center: northWallHandle, radius: 16 });

      function drawSensor(sensor) {
        const base = mapPoint({ x: sensor.poseX, y: sensor.poseY });
        const headingRad = sensor.headingDeg * Math.PI / 180;
        const coneHalfAngle = 28 * Math.PI / 180;
        const coneRadiusPx = maxRangeCm * scale * 0.9;

        ctx.beginPath();
        ctx.moveTo(base.x, base.y);
        ctx.arc(base.x, base.y, coneRadiusPx, -headingRad - coneHalfAngle, -headingRad + coneHalfAngle);
        ctx.closePath();
        ctx.fillStyle = sensor.coneColor;
        ctx.fill();

        ctx.beginPath();
        ctx.arc(base.x, base.y, 8, 0, Math.PI * 2);
        ctx.fillStyle = sensor.color;
        ctx.fill();

        const arrowTip = {
          x: base.x + Math.cos(headingRad) * 34,
          y: base.y - Math.sin(headingRad) * 34
        };
        const rotateHandle = {
          x: base.x + Math.cos(headingRad) * 48,
          y: base.y - Math.sin(headingRad) * 48
        };
        ctx.beginPath();
        ctx.moveTo(base.x, base.y);
        ctx.lineTo(arrowTip.x, arrowTip.y);
        ctx.strokeStyle = sensor.color;
        ctx.lineWidth = 3;
        ctx.stroke();

        if (sensor.editable) {
          ctx.beginPath();
          ctx.arc(rotateHandle.x, rotateHandle.y, 8, 0, Math.PI * 2);
          ctx.fillStyle = "rgba(255,255,255,0.9)";
          ctx.fill();
          ctx.strokeStyle = sensor.color;
          ctx.lineWidth = 2;
          ctx.stroke();
        }

        ctx.fillStyle = "rgba(255,255,255,0.92)";
        ctx.font = "14px ui-monospace, monospace";
        ctx.textAlign = "left";
        ctx.fillText(`${sensor.label} (${sensor.role})`, base.x + 12, base.y - 10);
        ctx.fillText(`(${Math.round(sensor.poseX)}, ${Math.round(sensor.poseY)}) @ ${Math.round(sensor.headingDeg)} deg`, base.x + 12, base.y + 10);
        if (sensor.editable) {
          ctx.fillText("drag node to move, drag white handle to rotate", base.x + 12, base.y + 30);
          interactiveNodes.push({ nodeId: sensor.nodeId, mode: "move", center: base, radius: 16, sensor });
          interactiveNodes.push({ nodeId: sensor.nodeId, mode: "rotate", center: rotateHandle, radius: 14, sensor });
        }

        if (Number.isFinite(sensor.distanceCm) && sensor.distanceCm >= 0) {
          const detection = mapPoint(projectPoint(sensor, sensor.distanceCm));
          ctx.beginPath();
          ctx.moveTo(base.x, base.y);
          ctx.lineTo(detection.x, detection.y);
          ctx.strokeStyle = sensor.color;
          ctx.lineWidth = 2;
          ctx.setLineDash([10, 6]);
          ctx.stroke();
          ctx.setLineDash([]);
        }
      }

      sensors.forEach(drawSensor);

      clusters.forEach(cluster => {
        const mapped = mapPoint(cluster);
        const isFused = cluster.members.length > 1;
        ctx.beginPath();
        ctx.arc(mapped.x, mapped.y, isFused ? 10 : 7, 0, Math.PI * 2);
        ctx.fillStyle = isFused ? "#fff3a1" : "#9bb1ff";
        ctx.fill();
        ctx.strokeStyle = isFused ? "#ffc46a" : "#9bb1ff";
        ctx.lineWidth = 3;
        ctx.stroke();

        ctx.fillStyle = "rgba(255,255,255,0.92)";
        ctx.font = "13px ui-monospace, monospace";
        ctx.textAlign = "left";
        ctx.fillText(isFused ? `Fused target from ${cluster.members.length} views` : "Single-node target", mapped.x + 12, mapped.y - 10);
        ctx.fillText(`(${Math.round(cluster.x)}, ${Math.round(cluster.y)})`, mapped.x + 12, mapped.y + 10);
      });

      const uniqueSensorPositions = new Set(sensors.map(sensor => `${Math.round(sensor.poseX)}:${Math.round(sensor.poseY)}`));
      roomFusionLayout = { interactiveNodes, unmapPoint, sensors, roomWidthCm, roomHeightCm };
      if (sensors.length > 1 && uniqueSensorPositions.size <= 1) {
        ctx.fillStyle = "rgba(255,255,255,0.72)";
        ctx.font = "16px ui-monospace, monospace";
        ctx.textAlign = "center";
        ctx.fillText("All sensors still share the same pose. Set manual X/Y/heading values to model the room accurately.", w / 2, h - 18);
      }

      if (roomPeers.length === 0) {
        ctx.fillStyle = "rgba(255,255,255,0.72)";
        ctx.font = "22px Georgia, serif";
        ctx.textAlign = "center";
        ctx.fillText("No peer nodes visible yet", w / 2, h / 2 - 12);
        ctx.font = "14px ui-monospace, monospace";
        ctx.fillText("Connect another node to the same room to enable fused room layout hints.", w / 2, h / 2 + 18);
      }
    }

    function handleGenericRadarFrame(event) {
      latestGenericFrame = event;
      lastMode = lastMode === "waiting" ? "raw" : lastMode;
      updateLatestFrame(event);
      pushRawActivityFrame(event);
      drawStaticRadarSurface();
    }

    function handleEvent(event) {
      addEvent(event);

      switch (event.event) {
        case "boot":
          lastMode = "booted";
          updatePresence(undefined, event);
          break;

        case "ha_config":
          applyHaConfig(event);
          break;

        case "ha_config_saved":
          setHaSetupStatus(event.configured ? "Settings saved. Waiting for Wi-Fi and MQTT." : "Settings saved, but configuration is still incomplete.");
          if (pendingHaConfigSaved?.resolve) pendingHaConfigSaved.resolve(event);
          break;

        case "wifi_scan_results":
          populateWifiSelect(event.networks);
          setHaSetupStatus(`Found ${event.count ?? 0} Wi-Fi network${event.count === 1 ? "" : "s"}.`);
          break;

        case "heartbeat":
          updateConnectionDiagnostics(event);
          if (typeof event.presence === "boolean") updatePresence(event.presence, event);
          break;

        case "presence_sample":
        case "presence_changed":
          updatePresence(event.presence, event);
          break;

        case "ld2420_energy":
          handleLd2420Energy(event);
          break;

        case "ld2420_text_range":
          handleLd2420TextRange(event);
          break;

        case "radar_uart_frame":
          handleGenericRadarFrame(event);
          break;
      }

      updateMetrics(event);
      drawStaticRadarSurface();
    }

    function handleLine(line) {
      const trimmed = line.trim();
      if (!trimmed) return;

      appendRawLine(trimmed);

      try {
        const event = JSON.parse(trimmed);
        handleEvent(event);
      } catch (error) {
        addEvent({ event: "parse_error", ms: performance.now(), line: trimmed, error: String(error) });
      }
    }

    async function connectSerial() {
      if (!("serial" in navigator)) {
        alert("Web Serial is not available. Use Chrome or Edge on desktop, served from localhost or HTTPS.");
        return;
      }

      stopSnapshotPolling();
      disconnectDeviceSocket();

      setConnectionState("connecting", "Requesting port");
      port = await navigator.serial.requestPort();

      setConnectionState("connecting", "Opening port");
      await port.open({ baudRate: BAUD_RATE, dataBits: 8, stopBits: 1, parity: "none", flowControl: "none" });

      keepReading = true;
      setConnectionState("connected", `Connected @ ${BAUD_RATE}`);

      readLoop().catch(error => {
        console.error(error);
        addEvent({ event: "serial_error", ms: performance.now(), error: String(error) });
        setConnectionState("disconnected", "Serial error");
      });

      sendCommandText("ha_status").catch(() => {});
    }

    async function disconnectSerial() {
      keepReading = false;

      try {
        if (reader) {
          await reader.cancel();
          reader.releaseLock();
          reader = null;
        }

        if (port) {
          await port.close();
          port = null;
        }
      } catch (error) {
        console.warn(error);
      }

      setConnectionState("disconnected", "Disconnected");
      startSnapshotPolling();
      connectDeviceSocket();
    }

    async function readLoop() {
      const textDecoder = new TextDecoder();

      while (port && port.readable && keepReading) {
        reader = port.readable.getReader();

        try {
          while (keepReading) {
            const { value, done } = await reader.read();
            if (done) break;
            if (!value) continue;

            rawLineBuffer += textDecoder.decode(value, { stream: true });

            let newlineIndex;
            while ((newlineIndex = rawLineBuffer.indexOf("\n")) >= 0) {
              const line = rawLineBuffer.slice(0, newlineIndex);
              rawLineBuffer = rawLineBuffer.slice(newlineIndex + 1);
              handleLine(line);
            }
          }
        } finally {
          reader.releaseLock();
          reader = null;
        }
      }
    }

    async function sendCommandText(command) {
      const clean = command.trim();
      if (!clean) return;

      if (!port || !port.writable) {
        if (!supportsNetworkTransport()) {
          alert("Not connected.");
          return;
        }

        const response = await fetch(DEVICE_COMMAND_ENDPOINT, {
          method: "POST",
          headers: { "Content-Type": "text/plain" },
          body: clean
        });

        if (!response.ok) {
          throw new Error(`Command request failed (${response.status})`);
        }

        applyDeviceSnapshot(await response.json());
        return;
      }

      const writer = port.writable.getWriter();
      const encoded = new TextEncoder().encode(`${clean}\n`);

      try {
        await writer.write(encoded);
      } finally {
        writer.releaseLock();
      }
    }

    async function sendCommand() {
      const command = els.commandInput.value.trim();
      if (!command) return;
      await sendCommandText(command);
      els.commandInput.value = "";
    }

    function clearUi() {
      rawLines = [];
      events = [];
      activityFrames = [];
      latestEnergyFrame = null;
      latestTextRangeFrame = null;
      latestGenericFrame = null;
      lastPresenceValue = undefined;
      lastMode = "waiting";
      latestRoomPeers = [];
      latestUdpDiscoveryPeers = [];
      latestBleBeacons = [];
      calibrationSession = {
        active: false,
        targetNodeId: "",
        samples: [],
        suggestion: null,
        lastSampleAt: 0
      };
      localRoomConfigDraft = {
        roomId: "room-default",
        sensorRole: "auto",
        poseX: "0",
        poseY: "0",
        heading: "-90",
        roomWidth: "600",
        roomHeight: "400"
      };
      resetSnapshotTracking();
      gatePeaks = new Array(GATE_COUNT).fill(1);
      gateHold = new Array(GATE_COUNT).fill(0);
      syntheticHold = new Array(GATE_COUNT).fill(0);

      rawLines = [];
      events = [];
      scheduleRawRender();
      renderEvents();
      setTextContentIfChanged(els.latestFramePre, "No radar frame received yet.");
      setTextContentIfChanged(els.uptimeMetric, "—");
      setTextContentIfChanged(els.heapMetric, "—");
      setTextContentIfChanged(els.radarBytesMetric, "0");
      setTextContentIfChanged(els.radarFramesMetric, "0");
      setTextContentIfChanged(els.rangeMetric, "—");
      setTextContentIfChanged(els.modeMetric, "—");
      setTextContentIfChanged(els.wifiStateMetric, "—");
      setTextContentIfChanged(els.mqttStateMetric, "—");
      setTextContentIfChanged(els.ipAddressMetric, "—");
      setTextContentIfChanged(els.topicPrefixMetric, "—");
      setTextContentIfChanged(els.wifiRssiMetric, "—");
      setTextContentIfChanged(els.wifiChannelMetric, "—");
      setTextContentIfChanged(els.wifiBssidMetric, "—");
      setTextContentIfChanged(els.wifiReasonText, "No connection diagnostics received yet.");
      setTextContentIfChanged(els.filteredPresenceMetric, "—");
      setTextContentIfChanged(els.gpioPresenceMetric, "—");
      els.candidateMetric.textContent = "—";
      els.presenceDecayMetric.textContent = "—";
      els.peopleEstimateMetric.textContent = "—";
      els.activeGateMetric.textContent = "—";
      els.activityScoreMetric.textContent = "—";
      els.dominantGateMetric.textContent = "—";
      els.dominantDistanceMetric.textContent = "—";
      els.dominantEnergyMetric.textContent = "—";
      els.totalGateEnergyMetric.textContent = "—";
      els.roomPeopleMetric.textContent = "—";
      els.roomActiveNodesMetric.textContent = "—";
      els.roomPeerNodesMetric.textContent = "—";
      els.roomActivityMetric.textContent = "—";
      els.bleBeaconCountMetric.textContent = "0";
      els.bleBeaconList.textContent = "No BLE beacons seen yet.";
      populateRoomEditorTargetOptions();
      populateCalibrationTargetOptions();
      renderPeerLinks();
      renderCalibrationState();
      setTextContentIfChanged(els.firmwareSyncCandidate, "No higher peer release visible.");
      setTextContentIfChanged(els.firmwareSyncStatus, "Waiting for firmware sync telemetry.");
      setRoomEditorStatus("");
      els.statusLedSwatch.style.background = "#000000";
      els.statusLedText.textContent = "—";

      updatePresence(undefined);
      drawActivityMap();
      drawStaticRadarSurface();
      drawRoomFusionView();
    }

    function updateRoomEditorPoseInputs(xCm, yCm, headingDeg) {
      const step = roomEditorStep();
      if (Number.isFinite(xCm)) els.poseXInput.value = String(roundRoomValue(xCm, step));
      if (Number.isFinite(yCm)) els.poseYInput.value = String(roundRoomValue(yCm, step));
      if (Number.isFinite(headingDeg)) {
        const normalizedHeading = Math.round(((headingDeg + 180) % 360 + 360) % 360 - 180);
        els.headingInput.value = String(roundRoomValue(normalizedHeading, 5));
      }
      if (isEditingLocalNode()) {
        syncLocalRoomDraftFromInputs();
      }
      updateRangeDisplays();
      persistSetupFormState();
      drawRoomFusionView();
      scheduleRoomPoseSave();
    }

    function updateRoomEditorBoundsInputs(roomWidthCm, roomHeightCm) {
      const step = roomEditorStep();
      if (Number.isFinite(roomWidthCm)) {
        els.roomWidthInput.value = String(Math.max(100, Math.min(4000, roundRoomValue(roomWidthCm, step))));
      }
      if (Number.isFinite(roomHeightCm)) {
        els.roomHeightInput.value = String(Math.max(100, Math.min(4000, roundRoomValue(roomHeightCm, step))));
      }
      if (isEditingLocalNode()) {
        syncLocalRoomDraftFromInputs();
      }
      persistSetupFormState();
      drawRoomFusionView();
      scheduleRoomPoseSave();
    }

    function roomCanvasPointFromEvent(event) {
      const rect = els.roomFusionCanvas.getBoundingClientRect();
      return {
        x: (event.clientX - rect.left) * (els.roomFusionCanvas.width / rect.width),
        y: (event.clientY - rect.top) * (els.roomFusionCanvas.height / rect.height)
      };
    }

    function beginRoomEditorInteraction(event) {
      if (!roomFusionLayout?.interactiveNodes?.length) return;

      const point = roomCanvasPointFromEvent(event);
      const hit = roomFusionLayout.interactiveNodes.find(target => Math.hypot(point.x - target.center.x, point.y - target.center.y) <= target.radius);
      if (!hit) return;

      roomEditorState = { mode: hit.mode, nodeId: hit.nodeId };
      try {
        els.roomFusionCanvas.setPointerCapture(event.pointerId);
      } catch (error) {
        // Some browsers reject pointer capture for synthetic or already-ended pointers.
      }
      event.preventDefault();
    }

    function moveRoomEditorInteraction(event) {
      if (!roomEditorState.mode || !roomFusionLayout?.interactiveNodes?.length) return;

      const sensor = roomFusionLayout.sensors.find(candidate => candidate.nodeId === roomEditorState.nodeId);
      if (!sensor) return;

      const point = roomCanvasPointFromEvent(event);
      const roomPoint = roomFusionLayout.unmapPoint(point);
      if (roomEditorState.mode === "move") {
        updateRoomEditorPoseInputs(roomPoint.x, roomPoint.y, Number(els.headingInput.value));
      } else if (roomEditorState.mode === "rotate") {
        const dx = roomPoint.x - Number(els.poseXInput.value || sensor.poseX || 0);
        const dy = roomPoint.y - Number(els.poseYInput.value || sensor.poseY || 0);
        const headingDeg = Math.atan2(dy, dx) * 180 / Math.PI;
        updateRoomEditorPoseInputs(Number(els.poseXInput.value || sensor.poseX || 0), Number(els.poseYInput.value || sensor.poseY || 0), headingDeg);
      } else if (roomEditorState.mode === "room-width") {
        updateRoomEditorBoundsInputs(roomPoint.x, Number(els.roomHeightInput.value || roomFusionLayout.roomHeightCm || 400));
      } else if (roomEditorState.mode === "room-height") {
        updateRoomEditorBoundsInputs(Number(els.roomWidthInput.value || roomFusionLayout.roomWidthCm || 600), roomPoint.y);
      }
      event.preventDefault();
    }

    function endRoomEditorInteraction(event) {
      if (!roomEditorState.mode) return;
      try {
        els.roomFusionCanvas.releasePointerCapture(event.pointerId);
      } catch (error) {
        // Ignore release failures when the pointer is already gone.
      }
      roomEditorState = { mode: null, nodeId: null };
    }

    els.connectButton.addEventListener("click", () => {
      connectSerial().catch(error => {
        console.error(error);
        setConnectionState("disconnected", "Connection failed");
        addEvent({ event: "connection_error", ms: performance.now(), error: String(error) });
        startSnapshotPolling();
      });
    });

    els.disconnectButton.addEventListener("click", () => disconnectSerial());
    els.clearButton.addEventListener("click", () => clearUi());
    els.energyButton.addEventListener("click", () => sendCommandText("energy").catch(error => addEvent({ event: "write_error", ms: performance.now(), error: String(error) })));
    els.statusButton.addEventListener("click", () => sendCommandText("status").catch(error => addEvent({ event: "write_error", ms: performance.now(), error: String(error) })));
    els.dashboardViewButton.addEventListener("click", () => setCurrentView("dashboard"));
    els.setupViewButton.addEventListener("click", () => setCurrentView("setup"));
    els.sensorsViewButton.addEventListener("click", () => setCurrentView("sensors"));
    els.consoleViewButton.addEventListener("click", () => setCurrentView("console"));
    els.dashboardOpenSetupButton.addEventListener("click", () => setCurrentView("setup"));
    els.dashboardOpenSensorsButton.addEventListener("click", () => setCurrentView("sensors"));
    els.dashboardOpenConsoleButton.addEventListener("click", () => setCurrentView("console"));
    els.firmwareSyncButton.addEventListener("click", () => {
      setTextContentIfChanged(els.firmwareSyncStatus, "Queuing firmware sync to the highest visible peer release...");
      sendCommandText("firmware_sync").catch(error => addEvent({ event: "write_error", ms: performance.now(), error: String(error) }));
    });
    els.wizardConnectButton.addEventListener("click", () => {
      if (port) return;
      els.connectButton.click();
    });
    els.wizardStatusButton.addEventListener("click", () => sendCommandText("status").catch(error => addEvent({ event: "write_error", ms: performance.now(), error: String(error) })));
    els.wizardScanWifiButton.addEventListener("click", () => scanWifiNetworks().catch(error => addEvent({ event: "write_error", ms: performance.now(), error: String(error) })));
    els.wizardSaveSetupButton.addEventListener("click", () => saveHomeAssistantSetup().catch(error => addEvent({ event: "write_error", ms: performance.now(), error: String(error) })));
    els.wizardOpenHaButton.addEventListener("click", () => oneClickAddToHomeAssistant().catch(error => addEvent({ event: "write_error", ms: performance.now(), error: String(error) })));
    els.wizardSaveTuningButton.addEventListener("click", () => saveAdvancedSettings().catch(error => addEvent({ event: "write_error", ms: performance.now(), error: String(error) })));
    els.wizardOpenSensorsButton.addEventListener("click", () => setCurrentView("sensors"));
    els.wizardOpenConsoleButton.addEventListener("click", () => setCurrentView("console"));
    els.copyObservationTopicButton.addEventListener("click", () => copyObservationTopic());
    els.wizardCopyObservationTopicButton.addEventListener("click", () => copyObservationTopic());
    els.unitsToggleButton.addEventListener("click", () => setUnitSystem(unitSystem === "metric" ? "imperial" : "metric"));
    els.settingsToggleButton.addEventListener("click", () => {
      settingsUserToggled = true;
      setSettingsOpen(!settingsOpen);
    });
    els.scanWifiButton.addEventListener("click", () => scanWifiNetworks().catch(error => addEvent({ event: "write_error", ms: performance.now(), error: String(error) })));
    els.saveHaButton.addEventListener("click", () => saveHomeAssistantSetup().catch(error => addEvent({ event: "write_error", ms: performance.now(), error: String(error) })));
    els.saveTuningButton.addEventListener("click", () => saveAdvancedSettings().catch(error => addEvent({ event: "write_error", ms: performance.now(), error: String(error) })));
    els.addToHaButton.addEventListener("click", () => oneClickAddToHomeAssistant().catch(error => addEvent({ event: "write_error", ms: performance.now(), error: String(error) })));
    els.openMosquittoButton.addEventListener("click", () => openMosquittoSetup());
    els.calibrationTargetSelect.addEventListener("change", () => {
      calibrationSession.targetNodeId = els.calibrationTargetSelect.value || "";
      renderCalibrationState();
    });
    els.calibrationStartButton.addEventListener("click", () => startCalibrationCapture());
    els.calibrationStopButton.addEventListener("click", () => stopCalibrationCapture());
    els.calibrationClearButton.addEventListener("click", () => clearCalibrationCapture());
    els.calibrationLoadButton.addEventListener("click", () => loadCalibrationSuggestion());
    els.calibrationPublishButton.addEventListener("click", () => publishCalibrationSuggestion().catch(error => addEvent({ event: "write_error", ms: performance.now(), error: String(error) })));
    els.roomEditorTargetInput.addEventListener("change", () => {
      applySelectedRoomEditorTarget();
      persistSetupFormState();
      setRoomEditorStatus(`Editing ${editorTargetLabel(getSelectedRoomEditorTarget())}.`);
    });
    els.roomSnapEnabledInput.addEventListener("change", () => {
      persistSetupFormState();
      drawRoomFusionView();
    });
    els.roomGridSizeInput.addEventListener("change", () => {
      persistSetupFormState();
      drawRoomFusionView();
    });
    els.sendButton.addEventListener("click", () => sendCommand().catch(error => addEvent({ event: "write_error", ms: performance.now(), error: String(error) })));
    els.roomFusionCanvas.addEventListener("pointerdown", beginRoomEditorInteraction);
    els.roomFusionCanvas.addEventListener("pointermove", moveRoomEditorInteraction);
    els.roomFusionCanvas.addEventListener("pointerup", endRoomEditorInteraction);
    els.roomFusionCanvas.addEventListener("pointercancel", endRoomEditorInteraction);

    [
      els.wifiSsidInput,
      els.wifiPasswordInput,
      els.mqttHostInput,
      els.mqttPortInput,
      els.mqttUserInput,
      els.mqttPasswordInput,
      els.nodeIdInput,
      els.friendlyNameInput,
      els.roomIdInput,
      els.sensorRoleInput,
      els.poseXInput,
      els.poseYInput,
      els.headingInput,
      els.roomWidthInput,
      els.roomHeightInput,
      els.maxRangeInput,
      els.minGateEnergyInput,
      els.sensitivityInput,
      els.presenceHoldInput,
      els.minActiveGatesInput,
      els.minActivityScoreInput,
      els.headingInput,
      els.ledBrightnessInput
    ].forEach(element => {
      element.addEventListener("input", () => {
        if (isEditingLocalNode()) {
          syncLocalRoomDraftFromInputs();
        }
        persistSetupFormState();
        drawRoomFusionView();
      });
    });

    els.wifiSelect.addEventListener("change", () => persistSetupFormState());
    els.ledEnabledInput.addEventListener("change", () => persistSetupFormState());

    [
      els.maxRangeInput,
      els.minGateEnergyInput,
      els.sensitivityInput,
      els.presenceHoldInput,
      els.minActiveGatesInput,
      els.minActivityScoreInput,
      els.ledBrightnessInput
    ].forEach(element => {
      element.addEventListener("input", () => {
        updateRangeDisplays();
        persistSetupFormState();
      });
    });

    els.commandInput.addEventListener("keydown", event => {
      if (event.key === "Enter") {
        sendCommand().catch(error => addEvent({ event: "write_error", ms: performance.now(), error: String(error) }));
      }
    });

    window.addEventListener("beforeunload", () => {
      if (port) disconnectSerial();
      disconnectDeviceSocket();
    });

    restoreSetupFormState();
    try {
      const savedUnitSystem = window.localStorage.getItem(LOCAL_STORAGE_UNITS_KEY);
      if (savedUnitSystem === "imperial" || savedUnitSystem === "metric") {
        unitSystem = savedUnitSystem;
      }
    } catch (error) {
      console.warn("Failed to restore unit system", error);
    }
    clearUi();
    setUnitSystem(unitSystem, false);
    setSettingsOpen(true);
    try {
      setCurrentView(window.localStorage.getItem(LOCAL_STORAGE_VIEW_KEY) || "dashboard", false);
    } catch (error) {
      setCurrentView("dashboard", false);
    }
    if (supportsNetworkTransport()) {
      setConnectionState("connecting", `Connecting to device @ ${window.location.host}`);
      setHaSetupStatus("Connected to the device UI. You can configure Wi-Fi and MQTT here without USB, or attach serial for direct access.");
      startSnapshotPolling();
      connectDeviceSocket();
    } else {
      setConnectionState("disconnected", "Disconnected");
      setHaSetupStatus("Connect over serial, scan Wi-Fi if needed, save settings, then open Home Assistant. The device should appear automatically under MQTT.");
    }
  </script>
</body>
</html>

)LBHTML";
