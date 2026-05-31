#![no_std]
#![no_main]

extern crate alloc;

mod bench;

esp_bootloader_esp_idf::esp_app_desc!();

use alloc::string::{String, ToString};
use alloc::boxed::Box;
use alloc::vec::Vec;
#[cfg(feature = "https-ota")]
use alloc::ffi::CString;
use core::cell::RefCell;
#[cfg(feature = "https-ota")]
use core::ffi::CStr;
#[cfg(not(feature = "usb-console"))]
use core::marker::PhantomData;
use core::sync::atomic::{AtomicBool, Ordering};
use embedded_storage::{ReadStorage, Storage};
use embedded_storage::nor_flash::{ErrorType, MultiwriteNorFlash, NorFlash, ReadNorFlash};
use embassy_net::{
    Config as NetConfig, IpAddress, IpEndpoint, Ipv4Address, Ipv4Cidr, Stack as NetStack,
    StackResources as NetStackResources, StaticConfigV4,
};
use embassy_net::dns::DnsQueryType;
use embassy_net::tcp::TcpSocket;
use embassy_net::udp::{PacketMetadata as UdpPacketMetadata, UdpSocket};
use embassy_executor::Spawner;
#[cfg(feature = "ble-scan")]
use embassy_futures::join::join;
use embassy_futures::{select::{select, select3, Either, Either3}, yield_now};
use embassy_sync::{
    blocking_mutex::raw::CriticalSectionRawMutex,
    channel::Channel as SyncChannel,
    mutex::Mutex,
};
use embassy_time::{Duration, Timer, with_timeout};
use esp_alloc as _;
use esp_backtrace as _;
use esp_bootloader_esp_idf::ota_updater::OtaUpdater;
use esp_bootloader_esp_idf::partitions::{
    DataPartitionSubType, PartitionType, PARTITION_TABLE_MAX_LEN, read_partition_table,
};
use esp_hal::{
    Blocking,
    clock::CpuClock,
    efuse,
    gpio::{Input, InputConfig, Level, Pull},
    interrupt::software::SoftwareInterruptControl,
    rtc_cntl::{SocResetReason, reset_reason},
    rmt::{Channel, PulseCode, Rmt, Tx, TxChannelConfig, TxChannelCreator},
    system::Cpu,
    time::{Instant, Rate},
    timer::timg::TimerGroup,
    uart::{Config as UartConfig, Uart},
};
#[cfg(feature = "https-ota")]
use esp_hal::rng::{Trng, TrngSource};
#[cfg(feature = "https-ota")]
use mbedtls_rs::io::{
    Error as TlsIoError, ErrorKind as TlsIoErrorKind, ErrorType as TlsErrorType,
    Read as TlsRead, Write as TlsWrite,
};
#[cfg(feature = "https-ota")]
use mbedtls_rs::{Certificate, ClientSessionConfig, Session, SessionConfig, SessionError, Tls, TlsReference, TlsVersion, X509};
use esp_nvs::{Key, Nvs};
#[cfg(feature = "usb-console")]
use esp_hal::usb_serial_jtag::UsbSerialJtag;
use esp_radio::wifi::{
    self as radio_wifi,
    ap::{AccessPointConfig, AccessPointInfo},
    scan::{ScanConfig, ScanTypeConfig},
    sta::StationConfig,
    AuthenticationMethod as WifiAuthenticationMethod,
    Config as RadioWifiConfig,
    ControllerConfig as RadioWifiControllerConfig,
    Interface as WifiInterface,
    PowerSaveMode,
    WifiController,
};
use esp_storage::FlashStorage;
use espwaverider_core::{
    ble::{
        BLE_TAG_DEFAULT_MIN_RSSI, BLE_TAG_FRESHNESS_MS,
        BleBeaconSightingState, BleIdentityTagState, ble_beacon_sighting_active,
        ble_identity_tag_present, clear_ble_identity_tag, normalize_ble_identity_value,
    },
    board::{PresencePinMode, ESP32_S3_DEVKITM_1},
    command::{
        parse_device_command, BleTagClearPayload, BleTagConfigPayload, DeviceCommand, HomeAssistantConfigPayload,
        HomeAssistantMqttEndpointPayload, HomeAssistantWebSocketConfigPayload,
        RoomConfigPayload, RoomPosePublishPayload, TuningConfigPayload,
    },
    metrics::{
        build_radar_derived_metrics, effective_min_activity_score, effective_min_gate_energy,
        radar_detection_decision, RadarDerivedMetrics, RadarDetectionDecision, RadarTuning,
    },
    mqtt::{looks_like_room_summary_payload, mqtt_room_summary_payload_from_publish},
    radar::{parse_radar_frame, EnergyFrame, GenericFrame, RadarFrame, TextFrame},
    release::{
        firmware_release_asset_url, firmware_release_asset_urls, parse_download_url, DownloadUrlScheme,
    },
    snapshot::{
        BleBeaconSnapshot, BleTagSnapshot, DeviceSnapshot, FirmwareSyncSnapshot,
        LatestEnergyFrameSnapshot, LatestGenericFrameSnapshot, LatestTextFrameSnapshot,
        RoomPeerSnapshot, RuntimeBenchmarkSnapshot, UdpDiscoveryPeerSnapshot,
        UdpDiscoverySnapshot, WifiLinkSnapshot,
    },
};
#[cfg(feature = "ble-scan")]
use espwaverider_core::ble::{
    extract_ble_local_name, extract_ble_service_uuid, record_ble_advertisement,
};
use smoltcp::wire::{DhcpMessageType, DhcpPacket, DhcpRepr};
use static_cell::StaticCell;
#[cfg(feature = "ble-scan")]
use trouble_host::prelude::{
    Address as TroubleAddress, DefaultPacketPool, EventHandler as TroubleEventHandler,
    ExternalController, HostResources, PhySet, ScanConfig as BleScanConfig, Scanner,
};

#[cfg(not(feature = "usb-console"))]
struct UsbSerialJtag<'a, M> {
    _marker: PhantomData<(&'a (), M)>,
}

#[cfg(not(feature = "usb-console"))]
impl<'a, M> UsbSerialJtag<'a, M> {
    fn new() -> Self {
        Self {
            _marker: PhantomData,
        }
    }

    fn read_byte(&mut self) -> Result<u8, ()> {
        Err(())
    }

    fn write(&mut self, bytes: &[u8]) -> Result<usize, ()> {
        Ok(bytes.len())
    }

    fn flush_tx(&mut self) -> Result<(), ()> {
        Ok(())
    }
}

const RUST_FIRMWARE_VERSION: &str = env!("CARGO_PKG_VERSION");
const RUST_BUILD_TARGET: &str = "lonely-esp32-s3-devkitm-1";
const RUST_GIT_SHA: &str = "rust-port";
const FIRMWARE_RELEASE_REPO_OWNER: &str = "JerrettDavis";
const FIRMWARE_RELEASE_REPO_NAME: &str = "EspWaveRider";
const DEFAULT_NODE_ID_PREFIX: &str = "lb_mmwave_presence";
const DEFAULT_FRIENDLY_NAME_PREFIX: &str = "LB mmWave Presence";
const DEFAULT_ROOM_ID: &str = "room-default";
const DEFAULT_SENSOR_ROLE: &str = "auto";
const DEFAULT_MQTT_PORT: u16 = 1883;
const DEFAULT_MQTT_WEBSOCKET_PATH: &str = "/mqtt";
const DEFAULT_TOPIC_PREFIX: &str = "lb_mmwave";
const DEFAULT_AP_IP: &str = "192.168.4.1";
const DEFAULT_WIFI_CONNECTING_REASON_TEXT: &str = "connecting";
const DEFAULT_WIFI_DISCONNECT_REASON_TEXT: &str = "not_connected";
const DEFAULT_WIFI_NOT_CONFIGURED_REASON_TEXT: &str = "not_configured";
const DEVICE_MONITOR_PAGE: &str = include_str!("../../../../src/visualizer/index.html");
const SETTINGS_NAMESPACE: Key = Key::from_str("ha");
const SETTINGS_KEY_ENABLED: Key = Key::from_str("enabled");
const SETTINGS_KEY_WIFI_SSID: Key = Key::from_str("wifi_ssid");
const SETTINGS_KEY_WIFI_PASS: Key = Key::from_str("wifi_pass");
const SETTINGS_KEY_MQTT_HOST: Key = Key::from_str("mqtt_host");
const SETTINGS_KEY_MQTT_PORT: Key = Key::from_str("mqtt_port");
const SETTINGS_KEY_MQTT_USER: Key = Key::from_str("mqtt_user");
const SETTINGS_KEY_MQTT_PASS: Key = Key::from_str("mqtt_pass");
const SETTINGS_KEY_MQTT_WS: Key = Key::from_str("mqtt_ws");
const SETTINGS_KEY_MQTT_WSP: Key = Key::from_str("mqtt_wsp");
const SETTINGS_KEY_MQTT_HDR: Key = Key::from_str("mqtt_hdr");
const SETTINGS_KEY_NODE_ID: Key = Key::from_str("node_id");
const SETTINGS_KEY_FRIENDLY: Key = Key::from_str("friendly");
const SETTINGS_KEY_ROOM_ID: Key = Key::from_str("room_id");
const SETTINGS_KEY_SENSOR_ROLE: Key = Key::from_str("sensor_role");
const SETTINGS_KEY_POSE_X: Key = Key::from_str("pose_x");
const SETTINGS_KEY_POSE_Y: Key = Key::from_str("pose_y");
const SETTINGS_KEY_POSE_H: Key = Key::from_str("pose_h");
const SETTINGS_KEY_ROOM_W: Key = Key::from_str("room_w");
const SETTINGS_KEY_ROOM_H: Key = Key::from_str("room_h");
const SETTINGS_KEY_MAX_RANGE: Key = Key::from_str("max_range");
const SETTINGS_KEY_MIN_ENERGY: Key = Key::from_str("min_energy");
const SETTINGS_KEY_SENSE_PCT: Key = Key::from_str("sense_pct");
const SETTINGS_KEY_HOLD_MS: Key = Key::from_str("hold_ms");
const SETTINGS_KEY_MIN_GATES: Key = Key::from_str("min_gates");
const SETTINGS_KEY_MIN_ACT: Key = Key::from_str("min_act");
const SETTINGS_KEY_LED_ON: Key = Key::from_str("led_on");
const SETTINGS_KEY_LED_BRI: Key = Key::from_str("led_bri");
const SETTINGS_KEY_BOOT_COUNT: Key = Key::from_str("boot_cnt");
const MAX_BLE_SIGHTINGS: usize = 16;
const MAX_BLE_TAGS: usize = 8;
#[cfg(feature = "ble-scan")]
const MAX_BLE_ADV_DATA_LEN: usize = 31;
#[cfg(feature = "ble-scan")]
const BLE_SCAN_INTERVAL_MS: u64 = 2000;
#[cfg(feature = "ble-scan")]
const BLE_SCAN_WINDOW_MS: u64 = 100;
#[cfg(feature = "ble-scan")]
const ENABLE_BLE_SCANNER_TASK: bool = false;
const DEFAULT_AP_NETMASK: Ipv4Address = Ipv4Address::new(255, 255, 255, 0);
const DEFAULT_AP_GATEWAY: Ipv4Address = Ipv4Address::new(192, 168, 4, 1);
const DEFAULT_DHCP_LEASE_SECS: u32 = 86_400;
const DEFAULT_DHCP_RENEW_SECS: u32 = 43_200;
const DEFAULT_DHCP_REBIND_SECS: u32 = 75_600;
const DHCP_SERVER_PORT: u16 = 67;
const DHCP_CLIENT_PORT: u16 = 68;
const HTTP_SERVER_PORT: u16 = 80;
const HTTP_SOCKET_BUFFER_SIZE: usize = 2048;
const AP_NET_STACK_SOCKETS: usize = 4;
const STATION_NET_STACK_SOCKETS: usize = 6;
const MAX_UDP_DISCOVERY_PEERS: usize = 12;
const UDP_DISCOVERY_PORT: u16 = 42110;
const UDP_DISCOVERY_ANNOUNCE_MS: u32 = 5_000;
const UDP_DISCOVERY_PEER_FRESHNESS_MS: u32 = 20_000;
const ROOM_SUMMARY_KEEPALIVE_MS: u32 = 10_000;
const ROOM_SUMMARY_PEER_FRESHNESS_MS: u32 = 20_000;
const MQTT_STATE_DISCONNECTED: i32 = -1;
const MQTT_STATE_UNSUPPORTED_TRANSPORT: i32 = -2;
const MQTT_STATE_INVALID_HOST: i32 = -3;
const MQTT_STATE_CONNECT_ERROR: i32 = -4;
const MQTT_STATE_PROTOCOL_ERROR: i32 = -5;
const MQTT_KEEPALIVE_SECS: u16 = 30;
const MQTT_SOCKET_BUFFER_SIZE: usize = 1024;
const MQTT_PACKET_ID_SUBSCRIBE: u16 = 1;
const MQTT_SUMMARY_PUBLISH_MS: u32 = 5_000;
const FIRMWARE_HTTP_HEADER_BUFFER_SIZE: usize = 8192;
const FIRMWARE_HTTP_READ_BUFFER_SIZE: usize = 2048;
const FIRMWARE_SYNC_MAX_REDIRECTS: usize = 4;
#[cfg(feature = "https-ota")]
const OTA_CA_BUNDLE: &CStr = match CStr::from_bytes_with_nul(
    concat!(include_str!("ota_ca_bundle.pem"), "\0").as_bytes(),
) {
    Ok(bundle) => bundle,
    Err(_) => panic!("ota_ca_bundle.pem is not a valid PEM bundle"),
};
const WIFI_CONNECT_TIMEOUT_MS: u64 = 5_000;
const BOARD_PROFILE: espwaverider_core::board::BoardProfile = ESP32_S3_DEVKITM_1;
const PRESENCE_POLL_MS: u32 = 25;
const WIFI_CONNECT_RETRY_MS: u32 = 10_000;
const WIFI_STATUS_POLL_MS: u32 = 1_000;
const RADAR_IDLE_FRAME_GAP_MS: u32 = 20;
const MIN_PRESENCE_HOLD_MS: u16 = 3000;
const MAX_PRESENCE_HOLD_MS: u16 = 60_000;
const RADAR_FRAME_BUFFER_SIZE: usize = 256;
const STATUS_LED_BUFFER_SIZE: usize = 3 * 8 + 1;
const STATUS_LED_CODE_PERIOD_NS: u32 = 1250;
const STATUS_LED_T0H_NS: u32 = 400;
const STATUS_LED_T0L_NS: u32 = STATUS_LED_CODE_PERIOD_NS - STATUS_LED_T0H_NS;
const STATUS_LED_T1H_NS: u32 = 850;
const STATUS_LED_T1L_NS: u32 = STATUS_LED_CODE_PERIOD_NS - STATUS_LED_T1H_NS;
const LD2420_ENABLE_CONFIG: &[u8] = &[
    0xFD, 0xFC, 0xFB, 0xFA, 0x04, 0x00, 0xFF, 0x00, 0x02, 0x00, 0x04, 0x03, 0x02, 0x01,
];
const LD2420_SET_ENERGY_MODE: &[u8] = &[
    0xFD, 0xFC, 0xFB, 0xFA, 0x08, 0x00, 0x12, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
    0x04, 0x03, 0x02, 0x01,
];
const LD2420_DISABLE_CONFIG: &[u8] = &[
    0xFD, 0xFC, 0xFB, 0xFA, 0x02, 0x00, 0xFE, 0x00, 0x04, 0x03, 0x02, 0x01,
];

static AP_NET_RESOURCES: StaticCell<NetStackResources<AP_NET_STACK_SOCKETS>> = StaticCell::new();
static STATION_NET_RESOURCES: StaticCell<NetStackResources<STATION_NET_STACK_SOCKETS>> =
    StaticCell::new();
static SETTINGS_FLASH_STORAGE: StaticCell<RefCell<FlashStorage<'static>>> = StaticCell::new();
#[cfg(feature = "https-ota")]
static TLS_ENTROPY_READY: AtomicBool = AtomicBool::new(false);
static DHCP_RX_META: StaticCell<[UdpPacketMetadata; 4]> = StaticCell::new();
static DHCP_TX_META: StaticCell<[UdpPacketMetadata; 4]> = StaticCell::new();
static DHCP_RX_BUFFER: StaticCell<[u8; 1536]> = StaticCell::new();
static DHCP_TX_BUFFER: StaticCell<[u8; 1536]> = StaticCell::new();
static AP_HTTP_RX_BUFFER: StaticCell<[u8; HTTP_SOCKET_BUFFER_SIZE]> = StaticCell::new();
static AP_HTTP_TX_BUFFER: StaticCell<[u8; HTTP_SOCKET_BUFFER_SIZE]> = StaticCell::new();
static STATION_HTTP_RX_BUFFER: StaticCell<[u8; HTTP_SOCKET_BUFFER_SIZE]> = StaticCell::new();
static STATION_HTTP_TX_BUFFER: StaticCell<[u8; HTTP_SOCKET_BUFFER_SIZE]> = StaticCell::new();
static STATION_DISCOVERY_RX_META: StaticCell<[UdpPacketMetadata; 8]> = StaticCell::new();
static STATION_DISCOVERY_TX_META: StaticCell<[UdpPacketMetadata; 8]> = StaticCell::new();
static STATION_DISCOVERY_RX_BUFFER: StaticCell<[u8; 2048]> = StaticCell::new();
static STATION_DISCOVERY_TX_BUFFER: StaticCell<[u8; 2048]> = StaticCell::new();
static STATION_MQTT_RX_BUFFER: StaticCell<[u8; MQTT_SOCKET_BUFFER_SIZE]> = StaticCell::new();
static STATION_MQTT_TX_BUFFER: StaticCell<[u8; MQTT_SOCKET_BUFFER_SIZE]> = StaticCell::new();
static HTTP_REQUEST_CHANNEL: SyncChannel<CriticalSectionRawMutex, HttpRequestMessage, 1> =
    SyncChannel::new();
static HTTP_RESPONSE_CHANNEL: SyncChannel<CriticalSectionRawMutex, String, 1> = SyncChannel::new();
static UDP_DISCOVERY_PACKET_CHANNEL: SyncChannel<
    CriticalSectionRawMutex,
    heapless::String<512>,
    4,
> = SyncChannel::new();
static UDP_DISCOVERY_ANNOUNCE_CHANNEL: SyncChannel<
    CriticalSectionRawMutex,
    heapless::String<384>,
    1,
> = SyncChannel::new();
static MQTT_COMMAND_CHANNEL: SyncChannel<CriticalSectionRawMutex, MqttTaskCommand, 4> =
    SyncChannel::new();
static MQTT_EVENT_CHANNEL: SyncChannel<CriticalSectionRawMutex, MqttTaskEvent, 8> =
    SyncChannel::new();
#[cfg(feature = "ble-scan")]
static BLE_SCAN_EVENT_CHANNEL: SyncChannel<CriticalSectionRawMutex, BleScanEventMessage, 16> =
    SyncChannel::new();
static UDP_DISCOVERY_STARTED: Mutex<CriticalSectionRawMutex, bool> = Mutex::new(false);
static MQTT_TASK_STARTED: AtomicBool = AtomicBool::new(false);

#[derive(Debug, Clone, PartialEq, Eq, Default)]
struct MqttTaskConfig {
    wifi_connected: bool,
    enabled: bool,
    use_websockets: bool,
    host: heapless::String<96>,
    port: u16,
    username: heapless::String<64>,
    password: heapless::String<64>,
    room_id: heapless::String<64>,
    node_id: heapless::String<64>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
struct MqttPublishMessage {
    topic: heapless::String<128>,
    payload: heapless::String<384>,
    retain: bool,
}

#[derive(Debug, Clone, PartialEq, Eq)]
enum MqttTaskCommand {
    Configure(MqttTaskConfig),
    Publish(MqttPublishMessage),
}

#[derive(Debug, Clone, PartialEq, Eq, Default)]
struct MqttTaskStatus {
    connected: bool,
    state: i32,
    state_text: heapless::String<32>,
    host_ip: heapless::String<32>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
enum MqttTaskEvent {
    Status(MqttTaskStatus),
    RoomSummary(heapless::String<512>),
}

#[cfg(feature = "ble-scan")]
#[derive(Debug, Clone, PartialEq, Eq)]
struct BleScanEventMessage {
    address: [u8; 6],
    rssi: i8,
    adv_data: heapless::Vec<u8, MAX_BLE_ADV_DATA_LEN>,
    scan_data: heapless::Vec<u8, MAX_BLE_ADV_DATA_LEN>,
}

#[cfg(feature = "ble-scan")]
struct BleScanHandler {
    last_adv: RefCell<Option<(TroubleAddress, heapless::Vec<u8, MAX_BLE_ADV_DATA_LEN>)>>,
}

#[cfg(feature = "ble-scan")]
impl BleScanHandler {
    fn new() -> Self {
        Self {
            last_adv: RefCell::new(None),
        }
    }

    fn handle_report(&self, address: TroubleAddress, rssi: i8, scan_response: bool, data: &[u8]) {
        let mut copied = heapless::Vec::<u8, MAX_BLE_ADV_DATA_LEN>::new();
        let max_len = data.len().min(MAX_BLE_ADV_DATA_LEN);
        let _ = copied.extend_from_slice(&data[..max_len]);

        let (adv_data, scan_data) = if scan_response {
            let cached = self.last_adv.borrow_mut().take();
            match cached {
                Some((cached_address, cached_adv_data)) if cached_address == address => {
                    (cached_adv_data, copied)
                }
                _ => return,
            }
        } else {
            *self.last_adv.borrow_mut() = Some((address, copied.clone()));
            (copied, heapless::Vec::new())
        };

        let _ = BLE_SCAN_EVENT_CHANNEL.try_send(BleScanEventMessage {
            address: address.addr.into_inner(),
            rssi,
            adv_data,
            scan_data,
        });
    }
}

#[cfg(feature = "ble-scan")]
impl TroubleEventHandler for BleScanHandler {
    fn on_adv_reports(&self, reports: bt_hci::param::LeAdvReportsIter) {
        for report in reports {
            let Ok(report) = report else {
                continue;
            };

            self.handle_report(
                TroubleAddress {
                    kind: report.addr_kind,
                    addr: report.addr,
                },
                report.rssi,
                report.event_kind == bt_hci::param::LeAdvEventKind::ScanRsp,
                report.data,
            );
        }
    }
}

#[derive(Clone, Copy)]
struct SettingsStorage(&'static RefCell<FlashStorage<'static>>);

impl SettingsStorage {
    fn new(flash: esp_hal::peripherals::FLASH<'static>) -> Self {
        Self(SETTINGS_FLASH_STORAGE.init(RefCell::new(
            FlashStorage::new(flash).multicore_auto_park(),
        )))
    }
}

impl esp_nvs::platform::Crc for SettingsStorage {
    fn crc32(init: u32, data: &[u8]) -> u32 {
        esp_hal::rom::crc::crc32_le(init, data)
    }
}

impl ErrorType for SettingsStorage {
    type Error = <FlashStorage<'static> as ErrorType>::Error;
}

impl ReadNorFlash for SettingsStorage {
    const READ_SIZE: usize = <FlashStorage<'static> as ReadNorFlash>::READ_SIZE;

    fn read(&mut self, offset: u32, bytes: &mut [u8]) -> Result<(), Self::Error> {
        let mut flash = self.0.borrow_mut();
        ReadNorFlash::read(&mut *flash, offset, bytes)
    }

    fn capacity(&self) -> usize {
        let flash = self.0.borrow();
        ReadNorFlash::capacity(&*flash)
    }
}

impl NorFlash for SettingsStorage {
    const WRITE_SIZE: usize = <FlashStorage<'static> as NorFlash>::WRITE_SIZE;
    const ERASE_SIZE: usize = <FlashStorage<'static> as NorFlash>::ERASE_SIZE;

    fn write(&mut self, offset: u32, bytes: &[u8]) -> Result<(), Self::Error> {
        let mut flash = self.0.borrow_mut();
        NorFlash::write(&mut *flash, offset, bytes)
    }

    fn erase(&mut self, from: u32, to: u32) -> Result<(), Self::Error> {
        let mut flash = self.0.borrow_mut();
        NorFlash::erase(&mut *flash, from, to)
    }
}

impl MultiwriteNorFlash for SettingsStorage {}

impl ReadStorage for SettingsStorage {
    type Error = <FlashStorage<'static> as ReadStorage>::Error;

    fn read(&mut self, offset: u32, bytes: &mut [u8]) -> Result<(), Self::Error> {
        let mut flash = self.0.borrow_mut();
        ReadStorage::read(&mut *flash, offset, bytes)
    }

    fn capacity(&self) -> usize {
        let flash = self.0.borrow();
        ReadStorage::capacity(&*flash)
    }
}

impl Storage for SettingsStorage {
    fn write(&mut self, offset: u32, bytes: &[u8]) -> Result<(), Self::Error> {
        let mut flash = self.0.borrow_mut();
        Storage::write(&mut *flash, offset, bytes)
    }
}

type SettingsNvs = Nvs<SettingsStorage>;

enum HttpRequestMessage {
    Snapshot,
    Command(heapless::String<256>),
}

enum HttpRoute {
    RootPage,
    Snapshot,
    Command(heapless::String<256>),
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum LedPhase {
    Disabled,
    ActiveDetection,
    PresenceDecay,
    Idle,
}

impl LedPhase {
    fn as_str(self) -> &'static str {
        match self {
            Self::Disabled => "disabled",
            Self::ActiveDetection => "active_detection",
            Self::PresenceDecay => "presence_decay",
            Self::Idle => "idle",
        }
    }
}

fn detection_decision_reason(decision: RadarDetectionDecision) -> &'static str {
    match decision {
        RadarDetectionDecision::Candidate => "radar_candidate",
        RadarDetectionDecision::InvalidMetrics => "invalid_metrics",
        RadarDetectionDecision::OutOfRange => "out_of_range",
        RadarDetectionDecision::InsufficientActiveGates => "insufficient_active_gates",
        RadarDetectionDecision::LowActivity => "low_activity",
        RadarDetectionDecision::MissingEnergyFrame => "missing_energy_frame",
        RadarDetectionDecision::LowEnergy => "low_energy",
        RadarDetectionDecision::NearFieldClutter => "near_field_clutter",
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
struct DetectionDebug {
    led_phase: LedPhase,
    detection_reason: &'static str,
    radar_candidate: bool,
    gpio_fallback: bool,
    clutter_suppressed: bool,
    effective_min_gate_energy: u16,
    effective_min_activity_score: u8,
}

#[derive(Debug, Clone)]
struct FirmwareSyncState {
    in_progress: bool,
    pending: bool,
    target_version: String,
    target_node_id: String,
    target_source: String,
    download_url: String,
    status: String,
    last_error: String,
    last_started_ms: u32,
    last_completed_ms: u32,
    last_success: bool,
}

impl Default for FirmwareSyncState {
    fn default() -> Self {
        Self {
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
        }
    }
}

#[derive(Clone, Copy, Debug, Default)]
struct StatusLedColor {
    r: u8,
    g: u8,
    b: u8,
}

struct StatusLed<'ch> {
    channel: Option<Channel<'ch, Blocking, Tx>>,
    buffer: [PulseCode; STATUS_LED_BUFFER_SIZE],
    pulses: (PulseCode, PulseCode),
}

impl<'ch> StatusLed<'ch> {
    fn new(channel: Channel<'ch, Blocking, Tx>) -> Self {
        Self {
            channel: Some(channel),
            buffer: [PulseCode::end_marker(); STATUS_LED_BUFFER_SIZE],
            pulses: status_led_pulses_for_clock(80),
        }
    }

