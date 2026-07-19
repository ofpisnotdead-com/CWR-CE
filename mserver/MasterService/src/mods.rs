use std::path::Path;
use std::sync::Arc;
use std::time::Duration;

use anyhow::{bail, Context, Result};
use bytes::Bytes;
use futures::StreamExt;
use object_store::aws::{AmazonS3, AmazonS3Builder};
use object_store::local::LocalFileSystem;
use object_store::path::Path as ObjectPath;
use object_store::signer::Signer;
use object_store::{ObjectStore, WriteMultipart};
use reqwest::Method;
use sha2::{Digest, Sha256};

use crate::model::{ListModsQuery, ModCatalogEntry};

/// A streamed artifact download body: `(size_bytes, byte-stream)`. Lets the HTTP layer
/// stream a mod download straight from the store without buffering the whole artifact.
pub type ArtifactStream =
    futures::stream::BoxStream<'static, std::result::Result<Bytes, object_store::Error>>;

/// An in-flight streamed upload to a temporary object. The publisher compresses the artifact
/// (`.pbo.zst`) before upload, so the service stores the chunks verbatim — written as they
/// arrive so the process never holds the whole artifact (mods reach tens of GB).
/// `ModStore::finalize_mod` then derives the mod id and moves the temp object into place.
pub struct ArtifactUpload {
    temp: ObjectPath,
    writer: WriteMultipart,
    hasher: Sha256,
    size: u64,
}

impl ArtifactUpload {
    /// Append a chunk verbatim. `WriteMultipart` uploads completed parts in the background, so
    /// memory stays bounded regardless of total size.
    pub fn write(&mut self, chunk: &[u8]) {
        self.writer.write(chunk);
        self.hasher.update(chunk);
        self.size += chunk.len() as u64;
    }
}

const MOD_METADATA_FILE: &str = "mod.json";
const MOD_ARTIFACT_EXT: &str = "pbo.zst";

/// Inputs for publishing a packed mod to the workshop.
pub struct PublishModInput {
    pub name: String,
    pub app_name: Option<String>,
    pub actver: Option<i32>,
    pub version_tag: Option<String>,
    pub version: Option<String>,
    pub description: Option<String>,
    pub authors: Vec<String>,
    pub homepage_url: Option<String>,
    pub file: Vec<u8>,
}

/// Catalog metadata for a streamed upload, paired with an `ArtifactUpload` at finalize time.
/// `version` is optional (defaults to the raw content hash); the rest map straight onto the
/// `ModCatalogEntry`.
#[derive(Default)]
pub struct ModUploadMeta {
    pub name: String,
    pub app_name: Option<String>,
    pub actver: Option<i32>,
    pub version_tag: Option<String>,
    pub version: Option<String>,
    pub folder_name: Option<String>,
    pub description: Option<String>,
    pub authors: Vec<String>,
    pub homepage_url: Option<String>,
}

/// Object-store-backed mod artifact store. Each mod lives under `<modId>/`: the publisher-
/// compressed artifact (`<modId>.pbo.zst`, stored verbatim) plus a `mod.json` the catalog scan
/// reads. Backed by the local filesystem
/// for `cargo`-local dev and by S3 (MinIO) for deployment, so multiple stateless replicas
/// share one artifact store. The query layer above is identical for both backends.
#[derive(Clone)]
pub struct ModStore {
    store: Arc<dyn ObjectStore>,
    signer: Option<Arc<dyn Signer>>,
    prefix: Option<ObjectPath>,
    signed_url_ttl: Duration,
}

pub struct S3StoreConfig<'a> {
    pub bucket: &'a str,
    pub endpoint: &'a str,
    pub region: &'a str,
    pub access_key: &'a str,
    pub secret_key: &'a str,
    pub allow_http: bool,
    pub virtual_hosted_style: bool,
    pub prefix: Option<&'a str>,
    pub signed_downloads: bool,
    pub signed_url_ttl: Duration,
}

