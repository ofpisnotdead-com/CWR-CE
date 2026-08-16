//! PBO container read/write. See the crate docs for the on-disk layout.

use std::fs;
use std::io::Write;
use std::path::{Component, Path};

use anyhow::{bail, Context, Result};
use walkdir::WalkDir;

use crate::lzss;

/// Stored raw (no packing).
pub const MIME_NONE: u32 = 0;
/// LZSS-compressed entry ('Cprs', little-endian on disk).
pub const MIME_COMPRESSED: u32 = 0x4370_7273;
/// Marks the leading properties entry ('Vers').
pub const MIME_VERSION: u32 = 0x5665_7273;
/// Encrypted headers ('Encr') — recognised but unsupported.
pub const MIME_ENCRYPTED: u32 = 0x456e_6372;

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct PboEntry {
    /// Normalised logical path within the archive. Kept forward-slash internally for portable
    /// lookup/extract, serialized with OFP/PBO backslashes on write.
    pub name: String,
    pub mime: u32,
    /// Decompressed size when compressed; 0 for stored entries.
    pub original_size: u32,
    pub timestamp: u32,
    /// On-disk (stored/compressed) size of the data block.
    pub data_size: u32,
    /// Offset of this entry's data within the data region (computed on read/pack).
    pub data_offset: u64,
}

impl PboEntry {
    #[must_use]
    pub const fn is_compressed(&self) -> bool {
        self.mime == MIME_COMPRESSED
    }

    /// The logical (decompressed) size of the entry's contents.
    #[must_use]
    pub const fn unpacked_size(&self) -> u64 {
        if self.is_compressed() {
            self.original_size as u64
        } else {
            self.data_size as u64
        }
    }
}

#[derive(Clone, Debug, Default)]
pub struct Pbo {
    pub properties: Vec<(String, String)>,
    pub entries: Vec<PboEntry>,
    data: Vec<u8>,
}

impl Pbo {
    /// Read and parse a PBO from a file.
    pub fn read_path<P: AsRef<Path>>(path: P) -> Result<Self> {
        let path = path.as_ref();
        let bytes = fs::read(path).with_context(|| format!("reading {}", path.display()))?;
        Self::read_bytes(&bytes)
    }

    /// Read and parse a PBO from an in-memory buffer.
    pub fn read_bytes(buf: &[u8]) -> Result<Self> {
        let mut cur = 0usize;
        let mut properties: Vec<(String, String)> = Vec::new();
        let mut entries: Vec<PboEntry> = Vec::new();

        let first = read_entry(buf, &mut cur)?;
        let mut read_more = true;
        if first.name.is_empty() {
            if first.mime == MIME_VERSION {
                loop {
                    let key = read_cstr(buf, &mut cur)?;
                    if key.is_empty() {
                        break;
                    }
                    let value = read_cstr(buf, &mut cur)?;
                    properties.push((key, value));
                }
            } else {
                // Empty archive: the first entry is the terminator.
                read_more = false;
            }
        } else {
            entries.push(first);
        }

        if properties
            .iter()
            .any(|(k, _)| k.eq_ignore_ascii_case("encryption"))
        {
            bail!("encrypted PBO headers are not supported");
        }

        if read_more {
            loop {
                let entry = read_entry(buf, &mut cur)?;
                if entry.name.is_empty() {
                    break;
                }
                entries.push(entry);
            }
        }

        let header_size = cur;
        let mut offset = 0usize;
        for entry in &mut entries {
            entry.data_offset = offset as u64;
            offset += entry.data_size as usize;
        }

        let data_end = header_size + offset;
        if buf.len() < data_end {
            bail!(
                "PBO data truncated: header+data needs {data_end} bytes, file has {}",
                buf.len()
            );
        }
        let data = buf[header_size..data_end].to_vec();

        Ok(Self {
            properties,
            entries,
            data,
        })
    }

    /// A property value by case-insensitive key (e.g. `prefix`, `product`).
    #[must_use]
    pub fn property(&self, key: &str) -> Option<&str> {
        self.properties
            .iter()
            .find(|(k, _)| k.eq_ignore_ascii_case(key))
            .map(|(_, v)| v.as_str())
    }