    fn write(&mut self, color: StatusLedColor) {
        self.encode(color);

        let Some(channel) = self.channel.take() else {
            return;
        };

        match channel.transmit(&self.buffer).and_then(|transaction| transaction.wait()) {
            Ok(channel) => self.channel = Some(channel),
            Err((_, channel)) => self.channel = Some(channel),
        }
    }

    fn encode(&mut self, color: StatusLedColor) {
        let mut cursor = 0;

        for byte in [color.g, color.r, color.b] {
            encode_status_led_byte(byte, &self.pulses, &mut self.buffer, &mut cursor);
        }

        self.buffer[cursor] = PulseCode::end_marker();
    }
}

#[derive(Debug, Clone)]
struct FirmwareState {
    enabled: bool,
    boot_count: u32,
    last_reset_reason: String,
    wifi_ssid: String,
    wifi_password: String,
    mqtt_host: String,
    mqtt_port: u16,
    mqtt_username: String,
    mqtt_password: String,
    mqtt_use_websockets: bool,
    mqtt_websocket_path: String,
    mqtt_host_header: String,
    mqtt_connected: bool,
    mqtt_state: i32,
    mqtt_state_text: String,
    mqtt_host_ip: String,
    mqtt_runtime_generation: u32,
    mqtt_runtime_announced_generation: u32,
    mqtt_runtime_announced_wifi_connected: bool,
    last_mqtt_room_summary_publish_ms: u32,
    node_id: String,
    friendly_name: String,
    mac_address: [u8; 6],
    wifi_connected: bool,
    wifi_disconnect_reason: u16,
    wifi_disconnect_reason_text: String,
    wifi_rssi_dbm: i32,
    wifi_channel: u8,
    wifi_bssid: String,
    wifi_reconfigure_pending: bool,
    wifi_runtime_started: bool,
    last_wifi_attempt_ms: u32,
    last_wifi_status_poll_ms: u32,
    ip_address: String,
    subnet_mask: String,
    gateway_ip: String,
    dns_1: String,
    dns_2: String,
    broadcast_ip: String,
    room_id: String,
    sensor_role: String,
    pose_x_cm: i32,
    pose_y_cm: i32,
    heading_deg: i32,
    room_width_cm: u16,
    room_height_cm: u16,
    max_detection_range_cm: u16,
    min_gate_energy: u16,
    sensitivity_percent: u8,
    presence_hold_ms: u16,
    min_active_gates: u8,
    min_activity_score: u8,
    led_enabled: bool,
    led_brightness: u8,
    presence: bool,
    gpio_presence: bool,
    detection_candidate: bool,
    presence_initialized: bool,
    presence_changes_total: u32,
    last_presence_poll_ms: u32,
    last_detection_ms: u32,
    radar_bytes_total: u32,
    radar_frames_total: u32,
    ld2420_energy_frames_total: u32,
    last_radar_byte_ms: u32,
    radar_buffer: heapless::Vec<u8, RADAR_FRAME_BUFFER_SIZE>,
    latest_energy_frame: Option<LatestEnergyFrameSnapshot>,
    latest_text_frame: Option<LatestTextFrameSnapshot>,
    latest_generic_frame: Option<LatestGenericFrameSnapshot>,
    latest_radar_frame: Option<RadarFrame>,
    runtime_benchmark: Option<RuntimeBenchmarkSnapshot>,
    firmware_sync_state: FirmwareSyncState,
    last_udp_discovery_announce_ms: u32,
    udp_discovery_peers: Vec<UdpDiscoveryPeerState>,
    ble_sightings: Vec<BleBeaconSightingState>,
    ble_identity_tags: Vec<BleIdentityTagState>,
}

#[derive(Debug, Clone)]
struct UdpDiscoveryPeerState {
    node_id: String,
    friendly_name: String,
    room_id: String,
    sensor_role: String,
    firmware_version: String,
    build_target: String,
    hostname: String,
    ip_address: String,
    wifi_rssi_dbm: i32,
    wifi_channel: u8,
    uptime_s: u32,
    free_heap_bytes: u32,
    last_seen_ms: u32,
    pose_x_cm: i32,
    pose_y_cm: i32,
    heading_deg: i32,
    room_width_cm: u16,
    room_height_cm: u16,
    last_room_summary_ms: u32,
}

impl Default for FirmwareState {
    fn default() -> Self {
        Self {
            enabled: true,
            boot_count: 0,
            last_reset_reason: String::from("unknown"),
            wifi_ssid: String::new(),
            wifi_password: String::new(),
            mqtt_host: String::new(),
            mqtt_port: DEFAULT_MQTT_PORT,
            mqtt_username: String::new(),
            mqtt_password: String::new(),
            mqtt_use_websockets: false,
            mqtt_websocket_path: String::from(DEFAULT_MQTT_WEBSOCKET_PATH),
            mqtt_host_header: String::new(),
            mqtt_connected: false,
            mqtt_state: MQTT_STATE_DISCONNECTED,
            mqtt_state_text: String::from("DISCONNECTED"),
            mqtt_host_ip: String::new(),
            mqtt_runtime_generation: 1,
            mqtt_runtime_announced_generation: 0,
            mqtt_runtime_announced_wifi_connected: false,
            last_mqtt_room_summary_publish_ms: 0,
            node_id: String::from(DEFAULT_NODE_ID_PREFIX),
            friendly_name: String::from(DEFAULT_FRIENDLY_NAME_PREFIX),
            mac_address: [0; 6],
            wifi_connected: false,
            wifi_disconnect_reason: 0,
            wifi_disconnect_reason_text: String::from(DEFAULT_WIFI_NOT_CONFIGURED_REASON_TEXT),
            wifi_rssi_dbm: 0,
            wifi_channel: 0,
            wifi_bssid: String::new(),
            wifi_reconfigure_pending: false,
            wifi_runtime_started: false,
            last_wifi_attempt_ms: 0,
            last_wifi_status_poll_ms: 0,
            ip_address: String::new(),
            subnet_mask: String::new(),
            gateway_ip: String::new(),
            dns_1: String::new(),
            dns_2: String::new(),
            broadcast_ip: String::new(),
            room_id: String::from(DEFAULT_ROOM_ID),
            sensor_role: String::from(DEFAULT_SENSOR_ROLE),
            pose_x_cm: 0,
            pose_y_cm: 0,
            heading_deg: -90,
            room_width_cm: 600,
            room_height_cm: 400,
            max_detection_range_cm: 1120,
            min_gate_energy: 25,
            sensitivity_percent: 55,
            presence_hold_ms: 4000,
            min_active_gates: 1,
            min_activity_score: 10,
            led_enabled: true,
            led_brightness: 32,
            presence: false,
            gpio_presence: false,
            detection_candidate: false,
            presence_initialized: false,
            presence_changes_total: 0,
            last_presence_poll_ms: 0,
            last_detection_ms: 0,
            radar_bytes_total: 0,
            radar_frames_total: 0,
            ld2420_energy_frames_total: 0,
            last_radar_byte_ms: 0,
            radar_buffer: heapless::Vec::new(),
            latest_energy_frame: None,
            latest_text_frame: None,
            latest_generic_frame: None,
            latest_radar_frame: None,
            runtime_benchmark: None,
            firmware_sync_state: FirmwareSyncState::default(),
            last_udp_discovery_announce_ms: 0,
            udp_discovery_peers: Vec::new(),
            ble_sightings: default_ble_sightings(),
            ble_identity_tags: default_ble_identity_tags(),
        }
    }
}

impl FirmwareState {
    fn initialize_device_identity(&mut self, mac_address: [u8; 6]) {
        self.mac_address = mac_address;

        if self.node_id == DEFAULT_NODE_ID_PREFIX || self.node_id.is_empty() {
            self.node_id = default_node_id(mac_address);
        }

        if self.friendly_name == DEFAULT_FRIENDLY_NAME_PREFIX || self.friendly_name.is_empty() {
            self.friendly_name = default_friendly_name(mac_address);
        }
    }

    fn mark_wifi_disconnected(&mut self, reason_text: &str) {
        self.wifi_connected = false;
        self.wifi_disconnect_reason = 0;
        self.wifi_disconnect_reason_text = String::from(reason_text);
        self.wifi_rssi_dbm = 0;
        self.wifi_bssid.clear();
        self.mark_mqtt_runtime_dirty();
    }

    fn mark_wifi_controller_error(&mut self, stage: &str, err: &radio_wifi::WifiError) {
        self.wifi_connected = false;
        self.wifi_disconnect_reason = 0;
        self.wifi_disconnect_reason_text = format_wifi_controller_error(stage, err);
        self.wifi_rssi_dbm = 0;
        self.wifi_bssid.clear();
    }

    fn mark_wifi_connect_error(&mut self, err: &radio_wifi::WifiError) {
        self.wifi_connected = false;
        self.wifi_rssi_dbm = 0;

        match err {
            radio_wifi::WifiError::Disconnected(info) => {
                self.wifi_disconnect_reason = 1;
                self.wifi_disconnect_reason_text = format_wifi_disconnect_info(info);
                self.wifi_bssid = mac_address_string(info.bssid);
            }
            _ => {
                self.wifi_disconnect_reason = 0;
                self.wifi_disconnect_reason_text = format_wifi_controller_error("connect", err);
                self.wifi_bssid.clear();
            }
        }
    }

    fn home_assistant_configured(&self) -> bool {
        self.enabled
            && !self.wifi_ssid.is_empty()
            && !self.mqtt_host.is_empty()
            && !self.node_id.is_empty()
    }

    fn sync_ip_config(&mut self, stack: NetStack<'static>) {
        if let Some(config) = stack.config_v4() {
            self.ip_address = ipv4_address_string(config.address.address());
            self.subnet_mask = ipv4_address_string(config.address.netmask());
            self.gateway_ip = config
                .gateway
                .map(ipv4_address_string)
                .unwrap_or_default();
            self.broadcast_ip = config
                .address
                .broadcast()
                .map(ipv4_address_string)
                .unwrap_or_default();

            self.dns_1 = config
                .dns_servers
                .first()
                .copied()
                .map(ipv4_address_string)
                .unwrap_or_default();
            self.dns_2 = config
                .dns_servers
                .get(1)
                .copied()
                .map(ipv4_address_string)
                .unwrap_or_default();
        } else {
            self.ip_address.clear();
            self.subnet_mask.clear();
            self.gateway_ip.clear();
            self.dns_1.clear();
            self.dns_2.clear();
            self.broadcast_ip.clear();
        }
    }

    fn mqtt_transport(&self) -> &'static str {
        if self.mqtt_use_websockets || mqtt_endpoint_uses_websockets(&self.mqtt_host) {
            "websocket"
        } else {
            "tcp"
        }
    }

    fn normalized_websocket_path(&self) -> String {
        normalize_websocket_path(&self.mqtt_websocket_path)
    }

    fn device_hostname(&self) -> String {
        sanitize_hostname(if self.node_id.is_empty() {
            DEFAULT_NODE_ID_PREFIX
        } else {
            self.node_id.as_str()
        })
    }

    fn dashboard_url(&self) -> String {
        let hostname = self.device_hostname();
        let mut url = String::from("http://");
        url.push_str(&hostname);
        url.push_str(".local/");
        url
    }

    fn active_udp_discovery_peers(&self, uptime_ms: u32) -> Vec<UdpDiscoveryPeerSnapshot> {
        self.udp_discovery_peers
            .iter()
            .filter(|peer| uptime_ms.saturating_sub(peer.last_seen_ms) <= UDP_DISCOVERY_PEER_FRESHNESS_MS)
            .map(|peer| UdpDiscoveryPeerSnapshot {
                node_id: peer.node_id.clone(),
                friendly_name: peer.friendly_name.clone(),
                room_id: peer.room_id.clone(),
                sensor_role: peer.sensor_role.clone(),
                firmware_version: peer.firmware_version.clone(),
                build_target: peer.build_target.clone(),
                hostname: peer.hostname.clone(),
                ip_address: peer.ip_address.clone(),
                wifi_rssi_dbm: peer.wifi_rssi_dbm,
                wifi_channel: peer.wifi_channel,
                uptime_s: peer.uptime_s,
                free_heap_bytes: peer.free_heap_bytes,
                age_ms: uptime_ms.saturating_sub(peer.last_seen_ms),
            })
            .collect()
    }

    fn active_room_peers(&self, uptime_ms: u32) -> Vec<RoomPeerSnapshot> {
        self.udp_discovery_peers
            .iter()
            .filter(|peer| {
                peer.room_id == self.room_id
                    && peer.last_room_summary_ms > 0
                    && uptime_ms.saturating_sub(peer.last_room_summary_ms)
                        <= ROOM_SUMMARY_PEER_FRESHNESS_MS
            })
            .map(|peer| RoomPeerSnapshot {
                node_id: peer.node_id.clone(),
                build_target: peer.build_target.clone(),
                pose_x_cm: peer.pose_x_cm,
                pose_y_cm: peer.pose_y_cm,
                heading_deg: peer.heading_deg,
                room_width_cm: peer.room_width_cm,
                room_height_cm: peer.room_height_cm,
            })
            .collect()
    }

    fn firmware_sync_snapshot(&self, uptime_ms: u32) -> FirmwareSyncSnapshot {
        let local_core = semantic_version_core(RUST_FIRMWARE_VERSION);
        let mut highest_peer_node_id = String::new();
        let mut highest_peer_version = String::new();
        let mut highest_peer_source = String::new();

        for peer in self
            .udp_discovery_peers
            .iter()
            .filter(|peer| uptime_ms.saturating_sub(peer.last_seen_ms) <= UDP_DISCOVERY_PEER_FRESHNESS_MS)
        {
            if peer.build_target != RUST_BUILD_TARGET {
                continue;
            }

            let candidate_version = semantic_version_core(&peer.firmware_version);
            if candidate_version.is_empty() {
                continue;
            }

            if highest_peer_version.is_empty()
                || compare_semantic_versions(&candidate_version, &highest_peer_version) > 0
            {
                highest_peer_node_id = peer.node_id.clone();
                highest_peer_version = candidate_version;
                highest_peer_source = if peer.last_room_summary_ms > 0
                    && uptime_ms.saturating_sub(peer.last_room_summary_ms)
                        <= ROOM_SUMMARY_PEER_FRESHNESS_MS
                {
                    String::from("room_summary")
                } else {
                    String::from("udp_discovery")
                };
            }
        }

        let sync_available = !highest_peer_version.is_empty()
            && compare_semantic_versions(&highest_peer_version, &local_core) > 0;

        FirmwareSyncSnapshot {
            local_version_core: local_core,
            highest_peer_node_id,
            highest_peer_version,
            highest_peer_source,
            sync_available,
            in_progress: self.firmware_sync_state.in_progress,
            pending: self.firmware_sync_state.pending,
            target_version: self.firmware_sync_state.target_version.clone(),
            target_node_id: self.firmware_sync_state.target_node_id.clone(),
            target_source: self.firmware_sync_state.target_source.clone(),
            download_url: self.firmware_sync_state.download_url.clone(),
            status: self.firmware_sync_state.status.clone(),
            last_error: self.firmware_sync_state.last_error.clone(),
            last_started_ms: self.firmware_sync_state.last_started_ms,
            last_completed_ms: self.firmware_sync_state.last_completed_ms,
            last_success: self.firmware_sync_state.last_success,
        }
    }

    fn request_firmware_update(
        &mut self,
        target_version: &str,
        target_node_id: &str,
        target_source: &str,
    ) {
        let target_core = semantic_version_core(target_version);
        if target_core.is_empty() {
            self.firmware_sync_state.last_success = false;
            self.firmware_sync_state.last_error = String::from("target_version_is_not_a_release");
            self.firmware_sync_state.status =
                String::from("Peer version is not a release build; GitHub sync is unavailable.");
            self.firmware_sync_state.last_completed_ms = 0;
            return;
        }

        if self.firmware_sync_state.pending || self.firmware_sync_state.in_progress {
            self.firmware_sync_state.last_success = false;
            self.firmware_sync_state.last_error = String::from("firmware_sync_busy");
            self.firmware_sync_state.status = String::from("Firmware sync already in progress.");
            return;
        }

        self.firmware_sync_state.pending = true;
        self.firmware_sync_state.last_success = false;
        self.firmware_sync_state.target_version = target_core.clone();
        self.firmware_sync_state.target_node_id = String::from(target_node_id);
        self.firmware_sync_state.target_source = String::from(target_source);
        self.firmware_sync_state.download_url = firmware_release_asset_url(
            FIRMWARE_RELEASE_REPO_OWNER,
            FIRMWARE_RELEASE_REPO_NAME,
            &target_core,
            RUST_BUILD_TARGET,
        );
        self.firmware_sync_state.last_error.clear();
        self.firmware_sync_state.status = {
            let mut status = String::from("Queued firmware sync to release ");
            status.push_str(&target_core);
            if !self.firmware_sync_state.download_url.is_empty() {
                status.push_str(" for ");
                status.push_str(RUST_BUILD_TARGET);
            }
            status
        };
    }

    fn request_firmware_sync(&mut self, uptime_ms: u32) {
        let snapshot = self.firmware_sync_snapshot(uptime_ms);

        if snapshot.highest_peer_version.is_empty() {
            self.firmware_sync_state.last_success = false;
            self.firmware_sync_state.last_error = String::from("no_peer_release_candidate");
            self.firmware_sync_state.status =
                String::from("No peer release version is available to sync from.");
            self.firmware_sync_state.last_completed_ms = uptime_ms;
            self.firmware_sync_state.pending = false;
            return;
        }

        if compare_semantic_versions(&snapshot.highest_peer_version, &snapshot.local_version_core) <= 0 {
            self.firmware_sync_state.last_success = true;
            self.firmware_sync_state.last_error.clear();
            self.firmware_sync_state.status =
                String::from("Already on the highest visible peer release.");
            self.firmware_sync_state.last_completed_ms = uptime_ms;
            self.firmware_sync_state.pending = false;
            return;
        }

        self.request_firmware_update(
            &snapshot.highest_peer_version,
            &snapshot.highest_peer_node_id,
            &snapshot.highest_peer_source,
        );
    }

    async fn service_firmware_sync(
        &mut self,
        uptime_ms: u32,
        station_stack: Option<NetStack<'static>>,
        flash_storage: SettingsStorage,
    ) {
        if !self.firmware_sync_state.pending || self.firmware_sync_state.in_progress {
            return;
        }

        self.firmware_sync_state.pending = false;
        self.firmware_sync_state.in_progress = true;
        self.firmware_sync_state.last_success = false;
        self.firmware_sync_state.last_started_ms = uptime_ms;
        if self.firmware_sync_state.download_url.is_empty() {
            self.firmware_sync_state.last_error = String::from("release_download_url_missing");
            self.firmware_sync_state.status = String::from(
                "Rust firmware sync could not resolve a GitHub release asset URL for this board target.",
            );
        } else {
            let download_urls = firmware_release_download_url_candidates(
                &self.firmware_sync_state.target_version,
                &self.firmware_sync_state.download_url,
            );
            let mut last_error: Option<String> = None;
            let mut staged_bytes_written: Option<(usize, DownloadUrlScheme)> = None;

            for candidate_url in download_urls {
                let Some(download_url) = parse_download_url(candidate_url.as_str()) else {
                    last_error = Some(String::from("release_download_url_invalid"));
                    break;
                };

                match download_url.scheme {
                    DownloadUrlScheme::Https => {
                        #[cfg(not(feature = "https-ota"))]
                        {
                            last_error = Some(String::from("firmware_https_disabled"));
                            break;
                        }

                        #[cfg(feature = "https-ota")]
                        {
                            ensure_ota_tls_entropy();
                            let Ok(mut tls_trng) = Trng::try_new() else {
                                self.firmware_sync_state.last_error = String::from("firmware_https_tls_init_failed");
                                self.firmware_sync_state.status = String::from(
                                    "Rust firmware sync could not initialize TLS after Wi-Fi came up.",
                                );
                                self.firmware_sync_state.last_completed_ms = uptime_ms;
                                self.firmware_sync_state.in_progress = false;
                                return;
                            };
                            let Ok(tls) = Tls::new(&mut tls_trng) else {
                                self.firmware_sync_state.last_error = String::from("firmware_https_tls_init_failed");
                                self.firmware_sync_state.status = String::from(
                                    "Rust firmware sync could not initialize TLS after Wi-Fi came up.",
                                );
                                self.firmware_sync_state.last_completed_ms = uptime_ms;
                                self.firmware_sync_state.in_progress = false;
                                return;
                            };
                            match perform_https_firmware_update(
                                station_stack,
                                flash_storage,
                                tls.reference(),
                                &download_url,
                            )
                            .await
                            {
                                Ok(bytes_written) => {
                                    staged_bytes_written = Some((bytes_written, DownloadUrlScheme::Https));
                                    break;
                                }
                                Err(err) if err == "firmware_http_not_found" => {
                                    last_error = Some(err);
                                }
                                Err(err) => {
                                    last_error = Some(err);
                                    break;
                                }
                            }
                        }
                    }
                    DownloadUrlScheme::Http => {
                        match perform_http_firmware_update(
                            station_stack,
                            flash_storage,
                            &download_url,
                        )
                        .await
                        {
                            Ok(bytes_written) => {
                                staged_bytes_written = Some((bytes_written, DownloadUrlScheme::Http));
                                break;
                            }
                            Err(err) if err == "firmware_http_not_found" => {
                                last_error = Some(err);
                            }
                            Err(err) => {
                                last_error = Some(err);
                                break;
                            }
                        }
                    }
                }
            }

            if let Some((bytes_written, scheme)) = staged_bytes_written {
                self.firmware_sync_state.last_success = true;
                self.firmware_sync_state.last_error.clear();
                self.firmware_sync_state.status = {
                    let mut status = match scheme {
                        DownloadUrlScheme::Https => {
                            String::from("Firmware staged into the next OTA partition from HTTPS source (")
                        }
                        DownloadUrlScheme::Http => {
                            String::from("Firmware staged into the next OTA partition from HTTP source (")
                        }
                    };
                    append_u32_decimal_string(
                        &mut status,
                        bytes_written.min(u32::MAX as usize) as u32,
                    );
                    status.push_str(" bytes). Reboot to boot the staged image.");
                    status
                };
            } else if let Some(err) = last_error {
                if err == "firmware_https_disabled" {
                    self.firmware_sync_state.last_error = err;
                    self.firmware_sync_state.status =
                        String::from("Rust firmware sync HTTPS support is disabled in this build.");
                } else if err == "release_download_url_invalid" {
                    self.firmware_sync_state.last_error = err;
                    self.firmware_sync_state.status = String::from(
                        "Rust firmware sync resolved a release asset URL, but it is not a valid HTTP or HTTPS download target.",
                    );
                } else {
                    self.firmware_sync_state.last_error = err;
                    self.firmware_sync_state.status = String::from(
                        "Rust firmware sync failed while downloading or staging the firmware image.",
                    );
                }
            }
        }
        self.firmware_sync_state.in_progress = false;
        self.firmware_sync_state.last_completed_ms = uptime_ms;
    }

    fn sync_udp_discovery_peer(&mut self, payload: &str, uptime_ms: u32) {
        if json_field_string(payload, "kind").as_deref() != Some("lb_udp_discovery") {
            return;
        }

        let Some(node_id) = json_field_string(payload, "node_id") else {
            return;
        };

        if node_id.is_empty() || node_id == self.node_id {
            return;
        }

        let peer = UdpDiscoveryPeerState {
            node_id: node_id.clone(),
            friendly_name: json_field_string(payload, "friendly_name").unwrap_or_default(),
            room_id: json_field_string(payload, "room_id").unwrap_or_default(),
            sensor_role: json_field_string(payload, "sensor_role").unwrap_or_default(),
            firmware_version: json_field_string(payload, "firmware_version").unwrap_or_default(),
            build_target: json_field_string(payload, "build_target").unwrap_or_default(),
            hostname: json_field_string(payload, "hostname").unwrap_or_default(),
            ip_address: json_field_string(payload, "ip_address").unwrap_or_default(),
            wifi_rssi_dbm: json_field_i32(payload, "wifi_rssi_dbm").unwrap_or_default(),
            wifi_channel: json_field_i32(payload, "wifi_channel").unwrap_or_default().max(0) as u8,
            uptime_s: json_field_u32(payload, "uptime_s").unwrap_or_default(),
            free_heap_bytes: json_field_u32(payload, "free_heap_bytes").unwrap_or_default(),
            last_seen_ms: uptime_ms,
            pose_x_cm: 0,
            pose_y_cm: 0,
            heading_deg: 0,
            room_width_cm: 0,
            room_height_cm: 0,
            last_room_summary_ms: 0,
        };

        if let Some(existing) = self
            .udp_discovery_peers
            .iter_mut()
            .find(|existing| existing.node_id == node_id)
        {
            existing.friendly_name = peer.friendly_name;
            existing.room_id = peer.room_id;
            existing.sensor_role = peer.sensor_role;
            existing.firmware_version = peer.firmware_version;
            existing.build_target = peer.build_target;
            existing.hostname = peer.hostname;
            existing.ip_address = peer.ip_address;
            existing.wifi_rssi_dbm = peer.wifi_rssi_dbm;
            existing.wifi_channel = peer.wifi_channel;
            existing.uptime_s = peer.uptime_s;
            existing.free_heap_bytes = peer.free_heap_bytes;
            existing.last_seen_ms = peer.last_seen_ms;
        } else {
            if self.udp_discovery_peers.len() >= MAX_UDP_DISCOVERY_PEERS {
                if let Some((oldest_index, _)) = self
                    .udp_discovery_peers
                    .iter()
                    .enumerate()
                    .min_by_key(|(_, existing)| existing.last_seen_ms)
                {
                    let _ = self.udp_discovery_peers.remove(oldest_index);
                }
            }
            self.udp_discovery_peers.push(peer);
        }

        self.udp_discovery_peers
            .retain(|existing| uptime_ms.saturating_sub(existing.last_seen_ms) <= UDP_DISCOVERY_PEER_FRESHNESS_MS);
    }

