use alloc::string::String;
use core::fmt::Write;

pub const BLE_SIGHTING_FRESHNESS_MS: u32 = 30_000;
pub const BLE_TAG_FRESHNESS_MS: u32 = 45_000;
pub const BLE_TAG_DEFAULT_MIN_RSSI: i32 = -88;

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct BleBeaconSightingState {
    pub occupied: bool,
    pub address: String,
    pub name: String,
    pub service_uuid: String,
    pub rssi: i32,
    pub last_seen_ms: u32,
}

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct BleIdentityTagState {
    pub occupied: bool,
    pub label: String,
    pub address: String,
    pub min_rssi: i32,
    pub last_rssi: i32,
    pub last_seen_ms: u32,
}

pub fn clear_ble_identity_tag(tag: &mut BleIdentityTagState) {
    tag.occupied = false;
    tag.label.clear();
    tag.address.clear();
    tag.min_rssi = BLE_TAG_DEFAULT_MIN_RSSI;
    tag.last_rssi = -127;
    tag.last_seen_ms = 0;
}

pub fn ble_identity_tag_present(tag: &BleIdentityTagState, uptime_ms: u32) -> bool {
    tag.occupied
        && tag.last_seen_ms > 0
        && uptime_ms.saturating_sub(tag.last_seen_ms) <= BLE_TAG_FRESHNESS_MS
}

pub fn ble_beacon_sighting_active(sighting: &BleBeaconSightingState, uptime_ms: u32) -> bool {
    sighting.occupied
        && uptime_ms.saturating_sub(sighting.last_seen_ms) <= BLE_SIGHTING_FRESHNESS_MS
}

pub fn normalize_ble_identity_value(value: &str) -> String {
    let mut normalized = String::new();
    for ch in value.chars() {
        match ch {
            'A'..='F' => normalized.push(((ch as u8) - b'A' + b'a') as char),
            'a'..='f' | '0'..='9' => normalized.push(ch),
            _ => {}
        }
    }
    normalized
}

pub fn record_ble_advertisement(
    sightings: &mut [BleBeaconSightingState],
    tags: &mut [BleIdentityTagState],
    address: &str,
    name: &str,
    service_uuid: &str,
    rssi: i32,
    now_ms: u32,
) {
    if address.is_empty() || sightings.is_empty() {
        return;
    }

    let mut slot = None;
    for (index, sighting) in sightings.iter().enumerate() {
        if sighting.occupied && sighting.address == address {
            slot = Some(index);
            break;
        }

        if !sighting.occupied && slot.is_none() {
            slot = Some(index);
        }
    }

    let slot = slot.unwrap_or_else(|| {
        sightings
            .iter()
            .enumerate()
            .min_by_key(|(_, sighting)| sighting.last_seen_ms)
            .map(|(index, _)| index)
            .unwrap_or(0)
    });

    let sighting = &mut sightings[slot];
    sighting.occupied = true;
    sighting.address.clear();
    sighting.address.push_str(address);
    sighting.name.clear();
    sighting.name.push_str(name);
    sighting.service_uuid.clear();
    sighting.service_uuid.push_str(service_uuid);
    sighting.rssi = rssi;
    sighting.last_seen_ms = now_ms;

    let normalized_address = normalize_ble_identity_value(address);
    for tag in tags.iter_mut() {
        if !tag.occupied || tag.address != normalized_address || rssi < tag.min_rssi {
            continue;
        }

        tag.last_rssi = rssi;
        tag.last_seen_ms = now_ms;
    }
}

pub fn extract_ble_local_name(adv_data: &[u8], scan_data: &[u8]) -> String {
    let mut shortened = None;

    for payload in [scan_data, adv_data] {
        for (kind, data) in iter_ad_structures(payload) {
            match kind {
                0x09 => {
                    if let Ok(name) = core::str::from_utf8(data) {
                        return String::from(name);
                    }
                }
                0x08 if shortened.is_none() => {
                    if let Ok(name) = core::str::from_utf8(data) {
                        shortened = Some(String::from(name));
                    }
                }
                _ => {}
            }
        }
    }

    shortened.unwrap_or_default()
}

pub fn extract_ble_service_uuid(adv_data: &[u8], scan_data: &[u8]) -> String {
    for payload in [adv_data, scan_data] {
        for (kind, data) in iter_ad_structures(payload) {
            match kind {
                0x07 | 0x06 if data.len() >= 16 => return format_uuid128(&data[..16]),
                0x05 | 0x04 if data.len() >= 4 => {
                    let value = u32::from_le_bytes([data[0], data[1], data[2], data[3]]);
                    let mut text = String::new();
                    let _ = write!(&mut text, "{value:08x}");
                    return text;
                }
                0x03 | 0x02 if data.len() >= 2 => {
                    let value = u16::from_le_bytes([data[0], data[1]]);
                    let mut text = String::new();
                    let _ = write!(&mut text, "{value:04x}");
                    return text;
                }
                _ => {}
            }
        }
    }

    String::new()
}

fn iter_ad_structures(mut payload: &[u8]) -> impl Iterator<Item = (u8, &[u8])> {
    core::iter::from_fn(move || {
        while let Some((&len, rest)) = payload.split_first() {
            payload = rest;
            if len == 0 {
                return None;
            }

            let len = len as usize;
            if payload.len() < len {
                payload = &[];
                return None;
            }

            let (field, remaining) = payload.split_at(len);
            payload = remaining;
            let (&kind, data) = field.split_first()?;
            return Some((kind, data));
        }

        None
    })
}

