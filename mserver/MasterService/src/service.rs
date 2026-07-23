use std::path::PathBuf;
use std::sync::Arc;

use anyhow::Result;
use sha2::Digest;

use crate::model::{
    DirectoryServerRecord, ListModsQuery, ListServersQuery, ModCatalogEntry, ModUsageServer,
    RegisterServerRequest, ServerDetail, ServerModReference, ServerPlayer, ServerVersionGroup,
    ServiceMetadata, ServiceSummary,
};
use crate::mods::{ArtifactStream, ArtifactUpload, ModStore, ModUploadMeta};
use crate::repository::SqliteServerDirectory;

pub const SERVICE_NAME: &str = "papa-bear";
pub const PRODUCT_NAME: &str = "PAPA BEAR";
pub const API_VERSION: &str = "v1";
pub const OPENAPI_URL: &str = "/openapi/v1.yaml";
pub const MODS_URL: &str = "/v1/mods";

pub struct PapaBearService {
    directory: Arc<SqliteServerDirectory>,
    mod_store: Option<Arc<ModStore>>,
}

impl PapaBearService {
    pub const fn new(directory: Arc<SqliteServerDirectory>) -> Self {
        Self {
            directory,
            mod_store: None,
        }
    }

    pub fn with_mod_store(
        directory: Arc<SqliteServerDirectory>,
        mod_store: Option<ModStore>,
    ) -> Self {
        Self {
            directory,
            mod_store: mod_store.map(Arc::new),
        }
    }

    /// Convenience used by tests and the local-dev path: a local-filesystem mod store rooted
    /// at `mods_root`.
    pub fn with_mods_root(
        directory: Arc<SqliteServerDirectory>,
        mods_root: Option<PathBuf>,
    ) -> Self {
        let mod_store = mods_root.map(|root| ModStore::local(&root).expect("local mod store"));
        Self::with_mod_store(directory, mod_store)
    }

    pub async fn register(
        &self,
        request: RegisterServerRequest,
        now_unix_ms: i64,
    ) -> Result<DirectoryServerRecord> {
        self.directory.register(request, now_unix_ms).await
    }

    pub async fn observe(
        &self,
        server_id: &str,
        observed_unix_ms: i64,
        reachable: bool,
    ) -> Result<Option<DirectoryServerRecord>> {
        self.directory
            .observe(server_id, observed_unix_ms, reachable)
            .await
    }

    pub async fn list(&self, query: &ListServersQuery) -> Result<Vec<DirectoryServerRecord>> {
        self.directory.list(query, now_unix_millis()).await
    }

    pub async fn version_groups(
        &self,
        query: &ListServersQuery,
    ) -> Result<Vec<ServerVersionGroup>> {
        self.directory
            .version_groups(query, now_unix_millis())
            .await
    }

    pub async fn get(&self, server_id: &str) -> Result<Option<DirectoryServerRecord>> {
        self.directory.get(server_id).await
    }

    pub async fn set_token_hash(&self, server_id: &str, token_hash: &str) -> Result<()> {
        self.directory.set_token_hash(server_id, token_hash).await
    }

    pub async fn server_token_hash(&self, server_id: &str) -> Result<Option<String>> {
        self.directory.server_token_hash(server_id).await
    }

    pub async fn get_server_detail(&self, server_id: &str) -> Result<Option<ServerDetail>> {
        let Some(server) = self.get(server_id).await? else {
            return Ok(None);
        };

        Ok(Some(ServerDetail {
            players: server_players(&server),
            mods: self.resolve_server_mods(&server.mod_list).await?,
            player_history: self.directory.history(server_id, 12).await?,
            recent_sessions: self.directory.sessions(server_id, 3).await?,
            server,
        }))
    }

    pub async fn unregister(&self, server_id: &str) -> Result<bool> {
        self.directory.unregister(server_id).await
    }

    pub async fn prune_stale(&self, min_last_seen_unix_ms: i64) -> Result<usize> {
        self.directory.prune_stale(min_last_seen_unix_ms).await
    }

    pub async fn list_mods(&self, query: &ListModsQuery) -> Result<Vec<ModCatalogEntry>> {
        match &self.mod_store {
            Some(store) => store.list_mods(query).await,
            None => Ok(Vec::new()),
        }
    }

    pub async fn get_mod(&self, mod_id: &str) -> Result<Option<ModCatalogEntry>> {
        match &self.mod_store {
            Some(store) => store.get_mod(mod_id).await,
            None => Ok(None),
        }
    }

    pub const fn mods_enabled(&self) -> bool {
        self.mod_store.is_some()
    }

    /// Begin a streamed mod upload (`None` if mods are disabled). The handler writes the
    /// uploaded body to it chunk-by-chunk, then calls `finalize_mod_upload`.
    pub async fn begin_mod_upload(&self) -> Result<Option<ArtifactUpload>> {
        match &self.mod_store {
            Some(store) => Ok(Some(store.begin_upload().await?)),
            None => Ok(None),
        }
    }