    fn sync_room_summary_peer(&mut self, payload: &str, uptime_ms: u32) {
        if !looks_like_room_summary_payload(payload) {
            return;
        }

        let Some(node_id) = json_field_string(payload, "node_id") else {
            return;
        };

        if node_id.is_empty() || node_id == self.node_id {
            return;
        }

        let peer_index = if let Some(index) = self
            .udp_discovery_peers
            .iter()
            .position(|existing| existing.node_id == node_id)
        {
            index
        } else {
            if self.udp_discovery_peers.len() >= MAX_UDP_DISCOVERY_PEERS {
                if let Some((oldest_index, _)) = self
                    .udp_discovery_peers
                    .iter()
                    .enumerate()
                    .min_by_key(|(_, existing)| existing.last_seen_ms)
                {
                    let _ = self.udp_discovery_peers.remove(oldest_index);
                }
            }

            self.udp_discovery_peers.push(UdpDiscoveryPeerState {
                node_id: node_id.clone(),
                friendly_name: String::new(),
                room_id: String::new(),
                sensor_role: String::new(),
                firmware_version: String::new(),
                build_target: String::new(),
                hostname: String::new(),
                ip_address: String::new(),
                wifi_rssi_dbm: 0,
                wifi_channel: 0,
                uptime_s: 0,
                free_heap_bytes: 0,
                last_seen_ms: uptime_ms,
                pose_x_cm: 0,
                pose_y_cm: 0,
                heading_deg: 0,
                room_width_cm: 0,
                room_height_cm: 0,
                last_room_summary_ms: 0,
            });
            self.udp_discovery_peers.len() - 1
        };

        let peer = &mut self.udp_discovery_peers[peer_index];
        peer.room_id = json_field_string(payload, "room_id").unwrap_or_default();
        peer.sensor_role = json_field_string(payload, "sensor_role").unwrap_or_default();
        peer.firmware_version = json_field_string(payload, "firmware_version").unwrap_or_default();
        peer.build_target = json_field_string(payload, "build_target").unwrap_or_default();
        peer.pose_x_cm = json_field_i32(payload, "pose_x_cm").unwrap_or_default();
        peer.pose_y_cm = json_field_i32(payload, "pose_y_cm").unwrap_or_default();
        peer.heading_deg = json_field_i32(payload, "heading_deg").unwrap_or(-90);
        peer.room_width_cm = json_field_u32(payload, "room_width_cm").unwrap_or(600) as u16;
        peer.room_height_cm = json_field_u32(payload, "room_height_cm").unwrap_or(400) as u16;
        peer.last_seen_ms = uptime_ms;
        peer.last_room_summary_ms = uptime_ms;
    }

    fn clear_udp_discovery_peers(&mut self) {
        self.last_udp_discovery_announce_ms = 0;
        self.udp_discovery_peers.clear();
    }

    fn access_point_ssid(&self) -> String {
        let hostname = self.device_hostname();
        let mut ssid = String::from("LB-MMWave-");
        ssid.push_str(&hostname);
        if ssid.len() > 31 {
            ssid.truncate(31);
        }
        ssid
    }

    fn access_point_password(&self) -> String {
        let mut password = String::from("lbmmw");
        append_hex_byte_lower(&mut password, self.mac_address[3]);
        append_hex_byte_lower(&mut password, self.mac_address[4]);
        append_hex_byte_lower(&mut password, self.mac_address[5]);
        password
    }

    fn topic_prefix(&self) -> String {
        let mut topic = String::from(DEFAULT_TOPIC_PREFIX);
        if !self.node_id.is_empty() {
            topic.push('/');
            topic.push_str(&self.node_id);
        }
        topic
    }

    fn mark_mqtt_runtime_dirty(&mut self) {
        self.mqtt_runtime_generation = self.mqtt_runtime_generation.wrapping_add(1).max(1);
    }

    fn apply_mqtt_status(&mut self, status: MqttTaskStatus) {
        self.mqtt_connected = status.connected;
        self.mqtt_state = status.state;
        self.mqtt_state_text = String::from(status.state_text.as_str());
        self.mqtt_host_ip = String::from(status.host_ip.as_str());
    }

    fn mqtt_task_config(&self) -> MqttTaskConfig {
        MqttTaskConfig {
            wifi_connected: self.wifi_connected,
            enabled: self.enabled && self.home_assistant_configured(),
            use_websockets: self.mqtt_use_websockets,
            host: heapless_string(&self.mqtt_host),
            port: self.mqtt_port,
            username: heapless_string(&self.mqtt_username),
            password: heapless_string(&self.mqtt_password),
            room_id: heapless_string(&self.room_id),
            node_id: heapless_string(&self.node_id),
        }
    }

    fn radar_tuning(&self) -> RadarTuning {
        RadarTuning {
            max_detection_range_cm: self.max_detection_range_cm,
            min_gate_energy: self.min_gate_energy,
            sensitivity_percent: self.sensitivity_percent,
            min_active_gates: self.min_active_gates,
            min_activity_score: self.min_activity_score,
        }
    }

    fn active_radar_metrics(&self) -> Option<RadarDerivedMetrics> {
        self.latest_radar_frame
            .as_ref()
            .map(|frame| build_radar_derived_metrics(Some(frame), self.gpio_presence))
    }

    fn radar_candidate_from_metrics(&self, metrics: &RadarDerivedMetrics) -> bool {
        matches!(
            radar_detection_decision(metrics, self.latest_radar_frame.as_ref(), self.radar_tuning()),
            RadarDetectionDecision::Candidate
        )
    }

    fn gpio_fallback_active(&self) -> bool {
        self.gpio_presence && self.latest_energy_frame.is_none() && self.latest_text_frame.is_none()
    }

    fn detection_debug(&self, uptime_ms: u32) -> DetectionDebug {
        let metrics = self.active_radar_metrics();
        let tuning = self.radar_tuning();
        let effective_min_gate_energy = effective_min_gate_energy(tuning);
        let effective_min_activity_score = effective_min_activity_score(tuning);
        let gpio_fallback = self.gpio_fallback_active();
        let decision = metrics
            .as_ref()
            .map(|metrics| radar_detection_decision(metrics, self.latest_radar_frame.as_ref(), tuning))
            .unwrap_or(RadarDetectionDecision::InvalidMetrics);
        let radar_candidate = matches!(decision, RadarDetectionDecision::Candidate);
        let clutter_suppressed = matches!(decision, RadarDetectionDecision::NearFieldClutter);

        let led_phase = if !self.led_enabled {
            LedPhase::Disabled
        } else if self.detection_candidate {
            LedPhase::ActiveDetection
        } else if self.presence_decay_remaining_ms(uptime_ms) > 0 {
            LedPhase::PresenceDecay
        } else {
            LedPhase::Idle
        };

        let detection_reason = if gpio_fallback && !radar_candidate {
            "gpio_fallback"
        } else {
            detection_decision_reason(decision)
        };

        DetectionDebug {
            led_phase,
            detection_reason,
            radar_candidate,
            gpio_fallback,
            clutter_suppressed,
            effective_min_gate_energy,
            effective_min_activity_score,
        }
    }

    fn debug_status_json(&self, uptime_ms: u32) -> String {
        let debug = self.detection_debug(uptime_ms);
        let live_metrics = self.active_radar_metrics();

        let mut json = String::from("{");
        json.push_str("\"status_led_hex\":\"");
        json.push_str(&self.status_led_hex(uptime_ms));
        json.push_str("\",");
        json.push_str("\"led_phase\":\"");
        json.push_str(debug.led_phase.as_str());
        json.push_str("\",");
        json.push_str("\"detection_reason\":\"");
        json.push_str(debug.detection_reason);
        json.push_str("\",");
        json.push_str("\"detection_candidate\":");
        json.push_str(if self.detection_candidate { "true" } else { "false" });
        json.push_str(",\"presence\":");
        json.push_str(if self.presence { "true" } else { "false" });
        json.push_str(",\"gpio_presence\":");
        json.push_str(if self.gpio_presence { "true" } else { "false" });
        json.push_str(",\"radar_candidate\":");
        json.push_str(if debug.radar_candidate { "true" } else { "false" });
        json.push_str(",\"gpio_fallback\":");
        json.push_str(if debug.gpio_fallback { "true" } else { "false" });
        json.push_str(",\"clutter_suppressed\":");
        json.push_str(if debug.clutter_suppressed { "true" } else { "false" });
        json.push_str(",\"presence_decay_remaining_ms\":");
        append_u32(&mut json, self.presence_decay_remaining_ms(uptime_ms));
        json.push_str(",\"effective_min_gate_energy\":");
        append_u32(&mut json, u32::from(debug.effective_min_gate_energy));
        json.push_str(",\"effective_min_activity_score\":");
        append_u32(&mut json, u32::from(debug.effective_min_activity_score));
        json.push_str(",\"active_gate_count\":");
        append_u32(
            &mut json,
            u32::from(live_metrics.as_ref().map(|metrics| metrics.active_gate_count).unwrap_or(0)),
        );
        json.push_str(",\"activity_score\":");
        append_u32(
            &mut json,
            u32::from(live_metrics.as_ref().map(|metrics| metrics.activity_score).unwrap_or(0)),
        );
        json.push_str(",\"dominant_gate_distance_cm\":");
        append_i32(
            &mut json,
            live_metrics
                .as_ref()
                .map(|metrics| metrics.dominant_gate_distance_cm)
                .unwrap_or(-1),
        );
        json.push_str(",\"dominant_gate_energy\":");
        append_u32(
            &mut json,
            u32::from(live_metrics.as_ref().map(|metrics| metrics.dominant_gate_energy).unwrap_or(0)),
        );
        json.push('}');
        json
    }

    fn sync_radar_detection_state(&mut self, uptime_ms: u32) {
        let metrics = self.active_radar_metrics();
        let radar_candidate = metrics
            .as_ref()
            .map(|metrics| self.radar_candidate_from_metrics(metrics))
            .unwrap_or(false);
        let gpio_fallback = self.gpio_fallback_active();

        self.detection_candidate = radar_candidate || gpio_fallback;

        if self.detection_candidate {
            self.last_detection_ms = uptime_ms;
        }
    }

    fn effective_presence_hold_ms(&self) -> u16 {
        self.presence_hold_ms
            .clamp(MIN_PRESENCE_HOLD_MS, MAX_PRESENCE_HOLD_MS)
    }

    fn presence_decay_remaining_ms(&self, uptime_ms: u32) -> u32 {
        let hold_ms = u32::from(self.effective_presence_hold_ms());
        if hold_ms == 0 || self.last_detection_ms == 0 {
            return 0;
        }

        let elapsed = uptime_ms.saturating_sub(self.last_detection_ms);
        if elapsed >= hold_ms {
            0
        } else {
            hold_ms - elapsed
        }
    }

    fn poll_presence(&mut self, gpio_presence: bool, uptime_ms: u32) {
        if uptime_ms.saturating_sub(self.last_presence_poll_ms) < PRESENCE_POLL_MS {
            return;
        }

        self.last_presence_poll_ms = uptime_ms;
        self.gpio_presence = gpio_presence;
        self.sync_radar_detection_state(uptime_ms);

        let presence = self.detection_candidate || self.presence_decay_remaining_ms(uptime_ms) > 0;

        if !self.presence_initialized {
            self.presence_initialized = true;
            self.presence = presence;
            return;
        }

        if presence != self.presence {
            self.presence = presence;
            self.presence_changes_total = self.presence_changes_total.saturating_add(1);
        }
    }

    fn status_led_rgb(&self, uptime_ms: u32) -> StatusLedColor {
        if !self.led_enabled {
            return StatusLedColor::default();
        }

        let brightness = self.led_brightness;

        if self.detection_candidate {
            return StatusLedColor {
                r: 0,
                g: brightness,
                b: 0,
            };
        }

        let remaining = self.presence_decay_remaining_ms(uptime_ms);
        let hold = u32::from(self.effective_presence_hold_ms());
        if remaining == 0 || hold == 0 {
            return StatusLedColor {
                r: brightness,
                g: 0,
                b: 0,
            };
        }

        let green = ((remaining * u32::from(brightness)) / hold) as u8;
        let red = brightness.saturating_sub(green);

        StatusLedColor { r: red, g: green, b: 0 }
    }

    fn status_led_hex(&self, uptime_ms: u32) -> String {
        let color = self.status_led_rgb(uptime_ms);
        let mut output = String::with_capacity(6);
        append_hex_byte(&mut output, color.r);
        append_hex_byte(&mut output, color.g);
        append_hex_byte(&mut output, color.b);
        output
    }

    fn apply_room_config(&mut self, payload: RoomConfigPayload<'_>) {
        self.room_id = String::from(payload.room_id);
        self.sensor_role = String::from(payload.sensor_role);
        self.pose_x_cm = i32::from(payload.pose_x_cm);
        self.pose_y_cm = i32::from(payload.pose_y_cm);
        self.heading_deg = i32::from(payload.heading_deg);
        self.room_width_cm = payload.room_width_cm;
        self.room_height_cm = payload.room_height_cm;
        self.mark_mqtt_runtime_dirty();
        self.last_mqtt_room_summary_publish_ms = 0;
    }

    fn apply_home_assistant_config(&mut self, payload: HomeAssistantConfigPayload<'_>) {
        self.enabled = true;
        self.wifi_ssid = String::from(payload.wifi_ssid);
        self.wifi_password = String::from(payload.wifi_password);
        self.mqtt_host = String::from(payload.mqtt_host);
        self.mqtt_port = payload.mqtt_port;
        self.mqtt_username = String::from(payload.mqtt_username);
        self.mqtt_password = String::from(payload.mqtt_password);
        self.mqtt_use_websockets = self.mqtt_use_websockets || mqtt_endpoint_uses_websockets(payload.mqtt_host);
        self.mqtt_websocket_path = self.normalized_websocket_path();
        self.node_id = if payload.node_id.is_empty() {
            default_node_id(self.mac_address)
        } else {
            String::from(payload.node_id)
        };
        self.friendly_name = if payload.friendly_name.is_empty() {
            default_friendly_name(self.mac_address)
        } else {
            String::from(payload.friendly_name)
        };

        if self.room_id.is_empty() {
            self.room_id = String::from(DEFAULT_ROOM_ID);
        }
        if self.sensor_role.is_empty() {
            self.sensor_role = String::from(DEFAULT_SENSOR_ROLE);
        }

        self.wifi_reconfigure_pending = self.home_assistant_configured();
        self.wifi_runtime_started = false;
        if !self.home_assistant_configured() {
            self.mark_wifi_disconnected(DEFAULT_WIFI_NOT_CONFIGURED_REASON_TEXT);
        }
        self.mark_mqtt_runtime_dirty();
        self.last_mqtt_room_summary_publish_ms = 0;
    }

    fn apply_home_assistant_websocket_config(
        &mut self,
        payload: HomeAssistantWebSocketConfigPayload<'_>,
    ) {
        self.mqtt_use_websockets = payload.enabled;
        self.mqtt_websocket_path = if payload.path.is_empty() {
            String::from(DEFAULT_MQTT_WEBSOCKET_PATH)
        } else {
            normalize_websocket_path(payload.path)
        };
        self.mqtt_host_header = String::from(payload.host_header);
        self.mark_mqtt_runtime_dirty();
    }

    fn apply_home_assistant_mqtt_endpoint(
        &mut self,
        payload: HomeAssistantMqttEndpointPayload<'_>,
    ) {
        if !payload.mqtt_host.is_empty() {
            self.mqtt_host = String::from(payload.mqtt_host);
        }
        if let Some(port) = payload.mqtt_port {
            self.mqtt_port = port;
        }
        if let Some(use_websockets) = payload.use_websockets {
            self.mqtt_use_websockets = use_websockets;
        }
        if !payload.websocket_path.is_empty() {
            self.mqtt_websocket_path = normalize_websocket_path(payload.websocket_path);
        }
        self.mqtt_host_header = String::from(payload.host_header);
        self.mark_mqtt_runtime_dirty();
    }

    fn apply_tuning_config(&mut self, payload: TuningConfigPayload) {
        self.max_detection_range_cm = payload.max_detection_range_cm;
        self.min_gate_energy = payload.min_gate_energy;
        self.sensitivity_percent = payload.sensitivity_percent;
        self.presence_hold_ms = payload.presence_hold_ms;
        self.min_active_gates = payload.min_active_gates;
        self.min_activity_score = payload.min_activity_score;
        self.led_enabled = payload.led_enabled;
        self.led_brightness = payload.led_brightness;
    }

    fn apply_ble_tag_config(&mut self, payload: BleTagConfigPayload<'_>) -> bool {
        let slot = usize::from(payload.slot);
        if slot >= self.ble_identity_tags.len() {
            return false;
        }

        let tag = &mut self.ble_identity_tags[slot];
        tag.label = String::from(payload.label);
        tag.address = normalize_ble_identity_value(payload.address);
        tag.min_rssi = payload.min_rssi.clamp(-120, -20);
        tag.last_rssi = -127;
        tag.last_seen_ms = 0;
        tag.occupied = !tag.label.is_empty() && !tag.address.is_empty();
        if !tag.occupied {
            clear_ble_identity_tag(tag);
        }

        true
    }

    fn apply_ble_tag_clear(&mut self, payload: BleTagClearPayload) -> bool {
        let slot = usize::from(payload.slot);
        if slot >= self.ble_identity_tags.len() {
            return false;
        }

        clear_ble_identity_tag(&mut self.ble_identity_tags[slot]);
        true
    }

    fn apply_radar_frame(&mut self, frame: RadarFrame) {
        self.radar_frames_total = self.radar_frames_total.saturating_add(1);

        match &frame {
            RadarFrame::Energy(energy) => {
                self.ld2420_energy_frames_total = self.ld2420_energy_frames_total.saturating_add(1);
                self.latest_energy_frame = Some(snapshot_from_energy_frame(
                    energy,
                    self.radar_bytes_total,
                    self.radar_frames_total,
                    self.ld2420_energy_frames_total,
                ));
                self.latest_radar_frame = Some(frame);
            }
            RadarFrame::Text(text) => {
                self.latest_text_frame = Some(snapshot_from_text_frame(
                    text,
                    self.radar_bytes_total,
                    self.radar_frames_total,
                ));
                self.latest_radar_frame = Some(frame);
            }
            RadarFrame::Generic(generic) => {
                self.latest_generic_frame = Some(snapshot_from_generic_frame(
                    generic,
                    self.radar_bytes_total,
                    self.radar_frames_total,
                ));
                if self.latest_energy_frame.is_none() && self.latest_text_frame.is_none() {
                    self.latest_radar_frame = Some(frame);
                }
            }
        }
    }

