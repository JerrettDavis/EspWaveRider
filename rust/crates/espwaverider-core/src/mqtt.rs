fn json_string_field<'a>(payload: &'a str, field: &str) -> Option<&'a str> {
    let mut pattern = alloc::string::String::from("\"");
    pattern.push_str(field);
    pattern.push_str("\"");

    let field_start = payload.find(pattern.as_str())?;
    let mut remainder = payload.get(field_start + pattern.len()..)?.trim_start();
    remainder = remainder.strip_prefix(':')?.trim_start();
    remainder = remainder.strip_prefix('"')?;

    let mut escaped = false;
    for (index, ch) in remainder.char_indices() {
        if escaped {
            escaped = false;
            continue;
        }
        match ch {
            '\\' => escaped = true,
            '"' => return remainder.get(..index),
            _ => {}
        }
    }

    None
}

pub fn looks_like_room_summary_payload(payload: &str) -> bool {
    match json_string_field(payload, "kind") {
        Some("lb_room_summary") => json_string_field(payload, "node_id").is_some(),
        Some(_) => false,
        None => json_string_field(payload, "node_id").is_some(),
    }
}

pub fn mqtt_publish_payload(packet: &[u8]) -> Option<&str> {
    if packet.is_empty() || (packet[0] >> 4) != 0x03 {
        return None;
    }

    let mut header_len = 1_usize;
    let mut multiplier = 1_usize;
    loop {
        let encoded = *packet.get(header_len)?;
        header_len += 1;
        if encoded & 0x80 == 0 {
            break;
        }
        multiplier = multiplier.checked_mul(128)?;
        if multiplier > 128 * 128 * 128 {
            return None;
        }
    }

    let topic_len_bytes = packet.get(header_len..header_len + 2)?;
    let topic_len = u16::from_be_bytes([topic_len_bytes[0], topic_len_bytes[1]]) as usize;
    let mut payload_offset = header_len + 2 + topic_len;
    let qos = (packet[0] >> 1) & 0x03;
    if qos > 0 {
        payload_offset += 2;
    }

    core::str::from_utf8(packet.get(payload_offset..)?).ok()
}

pub fn mqtt_room_summary_payload_from_publish(packet: &[u8]) -> Option<&str> {
    let payload = mqtt_publish_payload(packet)?;
    looks_like_room_summary_payload(payload).then_some(payload)
}

#[cfg(test)]
mod tests {
    use super::{looks_like_room_summary_payload, mqtt_publish_payload, mqtt_room_summary_payload_from_publish};

    #[test]
    fn accepts_cpp_style_room_summary_without_kind() {
        let payload = "{\"node_id\":\"lb_mmwave_presence_test2\",\"room_id\":\"room-default\"}";
        assert!(looks_like_room_summary_payload(payload));
    }

    #[test]
    fn rejects_non_room_summary_payload_with_other_kind() {
        let payload = "{\"kind\":\"lb_udp_discovery\",\"node_id\":\"lb_mmwave_presence_test2\"}";
        assert!(!looks_like_room_summary_payload(payload));
    }

    #[test]
    fn extracts_room_summary_payload_from_qos0_publish_packet() {
        let payload = "{\"node_id\":\"lb_mmwave_presence_test2\",\"room_id\":\"room-default\"}";
        let packet = mqtt_publish_packet("lb_mmwave/rooms/room-default/nodes/test/summary", payload, None, 0x30);

        assert_eq!(mqtt_publish_payload(packet.as_slice()), Some(payload));
        assert_eq!(mqtt_room_summary_payload_from_publish(packet.as_slice()), Some(payload));
    }

    #[test]
    fn extracts_room_summary_payload_from_qos1_publish_packet() {
        let payload = "{\"node_id\":\"lb_mmwave_presence_test2\",\"room_id\":\"room-default\"}";
        let packet = mqtt_publish_packet(
            "lb_mmwave/rooms/room-default/nodes/test/summary",
            payload,
            Some(1),
            0x32,
        );

        assert_eq!(mqtt_publish_payload(packet.as_slice()), Some(payload));
        assert_eq!(mqtt_room_summary_payload_from_publish(packet.as_slice()), Some(payload));
    }

    fn mqtt_publish_packet(topic: &str, payload: &str, packet_id: Option<u16>, header: u8) -> alloc::vec::Vec<u8> {
        let mut variable_and_payload = alloc::vec::Vec::new();
        variable_and_payload.extend_from_slice(&(topic.len() as u16).to_be_bytes());
        variable_and_payload.extend_from_slice(topic.as_bytes());
        if let Some(packet_id) = packet_id {
            variable_and_payload.extend_from_slice(&packet_id.to_be_bytes());
        }
        variable_and_payload.extend_from_slice(payload.as_bytes());

        let mut packet = alloc::vec![header];
        encode_remaining_length(&mut packet, variable_and_payload.len());
        packet.extend_from_slice(&variable_and_payload);
        packet
    }

    fn encode_remaining_length(output: &mut alloc::vec::Vec<u8>, mut value: usize) {
        loop {
            let mut encoded = (value % 128) as u8;
            value /= 128;
            if value > 0 {
                encoded |= 0x80;
            }
            output.push(encoded);
            if value == 0 {
                break;
            }
        }
    }
}