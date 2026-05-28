use alloc::vec::Vec;

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum DeviceCommand<'a> {
    Ping,
    Status,
    HomeAssistantStatus,
    WifiScan,
    RuntimeBenchmark,
    HomeAssistantConfig(&'a str),
    HomeAssistantRoomConfig(&'a str),
    HomeAssistantRoomPosePublish(&'a str),
    TuningConfig(&'a str),
    BleTagConfig(&'a str),
    BleTagClear(&'a str),
    HomeAssistantWebSocketConfig(&'a str),
    HomeAssistantMqttEndpoint(&'a str),
    FirmwareSync,
    FirmwareUpdate(&'a str),
    Energy,
    Radar(&'a str),
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct RoomConfigPayload<'a> {
    pub room_id: &'a str,
    pub sensor_role: &'a str,
    pub pose_x_cm: i16,
    pub pose_y_cm: i16,
    pub heading_deg: i16,
    pub room_width_cm: u16,
    pub room_height_cm: u16,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct RoomPosePublishPayload<'a> {
    pub node_id: &'a str,
    pub room_id: &'a str,
    pub sensor_role: &'a str,
    pub pose_x_cm: i16,
    pub pose_y_cm: i16,
    pub heading_deg: i16,
    pub room_width_cm: u16,
    pub room_height_cm: u16,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct BleTagConfigPayload<'a> {
    pub slot: u8,
    pub label: &'a str,
    pub address: &'a str,
    pub min_rssi: i32,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct BleTagClearPayload {
    pub slot: u8,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct TuningConfigPayload {
    pub max_detection_range_cm: u16,
    pub min_gate_energy: u16,
    pub sensitivity_percent: u8,
    pub presence_hold_ms: u16,
    pub min_active_gates: u8,
    pub min_activity_score: u8,
    pub led_enabled: bool,
    pub led_brightness: u8,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct HomeAssistantConfigPayload<'a> {
    pub wifi_ssid: &'a str,
    pub wifi_password: &'a str,
    pub mqtt_host: &'a str,
    pub mqtt_port: u16,
    pub mqtt_username: &'a str,
    pub mqtt_password: &'a str,
    pub node_id: &'a str,
    pub friendly_name: &'a str,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct HomeAssistantWebSocketConfigPayload<'a> {
    pub enabled: bool,
    pub path: &'a str,
    pub host_header: &'a str,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct HomeAssistantMqttEndpointPayload<'a> {
    pub mqtt_host: &'a str,
    pub mqtt_port: Option<u16>,
    pub use_websockets: Option<bool>,
    pub websocket_path: &'a str,
    pub host_header: &'a str,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct CommandParseError;

impl core::fmt::Display for CommandParseError {
    fn fmt(&self, formatter: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        formatter.write_str("unsupported command")
    }
}

impl core::error::Error for CommandParseError {}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct CommandPayloadParseError {
    message: &'static str,
}

impl CommandPayloadParseError {
    fn new(message: &'static str) -> Self {
        Self { message }
    }
}

impl core::fmt::Display for CommandPayloadParseError {
    fn fmt(&self, formatter: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        formatter.write_str(self.message)
    }
}

impl core::error::Error for CommandPayloadParseError {}

pub fn parse_device_command(input: &str) -> Result<DeviceCommand<'_>, CommandParseError> {
    match input {
        "ping" => Ok(DeviceCommand::Ping),
        "status" => Ok(DeviceCommand::Status),
        "ha_status" => Ok(DeviceCommand::HomeAssistantStatus),
        "wifi_scan" => Ok(DeviceCommand::WifiScan),
        "runtime_benchmark" => Ok(DeviceCommand::RuntimeBenchmark),
        "firmware_sync" => Ok(DeviceCommand::FirmwareSync),
        "energy" => Ok(DeviceCommand::Energy),
        _ => parse_prefixed_command(input),
    }
}

fn parse_prefixed_command(input: &str) -> Result<DeviceCommand<'_>, CommandParseError> {
    if let Some(value) = input.strip_prefix("ha_config:") {
        return Ok(DeviceCommand::HomeAssistantConfig(value));
    }
    if let Some(value) = input.strip_prefix("ha_room_config:") {
        return Ok(DeviceCommand::HomeAssistantRoomConfig(value));
    }
    if let Some(value) = input.strip_prefix("ha_room_pose_publish:") {
        return Ok(DeviceCommand::HomeAssistantRoomPosePublish(value));
    }
    if let Some(value) = input.strip_prefix("tuning_config:") {
        return Ok(DeviceCommand::TuningConfig(value));
    }
    if let Some(value) = input.strip_prefix("ble_tag_config:") {
        return Ok(DeviceCommand::BleTagConfig(value));
    }
    if let Some(value) = input.strip_prefix("ble_tag_clear:") {
        return Ok(DeviceCommand::BleTagClear(value));
    }
    if let Some(value) = input.strip_prefix("ha_ws_config:") {
        return Ok(DeviceCommand::HomeAssistantWebSocketConfig(value));
    }
    if let Some(value) = input.strip_prefix("ha_mqtt_endpoint:") {
        return Ok(DeviceCommand::HomeAssistantMqttEndpoint(value));
    }
    if let Some(value) = input.strip_prefix("firmware_update:") {
        return Ok(DeviceCommand::FirmwareUpdate(value));
    }
    if let Some(value) = input.strip_prefix("radar:") {
        return Ok(DeviceCommand::Radar(value));
    }

    Err(CommandParseError)
}

impl<'a> DeviceCommand<'a> {
    pub fn home_assistant_config_payload(
        &self,
    ) -> Result<HomeAssistantConfigPayload<'a>, CommandPayloadParseError> {
        match self {
            DeviceCommand::HomeAssistantConfig(payload) => parse_home_assistant_config_payload(payload),
            _ => Err(CommandPayloadParseError::new("command is not ha_config")),
        }
    }

    pub fn room_config_payload(&self) -> Result<RoomConfigPayload<'a>, CommandPayloadParseError> {
        match self {
            DeviceCommand::HomeAssistantRoomConfig(payload) => parse_room_config_payload(payload),
            _ => Err(CommandPayloadParseError::new("command is not ha_room_config")),
        }
    }

    pub fn room_pose_publish_payload(
        &self,
    ) -> Result<RoomPosePublishPayload<'a>, CommandPayloadParseError> {
        match self {
            DeviceCommand::HomeAssistantRoomPosePublish(payload) => {
                parse_room_pose_publish_payload(payload)
            }
            _ => Err(CommandPayloadParseError::new("command is not ha_room_pose_publish")),
        }
    }

    pub fn home_assistant_websocket_config_payload(
        &self,
    ) -> Result<HomeAssistantWebSocketConfigPayload<'a>, CommandPayloadParseError> {
        match self {
            DeviceCommand::HomeAssistantWebSocketConfig(payload) => {
                parse_home_assistant_websocket_config_payload(payload)
            }
            _ => Err(CommandPayloadParseError::new("command is not ha_ws_config")),
        }
    }

    pub fn home_assistant_mqtt_endpoint_payload(
        &self,
    ) -> Result<HomeAssistantMqttEndpointPayload<'a>, CommandPayloadParseError> {
        match self {
            DeviceCommand::HomeAssistantMqttEndpoint(payload) => {
                parse_home_assistant_mqtt_endpoint_payload(payload)
            }
            _ => Err(CommandPayloadParseError::new("command is not ha_mqtt_endpoint")),
        }
    }

    pub fn tuning_config_payload(&self) -> Result<TuningConfigPayload, CommandPayloadParseError> {
        match self {
            DeviceCommand::TuningConfig(payload) => parse_tuning_config_payload(payload),
            _ => Err(CommandPayloadParseError::new("command is not tuning_config")),
        }
    }

    pub fn ble_tag_config_payload(&self) -> Result<BleTagConfigPayload<'a>, CommandPayloadParseError> {
        match self {
            DeviceCommand::BleTagConfig(payload) => parse_ble_tag_config_payload(payload),
            _ => Err(CommandPayloadParseError::new("command is not ble_tag_config")),
        }
    }

    pub fn ble_tag_clear_payload(&self) -> Result<BleTagClearPayload, CommandPayloadParseError> {
        match self {
            DeviceCommand::BleTagClear(payload) => parse_ble_tag_clear_payload(payload),
            _ => Err(CommandPayloadParseError::new("command is not ble_tag_clear")),
        }
    }

}

pub fn parse_home_assistant_config_payload(
    payload: &str,
) -> Result<HomeAssistantConfigPayload<'_>, CommandPayloadParseError> {
    let fields = split_fields::<8>(payload)?;

    Ok(HomeAssistantConfigPayload {
        wifi_ssid: fields[0],
        wifi_password: fields[1],
        mqtt_host: fields[2],
        mqtt_port: parse_port_or_default(fields[3], 1883)?,
        mqtt_username: fields[4],
        mqtt_password: fields[5],
        node_id: fields[6],
        friendly_name: fields[7],
    })
}

pub fn parse_room_config_payload(
    payload: &str,
) -> Result<RoomConfigPayload<'_>, CommandPayloadParseError> {
    let fields = split_fields::<7>(payload)?;

    Ok(RoomConfigPayload {
        room_id: fields[0],
        sensor_role: fields[1],
        pose_x_cm: parse_i16(fields[2], "invalid room pose x")?,
        pose_y_cm: parse_i16(fields[3], "invalid room pose y")?,
        heading_deg: parse_i16(fields[4], "invalid room heading")?,
        room_width_cm: parse_u16(fields[5], "invalid room width")?,
        room_height_cm: parse_u16(fields[6], "invalid room height")?,
    })
}

pub fn parse_room_pose_publish_payload(
    payload: &str,
) -> Result<RoomPosePublishPayload<'_>, CommandPayloadParseError> {
    let fields = split_fields::<8>(payload)?;

    Ok(RoomPosePublishPayload {
        node_id: fields[0],
        room_id: fields[1],
        sensor_role: fields[2],
        pose_x_cm: parse_i16(fields[3], "invalid room pose x")?,
        pose_y_cm: parse_i16(fields[4], "invalid room pose y")?,
        heading_deg: parse_i16(fields[5], "invalid room heading")?,
        room_width_cm: parse_u16(fields[6], "invalid room width")?,
        room_height_cm: parse_u16(fields[7], "invalid room height")?,
    })
}

pub fn parse_tuning_config_payload(payload: &str) -> Result<TuningConfigPayload, CommandPayloadParseError> {
    let fields = split_fields::<8>(payload)?;

    let parsed_max_range = parse_i32_or_default(fields[0], 0)?;
    let parsed_min_energy = parse_i32_or_default(fields[1], 0)?;
    let parsed_sensitivity = parse_i32_or_default(fields[2], 0)?;
    let parsed_hold_ms = parse_i32_or_default(fields[3], -1)?;
    let parsed_min_gates = parse_i32_or_default(fields[4], 0)?;
    let parsed_min_activity = parse_i32_or_default(fields[5], 0)?;
    let parsed_led_brightness = parse_i32_or_default(fields[7], 0)?;

    Ok(TuningConfigPayload {
        max_detection_range_cm: if parsed_max_range > 0 {
            clamp_i32(parsed_max_range, 1, u16::MAX as i32) as u16
        } else {
            16 * 70
        },
        min_gate_energy: if parsed_min_energy > 0 {
            clamp_i32(parsed_min_energy, 1, u16::MAX as i32) as u16
        } else {
            25
        },
        sensitivity_percent: clamp_i32(if parsed_sensitivity > 0 { parsed_sensitivity } else { 55 }, 10, 100)
            as u8,
        presence_hold_ms: if parsed_hold_ms >= 0 {
            clamp_i32(parsed_hold_ms, 3000, 60000) as u16
        } else {
            4000
        },
        min_active_gates: clamp_i32(if parsed_min_gates > 0 { parsed_min_gates } else { 1 }, 1, 16) as u8,
        min_activity_score: clamp_i32(if parsed_min_activity > 0 { parsed_min_activity } else { 10 }, 1, 100)
            as u8,
        led_enabled: parse_bool_token(fields[6]),
        led_brightness: clamp_i32(if parsed_led_brightness > 0 { parsed_led_brightness } else { 32 }, 1, 255)
            as u8,
    })
}

pub fn parse_home_assistant_websocket_config_payload(
    payload: &str,
) -> Result<HomeAssistantWebSocketConfigPayload<'_>, CommandPayloadParseError> {
    let fields = split_fields::<3>(payload)?;

    Ok(HomeAssistantWebSocketConfigPayload {
        enabled: parse_bool_token(fields[0]),
        path: fields[1],
        host_header: fields[2],
    })
}

pub fn parse_home_assistant_mqtt_endpoint_payload(
    payload: &str,
) -> Result<HomeAssistantMqttEndpointPayload<'_>, CommandPayloadParseError> {
    let fields = split_fields::<5>(payload)?;

    Ok(HomeAssistantMqttEndpointPayload {
        mqtt_host: fields[0],
        mqtt_port: parse_optional_port(fields[1])?,
        use_websockets: parse_optional_bool_token(fields[2]),
        websocket_path: fields[3],
        host_header: fields[4],
    })
}

pub fn parse_ble_tag_config_payload(
    payload: &str,
) -> Result<BleTagConfigPayload<'_>, CommandPayloadParseError> {
    let fields = split_fields::<4>(payload)?;
    let slot = parse_u8(fields[0], "invalid ble tag slot")?;
    let parsed_min_rssi = if fields[3].is_empty() {
        -88
    } else {
        parse_i32(fields[3], "invalid ble tag min rssi")?
    };

    Ok(BleTagConfigPayload {
        slot,
        label: fields[1],
        address: fields[2],
        min_rssi: clamp_i32(parsed_min_rssi, -120, -20),
    })
}