    fn snapshot_json(&self, uptime_ms: u32) -> String {
        match self.snapshot(uptime_ms).to_json() {
            Ok(json) => json,
            Err(_) => String::from(r#"{"error":"snapshot_serialization_failed"}"#),
        }
    }

    fn snapshot(&self, uptime_ms: u32) -> DeviceSnapshot {
        let live_metrics = self.active_radar_metrics();
        let benchmark = self.runtime_benchmark.as_ref();
        let device_hostname = self.device_hostname();
        let dashboard_url = self.dashboard_url();
        let ap_ssid = self.access_point_ssid();
        let ap_password = self.access_point_password();
        let wifi_disconnect_reason_text = if self.home_assistant_configured() {
            DEFAULT_WIFI_DISCONNECT_REASON_TEXT
        } else {
            DEFAULT_WIFI_NOT_CONFIGURED_REASON_TEXT
        };

        DeviceSnapshot {
            enabled: self.enabled,
            configured: self.home_assistant_configured(),
            boot_count: self.boot_count,
            last_reset_reason: self.last_reset_reason.clone(),
            wifi_ssid: self.wifi_ssid.clone(),
            mqtt_host: self.mqtt_host.clone(),
            mqtt_port: self.mqtt_port,
            mqtt_transport: String::from(self.mqtt_transport()),
            mqtt_ws_path: self.normalized_websocket_path(),
            mqtt_host_header: self.mqtt_host_header.clone(),
            mqtt_username_set: !self.mqtt_username.is_empty(),
            firmware_version: String::from(RUST_FIRMWARE_VERSION),
            build_target: String::from(RUST_BUILD_TARGET),
            git_sha: String::from(RUST_GIT_SHA),
            node_id: self.node_id.clone(),
            friendly_name: self.friendly_name.clone(),
            room_id: self.room_id.clone(),
            sensor_role: self.sensor_role.clone(),
            pose_x_cm: self.pose_x_cm,
            pose_y_cm: self.pose_y_cm,
            heading_deg: self.heading_deg,
            room_width_cm: self.room_width_cm,
            room_height_cm: self.room_height_cm,
            max_detection_range_cm: self.max_detection_range_cm,
            min_gate_energy: self.min_gate_energy,
            sensitivity_percent: self.sensitivity_percent,
            presence_hold_ms: self.presence_hold_ms,
            min_active_gates: self.min_active_gates,
            min_activity_score: self.min_activity_score,
            led_enabled: self.led_enabled,
            led_brightness: self.led_brightness,
            wifi_connected: self.wifi_connected,
            wifi_disconnect_reason: self.wifi_disconnect_reason,
            wifi_disconnect_reason_text: if self.wifi_disconnect_reason_text.is_empty() {
                String::from(wifi_disconnect_reason_text)
            } else {
                self.wifi_disconnect_reason_text.clone()
            },
            ip_address: self.ip_address.clone(),
            wifi_link: WifiLinkSnapshot {
                connected: self.wifi_connected,
                ssid: self.wifi_ssid.clone(),
                rssi_dbm: self.wifi_rssi_dbm,
                channel: self.wifi_channel,
                bssid: self.wifi_bssid.clone(),
                mac_address: mac_address_string(self.mac_address),
                subnet_mask: self.subnet_mask.clone(),
                gateway_ip: self.gateway_ip.clone(),
                dns_1: self.dns_1.clone(),
                dns_2: self.dns_2.clone(),
                broadcast_ip: self.broadcast_ip.clone(),
            },
            mqtt_connected: self.mqtt_connected,
            mqtt_state: self.mqtt_state,
            mqtt_state_text: self.mqtt_state_text.clone(),
            mqtt_host_ip: self.mqtt_host_ip.clone(),
            topic_prefix: self.topic_prefix(),
            device_hostname,
            dashboard_url,
            ap_ssid,
            ap_password,
            ap_ip: String::from(DEFAULT_AP_IP),
            uptime_ms,
            free_heap: free_heap_bytes(),
            presence: self.presence,
            gpio_presence: self.gpio_presence,
            detection_candidate: self.detection_candidate,
            presence_decay_remaining_ms: self.presence_decay_remaining_ms(uptime_ms),
            radar_bytes_total: self.radar_bytes_total,
            radar_frames_total: self.radar_frames_total,
            ld2420_energy_frames_total: self.ld2420_energy_frames_total,
            presence_changes_total: self.presence_changes_total,
            people_estimate: live_metrics
                .as_ref()
                .map(|metrics| metrics.estimated_people)
                .or_else(|| benchmark.map(|benchmark| benchmark.people_estimate))
                .unwrap_or(0),
            active_gate_count: live_metrics
                .as_ref()
                .map(|metrics| metrics.active_gate_count)
                .or_else(|| benchmark.map(|benchmark| benchmark.active_gate_count))
                .unwrap_or(0),
            activity_score: live_metrics
                .as_ref()
                .map(|metrics| metrics.activity_score)
                .or_else(|| benchmark.map(|benchmark| benchmark.activity_score))
                .unwrap_or(0),
            dominant_gate_index: live_metrics
                .as_ref()
                .map(|metrics| metrics.dominant_gate_index)
                .unwrap_or(-1),
            dominant_gate_distance_cm: live_metrics
                .as_ref()
                .map(|metrics| metrics.dominant_gate_distance_cm)
                .or_else(|| benchmark.map(|benchmark| benchmark.dominant_gate_distance_cm))
                .unwrap_or(-1),
            dominant_gate_energy: live_metrics
                .as_ref()
                .map(|metrics| metrics.dominant_gate_energy)
                .unwrap_or(0),
            total_gate_energy: live_metrics
                .as_ref()
                .map(|metrics| metrics.total_gate_energy)
                .unwrap_or(0),
            status_led_hex: self.status_led_hex(uptime_ms),
            runtime_benchmark: self.runtime_benchmark.clone(),
            room_people_estimate: live_metrics
                .as_ref()
                .map(|metrics| metrics.estimated_people)
                .or_else(|| benchmark.map(|benchmark| benchmark.people_estimate))
                .unwrap_or(0),
            room_active_nodes: 1_u8.saturating_add(self.active_room_peers(uptime_ms).len() as u8),
            room_peer_nodes: self.active_room_peers(uptime_ms).len() as u8,
            room_activity_score: live_metrics
                .as_ref()
                .map(|metrics| metrics.activity_score)
                .or_else(|| benchmark.map(|benchmark| benchmark.activity_score))
                .unwrap_or(0),
            firmware_sync: self.firmware_sync_snapshot(uptime_ms),
            udp_discovery: {
                let peers = self.active_udp_discovery_peers(uptime_ms);
                UdpDiscoverySnapshot {
                    started: self.wifi_connected,
                    port: UDP_DISCOVERY_PORT,
                    peer_count: peers.len().min(u8::MAX as usize) as u8,
                    last_announce_ms: self.last_udp_discovery_announce_ms,
                    peers,
                }
            },
            room_peers: self.active_room_peers(uptime_ms),
            ble_beacon_count: self.active_ble_beacon_count(uptime_ms),
            ble_beacons: self.ble_beacon_snapshots(uptime_ms),
            ble_tagged_people_count: self.active_ble_tag_count(uptime_ms),
            ble_tags: self.ble_tag_snapshots(uptime_ms),
            latest_energy_frame: self.latest_energy_frame.clone(),
            latest_text_frame: self.latest_text_frame.clone(),
            latest_generic_frame: self.latest_generic_frame.clone(),
        }
    }

    fn active_ble_beacon_count(&self, uptime_ms: u32) -> u16 {
        self.ble_sightings
            .iter()
            .filter(|sighting| ble_beacon_sighting_active(sighting, uptime_ms))
            .count()
            .min(u16::MAX as usize) as u16
    }

    fn ble_beacon_snapshots(&self, uptime_ms: u32) -> Vec<BleBeaconSnapshot> {
        self.ble_sightings
            .iter()
            .filter(|sighting| ble_beacon_sighting_active(sighting, uptime_ms))
            .map(|sighting| BleBeaconSnapshot {
                address: sighting.address.clone(),
                name: sighting.name.clone(),
                service_uuid: sighting.service_uuid.clone(),
                rssi: sighting.rssi,
                age_ms: uptime_ms.saturating_sub(sighting.last_seen_ms),
            })
            .collect()
    }

    fn active_ble_tag_count(&self, uptime_ms: u32) -> u16 {
        self.ble_identity_tags
            .iter()
            .filter(|tag| ble_identity_tag_present(tag, uptime_ms))
            .count()
            .min(u16::MAX as usize) as u16
    }

    fn ble_tag_snapshots(&self, uptime_ms: u32) -> Vec<BleTagSnapshot> {
        self.ble_identity_tags
            .iter()
            .enumerate()
            .filter_map(|(slot, tag)| {
                if !tag.occupied {
                    return None;
                }

                Some(BleTagSnapshot {
                    slot: Some(slot.min(u8::MAX as usize) as u8),
                    label: Some(tag.label.clone()),
                    address: Some(tag.address.clone()),
                    min_rssi: Some(tag.min_rssi),
                    rssi: Some(tag.last_rssi),
                    age_ms: Some(if tag.last_seen_ms > 0 {
                        uptime_ms.saturating_sub(tag.last_seen_ms)
                    } else {
                        BLE_TAG_FRESHNESS_MS.saturating_add(1)
                    }),
                    present: Some(ble_identity_tag_present(tag, uptime_ms)),
                })
            })
            .collect()
    }
}

fn firmware_release_download_url_candidates(target_version: &str, primary_url: &str) -> Vec<String> {
    let mut urls = firmware_release_asset_urls(
        FIRMWARE_RELEASE_REPO_OWNER,
        FIRMWARE_RELEASE_REPO_NAME,
        target_version,
        RUST_BUILD_TARGET,
    );

    if !primary_url.is_empty() && !urls.iter().any(|existing| existing == primary_url) {
        urls.insert(0, String::from(primary_url));
    }

    urls
}

#[embassy_executor::task]
async fn ap_net_task(mut runner: embassy_net::Runner<'static, WifiInterface<'static>>) -> ! {
    runner.run().await
}

#[embassy_executor::task]
async fn station_net_task(mut runner: embassy_net::Runner<'static, WifiInterface<'static>>) -> ! {
    runner.run().await
}

#[embassy_executor::task]
async fn station_udp_discovery_task(stack: NetStack<'static>) -> ! {
    let rx_meta = STATION_DISCOVERY_RX_META.init([UdpPacketMetadata::EMPTY; 8]);
    let tx_meta = STATION_DISCOVERY_TX_META.init([UdpPacketMetadata::EMPTY; 8]);
    let rx_buffer = STATION_DISCOVERY_RX_BUFFER.init([0; 2048]);
    let tx_buffer = STATION_DISCOVERY_TX_BUFFER.init([0; 2048]);
    let mut recv_buffer = [0_u8; 512];

    loop {
        if !stack.is_config_up() {
            *UDP_DISCOVERY_STARTED.lock().await = false;
            stack.wait_config_up().await;
        }

        let mut socket = UdpSocket::new(stack, rx_meta, rx_buffer, tx_meta, tx_buffer);

        if socket.bind(UDP_DISCOVERY_PORT).is_err() {
            yield_now().await;
            continue;
        }

        *UDP_DISCOVERY_STARTED.lock().await = true;

        loop {
            let receive_future = socket.recv_from(&mut recv_buffer);
            let announce_future = UDP_DISCOVERY_ANNOUNCE_CHANNEL.receive();

            match select(receive_future, announce_future).await {
                Either::First(Ok((size, _meta))) => {
                    if let Ok(text) = core::str::from_utf8(&recv_buffer[..size]) {
                        let mut payload = heapless::String::<512>::new();
                        if payload.push_str(text).is_ok() {
                            let _ = UDP_DISCOVERY_PACKET_CHANNEL.try_send(payload);
                        }
                    }
                }
                Either::First(Err(_)) => break,
                Either::Second(payload) => {
                    let destination = stack
                        .config_v4()
                        .and_then(|config| config.address.broadcast())
                        .unwrap_or(Ipv4Address::new(255, 255, 255, 255));
                    let endpoint = IpEndpoint::new(IpAddress::Ipv4(destination), UDP_DISCOVERY_PORT);
                    if socket.send_to(payload.as_bytes(), endpoint).await.is_err() {
                        break;
                    }
                }
            }

            if !stack.is_config_up() {
                break;
            }
        }
    }
}

#[embassy_executor::task]
async fn station_mqtt_summary_task(stack: NetStack<'static>) -> ! {
    let rx_buffer = STATION_MQTT_RX_BUFFER.init([0; MQTT_SOCKET_BUFFER_SIZE]);
    let tx_buffer = STATION_MQTT_TX_BUFFER.init([0; MQTT_SOCKET_BUFFER_SIZE]);
    let mut read_buffer = [0_u8; 512];
    let mut inbound = heapless::Vec::<u8, 2048>::new();
    let mut config = MqttTaskConfig::default();
    let mut pending_publish: Option<MqttPublishMessage> = None;

    loop {
        while let Ok(command) = MQTT_COMMAND_CHANNEL.receiver().try_receive() {
            match command {
                MqttTaskCommand::Configure(next) => config = next,
                MqttTaskCommand::Publish(message) => pending_publish = Some(message),
            }
        }

        if !config.enabled || !config.wifi_connected || config.host.is_empty() {
            push_mqtt_status(false, MQTT_STATE_DISCONNECTED, "DISCONNECTED", "");
            match MQTT_COMMAND_CHANNEL.receive().await {
                MqttTaskCommand::Configure(next) => config = next,
                MqttTaskCommand::Publish(message) => pending_publish = Some(message),
            }
            continue;
        }

        if !stack.is_config_up() {
            push_mqtt_status(false, MQTT_STATE_DISCONNECTED, "WAITING_FOR_IP", "");
            match select(stack.wait_config_up(), MQTT_COMMAND_CHANNEL.receive()).await {
                Either::First(()) => {}
                Either::Second(command) => match command {
                    MqttTaskCommand::Configure(next) => config = next,
                    MqttTaskCommand::Publish(message) => {
                        pending_publish = Some(message)
                    }
                },
            }
            continue;
        }

        if config.use_websockets {
            push_mqtt_status(
                false,
                MQTT_STATE_UNSUPPORTED_TRANSPORT,
                "WEBSOCKETS_UNSUPPORTED",
                "",
            );
            match MQTT_COMMAND_CHANNEL.receive().await {
                MqttTaskCommand::Configure(next) => config = next,
                MqttTaskCommand::Publish(message) => pending_publish = Some(message),
            }
            continue;
        }

        let Some(broker_ip) = parse_ipv4_address(config.host.as_str()) else {
            push_mqtt_status(false, MQTT_STATE_INVALID_HOST, "INVALID_HOST", "");
            match MQTT_COMMAND_CHANNEL.receive().await {
                MqttTaskCommand::Configure(next) => config = next,
                MqttTaskCommand::Publish(message) => pending_publish = Some(message),
            }
            continue;
        };

        let mut socket = TcpSocket::new(stack, rx_buffer, tx_buffer);

        if socket.connect(IpEndpoint::new(IpAddress::Ipv4(broker_ip), config.port)).await.is_err() {
            push_mqtt_status(false, MQTT_STATE_CONNECT_ERROR, "CONNECT_ERROR", config.host.as_str());
            continue;
        }

        if mqtt_send_connect(&mut socket, &config).await.is_err()
            || mqtt_wait_for_connack(&mut socket, &mut inbound, &mut read_buffer).await.is_err()
            || mqtt_send_subscribe_room_summary(&mut socket, &config).await.is_err()
            || mqtt_wait_for_suback(&mut socket, &mut inbound, &mut read_buffer).await.is_err()
        {
            let _ = socket.flush().await;
            socket.abort();
            push_mqtt_status(false, MQTT_STATE_PROTOCOL_ERROR, "PROTOCOL_ERROR", config.host.as_str());
            continue;
        }

        push_mqtt_status(true, 0, "CONNECTED", config.host.as_str());

        if let Some(message) = pending_publish.take() {
            let _ = mqtt_send_publish_message(&mut socket, &message).await;
        }

        loop {
            while let Some(packet) = take_mqtt_packet(&mut inbound) {
                if mqtt_is_pingresp_packet(packet.as_slice()) {
                    continue;
                }

                if let Some(summary) = mqtt_room_summary_payload_from_publish(packet.as_slice()) {
                    let mut event_payload = heapless::String::<512>::new();
                    if event_payload.push_str(summary).is_ok() {
                        let _ = MQTT_EVENT_CHANNEL.try_send(MqttTaskEvent::RoomSummary(event_payload));
                    }
                }
            }

            let wait_for_read = socket.wait_read_ready();
            let wait_for_command = MQTT_COMMAND_CHANNEL.receive();
            let wait_for_keepalive = Timer::after(Duration::from_secs(u64::from(
                MQTT_KEEPALIVE_SECS.saturating_div(2).max(1),
            )));

            match select3(wait_for_read, wait_for_command, wait_for_keepalive).await {
                Either3::First(()) => {
                    let Ok(size) = socket.read(&mut read_buffer).await else {
                        break;
                    };
                    if size == 0 || inbound.extend_from_slice(&read_buffer[..size]).is_err() {
                        break;
                    }
                }
                Either3::Second(command) => match command {
                    MqttTaskCommand::Configure(next) => {
                        if next != config {
                            config = next;
                            break;
                        }
                    }
                    MqttTaskCommand::Publish(message) => {
                        if mqtt_send_publish_message(&mut socket, &message).await.is_err() {
                            break;
                        }
                    }
                },
                Either3::Third(()) => {
                    if mqtt_send_pingreq(&mut socket).await.is_err() {
                        break;
                    }
                }
            }
        }

        let _ = socket.flush().await;
        socket.abort();
        push_mqtt_status(false, MQTT_STATE_DISCONNECTED, "DISCONNECTED", "");
    }
}

#[embassy_executor::task]
async fn ap_dhcp_task(stack: NetStack<'static>) -> ! {
    let rx_meta = DHCP_RX_META.init([UdpPacketMetadata::EMPTY; 4]);
    let tx_meta = DHCP_TX_META.init([UdpPacketMetadata::EMPTY; 4]);
    let rx_buffer = DHCP_RX_BUFFER.init([0; 1536]);
    let tx_buffer = DHCP_TX_BUFFER.init([0; 1536]);
    let mut socket = UdpSocket::new(stack, rx_meta, rx_buffer, tx_meta, tx_buffer);
    let _ = socket.bind(DHCP_SERVER_PORT);
    let broadcast_endpoint = IpEndpoint::new(IpAddress::Ipv4(Ipv4Address::new(255, 255, 255, 255)), DHCP_CLIENT_PORT);
    let mut request_buffer = [0_u8; 576];

    loop {
        let Ok((size, _meta)) = socket.recv_from(&mut request_buffer).await else {
            continue;
        };

        let Ok(packet) = DhcpPacket::new_checked(&request_buffer[..size]) else {
            continue;
        };
        let Ok(request) = DhcpRepr::parse(&packet) else {
            continue;
        };

        let lease_ip = dhcp_lease_address(request.client_hardware_address.as_bytes());
        let response_type = match request.message_type {
            DhcpMessageType::Discover => Some(DhcpMessageType::Offer),
            DhcpMessageType::Request => Some(DhcpMessageType::Ack),
            _ => None,
        };

        let Some(response_type) = response_type else {
            continue;
        };

        let mut dns_servers = heapless::Vec::new();
        let _ = dns_servers.push(DEFAULT_AP_GATEWAY);

        let response = DhcpRepr {
            message_type: response_type,
            transaction_id: request.transaction_id,
            secs: request.secs,
            client_hardware_address: request.client_hardware_address,
            client_ip: Ipv4Address::UNSPECIFIED,
            your_ip: lease_ip,
            server_ip: DEFAULT_AP_GATEWAY,
            router: Some(DEFAULT_AP_GATEWAY),
            subnet_mask: Some(DEFAULT_AP_NETMASK),
            relay_agent_ip: Ipv4Address::UNSPECIFIED,
            broadcast: true,
            requested_ip: Some(lease_ip),
            client_identifier: request.client_identifier,
            server_identifier: Some(DEFAULT_AP_GATEWAY),
            parameter_request_list: request.parameter_request_list,
            dns_servers: Some(dns_servers),
            max_size: request.max_size,
            lease_duration: Some(DEFAULT_DHCP_LEASE_SECS),
            renew_duration: Some(DEFAULT_DHCP_RENEW_SECS),
            rebind_duration: Some(DEFAULT_DHCP_REBIND_SECS),
            additional_options: &[],
        };

        let packet_len = response.buffer_len();
        let _ = socket
            .send_to_with(packet_len, broadcast_endpoint, |buffer| {
                let mut packet = DhcpPacket::new_unchecked(buffer);
                let _ = response.emit(&mut packet);
            })
            .await;
    }
}

#[embassy_executor::task]
async fn ap_http_server_task(stack: NetStack<'static>) -> ! {
    let rx_buffer = AP_HTTP_RX_BUFFER.init([0; HTTP_SOCKET_BUFFER_SIZE]);
    let tx_buffer = AP_HTTP_TX_BUFFER.init([0; HTTP_SOCKET_BUFFER_SIZE]);
    serve_http_connections(stack, rx_buffer, tx_buffer).await
}

#[embassy_executor::task]
async fn station_http_server_task(stack: NetStack<'static>) -> ! {
    let rx_buffer = STATION_HTTP_RX_BUFFER.init([0; HTTP_SOCKET_BUFFER_SIZE]);
    let tx_buffer = STATION_HTTP_TX_BUFFER.init([0; HTTP_SOCKET_BUFFER_SIZE]);
    serve_http_connections(stack, rx_buffer, tx_buffer).await
}

#[cfg(feature = "ble-scan")]
#[embassy_executor::task]
async fn ble_scanner_task(bluetooth: esp_hal::peripherals::BT<'static>) -> ! {
    let connector = match esp_radio::ble::controller::BleConnector::new(bluetooth, Default::default()) {
        Ok(connector) => connector,
        Err(_) => loop {
            Timer::after(Duration::from_secs(5)).await;
        },
    };

    let controller: ExternalController<_, 4> = ExternalController::new(connector);
    let mut resources: HostResources<DefaultPacketPool, 1, 1> = HostResources::new();
    let stack = trouble_host::new(controller, &mut resources)
        .set_random_address(TroubleAddress::random([0x3c, 0xdc, 0x75, 0x71, 0x53, 0xdd]));
    let host = stack.build();
    let central = host.central;
    let mut runner = host.runner;
    let handler = BleScanHandler::new();
    let mut scanner = Scanner::new(central);

    let _ = join(runner.run_with_handler(&handler), async {
        let mut config = BleScanConfig::default();
        config.active = false;
        config.phys = PhySet::M1;
        config.interval = Duration::from_millis(BLE_SCAN_INTERVAL_MS);
        config.window = Duration::from_millis(BLE_SCAN_WINDOW_MS);
        let mut _session = scanner.scan(&config).await.unwrap();

        loop {
            Timer::after(Duration::from_secs(1)).await;
        }
    })
    .await;

    loop {
        Timer::after(Duration::from_secs(5)).await;
    }
}

async fn serve_http_connections(
    stack: NetStack<'static>,
    rx_buffer: &'static mut [u8; HTTP_SOCKET_BUFFER_SIZE],
    tx_buffer: &'static mut [u8; HTTP_SOCKET_BUFFER_SIZE],
) -> ! {
    let mut socket = TcpSocket::new(stack, rx_buffer, tx_buffer);

    loop {
        if !stack.is_config_up() {
            stack.wait_config_up().await;
        }

        if socket.accept(HTTP_SERVER_PORT).await.is_err() {
            socket.abort();
            let _ = socket.flush().await;
            continue;
        }

        let response = match read_http_route(&mut socket).await {
            Ok(HttpRoute::RootPage) => {
                let _ = send_http_response_header(
                    &mut socket,
                    "200 OK",
                    "text/html; charset=utf-8",
                    DEVICE_MONITOR_PAGE.len(),
                )
                .await;
                let _ = send_http_body_chunks(&mut socket, DEVICE_MONITOR_PAGE.as_bytes()).await;
                let _ = socket.flush().await;
                socket.close();
                continue;
            }
            Ok(HttpRoute::Snapshot) => {
                let (status, body) = match request_http_response(HttpRequestMessage::Snapshot).await {
                    Ok(body) => ("200 OK", body),
                    Err(body) => ("500 Internal Server Error", body),
                };
                let _ = send_http_response_header(
                    &mut socket,
                    status,
                    "application/json",
                    body.len(),
                )
                .await;
                let _ = send_http_body_chunks(&mut socket, body.as_bytes()).await;
                let _ = socket.flush().await;
                socket.close();
                continue;
            }
            Ok(HttpRoute::Command(command)) => {
                let (status, body) = match request_http_response(HttpRequestMessage::Command(command)).await {
                    Ok(body) => ("200 OK", body),
                    Err(body) => ("500 Internal Server Error", body),
                };
                let _ = send_http_response_header(
                    &mut socket,
                    status,
                    "application/json",
                    body.len(),
                )
                .await;
                let _ = send_http_body_chunks(&mut socket, body.as_bytes()).await;
                let _ = socket.flush().await;
                socket.close();
                continue;
            }
            Err(response) => response,
        };

        let _ = socket_write_all(&mut socket, response.as_bytes()).await;
        let _ = socket.flush().await;
        socket.close();
    }
}

#[esp_rtos::main]
async fn main(spawner: Spawner) {
    let peripherals = esp_hal::init(esp_hal::Config::default().with_cpu_clock(CpuClock::_80MHz));
    esp_alloc::heap_allocator!(size: 192 * 1024);
    let presence_config = match BOARD_PROFILE.radar_presence_pin_mode {
        PresencePinMode::Input => InputConfig::default(),
        PresencePinMode::InputPulldown => InputConfig::default().with_pull(Pull::Down),
    };
    let presence_pin = Input::new(peripherals.GPIO6, presence_config);
    let mut radar_uart = Uart::new(peripherals.UART1, UartConfig::default())
        .unwrap()
        .with_rx(peripherals.GPIO4)
        .with_tx(peripherals.GPIO5);
    let timg0 = TimerGroup::new(peripherals.TIMG0);
    let sw_interrupt = SoftwareInterruptControl::new(peripherals.SW_INTERRUPT);
    esp_rtos::start(timg0.timer0, sw_interrupt.software_interrupt0);
    #[cfg(feature = "usb-console")]
    let mut usb_serial = UsbSerialJtag::new(peripherals.USB_DEVICE);
    #[cfg(not(feature = "usb-console"))]
    let mut usb_serial = UsbSerialJtag::new();
    let rmt = Rmt::new(peripherals.RMT, Rate::from_mhz(80)).unwrap();
    let led_channel = rmt
        .channel0
        .configure_tx(&status_led_config())
        .unwrap()
        .with_pin(peripherals.GPIO48);
    let mut status_led = StatusLed::new(led_channel);
    let boot_started = Instant::now();
    let mut state = FirmwareState::default();
    state.last_reset_reason = format_soc_reset_reason(reset_reason(Cpu::ProCpu));
    let mut base_mac_address = [0_u8; 6];
    base_mac_address.copy_from_slice(efuse::base_mac_address().as_bytes());
    state.initialize_device_identity(base_mac_address);
    let settings_storage = SettingsStorage::new(peripherals.FLASH);
    let mut settings = open_settings_nvs(settings_storage).ok();
    if let Some(settings) = settings.as_mut() {
        load_persisted_config(&mut state, settings);
        state.boot_count = state.boot_count.saturating_add(1);
    }
    state.initialize_device_identity(base_mac_address);
    let mut wifi_device: Option<esp_hal::peripherals::WIFI<'static>> = Some(peripherals.WIFI);
    #[cfg(feature = "ble-scan")]
    let mut bluetooth_device: Option<esp_hal::peripherals::BT<'static>> = Some(peripherals.BT);
    let mut wifi_controller = None;
    let mut ap_stack = None;
    let mut station_stack = None;
    #[cfg(feature = "ble-scan")]
    let mut ble_scanner_started = false;
    let mut command_buffer = heapless::Vec::<u8, 256>::new();
    #[cfg(feature = "usb-console")]
    let mut last_runtime_debug_line = String::new();

    state.poll_presence(presence_pin.is_high(), uptime_ms(&boot_started));
    update_status_led(&mut status_led, state.status_led_rgb(uptime_ms(&boot_started)));

    #[cfg(feature = "device-benchmarks")]
    {
        let benchmark = bench::run_device_benchmarks(uptime_ms(&boot_started));
        write_benchmark_lines(&mut usb_serial, &benchmark);
        state.runtime_benchmark = Some(benchmark);
    }

    write_line(
        &mut usb_serial,
        "EspWaveRider Rust bootstrap on ESP32-S3 DevKitM-1",
    );
    write_line(
        &mut usb_serial,
        "Commands: status, debug_status, ha_status, ha_config:..., ha_ws_config:..., ha_mqtt_endpoint:..., ha_room_config:..., tuning_config:..., runtime_benchmark, firmware_sync",
    );
    #[cfg(feature = "usb-console")]
    write_line(&mut usb_serial, &startup_debug_line(&state));

    loop {
        let now = uptime_ms(&boot_started);
        poll_radar_uart(&mut radar_uart, &mut state, now);
        poll_wifi_runtime(
            &spawner,
            &mut state,
            &mut wifi_device,
            &mut wifi_controller,
            &mut ap_stack,
            &mut station_stack,
            &mut usb_serial,
            now,
        )
        .await;
        #[cfg(feature = "ble-scan")]
        if ENABLE_BLE_SCANNER_TASK && !ble_scanner_started && state.wifi_connected {
            if let Some(bluetooth) = bluetooth_device.take() {
                if let Ok(task) = ble_scanner_task(bluetooth) {
                    spawner.spawn(task);
                    ble_scanner_started = true;
                }
            }
        }
        process_udp_discovery(&mut state, station_stack, now);
        process_mqtt_runtime(&mut state, now);
        process_ble_scan_events(&mut state, now);
        let handled_http_request = process_pending_http_request(
            &mut state,
            &boot_started,
            &mut usb_serial,
            &mut radar_uart,
            settings.as_mut(),
            &mut wifi_controller,
        )
        .await;
        if !handled_http_request {
            state
                .service_firmware_sync(now, station_stack, settings_storage)
                .await;
        }
        state.poll_presence(presence_pin.is_high(), now);
        update_status_led(&mut status_led, state.status_led_rgb(now));
        #[cfg(feature = "usb-console")]
        {
            let current_runtime_debug_line = runtime_debug_line(&state);
            if current_runtime_debug_line != last_runtime_debug_line {
                write_line(&mut usb_serial, &current_runtime_debug_line);
                last_runtime_debug_line = current_runtime_debug_line;
            }
        }

        if let Ok(byte) = usb_serial.read_byte() {
            match byte {
                b'\n' => {
                    process_command_buffer(
                        &mut command_buffer,
                        &mut state,
                        &boot_started,
                        &mut usb_serial,
                        &mut radar_uart,
                        settings.as_mut(),
                        &mut wifi_controller,
                    )
                    .await;
                }
                b'\r' => {}
                0x08 | 0x7f => {
                    let _ = command_buffer.pop();
                }
                byte if !byte.is_ascii_control() => {
                    let _ = command_buffer.push(byte);
                }
                _ => {}
            }
        }

        yield_now().await;
    }
}

fn poll_radar_uart(
    radar_uart: &mut Uart<'_, Blocking>,
    state: &mut FirmwareState,
    uptime_ms: u32,
) {
    let mut read_buffer = [0_u8; 32];

    while radar_uart.read_ready() {
        let Ok(read) = radar_uart.read_buffered(&mut read_buffer) else {
            break;
        };

        if read == 0 {
            break;
        }

        for &byte in &read_buffer[..read] {
            state.radar_bytes_total = state.radar_bytes_total.saturating_add(1);
            state.last_radar_byte_ms = uptime_ms;

            if state.radar_buffer.len() >= RADAR_FRAME_BUFFER_SIZE {
                flush_radar_buffer_if_needed(state, uptime_ms, true);
            }

            let _ = state.radar_buffer.push(byte);
        }
    }

    flush_radar_buffer_if_needed(state, uptime_ms, false);
}

async fn poll_wifi_runtime(
    spawner: &Spawner,
    state: &mut FirmwareState,
    wifi_device: &mut Option<esp_hal::peripherals::WIFI<'static>>,
    wifi_controller: &mut Option<WifiController<'static>>,
    ap_stack: &mut Option<NetStack<'static>>,
    station_stack: &mut Option<NetStack<'static>>,
    _usb_serial: &mut UsbSerialJtag<'_, esp_hal::Blocking>,
    uptime_ms: u32,
) {
    if let Some(stack) = station_stack.as_ref().copied() {
        state.sync_ip_config(stack);
    } else {
        state.ip_address.clear();
        state.subnet_mask.clear();
        state.gateway_ip.clear();
        state.dns_1.clear();
        state.dns_2.clear();
        state.broadcast_ip.clear();
    }

    if wifi_controller.is_none() {
        let Some(device) = wifi_device.take() else {
            return;
        };

        let controller_config =
            RadioWifiControllerConfig::default().with_initial_config(build_radio_config(state));

        match radio_wifi::new(device, controller_config) {
            Ok((mut controller, interfaces)) => {
                if wifi_runs_access_point(state) && ap_stack.is_none() {
                    match start_ap_network_runtime(spawner, interfaces.access_point) {
                        Ok(stack) => {
                            *ap_stack = Some(stack);
                        }
                        Err(err) => {
                            state.wifi_runtime_started = false;
                            state.wifi_disconnect_reason_text = err;
                            return;
                        }
                    }
                } else if !wifi_runs_access_point(state) {
                    *ap_stack = None;
                }

                if station_stack.is_none() {
                    match start_station_network_runtime(spawner, interfaces.station) {
                        Ok(stack) => {
                            *station_stack = Some(stack);
                        }
                        Err(err) => {
                            state.wifi_runtime_started = false;
                            state.wifi_disconnect_reason_text = err;
                            return;
                        }
                    }
                }

                match controller.set_config(&build_radio_config(state)) {
                    Ok(()) => {
                        state.wifi_runtime_started = true;
                        if state.home_assistant_configured() {
                            state.wifi_connected = false;
                            state.wifi_disconnect_reason = 0;
                            state.wifi_disconnect_reason_text =
                                String::from(DEFAULT_WIFI_CONNECTING_REASON_TEXT);
                            state.last_wifi_attempt_ms =
                                uptime_ms.saturating_sub(WIFI_CONNECT_RETRY_MS);
                        } else {
                            state.mark_wifi_disconnected(DEFAULT_WIFI_NOT_CONFIGURED_REASON_TEXT);
                        }
                        state.wifi_reconfigure_pending = false;
                        *wifi_controller = Some(controller);
                    }
                    Err(err) => {
                        state.wifi_runtime_started = false;
                        state.mark_wifi_controller_error("set_config", &err);
                    }
                }
            }
            Err(err) => {
                state.wifi_runtime_started = false;
                state.mark_wifi_controller_error("new", &err);
            }
        }

        return;
    }

    let Some(controller) = wifi_controller.as_mut() else {
        return;
    };

    if let Ok((channel, _secondary)) = controller.channel() {
        state.wifi_channel = channel;
    }

    if state.wifi_reconfigure_pending {
        match controller.set_config(&build_radio_config(state)) {
            Ok(()) => {
                state.wifi_runtime_started = true;
                if state.home_assistant_configured() {
                    attempt_wifi_connect(controller, state, uptime_ms).await;
                } else {
                    state.mark_wifi_disconnected(DEFAULT_WIFI_NOT_CONFIGURED_REASON_TEXT);
                }
            }
            Err(err) => {
                state.wifi_runtime_started = false;
                state.mark_wifi_controller_error("set_config", &err);
            }
        }
        state.wifi_reconfigure_pending = false;
    }

    if controller.is_connected() {
        state.wifi_connected = true;
        state.wifi_disconnect_reason = 0;
        state.wifi_disconnect_reason_text = String::from("connected");

        if uptime_ms.saturating_sub(state.last_wifi_status_poll_ms) >= WIFI_STATUS_POLL_MS {
            state.last_wifi_status_poll_ms = uptime_ms;

            if let Ok(access_point_info) = controller.ap_info() {
                state.wifi_bssid = mac_address_string(access_point_info.bssid);
                state.wifi_channel = access_point_info.channel;
                state.wifi_rssi_dbm = i32::from(access_point_info.signal_strength);
            }

            if let Ok(rssi_dbm) = controller.rssi() {
                state.wifi_rssi_dbm = rssi_dbm;
            }
        }
    } else {
        if state.wifi_runtime_started {
            if state.wifi_disconnect_reason_text == DEFAULT_WIFI_CONNECTING_REASON_TEXT {
                state.wifi_connected = false;
                state.wifi_disconnect_reason = 0;
                state.wifi_rssi_dbm = 0;
                state.wifi_bssid.clear();
            } else if state.wifi_disconnect_reason_text.starts_with("disconnected:") {
                state.wifi_connected = false;
                state.wifi_rssi_dbm = 0;
            } else if state.wifi_disconnect_reason_text.starts_with("connect_err:")
                || state.wifi_disconnect_reason_text.starts_with("set_config_err:")
                || state.wifi_disconnect_reason_text.starts_with("new_err:")
            {
                state.wifi_connected = false;
                state.wifi_disconnect_reason = 0;
                state.wifi_rssi_dbm = 0;
                state.wifi_bssid.clear();
            } else {
                state.mark_wifi_disconnected(DEFAULT_WIFI_DISCONNECT_REASON_TEXT);
            }
        } else {
            state.mark_wifi_disconnected(DEFAULT_WIFI_DISCONNECT_REASON_TEXT);
        }

        if state.home_assistant_configured()
            && uptime_ms.saturating_sub(state.last_wifi_attempt_ms) >= WIFI_CONNECT_RETRY_MS
        {
            attempt_wifi_connect(controller, state, uptime_ms).await;
        }
    }
}

async fn attempt_wifi_connect(
    controller: &mut WifiController<'static>,
    state: &mut FirmwareState,
    uptime_ms: u32,
) {
    match with_timeout(
        Duration::from_millis(WIFI_CONNECT_TIMEOUT_MS),
        controller.connect_async(),
    )
    .await
    {
        Ok(Ok(info)) => {
            let _ = controller.set_power_saving(PowerSaveMode::Maximum);
            state.wifi_connected = true;
            state.wifi_disconnect_reason = 0;
            state.wifi_disconnect_reason_text = String::from("connected");
            state.wifi_channel = info.channel;
            state.wifi_bssid = mac_address_string(info.bssid);
        }
        Ok(Err(err)) => {
            state.mark_wifi_connect_error(&err);
        }
        Err(_) => {
            state.wifi_connected = false;
            state.wifi_disconnect_reason = 0;
            state.wifi_disconnect_reason_text = String::from(DEFAULT_WIFI_CONNECTING_REASON_TEXT);
            state.wifi_rssi_dbm = 0;
            state.wifi_bssid.clear();
        }
    }

    state.last_wifi_attempt_ms = uptime_ms;
}
fn build_radio_config(state: &FirmwareState) -> RadioWifiConfig {
    let access_point_config = AccessPointConfig::default()
        .with_ssid(state.access_point_ssid())
        .with_password(state.access_point_password())
        .with_auth_method(WifiAuthenticationMethod::Wpa2Personal);

    if state.home_assistant_configured() {
        RadioWifiConfig::Station(build_station_config(state))
    } else {
        RadioWifiConfig::AccessPoint(access_point_config)
    }
}

fn wifi_runs_access_point(state: &FirmwareState) -> bool {
    !state.home_assistant_configured()
}

fn start_ap_network_runtime(spawner: &Spawner, interface: WifiInterface<'static>) -> Result<NetStack<'static>, String> {
    let resources = AP_NET_RESOURCES.init(NetStackResources::new());
    let (stack, runner) = embassy_net::new(
        interface,
        NetConfig::ipv4_static(ap_static_config()),
        resources,
        0x3cdc757153dc_u64,
    );

    let ap_net_token = ap_net_task(runner).map_err(|_| String::from("ap_net_spawn_failed"))?;
    spawner.spawn(ap_net_token);

    let ap_dhcp_token = ap_dhcp_task(stack).map_err(|_| String::from("ap_dhcp_spawn_failed"))?;
    spawner.spawn(ap_dhcp_token);

    let http_token = ap_http_server_task(stack).map_err(|_| String::from("http_spawn_failed"))?;
    spawner.spawn(http_token);

    Ok(stack)
}

fn start_station_network_runtime(
    spawner: &Spawner,
    interface: WifiInterface<'static>,
) -> Result<NetStack<'static>, String> {
    let resources = STATION_NET_RESOURCES.init(NetStackResources::new());
    let (stack, runner) = embassy_net::new(
        interface,
        NetConfig::dhcpv4(Default::default()),
        resources,
        0x3cdc757153dd_u64,
    );

    let station_net_token = station_net_task(runner)
        .map_err(|_| String::from("station_net_spawn_failed"))?;
    spawner.spawn(station_net_token);

    let http_token = station_http_server_task(stack)
        .map_err(|_| String::from("station_http_spawn_failed"))?;
    spawner.spawn(http_token);

    let discovery_token = station_udp_discovery_task(stack)
        .map_err(|_| String::from("station_udp_discovery_spawn_failed"))?;
    spawner.spawn(discovery_token);

    if !MQTT_TASK_STARTED.swap(true, Ordering::AcqRel) {
        let mqtt_token = station_mqtt_summary_task(stack)
            .map_err(|_| String::from("station_mqtt_spawn_failed"))?;
        spawner.spawn(mqtt_token);
    }

    Ok(stack)
}

fn process_mqtt_runtime(state: &mut FirmwareState, uptime_ms: u32) {
    while let Ok(event) = MQTT_EVENT_CHANNEL.receiver().try_receive() {
        match event {
            MqttTaskEvent::Status(status) => state.apply_mqtt_status(status),
            MqttTaskEvent::RoomSummary(payload) => {
                state.sync_room_summary_peer(payload.as_str(), uptime_ms)
            }
        }
    }

    if state.mqtt_runtime_generation != state.mqtt_runtime_announced_generation
        || state.wifi_connected != state.mqtt_runtime_announced_wifi_connected
    {
        let config = state.mqtt_task_config();
        if MQTT_COMMAND_CHANNEL.try_send(MqttTaskCommand::Configure(config)).is_ok() {
            state.mqtt_runtime_announced_generation = state.mqtt_runtime_generation;
            state.mqtt_runtime_announced_wifi_connected = state.wifi_connected;
        }
    }

    if state.wifi_connected
        && uptime_ms.saturating_sub(state.last_mqtt_room_summary_publish_ms)
            >= MQTT_SUMMARY_PUBLISH_MS
    {
        if let Some(payload) = build_room_summary_payload(state, uptime_ms) {
            if MQTT_COMMAND_CHANNEL
                .try_send(MqttTaskCommand::Publish(build_room_summary_publish_message(
                    state, payload,
                )))
                .is_ok()
            {
                state.last_mqtt_room_summary_publish_ms = uptime_ms;
            }
        }
    }
}

#[cfg(feature = "ble-scan")]
fn process_ble_scan_events(state: &mut FirmwareState, uptime_ms: u32) {
    while let Ok(event) = BLE_SCAN_EVENT_CHANNEL.try_receive() {
        let address = format_ble_address(event.address);
        let name = extract_ble_local_name(event.adv_data.as_slice(), event.scan_data.as_slice());
        let service_uuid =
            extract_ble_service_uuid(event.adv_data.as_slice(), event.scan_data.as_slice());

        record_ble_advertisement(
            state.ble_sightings.as_mut_slice(),
            state.ble_identity_tags.as_mut_slice(),
            address.as_str(),
            name.as_str(),
            service_uuid.as_str(),
            i32::from(event.rssi),
            uptime_ms,
        );
    }
}

#[cfg(not(feature = "ble-scan"))]
fn process_ble_scan_events(_state: &mut FirmwareState, _uptime_ms: u32) {}

fn process_udp_discovery(state: &mut FirmwareState, station_stack: Option<NetStack<'static>>, uptime_ms: u32) {
    if station_stack.is_none() {
        state.clear_udp_discovery_peers();
        return;
    }

    if !state.wifi_connected {
        state.udp_discovery_peers.retain(|peer| {
            uptime_ms.saturating_sub(peer.last_seen_ms) <= UDP_DISCOVERY_PEER_FRESHNESS_MS
        });
        return;
    }

    if uptime_ms.saturating_sub(state.last_udp_discovery_announce_ms) >= UDP_DISCOVERY_ANNOUNCE_MS {
        if let Some(payload) = build_udp_discovery_payload(state, uptime_ms) {
            if UDP_DISCOVERY_ANNOUNCE_CHANNEL.try_send(payload).is_ok() {
                state.last_udp_discovery_announce_ms = uptime_ms;
            }
        }
    }

    while let Ok(payload) = UDP_DISCOVERY_PACKET_CHANNEL.try_receive() {
        state.sync_udp_discovery_peer(payload.as_str(), uptime_ms);
        state.sync_room_summary_peer(payload.as_str(), uptime_ms);
    }

    state.udp_discovery_peers.retain(|peer| {
        uptime_ms.saturating_sub(peer.last_seen_ms) <= UDP_DISCOVERY_PEER_FRESHNESS_MS
    });
}

fn build_udp_discovery_payload(
    state: &FirmwareState,
    uptime_ms: u32,
) -> Option<heapless::String<384>> {
    let mut payload = heapless::String::<384>::new();
    payload.push_str("{").ok()?;
    payload.push_str("\"kind\":\"lb_udp_discovery\",").ok()?;
    push_json_string_field(&mut payload, "node_id", &state.node_id)?;
    payload.push(',').ok()?;
    push_json_string_field(&mut payload, "friendly_name", &state.friendly_name)?;
    payload.push(',').ok()?;
    push_json_string_field(&mut payload, "room_id", &state.room_id)?;
    payload.push(',').ok()?;
    push_json_string_field(&mut payload, "sensor_role", &state.sensor_role)?;
    payload.push(',').ok()?;
    push_json_string_field(&mut payload, "firmware_version", RUST_FIRMWARE_VERSION)?;
    payload.push(',').ok()?;
    push_json_string_field(&mut payload, "build_target", RUST_BUILD_TARGET)?;
    payload.push(',').ok()?;
    push_json_string_field(&mut payload, "hostname", &state.device_hostname())?;
    payload.push(',').ok()?;
    push_json_string_field(&mut payload, "ip_address", &state.ip_address)?;
    payload.push_str(",\"wifi_rssi_dbm\":").ok()?;
    append_i32_heapless(&mut payload, state.wifi_rssi_dbm)?;
    payload.push_str(",\"wifi_channel\":").ok()?;
    append_u32_heapless(&mut payload, u32::from(state.wifi_channel))?;
    payload.push_str(",\"uptime_s\":").ok()?;
    append_u32_heapless(&mut payload, uptime_ms / 1000)?;
    payload.push_str(",\"free_heap_bytes\":").ok()?;
    append_u32_heapless(&mut payload, free_heap_bytes())?;
    payload.push('}').ok()?;
    Some(payload)
}

fn build_room_summary_payload(
    state: &FirmwareState,
    uptime_ms: u32,
) -> Option<heapless::String<384>> {
    if !state.wifi_connected || uptime_ms.saturating_sub(state.last_udp_discovery_announce_ms) % ROOM_SUMMARY_KEEPALIVE_MS >= UDP_DISCOVERY_ANNOUNCE_MS {
        return None;
    }

    let metrics = state.active_radar_metrics();
    let mut payload = heapless::String::<384>::new();
    payload.push_str("{").ok()?;
    push_json_string_field(&mut payload, "kind", "lb_room_summary")?;
    payload.push(',').ok()?;
    push_json_string_field(&mut payload, "node_id", &state.node_id)?;
    payload.push(',').ok()?;
    push_json_string_field(&mut payload, "room_id", &state.room_id)?;
    payload.push(',').ok()?;
    push_json_string_field(&mut payload, "sensor_role", &state.sensor_role)?;
    payload.push(',').ok()?;
    push_json_string_field(&mut payload, "firmware_version", RUST_FIRMWARE_VERSION)?;
    payload.push(',').ok()?;
    push_json_string_field(&mut payload, "build_target", RUST_BUILD_TARGET)?;
    payload.push_str(",\"pose_x_cm\":").ok()?;
    append_i32_heapless(&mut payload, state.pose_x_cm)?;
    payload.push_str(",\"pose_y_cm\":").ok()?;
    append_i32_heapless(&mut payload, state.pose_y_cm)?;
    payload.push_str(",\"heading_deg\":").ok()?;
    append_i32_heapless(&mut payload, state.heading_deg)?;
    payload.push_str(",\"room_width_cm\":").ok()?;
    append_u32_heapless(&mut payload, u32::from(state.room_width_cm))?;
    payload.push_str(",\"room_height_cm\":").ok()?;
    append_u32_heapless(&mut payload, u32::from(state.room_height_cm))?;
    payload.push_str(",\"presence\":").ok()?;
    payload.push_str(if state.presence { "true" } else { "false" }).ok()?;
    payload.push_str(",\"detection_candidate\":").ok()?;
    payload.push_str(if state.detection_candidate { "true" } else { "false" }).ok()?;
    payload.push_str(",\"people_estimate\":").ok()?;
    append_u32_heapless(
        &mut payload,
        metrics.as_ref().map(|value| u32::from(value.estimated_people)).unwrap_or(0),
    )?;
    payload.push_str(",\"active_gate_count\":").ok()?;
    append_u32_heapless(
        &mut payload,
        metrics.as_ref().map(|value| u32::from(value.active_gate_count)).unwrap_or(0),
    )?;
    payload.push_str(",\"dominant_gate_distance_cm\":").ok()?;
    append_i32_heapless(
        &mut payload,
        metrics.as_ref().map(|value| value.dominant_gate_distance_cm).unwrap_or(-1),
    )?;
    payload.push_str(",\"activity_score\":").ok()?;
    append_u32_heapless(
        &mut payload,
        metrics.as_ref().map(|value| u32::from(value.activity_score)).unwrap_or(0),
    )?;
    payload.push_str(",\"updated_ms\":").ok()?;
    append_u32_heapless(&mut payload, uptime_ms)?;
    payload.push('}').ok()?;
    Some(payload)
}

fn heapless_string<const N: usize>(value: &str) -> heapless::String<N> {
    let mut output = heapless::String::<N>::new();
    let _ = output.push_str(value);
    output
}

fn parse_ipv4_address(value: &str) -> Option<Ipv4Address> {
    let mut octets = [0_u8; 4];
    let mut count = 0;
    for part in value.split('.') {
        if count >= 4 {
            return None;
        }
        octets[count] = part.parse::<u8>().ok()?;
        count += 1;
    }

    (count == 4).then(|| Ipv4Address::new(octets[0], octets[1], octets[2], octets[3]))
}

fn push_mqtt_status(connected: bool, state: i32, state_text: &str, host_ip: &str) {
    let mut text = heapless::String::<32>::new();
    let _ = text.push_str(state_text);
    let mut host = heapless::String::<32>::new();
    let _ = host.push_str(host_ip);
    let _ = MQTT_EVENT_CHANNEL.try_send(MqttTaskEvent::Status(MqttTaskStatus {
        connected,
        state,
        state_text: text,
        host_ip: host,
    }));
}

fn encode_mqtt_remaining_length<const N: usize>(
    output: &mut heapless::Vec<u8, N>,
    mut value: usize,
) -> Option<()> {
    loop {
        let mut encoded = (value % 128) as u8;
        value /= 128;
        if value > 0 {
            encoded |= 0x80;
        }
        output.push(encoded).ok()?;
        if value == 0 {
            return Some(());
        }
    }
}

fn encode_mqtt_utf8<const N: usize>(output: &mut heapless::Vec<u8, N>, value: &str) -> Option<()> {
    let len = value.len();
    if len > u16::MAX as usize {
        return None;
    }
    output.extend_from_slice(&(len as u16).to_be_bytes()).ok()?;
    output.extend_from_slice(value.as_bytes()).ok()?;
    Some(())
}

fn build_mqtt_connect_packet(config: &MqttTaskConfig) -> Option<heapless::Vec<u8, 512>> {
    let mut variable_and_payload = heapless::Vec::<u8, 512>::new();
    encode_mqtt_utf8(&mut variable_and_payload, "MQTT")?;
    variable_and_payload.push(0x04).ok()?;
    let mut flags = 0x02;
    if !config.username.is_empty() {
        flags |= 0x80;
    }
    if !config.password.is_empty() {
        flags |= 0x40;
    }
    variable_and_payload.push(flags).ok()?;
    variable_and_payload
        .extend_from_slice(&MQTT_KEEPALIVE_SECS.to_be_bytes())
        .ok()?;
    encode_mqtt_utf8(&mut variable_and_payload, config.node_id.as_str())?;
    if !config.username.is_empty() {
        encode_mqtt_utf8(&mut variable_and_payload, config.username.as_str())?;
    }
    if !config.password.is_empty() {
        encode_mqtt_utf8(&mut variable_and_payload, config.password.as_str())?;
    }

    let mut packet = heapless::Vec::<u8, 512>::new();
    packet.push(0x10).ok()?;
    encode_mqtt_remaining_length(&mut packet, variable_and_payload.len())?;
    packet.extend_from_slice(variable_and_payload.as_slice()).ok()?;
    Some(packet)
}

fn build_mqtt_subscribe_packet(config: &MqttTaskConfig) -> Option<heapless::Vec<u8, 256>> {
    let mut topic = heapless::String::<128>::new();
    topic.push_str(DEFAULT_TOPIC_PREFIX).ok()?;
    topic.push_str("/rooms/").ok()?;
    topic
        .push_str(&sanitize_hostname(if config.room_id.is_empty() {
            DEFAULT_ROOM_ID
        } else {
            config.room_id.as_str()
        }))
        .ok()?;
    topic.push_str("/nodes/+/summary").ok()?;

    let mut variable_and_payload = heapless::Vec::<u8, 256>::new();
    variable_and_payload
        .extend_from_slice(&MQTT_PACKET_ID_SUBSCRIBE.to_be_bytes())
        .ok()?;
    encode_mqtt_utf8(&mut variable_and_payload, topic.as_str())?;
    variable_and_payload.push(0x00).ok()?;

    let mut packet = heapless::Vec::<u8, 256>::new();
    packet.push(0x82).ok()?;
    encode_mqtt_remaining_length(&mut packet, variable_and_payload.len())?;
    packet.extend_from_slice(variable_and_payload.as_slice()).ok()?;
    Some(packet)
}

fn build_mqtt_publish_packet(
    topic: &str,
    payload: &str,
    retain: bool,
) -> Option<heapless::Vec<u8, 768>> {
    let mut variable_and_payload = heapless::Vec::<u8, 768>::new();
    encode_mqtt_utf8(&mut variable_and_payload, topic)?;
    variable_and_payload.extend_from_slice(payload.as_bytes()).ok()?;

    let mut packet = heapless::Vec::<u8, 768>::new();
    packet.push(if retain { 0x31 } else { 0x30 }).ok()?;
    encode_mqtt_remaining_length(&mut packet, variable_and_payload.len())?;
    packet.extend_from_slice(variable_and_payload.as_slice()).ok()?;
    Some(packet)
}

fn build_mqtt_pingreq_packet() -> heapless::Vec<u8, 2> {
    let mut packet = heapless::Vec::<u8, 2>::new();
    let _ = packet.extend_from_slice(&[0xC0, 0x00]);
    packet
}

fn build_room_summary_publish_message(
    state: &FirmwareState,
    payload: heapless::String<384>,
) -> MqttPublishMessage {
    let mut topic = heapless::String::<128>::new();
    let _ = topic.push_str(DEFAULT_TOPIC_PREFIX);
    let _ = topic.push_str("/rooms/");
    let _ = topic.push_str(&sanitize_hostname(if state.room_id.is_empty() {
        DEFAULT_ROOM_ID
    } else {
        state.room_id.as_str()
    }));
    let _ = topic.push_str("/nodes/");
    let _ = topic.push_str(state.node_id.as_str());
    let _ = topic.push_str("/summary");

    MqttPublishMessage {
        topic,
        payload,
        retain: true,
    }
}

fn build_room_pose_publish_message(
    state: &FirmwareState,
    payload: &RoomPosePublishPayload<'_>,
) -> Option<MqttPublishMessage> {
    if payload.node_id.is_empty() {
        return None;
    }

    let mut topic = heapless::String::<128>::new();
    topic.push_str(DEFAULT_TOPIC_PREFIX).ok()?;
    topic.push_str("/rooms/").ok()?;
    topic
        .push_str(&sanitize_hostname(if state.room_id.is_empty() {
            DEFAULT_ROOM_ID
        } else {
            state.room_id.as_str()
        }))
        .ok()?;
    topic.push_str("/nodes/").ok()?;
    topic.push_str(payload.node_id).ok()?;
    topic.push_str("/pose/set").ok()?;

    let mut body = heapless::String::<384>::new();
    body.push_str("{").ok()?;
    push_json_string_field(&mut body, "node_id", payload.node_id)?;
    body.push(',').ok()?;
    push_json_string_field(
        &mut body,
        "room_id",
        if payload.room_id.is_empty() {
            state.room_id.as_str()
        } else {
            payload.room_id
        },
    )?;
    body.push(',').ok()?;
    push_json_string_field(
        &mut body,
        "sensor_role",
        if payload.sensor_role.is_empty() {
            state.sensor_role.as_str()
        } else {
            payload.sensor_role
        },
    )?;
    body.push_str(",\"pose_x_cm\":").ok()?;
    append_i32_heapless(&mut body, i32::from(payload.pose_x_cm))?;
    body.push_str(",\"pose_y_cm\":").ok()?;
    append_i32_heapless(&mut body, i32::from(payload.pose_y_cm))?;
    body.push_str(",\"heading_deg\":").ok()?;
    append_i32_heapless(&mut body, i32::from(payload.heading_deg))?;
    body.push_str(",\"room_width_cm\":").ok()?;
    append_u32_heapless(&mut body, u32::from(payload.room_width_cm))?;
    body.push_str(",\"room_height_cm\":").ok()?;
    append_u32_heapless(&mut body, u32::from(payload.room_height_cm))?;
    body.push('}').ok()?;

    Some(MqttPublishMessage {
        topic,
        payload: body,
        retain: false,
    })
}

async fn mqtt_send_connect(socket: &mut TcpSocket<'_>, config: &MqttTaskConfig) -> Result<(), ()> {
    let packet = build_mqtt_connect_packet(config).ok_or(())?;
    socket_write_all(socket, packet.as_slice()).await?;
    socket.flush().await.map_err(|_| ())
}

async fn mqtt_send_subscribe_room_summary(
    socket: &mut TcpSocket<'_>,
    config: &MqttTaskConfig,
) -> Result<(), ()> {
    let packet = build_mqtt_subscribe_packet(config).ok_or(())?;
    socket_write_all(socket, packet.as_slice()).await?;
    socket.flush().await.map_err(|_| ())
}

async fn mqtt_send_publish_message(
    socket: &mut TcpSocket<'_>,
    message: &MqttPublishMessage,
) -> Result<(), ()> {
    let packet = build_mqtt_publish_packet(message.topic.as_str(), message.payload.as_str(), message.retain)
        .ok_or(())?;
    socket_write_all(socket, packet.as_slice()).await?;
    socket.flush().await.map_err(|_| ())
}

async fn mqtt_send_pingreq(socket: &mut TcpSocket<'_>) -> Result<(), ()> {
    let packet = build_mqtt_pingreq_packet();
    socket_write_all(socket, packet.as_slice()).await?;
    socket.flush().await.map_err(|_| ())
}

fn mqtt_is_pingresp_packet(packet: &[u8]) -> bool {
    packet.len() == 2 && packet[0] == 0xD0 && packet[1] == 0x00
}

fn mqtt_packet_length(buffer: &[u8]) -> Result<Option<usize>, ()> {
    if buffer.len() < 2 {
        return Ok(None);
    }

    let mut multiplier = 1_usize;
    let mut value = 0_usize;
    let mut index = 1_usize;
    loop {
        if index >= buffer.len() {
            return Ok(None);
        }
        let encoded = buffer[index];
        value = value
            .checked_add(((encoded & 0x7f) as usize).checked_mul(multiplier).ok_or(())?)
            .ok_or(())?;
        index += 1;
        if encoded & 0x80 == 0 {
            break;
        }
        multiplier = multiplier.checked_mul(128).ok_or(())?;
        if multiplier > 128 * 128 * 128 {
            return Err(());
        }
    }

    let total = index.checked_add(value).ok_or(())?;
    if buffer.len() < total {
        Ok(None)
    } else {
        Ok(Some(total))
    }
}

fn take_mqtt_packet(buffer: &mut heapless::Vec<u8, 2048>) -> Option<heapless::Vec<u8, 1024>> {
    let packet_len = mqtt_packet_length(buffer.as_slice()).ok().flatten()?;
    if packet_len > 1024 {
        return None;
    }

    let mut packet = heapless::Vec::<u8, 1024>::new();
    packet.extend_from_slice(&buffer.as_slice()[..packet_len]).ok()?;
    let remaining = buffer.len().saturating_sub(packet_len);
    buffer.as_mut_slice().copy_within(packet_len.., 0);
    buffer.truncate(remaining);
    Some(packet)
}

async fn mqtt_wait_for_packet(
    socket: &mut TcpSocket<'_>,
    inbound: &mut heapless::Vec<u8, 2048>,
    read_buffer: &mut [u8; 512],
) -> Result<heapless::Vec<u8, 1024>, ()> {
    loop {
        if let Some(packet) = take_mqtt_packet(inbound) {
            return Ok(packet);
        }

        let size = socket.read(read_buffer).await.map_err(|_| ())?;
        if size == 0 || inbound.extend_from_slice(&read_buffer[..size]).is_err() {
            return Err(());
        }
    }
}

async fn mqtt_wait_for_connack(
    socket: &mut TcpSocket<'_>,
    inbound: &mut heapless::Vec<u8, 2048>,
    read_buffer: &mut [u8; 512],
) -> Result<(), ()> {
    let packet = mqtt_wait_for_packet(socket, inbound, read_buffer).await?;
    if packet.len() < 4 || (packet[0] >> 4) != 0x02 || packet[3] != 0 {
        return Err(());
    }
    Ok(())
}

async fn mqtt_wait_for_suback(
    socket: &mut TcpSocket<'_>,
    inbound: &mut heapless::Vec<u8, 2048>,
    read_buffer: &mut [u8; 512],
) -> Result<(), ()> {
    let packet = mqtt_wait_for_packet(socket, inbound, read_buffer).await?;
    if packet.is_empty() || (packet[0] >> 4) != 0x09 {
        return Err(());
    }
    Ok(())
}

fn append_u32_heapless(output: &mut heapless::String<384>, value: u32) -> Option<()> {
    let mut digits = [0_u8; 10];
    let mut remaining = value;
    let mut count = 0;

    loop {
        digits[count] = (remaining % 10) as u8;
        count += 1;
        remaining /= 10;
        if remaining == 0 {
            break;
        }
    }

    for index in (0..count).rev() {
        output.push(char::from(b'0' + digits[index])).ok()?;
    }

    Some(())
}

fn free_heap_bytes() -> u32 {
    core::cmp::min(esp_alloc::HEAP.free(), u32::MAX as usize) as u32
}

fn append_i32_heapless(output: &mut heapless::String<384>, value: i32) -> Option<()> {
    if value < 0 {
        output.push('-').ok()?;
        append_u32_heapless(output, value.unsigned_abs())
    } else {
        append_u32_heapless(output, value as u32)
    }
}

fn append_u32_decimal_string(output: &mut String, value: u32) {
    let _ = core::fmt::Write::write_fmt(output, format_args!("{}", value));
}

fn push_json_string_field(
    payload: &mut heapless::String<384>,
    key: &str,
    value: &str,
) -> Option<()> {
    payload.push('"').ok()?;
    payload.push_str(key).ok()?;
    payload.push_str("\":\"").ok()?;
    for ch in value.chars() {
        match ch {
            '\\' => payload.push_str("\\\\").ok()?,
            '"' => payload.push_str("\\\"").ok()?,
            '\n' => payload.push_str("\\n").ok()?,
            '\r' => payload.push_str("\\r").ok()?,
            '\t' => payload.push_str("\\t").ok()?,
            _ => payload.push(ch).ok()?,
        }
    }
    payload.push('"').ok()?;
    Some(())
}

fn json_field_string(payload: &str, field: &str) -> Option<String> {
    let mut pattern = String::from("\"");
    pattern.push_str(field);
    pattern.push_str("\":\"");
    let start = payload.find(&pattern)? + pattern.len();
    let rest = &payload[start..];
    let end = rest.find('"')?;
    Some(String::from(&rest[..end]))
}

fn json_field_i32(payload: &str, field: &str) -> Option<i32> {
    json_field_number(payload, field)?.parse::<i32>().ok()
}

fn json_field_u32(payload: &str, field: &str) -> Option<u32> {
    json_field_number(payload, field)?.parse::<u32>().ok()
}

fn json_field_number<'a>(payload: &'a str, field: &str) -> Option<&'a str> {
    let mut pattern = String::from("\"");
    pattern.push_str(field);
    pattern.push_str("\":");
    let start = payload.find(&pattern)? + pattern.len();
    let rest = &payload[start..];
    let end = rest.find([',', '}']).unwrap_or(rest.len());
    Some(rest[..end].trim())
}

