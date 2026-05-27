use criterion::{black_box, criterion_group, criterion_main, Criterion};
use espwaverider_core::{
    metrics::{build_radar_derived_metrics, radar_detection_candidate, RadarTuning},
    mqtt::mqtt_room_summary_payload_from_publish,
    radar::{parse_radar_frame, EnergyFrame, RadarFrame},
    snapshot::DeviceSnapshot,
};

fn criterion_benchmark(criterion: &mut Criterion) {
    let snapshot = DeviceSnapshot::from_json(include_str!("../tests/fixtures/live_snapshot_node2.json")).unwrap();
    let generic_bytes = decode_hex(&snapshot.latest_generic_frame.as_ref().unwrap().hex);
    let energy_frame = RadarFrame::Energy(energy_frame_from_snapshot(&snapshot));
    let room_summary_publish = mqtt_publish_packet(
        "lb_mmwave/rooms/room-default/nodes/lb_mmwave_presence_test2/summary",
        "{\"node_id\":\"lb_mmwave_presence_test2\",\"room_id\":\"room-default\"}",
        None,
        0x31,
    );
    let tuning = RadarTuning {
        max_detection_range_cm: snapshot.max_detection_range_cm,
        min_gate_energy: snapshot.min_gate_energy,
        sensitivity_percent: snapshot.sensitivity_percent,
        min_active_gates: snapshot.min_active_gates,
        min_activity_score: snapshot.min_activity_score,
    };

    let mut group = criterion.benchmark_group("parity-runtime");
    group.bench_function("parse_generic_fixture", |bench| {
        bench.iter(|| black_box(parse_radar_frame(black_box(&generic_bytes)).unwrap()))
    });
    group.bench_function("derive_metrics_from_energy_fixture", |bench| {
        bench.iter(|| black_box(build_radar_derived_metrics(Some(black_box(&energy_frame)), false)))
    });
    group.bench_function("detection_candidate_from_fixture", |bench| {
        let metrics = build_radar_derived_metrics(Some(&energy_frame), false);
        bench.iter(|| {
            black_box(radar_detection_candidate(
                black_box(&metrics),
                Some(black_box(&energy_frame)),
                black_box(tuning),
            ))
        })
    });
    group.bench_function("mqtt_room_summary_publish_parse", |bench| {
        bench.iter(|| {
            black_box(mqtt_room_summary_payload_from_publish(black_box(room_summary_publish.as_slice())).unwrap())
        })
    });
    group.finish();
}

criterion_group!(benches, criterion_benchmark);
criterion_main!(benches);

fn energy_frame_from_snapshot(snapshot: &DeviceSnapshot) -> EnergyFrame {
    let energy = snapshot.latest_energy_frame.as_ref().unwrap();
    let mut gates = [0_u16; 16];
    for (index, gate) in energy.gates.iter().copied().enumerate() {
        gates[index] = gate;
    }

    EnergyFrame {
        length: energy.length as usize,
        payload_length: energy.payload_length,
        presence: energy.presence,
        distance_cm: energy.distance_cm,
        gates,
    }
}

fn decode_hex(hex: &str) -> Vec<u8> {
    hex.as_bytes()
        .chunks_exact(2)
        .map(|chunk| {
            let value = core::str::from_utf8(chunk).unwrap();
            u8::from_str_radix(value, 16).unwrap()
        })
        .collect()
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