    /// Decompressed contents of an entry.
    pub fn entry_data(&self, entry: &PboEntry) -> Result<Vec<u8>> {
        let start = usize::try_from(entry.data_offset).context("entry offset too large")?;
        let end = start + entry.data_size as usize;
        let raw = self
            .data
            .get(start..end)
            .ok_or_else(|| anyhow::anyhow!("entry '{}' data out of range", entry.name))?;
        match entry.mime {
            MIME_COMPRESSED => lzss::decode(raw, entry.original_size as usize),
            MIME_ENCRYPTED => bail!("encrypted entry '{}' is not supported", entry.name),
            _ => Ok(raw.to_vec()),
        }
    }

    /// Decompressed contents of an entry by name, if present.
    pub fn read(&self, name: &str) -> Option<Result<Vec<u8>>> {
        let normalized = normalize_name(name);
        self.entries
            .iter()
            .find(|e| e.name == normalized)
            .map(|e| self.entry_data(e))
    }

    /// Pack every file under `root` into a store-only (uncompressed) PBO. When `prefix`
    /// is given, a `prefix` property is written so the engine mounts the addon correctly.
    pub fn pack_dir<P: AsRef<Path>>(root: P, prefix: Option<&str>) -> Result<Self> {
        let root = root.as_ref();
        let mut files: Vec<(String, Vec<u8>)> = Vec::new();
        for entry in WalkDir::new(root).sort_by_file_name() {
            let entry = entry?;
            if !entry.file_type().is_file() {
                continue;
            }
            let rel = entry
                .path()
                .strip_prefix(root)
                .with_context(|| format!("path outside root: {}", entry.path().display()))?;
            let name = normalize_name(&rel.to_string_lossy());
            let bytes = fs::read(entry.path())
                .with_context(|| format!("reading {}", entry.path().display()))?;
            files.push((name, bytes));
        }
        files.sort_by(|a, b| a.0.cmp(&b.0));

        let mut data: Vec<u8> = Vec::new();
        let mut entries: Vec<PboEntry> = Vec::new();
        let mut offset = 0u64;
        for (name, bytes) in files {
            let len = u32::try_from(bytes.len())
                .with_context(|| format!("file '{name}' exceeds 4 GiB"))?;
            entries.push(PboEntry {
                name,
                mime: MIME_NONE,
                original_size: 0,
                timestamp: 0,
                data_size: len,
                data_offset: offset,
            });
            offset += u64::from(len);
            data.extend_from_slice(&bytes);
        }

        let properties = prefix
            .map(|p| vec![("prefix".to_string(), p.to_string())])
            .unwrap_or_default();

        Ok(Self {
            properties,
            entries,
            data,
        })
    }

    /// Serialise this PBO to a writer (header + data; no trailing checksum).
    pub fn write<W: Write>(&self, writer: &mut W) -> Result<()> {
        if !self.properties.is_empty() {
            write_cstr(writer, "")?; // empty name marks the properties entry
            write_u32(writer, MIME_VERSION)?;
            write_u32(writer, 0)?; // original size
            write_u32(writer, 0)?; // reserved
            write_u32(writer, 0)?; // timestamp
            write_u32(writer, 0)?; // data size
            for (key, value) in &self.properties {
                write_cstr(writer, key)?;
                write_cstr(writer, value)?;
            }
            write_cstr(writer, "")?; // end of properties
        }

        for entry in &self.entries {
            write_cstr(writer, &wire_name(&entry.name))?;
            write_u32(writer, entry.mime)?;
            write_u32(writer, entry.original_size)?;
            write_u32(writer, 0)?; // reserved (offset is computed on read)
            write_u32(writer, entry.timestamp)?;
            write_u32(writer, entry.data_size)?;
        }

        // Terminator: empty name + five zero u32s.
        write_cstr(writer, "")?;
        for _ in 0..5 {
            write_u32(writer, 0)?;
        }

        writer.write_all(&self.data)?;
        Ok(())
    }

