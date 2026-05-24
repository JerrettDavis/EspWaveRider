const path = require('path');
const { pathToFileURL } = require('url');
const { test, expect } = require('@playwright/test');

const dashboardUrl = pathToFileURL(path.resolve(__dirname, '../src/visualizer/index.html')).href;

function makeBaseSnapshot(overrides = {}) {
  return {
    firmware_version: 'v1.0.0',
    build_target: 'esp32-s3-devkitm-1',
    git_sha: '027df90',
    node_id: 'lb_mmwave_presence_test1',
    device_hostname: 'lb-mmwave-presence-test1',
    room_id: 'room-default',
    sensor_role: 'auto',
    pose_x_cm: 0,
    pose_y_cm: 100,
    heading_deg: 5,
    room_width_cm: 600,
    room_height_cm: 400,
    presence: true,
    gpio_presence: true,
    detection_candidate: true,
    presence_decay_remaining_ms: 3900,
    people_estimate: 1,
    active_gate_count: 2,
    activity_score: 100,
    dominant_gate_distance_cm: 35,
    dominant_gate_energy: 11080,
    total_gate_energy: 18473,
    room_people_estimate: 1,
    room_active_nodes: 1,
    room_peer_nodes: 2,
    room_activity_score: 100,
    ble_beacon_count: 0,
    ble_beacons: [],
    wifi_connected: true,
    wifi_disconnect_reason: 0,
    wifi_disconnect_reason_text: 'connected',
    ip_address: '10.0.107.148',
    wifi_link: {
      connected: true,
      ssid: 'JDH-IoT',
      rssi_dbm: -45,
      channel: 1,
      bssid: 'EA:38:83:12:FE:91',
      mac_address: '3C:DC:75:71:53:DC',
      subnet_mask: '255.255.255.0',
      gateway_ip: '10.0.107.1',
      dns_1: '10.0.107.1',
      dns_2: '0.0.0.0',
      broadcast_ip: '10.0.107.255'
    },
    mqtt_connected: true,
    mqtt_state: 0,
    mqtt_state_text: 'CONNECTED',
    mqtt_host_ip: '10.0.107.46',
    topic_prefix: 'lb_mmwave/lb_mmwave_presence_test1',
    firmware_sync: {
      local_version_core: '1.0.0',
      highest_peer_node_id: 'lb_mmwave_presence_test2',
      highest_peer_version: '1.0.1',
      highest_peer_source: 'room_summary',
      sync_available: true,
      in_progress: false,
      pending: false,
      target_version: '',
      target_node_id: '',
      target_source: '',
      download_url: '',
      status: '',
      last_error: '',
      last_started_ms: 0,
      last_completed_ms: 0,
      last_success: false
    },
    room_peers: [
      {
        node_id: 'lb_mmwave_presence_test2',
        friendly_name: 'LB mmWave Presence Test 2',
        room_id: 'room-default',
        sensor_role: 'auto',
        firmware_version: 'v1.0.1',
        hostname: 'lb-mmwave-presence-test2',
        ip_address: '10.0.107.149',
        presence: true,
        detection_candidate: false,
        people_estimate: 0,
        active_gate_count: 2,
        dominant_gate_distance_cm: 35,
        activity_score: 100,
        pose_x_cm: 225,
        pose_y_cm: -50,
        heading_deg: 90,
        room_width_cm: 800,
        room_height_cm: 400,
        relative_angle_guess_deg: 45,
        relative_angle_confidence_percent: 20,
        relative_offset_x_cm: 15,
        relative_offset_y_cm: 0,
        distance_delta_cm: 0,
        freshness_ms: 1500
      },
      {
        node_id: 'lb_mmwave_presence_test3',
        friendly_name: 'LB mmWave Presence Test 3',
        room_id: 'room-default',
        sensor_role: 'auto',
        firmware_version: 'v1.0.0',
        hostname: 'lb-mmwave-presence-test3',
        ip_address: '10.0.107.150',
        presence: false,
        detection_candidate: false,
        people_estimate: 0,
        active_gate_count: 1,
        dominant_gate_distance_cm: 80,
        activity_score: 18,
        pose_x_cm: 50,
        pose_y_cm: 25,
        heading_deg: -90,
        room_width_cm: 800,
        room_height_cm: 400,
        freshness_ms: 2500
      }
    ],
    udp_discovery: {
      started: true,
      port: 42110,
      peer_count: 2,
      last_announce_ms: 120000,
      peers: [
        {
          node_id: 'lb_mmwave_presence_test2',
          friendly_name: 'LB mmWave Presence Test 2',
          room_id: 'room-default',
          sensor_role: 'auto',
          firmware_version: 'v1.0.1',
          hostname: 'lb-mmwave-presence-test2',
          ip_address: '10.0.107.149',
          wifi_rssi_dbm: -42,
          wifi_channel: 1,
          uptime_s: 1622,
          free_heap_bytes: 177508,
          age_ms: 900
        },
        {
          node_id: 'lb_mmwave_presence_test3',
          friendly_name: 'LB mmWave Presence Test 3',
          room_id: 'room-default',
          sensor_role: 'auto',
          firmware_version: 'v1.0.0',
          hostname: 'lb-mmwave-presence-test3',
          ip_address: '10.0.107.150',
          wifi_rssi_dbm: -50,
          wifi_channel: 6,
          uptime_s: 900,
          free_heap_bytes: 165000,
          age_ms: 1800
        }
      ]
    },
    latest_energy_frame: {
      length: 90,
      payload_length: 35,
      presence: true,
      distance_cm: 105,
      bytes_total: 49321,
      frames_total: 708,
      energy_frames_total: 647,
      gates: [11080, 4964, 1313, 565, 185, 101, 80, 50, 13, 20, 29, 18, 13, 13, 16, 13]
    },
    latest_text_frame: null,
    latest_generic_frame: null,
    ...overrides
  };
}

