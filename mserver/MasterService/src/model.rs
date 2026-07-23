use serde::{Deserialize, Serialize};
use utoipa::{IntoParams, ToSchema};

#[derive(Clone, Copy, Debug, Deserialize, PartialEq, Eq, Serialize, ToSchema)]
#[serde(rename_all = "snake_case")]
pub enum VerificationState {
    SelfReported,
    Verified,
    Unreachable,
}

#[derive(Clone, Debug, Deserialize, PartialEq, Eq, Serialize, ToSchema)]
pub struct RegisterServerRequest {
    #[serde(rename = "app", default)]
    pub app_name: String,
    #[serde(rename = "serverId")]
    pub server_id: String,
    pub address: String,
    pub hostport: u16,
    pub hostname: String,
    pub gametype: String,
    pub actver: i32,
    pub reqver: i32,
    /// Build identity (e.g. "rc1" or a git sha; "-dev" suffix for dev builds). Empty when
    /// the publisher predates the version-tag wire field.
    #[serde(rename = "vertag", default)]
    pub version_tag: String,
    pub state: i32,
    pub numplayers: i32,
    pub maxplayers: i32,
    pub password: bool,
    #[serde(rename = "mod")]
    pub mod_list: String,
    #[serde(rename = "equalModRequired")]
    pub equal_mod_required: bool,
    #[serde(rename = "impl")]
    pub transport_impl: String,
    pub platform: String,
    #[serde(rename = "timeleft", default)]
    pub time_left: i32,
    #[serde(rename = "stateElapsedSeconds", default)]
    pub state_elapsed_seconds: i32,
    #[serde(rename = "mapname", default)]
    pub map_name: String,
    #[serde(default)]
    pub cadet: bool,
    #[serde(default)]
    pub difficulty: i32,
    #[serde(default)]
    pub jip: bool,
    #[serde(rename = "disabledAI", default)]
    pub disabled_ai: bool,
    #[serde(default)]
    pub respawn: i32,
    #[serde(rename = "respawnDelay", default)]
    pub respawn_delay: i32,
    #[serde(default)]
    pub locked: bool,
    #[serde(default)]
    pub dedicated: bool,
    #[serde(default)]
    pub description: String,
    #[serde(default)]
    pub param1: String,
    #[serde(default)]
    pub param2: String,
    #[serde(rename = "requiredAddons", default)]
    pub required_addons: String,
}

#[derive(Clone, Debug, Deserialize, PartialEq, Eq, Serialize, ToSchema)]
pub struct DirectoryServerRecord {
    #[serde(rename = "app", default)]
    pub app_name: String,
    #[serde(rename = "serverId")]
    pub server_id: String,
    pub address: String,
    pub hostport: u16,
    pub hostname: String,
    pub gametype: String,
    pub actver: i32,
    pub reqver: i32,
    #[serde(rename = "vertag", default)]
    pub version_tag: String,
    pub state: i32,
    pub numplayers: i32,
    pub maxplayers: i32,
    pub password: bool,
    #[serde(rename = "mod")]
    pub mod_list: String,
    #[serde(rename = "equalModRequired")]
    pub equal_mod_required: bool,
    #[serde(rename = "impl")]
    pub transport_impl: String,
    pub platform: String,
    #[serde(rename = "timeleft")]
    pub time_left: i32,
    #[serde(rename = "stateElapsedSeconds")]
    pub state_elapsed_seconds: i32,
    #[serde(rename = "mapname")]
    pub map_name: String,
    pub cadet: bool,
    pub difficulty: i32,
    pub jip: bool,
    #[serde(rename = "disabledAI")]
    pub disabled_ai: bool,
    pub respawn: i32,
    #[serde(rename = "respawnDelay")]
    pub respawn_delay: i32,
    pub locked: bool,
    pub dedicated: bool,
    pub description: String,
    pub param1: String,
    pub param2: String,
    #[serde(rename = "requiredAddons")]
    pub required_addons: String,
    #[serde(rename = "lastSeenUnixMs")]
    pub last_seen_unix_ms: i64,
    #[serde(rename = "verificationState")]
    pub verification_state: VerificationState,
    #[serde(rename = "lastObservedUnixMs")]
    pub last_observed_unix_ms: Option<i64>,
    #[serde(rename = "observedReachable")]
    pub observed_reachable: Option<bool>,
    /// Server token, returned ONLY in the register response when issued/rotated. Never
    /// stored (the DB keeps a hash) and never included in list/detail rows.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub token: Option<String>,
}