fn semantic_version_core(version: &str) -> String {
    let trimmed = version.trim();
    if trimmed.is_empty() {
        return String::new();
    }

    let bytes = trimmed.as_bytes();
    let mut cursor = if matches!(bytes.first(), Some(b'v' | b'V')) { 1 } else { 0 };
    let major_start = cursor;
    while cursor < bytes.len() && bytes[cursor].is_ascii_digit() {
        cursor += 1;
    }
    if cursor <= major_start || cursor >= bytes.len() || bytes[cursor] != b'.' {
        return String::new();
    }

    let minor_start = cursor + 1;
    cursor = minor_start;
    while cursor < bytes.len() && bytes[cursor].is_ascii_digit() {
        cursor += 1;
    }
    if cursor <= minor_start || cursor >= bytes.len() || bytes[cursor] != b'.' {
        return String::new();
    }

    let patch_start = cursor + 1;
    cursor = patch_start;
    while cursor < bytes.len() && bytes[cursor].is_ascii_digit() {
        cursor += 1;
    }
    if cursor <= patch_start {
        return String::new();
    }

    String::from(&trimmed[major_start..cursor])
}

fn compare_semantic_versions(left: &str, right: &str) -> i32 {
    let left_core = semantic_version_core(left);
    let right_core = semantic_version_core(right);
    if left_core.is_empty() && right_core.is_empty() {
        return 0;
    }
    if left_core.is_empty() {
        return -1;
    }
    if right_core.is_empty() {
        return 1;
    }

    let mut left_parts = [0_u32; 3];
    let mut right_parts = [0_u32; 3];
    fill_semantic_parts(&left_core, &mut left_parts);
    fill_semantic_parts(&right_core, &mut right_parts);

    for index in 0..3 {
        if left_parts[index] < right_parts[index] {
            return -1;
        }
        if left_parts[index] > right_parts[index] {
            return 1;
        }
    }

    0
}