    /// Serialise this PBO to a file.
    pub fn write_path<P: AsRef<Path>>(&self, path: P) -> Result<()> {
        let path = path.as_ref();
        let mut file =
            fs::File::create(path).with_context(|| format!("creating {}", path.display()))?;
        self.write(&mut file)
    }

    /// Extract every entry under `dest`, creating parent directories as needed.
    pub fn unpack_to_dir<P: AsRef<Path>>(&self, dest: P) -> Result<()> {
        let dest = dest.as_ref();
        for entry in &self.entries {
            let bytes = self.entry_data(entry)?;
            let relative = Path::new(&entry.name);
            if relative.is_absolute()
                || relative
                    .components()
                    .any(|component| !matches!(component, Component::Normal(_) | Component::CurDir))
                || entry
                    .name
                    .split('/')
                    .next()
                    .is_some_and(|component| component.ends_with(':'))
            {
                bail!("archive entry '{}' escapes the destination", entry.name);
            }
            let path = dest.join(relative);
            if let Some(parent) = path.parent() {
                fs::create_dir_all(parent)
                    .with_context(|| format!("creating {}", parent.display()))?;
            }
            fs::write(&path, bytes).with_context(|| format!("writing {}", path.display()))?;
        }
        Ok(())
    }
}

fn normalize_name(raw: &str) -> String {
    raw.replace('\\', "/").to_lowercase()
}

fn wire_name(name: &str) -> String {
    name.replace('/', "\\")
}

fn read_cstr(buf: &[u8], cur: &mut usize) -> Result<String> {
    let start = *cur;
    while *cur < buf.len() && buf[*cur] != 0 {
        *cur += 1;
    }
    if *cur >= buf.len() {
        bail!("PBO header truncated (unterminated string)");
    }
    let text = String::from_utf8_lossy(&buf[start..*cur]).into_owned();
    *cur += 1; // skip the null terminator
    Ok(text)
}

fn read_u32(buf: &[u8], cur: &mut usize) -> Result<u32> {
    let slice = buf
        .get(*cur..*cur + 4)
        .ok_or_else(|| anyhow::anyhow!("PBO header truncated (expected u32)"))?;
    *cur += 4;
    Ok(u32::from_le_bytes([slice[0], slice[1], slice[2], slice[3]]))
}

fn read_entry(buf: &[u8], cur: &mut usize) -> Result<PboEntry> {
    let raw_name = read_cstr(buf, cur)?;
    let mime = read_u32(buf, cur)?;
    let original_size = read_u32(buf, cur)?;
    let _reserved = read_u32(buf, cur)?;
    let timestamp = read_u32(buf, cur)?;
    let data_size = read_u32(buf, cur)?;
    Ok(PboEntry {
        name: normalize_name(&raw_name),
        mime,
        original_size,
        timestamp,
        data_size,
        data_offset: 0,
    })
}

fn write_cstr<W: Write>(writer: &mut W, text: &str) -> Result<()> {
    writer.write_all(text.as_bytes())?;
    writer.write_all(&[0u8])?;
    Ok(())
}

