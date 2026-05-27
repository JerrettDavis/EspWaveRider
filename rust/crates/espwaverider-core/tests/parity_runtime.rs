use espwaverider_core::{
    metrics::{build_radar_derived_metrics, radar_detection_candidate, RadarTuning},
    radar::{parse_radar_frame, EnergyFrame, GenericFrame, RadarFrame},
    snapshot::DeviceSnapshot,
};

#[test]
fn energy_frame_fixture_matches_snapshot_metrics_contract() {
    let snapshot = live_snapshot_fixture();
    let frame = energy_frame_from_snapshot(&snapshot);
    let radar_frame = RadarFrame::Energy(frame.clone());
    let metrics = build_radar_derived_metrics(Some(&radar_frame), snapshot.gpio_presence);

    assert_eq!(frame.length, snapshot.latest_energy_frame.as_ref().unwrap().length as usize);
    assert_eq!(metrics.estimated_people, snapshot.people_estimate);
    assert_eq!(metrics.active_gate_count, snapshot.active_gate_count);
    assert_eq!(metrics.activity_score, snapshot.activity_score);
    assert_eq!(metrics.dominant_gate_index, snapshot.dominant_gate_index);
    assert_eq!(metrics.dominant_gate_distance_cm, snapshot.dominant_gate_distance_cm);
    assert_eq!(metrics.dominant_gate_energy, snapshot.dominant_gate_energy);
    assert_eq!(metrics.total_gate_energy, snapshot.total_gate_energy);
}

#[test]
fn generic_frame_fixture_matches_snapshot_ascii_and_hex() {
    let snapshot = live_snapshot_fixture();
    let generic = snapshot.latest_generic_frame.as_ref().unwrap();
    let bytes = decode_hex(&generic.hex);
    let parsed = parse_radar_frame(&bytes).unwrap();

    assert_eq!(
        parsed,
        RadarFrame::Generic(GenericFrame {
            length: generic.length as usize,
            hex: generic.hex.clone(),
            ascii: generic.ascii.clone(),
        })
    );
}

#[test]
fn detection_candidate_matches_live_snapshot_flag() {
    let snapshot = live_snapshot_fixture();
    let frame = RadarFrame::Energy(energy_frame_from_snapshot(&snapshot));
    let metrics = build_radar_derived_metrics(Some(&frame), snapshot.gpio_presence);
    let tuning = RadarTuning {
        max_detection_range_cm: snapshot.max_detection_range_cm,
        min_gate_energy: snapshot.min_gate_energy,
        sensitivity_percent: snapshot.sensitivity_percent,
        min_active_gates: snapshot.min_active_gates,
        min_activity_score: snapshot.min_activity_score,
    };

    assert_eq!(radar_detection_candidate(&metrics, Some(&frame), tuning), snapshot.detection_candidate);
}

fn live_snapshot_fixture() -> DeviceSnapshot {
    DeviceSnapshot::from_json(include_str!("fixtures/live_snapshot_node2.json")).unwrap()
}

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