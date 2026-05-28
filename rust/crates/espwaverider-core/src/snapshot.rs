use alloc::string::String;
use alloc::vec::Vec;

use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct FirmwareMetadata {
    pub version: String,
    pub build_target: String,
    pub git_sha: String,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct WifiLinkSnapshot {
    pub connected: bool,
    pub ssid: String,
    pub rssi_dbm: i32,
    pub channel: u8,
    pub bssid: String,
    pub mac_address: String,
    pub subnet_mask: String,
    pub gateway_ip: String,
    pub dns_1: String,
    pub dns_2: String,
    pub broadcast_ip: String,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct FirmwareSyncSnapshot {
    pub local_version_core: String,
    pub highest_peer_node_id: String,
    pub highest_peer_version: String,
    pub highest_peer_source: String,
    pub sync_available: bool,
    pub in_progress: bool,
    pub pending: bool,
    pub target_version: String,
    pub target_node_id: String,
    pub target_source: String,
    pub download_url: String,
    pub status: String,
    pub last_error: String,
    pub last_started_ms: u32,
    pub last_completed_ms: u32,
    pub last_success: bool,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct UdpDiscoveryPeerSnapshot {
    pub node_id: String,
    pub friendly_name: String,
    pub room_id: String,
    pub sensor_role: String,
    pub firmware_version: String,
    pub hostname: String,
    pub ip_address: String,
    pub wifi_rssi_dbm: i32,
    pub wifi_channel: u8,
    pub uptime_s: u32,
    pub free_heap_bytes: u32,
    pub age_ms: u32,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct UdpDiscoverySnapshot {
    pub started: bool,
    pub port: u16,
    pub peer_count: u8,
    pub last_announce_ms: u32,
    pub peers: Vec<UdpDiscoveryPeerSnapshot>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct RoomPeerSnapshot {
    pub node_id: String,
    pub pose_x_cm: i32,
    pub pose_y_cm: i32,
    pub heading_deg: i32,
    pub room_width_cm: u16,
    pub room_height_cm: u16,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct BleBeaconSnapshot {
    pub address: String,
    pub name: String,
    pub service_uuid: String,
    pub rssi: i32,
    pub age_ms: u32,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct BleTagSnapshot {
    pub slot: Option<u8>,
    pub label: Option<String>,
    pub address: Option<String>,
    pub min_rssi: Option<i32>,
    pub rssi: Option<i32>,
    pub age_ms: Option<u32>,
    pub present: Option<bool>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct LatestEnergyFrameSnapshot {
    pub length: u16,
    pub payload_length: u16,
    pub presence: bool,
    pub distance_cm: u16,
    pub bytes_total: u32,
    pub frames_total: u32,
    pub energy_frames_total: u32,
    pub gates: Vec<u16>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct LatestTextFrameSnapshot {
    pub length: u16,
    pub presence: bool,
    pub range: i32,
    pub bytes_total: u32,
    pub frames_total: u32,
    pub hex: String,
    pub ascii: String,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct LatestGenericFrameSnapshot {
    pub length: u16,
    pub bytes_total: u32,
    pub frames_total: u32,
    pub hex: String,
    pub ascii: String,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct RuntimeBenchmarkMeasurementSnapshot {
    pub total_us: u32,
    pub per_iter_ns: u32,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct RuntimeBenchmarkSnapshot {
    pub measured_at_ms: u32,
    pub iterations: u32,
    pub parse_command_fixture: RuntimeBenchmarkMeasurementSnapshot,
    pub parse_room_config_fixture: RuntimeBenchmarkMeasurementSnapshot,
    pub parse_tuning_config_fixture: RuntimeBenchmarkMeasurementSnapshot,
    pub parse_generic_fixture: RuntimeBenchmarkMeasurementSnapshot,
    pub derive_metrics_fixture: RuntimeBenchmarkMeasurementSnapshot,
    pub detection_candidate_fixture: RuntimeBenchmarkMeasurementSnapshot,
    pub detection_candidate: bool,
    pub people_estimate: u8,
    pub active_gate_count: u8,
    pub activity_score: u8,
    pub dominant_gate_distance_cm: i32,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct DeviceSnapshot {
    pub enabled: bool,
    pub configured: bool,
    #[serde(default)]
    pub boot_count: u32,
    #[serde(default)]
    pub last_reset_reason: String,
    pub wifi_ssid: String,
    pub mqtt_host: String,
    pub mqtt_port: u16,
    pub mqtt_transport: String,
    pub mqtt_ws_path: String,
    pub mqtt_host_header: String,
    pub mqtt_username_set: bool,
    pub firmware_version: String,
    pub build_target: String,
    pub git_sha: String,
    pub node_id: String,
    pub friendly_name: String,
    pub room_id: String,
    pub sensor_role: String,
    pub pose_x_cm: i32,
    pub pose_y_cm: i32,
    pub heading_deg: i32,
    pub room_width_cm: u16,
    pub room_height_cm: u16,
    pub max_detection_range_cm: u16,
    pub min_gate_energy: u16,
    pub sensitivity_percent: u8,
    pub presence_hold_ms: u16,
    pub min_active_gates: u8,
    pub min_activity_score: u8,
    pub led_enabled: bool,
    pub led_brightness: u8,
    pub wifi_connected: bool,
    pub wifi_disconnect_reason: u16,
    pub wifi_disconnect_reason_text: String,
    pub ip_address: String,
    pub wifi_link: WifiLinkSnapshot,
    pub mqtt_connected: bool,
    pub mqtt_state: i32,
    pub mqtt_state_text: String,
    pub mqtt_host_ip: String,
    pub topic_prefix: String,
    pub device_hostname: String,
    pub dashboard_url: String,
    pub ap_ssid: String,
    pub ap_password: String,
    pub ap_ip: String,
    pub uptime_ms: u32,
    pub free_heap: u32,
    pub presence: bool,
    pub gpio_presence: bool,
    pub detection_candidate: bool,
    pub presence_decay_remaining_ms: u32,
    pub radar_bytes_total: u32,
    pub radar_frames_total: u32,
    pub ld2420_energy_frames_total: u32,
    pub presence_changes_total: u32,
    pub people_estimate: u8,
    pub active_gate_count: u8,
    pub activity_score: u8,
    pub dominant_gate_index: i32,
    pub dominant_gate_distance_cm: i32,
    pub dominant_gate_energy: u16,
    pub total_gate_energy: u32,
    pub status_led_hex: String,
    #[serde(default)]
    pub runtime_benchmark: Option<RuntimeBenchmarkSnapshot>,
    pub room_people_estimate: u8,
    pub room_active_nodes: u8,
    pub room_peer_nodes: u8,
    pub room_activity_score: u8,
    pub firmware_sync: FirmwareSyncSnapshot,
    pub udp_discovery: UdpDiscoverySnapshot,
    pub room_peers: Vec<RoomPeerSnapshot>,
    pub ble_beacon_count: u16,
    pub ble_beacons: Vec<BleBeaconSnapshot>,
    pub ble_tagged_people_count: u16,
    pub ble_tags: Vec<BleTagSnapshot>,
    pub latest_energy_frame: Option<LatestEnergyFrameSnapshot>,
    pub latest_text_frame: Option<LatestTextFrameSnapshot>,
    pub latest_generic_frame: Option<LatestGenericFrameSnapshot>,
}

impl DeviceSnapshot {
    pub fn firmware(&self) -> FirmwareMetadata {
        FirmwareMetadata {
            version: self.firmware_version.clone(),
            build_target: self.build_target.clone(),
            git_sha: self.git_sha.clone(),
        }
    }

    pub fn to_json(&self) -> Result<String, serde_json::Error> {
        serde_json::to_string(self)
    }

    pub fn from_json(json: &str) -> Result<Self, serde_json::Error> {
        serde_json::from_str(json)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use alloc::vec;

    fn fixture_snapshot() -> DeviceSnapshot {
        DeviceSnapshot {
            enabled: true,
            configured: true,
            boot_count: 12,
            last_reset_reason: "cpu_mwdt1".into(),
            wifi_ssid: "JDH-IoT".into(),
            mqtt_host: "10.0.107.46".into(),
            mqtt_port: 1883,
            mqtt_transport: "tcp".into(),
            mqtt_ws_path: "/mqtt".into(),
            mqtt_host_header: String::new(),
            mqtt_username_set: true,
            firmware_version: "v1.0.1".into(),
            build_target: "lonely-esp32-s3-devkitm-1".into(),
            git_sha: "4b1a5fc".into(),
            node_id: "lb_mmwave_presence_test2".into(),
            friendly_name: "LB mmWav979e7 Presence Test 2".into(),
            room_id: "room-default".into(),
            sensor_role: "auto".into(),
            pose_x_cm: 225,
            pose_y_cm: -50,
            heading_deg: 90,
            room_width_cm: 800,
            room_height_cm: 400,
            max_detection_range_cm: 1120,
            min_gate_energy: 25,
            sensitivity_percent: 55,
            presence_hold_ms: 4000,
            min_active_gates: 1,
            min_activity_score: 10,
            led_enabled: true,
            led_brightness: 32,
            wifi_connected: true,
            wifi_disconnect_reason: 0,
            wifi_disconnect_reason_text: "connected".into(),
            ip_address: "10.0.107.149".into(),
            wifi_link: WifiLinkSnapshot {
                connected: true,
                ssid: "JDH-IoT".into(),
                rssi_dbm: -45,
                channel: 1,
                bssid: "EA:38:83:12:FE:91".into(),
                mac_address: "3C:DC:75:71:53:D4".into(),
                subnet_mask: "255.255.255.0".into(),
                gateway_ip: "10.0.107.1".into(),
                dns_1: "10.0.107.1".into(),
                dns_2: "0.0.0.0".into(),
                broadcast_ip: "10.0.107.255".into(),
            },
            mqtt_connected: true,
            mqtt_state: 0,
            mqtt_state_text: "CONNECTED".into(),
            mqtt_host_ip: "10.0.107.46".into(),
            topic_prefix: "lb_mmwave/lb_mmwave_presence_test2".into(),
            device_hostname: "lb-mmwave-presence-test2".into(),
            dashboard_url: "http://lb-mmwave-presence-test2.local/".into(),
            ap_ssid: "LB-MMWave-lb-mmwave-presence-te".into(),
            ap_password: "lbmmw75dc3c".into(),
            ap_ip: "192.168.4.1".into(),
            uptime_ms: 21_409_598,
            free_heap: 164_380,
            presence: true,
            gpio_presence: false,
            detection_candidate: true,
            presence_decay_remaining_ms: 3_867,
            radar_bytes_total: 9_121_501,
            radar_frames_total: 189_635,
            ld2420_energy_frames_total: 189_631,
            presence_changes_total: 312,
            people_estimate: 0,
            active_gate_count: 2,
            activity_score: 100,
            dominant_gate_index: 0,
            dominant_gate_distance_cm: 35,
            dominant_gate_energy: 12_898,
            total_gate_energy: 17_335,
            status_led_hex: "002000".into(),
            runtime_benchmark: None,
            room_people_estimate: 1,
            room_active_nodes: 1,
            room_peer_nodes: 0,
            room_activity_score: 100,
            firmware_sync: FirmwareSyncSnapshot {
                local_version_core: "1.0.1".into(),
                highest_peer_node_id: "lb_mmwave_presence_test1".into(),
                highest_peer_version: "1.0.1".into(),
                highest_peer_source: "udp_discovery".into(),
                sync_available: false,
                in_progress: false,
                pending: false,
                target_version: String::new(),
                target_node_id: String::new(),
                target_source: String::new(),
                download_url: String::new(),
                status: String::new(),
                last_error: String::new(),
                last_started_ms: 0,
                last_completed_ms: 0,
                last_success: false,
            },
            udp_discovery: UdpDiscoverySnapshot {
                started: true,
                port: 42_110,
                peer_count: 1,
                last_announce_ms: 21_407_012,
                peers: vec![UdpDiscoveryPeerSnapshot {
                    node_id: "lb_mmwave_presence_test1".into(),
                    friendly_name: "LB mmWav979e7 Presence Test 1".into(),
                    room_id: "room-default".into(),
                    sensor_role: "auto".into(),
                    firmware_version: "v1.0.1".into(),
                    hostname: "lb-mmwave-presence-test1".into(),
                    ip_address: "10.0.107.148".into(),
                    wifi_rssi_dbm: -55,
                    wifi_channel: 1,
                    uptime_s: 7_304,
                    free_heap_bytes: 175_884,
                    age_ms: 5_677,
                }],
            },
            room_peers: Vec::new(),
            ble_beacon_count: 16,
            ble_beacons: vec![BleBeaconSnapshot {
                address: "78:6d:eb:36:1c:6a".into(),
                name: "D618F139706845E2".into(),
                service_uuid: String::new(),
                rssi: -85,
                age_ms: 87,
            }],
            ble_tagged_people_count: 0,
            ble_tags: Vec::new(),
            latest_energy_frame: Some(LatestEnergyFrameSnapshot {
                length: 45,
                payload_length: 35,
                presence: false,
                distance_cm: 0,
                bytes_total: 9_121_501,
                frames_total: 189_635,
                energy_frames_total: 189_631,
                gates: vec![12_898, 3_730, 362, 36, 80, 98, 20, 13, 13, 9, 10, 13, 20, 10, 13, 10],
            }),
            latest_text_frame: None,
            latest_generic_frame: Some(LatestGenericFrameSnapshot {
                length: 194,
                bytes_total: 4_231,
                frames_total: 76,
                hex: "110012000D000D000D00F8F7F6F5F4F3F2F123000169005257F113DD006801280014003A0014000D000A002000140011000D000D000A00F8F7F6F5F4F3F2F12300016900754AA511C901DD0048006400120049000D00140014001900110014000D000D00F8F7F6F5F4F3F2F12300016900A1414510610152001A001400120019001400110019001400140011000A001400F8F7F6F5F4F3F2F12300016900444B241328015A0028001D0050002D001A002900110014000D00110022001100F8F7F6F5".into(),
                ascii: "..................#..i.RW....h.(...:....... ...................#..i.uJ......H.d...I.........................#..i..AE.a.R.................................#..i.DK$.(.Z.(...P.-...).........\".......".into(),
            }),
        }
    }

    #[test]
    fn round_trips_device_snapshot_json() {
        let snapshot = fixture_snapshot();

        let json = snapshot.to_json().unwrap();
        let parsed = DeviceSnapshot::from_json(&json).unwrap();

        assert_eq!(parsed, snapshot);
    }

    #[test]
    fn exposes_firmware_metadata_view() {
        let snapshot = fixture_snapshot();

        assert_eq!(
            snapshot.firmware(),
            FirmwareMetadata {
                version: "v1.0.1".into(),
                build_target: "lonely-esp32-s3-devkitm-1".into(),
                git_sha: "4b1a5fc".into(),
            }
        );
    }
}
