use std::path::Path;
use std::sync::Arc;
use std::time::{Duration, SystemTime, UNIX_EPOCH};

use anyhow::{bail, Context, Result};
use bytes::Bytes;
use futures::StreamExt;
use object_store::aws::{AmazonS3, AmazonS3Builder};
use object_store::local::LocalFileSystem;
use object_store::path::Path as ObjectPath;
use object_store::signer::Signer;
use object_store::{ObjectStore, PutMode, PutOptions, UpdateVersion, WriteMultipart};
use reqwest::Method;
use serde::{Deserialize, Serialize};
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
const CURRENT_FILE: &str = "current";

#[derive(Deserialize, Serialize)]
struct CurrentRevision {
    #[serde(rename = "packageRevision")]
    package_revision: u64,
}

struct CurrentState {
    revision: u64,
    update: Option<UpdateVersion>,
}

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

/// Object-store-backed mod artifact store. Immutable artifacts and metadata live under
/// `<modId>/revisions/<revision>/`, with a conditional `current` object selecting the catalog
/// entry. The local filesystem backend serves development and S3 lets stateless replicas share
/// one store.
#[derive(Clone)]
pub struct ModStore {
    store: Arc<dyn ObjectStore>,
    signer: Option<Arc<dyn Signer>>,
    prefix: Option<ObjectPath>,
    signed_url_ttl: Duration,
    current_lock: Arc<tokio::sync::Mutex<()>>,
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
            current_lock: Arc::new(tokio::sync::Mutex::new(())),
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
            current_lock: Arc::new(tokio::sync::Mutex::new(())),
        })
    }

    fn artifact_path(mod_id: &str) -> ObjectPath {
        ObjectPath::from(format!("{mod_id}/{mod_id}.{MOD_ARTIFACT_EXT}"))
    }

    fn revision_artifact_path(mod_id: &str, revision: u64) -> ObjectPath {
        ObjectPath::from(format!(
            "{mod_id}/revisions/{revision}/{mod_id}.{MOD_ARTIFACT_EXT}"
        ))
    }

    fn metadata_path(mod_id: &str) -> ObjectPath {
        ObjectPath::from(format!("{mod_id}/{MOD_METADATA_FILE}"))
    }

    fn revision_metadata_path(mod_id: &str, revision: u64) -> ObjectPath {
        ObjectPath::from(format!("{mod_id}/revisions/{revision}/{MOD_METADATA_FILE}"))
    }

    fn current_path(mod_id: &str) -> ObjectPath {
        ObjectPath::from(format!("{mod_id}/{CURRENT_FILE}"))
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

    /// Store the first immutable package revision under a stable slug identity.
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
        if self.get_mod(&slug).await?.is_some() {
            bail!("mod id '{slug}' already exists");
        }

        let digest = Sha256::digest(&input.file);
        let sha256 = hex(&digest);
        let version = match input.version.as_deref() {
            Some(value) => sanitize_version(value)
                .ok_or_else(|| anyhow::anyhow!("invalid version '{value}'"))?,
            None => content_hash8(&input.file),
        };
        let size_bytes = input.file.len() as u64;
        let entry = ModCatalogEntry {
            mod_id: slug.clone(),
            app_name: input.app_name.clone(),
            actver: input.actver,
            version_tag: input.version_tag.clone(),
            compatible: false,
            name: input.name.clone(),
            version,
            package_revision: 1,
            sha256: Some(sha256),
            published_unix_ms: Some(now_unix_millis()),
            folder_name: None,
            description: input.description.clone().unwrap_or_default(),
            authors: input.authors.clone(),
            homepage_url: input.homepage_url.clone(),
            download_url: Some(format!("/v1/mods/{slug}/revisions/1/download")),
            size_bytes: Some(size_bytes),
        };
        self.write_new_revision(&entry, Bytes::from(input.file.clone()))
            .await?;
        self.create_current(&slug, 1).await?;
        Ok(entry)
    }

    async fn write_new_revision(&self, entry: &ModCatalogEntry, artifact: Bytes) -> Result<()> {
        let artifact_path = self.store_path(Self::revision_artifact_path(
            &entry.mod_id,
            entry.package_revision,
        ));
        self.store
            .put_opts(&artifact_path, artifact.into(), PutMode::Create.into())
            .await
            .with_context(|| {
                format!(
                    "reserving revision {} for {}",
                    entry.package_revision, entry.mod_id
                )
            })?;
        let metadata_path = self.store_path(Self::revision_metadata_path(
            &entry.mod_id,
            entry.package_revision,
        ));
        let json = serde_json::to_vec_pretty(entry)?;
        if let Err(error) = self
            .store
            .put_opts(
                &metadata_path,
                Bytes::from(json).into(),
                PutMode::Create.into(),
            )
            .await
        {
            let _ = self.store.delete(&artifact_path).await;
            return Err(error).with_context(|| {
                format!(
                    "writing revision {} metadata for {}",
                    entry.package_revision, entry.mod_id
                )
            });
        }
        Ok(())
    }

    async fn create_current(&self, mod_id: &str, revision: u64) -> Result<()> {
        let bytes = serde_json::to_vec(&CurrentRevision {
            package_revision: revision,
        })?;
        self.store
            .put_opts(
                &self.store_path(Self::current_path(mod_id)),
                Bytes::from(bytes).into(),
                PutMode::Create.into(),
            )
            .await
            .with_context(|| format!("creating current revision for {mod_id}"))?;
        Ok(())
    }

    async fn set_current(
        &self,
        mod_id: &str,
        revision: u64,
        update: Option<UpdateVersion>,
    ) -> Result<()> {
        let mode = update.map_or(PutMode::Create, PutMode::Update);
        let bytes = serde_json::to_vec(&CurrentRevision {
            package_revision: revision,
        })?;
        let result = self
            .store
            .put_opts(
                &self.store_path(Self::current_path(mod_id)),
                Bytes::from(bytes).into(),
                PutOptions {
                    mode,
                    ..Default::default()
                },
            )
            .await;
        if matches!(&result, Err(object_store::Error::NotImplemented)) {
            let _guard = self.current_lock.lock().await;
            if self
                .current_state(mod_id)
                .await?
                .is_some_and(|state| state.revision >= revision)
            {
                return Ok(());
            }
            let bytes = serde_json::to_vec(&CurrentRevision {
                package_revision: revision,
            })?;
            self.store
                .put(
                    &self.store_path(Self::current_path(mod_id)),
                    Bytes::from(bytes).into(),
                )
                .await
                .with_context(|| format!("advancing {mod_id} to revision {revision}"))?;
        } else {
            result.with_context(|| format!("advancing {mod_id} to revision {revision}"))?;
        }
        Ok(())
    }

    async fn current_state(&self, mod_id: &str) -> Result<Option<CurrentState>> {
        let path = self.store_path(Self::current_path(mod_id));
        match self.store.get(&path).await {
            Ok(result) => {
                let update = UpdateVersion {
                    e_tag: result.meta.e_tag.clone(),
                    version: result.meta.version.clone(),
                };
                let current: CurrentRevision = serde_json::from_slice(&result.bytes().await?)
                    .with_context(|| format!("parsing current revision for {mod_id}"))?;
                Ok(Some(CurrentState {
                    revision: current.package_revision,
                    update: Some(update),
                }))
            }
            Err(object_store::Error::NotFound { .. }) => {
                match self
                    .store
                    .head(&self.store_path(Self::metadata_path(mod_id)))
                    .await
                {
                    Ok(_) => Ok(Some(CurrentState {
                        revision: 1,
                        update: None,
                    })),
                    Err(object_store::Error::NotFound { .. }) => Ok(None),
                    Err(error) => Err(error.into()),
                }
            }
            Err(error) => Err(error.into()),
        }
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

    /// Finalize a streamed upload as revision 1 of a stable slug identity.
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

        if self.get_mod(&slug).await?.is_some() {
            let _ = self.store.delete(&temp).await;
            bail!("mod id '{slug}' already exists");
        }

        let digest = hasher.finalize();
        let version = match meta.version.as_deref() {
            Some(value) => match sanitize_version(value) {
                Some(version) => version,
                None => {
                    let _ = self.store.delete(&temp).await;
                    bail!("invalid version '{value}'");
                }
            },
            None => digest.iter().take(4).map(|b| format!("{b:02x}")).collect(),
        };

        let artifact_path = self.store_path(Self::revision_artifact_path(&slug, 1));
        self.store
            .rename_if_not_exists(&temp, &artifact_path)
            .await
            .with_context(|| format!("placing revision 1 artifact for {slug}"))?;

        let entry = ModCatalogEntry {
            mod_id: slug.clone(),
            app_name: meta.app_name,
            actver: meta.actver,
            version_tag: meta.version_tag,
            compatible: false,
            name: meta.name,
            version,
            package_revision: 1,
            sha256: Some(hex(&digest)),
            published_unix_ms: Some(now_unix_millis()),
            folder_name: meta
                .folder_name
                .map(|value| value.trim().to_string())
                .filter(|value| !value.is_empty()),
            description: meta.description.unwrap_or_default(),
            authors: meta.authors,
            homepage_url: meta.homepage_url,
            download_url: Some(format!("/v1/mods/{slug}/revisions/1/download")),
            size_bytes: Some(size),
        };
        let json = serde_json::to_vec_pretty(&entry)?;
        self.store
            .put_opts(
                &self.store_path(Self::revision_metadata_path(&slug, 1)),
                Bytes::from(json).into(),
                PutMode::Create.into(),
            )
            .await
            .with_context(|| format!("writing revision 1 metadata for {slug}"))?;
        self.create_current(&slug, 1).await?;
        Ok(entry)
    }

    pub async fn add_revision(
        &self,
        mod_id: &str,
        bytes: Vec<u8>,
        meta: ModUploadMeta,
    ) -> Result<ModCatalogEntry> {
        let mut upload = self.begin_upload().await?;
        upload.write(&bytes);
        self.finalize_revision(upload, mod_id, meta).await
    }

    pub async fn finalize_revision(
        &self,
        upload: ArtifactUpload,
        mod_id: &str,
        meta: ModUploadMeta,
    ) -> Result<ModCatalogEntry> {
        if !is_safe_segment(mod_id) {
            bail!("invalid mod id '{mod_id}'");
        }
        let ArtifactUpload {
            temp,
            writer,
            hasher,
            size,
        } = upload;
        writer
            .finish()
            .await
            .with_context(|| format!("finishing upload {temp}"))?;
        if size == 0 {
            let _ = self.store.delete(&temp).await;
            bail!("mod artifact is empty");
        }
        let digest = hasher.finalize();
        let sha256 = hex(&digest);
        let Some(mut current) = self.current_state(mod_id).await? else {
            let _ = self.store.delete(&temp).await;
            bail!("mod id '{mod_id}' does not exist");
        };
        let latest = self
            .get_revision(mod_id, current.revision)
            .await?
            .ok_or_else(|| anyhow::anyhow!("current revision for '{mod_id}' is missing"))?;
        if latest.sha256.as_deref() == Some(sha256.as_str()) {
            let _ = self.store.delete(&temp).await;
            return Ok(latest);
        }

        let version = match meta.version.as_deref() {
            Some(value) => sanitize_version(value)
                .ok_or_else(|| anyhow::anyhow!("invalid version '{value}'"))?,
            None => latest.version.clone(),
        };
        let mut revision = current.revision + 1;
        loop {
            let artifact_path = self.store_path(Self::revision_artifact_path(mod_id, revision));
            match self.store.rename_if_not_exists(&temp, &artifact_path).await {
                Ok(()) => break,
                Err(object_store::Error::AlreadyExists { .. }) => revision += 1,
                Err(error) => return Err(error.into()),
            }
        }

        let entry = ModCatalogEntry {
            mod_id: mod_id.to_string(),
            app_name: meta.app_name.or(latest.app_name),
            actver: meta.actver.or(latest.actver),
            version_tag: meta.version_tag.or(latest.version_tag),
            compatible: false,
            name: if meta.name.trim().is_empty() {
                latest.name
            } else {
                meta.name
            },
            version,
            package_revision: revision,
            sha256: Some(sha256),
            published_unix_ms: Some(now_unix_millis()),
            folder_name: meta.folder_name.or(latest.folder_name),
            description: meta.description.unwrap_or(latest.description),
            authors: if meta.authors.is_empty() {
                latest.authors
            } else {
                meta.authors
            },
            homepage_url: meta.homepage_url.or(latest.homepage_url),
            download_url: Some(format!("/v1/mods/{mod_id}/revisions/{revision}/download")),
            size_bytes: Some(size),
        };
        let metadata_path = self.store_path(Self::revision_metadata_path(mod_id, revision));
        self.store
            .put_opts(
                &metadata_path,
                Bytes::from(serde_json::to_vec_pretty(&entry)?).into(),
                PutMode::Create.into(),
            )
            .await
            .with_context(|| format!("writing revision {revision} metadata for {mod_id}"))?;
        if self
            .set_current(mod_id, revision, current.update.take())
            .await
            .is_err()
        {
            let state = self
                .current_state(mod_id)
                .await?
                .ok_or_else(|| anyhow::anyhow!("current revision for '{mod_id}' disappeared"))?;
            if state.revision < revision {
                self.set_current(mod_id, revision, state.update).await?;
            }
        }
        Ok(entry)
    }

    /// Stream a published mod's artifact: `(size_bytes, byte-stream)`, or `None` if absent.
    pub async fn artifact_stream(&self, mod_id: &str) -> Result<Option<(u64, ArtifactStream)>> {
        let Some(current) = self.current_state(mod_id).await? else {
            return Ok(None);
        };
        self.revision_stream(mod_id, current.revision).await
    }

    pub async fn revision_stream(
        &self,
        mod_id: &str,
        revision: u64,
    ) -> Result<Option<(u64, ArtifactStream)>> {
        if !is_safe_segment(mod_id) {
            return Ok(None);
        }
        let revision_path = self.store_path(Self::revision_artifact_path(mod_id, revision));
        let path = if revision == 1 && self.store.head(&revision_path).await.is_err() {
            self.store_path(Self::artifact_path(mod_id))
        } else {
            revision_path
        };
        match self.store.get(&path).await {
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
        let Some(current) = self.current_state(mod_id).await? else {
            return Ok(None);
        };
        self.signed_revision_url(mod_id, current.revision).await
    }

    pub async fn signed_revision_url(
        &self,
        mod_id: &str,
        revision: u64,
    ) -> Result<Option<(u64, String)>> {
        if !is_safe_segment(mod_id) {
            return Ok(None);
        }
        let Some(signer) = &self.signer else {
            return Ok(None);
        };
        let revision_path = self.store_path(Self::revision_artifact_path(mod_id, revision));
        let path = if revision == 1 && self.store.head(&revision_path).await.is_err() {
            self.store_path(Self::artifact_path(mod_id))
        } else {
            revision_path
        };
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
        let Some(current) = self.current_state(mod_id).await? else {
            return Ok(None);
        };
        self.revision_bytes(mod_id, current.revision).await
    }

    pub async fn revision_bytes(&self, mod_id: &str, revision: u64) -> Result<Option<Vec<u8>>> {
        if !is_safe_segment(mod_id) {
            return Ok(None);
        }
        let revision_path = self.store_path(Self::revision_artifact_path(mod_id, revision));
        let path = if revision == 1 && self.store.head(&revision_path).await.is_err() {
            self.store_path(Self::artifact_path(mod_id))
        } else {
            revision_path
        };
        match self.store.get(&path).await {
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
        if self.current_state(mod_id).await?.is_none() {
            return Ok(false);
        }
        let prefix = self.store_path(ObjectPath::from(format!("{mod_id}/")));
        let mut objects = self.store.list(Some(&prefix));
        let mut paths = Vec::new();
        while let Some(object) = objects.next().await {
            paths.push(object?.location);
        }
        for path in paths {
            self.store.delete(&path).await?;
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
        let Some(current) = self.current_state(mod_id).await? else {
            return Ok(None);
        };
        self.get_revision(mod_id, current.revision).await
    }

    pub async fn get_revision(
        &self,
        mod_id: &str,
        revision: u64,
    ) -> Result<Option<ModCatalogEntry>> {
        if !is_safe_segment(mod_id) || revision == 0 {
            return Ok(None);
        }
        let revision_path = self.store_path(Self::revision_metadata_path(mod_id, revision));
        match self.store.get(&revision_path).await {
            Ok(result) => {
                let bytes = result.bytes().await?;
                return Ok(Some(
                    self.parse_metadata(mod_id, revision, &bytes, false).await?,
                ));
            }
            Err(object_store::Error::NotFound { .. }) if revision == 1 => {}
            Err(object_store::Error::NotFound { .. }) => return Ok(None),
            Err(error) => return Err(error.into()),
        }
        match self
            .store
            .get(&self.store_path(Self::metadata_path(mod_id)))
            .await
        {
            Ok(result) => {
                let bytes = result.bytes().await?;
                Ok(Some(self.parse_metadata(mod_id, 1, &bytes, true).await?))
            }
            Err(object_store::Error::NotFound { .. }) => Ok(None),
            Err(error) => Err(error.into()),
        }
    }

    pub async fn list_revisions(&self, mod_id: &str) -> Result<Vec<ModCatalogEntry>> {
        let Some(current) = self.current_state(mod_id).await? else {
            return Ok(Vec::new());
        };
        let mut revisions = Vec::new();
        for revision in (1..=current.revision).rev() {
            if let Some(entry) = self.get_revision(mod_id, revision).await? {
                revisions.push(entry);
            }
        }
        Ok(revisions)
    }

    async fn read_all(&self) -> Result<Vec<ModCatalogEntry>> {
        let mut mod_ids = std::collections::BTreeSet::new();
        let mut listing = self.store.list(self.list_prefix());
        while let Some(meta) = listing.next().await {
            let location = meta?.location;
            if location.filename() == Some(MOD_METADATA_FILE)
                || location.filename() == Some(CURRENT_FILE)
            {
                let relative = self.strip_prefix(&location);
                if let Some((mod_id, _)) = relative.split_once('/') {
                    mod_ids.insert(mod_id.to_string());
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

    async fn parse_metadata(
        &self,
        dir_id: &str,
        revision: u64,
        bytes: &[u8],
        legacy: bool,
    ) -> Result<ModCatalogEntry> {
        let mut metadata: ModCatalogEntry = serde_json::from_slice(bytes)
            .with_context(|| format!("parsing mod.json for {dir_id}"))?;

        if metadata.mod_id.is_empty() {
            metadata.mod_id = dir_id.to_string();
        }
        metadata.package_revision = revision;

        let artifact_path = if legacy {
            Self::artifact_path(&metadata.mod_id)
        } else {
            Self::revision_artifact_path(&metadata.mod_id, revision)
        };
        if metadata.size_bytes.unwrap_or(0) == 0 {
            if let Ok(head) = self
                .store
                .head(&self.store_path(artifact_path.clone()))
                .await
            {
                metadata.size_bytes = Some(head.size as u64);
            }
        }
        if metadata.sha256.is_none() {
            if let Ok(result) = self.store.get(&self.store_path(artifact_path)).await {
                metadata.sha256 = Some(hex(&Sha256::digest(&result.bytes().await?)));
            }
        }
        if metadata.download_url.is_none() {
            metadata.download_url = Some(format!(
                "/v1/mods/{}/revisions/{revision}/download",
                metadata.mod_id
            ));
        }

        Ok(metadata)
    }
}

fn now_unix_millis() -> i64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_millis()
        .try_into()
        .unwrap_or(i64::MAX)
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
            Some("/v1/mods/legacy-mod/revisions/1/download")
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
        assert_eq!(entry.mod_id, "synthetic-core-pack");
        assert_eq!(entry.app_name.as_deref(), Some("CWR"));
        assert_eq!(entry.actver, Some(302));
        assert_eq!(entry.version_tag.as_deref(), Some("rc1"));

        let fetched = store.get_mod("synthetic-core-pack").await.unwrap().unwrap();
        assert_eq!(fetched.name, "Synthetic Core Pack");

        // The artifact arrives already compressed; the service stores and serves it verbatim,
        // and size_bytes is exactly that uploaded (download) size.
        let stored = store.artifact_bytes("synthetic-core-pack").await.unwrap();
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
        assert_eq!(entry.mod_id, "streamed-mod");

        let stored = store.artifact_bytes("streamed-mod").await.unwrap().unwrap();
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
        assert!(store.get_mod("temp-mod").await.unwrap().is_some());
        store
            .add_revision("temp-mod", vec![8, 8, 8], super::ModUploadMeta::default())
            .await
            .unwrap();

        // Delete returns true (existed) and removes both the entry and the artifact.
        assert!(store.delete_mod("temp-mod").await.unwrap());
        assert!(store.get_mod("temp-mod").await.unwrap().is_none());
        assert!(store.artifact_bytes("temp-mod").await.unwrap().is_none());
        assert!(store.revision_bytes("temp-mod", 1).await.unwrap().is_none());
        assert!(store.revision_bytes("temp-mod", 2).await.unwrap().is_none());
        assert!(store
            .list_mods(&ListModsQuery::default())
            .await
            .unwrap()
            .is_empty());

        // Deleting again, or a never-existing / unsafe id, returns false.
        assert!(!store.delete_mod("temp-mod").await.unwrap());
        assert!(!store.delete_mod("never-existed").await.unwrap());
        assert!(!store.delete_mod("../escape").await.unwrap());
    }

    #[tokio::test]
    async fn stable_id_keeps_immutable_revision_history() {
        let root = tempdir().unwrap();
        let store = ModStore::local(root.path()).unwrap();
        let first = store
            .publish_mod(&super::PublishModInput {
                name: "Stable Mod".to_string(),
                app_name: Some("CWR".to_string()),
                actver: Some(302),
                version_tag: Some("rc1".to_string()),
                version: Some("1.0".to_string()),
                description: Some("first".to_string()),
                authors: vec!["Author".to_string()],
                homepage_url: Some("https://example.invalid/stable".to_string()),
                file: vec![1, 2, 3],
            })
            .await
            .unwrap();
        assert_eq!(first.mod_id, "stable-mod");
        assert_eq!(first.package_revision, 1);

        let second = store
            .add_revision("stable-mod", vec![4, 5, 6], super::ModUploadMeta::default())
            .await
            .unwrap();
        assert_eq!(second.mod_id, "stable-mod");
        assert_eq!(second.package_revision, 2);
        assert_eq!(second.name, "Stable Mod");
        assert_eq!(second.version, "1.0");
        assert_eq!(second.description, "first");

        let latest = store.get_mod("stable-mod").await.unwrap().unwrap();
        assert_eq!(latest.package_revision, 2);
        assert_eq!(
            store
                .revision_bytes("stable-mod", 1)
                .await
                .unwrap()
                .unwrap(),
            vec![1, 2, 3]
        );
        assert_eq!(
            store
                .revision_bytes("stable-mod", 2)
                .await
                .unwrap()
                .unwrap(),
            vec![4, 5, 6]
        );
        let history = store.list_revisions("stable-mod").await.unwrap();
        assert_eq!(
            history
                .iter()
                .map(|entry| entry.package_revision)
                .collect::<Vec<_>>(),
            vec![2, 1]
        );

        let restored = store
            .add_revision("stable-mod", vec![1, 2, 3], super::ModUploadMeta::default())
            .await
            .unwrap();
        assert_eq!(restored.package_revision, 3);
        assert_eq!(
            store
                .get_mod("stable-mod")
                .await
                .unwrap()
                .unwrap()
                .package_revision,
            3
        );
    }

    #[tokio::test]
    async fn identical_current_upload_is_idempotent() {
        let root = tempdir().unwrap();
        let store = ModStore::local(root.path()).unwrap();
        store
            .publish_mod(&super::PublishModInput {
                name: "Idempotent Mod".to_string(),
                app_name: None,
                actver: None,
                version_tag: None,
                version: Some("1.0".to_string()),
                description: None,
                authors: Vec::new(),
                homepage_url: None,
                file: vec![7, 8, 9],
            })
            .await
            .unwrap();

        let repeated = store
            .add_revision(
                "idempotent-mod",
                vec![7, 8, 9],
                super::ModUploadMeta::default(),
            )
            .await
            .unwrap();
        assert_eq!(repeated.package_revision, 1);
        assert_eq!(
            store.list_revisions("idempotent-mod").await.unwrap().len(),
            1
        );
    }

    #[tokio::test]
    async fn concurrent_updates_allocate_distinct_revisions_without_regressing_latest() {
        let root = tempdir().unwrap();
        let store = ModStore::local(root.path()).unwrap();
        store
            .publish_mod(&super::PublishModInput {
                name: "Concurrent Mod".to_string(),
                app_name: None,
                actver: None,
                version_tag: None,
                version: Some("1.0".to_string()),
                description: None,
                authors: Vec::new(),
                homepage_url: None,
                file: vec![1],
            })
            .await
            .unwrap();

        let left = store.clone();
        let right = store.clone();
        let (left, right) = tokio::join!(
            left.add_revision("concurrent-mod", vec![2], super::ModUploadMeta::default()),
            right.add_revision("concurrent-mod", vec![3], super::ModUploadMeta::default())
        );
        let mut allocated = vec![
            left.unwrap().package_revision,
            right.unwrap().package_revision,
        ];
        allocated.sort_unstable();
        assert_eq!(allocated, vec![2, 3]);
        assert_eq!(
            store
                .get_mod("concurrent-mod")
                .await
                .unwrap()
                .unwrap()
                .package_revision,
            3
        );
    }
}
