use alloc::{string::String, vec::Vec};

fn legacy_release_target_alias(build_target: &str) -> Option<&'static str> {
    match build_target.trim() {
        "lonely-esp32-s3-devkitm-1" => Some("esp32-s3-devkitm-1"),
        "esp32-generic-uart1" => Some("esp32dev-uart1"),
        "heltec-wifi-lora-32-v4-compatible" => Some("heltec-wifi-lora-32-v4"),
        _ => None,
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DownloadUrlScheme {
    Http,
    Https,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ParsedDownloadUrl {
    pub scheme: DownloadUrlScheme,
    pub host: String,
    pub port: u16,
    pub path: String,
}

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

pub fn firmware_release_asset_urls(
    owner: &str,
    repo: &str,
    version: &str,
    build_target: &str,
) -> Vec<String> {
    let canonical_url = firmware_release_asset_url(owner, repo, version, build_target);
    if canonical_url.is_empty() {
        return Vec::new();
    }

    let mut urls = Vec::with_capacity(2);
    urls.push(canonical_url);

    if let Some(alias_target) = legacy_release_target_alias(build_target) {
        let alias_url = firmware_release_asset_url(owner, repo, version, alias_target);
        if !alias_url.is_empty() && !urls.iter().any(|existing| existing == &alias_url) {
            urls.push(alias_url);
        }
    }

    urls
}

pub fn parse_download_url(url: &str) -> Option<ParsedDownloadUrl> {
    let trimmed = url.trim();
    let (scheme_text, remainder) = trimmed.split_once("://")?;
    let scheme = if scheme_text.eq_ignore_ascii_case("http") {
        DownloadUrlScheme::Http
    } else if scheme_text.eq_ignore_ascii_case("https") {
        DownloadUrlScheme::Https
    } else {
        return None;
    };

    let slash_index = remainder.find('/')?;
    let authority = &remainder[..slash_index];
    let path = &remainder[slash_index..];
    if authority.is_empty() || path.is_empty() {
        return None;
    }

    let default_port = match scheme {
        DownloadUrlScheme::Http => 80,
        DownloadUrlScheme::Https => 443,
    };

    let (host, port) = if let Some((host, port_text)) = authority.rsplit_once(':') {
        if host.is_empty() || port_text.is_empty() || !port_text.bytes().all(|byte| byte.is_ascii_digit()) {
            return None;
        }

        let port = port_text.parse::<u16>().ok()?;
        (host, port)
    } else {
        (authority, default_port)
    };

    Some(ParsedDownloadUrl {
        scheme,
        host: String::from(host),
        port,
        path: String::from(path),
    })
}

#[cfg(test)]
mod tests {
    use alloc::vec;

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

    #[test]
    fn builds_release_asset_url_candidates_with_legacy_alias() {
        assert_eq!(
            firmware_release_asset_urls(
                "JerrettDavis",
                "EspWaveRider",
                "v1.2.3-4-gabcd",
                "lonely-esp32-s3-devkitm-1"
            ),
            vec![
                String::from(
                    "https://github.com/JerrettDavis/EspWaveRider/releases/download/v1.2.3/EspWaveRider-1.2.3-lonely-esp32-s3-devkitm-1.bin"
                ),
                String::from(
                    "https://github.com/JerrettDavis/EspWaveRider/releases/download/v1.2.3/EspWaveRider-1.2.3-esp32-s3-devkitm-1.bin"
                ),
            ]
        );
    }

    #[test]
    fn parses_https_download_url() {
        assert_eq!(
            parse_download_url(
                "https://github.com/JerrettDavis/EspWaveRider/releases/download/v1.2.3/EspWaveRider-1.2.3-lonely-esp32-s3-devkitm-1.bin"
            ),
            Some(ParsedDownloadUrl {
                scheme: DownloadUrlScheme::Https,
                host: String::from("github.com"),
                port: 443,
                path: String::from(
                    "/JerrettDavis/EspWaveRider/releases/download/v1.2.3/EspWaveRider-1.2.3-lonely-esp32-s3-devkitm-1.bin"
                ),
            })
        );
    }

    #[test]
    fn parses_http_download_url_with_explicit_port() {
        assert_eq!(
            parse_download_url("http://example.local:8080/fw.bin"),
            Some(ParsedDownloadUrl {
                scheme: DownloadUrlScheme::Http,
                host: String::from("example.local"),
                port: 8080,
                path: String::from("/fw.bin"),
            })
        );
    }

    #[test]
    fn rejects_invalid_download_url() {
        assert!(parse_download_url("github.com/releases/download/fw.bin").is_none());
        assert!(parse_download_url("ftp://github.com/fw.bin").is_none());
        assert!(parse_download_url("https://github.com").is_none());
    }
}