pub fn parse_ble_tag_clear_payload(payload: &str) -> Result<BleTagClearPayload, CommandPayloadParseError> {
    Ok(BleTagClearPayload {
        slot: parse_u8(payload, "invalid ble tag slot")?,
    })
}

fn split_fields<const N: usize>(payload: &str) -> Result<[&str; N], CommandPayloadParseError> {
    let fields: Vec<&str> = payload.split('|').collect();
    if fields.len() != N {
        return Err(CommandPayloadParseError::new("invalid field count"));
    }

    fields
        .try_into()
        .map_err(|_| CommandPayloadParseError::new("invalid field count"))
}

fn parse_bool_token(token: &str) -> bool {
    matches!(token, "1" | "true" | "on")
}

fn parse_i16(value: &str, message: &'static str) -> Result<i16, CommandPayloadParseError> {
    value
        .parse::<i16>()
        .map_err(|_| CommandPayloadParseError::new(message))
}

fn parse_i32(value: &str, message: &'static str) -> Result<i32, CommandPayloadParseError> {
    value
        .parse::<i32>()
        .map_err(|_| CommandPayloadParseError::new(message))
}

fn parse_u8(value: &str, message: &'static str) -> Result<u8, CommandPayloadParseError> {
    value
        .parse::<u8>()
        .map_err(|_| CommandPayloadParseError::new(message))
}

