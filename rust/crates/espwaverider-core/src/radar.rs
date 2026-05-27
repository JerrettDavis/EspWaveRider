use alloc::string::String;

const ENERGY_HEADER: [u8; 4] = [0xF4, 0xF3, 0xF2, 0xF1];
const ENERGY_FOOTER: [u8; 4] = [0xF8, 0xF7, 0xF6, 0xF5];
const ENERGY_GATE_COUNT: usize = 16;
const ENERGY_FRAME_MIN_LENGTH: usize = 45;

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum RadarFrame {
    Energy(EnergyFrame),
    Text(TextFrame),
    Generic(GenericFrame),
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct EnergyFrame {
    pub length: usize,
    pub payload_length: u16,
    pub presence: bool,
    pub distance_cm: u16,
    pub gates: [u16; ENERGY_GATE_COUNT],
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct TextFrame {
    pub length: usize,
    pub presence: bool,
    pub range_cm: Option<u16>,
    pub hex: String,
    pub ascii: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct GenericFrame {
    pub length: usize,
    pub hex: String,
    pub ascii: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct RadarParseError;

impl core::fmt::Display for RadarParseError {
    fn fmt(&self, formatter: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        formatter.write_str("empty radar frame")
    }
}

impl core::error::Error for RadarParseError {}

pub fn parse_radar_frame(data: &[u8]) -> Result<RadarFrame, RadarParseError> {
    if data.is_empty() {
        return Err(RadarParseError);
    }

    if let Some(frame) = try_parse_energy_frame(data) {
        return Ok(RadarFrame::Energy(frame));
    }

    if let Some(frame) = try_parse_text_frame(data) {
        return Ok(RadarFrame::Text(frame));
    }

    Ok(RadarFrame::Generic(GenericFrame {
        length: data.len(),
        hex: build_hex_string(data),
        ascii: build_ascii_string(data),
    }))
}

pub fn try_parse_energy_frame(data: &[u8]) -> Option<EnergyFrame> {
    if data.len() < ENERGY_FRAME_MIN_LENGTH {
        return None;
    }

    if !starts_with_energy_header(data) || !ends_with_energy_footer(data) {
        return None;
    }

    let payload_length = read_le16(data, 4)?;
    let presence = data[6] != 0;
    let distance_cm = read_le16(data, 7)?;
    let mut gates = [0_u16; ENERGY_GATE_COUNT];

    for (index, gate) in gates.iter_mut().enumerate() {
        *gate = read_le16(data, 9 + (index * 2))?;
    }

    Some(EnergyFrame {
        length: data.len(),
        payload_length,
        presence,
        distance_cm,
        gates,
    })
}

pub fn try_parse_text_frame(data: &[u8]) -> Option<TextFrame> {
    let ascii = parse_textish_ascii(data)?;

    if !ascii.contains("Range") && !ascii.contains("ON") && !ascii.contains("OFF") {
        return None;
    }

    let presence = ascii.contains("ON");
    let range_cm = extract_range_cm(&ascii);

    Some(TextFrame {
        length: data.len(),
        presence,
        range_cm,
        hex: build_hex_string(data),
        ascii,
    })
}

fn starts_with_energy_header(data: &[u8]) -> bool {
    data.starts_with(&ENERGY_HEADER)
}

fn ends_with_energy_footer(data: &[u8]) -> bool {
    data.ends_with(&ENERGY_FOOTER)
}

fn read_le16(data: &[u8], offset: usize) -> Option<u16> {
    let first = *data.get(offset)?;
    let second = *data.get(offset + 1)?;
    Some(u16::from(first) | (u16::from(second) << 8))
}

fn parse_textish_ascii(data: &[u8]) -> Option<String> {
    let mut ascii = String::with_capacity(data.len());

    for &byte in data {
        match byte {
            b'\r' | b'\n' | b'\t' | 32..=126 => ascii.push(byte as char),
            _ => return None,
        }
    }

    Some(ascii)
}

fn extract_range_cm(ascii: &str) -> Option<u16> {
    let range_start = ascii.find("Range")?;
    let range_text = ascii.get(range_start + 5..)?.trim();
    range_text.parse::<u16>().ok()
}

fn build_hex_string(data: &[u8]) -> String {
    const HEX: &[u8; 16] = b"0123456789ABCDEF";
    let mut output = String::with_capacity(data.len() * 2);
    for byte in data {
        output.push(HEX[((byte >> 4) & 0x0F) as usize] as char);
        output.push(HEX[(byte & 0x0F) as usize] as char);
    }
    output
}

fn build_ascii_string(data: &[u8]) -> String {
    let mut output = String::with_capacity(data.len());
    for &byte in data {
        output.push(if (32..=126).contains(&byte) { byte as char } else { '.' });
    }
    output
}

#[cfg(test)]
mod tests {
    use super::*;
    use alloc::vec;
    use alloc::vec::Vec;

    #[test]
    fn parses_synthetic_energy_frame() {
        let mut frame = vec![0_u8; 45];
        frame[0..4].copy_from_slice(&ENERGY_HEADER);
        frame[4] = 35;
        frame[5] = 0;
        frame[6] = 0;
        frame[7] = 0;
        frame[8] = 0;

        let gates = [12898_u16, 3730, 362, 36, 80, 98, 20, 13, 13, 9, 10, 13, 20, 10, 13, 10];
        for (index, gate) in gates.iter().enumerate() {
            let offset = 9 + (index * 2);
            frame[offset] = (*gate & 0xFF) as u8;
            frame[offset + 1] = (*gate >> 8) as u8;
        }
        frame[41..45].copy_from_slice(&ENERGY_FOOTER);

        let parsed = parse_radar_frame(&frame).unwrap();

        assert_eq!(
            parsed,
            RadarFrame::Energy(EnergyFrame {
                length: 45,
                payload_length: 35,
                presence: false,
                distance_cm: 0,
                gates,
            })
        );
    }

    #[test]
    fn parses_text_frame_with_presence_and_range() {
        let frame = b"ON\r\nRange 22\r\n";

        let parsed = parse_radar_frame(frame).unwrap();

        assert_eq!(
            parsed,
            RadarFrame::Text(TextFrame {
                length: frame.len(),
                presence: true,
                range_cm: Some(22),
                hex: "4F4E0D0A52616E67652032320D0A".into(),
                ascii: "ON\r\nRange 22\r\n".into(),
            })
        );
    }

    #[test]
    fn falls_back_to_generic_frame_for_non_text_non_energy_bytes() {
        let bytes = decode_hex(
            "110012000D000D000D00F8F7F6F5F4F3F2F123000169005257F113DD006801280014003A0014000D000A002000140011000D000D000A00F8F7F6F5F4F3F2F12300016900754AA511C901DD0048006400120049000D00140014001900110014000D000D00F8F7F6F5F4F3F2F12300016900A1414510610152001A001400120019001400110019001400140011000A001400F8F7F6F5F4F3F2F12300016900444B241328015A0028001D0050002D001A002900110014000D00110022001100F8F7F6F5",
        );

        let parsed = parse_radar_frame(&bytes).unwrap();

        assert_eq!(
            parsed,
            RadarFrame::Generic(GenericFrame {
                length: 194,
                hex: "110012000D000D000D00F8F7F6F5F4F3F2F123000169005257F113DD006801280014003A0014000D000A002000140011000D000D000A00F8F7F6F5F4F3F2F12300016900754AA511C901DD0048006400120049000D00140014001900110014000D000D00F8F7F6F5F4F3F2F12300016900A1414510610152001A001400120019001400110019001400140011000A001400F8F7F6F5F4F3F2F12300016900444B241328015A0028001D0050002D001A002900110014000D00110022001100F8F7F6F5".into(),
                ascii: "..................#..i.RW....h.(...:....... ...................#..i.uJ......H.d...I.........................#..i..AE.a.R.................................#..i.DK$.(.Z.(...P.-...).........\".......".into(),
            })
        );
    }

    #[test]
    fn rejects_empty_frames() {
        assert_eq!(parse_radar_frame(&[]), Err(RadarParseError));
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
}