fn fill_semantic_parts(version: &str, parts: &mut [u32; 3]) {
    for (index, part) in version.split('.').take(3).enumerate() {
        parts[index] = part.parse::<u32>().unwrap_or(0);
    }
}

fn ap_static_config() -> StaticConfigV4 {
    let mut dns_servers = heapless::Vec::new();
    let _ = dns_servers.push(DEFAULT_AP_GATEWAY);

    StaticConfigV4 {
        address: Ipv4Cidr::new(DEFAULT_AP_GATEWAY, 24),
        gateway: Some(DEFAULT_AP_GATEWAY),
        dns_servers,
    }
}

fn dhcp_lease_address(client_mac: &[u8]) -> Ipv4Address {
    Ipv4Address::new(192, 168, 4, 20 + (client_mac[5] % 200))
}

fn ipv4_address_string(address: Ipv4Address) -> String {
    let mut text = String::new();
    let octets = address.octets();

    for (index, octet) in octets.iter().enumerate() {
        if index > 0 {
            text.push('.');
        }
        append_u32(&mut text, u32::from(*octet));
    }

    text
}

async fn request_http_response(request: HttpRequestMessage) -> Result<String, String> {
    HTTP_REQUEST_CHANNEL.sender().send(request).await;
    Ok(HTTP_RESPONSE_CHANNEL.receiver().receive().await)
}