fn parse_u16(value: &str, message: &'static str) -> Result<u16, CommandPayloadParseError> {
    value
        .parse::<u16>()
        .map_err(|_| CommandPayloadParseError::new(message))
}

fn parse_i32_or_default(value: &str, default: i32) -> Result<i32, CommandPayloadParseError> {
    if value.is_empty() {
        return Ok(default);
    }

    value
        .parse::<i32>()
        .map_err(|_| CommandPayloadParseError::new("invalid numeric field"))
}

fn clamp_i32(value: i32, min: i32, max: i32) -> i32 {
    value.clamp(min, max)
}

fn parse_port_or_default(value: &str, default: u16) -> Result<u16, CommandPayloadParseError> {
    Ok(parse_optional_port(value)?.unwrap_or(default))
}

fn parse_optional_port(value: &str) -> Result<Option<u16>, CommandPayloadParseError> {
    if value.is_empty() {
        return Ok(None);
    }

    let parsed = value
        .parse::<u16>()
        .map_err(|_| CommandPayloadParseError::new("invalid port field"))?;

    if parsed == 0 {
        Ok(None)
    } else {
        Ok(Some(parsed))
    }
}

fn parse_optional_bool_token(token: &str) -> Option<bool> {
    if token.is_empty() {
        None
    } else {
        Some(parse_bool_token(token))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_simple_commands() {
        assert_eq!(parse_device_command("ping"), Ok(DeviceCommand::Ping));
        assert_eq!(parse_device_command("runtime_benchmark"), Ok(DeviceCommand::RuntimeBenchmark));
        assert_eq!(parse_device_command("firmware_sync"), Ok(DeviceCommand::FirmwareSync));
    }

    #[test]
    fn parses_prefixed_commands() {
        assert_eq!(
            parse_device_command("firmware_update:v1.0.1"),
            Ok(DeviceCommand::FirmwareUpdate("v1.0.1"))
        );
        assert_eq!(
            parse_device_command("ha_config:ssid|pass|broker|1883|user|secret|node-1|Node 1"),
            Ok(DeviceCommand::HomeAssistantConfig(
                "ssid|pass|broker|1883|user|secret|node-1|Node 1"
            ))
        );
        assert_eq!(
            parse_device_command("ha_room_config:room-default|auto|50|25|-90|800|400"),
            Ok(DeviceCommand::HomeAssistantRoomConfig(
                "room-default|auto|50|25|-90|800|400"
            ))
        );
        assert_eq!(
            parse_device_command(
                "ha_room_pose_publish:node-2|room-default|auto|50|25|-90|800|400"
            ),
            Ok(DeviceCommand::HomeAssistantRoomPosePublish(
                "node-2|room-default|auto|50|25|-90|800|400"
            ))
        );
        assert_eq!(
            parse_device_command("ble_tag_config:2|Badge|AA:BB:CC:DD:EE:FF|-70"),
            Ok(DeviceCommand::BleTagConfig("2|Badge|AA:BB:CC:DD:EE:FF|-70"))
        );
        assert_eq!(
            parse_device_command("ble_tag_clear:2"),
            Ok(DeviceCommand::BleTagClear("2"))
        );
    }

    #[test]
    fn parses_typed_home_assistant_config_payload() {
        let command =
            parse_device_command("ha_config:ssid|pass|broker|0|user|secret|node-1|Node 1").unwrap();

        assert_eq!(
            command.home_assistant_config_payload().unwrap(),
            HomeAssistantConfigPayload {
                wifi_ssid: "ssid",
                wifi_password: "pass",
                mqtt_host: "broker",
                mqtt_port: 1883,
                mqtt_username: "user",
                mqtt_password: "secret",
                node_id: "node-1",
                friendly_name: "Node 1",
            }
        );
    }

    #[test]
    fn parses_typed_websocket_config_payload() {
        let command = parse_device_command("ha_ws_config:on|mqtt|example.com").unwrap();

        assert_eq!(
            command.home_assistant_websocket_config_payload().unwrap(),
            HomeAssistantWebSocketConfigPayload {
                enabled: true,
                path: "mqtt",
                host_header: "example.com",
            }
        );
    }

    #[test]
    fn parses_typed_mqtt_endpoint_payload() {
        let command = parse_device_command("ha_mqtt_endpoint:ws://broker.local/socket|9001||ws|header").unwrap();

        assert_eq!(
            command.home_assistant_mqtt_endpoint_payload().unwrap(),
            HomeAssistantMqttEndpointPayload {
                mqtt_host: "ws://broker.local/socket",
                mqtt_port: Some(9001),
                use_websockets: None,
                websocket_path: "ws",
                host_header: "header",
            }
        );
    }

    #[test]
    fn parses_typed_room_config_payload() {
        let command =
            parse_device_command("ha_room_config:room-default|auto|50|25|-90|800|400").unwrap();

        assert_eq!(
            command.room_config_payload().unwrap(),
            RoomConfigPayload {
                room_id: "room-default",
                sensor_role: "auto",
                pose_x_cm: 50,
                pose_y_cm: 25,
                heading_deg: -90,
                room_width_cm: 800,
                room_height_cm: 400,
            }
        );
    }

    #[test]
    fn parses_typed_room_pose_publish_payload() {
        let command = parse_device_command(
            "ha_room_pose_publish:node-2|room-default|auto|50|25|-90|800|400",
        )
        .unwrap();

        assert_eq!(
            command.room_pose_publish_payload().unwrap(),
            RoomPosePublishPayload {
                node_id: "node-2",
                room_id: "room-default",
                sensor_role: "auto",
                pose_x_cm: 50,
                pose_y_cm: 25,
                heading_deg: -90,
                room_width_cm: 800,
                room_height_cm: 400,
            }
        );
    }

    #[test]
    fn parses_typed_ble_tag_config_payload() {
        let command = parse_device_command("ble_tag_config:2|Badge|AA:BB:CC:DD:EE:FF|-70").unwrap();

        assert_eq!(
            command.ble_tag_config_payload().unwrap(),
            BleTagConfigPayload {
                slot: 2,
                label: "Badge",
                address: "AA:BB:CC:DD:EE:FF",
                min_rssi: -70,
            }
        );
    }

    #[test]
    fn parses_typed_ble_tag_clear_payload() {
        let command = parse_device_command("ble_tag_clear:2").unwrap();

        assert_eq!(
            command.ble_tag_clear_payload().unwrap(),
            BleTagClearPayload { slot: 2 }
        );
    }


    #[test]
    fn applies_ble_tag_default_min_rssi_and_clamps() {
        assert_eq!(
            parse_ble_tag_config_payload("2|Badge|AA:BB:CC:DD:EE:FF|").unwrap(),
            BleTagConfigPayload {
                slot: 2,
                label: "Badge",
                address: "AA:BB:CC:DD:EE:FF",
                min_rssi: -88,
            }
        );

        assert_eq!(
            parse_ble_tag_config_payload("2|Badge|AA:BB:CC:DD:EE:FF|-200").unwrap(),
            BleTagConfigPayload {
                slot: 2,
                label: "Badge",
                address: "AA:BB:CC:DD:EE:FF",
                min_rssi: -120,
            }
        );
    }

    #[test]
    fn applies_cpp_tuning_defaults_and_clamps() {
        let command = parse_device_command("tuning_config:0|0|500|-1|0|0|on|0").unwrap();

        assert_eq!(
            command.tuning_config_payload().unwrap(),
            TuningConfigPayload {
                max_detection_range_cm: 1120,
                min_gate_energy: 25,
                sensitivity_percent: 100,
                presence_hold_ms: 4000,
                min_active_gates: 1,
                min_activity_score: 10,
                led_enabled: true,
                led_brightness: 32,
            }
        );
    }

    #[test]
    fn rejects_bad_room_config_payloads() {
        assert_eq!(
            parse_room_config_payload("room-default|auto|50|oops|-90|800|400"),
            Err(CommandPayloadParseError::new("invalid room pose y"))
        );
    }

    #[test]
    fn rejects_unknown_commands() {
        assert_eq!(parse_device_command("nope"), Err(CommandParseError));
    }
}