impl RegisterServerRequest {
    pub fn into_record(self, now_unix_ms: i64) -> DirectoryServerRecord {
        DirectoryServerRecord {
            app_name: self.app_name,
            server_id: self.server_id,
            address: self.address,
            hostport: self.hostport,
            hostname: self.hostname,
            gametype: self.gametype,
            actver: self.actver,
            reqver: self.reqver,
            version_tag: self.version_tag,
            state: self.state,
            numplayers: self.numplayers,
            maxplayers: self.maxplayers,
            password: self.password,
            mod_list: self.mod_list,
            equal_mod_required: self.equal_mod_required,
            transport_impl: self.transport_impl,
            platform: self.platform,
            time_left: self.time_left,
            state_elapsed_seconds: self.state_elapsed_seconds,
            map_name: self.map_name,
            cadet: self.cadet,
            difficulty: self.difficulty,
            jip: self.jip,
            disabled_ai: self.disabled_ai,
            respawn: self.respawn,
            respawn_delay: self.respawn_delay,
            locked: self.locked,
            dedicated: self.dedicated,
            description: self.description,
            param1: self.param1,
            param2: self.param2,
            required_addons: self.required_addons,
            last_seen_unix_ms: now_unix_ms,
            verification_state: VerificationState::SelfReported,
            last_observed_unix_ms: None,
            observed_reachable: None,
            token: None,
        }
    }
}

#[derive(Clone, Debug, Default, Deserialize, IntoParams, PartialEq, Eq, Serialize)]
#[into_params(parameter_in = Query)]
pub struct ListServersQuery {
    #[serde(rename = "app")]
    pub app_name: Option<String>,
    pub actver: Option<i32>,
    #[serde(rename = "vertag")]
    pub version_tag: Option<String>,
    pub hostname: Option<String>,
    pub gametype: Option<String>,
    pub minplayers: Option<i32>,
    pub maxplayers: Option<i32>,
    #[serde(rename = "verificationState")]
    pub verification_state: Option<VerificationState>,
    #[serde(rename = "includeUnverifiedServers")]
    pub include_unverified_servers: Option<bool>,
    #[serde(rename = "includeFullServers")]
    pub include_full_servers: Option<bool>,
    #[serde(rename = "includePasswordedServers")]
    pub include_passworded_servers: Option<bool>,
    #[serde(rename = "impl")]
    pub transport_impl: Option<String>,
    pub limit: Option<usize>,
}

#[derive(Clone, Debug, Deserialize, PartialEq, Eq, Serialize, ToSchema)]
pub struct ServerVersionGroup {
    #[serde(rename = "app")]
    pub app_name: String,
    pub actver: i32,
    #[serde(rename = "vertag")]
    pub version_tag: String,
    pub servers: usize,
}

#[derive(Clone, Debug, Default, Deserialize, IntoParams, PartialEq, Eq, Serialize)]
#[into_params(parameter_in = Query)]
pub struct ListModsQuery {
    #[serde(rename = "app")]
    pub app_name: Option<String>,
    pub actver: Option<i32>,
    #[serde(rename = "vertag")]
    pub version_tag: Option<String>,
    pub q: Option<String>,
    pub limit: Option<usize>,
}

#[derive(Clone, Debug, Deserialize, PartialEq, Eq, Serialize, ToSchema)]
pub struct PruneServersRequest {
    #[serde(rename = "maxAgeMs")]
    pub max_age_ms: i64,
}

#[derive(Clone, Debug, Deserialize, PartialEq, Eq, Serialize, ToSchema)]
pub struct PruneServersResponse {
    pub removed: usize,
}

#[derive(Clone, Debug, Deserialize, PartialEq, Eq, Serialize, ToSchema)]
pub struct ObserveServerRequest {
    pub reachable: bool,
    #[serde(rename = "observedUnixMs")]
    pub observed_unix_ms: Option<i64>,
}

#[derive(Clone, Debug, Deserialize, PartialEq, Eq, Serialize, ToSchema)]
pub struct ServiceMetadata {
    #[serde(rename = "serviceName")]
    pub service_name: String,
    #[serde(rename = "productName")]
    pub product_name: String,
    #[serde(rename = "apiVersion")]
    pub api_version: String,
    #[serde(rename = "openapiUrl")]
    pub openapi_url: String,
    #[serde(rename = "modsUrl")]
    pub mods_url: String,
}