impl ModStore {
    /// Local-filesystem store rooted at `root` (created if missing). For `cargo`-local dev.
    pub fn local(root: &Path) -> Result<Self> {
        std::fs::create_dir_all(root)
            .with_context(|| format!("creating mods root {}", root.display()))?;
        let store = LocalFileSystem::new_with_prefix(root)
            .with_context(|| format!("opening local mods store at {}", root.display()))?;
        Ok(Self {
            store: Arc::new(store),
            signer: None,
            prefix: None,
            signed_url_ttl: Duration::from_secs(0),
        })
    }

    /// S3 (MinIO-compatible) store. `endpoint` is the MinIO URL (e.g. `http://minio:9000`);
    /// `allow_http` permits the in-cluster plaintext endpoint.
    pub fn s3(config: &S3StoreConfig<'_>) -> Result<Self> {
        let store: AmazonS3 = AmazonS3Builder::new()
            .with_bucket_name(config.bucket)
            .with_endpoint(config.endpoint)
            .with_region(config.region)
            .with_access_key_id(config.access_key)
            .with_secret_access_key(config.secret_key)
            .with_virtual_hosted_style_request(config.virtual_hosted_style)
            // ClientOptions replaces the builder's defaults wholesale, so allow_http must be
            // set here too. Disable the 30s request timeout: a streamed download/upload of a
            // large mod runs longer (bounded by the client's transfer speed), and the default
            // would abort the GET mid-stream — clients then see a truncated "partial file".
            .with_client_options(
                object_store::ClientOptions::new()
                    .with_allow_http(config.allow_http)
                    .with_timeout_disabled(),
            )
            .build()
            .with_context(|| {
                format!(
                    "opening S3 mods store at {}/{}",
                    config.endpoint, config.bucket
                )
            })?;
        let signer = config
            .signed_downloads
            .then(|| Arc::new(store.clone()) as Arc<dyn Signer>);
        Ok(Self {
            store: Arc::new(store),
            signer,
            prefix: normalize_prefix(config.prefix),
            signed_url_ttl: config.signed_url_ttl,
        })
    }

    fn artifact_path(mod_id: &str) -> ObjectPath {
        ObjectPath::from(format!("{mod_id}/{mod_id}.{MOD_ARTIFACT_EXT}"))
    }

    fn metadata_path(mod_id: &str) -> ObjectPath {
        ObjectPath::from(format!("{mod_id}/{MOD_METADATA_FILE}"))
    }

    fn store_path(&self, path: ObjectPath) -> ObjectPath {
        if let Some(prefix) = &self.prefix {
            ObjectPath::from(format!("{}/{}", prefix.as_ref(), path.as_ref()))
        } else {
            path
        }
    }

    fn list_prefix(&self) -> Option<&ObjectPath> {
        self.prefix.as_ref()
    }

