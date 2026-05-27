#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PresencePinMode {
    Input,
    InputPulldown,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct BoardProfile {
    pub key: &'static str,
    pub display_name: &'static str,
    pub manufacturer: &'static str,
    pub radar_model: &'static str,
    pub usb_baud: u32,
    pub radar_baud: u32,
    pub radar_serial_port: u8,
    pub radar_rx_pin: i8,
    pub radar_tx_pin: i8,
    pub radar_presence_pin: i8,
    pub radar_presence_pin_mode: PresencePinMode,
    pub status_rgb_led_pin: i8,
    pub status_rgb_led_count: u8,
    pub has_status_rgb_led: bool,
}

pub const ESP32_S3_DEVKITM_1: BoardProfile = BoardProfile {
    key: "esp32-s3-devkitm-1",
    display_name: "ESP32-S3 DevKitM-1",
    manufacturer: "Espressif",
    radar_model: "HLK-LD2420",
    usb_baud: 115_200,
    radar_baud: 115_200,
    radar_serial_port: 1,
    radar_rx_pin: 4,
    radar_tx_pin: 5,
    radar_presence_pin: 6,
    radar_presence_pin_mode: PresencePinMode::InputPulldown,
    status_rgb_led_pin: 48,
    status_rgb_led_count: 1,
    has_status_rgb_led: true,
};

pub const ESP32_GENERIC_UART1: BoardProfile = BoardProfile {
    key: "esp32-generic-uart1",
    display_name: "ESP32 Generic UART1",
    manufacturer: "Espressif",
    radar_model: "HLK-LD2420",
    usb_baud: 115_200,
    radar_baud: 115_200,
    radar_serial_port: 1,
    radar_rx_pin: 16,
    radar_tx_pin: 17,
    radar_presence_pin: 18,
    radar_presence_pin_mode: PresencePinMode::Input,
    status_rgb_led_pin: -1,
    status_rgb_led_count: 0,
    has_status_rgb_led: false,
};

pub const HELTEC_WIFI_LORA_32_V3: BoardProfile = BoardProfile {
    key: "heltec-wifi-lora-32-v3",
    display_name: "Heltec WiFi LoRa 32 V3",
    manufacturer: "Heltec",
    radar_model: "HLK-LD2420",
    usb_baud: 115_200,
    radar_baud: 115_200,
    radar_serial_port: 1,
    radar_rx_pin: 4,
    radar_tx_pin: 5,
    radar_presence_pin: 6,
    radar_presence_pin_mode: PresencePinMode::Input,
    status_rgb_led_pin: -1,
    status_rgb_led_count: 0,
    has_status_rgb_led: false,
};

pub const HELTEC_WIFI_LORA_32_V4: BoardProfile = BoardProfile {
    key: "heltec-wifi-lora-32-v4",
    display_name: "Heltec WiFi LoRa 32 V4",
    manufacturer: "Heltec",
    radar_model: "HLK-LD2420",
    usb_baud: 115_200,
    radar_baud: 115_200,
    radar_serial_port: 1,
    radar_rx_pin: 2,
    radar_tx_pin: 3,
    radar_presence_pin: 4,
    radar_presence_pin_mode: PresencePinMode::Input,
    status_rgb_led_pin: -1,
    status_rgb_led_count: 0,
    has_status_rgb_led: false,
};

pub const KNOWN_BOARD_PROFILES: [BoardProfile; 4] = [
    ESP32_S3_DEVKITM_1,
    ESP32_GENERIC_UART1,
    HELTEC_WIFI_LORA_32_V3,
    HELTEC_WIFI_LORA_32_V4,
];

pub fn board_profile_for_key(key: &str) -> Option<BoardProfile> {
    KNOWN_BOARD_PROFILES
        .iter()
        .copied()
        .find(|profile| profile.key == key)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn resolves_known_board_profile() {
        let profile = board_profile_for_key("esp32-s3-devkitm-1").unwrap();

        assert_eq!(profile, ESP32_S3_DEVKITM_1);
    }

    #[test]
    fn rejects_unknown_board_profile() {
        assert!(board_profile_for_key("not-a-board").is_none());
    }
}
