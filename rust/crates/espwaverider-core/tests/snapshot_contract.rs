use espwaverider_core::snapshot::DeviceSnapshot;

#[test]
fn parses_live_snapshot_fixture() {
    let json = include_str!("fixtures/live_snapshot_node2.json");
    let snapshot = DeviceSnapshot::from_json(json).unwrap();

    assert_eq!(snapshot.node_id, "lb_mmwave_presence_test2");
    assert_eq!(snapshot.build_target, "esp32-s3-devkitm-1");
    assert_eq!(snapshot.udp_discovery.peer_count, 1);
    assert_eq!(snapshot.udp_discovery.peers[0].ip_address, "10.0.107.148");
    assert_eq!(snapshot.ble_beacon_count, 16);
    assert_eq!(snapshot.latest_energy_frame.as_ref().unwrap().gates.len(), 16);
    assert!(snapshot.latest_text_frame.is_none());
    assert!(snapshot.firmware_sync.highest_peer_version.starts_with("1."));
}

#[test]
fn command_and_snapshot_contract_share_the_same_shape() {
    let json = include_str!("fixtures/live_snapshot_node2.json");
    let snapshot = DeviceSnapshot::from_json(json).unwrap();

    assert!(snapshot.configured);
    assert!(snapshot.wifi_connected);
    assert!(snapshot.mqtt_connected);
}

#[test]
fn parses_runtime_benchmark_snapshot_fixture() {
    let json = include_str!("fixtures/live_snapshot_node1_runtime_benchmark.json");
    let snapshot = DeviceSnapshot::from_json(json).unwrap();
    let runtime_benchmark = snapshot.runtime_benchmark.as_ref().unwrap();

    assert_eq!(snapshot.node_id, "lb_mmwave_presence_test1");
    assert_eq!(snapshot.dominant_gate_index, -1);
    assert_eq!(snapshot.dominant_gate_distance_cm, -1);
    assert_eq!(runtime_benchmark.iterations, 1000);
    assert_eq!(runtime_benchmark.parse_command_fixture.per_iter_ns, 4200);
    assert_eq!(runtime_benchmark.parse_room_config_fixture.per_iter_ns, 22100);
    assert_eq!(runtime_benchmark.parse_tuning_config_fixture.per_iter_ns, 20500);
    assert_eq!(runtime_benchmark.parse_generic_fixture.per_iter_ns, 924_847);
    assert_eq!(runtime_benchmark.detection_candidate_fixture.per_iter_ns, 632);
}