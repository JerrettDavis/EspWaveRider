use esp_hal::time::Instant;
use espwaverider_core::{
    command::parse_device_command,
    metrics::{build_radar_derived_metrics, radar_detection_candidate, RadarTuning},
    radar::{parse_radar_frame, EnergyFrame, RadarFrame},
    snapshot::{RuntimeBenchmarkMeasurementSnapshot, RuntimeBenchmarkSnapshot},
};

const ITERATIONS: usize = 1_000;
const SIMPLE_COMMAND: &str = "runtime_benchmark";
const ROOM_CONFIG_COMMAND: &str = "ha_room_config:room-default|auto|50|25|-90|800|400";
const TUNING_CONFIG_COMMAND: &str = "tuning_config:0|0|500|-1|0|0|on|0";
const GENERIC_FRAME_HEX: &str = "110012000D000D000D00F8F7F6F5F4F3F2F123000169005257F113DD006801280014003A0014000D000A002000140011000D000D000A00F8F7F6F5F4F3F2F12300016900754AA511C901DD0048006400120049000D00140014001900110014000D000D00F8F7F6F5F4F3F2F12300016900A1414510610152001A001400120019001400110019001400140011000A001400F8F7F6F5F4F3F2F12300016900444B241328015A0028001D0050002D001A002900110014000D00110022001100F8F7F6F5";
const ENERGY_GATES: [u16; 16] = [12898, 3730, 362, 36, 80, 98, 20, 13, 13, 9, 10, 13, 20, 10, 13, 10];

pub fn run_device_benchmarks(measured_at_ms: u32) -> RuntimeBenchmarkSnapshot {
    let generic_bytes = decode_hex(GENERIC_FRAME_HEX);
    let energy_frame = RadarFrame::Energy(EnergyFrame {
        length: 45,
        payload_length: 35,
        presence: false,
        distance_cm: 0,
        gates: ENERGY_GATES,
    });
    let tuning = RadarTuning::default();

    let command_start = Instant::now();
    for _ in 0..ITERATIONS {
        let _ = core::hint::black_box(parse_device_command(SIMPLE_COMMAND).unwrap());
    }
    let command_elapsed = command_start.elapsed();

    let room_config_start = Instant::now();
    for _ in 0..ITERATIONS {
        let command = parse_device_command(ROOM_CONFIG_COMMAND).unwrap();
        let _ = core::hint::black_box(command.room_config_payload().unwrap());
    }
    let room_config_elapsed = room_config_start.elapsed();

    let tuning_config_start = Instant::now();
    for _ in 0..ITERATIONS {
        let command = parse_device_command(TUNING_CONFIG_COMMAND).unwrap();
        let _ = core::hint::black_box(command.tuning_config_payload().unwrap());
    }
    let tuning_config_elapsed = tuning_config_start.elapsed();

    let parse_start = Instant::now();
    for _ in 0..ITERATIONS {
        let _ = core::hint::black_box(parse_radar_frame(generic_bytes.as_slice()).unwrap());
    }
    let parse_elapsed = parse_start.elapsed();

    let metrics_start = Instant::now();
    for _ in 0..ITERATIONS {
        let _ = core::hint::black_box(build_radar_derived_metrics(Some(&energy_frame), false));
    }
    let metrics_elapsed = metrics_start.elapsed();

    let metrics = build_radar_derived_metrics(Some(&energy_frame), false);
    let candidate_start = Instant::now();
    for _ in 0..ITERATIONS {
        let _ = core::hint::black_box(radar_detection_candidate(&metrics, Some(&energy_frame), tuning));
    }
    let candidate_elapsed = candidate_start.elapsed();

    RuntimeBenchmarkSnapshot {
        measured_at_ms,
        iterations: ITERATIONS as u32,
        parse_command_fixture: measurement(command_elapsed),
        parse_room_config_fixture: measurement(room_config_elapsed),
        parse_tuning_config_fixture: measurement(tuning_config_elapsed),
        parse_generic_fixture: measurement(parse_elapsed),
        derive_metrics_fixture: measurement(metrics_elapsed),
        detection_candidate_fixture: measurement(candidate_elapsed),
        detection_candidate: radar_detection_candidate(&metrics, Some(&energy_frame), tuning),
        people_estimate: metrics.estimated_people,
        active_gate_count: metrics.active_gate_count,
        activity_score: metrics.activity_score,
        dominant_gate_distance_cm: metrics.dominant_gate_distance_cm,
    }
}

fn measurement(elapsed: esp_hal::time::Duration) -> RuntimeBenchmarkMeasurementSnapshot {
    let total_us = elapsed.as_micros();
    let per_iter_ns = total_us.saturating_mul(1_000) / ITERATIONS as u64;

    RuntimeBenchmarkMeasurementSnapshot {
        total_us: total_us as u32,
        per_iter_ns: per_iter_ns as u32,
    }
}

fn decode_hex(hex: &str) -> heapless::Vec<u8, 194> {
    let mut bytes = heapless::Vec::new();
    for chunk in hex.as_bytes().chunks_exact(2) {
        let value = core::str::from_utf8(chunk).unwrap();
        bytes.push(u8::from_str_radix(value, 16).unwrap()).unwrap();
    }
    bytes
}