fn write_u32<W: Write>(writer: &mut W, value: u32) -> Result<()> {
    writer.write_all(&value.to_le_bytes())?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::path::PathBuf;

    fn fixture(name: &str) -> PathBuf {
        PathBuf::from(env!("CARGO_MANIFEST_DIR"))
            .join("../../tests/fixtures/pbo")
            .join(name)
    }

    #[test]
    fn reads_stored_addon_fixture() {
        let pbo = Pbo::read_path(fixture("addon_fixture.pbo")).unwrap();
        assert!(pbo.properties.is_empty());
        assert_eq!(pbo.entries.len(), 3);
        let entry = pbo
            .entries
            .iter()
            .find(|e| e.name == "config.bin")
            .expect("config.bin present");
        assert_eq!(entry.mime, MIME_NONE);
        assert_eq!(entry.data_size, 110);
        let data = pbo.entry_data(entry).unwrap();
        assert_eq!(data.len(), 110);
        let text = String::from_utf8_lossy(&data);
        assert!(
            text.contains("SyntheticArchiveFixture"),
            "unexpected config.bin text: {text:?}"
        );
    }

    #[test]
    fn reads_stored_addon_without_properties() {
        // addon_fixture.pbo starts directly with config.bin (no properties entry).
        let pbo = Pbo::read_path(fixture("addon_fixture.pbo")).unwrap();
        assert!(pbo.properties.is_empty());
        let entry = pbo
            .entries
            .iter()
            .find(|e| e.name == "config.bin")
            .expect("config.bin present");
        assert_eq!(entry.mime, MIME_NONE);
        assert_eq!(entry.data_size, 110);
        let data = pbo.entry_data(entry).unwrap();
        assert_eq!(data.len(), 110);
    }

    #[test]
    fn decompresses_every_entry_of_compressed_mission() {
        // mission_fixture.Intro.pbo is a mission PBO with LZSS-compressed entries. Reading every
        // entry exercises the LZSS decoder + checksum; a wrong decoder fails the checksum.
        let pbo = Pbo::read_path(fixture("mission_fixture.Intro.pbo")).unwrap();
        assert!(!pbo.entries.is_empty());
        let mut compressed = 0;
        for entry in &pbo.entries {
            let data = pbo
                .entry_data(entry)
                .unwrap_or_else(|e| panic!("decode {} failed: {e}", entry.name));
            assert_eq!(data.len() as u64, entry.unpacked_size());
            if entry.is_compressed() {
                compressed += 1;
            }
        }
        assert!(
            compressed > 0,
            "expected at least one LZSS-compressed entry in mission_fixture.Intro.pbo"
        );
    }

    #[test]
    fn pack_then_read_roundtrip_store_only() {
        let dir = tempfile::tempdir().unwrap();
        let root = dir.path();
        fs::write(root.join("config.cpp"), b"class CfgPatches {};\n").unwrap();
        fs::create_dir(root.join("data")).unwrap();
        fs::write(root.join("data/note.txt"), b"hello pbo\n").unwrap();

        let pbo = Pbo::pack_dir(root, Some("test\\addon")).unwrap();
        let mut bytes = Vec::new();
        pbo.write(&mut bytes).unwrap();

        // No trailing checksum: file ends exactly at header + data.
        let total_data: usize = pbo.entries.iter().map(|e| e.data_size as usize).sum();
        assert!(bytes.len() > total_data);

        let parsed = Pbo::read_bytes(&bytes).unwrap();
        // The prefix property is stored verbatim (OFP prefixes use backslashes).
        assert_eq!(parsed.property("prefix"), Some("test\\addon"));
        assert_eq!(parsed.entries.len(), 2);
        assert!(
            bytes
                .windows(b"data\\note.txt\0".len())
                .any(|window| window == b"data\\note.txt\0"),
            "PBO wire paths must use backslash separators"
        );
        assert!(
            !bytes
                .windows(b"data/note.txt\0".len())
                .any(|window| window == b"data/note.txt\0"),
            "PBO wire paths must not depend on host/Unix separators"
        );
        assert_eq!(
            parsed.read("config.cpp").unwrap().unwrap(),
            b"class CfgPatches {};\n"
        );
        assert_eq!(
            parsed.read("data/note.txt").unwrap().unwrap(),
            b"hello pbo\n"
        );
    }

    #[test]
    fn unpack_rejects_parent_and_absolute_entries() {
        let mut pbo = Pbo::read_path(fixture("addon_fixture.pbo")).unwrap();
        let dest = tempfile::tempdir().unwrap();

        pbo.entries[0].name = "../escape.bin".to_string();
        assert!(pbo.unpack_to_dir(dest.path()).is_err());
        assert!(!dest.path().parent().unwrap().join("escape.bin").exists());

        pbo.entries[0].name = "/absolute.bin".to_string();
        assert!(pbo.unpack_to_dir(dest.path()).is_err());

        pbo.entries[0].name = "c:/windows.bin".to_string();
        assert!(pbo.unpack_to_dir(dest.path()).is_err());
    }
}