    pub async fn finalize_mod_upload(
        &self,
        upload: ArtifactUpload,
        meta: ModUploadMeta,
    ) -> Result<ModCatalogEntry> {
        let store = self
            .mod_store
            .as_ref()
            .ok_or_else(|| anyhow::anyhow!("mods store is not configured"))?;
        store.finalize_mod(upload, meta).await
    }

    /// Delete a mod (its catalog entry + artifact) from the store. Returns `true` if it
    /// existed, `false` if there was nothing to delete or mods are disabled.
    pub async fn delete_mod(&self, mod_id: &str) -> Result<bool> {
        match &self.mod_store {
            Some(store) => store.delete_mod(mod_id).await,
            None => Ok(false),
        }
    }

    /// Stream a published mod's artifact: `(size_bytes, byte-stream)`, or `None` if mods are
    /// disabled / absent.
    pub async fn mod_artifact_stream(&self, mod_id: &str) -> Result<Option<(u64, ArtifactStream)>> {
        match &self.mod_store {
            Some(store) => store.artifact_stream(mod_id).await,
            None => Ok(None),
        }
    }

    /// Signed direct artifact URL: `(size_bytes, url)`, or `None` when the store cannot sign
    /// externally reachable URLs (local/dev/MinIO streaming compatibility) or the mod is absent.
    pub async fn mod_artifact_signed_url(&self, mod_id: &str) -> Result<Option<(u64, String)>> {
        match &self.mod_store {
            Some(store) => store.signed_artifact_url(mod_id).await,
            None => Ok(None),
        }
    }

    pub async fn mod_versions(&self, mod_id: &str) -> Result<Vec<ModCatalogEntry>> {
        let Some(current) = self.get_mod(mod_id).await? else {
            return Ok(Vec::new());
        };

        let family_key = mod_family_key(&current);
        let mut versions = self
            .list_mods(&ListModsQuery::default())
            .await?
            .into_iter()
            .filter(|entry| mod_family_key(entry) == family_key)
            .collect::<Vec<_>>();

        versions.sort_by(|lhs, rhs| {
            (lhs.mod_id != current.mod_id)
                .cmp(&(rhs.mod_id != current.mod_id))
                .then_with(|| rhs.version.cmp(&lhs.version))
                .then_with(|| lhs.mod_id.cmp(&rhs.mod_id))
        });
        Ok(versions)
    }

    pub async fn mod_usage_servers(&self, mod_id: &str) -> Result<Vec<ModUsageServer>> {
        let mut servers = self
            .directory
            .list(&ListServersQuery::default(), now_unix_millis())
            .await?
            .into_iter()
            .filter(|server| {
                split_server_mods(&server.mod_list)
                    .iter()
                    .any(|candidate| candidate == mod_id)
            })
            .map(|server| ModUsageServer {
                server_id: server.server_id,
                hostname: server.hostname,
                gametype: server.gametype,
                players: server.numplayers,
                max_players: server.maxplayers,
                password: server.password,
            })
            .collect::<Vec<_>>();

        servers.sort_by(|lhs, rhs| {
            rhs.players
                .cmp(&lhs.players)
                .then_with(|| lhs.hostname.cmp(&rhs.hostname))
        });
        Ok(servers)
    }

    pub fn metadata(&self) -> ServiceMetadata {
        ServiceMetadata {
            service_name: SERVICE_NAME.to_string(),
            product_name: PRODUCT_NAME.to_string(),
            api_version: API_VERSION.to_string(),
            openapi_url: OPENAPI_URL.to_string(),
            mods_url: MODS_URL.to_string(),
        }
    }

    pub async fn summary(&self) -> Result<ServiceSummary> {
        // public_verified counts only verified rows, independent of the additive list default.
        let public_servers = self
            .list(&ListServersQuery {
                verification_state: Some(crate::model::VerificationState::Verified),
                ..Default::default()
            })
            .await?;
        let all_servers = self
            .list(&ListServersQuery {
                include_unverified_servers: Some(true),
                ..Default::default()
            })
            .await?;
        let public_players = public_servers.iter().map(|server| server.numplayers).sum();
        let self_reported_servers = all_servers
            .iter()
            .filter(|server| {
                matches!(
                    server.verification_state,
                    crate::model::VerificationState::SelfReported
                )
            })
            .count();
        let unreachable_servers = all_servers
            .iter()
            .filter(|server| {
                matches!(
                    server.verification_state,
                    crate::model::VerificationState::Unreachable
                )
            })
            .count();
        let mod_count = self.list_mods(&ListModsQuery::default()).await?.len();
        Ok(ServiceSummary {
            public_verified_servers: public_servers.len(),
            public_players,
            self_reported_servers,
            unreachable_servers,
            mod_count,
        })
    }