    fn strip_prefix<'a>(&self, path: &'a ObjectPath) -> &'a str {
        let path = path.as_ref();
        if let Some(prefix) = &self.prefix {
            path.strip_prefix(prefix.as_ref())
                .and_then(|rest| rest.strip_prefix('/'))
                .unwrap_or(path)
        } else {
            path
        }
    }

    /// Store an uploaded mod: the artifact plus a `mod.json` the catalog scan picks up.
    /// `modId` is `slug(name)-version`, where `version` defaults to the first 8 hex of the
    /// file's SHA-256 (the `name-hash8` convention).
    pub async fn publish_mod(&self, input: &PublishModInput) -> Result<ModCatalogEntry> {
        let slug = slugify(&input.name);
        if slug.is_empty() {
            bail!(
                "mod name '{}' has no usable characters for an id",
                input.name
            );
        }
        if input.file.is_empty() {
            bail!("mod artifact is empty");
        }

        // The artifact arrives already compressed (`.pbo.zst`); store it verbatim.
        let version = match input.version.as_deref() {
            Some(value) => sanitize_version(value)
                .ok_or_else(|| anyhow::anyhow!("invalid version '{value}'"))?,
            None => content_hash8(&input.file),
        };
        let mod_id = format!("{slug}-{version}");

        let size_bytes = input.file.len() as u64;
        self.store
            .put(
                &self.store_path(Self::artifact_path(&mod_id)),
                Bytes::from(input.file.clone()).into(),
            )
            .await
            .with_context(|| format!("writing artifact for {mod_id}"))?;

        let entry = ModCatalogEntry {
            mod_id: mod_id.clone(),
            app_name: input.app_name.clone(),
            actver: input.actver,
            version_tag: input.version_tag.clone(),
            compatible: false,
            name: input.name.clone(),
            version,
            folder_name: None,
            description: input.description.clone().unwrap_or_default(),
            authors: input.authors.clone(),
            homepage_url: input.homepage_url.clone(),
            download_url: Some(format!("/v1/mods/{mod_id}/download")),
            size_bytes: Some(size_bytes),
        };
        let json = serde_json::to_vec_pretty(&entry)?;
        self.store
            .put(
                &self.store_path(Self::metadata_path(&mod_id)),
                Bytes::from(json).into(),
            )
            .await
            .with_context(|| format!("writing mod.json for {mod_id}"))?;

        Ok(entry)
    }

    /// Write a catalog entry's `mod.json` (used by dev seeding; writes no artifact, so the
    /// seeded entry carries a synthetic `size_bytes` and isn't downloadable).
    pub async fn put_metadata(&self, entry: &ModCatalogEntry) -> Result<()> {
        let json = serde_json::to_vec_pretty(entry)?;
        self.store
            .put(
                &self.store_path(Self::metadata_path(&entry.mod_id)),
                Bytes::from(json).into(),
            )
            .await
            .with_context(|| format!("writing mod.json for {}", entry.mod_id))?;
        Ok(())
    }

    /// Begin a streamed artifact upload to a unique temporary object.
    pub async fn begin_upload(&self) -> Result<ArtifactUpload> {
        let mut suffix = [0u8; 16];
        getrandom::getrandom(&mut suffix)
            .map_err(|error| anyhow::anyhow!("rng failed: {error}"))?;
        let temp = self.store_path(ObjectPath::from(format!("_incoming/{}.tmp", hex(&suffix))));
        let upload = self
            .store
            .put_multipart(&temp)
            .await
            .with_context(|| format!("starting upload {temp}"))?;
        Ok(ArtifactUpload {
            temp,
            writer: WriteMultipart::new(upload),
            hasher: Sha256::new(),
            size: 0,
        })
    }

    /// Finalize a streamed upload into a published mod: complete the temp object, derive the
    /// mod id (`slug(name)-version`, version defaulting to the content hash), move the temp
    /// object into place, and write `mod.json`.
    pub async fn finalize_mod(
        &self,
        upload: ArtifactUpload,
        meta: ModUploadMeta,
    ) -> Result<ModCatalogEntry> {
        let ArtifactUpload {
            temp,
            writer,
            hasher,
            size,
        } = upload;
        // Complete the temp object first so we never leave a dangling multipart, then
        // validate — deleting the temp object on any rejection.
        writer
            .finish()
            .await
            .with_context(|| format!("finishing upload {temp}"))?;

        let slug = slugify(&meta.name);
        if slug.is_empty() || size == 0 {
            let _ = self.store.delete(&temp).await;
            if slug.is_empty() {
                bail!(
                    "mod name '{}' has no usable characters for an id",
                    meta.name
                );
            }
            bail!("mod artifact is empty");
        }

        let version = match meta.version.as_deref() {
            Some(value) => match sanitize_version(value) {
                Some(version) => version,
                None => {
                    let _ = self.store.delete(&temp).await;
                    bail!("invalid version '{value}'");
                }
            },
            None => hasher
                .finalize()
                .iter()
                .take(4)
                .map(|b| format!("{b:02x}"))
                .collect(),
        };
        let mod_id = format!("{slug}-{version}");

        // Move the temp object into the mod's artifact path (server-side copy on S3).
        self.store
            .rename(&temp, &self.store_path(Self::artifact_path(&mod_id)))
            .await
            .with_context(|| format!("placing artifact for {mod_id}"))?;

        let entry = ModCatalogEntry {
            mod_id: mod_id.clone(),
            app_name: meta.app_name,
            actver: meta.actver,
            version_tag: meta.version_tag,
            compatible: false,
            name: meta.name,
            version,
            folder_name: meta
                .folder_name
                .map(|value| value.trim().to_string())
                .filter(|value| !value.is_empty()),
            description: meta.description.unwrap_or_default(),
            authors: meta.authors,
            homepage_url: meta.homepage_url,
            download_url: Some(format!("/v1/mods/{mod_id}/download")),
            size_bytes: Some(size),
        };
        let json = serde_json::to_vec_pretty(&entry)?;
        self.store
            .put(
                &self.store_path(Self::metadata_path(&mod_id)),
                Bytes::from(json).into(),
            )
            .await
            .with_context(|| format!("writing mod.json for {mod_id}"))?;
        Ok(entry)
    }

    /// Stream a published mod's artifact: `(size_bytes, byte-stream)`, or `None` if absent.
    pub async fn artifact_stream(&self, mod_id: &str) -> Result<Option<(u64, ArtifactStream)>> {
        if !is_safe_segment(mod_id) {
            return Ok(None);
        }
        match self
            .store
            .get(&self.store_path(Self::artifact_path(mod_id)))
            .await
        {
            Ok(result) => {
                let size = result.meta.size as u64;
                Ok(Some((size, result.into_stream())))
            }
            Err(object_store::Error::NotFound { .. }) => Ok(None),
            Err(error) => Err(error.into()),
        }
    }

    /// Signed direct URL for a published mod's artifact, or `None` when the backend is not
    /// externally reachable/signable or the artifact is absent.
    pub async fn signed_artifact_url(&self, mod_id: &str) -> Result<Option<(u64, String)>> {
        if !is_safe_segment(mod_id) {
            return Ok(None);
        }
        let Some(signer) = &self.signer else {
            return Ok(None);
        };
        let path = self.store_path(Self::artifact_path(mod_id));
        let size = match self.store.head(&path).await {
            Ok(meta) => meta.size as u64,
            Err(object_store::Error::NotFound { .. }) => return Ok(None),
            Err(error) => return Err(error.into()),
        };
        let url = signer
            .signed_url(Method::GET, &path, self.signed_url_ttl)
            .await?;
        Ok(Some((size, url.to_string())))
    }

    /// Raw bytes of a published mod's artifact, or `None` if absent. Rejects ids that could
    /// escape the store prefix.
    pub async fn artifact_bytes(&self, mod_id: &str) -> Result<Option<Vec<u8>>> {
        if !is_safe_segment(mod_id) {
            return Ok(None);
        }
        match self
            .store
            .get(&self.store_path(Self::artifact_path(mod_id)))
            .await
        {
            Ok(result) => Ok(Some(result.bytes().await?.to_vec())),
            Err(object_store::Error::NotFound { .. }) => Ok(None),
            Err(error) => Err(error.into()),
        }
    }

    /// Delete a mod from the store: its `mod.json` and its artifact. Returns `true` if the
    /// mod existed (its `mod.json` was present). Rejects unsafe ids.
    pub async fn delete_mod(&self, mod_id: &str) -> Result<bool> {
        if !is_safe_segment(mod_id) {
            return Ok(false);
        }
        // S3 DELETE is idempotent (it succeeds for a missing key), so existence can't be
        // inferred from the delete result — probe the mod.json (the catalog marker) first.
        match self
            .store
            .head(&self.store_path(Self::metadata_path(mod_id)))
            .await
        {
            Ok(_) => {}
            Err(object_store::Error::NotFound { .. }) => return Ok(false),
            Err(error) => return Err(error.into()),
        }
        self.store
            .delete(&self.store_path(Self::metadata_path(mod_id)))
            .await?;
        // Remove the artifact too; absent is fine (a metadata-only seed entry has none).
        match self
            .store
            .delete(&self.store_path(Self::artifact_path(mod_id)))
            .await
        {
            Ok(()) | Err(object_store::Error::NotFound { .. }) => {}
            Err(error) => return Err(error.into()),
        }
        Ok(true)
    }

    pub async fn list_mods(&self, query: &ListModsQuery) -> Result<Vec<ModCatalogEntry>> {
        let mut mods = self.read_all().await?;
        apply_mod_filters(&mut mods, query);
        Ok(mods)
    }

    pub async fn get_mod(&self, mod_id: &str) -> Result<Option<ModCatalogEntry>> {
        if !is_safe_segment(mod_id) {
            return Ok(None);
        }
        match self
            .store
            .get(&self.store_path(Self::metadata_path(mod_id)))
            .await
        {
            Ok(result) => {
                let bytes = result.bytes().await?;
                Ok(Some(self.parse_metadata(mod_id, &bytes).await?))
            }
            Err(object_store::Error::NotFound { .. }) => Ok(None),
            Err(error) => Err(error.into()),
        }
    }

    async fn read_all(&self) -> Result<Vec<ModCatalogEntry>> {
        // Collect every `<modId>/mod.json` object, then resolve each entry.
        let mut mod_ids = Vec::new();
        let mut listing = self.store.list(self.list_prefix());
        while let Some(meta) = listing.next().await {
            let location = meta?.location;
            if location.filename() == Some(MOD_METADATA_FILE) {
                let relative = self.strip_prefix(&location);
                if let Some((mod_id, _)) = relative.split_once('/') {
                    mod_ids.push(mod_id.to_string());
                }
            }
        }

        let mut mods = Vec::with_capacity(mod_ids.len());
        for mod_id in mod_ids {
            if let Some(entry) = self.get_mod(&mod_id).await? {
                mods.push(entry);
            }
        }

        mods.sort_by(|lhs, rhs| {
            lhs.name
                .to_lowercase()
                .cmp(&rhs.name.to_lowercase())
                .then_with(|| lhs.mod_id.cmp(&rhs.mod_id))
        });
        Ok(mods)
    }

    async fn parse_metadata(&self, dir_id: &str, bytes: &[u8]) -> Result<ModCatalogEntry> {
        let mut metadata: ModCatalogEntry = serde_json::from_slice(bytes)
            .with_context(|| format!("parsing mod.json for {dir_id}"))?;

        if metadata.mod_id.is_empty() {
            metadata.mod_id = dir_id.to_string();
        }

        // A mod.json written before these fields existed (or by hand) leaves them empty; fill
        // them from the store so the catalog always advertises a real size and a download
        // link rather than 0 bytes / no URL.
        if metadata.size_bytes.unwrap_or(0) == 0 {
            if let Ok(head) = self
                .store
                .head(&self.store_path(Self::artifact_path(&metadata.mod_id)))
                .await
            {
                metadata.size_bytes = Some(head.size as u64);
            }
        }
        if metadata.download_url.is_none() {
            metadata.download_url = Some(format!("/v1/mods/{}/download", metadata.mod_id));
        }

        Ok(metadata)
    }
}