async function applySnapshot(page, snapshot) {
  await page.evaluate((value) => {
    window.applyDeviceSnapshot(value);
  }, snapshot);
}

test.describe('offline dashboard', () => {
  test.beforeEach(async ({ page }) => {
    await page.goto(dashboardUrl, { waitUntil: 'domcontentloaded' });
  });

  test('renders version mismatch, firmware sync candidate, and peer links from a snapshot', async ({ page }) => {
    await applySnapshot(page, makeBaseSnapshot());

    await expect(page.locator('#firmwareVersionText')).toHaveText('Firmware: v1.0.0 (esp32-s3-devkitm-1)');
    await expect(page.locator('#peerVersionPill')).toBeVisible();
    await expect(page.locator('#peerVersionPillText')).toHaveText('1 peer version mismatch');
    await expect(page.locator('#versionBanner')).toHaveClass(/visible/);
    await expect(page.locator('#versionBannerText')).toContainText('lb_mmwave_presence_test2 on v1.0.1');

    await expect(page.locator('#firmwareSyncCandidate')).toHaveText('Highest peer release: 1.0.1 from lb_mmwave_presence_test2 via room_summary');
    await expect(page.locator('#firmwareSyncStatus')).toHaveText('A newer peer release (1.0.1) is available for this node.');
    await expect(page.locator('#firmwareSyncButton')).toBeEnabled();

    await expect(page.locator('#peerLinkSummary')).toHaveText('2 peer nodes on the network');
    await expect(page.locator('#peerLinkList')).toContainText('lb-mmwave-presence-test2.local');
    await expect(page.locator('#peerLinkList')).toContainText('10.0.107.149');
    await expect(page.locator('#peerLinkList')).toContainText('lb-mmwave-presence-test3.local');
  });

  test('populates setup editor and calibration peer targets from room peers', async ({ page }) => {
    await applySnapshot(page, makeBaseSnapshot());
    await page.click('#setupViewButton');

    await expect(page.locator('#roomEditorTargetInput')).toBeVisible();
    await expect(page.locator('#roomEditorTargetInput option')).toHaveCount(3);
    await expect(page.locator('#roomEditorTargetInput option').nth(0)).toHaveText('Local node (lb_mmwave_presence_test1)');
    await expect(page.locator('#roomEditorTargetInput option').nth(1)).toHaveText('lb_mmwave_presence_test2 (auto)');
    await expect(page.locator('#roomEditorTargetInput option').nth(2)).toHaveText('lb_mmwave_presence_test3 (auto)');

    await expect(page.locator('#calibrationTargetSelect option')).toHaveCount(3);
    await expect(page.locator('#calibrationTargetSelect option').nth(0)).toHaveText('Select a room peer');
    await expect(page.locator('#calibrationTargetSelect option').nth(1)).toHaveText('lb_mmwave_presence_test2 (auto)');
    await expect(page.locator('#calibrationTargetSelect option').nth(2)).toHaveText('lb_mmwave_presence_test3 (auto)');
  });

  test('renders firmware sync state transitions without live hardware', async ({ page }) => {
    await applySnapshot(page, makeBaseSnapshot());
    await expect(page.locator('#firmwareSyncButton')).toBeEnabled();

    await applySnapshot(page, makeBaseSnapshot({
      firmware_sync: {
        ...makeBaseSnapshot().firmware_sync,
        pending: true,
        target_version: '1.0.1',
        status: 'Queued firmware sync to 1.0.1'
      }
    }));
    await expect(page.locator('#firmwareSyncButton')).toBeDisabled();
    await expect(page.locator('#firmwareSyncStatus')).toHaveText('Queued firmware sync to 1.0.1');

    await applySnapshot(page, makeBaseSnapshot({
      firmware_sync: {
        ...makeBaseSnapshot().firmware_sync,
        in_progress: true,
        target_version: '1.0.1',
        status: 'Verified release checksum 4d62bf83b9a5..., downloading firmware.'
      }
    }));
    await expect(page.locator('#firmwareSyncStatus')).toContainText('downloading firmware');

    await applySnapshot(page, makeBaseSnapshot({
      firmware_sync: {
        ...makeBaseSnapshot().firmware_sync,
        sync_available: false,
        target_version: '1.0.1',
        last_error: 'download_timeout',
        status: 'Firmware sync failed: download_timeout',
        last_started_ms: 100,
        last_completed_ms: 200
      }
    }));
    await expect(page.locator('#firmwareSyncButton')).toBeDisabled();
    await expect(page.locator('#firmwareSyncStatus')).toHaveText('Last sync result: download_timeout');

    await applySnapshot(page, makeBaseSnapshot({
      firmware_version: 'v1.0.1',
      git_sha: '4b1a5fc',
      firmware_sync: {
        local_version_core: '1.0.1',
        highest_peer_node_id: 'lb_mmwave_presence_test2',
        highest_peer_version: '1.0.1',
        highest_peer_source: 'room_summary',
        sync_available: false,
        in_progress: false,
        pending: false,
        target_version: '',
        target_node_id: '',
        target_source: '',
        download_url: '',
        status: 'Already aligned with the highest visible peer release.',
        last_error: '',
        last_started_ms: 100,
        last_completed_ms: 300,
        last_success: true
      },
      room_peers: [
        {
          ...makeBaseSnapshot().room_peers[0],
          firmware_version: 'v1.0.1'
        },
        {
          ...makeBaseSnapshot().room_peers[1],
          firmware_version: 'v1.0.1'
        }
      ],
      udp_discovery: {
        ...makeBaseSnapshot().udp_discovery,
        peers: makeBaseSnapshot().udp_discovery.peers.map((peer) => ({
          ...peer,
          firmware_version: 'v1.0.1'
        }))
      }
    }));
    await expect(page.locator('#peerVersionPill')).toHaveAttribute('hidden', '');
    await expect(page.locator('#versionBanner')).not.toHaveClass(/visible/);
    await expect(page.locator('#firmwareSyncStatus')).toHaveText('Already aligned with the highest visible peer release.');
  });
});