async fn read_http_route(socket: &mut TcpSocket<'_>) -> Result<HttpRoute, String> {
    let mut buffer = [0_u8; 1024];
    let mut length = 0usize;

    loop {
        if length >= buffer.len() {
            return Err(http_response(
                "413 Payload Too Large",
                "application/json",
                r#"{"error":"http_request_too_large"}"#,
            ));
        }

        let read = socket
            .read(&mut buffer[length..])
            .await
            .map_err(|_| http_response("400 Bad Request", "application/json", r#"{"error":"http_read_failed"}"#))?;

        if read == 0 {
            return Err(http_response("400 Bad Request", "application/json", r#"{"error":"http_empty_request"}"#));
        }

        length += read;

        if let Some(header_end) = find_http_header_end(&buffer[..length]) {
            let request = core::str::from_utf8(&buffer[..length]).map_err(|_| {
                http_response("400 Bad Request", "application/json", r#"{"error":"http_invalid_utf8"}"#)
            })?;
            let content_length = parse_content_length(request).unwrap_or(0);
            let body_start = header_end + 4;

            if length < body_start + content_length {
                continue;
            }

            return parse_http_route(request, content_length);
        }
    }
}

fn parse_http_route(request: &str, content_length: usize) -> Result<HttpRoute, String> {
    let Some((head, body)) = request.split_once("\r\n\r\n") else {
        return Err(http_response("400 Bad Request", "application/json", r#"{"error":"http_malformed_request"}"#));
    };

    let Some(request_line) = head.lines().next() else {
        return Err(http_response("400 Bad Request", "application/json", r#"{"error":"http_missing_request_line"}"#));
    };

    let mut parts = request_line.split_whitespace();
    let method = parts.next().unwrap_or_default();
    let path = parts.next().unwrap_or_default();

    match (method, path) {
        ("GET", "/") | ("GET", "/index.html") => Ok(HttpRoute::RootPage),
        ("GET", "/api/snapshot") | ("GET", "/api/status") => Ok(HttpRoute::Snapshot),
        ("POST", "/api/command") => {
            let command_body = &body.as_bytes()[..content_length.min(body.len())];
            let command = core::str::from_utf8(command_body).map_err(|_| {
                http_response("400 Bad Request", "application/json", r#"{"error":"http_invalid_command_utf8"}"#)
            })?;
            let trimmed = command.trim();

            if trimmed.is_empty() {
                return Err(http_response("400 Bad Request", "application/json", r#"{"error":"http_empty_command"}"#));
            }

            let mut command_text = heapless::String::<256>::new();
            command_text.push_str(trimmed).map_err(|_| {
                http_response("413 Payload Too Large", "application/json", r#"{"error":"http_command_too_large"}"#)
            })?;

            Ok(HttpRoute::Command(command_text))
        }
        ("GET", _) => Ok(HttpRoute::RootPage),
        _ => Err(http_response("404 Not Found", "application/json", r#"{"error":"http_not_found"}"#)),
    }
}

fn parse_content_length(request: &str) -> Option<usize> {
    for line in request.lines() {
        let Some((name, value)) = line.split_once(':') else {
            continue;
        };

        if name.eq_ignore_ascii_case("Content-Length") {
            return value.trim().parse::<usize>().ok();
        }
    }

    None
}

fn find_http_header_end(buffer: &[u8]) -> Option<usize> {
    buffer.windows(4).position(|window| window == b"\r\n\r\n")
}

fn http_response(status: &str, content_type: &str, body: &str) -> String {
    let mut response = String::new();
    response.push_str("HTTP/1.1 ");
    response.push_str(status);
    response.push_str("\r\nContent-Type: ");
    response.push_str(content_type);
    response.push_str("\r\nContent-Length: ");
    append_u32(&mut response, body.len().min(u32::MAX as usize) as u32);
    response.push_str("\r\nConnection: close\r\nCache-Control: no-store\r\nAccess-Control-Allow-Origin: *\r\n\r\n");
    response.push_str(body);
    response
}

async fn send_http_response_header(
    socket: &mut TcpSocket<'_>,
    status: &str,
    content_type: &str,
    body_len: usize,
) -> Result<(), ()> {
    let mut response = String::new();
    response.push_str("HTTP/1.1 ");
    response.push_str(status);
    response.push_str("\r\nContent-Type: ");
    response.push_str(content_type);
    response.push_str("\r\nContent-Length: ");
    append_u32(&mut response, body_len.min(u32::MAX as usize) as u32);
    response.push_str("\r\nConnection: close\r\nCache-Control: no-store\r\nAccess-Control-Allow-Origin: *\r\n\r\n");
    socket_write_all(socket, response.as_bytes()).await
}

async fn send_http_body_chunks(socket: &mut TcpSocket<'_>, body: &[u8]) -> Result<(), ()> {
    const CHUNK_SIZE: usize = 1024;
    let mut offset = 0;

    while offset < body.len() {
        let end = (offset + CHUNK_SIZE).min(body.len());
        socket_write_all(socket, &body[offset..end]).await?;
        offset = end;
    }

    Ok(())
}

async fn socket_write_all(socket: &mut TcpSocket<'_>, mut bytes: &[u8]) -> Result<(), ()> {
    while !bytes.is_empty() {
        let written = socket.write(bytes).await.map_err(|_| ())?;
        if written == 0 {
            return Err(());
        }
        bytes = &bytes[written..];
    }

    Ok(())
}

async fn process_pending_http_request(
    state: &mut FirmwareState,
    boot_started: &Instant,
    usb_serial: &mut UsbSerialJtag<'_, esp_hal::Blocking>,
    radar_uart: &mut Uart<'_, Blocking>,
    settings: Option<&mut SettingsNvs>,
    wifi_controller: &mut Option<WifiController<'static>>,
) -> bool {
    let Ok(request) = HTTP_REQUEST_CHANNEL.receiver().try_receive() else {
        return false;
    };

    let response = match request {
        HttpRequestMessage::Snapshot => state.snapshot_json(uptime_ms(boot_started)),
        HttpRequestMessage::Command(command) => {
            handle_command(
                command.as_str(),
                state,
                boot_started,
                usb_serial,
                radar_uart,
                settings,
                wifi_controller,
            )
            .await
        }
    };

    let _ = HTTP_RESPONSE_CHANNEL.sender().try_send(response);
    true
}

struct OtaPartitionWriter<'a, F>
where
    F: NorFlash,
{
    partition: esp_bootloader_esp_idf::partitions::FlashRegion<'a, F>,
    offset: u32,
    trailing: [u8; 4],
    trailing_len: usize,
    total_bytes: usize,
}

impl<'a, F> OtaPartitionWriter<'a, F>
where
    F: NorFlash,
{
    fn new(
        mut partition: esp_bootloader_esp_idf::partitions::FlashRegion<'a, F>,
        image_len: Option<usize>,
    ) -> Result<Self, String> {
        let capacity = partition.capacity();
        let erase_len = image_len
            .map(|len| len.min(capacity))
            .map(|len| len.next_multiple_of(F::ERASE_SIZE))
            .unwrap_or(capacity)
            .min(capacity);
        partition
            .erase(0, erase_len.min(u32::MAX as usize) as u32)
            .map_err(|_| String::from("ota_partition_erase_failed"))?;

        Ok(Self {
            partition,
            offset: 0,
            trailing: [0xff; 4],
            trailing_len: 0,
            total_bytes: 0,
        })
    }

    fn write_chunk(&mut self, mut chunk: &[u8]) -> Result<(), String> {
        self.total_bytes = self.total_bytes.saturating_add(chunk.len());

        if self.trailing_len > 0 {
            let fill = (4 - self.trailing_len).min(chunk.len());
            self.trailing[self.trailing_len..self.trailing_len + fill]
                .copy_from_slice(&chunk[..fill]);
            self.trailing_len += fill;
            chunk = &chunk[fill..];

            if self.trailing_len == 4 {
                self.partition
                    .write(self.offset, &self.trailing)
                    .map_err(|_| String::from("ota_partition_write_failed"))?;
                self.offset = self.offset.saturating_add(4);
                self.trailing = [0xff; 4];
                self.trailing_len = 0;
            }
        }

        let aligned_len = chunk.len() - (chunk.len() % 4);
        if aligned_len > 0 {
            self.partition
                .write(self.offset, &chunk[..aligned_len])
                .map_err(|_| String::from("ota_partition_write_failed"))?;
            self.offset = self.offset.saturating_add(aligned_len as u32);
            chunk = &chunk[aligned_len..];
        }

        if !chunk.is_empty() {
            self.trailing[..chunk.len()].copy_from_slice(chunk);
            self.trailing_len = chunk.len();
        }

        Ok(())
    }

    fn finish(&mut self) -> Result<(), String> {
        if self.trailing_len == 0 {
            return Ok(());
        }

        self.partition
            .write(self.offset, &self.trailing)
            .map_err(|_| String::from("ota_partition_write_failed"))?;
        self.offset = self.offset.saturating_add(4);
        self.trailing = [0xff; 4];
        self.trailing_len = 0;
        Ok(())
    }
}

struct HttpResponseHeader {
    status_code: u16,
    content_length: Option<usize>,
    location: Option<String>,
    header_len: usize,
    bytes_filled: usize,
}

#[derive(Clone, Copy, Debug, Default)]
#[cfg(feature = "https-ota")]
struct TlsSocketError;

#[cfg(feature = "https-ota")]
impl core::fmt::Display for TlsSocketError {
    fn fmt(&self, formatter: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        formatter.write_str("tls_socket_error")
    }
}

#[cfg(feature = "https-ota")]
impl core::error::Error for TlsSocketError {}

#[cfg(feature = "https-ota")]
impl TlsIoError for TlsSocketError {
    fn kind(&self) -> TlsIoErrorKind {
        TlsIoErrorKind::Other
    }
}

#[cfg(feature = "https-ota")]
struct TlsTcpSocket<'a> {
    socket: TcpSocket<'a>,
}

#[cfg(feature = "https-ota")]
impl<'a> TlsTcpSocket<'a> {
    fn new(socket: TcpSocket<'a>) -> Self {
        Self { socket }
    }
}

#[cfg(feature = "https-ota")]
impl TlsErrorType for TlsTcpSocket<'_> {
    type Error = TlsSocketError;
}

#[cfg(feature = "https-ota")]
impl TlsRead for TlsTcpSocket<'_> {
    async fn read(&mut self, buf: &mut [u8]) -> Result<usize, Self::Error> {
        self.socket.read(buf).await.map_err(|_| TlsSocketError)
    }
}

#[cfg(feature = "https-ota")]
impl TlsWrite for TlsTcpSocket<'_> {
    async fn write(&mut self, buf: &[u8]) -> Result<usize, Self::Error> {
        self.socket.write(buf).await.map_err(|_| TlsSocketError)
    }

    async fn flush(&mut self) -> Result<(), Self::Error> {
        self.socket.flush().await.map_err(|_| TlsSocketError)
    }
}

#[cfg(feature = "https-ota")]
fn ota_client_config<'a>(server_name: Option<&'a CStr>) -> ClientSessionConfig<'a> {
    ClientSessionConfig {
        ca_chain: Some(Certificate::new(X509::PEM(OTA_CA_BUNDLE)).unwrap()),
        server_name,
        min_version: TlsVersion::Tls1_2,
        max_version: Some(TlsVersion::Tls1_2),
        ..ClientSessionConfig::new()
    }
}

async fn perform_http_firmware_update(
    station_stack: Option<NetStack<'static>>,
    mut flash_storage: SettingsStorage,
    download_url: &espwaverider_core::release::ParsedDownloadUrl,
) -> Result<usize, String> {
    let Some(stack) = station_stack else {
        return Err(String::from("firmware_sync_no_station_stack"));
    };

    if !stack.is_config_up() {
        return Err(String::from("firmware_sync_network_not_ready"));
    }

    let mut partition_table_bytes = Box::new([0_u8; PARTITION_TABLE_MAX_LEN]);
    let mut updater = OtaUpdater::new(&mut flash_storage, &mut partition_table_bytes)
        .map_err(|_| String::from("ota_partition_layout_invalid"))?;
    let (partition, _) = updater
        .next_partition()
        .map_err(|_| String::from("ota_next_partition_unavailable"))?;
    let bytes_written = download_http_firmware_image(stack, download_url, partition).await?;
    updater
        .activate_next_partition()
        .map_err(|_| String::from("ota_activate_partition_failed"))?;
    Ok(bytes_written)
}

#[cfg(feature = "https-ota")]
async fn perform_https_firmware_update(
    station_stack: Option<NetStack<'static>>,
    mut flash_storage: SettingsStorage,
    tls: TlsReference<'_>,
    download_url: &espwaverider_core::release::ParsedDownloadUrl,
) -> Result<usize, String> {
    let Some(stack) = station_stack else {
        return Err(String::from("firmware_sync_no_station_stack"));
    };

    if !stack.is_config_up() {
        return Err(String::from("firmware_sync_network_not_ready"));
    }

    let mut partition_table_bytes = Box::new([0_u8; PARTITION_TABLE_MAX_LEN]);
    let mut updater = OtaUpdater::new(&mut flash_storage, &mut partition_table_bytes)
        .map_err(|_| String::from("ota_partition_layout_invalid"))?;
    let (partition, _) = updater
        .next_partition()
        .map_err(|_| String::from("ota_next_partition_unavailable"))?;
    let bytes_written = download_https_firmware_image(stack, tls, download_url, partition).await?;
    updater
        .activate_next_partition()
        .map_err(|_| String::from("ota_activate_partition_failed"))?;
    Ok(bytes_written)
}

async fn download_http_firmware_image<F>(
    stack: NetStack<'static>,
    download_url: &espwaverider_core::release::ParsedDownloadUrl,
    partition: esp_bootloader_esp_idf::partitions::FlashRegion<'_, F>,
) -> Result<usize, String>
where
    F: NorFlash,
{
    let mut current_url = download_url.clone();

    for _ in 0..FIRMWARE_SYNC_MAX_REDIRECTS {
        let resolved_ip = resolve_download_host(stack, current_url.host.as_str()).await?;
        let mut rx_buffer = Box::new([0_u8; HTTP_SOCKET_BUFFER_SIZE]);
        let mut tx_buffer = Box::new([0_u8; HTTP_SOCKET_BUFFER_SIZE]);
        let mut socket = TcpSocket::new(stack, rx_buffer.as_mut(), tx_buffer.as_mut());
        socket
            .connect(IpEndpoint::new(IpAddress::Ipv4(resolved_ip), current_url.port))
            .await
            .map_err(|_| String::from("firmware_http_connect_failed"))?;

        let mut request = String::from("GET ");
        request.push_str(current_url.path.as_str());
        request.push_str(" HTTP/1.1\r\nHost: ");
        request.push_str(current_url.host.as_str());
        request.push_str("\r\nConnection: close\r\nUser-Agent: EspWaveRider-Rust/0.1.0\r\n\r\n");
        socket_write_all(&mut socket, request.as_bytes())
            .await
            .map_err(|_| String::from("firmware_http_request_failed"))?;

        let mut header_buffer = [0_u8; FIRMWARE_HTTP_HEADER_BUFFER_SIZE];
        let header = read_http_response_header_socket(&mut socket, &mut header_buffer).await?;

        match header.status_code {
            200 => {
                if let Some(content_length) = header.content_length {
                    if content_length > partition.capacity() {
                        return Err(String::from("ota_image_exceeds_partition"));
                    }
                }

                let mut writer = OtaPartitionWriter::new(partition, header.content_length)?;

                if header.header_len < header.bytes_filled {
                    let body_prefix = &header_buffer[header.header_len..header.bytes_filled];
                    if !body_prefix.is_empty() {
                        writer.write_chunk(body_prefix)?;
                    }
                }

                let mut read_buffer = [0_u8; FIRMWARE_HTTP_READ_BUFFER_SIZE];
                loop {
                    let read = socket
                        .read(&mut read_buffer)
                        .await
                        .map_err(|_| String::from("firmware_http_read_failed"))?;
                    if read == 0 {
                        break;
                    }
                    writer.write_chunk(&read_buffer[..read])?;
                }

                writer.finish()?;
                socket.close();
                let _ = socket.flush().await;
                return Ok(writer.total_bytes);
            }
            301 | 302 | 303 | 307 | 308 => {
                let Some(location) = header.location else {
                    return Err(String::from("firmware_http_redirect_missing_location"));
                };
                let Some(next_url) = parse_download_url(location.as_str()) else {
                    return Err(String::from("firmware_http_redirect_invalid_location"));
                };
                if next_url.scheme != DownloadUrlScheme::Http {
                    return Err(String::from("firmware_http_redirect_bad_scheme"));
                }
                current_url = next_url;
            }
            404 => return Err(String::from("firmware_http_not_found")),
            429 => return Err(String::from("firmware_http_rate_limited")),
            _ => return Err(String::from("firmware_http_bad_status")),
        }
    }

    Err(String::from("firmware_http_redirect_limit"))
}

#[cfg(feature = "https-ota")]
fn ensure_ota_tls_entropy() {
    if !TLS_ENTROPY_READY.load(Ordering::Acquire) {
        if TLS_ENTROPY_READY
            .compare_exchange(false, true, Ordering::AcqRel, Ordering::Acquire)
            .is_ok()
        {
            unsafe { TrngSource::increase_entropy_source_counter() };
        }
    }
}

#[cfg(feature = "https-ota")]
async fn download_https_firmware_image<F>(
    stack: NetStack<'static>,
    tls: TlsReference<'_>,
    download_url: &espwaverider_core::release::ParsedDownloadUrl,
    partition: esp_bootloader_esp_idf::partitions::FlashRegion<'_, F>,
) -> Result<usize, String>
where
    F: NorFlash,
{
    let mut current_url = download_url.clone();

    for _ in 0..FIRMWARE_SYNC_MAX_REDIRECTS {
        let resolved_ip = resolve_download_host(stack, current_url.host.as_str()).await?;
        let mut rx_buffer = [0_u8; HTTP_SOCKET_BUFFER_SIZE];
        let mut tx_buffer = [0_u8; HTTP_SOCKET_BUFFER_SIZE];
        let mut socket = TcpSocket::new(stack, &mut rx_buffer, &mut tx_buffer);
        socket
            .connect(IpEndpoint::new(IpAddress::Ipv4(resolved_ip), current_url.port))
            .await
            .map_err(|_| String::from("firmware_https_connect_failed"))?;
        let socket = TlsTcpSocket::new(socket);

        let server_name = CString::new(current_url.host.as_str())
            .map_err(|_| String::from("firmware_https_invalid_host"))?;
        let session_config = SessionConfig::Client(ota_client_config(Some(server_name.as_c_str())));
        let mut session = Session::new(tls, socket, &session_config)
            .map_err(|_| String::from("firmware_https_session_failed"))?;

        let mut request = String::from("GET ");
        request.push_str(current_url.path.as_str());
        request.push_str(" HTTP/1.1\r\nHost: ");
        request.push_str(current_url.host.as_str());
        request.push_str("\r\nConnection: close\r\nUser-Agent: EspWaveRider-Rust/0.1.0\r\n\r\n");
        write_all_io(&mut session, request.as_bytes())
            .await
            .map_err(|_| String::from("firmware_https_request_failed"))?;

        let mut header_buffer = Box::new([0_u8; FIRMWARE_HTTP_HEADER_BUFFER_SIZE]);
        let header = read_http_response_header_io(&mut session, header_buffer.as_mut()).await?;

        match header.status_code {
            200 => {
                if let Some(content_length) = header.content_length {
                    if content_length > partition.capacity() {
                        return Err(String::from("ota_image_exceeds_partition"));
                    }
                }

                let mut writer = OtaPartitionWriter::new(partition, header.content_length)?;
                let expected_total = header.content_length;

                let mut deferred_body_prefix = if header.header_len < header.bytes_filled {
                    Some(&header_buffer[header.header_len..header.bytes_filled])
                } else {
                    None
                };

                let mut read_buffer = Box::new([0_u8; FIRMWARE_HTTP_READ_BUFFER_SIZE]);
                loop {
                    if let Some(expected_total) = expected_total {
                        if writer.total_bytes >= expected_total {
                            break;
                        }
                    }

                    let read = match session.read(read_buffer.as_mut()).await {
                        Ok(read) => read,
                        Err(err) => {
                            return Err(format_https_session_error("firmware_https_read_failed", &err));
                        }
                    };
                    if read == 0 {
                        break;
                    }

                    if let Some(body_prefix) = deferred_body_prefix.take() {
                        if !body_prefix.is_empty() {
                            writer.write_chunk(body_prefix)?;
                        }
                    }

                    let write_len = if let Some(expected_total) = expected_total {
                        read.min(expected_total.saturating_sub(writer.total_bytes))
                    } else {
                        read
                    };
                    if write_len == 0 {
                        break;
                    }
                    writer.write_chunk(&read_buffer[..write_len])?;
                }

                if let Some(body_prefix) = deferred_body_prefix.take() {
                    if !body_prefix.is_empty() {
                        writer.write_chunk(body_prefix)?;
                    }
                }

                writer.finish()?;
                let _ = session.close().await;
                return Ok(writer.total_bytes);
            }
            301 | 302 | 303 | 307 | 308 => {
                let Some(location) = header.location else {
                    return Err(String::from("firmware_http_redirect_missing_location"));
                };
                let Some(next_url) = parse_download_url(location.as_str()) else {
                    return Err(String::from("firmware_http_redirect_invalid_location"));
                };
                if next_url.scheme != DownloadUrlScheme::Https {
                    return Err(String::from("firmware_http_redirect_bad_scheme"));
                }
                current_url = next_url;
            }
            404 => return Err(String::from("firmware_http_not_found")),
            429 => return Err(String::from("firmware_http_rate_limited")),
            _ => return Err(String::from("firmware_http_bad_status")),
        }
    }

    Err(String::from("firmware_http_redirect_limit"))
}


async fn resolve_download_host(stack: NetStack<'static>, host: &str) -> Result<Ipv4Address, String> {
    if let Some(ipv4) = parse_ipv4_address(host) {
        return Ok(ipv4);
    }

    let addresses = stack
        .dns_query(host, DnsQueryType::A)
        .await
        .map_err(|_| String::from("firmware_dns_lookup_failed"))?;
    addresses
        .iter()
        .find_map(|address| match address {
            IpAddress::Ipv4(ipv4) => Some(*ipv4),
        })
        .ok_or_else(|| String::from("firmware_dns_no_ipv4_result"))
}

async fn read_http_response_header_socket(
    socket: &mut TcpSocket<'_>,
    buffer: &mut [u8; FIRMWARE_HTTP_HEADER_BUFFER_SIZE],
) -> Result<HttpResponseHeader, String> {
    let mut filled = 0;
    let mut header_end = None;

    while filled < buffer.len() {
        let read = socket
            .read(&mut buffer[filled..])
            .await
            .map_err(|_| String::from("firmware_http_read_failed"))?;
        if read == 0 {
            break;
        }
        filled += read;

        if filled >= 4 {
            for index in 0..=filled - 4 {
                if &buffer[index..index + 4] == b"\r\n\r\n" {
                    header_end = Some(index + 4);
                    break;
                }
            }
        }

        if header_end.is_some() {
            break;
        }
    }

    parse_http_response_header(buffer, filled, header_end)
}

#[cfg(feature = "https-ota")]
async fn read_http_response_header_io<T>(
    socket: &mut T,
    buffer: &mut [u8; FIRMWARE_HTTP_HEADER_BUFFER_SIZE],
) -> Result<HttpResponseHeader, String>
where
    T: TlsRead,
{
    let mut filled = 0;
    let mut header_end = None;

    while filled < buffer.len() {
        let read = socket
            .read(&mut buffer[filled..])
            .await
            .map_err(|_| String::from("firmware_http_read_failed"))?;
        if read == 0 {
            break;
        }
        filled += read;

        if filled >= 4 {
            for index in 0..=filled - 4 {
                if &buffer[index..index + 4] == b"\r\n\r\n" {
                    header_end = Some(index + 4);
                    break;
                }
            }
        }

        if header_end.is_some() {
            break;
        }
    }

    parse_http_response_header(buffer, filled, header_end)
}

fn parse_http_response_header(
    buffer: &[u8; FIRMWARE_HTTP_HEADER_BUFFER_SIZE],
    bytes_filled: usize,
    header_end: Option<usize>,
) -> Result<HttpResponseHeader, String> {
    let Some(header_len) = header_end else {
        return Err(String::from("firmware_http_header_too_large"));
    };

    let header_text = core::str::from_utf8(&buffer[..header_len])
        .map_err(|_| String::from("firmware_http_header_invalid_utf8"))?;
    let mut lines = header_text.split("\r\n");
    let status_line = lines
        .next()
        .ok_or_else(|| String::from("firmware_http_header_missing_status"))?;
    let status_code = status_line
        .split_whitespace()
        .nth(1)
        .and_then(|value| value.parse::<u16>().ok())
        .ok_or_else(|| String::from("firmware_http_header_invalid_status"))?;

    let mut content_length = None;
    let mut location = None;
    for line in lines {
        if line.is_empty() {
            continue;
        }

        let Some((name, value)) = line.split_once(':') else {
            continue;
        };
        let value = value.trim();
        if name.eq_ignore_ascii_case("content-length") {
            content_length = value.parse::<usize>().ok();
        } else if name.eq_ignore_ascii_case("location") {
            location = Some(String::from(value));
        }
    }

    Ok(HttpResponseHeader {
        status_code,
        content_length,
        location,
        header_len,
        bytes_filled,
    })
}

#[cfg(feature = "https-ota")]
fn format_https_session_error(prefix: &str, err: &SessionError) -> String {
    let mut message = String::from(prefix);
    message.push(':');
    message.push_str(err.to_string().as_str());

    message
}


#[cfg(feature = "https-ota")]
async fn write_all_io<T>(socket: &mut T, mut bytes: &[u8]) -> Result<(), ()>
where
    T: TlsWrite,
{
    while !bytes.is_empty() {
        let written = socket.write(bytes).await.map_err(|_| ())?;
        if written == 0 {
            return Err(());
        }
        bytes = &bytes[written..];
    }
    socket.flush().await.map_err(|_| ())
}

fn build_station_config(state: &FirmwareState) -> StationConfig {
    let auth_method = if state.wifi_password.is_empty() {
        WifiAuthenticationMethod::None
    } else {
        WifiAuthenticationMethod::Wpa2Personal
    };

    StationConfig::default()
        .with_ssid(state.wifi_ssid.as_str())
        .with_password(state.wifi_password.clone())
        .with_auth_method(auth_method)
}

fn format_wifi_controller_error(stage: &str, err: &radio_wifi::WifiError) -> String {
    let mut text = String::from(stage);
    text.push_str("_err:");
    text.push_str(match err {
        radio_wifi::WifiError::Disconnected(_) => "disconnected",
        radio_wifi::WifiError::Unsupported => "unsupported",
        radio_wifi::WifiError::InvalidArguments => "invalid_arguments",
        radio_wifi::WifiError::Failed => "failed",
        radio_wifi::WifiError::OutOfMemory => "out_of_memory",
        radio_wifi::WifiError::InvalidSsid => "invalid_ssid",
        radio_wifi::WifiError::InvalidPassword => "invalid_password",
        radio_wifi::WifiError::NotConnected => "not_connected",
        _ => "unknown",
    });
    text
}

fn format_wifi_disconnect_info(info: &radio_wifi::DisconnectedStationInfo) -> String {
    let mut text = String::from("disconnected:");
    text.push_str(match info.reason {
        radio_wifi::DisconnectReason::Unspecified => "unspecified",
        radio_wifi::DisconnectReason::AuthenticationExpired => "authentication_expired",
        radio_wifi::DisconnectReason::AuthenticationLeave => "authentication_leave",
        radio_wifi::DisconnectReason::DisassociatedDueToInactivity => "inactive",
        radio_wifi::DisconnectReason::AssociationTooMany => "association_too_many",
        radio_wifi::DisconnectReason::Class2FrameFromNonAuthenticatedStation => "class2_non_authenticated",
        radio_wifi::DisconnectReason::Class3FrameFromNonAssociatedStation => "class3_non_associated",
        radio_wifi::DisconnectReason::AssociationLeave => "association_leave",
        radio_wifi::DisconnectReason::AssociationNotAuthenticated => "association_not_authenticated",
        radio_wifi::DisconnectReason::DisassociatedPowerCapabilityBad => "power_capability_bad",
        radio_wifi::DisconnectReason::DisassociatedUnsupportedChannel => "unsupported_channel",
        radio_wifi::DisconnectReason::BssTransitionDisassociated => "bss_transition",
        radio_wifi::DisconnectReason::IeInvalid => "ie_invalid",
        radio_wifi::DisconnectReason::MicFailure => "mic_failure",
        radio_wifi::DisconnectReason::FourWayHandshakeTimeout => "four_way_handshake_timeout",
        radio_wifi::DisconnectReason::GroupKeyUpdateTimeout => "group_key_update_timeout",
        radio_wifi::DisconnectReason::IeIn4wayDiffers => "ie_4way_differs",
        radio_wifi::DisconnectReason::GroupCipherInvalid => "group_cipher_invalid",
        radio_wifi::DisconnectReason::PairwiseCipherInvalid => "pairwise_cipher_invalid",
        radio_wifi::DisconnectReason::AkmpInvalid => "akmp_invalid",
        radio_wifi::DisconnectReason::UnsupportedRsnIeVersion => "unsupported_rsn_ie_version",
        radio_wifi::DisconnectReason::InvalidRsnIeCapabilities => "invalid_rsn_ie_capabilities",
        radio_wifi::DisconnectReason::_802_1xAuthenticationFailed => "8021x_authentication_failed",
        radio_wifi::DisconnectReason::CipherSuiteRejected => "cipher_suite_rejected",
        radio_wifi::DisconnectReason::TdlsPeerUnreachable => "tdls_peer_unreachable",
        radio_wifi::DisconnectReason::TdlsUnspecified => "tdls_unspecified",
        radio_wifi::DisconnectReason::SspRequestedDisassociation => "ssp_requested_disassociation",
        radio_wifi::DisconnectReason::NoSspRoamingAgreement => "no_ssp_roaming_agreement",
        radio_wifi::DisconnectReason::BadCipherOrAkm => "bad_cipher_or_akm",
        radio_wifi::DisconnectReason::NotAuthorizedThisLocation => "not_authorized_this_location",
        radio_wifi::DisconnectReason::ServiceChangePercludesTs => "service_change_precludes_ts",
        radio_wifi::DisconnectReason::UnspecifiedQos => "unspecified_qos",
        radio_wifi::DisconnectReason::NotEnoughBandwidth => "not_enough_bandwidth",
        radio_wifi::DisconnectReason::MissingAcks => "missing_acks",
        radio_wifi::DisconnectReason::ExceededTxOp => "exceeded_txop",
        radio_wifi::DisconnectReason::StationLeaving => "station_leaving",
        radio_wifi::DisconnectReason::EndBlockAck => "end_block_ack",
        radio_wifi::DisconnectReason::UnknownBlockAck => "unknown_block_ack",
        radio_wifi::DisconnectReason::Timeout => "timeout",
        radio_wifi::DisconnectReason::PeerInitiated => "peer_initiated",
        radio_wifi::DisconnectReason::AccessPointInitiatedDisassociation => "ap_initiated_disassociation",
        radio_wifi::DisconnectReason::InvalidFtActionFrameCount => "invalid_ft_action_frame_count",
        radio_wifi::DisconnectReason::InvalidPmkid => "invalid_pmkid",
        radio_wifi::DisconnectReason::InvalidMde => "invalid_mde",
        radio_wifi::DisconnectReason::InvalidFte => "invalid_fte",
        radio_wifi::DisconnectReason::TransmissionLinkEstablishmentFailed => "tle_failed",
        radio_wifi::DisconnectReason::AlterativeChannelOccupied => "alternative_channel_occupied",
        radio_wifi::DisconnectReason::BeaconTimeout => "beacon_timeout",
        radio_wifi::DisconnectReason::NoAccessPointFound => "no_access_point_found",
        radio_wifi::DisconnectReason::AuthenticationFailed => "authentication_failed",
        radio_wifi::DisconnectReason::AssociationFailed => "association_failed",
        radio_wifi::DisconnectReason::HandshakeTimeout => "handshake_timeout",
        radio_wifi::DisconnectReason::ConnectionFailed => "connection_failed",
        radio_wifi::DisconnectReason::AccessPointTsfReset => "ap_tsf_reset",
        radio_wifi::DisconnectReason::Roaming => "roaming",
        radio_wifi::DisconnectReason::AssociationComebackTimeTooLong => "association_comeback_time_too_long",
        radio_wifi::DisconnectReason::SaQueryTimeout => "sa_query_timeout",
        radio_wifi::DisconnectReason::NoAccessPointFoundWithCompatibleSecurity => "no_ap_with_compatible_security",
        radio_wifi::DisconnectReason::NoAccessPointFoundInAuthmodeThreshold => "no_ap_in_authmode_threshold",
        radio_wifi::DisconnectReason::NoAccessPointFoundInRssiThreshold => "no_ap_in_rssi_threshold",
        _ => "unknown",
    });
    text
}

fn flush_radar_buffer_if_needed(state: &mut FirmwareState, uptime_ms: u32, force: bool) {
    let should_flush = !state.radar_buffer.is_empty()
        && (force
            || uptime_ms.saturating_sub(state.last_radar_byte_ms) >= RADAR_IDLE_FRAME_GAP_MS
            || state.radar_buffer.len() >= RADAR_FRAME_BUFFER_SIZE);

    if !should_flush {
        return;
    }

    let bytes = state.radar_buffer.as_slice().to_vec();
    state.radar_buffer.clear();

    if let Ok(frame) = parse_radar_frame(&bytes) {
        state.apply_radar_frame(frame);
        state.sync_radar_detection_state(uptime_ms);
    }
}

async fn process_command_buffer(
    command_buffer: &mut heapless::Vec<u8, 256>,
    state: &mut FirmwareState,
    boot_started: &Instant,
    usb_serial: &mut UsbSerialJtag<'_, esp_hal::Blocking>,
    radar_uart: &mut Uart<'_, Blocking>,
    settings: Option<&mut SettingsNvs>,
    wifi_controller: &mut Option<WifiController<'static>>,
) {
    if command_buffer.is_empty() {
        return;
    }

    let response = match core::str::from_utf8(command_buffer.as_slice()) {
        Ok(command) => {
            handle_command(
                command.trim(),
                state,
                boot_started,
                usb_serial,
                radar_uart,
                settings,
                wifi_controller,
            )
            .await
        }
        Err(_) => String::from(r#"{"error":"invalid_utf8"}"#),
    };

    command_buffer.clear();
    write_line(usb_serial, &response);
}

async fn handle_command(
    command: &str,
    state: &mut FirmwareState,
    boot_started: &Instant,
    usb_serial: &mut UsbSerialJtag<'_, esp_hal::Blocking>,
    radar_uart: &mut Uart<'_, Blocking>,
    mut settings: Option<&mut SettingsNvs>,
    wifi_controller: &mut Option<WifiController<'static>>,
) -> String {
    if command.is_empty() || command == "snapshot" {
        return state.snapshot_json(uptime_ms(boot_started));
    }

    if command == "debug_status" {
        return state.debug_status_json(uptime_ms(boot_started));
    }

    match parse_device_command(command) {
        Ok(DeviceCommand::Ping)
        | Ok(DeviceCommand::Status)
        | Ok(DeviceCommand::HomeAssistantStatus)
        => state.snapshot_json(uptime_ms(boot_started)),
        Ok(DeviceCommand::WifiScan) => build_wifi_scan_results_json(wifi_controller).await,
        Ok(home_assistant_command @ DeviceCommand::HomeAssistantConfig(_)) => {
            match home_assistant_command.home_assistant_config_payload() {
                Ok(payload) => {
                    state.apply_home_assistant_config(payload);
                    persist_runtime_config(settings.as_deref_mut(), state);
                    state.snapshot_json(uptime_ms(boot_started))
                }
                Err(_) => String::from(r#"{"error":"invalid_home_assistant_config"}"#),
            }
        }
        Ok(room_command @ DeviceCommand::HomeAssistantRoomConfig(_)) => {
            match room_command.room_config_payload() {
                Ok(payload) => {
                    state.apply_room_config(payload);
                    persist_runtime_config(settings.as_deref_mut(), state);
                    state.snapshot_json(uptime_ms(boot_started))
                }
                Err(_) => String::from(r#"{"error":"invalid_room_config"}"#),
            }
        }
        Ok(websocket_command @ DeviceCommand::HomeAssistantWebSocketConfig(_)) => {
            match websocket_command.home_assistant_websocket_config_payload() {
                Ok(payload) => {
                    state.apply_home_assistant_websocket_config(payload);
                    persist_runtime_config(settings.as_deref_mut(), state);
                    state.snapshot_json(uptime_ms(boot_started))
                }
                Err(_) => String::from(r#"{"error":"invalid_home_assistant_websocket_config"}"#),
            }
        }
        Ok(mqtt_endpoint_command @ DeviceCommand::HomeAssistantMqttEndpoint(_)) => {
            match mqtt_endpoint_command.home_assistant_mqtt_endpoint_payload() {
                Ok(payload) => {
                    state.apply_home_assistant_mqtt_endpoint(payload);
                    persist_runtime_config(settings.as_deref_mut(), state);
                    state.snapshot_json(uptime_ms(boot_started))
                }
                Err(_) => String::from(r#"{"error":"invalid_home_assistant_mqtt_endpoint"}"#),
            }
        }
        Ok(tuning_command @ DeviceCommand::TuningConfig(_)) => {
            match tuning_command.tuning_config_payload() {
                Ok(payload) => {
                    state.apply_tuning_config(payload);
                    persist_runtime_config(settings.as_deref_mut(), state);
                    state.snapshot_json(uptime_ms(boot_started))
                }
                Err(_) => String::from(r#"{"error":"invalid_tuning_config"}"#),
            }
        }
        Ok(ble_tag_command @ DeviceCommand::BleTagConfig(_)) => {
            match ble_tag_command.ble_tag_config_payload() {
                Ok(payload) => {
                    if state.apply_ble_tag_config(payload) {
                        persist_runtime_config(settings.as_deref_mut(), state);
                        state.snapshot_json(uptime_ms(boot_started))
                    } else {
                        String::from(r#"{"error":"invalid_ble_tag_slot"}"#)
                    }
                }
                Err(_) => String::from(r#"{"error":"invalid_ble_tag_config"}"#),
            }
        }
        Ok(ble_tag_clear_command @ DeviceCommand::BleTagClear(_)) => {
            match ble_tag_clear_command.ble_tag_clear_payload() {
                Ok(payload) => {
                    if state.apply_ble_tag_clear(payload) {
                        persist_runtime_config(settings.as_deref_mut(), state);
                        state.snapshot_json(uptime_ms(boot_started))
                    } else {
                        String::from(r#"{"error":"invalid_ble_tag_slot"}"#)
                    }
                }
                Err(_) => String::from(r#"{"error":"invalid_ble_tag_slot"}"#),
            }
        }
        Ok(DeviceCommand::FirmwareSync) => {
            state.request_firmware_sync(uptime_ms(boot_started));
            state.snapshot_json(uptime_ms(boot_started))
        }
        Ok(room_pose_command @ DeviceCommand::HomeAssistantRoomPosePublish(_)) => {
            match room_pose_command.room_pose_publish_payload() {
                Ok(payload) => {
                    if let Some(message) = build_room_pose_publish_message(state, &payload) {
                        let _ = MQTT_COMMAND_CHANNEL.try_send(MqttTaskCommand::Publish(message));
                        state.snapshot_json(uptime_ms(boot_started))
                    } else {
                        String::from(r#"{"error":"invalid_room_pose_publish"}"#)
                    }
                }
                Err(_) => String::from(r#"{"error":"invalid_room_pose_publish"}"#),
            }
        }
        Ok(DeviceCommand::FirmwareUpdate(target_version)) => {
            state.request_firmware_update(target_version, "manual", "manual");
            state.snapshot_json(uptime_ms(boot_started))
        }
        Ok(DeviceCommand::RuntimeBenchmark) => {
            let benchmark = bench::run_device_benchmarks(uptime_ms(boot_started));
            write_benchmark_lines(usb_serial, &benchmark);
            state.runtime_benchmark = Some(benchmark);
            state.snapshot_json(uptime_ms(boot_started))
        }
        Ok(DeviceCommand::Energy) => {
            configure_ld2420_energy_mode(radar_uart);
            state.snapshot_json(uptime_ms(boot_started))
        }
        Ok(DeviceCommand::Radar(payload)) => {
            write_uart_all(radar_uart, payload.as_bytes());
            state.snapshot_json(uptime_ms(boot_started))
        }
        Err(_) => String::from(r#"{"error":"unsupported_command"}"#),
    }
}

async fn build_wifi_scan_results_json(
    wifi_controller: &mut Option<WifiController<'static>>,
) -> String {
    let Some(controller) = wifi_controller.as_mut() else {
        return String::from(r#"{"error":"wifi_scan_unavailable"}"#);
    };

    let scan_config = ScanConfig::default()
        .with_show_hidden(true)
        .with_max(16)
        .with_scan_type(ScanTypeConfig::Active {
            min: esp_hal::time::Duration::from_millis(10),
            max: esp_hal::time::Duration::from_millis(300),
        });

    match controller.scan_async(&scan_config).await {
        Ok(results) => wifi_scan_results_json(&results),
        Err(err) => {
            let mut body = String::from("{\"event\":\"wifi_scan_results\",\"count\":0,\"networks\":[],\"error\":\"");
            body.push_str(format_wifi_error_token(&err));
            body.push_str("\"}");
            body
        }
    }
}

fn wifi_scan_results_json(results: &[AccessPointInfo]) -> String {
    let mut body = String::from("{\"event\":\"wifi_scan_results\",\"count\":");
    append_u32(&mut body, results.len().min(u32::MAX as usize) as u32);
    body.push_str(",\"networks\":[");

    for (index, network) in results.iter().enumerate() {
        if index > 0 {
            body.push(',');
        }

        body.push('{');
        push_json_string_field_string(&mut body, "ssid", network.ssid.as_str());
        body.push_str(",\"bssid\":");
        push_json_string_string(&mut body, &mac_address_string(network.bssid));
        body.push_str(",\"auth_mode\":");
        push_json_string_string(&mut body, wifi_auth_mode_label(network.auth_method));
        body.push_str(",\"rssi\":");
        append_i32(&mut body, i32::from(network.signal_strength));
        body.push_str(",\"channel\":");
        append_u32(&mut body, u32::from(network.channel));
        body.push_str(",\"open\":");
        body.push_str(if matches!(network.auth_method, Some(WifiAuthenticationMethod::None)) {
            "true"
        } else {
            "false"
        });
        body.push('}');
    }

    body.push_str("]}");
    body
}

fn wifi_auth_mode_label(auth_method: Option<WifiAuthenticationMethod>) -> &'static str {
    match auth_method {
        Some(WifiAuthenticationMethod::None) => "OPEN",
        Some(WifiAuthenticationMethod::Wep) => "WEP",
        Some(WifiAuthenticationMethod::Wpa) => "WPA_PSK",
        Some(WifiAuthenticationMethod::Wpa2Personal) => "WPA2_PSK",
        Some(WifiAuthenticationMethod::WpaWpa2Personal) => "WPA_WPA2_PSK",
        Some(WifiAuthenticationMethod::Wpa2Enterprise) => "WPA2_ENTERPRISE",
        Some(WifiAuthenticationMethod::Wpa3Personal) => "WPA3_PSK",
        Some(WifiAuthenticationMethod::Wpa2Wpa3Personal) => "WPA2_WPA3_PSK",
        Some(WifiAuthenticationMethod::WapiPersonal) => "WAPI_PSK",
        _ => "UNKNOWN",
    }
}

fn format_wifi_error_token(err: &radio_wifi::WifiError) -> &'static str {
    match err {
        radio_wifi::WifiError::Disconnected(_) => "disconnected",
        radio_wifi::WifiError::Unsupported => "unsupported",
        radio_wifi::WifiError::InvalidArguments => "invalid_arguments",
        radio_wifi::WifiError::Failed => "failed",
        radio_wifi::WifiError::OutOfMemory => "out_of_memory",
        radio_wifi::WifiError::InvalidSsid => "invalid_ssid",
        radio_wifi::WifiError::InvalidPassword => "invalid_password",
        radio_wifi::WifiError::NotConnected => "not_connected",
        _ => "unknown",
    }
}

fn push_json_string_field_string(payload: &mut String, key: &str, value: &str) {
    payload.push('"');
    payload.push_str(key);
    payload.push_str("\":");
    push_json_string_string(payload, value);
}

fn push_json_string_string(payload: &mut String, value: &str) {
    payload.push('"');
    for ch in value.chars() {
        match ch {
            '\\' => payload.push_str("\\\\"),
            '"' => payload.push_str("\\\""),
            '\n' => payload.push_str("\\n"),
            '\r' => payload.push_str("\\r"),
            '\t' => payload.push_str("\\t"),
            _ => payload.push(ch),
        }
    }
    payload.push('"');
}

fn open_settings_nvs(flash_storage: SettingsStorage) -> Result<SettingsNvs, ()> {
    let mut flash_storage = flash_storage;
    let mut partition_table_bytes = Box::new([0_u8; PARTITION_TABLE_MAX_LEN]);
    let partition_table = read_partition_table(&mut flash_storage, partition_table_bytes.as_mut_slice())
        .map_err(|_| ())?;
    let partition = partition_table
        .find_partition(PartitionType::Data(DataPartitionSubType::Nvs))
        .map_err(|_| ())?
        .ok_or(())?;

    Nvs::new(partition.offset() as usize, partition.len() as usize, flash_storage).map_err(|_| ())
}

fn load_persisted_config(
    state: &mut FirmwareState,
    settings: &mut SettingsNvs,
) {
    if let Ok(value) = settings.get::<bool>(&SETTINGS_NAMESPACE, &SETTINGS_KEY_ENABLED) {
        state.enabled = value;
    }
    if let Ok(value) = settings.get::<String>(&SETTINGS_NAMESPACE, &SETTINGS_KEY_WIFI_SSID) {
        state.wifi_ssid = value;
    }
    if let Ok(value) = settings.get::<String>(&SETTINGS_NAMESPACE, &SETTINGS_KEY_WIFI_PASS) {
        state.wifi_password = value;
    }
    if let Ok(value) = settings.get::<String>(&SETTINGS_NAMESPACE, &SETTINGS_KEY_MQTT_HOST) {
        state.mqtt_host = value;
    }
    if let Ok(value) = settings.get::<u16>(&SETTINGS_NAMESPACE, &SETTINGS_KEY_MQTT_PORT) {
        state.mqtt_port = value;
    }
    if let Ok(value) = settings.get::<String>(&SETTINGS_NAMESPACE, &SETTINGS_KEY_MQTT_USER) {
        state.mqtt_username = value;
    }
    if let Ok(value) = settings.get::<String>(&SETTINGS_NAMESPACE, &SETTINGS_KEY_MQTT_PASS) {
        state.mqtt_password = value;
    }
    if let Ok(value) = settings.get::<bool>(&SETTINGS_NAMESPACE, &SETTINGS_KEY_MQTT_WS) {
        state.mqtt_use_websockets = value;
    }
    if let Ok(value) = settings.get::<String>(&SETTINGS_NAMESPACE, &SETTINGS_KEY_MQTT_WSP) {
        state.mqtt_websocket_path = if value.is_empty() {
            String::from(DEFAULT_MQTT_WEBSOCKET_PATH)
        } else {
            normalize_websocket_path(&value)
        };
    }
    if let Ok(value) = settings.get::<String>(&SETTINGS_NAMESPACE, &SETTINGS_KEY_MQTT_HDR) {
        state.mqtt_host_header = value;
    }
    if let Ok(value) = settings.get::<String>(&SETTINGS_NAMESPACE, &SETTINGS_KEY_NODE_ID) {
        state.node_id = value;
    }
    if let Ok(value) = settings.get::<String>(&SETTINGS_NAMESPACE, &SETTINGS_KEY_FRIENDLY) {
        state.friendly_name = value;
    }
    if let Ok(value) = settings.get::<String>(&SETTINGS_NAMESPACE, &SETTINGS_KEY_ROOM_ID) {
        state.room_id = value;
    }
    if let Ok(value) = settings.get::<String>(&SETTINGS_NAMESPACE, &SETTINGS_KEY_SENSOR_ROLE) {
        state.sensor_role = value;
    }
    if let Ok(value) = settings.get::<i16>(&SETTINGS_NAMESPACE, &SETTINGS_KEY_POSE_X) {
        state.pose_x_cm = i32::from(value);
    }
    if let Ok(value) = settings.get::<i16>(&SETTINGS_NAMESPACE, &SETTINGS_KEY_POSE_Y) {
        state.pose_y_cm = i32::from(value);
    }
    if let Ok(value) = settings.get::<i16>(&SETTINGS_NAMESPACE, &SETTINGS_KEY_POSE_H) {
        state.heading_deg = i32::from(value);
    }
    if let Ok(value) = settings.get::<u16>(&SETTINGS_NAMESPACE, &SETTINGS_KEY_ROOM_W) {
        state.room_width_cm = value;
    }
    if let Ok(value) = settings.get::<u16>(&SETTINGS_NAMESPACE, &SETTINGS_KEY_ROOM_H) {
        state.room_height_cm = value;
    }
    if let Ok(value) = settings.get::<u16>(&SETTINGS_NAMESPACE, &SETTINGS_KEY_MAX_RANGE) {
        state.max_detection_range_cm = value;
    }
    if let Ok(value) = settings.get::<u16>(&SETTINGS_NAMESPACE, &SETTINGS_KEY_MIN_ENERGY) {
        state.min_gate_energy = value;
    }
    if let Ok(value) = settings.get::<u8>(&SETTINGS_NAMESPACE, &SETTINGS_KEY_SENSE_PCT) {
        state.sensitivity_percent = value.clamp(10, 100);
    }
    if let Ok(value) = settings.get::<u16>(&SETTINGS_NAMESPACE, &SETTINGS_KEY_HOLD_MS) {
        state.presence_hold_ms = value.clamp(MIN_PRESENCE_HOLD_MS, MAX_PRESENCE_HOLD_MS);
    }
    if let Ok(value) = settings.get::<u8>(&SETTINGS_NAMESPACE, &SETTINGS_KEY_MIN_GATES) {
        state.min_active_gates = value.clamp(1, 16);
    }
    if let Ok(value) = settings.get::<u8>(&SETTINGS_NAMESPACE, &SETTINGS_KEY_MIN_ACT) {
        state.min_activity_score = value.clamp(1, 100);
    }
    if let Ok(value) = settings.get::<bool>(&SETTINGS_NAMESPACE, &SETTINGS_KEY_LED_ON) {
        state.led_enabled = value;
    }
    if let Ok(value) = settings.get::<u8>(&SETTINGS_NAMESPACE, &SETTINGS_KEY_LED_BRI) {
        state.led_brightness = value.max(1);
    }
    if let Ok(value) = settings.get::<u32>(&SETTINGS_NAMESPACE, &SETTINGS_KEY_BOOT_COUNT) {
        state.boot_count = value;
    }

    for slot in 0..MAX_BLE_TAGS {
        let (label_key, address_key, rssi_key) = ble_tag_key_strings(slot);
        let label_key = Key::from_str(label_key.as_str());
        let address_key = Key::from_str(address_key.as_str());
        let rssi_key = Key::from_str(rssi_key.as_str());

        let label = settings
            .get::<String>(&SETTINGS_NAMESPACE, &label_key)
            .unwrap_or_default();
        let address = settings
            .get::<String>(&SETTINGS_NAMESPACE, &address_key)
            .map(|value| normalize_ble_identity_value(&value))
            .unwrap_or_default();
        let min_rssi = settings
            .get::<i32>(&SETTINGS_NAMESPACE, &rssi_key)
            .unwrap_or(BLE_TAG_DEFAULT_MIN_RSSI)
            .clamp(-120, -20);

        let tag = &mut state.ble_identity_tags[slot];
        tag.label = label;
        tag.address = address;
        tag.min_rssi = min_rssi;
        tag.last_rssi = -127;
        tag.last_seen_ms = 0;
        tag.occupied = !tag.label.is_empty() && !tag.address.is_empty();
        if !tag.occupied {
            clear_ble_identity_tag(tag);
        }
    }

    state.wifi_reconfigure_pending = state.home_assistant_configured();
    if !state.home_assistant_configured() {
        state.mark_wifi_disconnected(DEFAULT_WIFI_NOT_CONFIGURED_REASON_TEXT);
    }
}

#[allow(dead_code)]
fn persist_boot_diagnostics(settings: &mut SettingsNvs, state: &FirmwareState) {
    let _ = settings.set(&SETTINGS_NAMESPACE, &SETTINGS_KEY_BOOT_COUNT, state.boot_count);
}

fn persist_runtime_config(settings: Option<&mut SettingsNvs>, state: &FirmwareState) {
    let Some(settings) = settings else {
        return;
    };

    let _ = settings.set(&SETTINGS_NAMESPACE, &SETTINGS_KEY_ENABLED, state.enabled);
    let _ = settings.set(&SETTINGS_NAMESPACE, &SETTINGS_KEY_WIFI_SSID, state.wifi_ssid.as_str());
    let _ = settings.set(&SETTINGS_NAMESPACE, &SETTINGS_KEY_WIFI_PASS, state.wifi_password.as_str());
    let _ = settings.set(&SETTINGS_NAMESPACE, &SETTINGS_KEY_MQTT_HOST, state.mqtt_host.as_str());
    let _ = settings.set(&SETTINGS_NAMESPACE, &SETTINGS_KEY_MQTT_PORT, state.mqtt_port);
    let _ = settings.set(&SETTINGS_NAMESPACE, &SETTINGS_KEY_MQTT_USER, state.mqtt_username.as_str());
    let _ = settings.set(&SETTINGS_NAMESPACE, &SETTINGS_KEY_MQTT_PASS, state.mqtt_password.as_str());
    let _ = settings.set(&SETTINGS_NAMESPACE, &SETTINGS_KEY_MQTT_WS, state.mqtt_use_websockets);
    let mqtt_websocket_path = normalize_websocket_path(&state.mqtt_websocket_path);
    let _ = settings.set(&SETTINGS_NAMESPACE, &SETTINGS_KEY_MQTT_WSP, mqtt_websocket_path.as_str());
    let _ = settings.set(&SETTINGS_NAMESPACE, &SETTINGS_KEY_MQTT_HDR, state.mqtt_host_header.as_str());
    let _ = settings.set(&SETTINGS_NAMESPACE, &SETTINGS_KEY_NODE_ID, state.node_id.as_str());
    let _ = settings.set(&SETTINGS_NAMESPACE, &SETTINGS_KEY_FRIENDLY, state.friendly_name.as_str());
    let _ = settings.set(&SETTINGS_NAMESPACE, &SETTINGS_KEY_ROOM_ID, state.room_id.as_str());
    let _ = settings.set(&SETTINGS_NAMESPACE, &SETTINGS_KEY_SENSOR_ROLE, state.sensor_role.as_str());
    let _ = settings.set(&SETTINGS_NAMESPACE, &SETTINGS_KEY_POSE_X, state.pose_x_cm as i16);
    let _ = settings.set(&SETTINGS_NAMESPACE, &SETTINGS_KEY_POSE_Y, state.pose_y_cm as i16);
    let _ = settings.set(&SETTINGS_NAMESPACE, &SETTINGS_KEY_POSE_H, state.heading_deg as i16);
    let _ = settings.set(&SETTINGS_NAMESPACE, &SETTINGS_KEY_ROOM_W, state.room_width_cm);
    let _ = settings.set(&SETTINGS_NAMESPACE, &SETTINGS_KEY_ROOM_H, state.room_height_cm);
    let _ = settings.set(&SETTINGS_NAMESPACE, &SETTINGS_KEY_MAX_RANGE, state.max_detection_range_cm);
    let _ = settings.set(&SETTINGS_NAMESPACE, &SETTINGS_KEY_MIN_ENERGY, state.min_gate_energy);
    let _ = settings.set(&SETTINGS_NAMESPACE, &SETTINGS_KEY_SENSE_PCT, state.sensitivity_percent);
    let _ = settings.set(&SETTINGS_NAMESPACE, &SETTINGS_KEY_HOLD_MS, state.presence_hold_ms);
    let _ = settings.set(&SETTINGS_NAMESPACE, &SETTINGS_KEY_MIN_GATES, state.min_active_gates);
    let _ = settings.set(&SETTINGS_NAMESPACE, &SETTINGS_KEY_MIN_ACT, state.min_activity_score);
    let _ = settings.set(&SETTINGS_NAMESPACE, &SETTINGS_KEY_LED_ON, state.led_enabled);
    let _ = settings.set(&SETTINGS_NAMESPACE, &SETTINGS_KEY_LED_BRI, state.led_brightness);

    for (slot, tag) in state.ble_identity_tags.iter().enumerate() {
        let (label_key, address_key, rssi_key) = ble_tag_key_strings(slot);
        let label_key = Key::from_str(label_key.as_str());
        let address_key = Key::from_str(address_key.as_str());
        let rssi_key = Key::from_str(rssi_key.as_str());

        let _ = settings.set(
            &SETTINGS_NAMESPACE,
            &label_key,
            if tag.occupied { tag.label.as_str() } else { "" },
        );
        let _ = settings.set(
            &SETTINGS_NAMESPACE,
            &address_key,
            if tag.occupied { tag.address.as_str() } else { "" },
        );
        let _ = settings.set(
            &SETTINGS_NAMESPACE,
            &rssi_key,
            if tag.occupied { tag.min_rssi } else { BLE_TAG_DEFAULT_MIN_RSSI },
        );
    }
}

fn default_ble_sightings() -> Vec<BleBeaconSightingState> {
    let mut sightings = Vec::with_capacity(MAX_BLE_SIGHTINGS);
    for _ in 0..MAX_BLE_SIGHTINGS {
        sightings.push(BleBeaconSightingState::default());
    }
    sightings
}

fn default_ble_identity_tags() -> Vec<BleIdentityTagState> {
    let mut tags = Vec::with_capacity(MAX_BLE_TAGS);
    for _ in 0..MAX_BLE_TAGS {
        tags.push(BleIdentityTagState {
            occupied: false,
            label: String::new(),
            address: String::new(),
            min_rssi: BLE_TAG_DEFAULT_MIN_RSSI,
            last_rssi: -127,
            last_seen_ms: 0,
        });
    }
    tags
}

#[cfg(feature = "ble-scan")]
fn format_ble_address(raw: [u8; 6]) -> String {
    let mut address = String::new();
    let _ = core::fmt::write(
        &mut address,
        format_args!(
            "{:02x}:{:02x}:{:02x}:{:02x}:{:02x}:{:02x}",
            raw[5], raw[4], raw[3], raw[2], raw[1], raw[0]
        ),
    );
    address
}

fn format_soc_reset_reason(reason: Option<SocResetReason>) -> String {
    String::from(match reason {
        Some(SocResetReason::ChipPowerOn) => "chip_power_on",
        Some(SocResetReason::CoreSw) => "core_sw",
        Some(SocResetReason::CoreDeepSleep) => "core_deep_sleep",
        Some(SocResetReason::CoreMwdt0) => "core_mwdt0",
        Some(SocResetReason::CoreMwdt1) => "core_mwdt1",
        Some(SocResetReason::CoreRtcWdt) => "core_rtc_wdt",
        Some(SocResetReason::CpuMwdt0) => "cpu_mwdt0",
        Some(SocResetReason::CpuSw) => "cpu_sw",
        Some(SocResetReason::CpuRtcWdt) => "cpu_rtc_wdt",
        Some(SocResetReason::SysBrownOut) => "sys_brownout",
        Some(SocResetReason::SysRtcWdt) => "sys_rtc_wdt",
        Some(SocResetReason::CpuMwdt1) => "cpu_mwdt1",
        Some(SocResetReason::SysSuperWdt) => "sys_super_wdt",
        Some(SocResetReason::SysClkGlitch) => "sys_clk_glitch",
        Some(SocResetReason::CoreEfuseCrc) => "core_efuse_crc",
        Some(SocResetReason::CoreUsbUart) => "core_usb_uart",
        Some(SocResetReason::CoreUsbJtag) => "core_usb_jtag",
        Some(SocResetReason::CorePwrGlitch) => "core_pwr_glitch",
        None => "unknown",
    })
}

fn ble_tag_key_strings(slot: usize) -> (String, String, String) {
    let mut label = String::from("btag");
    append_u32(&mut label, slot.min(u32::MAX as usize) as u32);
    label.push_str("_lbl");

    let mut address = String::from("btag");
    append_u32(&mut address, slot.min(u32::MAX as usize) as u32);
    address.push_str("_mac");

    let mut rssi = String::from("btag");
    append_u32(&mut rssi, slot.min(u32::MAX as usize) as u32);
    rssi.push_str("_rssi");

    (label, address, rssi)
}

fn uptime_ms(boot_started: &Instant) -> u32 {
    let millis = boot_started.elapsed().as_micros() / 1_000;
    millis.min(u32::MAX as u64) as u32
}

fn write_benchmark_lines(
    usb_serial: &mut UsbSerialJtag<'_, esp_hal::Blocking>,
    benchmark: &RuntimeBenchmarkSnapshot,
) {
    let mut line = String::new();

    line.push_str("device_bench iterations=");
    append_u32(&mut line, benchmark.iterations);
    write_line(usb_serial, &line);

    write_measurement_line(usb_serial, "parse_command_fixture", &benchmark.parse_command_fixture);
    write_measurement_line(usb_serial, "parse_room_config_fixture", &benchmark.parse_room_config_fixture);
    write_measurement_line(usb_serial, "parse_tuning_config_fixture", &benchmark.parse_tuning_config_fixture);
    write_measurement_line(usb_serial, "parse_generic_fixture", &benchmark.parse_generic_fixture);
    write_measurement_line(usb_serial, "derive_metrics_fixture", &benchmark.derive_metrics_fixture);
    write_measurement_line(
        usb_serial,
        "detection_candidate_fixture",
        &benchmark.detection_candidate_fixture,
    );
}

fn write_measurement_line(
    usb_serial: &mut UsbSerialJtag<'_, esp_hal::Blocking>,
    name: &str,
    measurement: &espwaverider_core::snapshot::RuntimeBenchmarkMeasurementSnapshot,
) {
    let mut line = String::from("device_bench ");
    line.push_str(name);
    line.push_str(" total_us=");
    append_u32(&mut line, measurement.total_us);
    line.push_str(" per_iter_ns=");
    append_u32(&mut line, measurement.per_iter_ns);
    write_line(usb_serial, &line);
}

fn update_status_led(status_led: &mut StatusLed<'_>, color: StatusLedColor) {
    status_led.write(color);
}

fn snapshot_from_energy_frame(
    energy: &EnergyFrame,
    bytes_total: u32,
    frames_total: u32,
    energy_frames_total: u32,
) -> LatestEnergyFrameSnapshot {
    LatestEnergyFrameSnapshot {
        length: energy.length as u16,
        payload_length: energy.payload_length,
        presence: energy.presence,
        distance_cm: energy.distance_cm,
        bytes_total,
        frames_total,
        energy_frames_total,
        gates: energy.gates.into_iter().collect(),
    }
}

fn snapshot_from_text_frame(
    text: &TextFrame,
    bytes_total: u32,
    frames_total: u32,
) -> LatestTextFrameSnapshot {
    LatestTextFrameSnapshot {
        length: text.length as u16,
        presence: text.presence,
        range: text.range_cm.map(i32::from).unwrap_or(-1),
        bytes_total,
        frames_total,
        hex: text.hex.clone(),
        ascii: text.ascii.clone(),
    }
}

fn snapshot_from_generic_frame(
    generic: &GenericFrame,
    bytes_total: u32,
    frames_total: u32,
) -> LatestGenericFrameSnapshot {
    LatestGenericFrameSnapshot {
        length: generic.length as u16,
        bytes_total,
        frames_total,
        hex: generic.hex.clone(),
        ascii: generic.ascii.clone(),
    }
}

fn configure_ld2420_energy_mode(radar_uart: &mut Uart<'_, Blocking>) {
    write_uart_all(radar_uart, LD2420_ENABLE_CONFIG);
    write_uart_all(radar_uart, LD2420_SET_ENERGY_MODE);
    write_uart_all(radar_uart, LD2420_DISABLE_CONFIG);
}

fn write_uart_all(radar_uart: &mut Uart<'_, Blocking>, mut bytes: &[u8]) {
    while !bytes.is_empty() {
        match radar_uart.write(bytes) {
            Ok(written) if written > 0 => bytes = &bytes[written..],
            _ => break,
        }
    }
    let _ = radar_uart.flush();
}

fn append_u32(output: &mut String, value: u32) {
    let mut digits = [0_u8; 10];
    let mut cursor = digits.len();
    let mut remaining = value;

    loop {
        cursor -= 1;
        digits[cursor] = b'0' + (remaining % 10) as u8;
        remaining /= 10;
        if remaining == 0 {
            break;
        }
    }
    for digit in &digits[cursor..] {
        output.push(*digit as char);
    }
}

fn append_i32(output: &mut String, value: i32) {
    if value < 0 {
        output.push('-');
        append_u32(output, value.unsigned_abs());
    } else {
        append_u32(output, value as u32);
    }
}

fn append_hex_byte(output: &mut String, value: u8) {
    const HEX: &[u8; 16] = b"0123456789ABCDEF";

    output.push(HEX[(value >> 4) as usize] as char);
    output.push(HEX[(value & 0x0F) as usize] as char);
}

fn append_hex_byte_lower(output: &mut String, value: u8) {
    const HEX: &[u8; 16] = b"0123456789abcdef";

    output.push(HEX[(value >> 4) as usize] as char);
    output.push(HEX[(value & 0x0F) as usize] as char);
}

fn mac_suffix_string(mac_address: [u8; 6]) -> String {
    let mut suffix = String::with_capacity(6);
    append_hex_byte_lower(&mut suffix, mac_address[3]);
    append_hex_byte_lower(&mut suffix, mac_address[4]);
    append_hex_byte_lower(&mut suffix, mac_address[5]);
    suffix
}

fn default_node_id(mac_address: [u8; 6]) -> String {
    let mut node_id = String::from(DEFAULT_NODE_ID_PREFIX);
    node_id.push('_');
    node_id.push_str(&mac_suffix_string(mac_address));
    node_id
}

fn default_friendly_name(mac_address: [u8; 6]) -> String {
    let mut friendly_name = String::from(DEFAULT_FRIENDLY_NAME_PREFIX);
    friendly_name.push(' ');
    friendly_name.push_str(&mac_suffix_string(mac_address));
    friendly_name
}

fn mac_address_string(mac_address: [u8; 6]) -> String {
    let mut output = String::with_capacity(17);
    for (index, byte) in mac_address.into_iter().enumerate() {
        if index > 0 {
            output.push(':');
        }
        append_hex_byte(&mut output, byte);
    }
    output
}

fn mqtt_endpoint_uses_websockets(mqtt_host: &str) -> bool {
    mqtt_host.starts_with("ws://")
        || mqtt_host.starts_with("wss://")
        || mqtt_host.starts_with("http://")
        || mqtt_host.starts_with("https://")
}

fn normalize_websocket_path(path: &str) -> String {
    if path.is_empty() {
        return String::from(DEFAULT_MQTT_WEBSOCKET_PATH);
    }

    if path.starts_with('/') {
        String::from(path)
    } else {
        let mut normalized = String::from("/");
        normalized.push_str(path);
        normalized
    }
}

fn sanitize_hostname(value: &str) -> String {
    let mut hostname = String::new();

    for character in value.chars() {
        if character.is_ascii_lowercase() || character.is_ascii_digit() {
            hostname.push(character);
            continue;
        }

        if character.is_ascii_uppercase() {
            hostname.push(character.to_ascii_lowercase());
            continue;
        }

        if (character == '-' || character == '_')
            && !hostname.is_empty()
            && !hostname.ends_with('-')
        {
            hostname.push('-');
        }
    }

    while hostname.starts_with('-') {
        hostname.remove(0);
    }
    while hostname.ends_with('-') {
        hostname.pop();
    }

    if hostname.is_empty() {
        hostname = String::from("lb-mmwave");
    }

    if hostname.len() > 31 {
        hostname.truncate(31);
    }

    hostname
}

fn status_led_config() -> TxChannelConfig {
    TxChannelConfig::default()
        .with_clk_divider(1)
        .with_idle_output_level(Level::Low)
        .with_carrier_modulation(false)
        .with_idle_output(true)
}

fn status_led_pulses_for_clock(src_clock_mhz: u32) -> (PulseCode, PulseCode) {
    (
        PulseCode::new(
            Level::High,
            ((STATUS_LED_T0H_NS * src_clock_mhz) / 1000) as u16,
            Level::Low,
            ((STATUS_LED_T0L_NS * src_clock_mhz) / 1000) as u16,
        ),
        PulseCode::new(
            Level::High,
            ((STATUS_LED_T1H_NS * src_clock_mhz) / 1000) as u16,
            Level::Low,
            ((STATUS_LED_T1L_NS * src_clock_mhz) / 1000) as u16,
        ),
    )
}

fn encode_status_led_byte(
    value: u8,
    pulses: &(PulseCode, PulseCode),
    buffer: &mut [PulseCode; STATUS_LED_BUFFER_SIZE],
    cursor: &mut usize,
) {
    for mask in [128, 64, 32, 16, 8, 4, 2, 1] {
        buffer[*cursor] = if (value & mask) == 0 {
            pulses.0
        } else {
            pulses.1
        };
        *cursor += 1;
    }
}

fn write_line(usb_serial: &mut UsbSerialJtag<'_, esp_hal::Blocking>, line: &str) {
    let _ = usb_serial.write(line.as_bytes());
    let _ = usb_serial.write(b"\r\n");
    let _ = usb_serial.flush_tx();
}

#[cfg(feature = "usb-console")]
fn startup_debug_line(state: &FirmwareState) -> String {
    let mut line = String::from("startup ha=");
    line.push_str(if state.home_assistant_configured() { "1" } else { "0" });
    line.push_str(" ssid=");
    line.push_str(if state.wifi_ssid.is_empty() { "0" } else { "1" });
    line.push_str(" mqtt_host=");
    line.push_str(if state.mqtt_host.is_empty() {
        "<empty>"
    } else {
        state.mqtt_host.as_str()
    });
    line.push_str(" room=");
    line.push_str(state.room_id.as_str());
    line
}

#[cfg(feature = "usb-console")]
fn runtime_debug_line(state: &FirmwareState) -> String {
    let mut line = String::from("runtime wifi=");
    line.push_str(if state.wifi_connected { "1" } else { "0" });
    line.push_str(" reason=");
    line.push_str(state.wifi_disconnect_reason_text.as_str());
    line.push_str(" ip=");
    line.push_str(state.ip_address.as_str());
    line.push_str(" mqtt=");
    line.push_str(if state.mqtt_connected { "1" } else { "0" });
    line.push_str(" mqtt_state=");
    append_i32_string(&mut line, state.mqtt_state);
    line
}

#[cfg(feature = "usb-console")]
fn append_i32_string(output: &mut String, value: i32) {
    let _ = core::fmt::Write::write_fmt(output, format_args!("{}", value));
}