#[derive(Clone, Debug, Deserialize, PartialEq, Eq, Serialize, ToSchema)]
pub struct ServiceSummary {
    #[serde(rename = "publicVerifiedServers")]
    pub public_verified_servers: usize,
    #[serde(rename = "publicPlayers")]
    pub public_players: i32,
    #[serde(rename = "selfReportedServers")]
    pub self_reported_servers: usize,
    #[serde(rename = "unreachableServers")]
    pub unreachable_servers: usize,
    #[serde(rename = "modCount")]
    pub mod_count: usize,
}

#[derive(Clone, Debug, Deserialize, PartialEq, Eq, Serialize, ToSchema)]
pub struct ModCatalogEntry {
    #[serde(rename = "modId")]
    pub mod_id: String,
    #[serde(rename = "app", default, skip_serializing_if = "Option::is_none")]
    pub app_name: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub actver: Option<i32>,
    #[serde(rename = "vertag", default, skip_serializing_if = "Option::is_none")]
    pub version_tag: Option<String>,
    #[serde(default)]
    pub compatible: bool,
    pub name: String,
    pub version: String,
    /// Canonical on-disk mod folder name (verbatim, incl. any `@` and case) the client
    /// installs into, e.g. `@fixture_alpha`, `@fixture_beta`, or `fixture_gamma`.
    /// Falls back to `@<modId>` when unset.
    #[serde(
        rename = "folderName",
        default,
        skip_serializing_if = "Option::is_none"
    )]
    pub folder_name: Option<String>,
    pub description: String,
    #[serde(default)]
    pub authors: Vec<String>,
    #[serde(rename = "homepageUrl")]
    pub homepage_url: Option<String>,
    #[serde(rename = "downloadUrl")]
    pub download_url: Option<String>,
    #[serde(rename = "sizeBytes", default)]
    pub size_bytes: Option<u64>,
}

#[derive(Clone, Debug, Deserialize, PartialEq, Eq, Serialize, ToSchema)]
pub struct ServerPlayer {
    pub name: String,
    pub role: String,
}

#[derive(Clone, Debug, Deserialize, PartialEq, Eq, Serialize, ToSchema)]
pub struct ServerPopulationSample {
    #[serde(rename = "observedUnixMs")]
    pub observed_unix_ms: i64,
    pub players: i32,
}

#[derive(Clone, Debug, Deserialize, PartialEq, Eq, Serialize, ToSchema)]
pub struct ServerRecentSession {
    pub mission: String,
    pub label: String,
    #[serde(rename = "playedMinutes")]
    pub played_minutes: i64,
    #[serde(rename = "peakPlayers")]
    pub peak_players: i32,
    #[serde(rename = "endedUnixMs")]
    pub ended_unix_ms: i64,
}

#[derive(Clone, Debug, Deserialize, PartialEq, Eq, Serialize, ToSchema)]
pub struct ModUsageServer {
    #[serde(rename = "serverId")]
    pub server_id: String,
    pub hostname: String,
    pub gametype: String,
    pub players: i32,
    #[serde(rename = "maxPlayers")]
    pub max_players: i32,
    pub password: bool,
}

#[derive(Clone, Debug, Deserialize, PartialEq, Eq, Serialize, ToSchema)]
pub struct ServerModReference {
    #[serde(rename = "modId")]
    pub mod_id: String,
    pub name: String,
    pub known: bool,
    pub version: Option<String>,
    pub description: Option<String>,
    #[serde(rename = "homepageUrl")]
    pub homepage_url: Option<String>,
    #[serde(rename = "downloadUrl")]
    pub download_url: Option<String>,
}

#[derive(Clone, Debug, Deserialize, PartialEq, Eq, Serialize, ToSchema)]
pub struct ServerDetail {
    pub server: DirectoryServerRecord,
    pub players: Vec<ServerPlayer>,
    pub mods: Vec<ServerModReference>,
    #[serde(rename = "playerHistory")]
    pub player_history: Vec<ServerPopulationSample>,
    #[serde(rename = "recentSessions")]
    pub recent_sessions: Vec<ServerRecentSession>,
}
