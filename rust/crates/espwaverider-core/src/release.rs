use alloc::string::String;

pub fn semantic_version_core(version: &str) -> String {
    let trimmed = version.trim();
    if trimmed.is_empty() {
        return String::new();
    }

    let bytes = trimmed.as_bytes();
    let mut cursor = if matches!(bytes.first(), Some(b'v' | b'V')) { 1 } else { 0 };
    let major_start = cursor;
    while cursor < bytes.len() && bytes[cursor].is_ascii_digit() {
        cursor += 1;
    }
    if cursor <= major_start || cursor >= bytes.len() || bytes[cursor] != b'.' {
        return String::new();
    }

    let minor_start = cursor + 1;
    cursor = minor_start;
    while cursor < bytes.len() && bytes[cursor].is_ascii_digit() {
        cursor += 1;
    }
    if cursor <= minor_start || cursor >= bytes.len() || bytes[cursor] != b'.' {
        return String::new();
    }

    let patch_start = cursor + 1;
    cursor = patch_start;
    while cursor < bytes.len() && bytes[cursor].is_ascii_digit() {
        cursor += 1;
    }
    if cursor <= patch_start {
        return String::new();
    }

    String::from(&trimmed[major_start..cursor])
}

pub fn firmware_release_asset_name(version: &str, build_target: &str, extension: &str) -> String {
    let version_core = semantic_version_core(version);
    if version_core.is_empty() || build_target.trim().is_empty() {
        return String::new();
    }

    let mut asset_name = String::from("EspWaveRider-");
    asset_name.push_str(&version_core);
    asset_name.push('-');
    asset_name.push_str(build_target.trim());
    asset_name.push_str(extension);
    asset_name
}

pub fn firmware_release_asset_url(
    owner: &str,
    repo: &str,
    version: &str,
    build_target: &str,
) -> String {
    let version_core = semantic_version_core(version);
    if version_core.is_empty() || owner.trim().is_empty() || repo.trim().is_empty() {
        return String::new();
    }

    let asset_name = firmware_release_asset_name(&version_core, build_target, ".bin");
    if asset_name.is_empty() {
        return String::new();
    }

    let mut url = String::from("https://github.com/");
    url.push_str(owner.trim());
    url.push('/');
    url.push_str(repo.trim());
    url.push_str("/releases/download/v");
    url.push_str(&version_core);
    url.push('/');
    url.push_str(&asset_name);
    url
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn normalizes_semantic_version_core() {
        assert_eq!(semantic_version_core("v1.2.3-4-gabcd"), "1.2.3");
        assert_eq!(semantic_version_core("1.2.3"), "1.2.3");
        assert!(semantic_version_core("main").is_empty());
    }

    #[test]
    fn builds_release_asset_name() {
        assert_eq!(
            firmware_release_asset_name("v1.2.3-4-gabcd", "lonely-esp32-s3-devkitm-1", ".bin"),
            "EspWaveRider-1.2.3-lonely-esp32-s3-devkitm-1.bin"
        );
    }

    #[test]
    fn builds_release_asset_url() {
        assert_eq!(
            firmware_release_asset_url(
                "JerrettDavis",
                "EspWaveRider",
                "v1.2.3-4-gabcd",
                "lonely-esp32-s3-devkitm-1"
            ),
            "https://github.com/JerrettDavis/EspWaveRider/releases/download/v1.2.3/EspWaveRider-1.2.3-lonely-esp32-s3-devkitm-1.bin"
        );
    }
}