fn format_uuid128(data: &[u8]) -> String {
    let mut text = String::new();
    if data.len() < 16 {
        return text;
    }

    let bytes = [
        data[15], data[14], data[13], data[12], data[11], data[10], data[9], data[8], data[7],
        data[6], data[5], data[4], data[3], data[2], data[1], data[0],
    ];

    let _ = write!(
        &mut text,
        "{:02x}{:02x}{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}",
        bytes[0],
        bytes[1],
        bytes[2],
        bytes[3],
        bytes[4],
        bytes[5],
        bytes[6],
        bytes[7],
        bytes[8],
        bytes[9],
        bytes[10],
        bytes[11],
        bytes[12],
        bytes[13],
        bytes[14],
        bytes[15]
    );
    text
}

#[cfg(test)]
mod tests {
    use super::{
        BLE_TAG_DEFAULT_MIN_RSSI, BleBeaconSightingState, BleIdentityTagState,
        ble_beacon_sighting_active, ble_identity_tag_present, clear_ble_identity_tag,
        extract_ble_local_name, extract_ble_service_uuid, normalize_ble_identity_value,
        record_ble_advertisement,
    };
    use alloc::{string::String, vec};

    #[test]
    fn normalizes_ble_identity_values() {
        assert_eq!(normalize_ble_identity_value("AA:BB-cc 11"), "aabbcc11");
    }

    #[test]
    fn records_ble_sighting_and_updates_matching_tag() {
        let mut sightings = vec![BleBeaconSightingState::default(); 2];
        let mut tags = vec![BleIdentityTagState {
            occupied: true,
            label: String::from("phone"),
            address: String::from("aabbccddeeff"),
            min_rssi: -88,
            last_rssi: -127,
            last_seen_ms: 0,
        }];

        record_ble_advertisement(
            &mut sightings,
            &mut tags,
            "AA:BB:CC:DD:EE:FF",
            "Phone",
            "180d",
            -67,
            1_234,
        );

        assert!(sightings[0].occupied);
        assert_eq!(sightings[0].address, "AA:BB:CC:DD:EE:FF");
        assert_eq!(sightings[0].name, "Phone");
        assert_eq!(sightings[0].service_uuid, "180d");
        assert_eq!(sightings[0].rssi, -67);
        assert!(ble_beacon_sighting_active(&sightings[0], 1_500));
        assert_eq!(tags[0].last_rssi, -67);
        assert_eq!(tags[0].last_seen_ms, 1_234);
        assert!(ble_identity_tag_present(&tags[0], 2_000));
    }

    #[test]
    fn ignores_tag_updates_below_min_rssi() {
        let mut sightings = vec![BleBeaconSightingState::default(); 1];
        let mut tags = vec![BleIdentityTagState {
            occupied: true,
            label: String::from("wallet"),
            address: String::from("aabbccddeeff"),
            min_rssi: -70,
            last_rssi: -127,
            last_seen_ms: 0,
        }];

        record_ble_advertisement(
            &mut sightings,
            &mut tags,
            "aa:bb:cc:dd:ee:ff",
            "Wallet",
            "",
            -90,
            900,
        );

        assert!(sightings[0].occupied);
        assert_eq!(tags[0].last_rssi, -127);
        assert_eq!(tags[0].last_seen_ms, 0);
    }

    #[test]
    fn reuses_oldest_sighting_slot_when_full() {
        let mut sightings = vec![
            BleBeaconSightingState {
                occupied: true,
                address: String::from("first"),
                name: String::new(),
                service_uuid: String::new(),
                rssi: -50,
                last_seen_ms: 100,
            },
            BleBeaconSightingState {
                occupied: true,
                address: String::from("second"),
                name: String::new(),
                service_uuid: String::new(),
                rssi: -60,
                last_seen_ms: 200,
            },
        ];

        record_ble_advertisement(
            &mut sightings,
            &mut [],
            "third",
            "Beacon",
            "180f",
            -42,
            500,
        );

        assert_eq!(sightings[0].address, "third");
        assert_eq!(sightings[0].rssi, -42);
        assert_eq!(sightings[0].last_seen_ms, 500);
        assert_eq!(sightings[1].address, "second");
    }

    #[test]
    fn clears_identity_tag_state() {
        let mut tag = BleIdentityTagState {
            occupied: true,
            label: String::from("tag"),
            address: String::from("abcd"),
            min_rssi: -50,
            last_rssi: -40,
            last_seen_ms: 123,
        };

        clear_ble_identity_tag(&mut tag);

        assert!(!tag.occupied);
        assert!(tag.label.is_empty());
        assert!(tag.address.is_empty());
        assert_eq!(tag.min_rssi, BLE_TAG_DEFAULT_MIN_RSSI);
        assert_eq!(tag.last_rssi, -127);
        assert_eq!(tag.last_seen_ms, 0);
    }

    #[test]
    fn extracts_local_name_prefering_scan_response_complete_name() {
        let adv_data = [2, 0x01, 0x06, 4, 0x08, b'T', b'e', b's'];
        let scan_data = [5, 0x09, b'T', b'e', b's', b't'];

        assert_eq!(extract_ble_local_name(&adv_data, &scan_data), "Test");
    }

    #[test]
    fn extracts_service_uuid_from_16_bit_ad_structure() {
        let adv_data = [3, 0x03, 0x0d, 0x18];

        assert_eq!(extract_ble_service_uuid(&adv_data, &[]), "180d");
    }

    #[test]
    fn extracts_service_uuid_from_128_bit_ad_structure() {
        let adv_data = [
            17, 0x07, 0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00,
            0x0d, 0x18, 0x00, 0x00,
        ];

        assert_eq!(
            extract_ble_service_uuid(&adv_data, &[]),
            "0000180d-0000-1000-8000-00805f9b34fb"
        );
    }
}