fn normalize_prefix(prefix: Option<&str>) -> Option<ObjectPath> {
    prefix
        .map(str::trim)
        .map(|value| value.trim_matches('/'))
        .filter(|value| !value.is_empty())
        .map(ObjectPath::from)
}

fn is_safe_segment(value: &str) -> bool {
    !value.is_empty() && !value.contains('/') && !value.contains('\\') && !value.contains("..")
}

fn slugify(name: &str) -> String {
    use unicode_normalization::UnicodeNormalization;
    let mut out = String::new();
    let mut pending_dash = false;
    // NFKD decomposes accented letters into base + combining mark (Č -> C + caron), so the
    // base survives the ascii filter and the mark is dropped — i.e. transliterate ČSLA -> csla.
    for ch in name.nfkd() {
        if ch.is_ascii_alphanumeric() {
            if pending_dash && !out.is_empty() {
                out.push('-');
            }
            pending_dash = false;
            out.push(ch.to_ascii_lowercase());
        } else if ('\u{0300}'..='\u{036f}').contains(&ch) {
            // Combining diacritic from NFKD — skip it without breaking the word.
        } else {
            pending_dash = true;
        }
    }
    out
}

fn sanitize_version(value: &str) -> Option<String> {
    let value = value.trim();
    if value.is_empty() || value.len() > 64 {
        return None;
    }
    value
        .chars()
        .all(|c| c.is_ascii_alphanumeric() || matches!(c, '.' | '_' | '-'))
        .then(|| value.to_string())
}

