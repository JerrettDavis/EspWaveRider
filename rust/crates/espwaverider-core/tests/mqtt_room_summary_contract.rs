use espwaverider_core::mqtt::{looks_like_room_summary_payload, mqtt_room_summary_payload_from_publish};

#[test]
fn accepts_cpp_room_summary_payload_shape_without_kind() {
    let payload = concat!(
        "{",
        "\"node_id\":\"lb_mmwave_presence_test2\",",
        "\"room_id\":\"room-default\",",
        "\"sensor_role\":\"auto\",",
        "\"firmware_version\":\"1.0.1\",",
        "\"pose_x_cm\":0,",
        "\"pose_y_cm\":0,",
        "\"heading_deg\":0,",
        "\"room_width_cm\":600,",
        "\"room_height_cm\":400,",
        "\"presence\":false,",
        "\"detection_candidate\":false,",
        "\"people_estimate\":0,",
        "\"active_gate_count\":0,",
        "\"dominant_gate_distance_cm\":-1,",
        "\"activity_score\":0,",
        "\"updated_ms\":12345",
        "}"
    );

    assert!(looks_like_room_summary_payload(payload));
}

#[test]
fn extracts_cpp_room_summary_from_publish_packet() {
    let payload = "{\"node_id\":\"lb_mmwave_presence_test2\",\"room_id\":\"room-default\"}";
    let packet = mqtt_publish_packet(
        "lb_mmwave/rooms/room-default/nodes/lb_mmwave_presence_test2/summary",
        payload,
        None,
        0x31,
    );

    assert_eq!(mqtt_room_summary_payload_from_publish(packet.as_slice()), Some(payload));
}

fn mqtt_publish_packet(topic: &str, payload: &str, packet_id: Option<u16>, header: u8) -> Vec<u8> {
    let mut variable_and_payload = Vec::new();
    variable_and_payload.extend_from_slice(&(topic.len() as u16).to_be_bytes());
    variable_and_payload.extend_from_slice(topic.as_bytes());
    if let Some(packet_id) = packet_id {
        variable_and_payload.extend_from_slice(&packet_id.to_be_bytes());
    }
    variable_and_payload.extend_from_slice(payload.as_bytes());

    let mut packet = vec![header];
    encode_remaining_length(&mut packet, variable_and_payload.len());
    packet.extend_from_slice(&variable_and_payload);
    packet
}

fn encode_remaining_length(output: &mut Vec<u8>, mut value: usize) {
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