use crate::radar::{EnergyFrame, RadarFrame, TextFrame};

const LD2420_GATE_COUNT: usize = 16;
const LD2420_GATE_SIZE_CM: i32 = 70;
const LD2420_ACTIVE_GATE_FLOOR: u16 = 25;
const LD2420_MAX_ESTIMATED_PEOPLE: u8 = 4;
const LD2420_NEAR_FIELD_CLUTTER_MAX_GATE_INDEX: i32 = 1;
const LD2420_NEAR_FIELD_CLUTTER_DISTANCE_DELTA_CM: i32 = 105;
const LD2420_NEAR_FIELD_CLUTTER_PEAK_SHARE_PERCENT: u32 = 45;
const LD2420_NEAR_FIELD_CLUTTER_BAND_GATES: usize = 3;
const LD2420_NEAR_FIELD_CLUTTER_BAND_SHARE_PERCENT: u32 = 55;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct RadarTuning {
    pub max_detection_range_cm: u16,
    pub min_gate_energy: u16,
    pub sensitivity_percent: u8,
    pub min_active_gates: u8,
    pub min_activity_score: u8,
}

impl Default for RadarTuning {
    fn default() -> Self {
        Self {
            max_detection_range_cm: (LD2420_GATE_COUNT as u16) * (LD2420_GATE_SIZE_CM as u16),
            min_gate_energy: LD2420_ACTIVE_GATE_FLOOR,
            sensitivity_percent: 55,
            min_active_gates: 1,
            min_activity_score: 10,
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct RadarDerivedMetrics {
    pub valid: bool,
    pub energy_based: bool,
    pub estimated_people: u8,
    pub active_gate_count: u8,
    pub dominant_gate_index: i32,
    pub dominant_gate_distance_cm: i32,
    pub dominant_gate_energy: u16,
    pub total_gate_energy: u32,
    pub activity_score: u8,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RadarDetectionDecision {
    Candidate,
    InvalidMetrics,
    OutOfRange,
    InsufficientActiveGates,
    LowActivity,
    MissingEnergyFrame,
    LowEnergy,
    NearFieldClutter,
}

impl Default for RadarDerivedMetrics {
    fn default() -> Self {
        Self {
            valid: false,
            energy_based: false,
            estimated_people: 0,
            active_gate_count: 0,
            dominant_gate_index: -1,
            dominant_gate_distance_cm: -1,
            dominant_gate_energy: 0,
            total_gate_energy: 0,
            activity_score: 0,
        }
    }
}

pub fn build_radar_derived_metrics(frame: Option<&RadarFrame>, gpio_presence: bool) -> RadarDerivedMetrics {
    let mut metrics = RadarDerivedMetrics {
        valid: frame.is_some() || gpio_presence,
        ..RadarDerivedMetrics::default()
    };

    match frame {
        Some(RadarFrame::Energy(energy)) => {
            metrics.energy_based = true;
            apply_energy_metrics(&mut metrics, energy);
            if energy_frame_looks_like_near_field_clutter(&metrics, energy) {
                metrics.estimated_people = 0;
            }
        }
        Some(RadarFrame::Text(text)) => apply_text_metrics(&mut metrics, text),
        Some(RadarFrame::Generic(_)) => apply_gpio_metrics(&mut metrics, gpio_presence),
        None => apply_gpio_metrics(&mut metrics, gpio_presence),
    }

    metrics
}

pub fn radar_detection_candidate(
    metrics: &RadarDerivedMetrics,
    frame: Option<&RadarFrame>,
    tuning: RadarTuning,
) -> bool {
    matches!(
        radar_detection_decision(metrics, frame, tuning),
        RadarDetectionDecision::Candidate
    )
}

pub fn radar_detection_decision(
    metrics: &RadarDerivedMetrics,
    frame: Option<&RadarFrame>,
    tuning: RadarTuning,
) -> RadarDetectionDecision {
    if !metrics.valid {
        return RadarDetectionDecision::InvalidMetrics;
    }

    if metrics.dominant_gate_distance_cm >= 0
        && metrics.dominant_gate_distance_cm > i32::from(tuning.max_detection_range_cm)
    {
        return RadarDetectionDecision::OutOfRange;
    }

    if metrics.active_gate_count < tuning.min_active_gates {
        return RadarDetectionDecision::InsufficientActiveGates;
    }

    let min_activity = effective_min_activity_score(tuning);
    if metrics.activity_score < min_activity {
        return RadarDetectionDecision::LowActivity;
    }

    if metrics.energy_based {
        let Some(RadarFrame::Energy(energy_frame)) = frame else {
            return RadarDetectionDecision::MissingEnergyFrame;
        };

        let min_energy = effective_min_gate_energy(tuning);
        let min_total_energy = u32::from(min_energy) * u32::from(tuning.min_active_gates.max(1));
        if metrics.dominant_gate_energy < min_energy && metrics.total_gate_energy < min_total_energy {
            return RadarDetectionDecision::LowEnergy;
        }

        if energy_frame_looks_like_near_field_clutter(metrics, energy_frame) {
            return RadarDetectionDecision::NearFieldClutter;
        }
    }

    if metrics.estimated_people > 0 || metrics.activity_score >= min_activity {
        RadarDetectionDecision::Candidate
    } else {
        RadarDetectionDecision::LowActivity
    }
}

pub fn effective_min_gate_energy(tuning: RadarTuning) -> u16 {
    let sensitivity = tuning.sensitivity_percent.clamp(10, 100);
    let scaled = (u32::from(tuning.min_gate_energy) * u32::from(150 - sensitivity)) / 100;
    scaled.max(10) as u16
}

pub fn effective_min_activity_score(tuning: RadarTuning) -> u8 {
    let sensitivity = tuning.sensitivity_percent.clamp(10, 100);
    let scaled = (u32::from(tuning.min_activity_score) * u32::from(150 - sensitivity)) / 100;
    scaled.clamp(1, 100) as u8
}

fn apply_energy_metrics(metrics: &mut RadarDerivedMetrics, energy: &EnergyFrame) {
    let mut peak_energy = 0_u16;
    let mut peak_index = -1_i32;
    let mut cluster_count = 0_u8;
    let mut cluster_open = false;

    for (gate_index, gate_energy) in energy.gates.iter().copied().enumerate() {
        metrics.total_gate_energy += u32::from(gate_energy);
        if gate_energy > peak_energy {
            peak_energy = gate_energy;
            peak_index = gate_index as i32;
        }
    }

    let active_threshold = if peak_energy > 0 {
        LD2420_ACTIVE_GATE_FLOOR.max(peak_energy / 5)
    } else {
        LD2420_ACTIVE_GATE_FLOOR
    };

    for gate_energy in energy.gates.iter().copied() {
        let gate_active = gate_energy >= active_threshold;
        if gate_active {
            metrics.active_gate_count += 1;
        }

        if gate_active && !cluster_open {
            cluster_count += 1;
            cluster_open = true;
        } else if !gate_active {
            cluster_open = false;
        }
    }

    metrics.dominant_gate_index = peak_index;
    metrics.dominant_gate_energy = peak_energy;
    if peak_index >= 0 {
        metrics.dominant_gate_distance_cm = (peak_index * LD2420_GATE_SIZE_CM) + (LD2420_GATE_SIZE_CM / 2);
    }

    if energy.presence && peak_energy > 0 {
        let mut estimated_people = 1_u8.max(cluster_count);
        if metrics.active_gate_count >= 8 && metrics.total_gate_energy >= (u32::from(peak_energy) * 3) {
            estimated_people += 1;
        }
        metrics.estimated_people = estimated_people.min(LD2420_MAX_ESTIMATED_PEOPLE);
    }

    let activity_score = (metrics.total_gate_energy / 300)
        + (u32::from(metrics.active_gate_count) * 6)
        + (u32::from(peak_energy) / 120);
    metrics.activity_score = activity_score.min(100) as u8;
}

fn apply_text_metrics(metrics: &mut RadarDerivedMetrics, text: &TextFrame) {
    metrics.estimated_people = if text.presence { 1 } else { 0 };
    metrics.active_gate_count = if text.presence { 1 } else { 0 };
    metrics.activity_score = if text.presence { 15 } else { 0 };

    if let Some(range_cm) = text.range_cm {
        metrics.dominant_gate_distance_cm = i32::from(range_cm);
        metrics.dominant_gate_index = ((i32::from(range_cm)) / LD2420_GATE_SIZE_CM).min((LD2420_GATE_COUNT - 1) as i32);
    }
}

fn apply_gpio_metrics(metrics: &mut RadarDerivedMetrics, gpio_presence: bool) {
    metrics.estimated_people = if gpio_presence { 1 } else { 0 };
    metrics.active_gate_count = if gpio_presence { 1 } else { 0 };
    metrics.activity_score = if gpio_presence { 5 } else { 0 };
}

fn energy_frame_looks_like_near_field_clutter(
    metrics: &RadarDerivedMetrics,
    energy: &EnergyFrame,
) -> bool {
    if !metrics.energy_based || metrics.dominant_gate_index < 0 {
        return false;
    }

    if metrics.dominant_gate_index > LD2420_NEAR_FIELD_CLUTTER_MAX_GATE_INDEX || metrics.total_gate_energy == 0 {
        return false;
    }

    let reported_distance_cm = i32::from(energy.distance_cm);
    if reported_distance_cm <= 0 || metrics.dominant_gate_distance_cm < 0 {
        return false;
    }

    if (reported_distance_cm - metrics.dominant_gate_distance_cm) < LD2420_NEAR_FIELD_CLUTTER_DISTANCE_DELTA_CM {
        return false;
    }

    let near_field_band_energy: u32 = energy.gates[..LD2420_GATE_COUNT.min(LD2420_NEAR_FIELD_CLUTTER_BAND_GATES)]
        .iter()
        .map(|value| u32::from(*value))
        .sum();
    let near_field_band_share_percent = (near_field_band_energy * 100) / metrics.total_gate_energy;
    if near_field_band_share_percent >= LD2420_NEAR_FIELD_CLUTTER_BAND_SHARE_PERCENT {
        return true;
    }

    let peak_share_percent = (u32::from(metrics.dominant_gate_energy) * 100) / metrics.total_gate_energy;
    if peak_share_percent < LD2420_NEAR_FIELD_CLUTTER_PEAK_SHARE_PERCENT {
        return false;
    }

    let mut reported_gate_index = (reported_distance_cm + (LD2420_GATE_SIZE_CM / 2)) / LD2420_GATE_SIZE_CM;
    reported_gate_index = reported_gate_index.clamp(0, (LD2420_GATE_COUNT - 1) as i32);

    let mut reported_neighborhood_energy = u32::from(energy.gates[reported_gate_index as usize]);
    if reported_gate_index > 0 {
        reported_neighborhood_energy += u32::from(energy.gates[(reported_gate_index - 1) as usize]);
    }
    if reported_gate_index < (LD2420_GATE_COUNT - 1) as i32 {
        reported_neighborhood_energy += u32::from(energy.gates[(reported_gate_index + 1) as usize]);
    }

    u32::from(metrics.dominant_gate_energy) > reported_neighborhood_energy
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::radar::{EnergyFrame, GenericFrame, RadarFrame, TextFrame};

    #[test]
    fn builds_energy_metrics_from_fixture_like_frame() {
        let frame = RadarFrame::Energy(EnergyFrame {
            length: 45,
            payload_length: 35,
            presence: false,
            distance_cm: 0,
            gates: [12898, 3730, 362, 36, 80, 98, 20, 13, 13, 9, 10, 13, 20, 10, 13, 10],
        });

        let metrics = build_radar_derived_metrics(Some(&frame), false);

        assert_eq!(metrics.valid, true);
        assert_eq!(metrics.energy_based, true);
        assert_eq!(metrics.estimated_people, 0);
        assert_eq!(metrics.active_gate_count, 2);
        assert_eq!(metrics.dominant_gate_index, 0);
        assert_eq!(metrics.dominant_gate_distance_cm, 35);
        assert_eq!(metrics.dominant_gate_energy, 12898);
        assert_eq!(metrics.total_gate_energy, 17335);
        assert_eq!(metrics.activity_score, 100);
    }

    #[test]
    fn suppresses_near_field_clutter_candidate() {
        let frame = RadarFrame::Energy(EnergyFrame {
            length: 45,
            payload_length: 35,
            presence: true,
            distance_cm: 280,
            gates: [1000, 500, 100, 10, 5, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
        });
        let metrics = build_radar_derived_metrics(Some(&frame), false);

        assert_eq!(metrics.estimated_people, 0);
        assert_eq!(
            radar_detection_decision(&metrics, Some(&frame), RadarTuning::default()),
            RadarDetectionDecision::NearFieldClutter
        );
        assert_eq!(radar_detection_candidate(&metrics, Some(&frame), RadarTuning::default()), false);
    }

    #[test]
    fn builds_text_metrics() {
        let frame = RadarFrame::Text(TextFrame {
            length: 14,
            presence: true,
            range_cm: Some(140),
            hex: "4F4E0D0A52616E6765203134300D0A".into(),
            ascii: "ON\r\nRange 140\r\n".into(),
        });

        let metrics = build_radar_derived_metrics(Some(&frame), false);

        assert_eq!(metrics.estimated_people, 1);
        assert_eq!(metrics.active_gate_count, 1);
        assert_eq!(metrics.activity_score, 15);
        assert_eq!(metrics.dominant_gate_distance_cm, 140);
        assert_eq!(metrics.dominant_gate_index, 2);
    }

    #[test]
    fn falls_back_to_gpio_presence_for_generic_frames() {
        let frame = RadarFrame::Generic(GenericFrame {
            length: 3,
            hex: "010203".into(),
            ascii: "...".into(),
        });

        let metrics = build_radar_derived_metrics(Some(&frame), true);

        assert_eq!(metrics.estimated_people, 1);
        assert_eq!(metrics.active_gate_count, 1);
        assert_eq!(metrics.activity_score, 5);
    }

    #[test]
    fn computes_effective_thresholds_from_sensitivity() {
        let tuning = RadarTuning {
            min_gate_energy: 25,
            sensitivity_percent: 55,
            min_activity_score: 10,
            ..RadarTuning::default()
        };

        assert_eq!(effective_min_gate_energy(tuning), 23);
        assert_eq!(effective_min_activity_score(tuning), 9);
    }
}