fn content_hash8(bytes: &[u8]) -> String {
    let digest = Sha256::digest(bytes);
    digest.iter().take(4).map(|b| format!("{b:02x}")).collect()
}

fn hex(bytes: &[u8]) -> String {
    bytes.iter().map(|b| format!("{b:02x}")).collect()
}

fn apply_mod_filters(mods: &mut Vec<ModCatalogEntry>, query: &ListModsQuery) {
    let text_filter = query.q.as_ref().map(|value| value.to_lowercase());

    mods.retain_mut(|entry| {
        entry.compatible = is_mod_compatible(entry, query);
        if (query.app_name.is_some() || query.actver.is_some() || query.version_tag.is_some())
            && !entry.compatible
        {
            return false;
        }

        if let Some(filter) = &text_filter {
            let haystacks = [
                entry.mod_id.to_lowercase(),
                entry.name.to_lowercase(),
                entry.description.to_lowercase(),
            ];
            if !haystacks.iter().any(|value| value.contains(filter)) {
                return false;
            }
        }

        true
    });

    if let Some(limit) = query.limit {
        mods.truncate(limit);
    }
}

fn is_mod_compatible(entry: &ModCatalogEntry, query: &ListModsQuery) -> bool {
    if let Some(app_name) = query.app_name.as_deref() {
        if entry.app_name.as_deref() != Some(app_name) {
            return false;
        }
    }
    if let Some(actver) = query.actver {
        if entry.actver != Some(actver) {
            return false;
        }
    }
    if let Some(version_tag) = query
        .version_tag
        .as_deref()
        .filter(|value| !value.is_empty())
    {
        if let Some(entry_tag) = entry
            .version_tag
            .as_deref()
            .filter(|value| !value.is_empty())
        {
            if entry_tag != version_tag {
                return false;
            }
        }
    }
    query.app_name.is_some() || query.actver.is_some() || query.version_tag.is_some()
}