    async fn resolve_server_mods(&self, mod_list: &str) -> Result<Vec<ServerModReference>> {
        let mut resolved = Vec::new();
        for mod_id in split_server_mods(mod_list) {
            if let Some(entry) = self.get_mod(&mod_id).await? {
                resolved.push(ServerModReference {
                    mod_id: entry.mod_id,
                    name: entry.name,
                    known: true,
                    version: Some(entry.version),
                    description: Some(entry.description),
                    homepage_url: entry.homepage_url,
                    download_url: entry.download_url,
                });
            } else {
                resolved.push(ServerModReference {
                    mod_id: mod_id.clone(),
                    name: mod_id,
                    known: false,
                    version: None,
                    description: None,
                    homepage_url: None,
                    download_url: None,
                });
            }
        }

        Ok(resolved)
    }
}

fn split_server_mods(mod_list: &str) -> Vec<String> {
    let mut mod_ids = Vec::new();
    for mod_id in mod_list
        .split(|ch: char| ch == ',' || ch == ';' || ch == '|' || ch.is_whitespace())
        .map(str::trim)
        .filter(|mod_id| !mod_id.is_empty())
    {
        if !mod_ids.iter().any(|existing| existing == mod_id) {
            mod_ids.push(mod_id.to_string());
        }
    }

    mod_ids
}

fn mod_family_key(entry: &ModCatalogEntry) -> String {
    let homepage = entry
        .homepage_url
        .as_deref()
        .map(str::trim)
        .filter(|value| !value.is_empty())
        .map(str::to_lowercase);
    if let Some(homepage) = homepage {
        return homepage;
    }

    entry
        .name
        .chars()
        .filter(char::is_ascii_alphanumeric)
        .flat_map(char::to_lowercase)
        .collect()
}

fn server_players(server: &DirectoryServerRecord) -> Vec<ServerPlayer> {
    let player_count = usize::try_from(server.numplayers.max(0)).unwrap_or(0);
    let seeded_names = seeded_player_names(&server.server_id);

    (0..player_count)
        .map(|index| ServerPlayer {
            name: seeded_names.get(index).map_or_else(
                || format!("{} {}", player_role(index), index + 1),
                |name| (*name).to_string(),
            ),
            role: player_role(index).to_string(),
        })
        .collect()
}

const fn player_role(index: usize) -> &'static str {
    const ROLES: [&str; 8] = [
        "Rifleman",
        "Medic",
        "AT",
        "Grenadier",
        "Engineer",
        "Pilot",
        "Marksman",
        "Squad Lead",
    ];
    ROLES[index % ROLES.len()]
}

fn seeded_player_names(server_id: &str) -> &'static [&'static str] {
    match server_id {
        "srv-dev-01" => &[
            "Sokol", "Muller", "Kolar", "Bauer", "Novak", "Kovac", "Miko",
        ],
        "srv-dev-02" => &["Victor", "Hammer", "Iceman", "Rogue", "Mace", "Hawk"],
        "srv-dev-04" => &["Ghost", "Shade", "Raven", "Viper"],
        "srv-dev-06" => &["Lima", "Echo", "Torch", "Cinder", "Golem", "Brick"],
        "srv-dev-07" => &["Cerny", "Bilek", "Zeman", "Kral"],
        "srv-dev-09" => &["Sanctuary", "Razor", "Atlas", "Frost"],
        "srv-dev-11" => &["Monty", "Bishop", "Ledger", "Morris"],
        "srv-dev-13" => &["lolwut", "faguss", "storied", "krzychu"],
        "srv-dev-14" => &["Raven", "Spade", "Torch", "Cobalt"],
        "srv-dev-16" => &["Bear", "Wolf", "Fox", "Otter", "Boar", "Crow"],
        _ => &[],
    }
}

fn to_hex(bytes: &[u8]) -> String {
    bytes.iter().map(|byte| format!("{byte:02x}")).collect()
}

/// SHA-256 hex of a server token (what the DB stores; never the token itself).
pub fn token_hash(token: &str) -> String {
    to_hex(&sha2::Sha256::digest(token.as_bytes()))
}

/// Generate a fresh server token: `(plaintext, hash)`. The plaintext is returned to the
/// server once on register; only the hash is persisted.
pub fn issue_token() -> Result<(String, String)> {
    let mut bytes = [0u8; 32];
    getrandom::getrandom(&mut bytes)
        .map_err(|error| anyhow::anyhow!("token rng failed: {error}"))?;
    let token = to_hex(&bytes);
    let hash = token_hash(&token);
    Ok((token, hash))
}

/// Wall-clock milliseconds since the Unix epoch, for list-visibility freshness windows.
fn now_unix_millis() -> i64 {
    let duration = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap_or_default();
    i64::try_from(duration.as_millis()).unwrap_or(i64::MAX)
}