#[cfg(test)]
mod tests {
    use super::{ModStore, ModUploadMeta};
    use crate::model::ListModsQuery;
    use tempfile::tempdir;

    fn streamed_meta(name: &str, version: &str) -> ModUploadMeta {
        ModUploadMeta {
            name: name.to_string(),
            version: Some(version.to_string()),
            ..Default::default()
        }
    }

    #[tokio::test]
    async fn scan_fills_missing_size_and_download_url_from_disk() {
        let root = tempdir().unwrap();
        let mod_dir = root.path().join("legacy-mod");
        std::fs::create_dir_all(&mod_dir).unwrap();
        // A hand-written / pre-sizeBytes manifest: no sizeBytes, no downloadUrl.
        std::fs::write(
            mod_dir.join("mod.json"),
            r#"{"modId":"legacy-mod","name":"Legacy","version":"1.0","description":"old"}"#,
        )
        .unwrap();
        std::fs::write(mod_dir.join("legacy-mod.pbo.zst"), vec![0u8; 4096]).unwrap();

        let store = ModStore::local(root.path()).unwrap();
        let mods = store.list_mods(&ListModsQuery::default()).await.unwrap();
        assert_eq!(mods.len(), 1);
        // Size comes from the artifact in the store; the link is the (relative) download route.
        assert_eq!(mods[0].size_bytes, Some(4096));
        assert_eq!(
            mods[0].download_url.as_deref(),
            Some("/v1/mods/legacy-mod/download")
        );
    }

    #[tokio::test]
    async fn publish_then_get_and_download_roundtrips() {
        let root = tempdir().unwrap();
        let store = ModStore::local(root.path()).unwrap();
        let entry = store
            .publish_mod(&super::PublishModInput {
                name: "Synthetic Core Pack".to_string(),
                app_name: Some("CWR".to_string()),
                actver: Some(302),
                version_tag: Some("rc1".to_string()),
                version: Some("1.0".to_string()),
                description: Some("demo".to_string()),
                authors: vec!["bis".to_string()],
                homepage_url: None,
                file: vec![1, 2, 3, 4, 5],
            })
            .await
            .unwrap();
        assert_eq!(entry.mod_id, "synthetic-core-pack-1.0");
        assert_eq!(entry.app_name.as_deref(), Some("CWR"));
        assert_eq!(entry.actver, Some(302));
        assert_eq!(entry.version_tag.as_deref(), Some("rc1"));

        let fetched = store
            .get_mod("synthetic-core-pack-1.0")
            .await
            .unwrap()
            .unwrap();
        assert_eq!(fetched.name, "Synthetic Core Pack");

        // The artifact arrives already compressed; the service stores and serves it verbatim,
        // and size_bytes is exactly that uploaded (download) size.
        let stored = store
            .artifact_bytes("synthetic-core-pack-1.0")
            .await
            .unwrap();
        assert_eq!(stored.as_deref(), Some([1, 2, 3, 4, 5].as_slice()));
        assert_eq!(entry.size_bytes, Some(5));

        assert!(store.artifact_bytes("missing").await.unwrap().is_none());
    }

    #[tokio::test]
    async fn streamed_upload_stores_chunks_verbatim() {
        // The streamed path (begin_upload -> write chunks -> finalize_mod) writes the already-
        // compressed artifact verbatim. Drive it with a multi-chunk payload and assert the stored
        // artifact is the exact concatenation, sized to the total. Broken-state delta: dropping or
        // re-encoding a chunk would change the stored bytes or the size.
        let root = tempdir().unwrap();
        let store = ModStore::local(root.path()).unwrap();

        let payload: Vec<u8> = (0..512 * 1024).map(|i| (i * 31 + 7) as u8).collect();
        let mut upload = store.begin_upload().await.unwrap();
        for chunk in payload.chunks(60 * 1024) {
            upload.write(chunk);
        }
        let entry = store
            .finalize_mod(upload, streamed_meta("Streamed Mod", "1.0"))
            .await
            .unwrap();
        assert_eq!(entry.mod_id, "streamed-mod-1.0");

        let stored = store
            .artifact_bytes("streamed-mod-1.0")
            .await
            .unwrap()
            .unwrap();
        assert_eq!(stored, payload);
        assert_eq!(entry.size_bytes, Some(payload.len() as u64));
    }

    #[tokio::test]
    async fn streamed_empty_upload_is_rejected() {
        // A streamed upload with no bytes must be rejected, not stored as a 0-byte artifact.
        let root = tempdir().unwrap();
        let store = ModStore::local(root.path()).unwrap();

        let upload = store.begin_upload().await.unwrap(); // no writes
        let error = store
            .finalize_mod(upload, streamed_meta("Empty Mod", "1.0"))
            .await
            .unwrap_err();
        assert!(error.to_string().contains("empty"), "got: {error}");
        assert!(store.get_mod("empty-mod-1.0").await.unwrap().is_none());
    }

    #[test]
    fn slugify_transliterates_diacritics() {
        assert_eq!(super::slugify("Žlutý Modul"), "zluty-modul");
        assert_eq!(super::slugify("Synthetic Core Pack"), "synthetic-core-pack");
        assert_eq!(super::slugify("Žluťoučký Kůň"), "zlutoucky-kun");
        assert_eq!(super::slugify("@Fixture Mod!"), "fixture-mod");
    }

    #[tokio::test]
    async fn delete_removes_mod_and_artifact_from_store() {
        let root = tempdir().unwrap();
        let store = ModStore::local(root.path()).unwrap();
        store
            .publish_mod(&super::PublishModInput {
                name: "Temp Mod".to_string(),
                app_name: None,
                actver: None,
                version_tag: None,
                version: Some("1.0".to_string()),
                description: None,
                authors: Vec::new(),
                homepage_url: None,
                file: vec![9, 9, 9],
            })
            .await
            .unwrap();
        assert!(store.get_mod("temp-mod-1.0").await.unwrap().is_some());

        // Delete returns true (existed) and removes both the entry and the artifact.
        assert!(store.delete_mod("temp-mod-1.0").await.unwrap());
        assert!(store.get_mod("temp-mod-1.0").await.unwrap().is_none());
        assert!(store
            .artifact_bytes("temp-mod-1.0")
            .await
            .unwrap()
            .is_none());
        assert!(store
            .list_mods(&ListModsQuery::default())
            .await
            .unwrap()
            .is_empty());

        // Deleting again, or a never-existing / unsafe id, returns false.
        assert!(!store.delete_mod("temp-mod-1.0").await.unwrap());
        assert!(!store.delete_mod("never-existed").await.unwrap());
        assert!(!store.delete_mod("../escape").await.unwrap());
    }
}
