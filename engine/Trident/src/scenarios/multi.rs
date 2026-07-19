//! Multi-instance test orchestrator — runs `*.test/` directories with multiple game instances.
//!
//! A multi-instance test is a directory containing:
//! - `test.toml` with `[[instances]]` array defining roles
//! - `{name}.sqf` per instance with assertions
//!
//! The orchestrator starts instances in TOML order, respecting `after` dependencies
//! (polling NGS state via harness), feeds each its SQF script, and waits for all
//! to complete. Test passes only if every instance exits 0.

use crate::client::{GameInstance, HarnessClient};
use crate::protocol::Command;
use crate::scenarios::ScenarioResult;
use anyhow::{Context, Result};
use papa_bear_archive::Pbo;
use papa_bear_client::query::query_server;
use std::collections::{HashMap, HashSet};
use std::fs::File;
use std::io::Write;
use std::path::{Component, Path, PathBuf};
use std::process::Stdio;
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};
use tokio::process::{Child, Command as ProcessCommand};

/// Parsed multi-instance test configuration from `test.toml`.
#[derive(serde::Deserialize)]
pub struct MultiTestConfig {
    /// Shared mission path (relative to repo root)
    #[serde(default)]
    pub mission: Option<String>,

    /// Data-directory override for tests that need a package different from the default.
    /// When unset, the runner uses the `--data-dir` CLI / `OFPR_DATA_DIR` value.
    /// Resolved relative to the repo root, not to the test path.
    #[serde(default)]
    pub data_dir: Option<String>,

    /// Game network port (default 2302)
    #[serde(default = "default_game_port")]
    pub game_port: u16,

    /// Per-test timeout override in seconds.
    #[serde(default)]
    pub timeout: Option<u64>,

    /// Per-test renderer override (e.g. "dummy" for headless logic-only scenarios that
    /// make no assertions on rendered output). Falls back to the global --render when unset.
    #[serde(default)]
    pub render: Option<String>,

    /// Environment variables that must be present before the scenario starts.
    /// Each value is available through an `${env.NAME}` placeholder.
    #[serde(default)]
    pub required_env: Vec<String>,

    /// Background services — started before game instances in array order
    #[serde(default)]
    pub services: Vec<ServiceConfig>,

    /// Instance definitions — started in array order
    pub instances: Vec<InstanceConfig>,
}

const fn default_game_port() -> u16 {
    2302
}

/// Configuration for a single game instance within a multi-instance test.
#[derive(Clone, serde::Deserialize)]
pub struct InstanceConfig {
    /// Role name — matches `{name}.sqf` in the test directory
    pub name: String,

    /// Instance type: "server" or "client"
    #[serde(rename = "type", default = "default_client")]
    pub instance_type: String,

    /// Name of the server instance to connect to (adds `--connect 127.0.0.1`)
    #[serde(default)]
    pub connect: Option<String>,

    /// Host to connect to when `connect` is set (default: 127.0.0.1).
    #[serde(default)]
    pub connect_host: Option<String>,

    /// Name of the instance to wait for before starting this one
    #[serde(default)]
    pub after: Option<String>,

    /// Required NGS `server_state` on the `after` instance before starting this one
    #[serde(default)]
    pub wait_ngs: Option<i64>,

    /// Required NGS `client_state` on the `after` instance before starting this one
    #[serde(default)]
    pub wait_ngs_client: Option<i64>,

    /// Auto-assign to role (e.g. "WEST:1")
    #[serde(default)]
    pub mp_assign: Option<String>,

    /// Whether this instance receives the shared mission as `--simulate` / `--test-mission`.
    #[serde(default = "default_true")]
    pub use_mission: bool,

    /// Per-instance mission override.
    #[serde(default)]
    pub mission: Option<String>,

    /// Additional CLI args
    #[serde(default)]
    pub extra_args: Vec<String>,

    /// Files to copy from the test directory into the instance user directory before launch.
    #[serde(default)]
    pub copy_files: Vec<InstanceCopyFileConfig>,

    /// Per-instance timeout override (seconds)
    #[serde(default)]
    pub timeout: Option<u64>,

    /// Delay before this instance is spawned, after dependency checks pass.
    #[serde(default)]
    pub start_delay_ms: Option<u64>,

    /// Per-instance network port override.
    #[serde(default)]
    pub network_port: Option<u16>,

    /// Per-instance connect port override (defaults to `network_port` if not set).
    #[serde(default)]
    pub connect_port: Option<u16>,

    /// Test-only machine identity seed used to derive the multiplayer player id.
    /// Applied before process launch through `POSEIDON_TEST_MACHINE_ID`.
    #[serde(default)]
    pub machine_id: Option<String>,

    /// Start this role automatically when the scenario begins.
    #[serde(default = "default_true")]
    pub autostart: bool,

    /// Set true to suppress the default `--autotest` injection for client instances.
    #[serde(default)]
    pub no_autotest: bool,

    /// Extra environment variables for the spawned process (values support
    /// `${...}` placeholders).
    #[serde(default)]
    pub env: Vec<InstanceEnvConfig>,
}

#[derive(Clone, serde::Deserialize)]
pub struct InstanceCopyFileConfig {
    pub source: String,
    pub target: String,
}

#[derive(Clone, serde::Deserialize)]
pub struct InstanceEnvConfig {
    pub name: String,
    pub value: String,
}

/// Configuration for a background process used by a multi-instance test.
#[derive(Clone, serde::Deserialize)]
pub struct ServiceConfig {
    /// Service name, used by `after` and `${service.<name>.url}` placeholders.
    pub name: String,

    /// Service type: "master-server" or "master-probe".
    #[serde(rename = "type")]
    pub service_type: String,

    /// Name of an earlier service that must exist before this starts.
    #[serde(default)]
    pub after: Option<String>,

    /// Service listen port. Use 0 for an ephemeral localhost port.
    #[serde(default)]
    pub port: Option<u16>,

    /// Listen address for HTTP services (default `127.0.0.1:<port>`).
    #[serde(default)]
    pub listen: Option<String>,

    /// `SQLite` database path for master-server.
    #[serde(default)]
    pub db: Option<String>,

    /// Explicit path to `papa-bear-master-service`.
    #[serde(default)]
    pub binary: Option<String>,

    /// Master service name for master-probe (default `master`).
    #[serde(default)]
    pub master: Option<String>,

    /// Probe interval.
    #[serde(default)]
    pub interval_secs: Option<u64>,

    /// Probe request timeout.
    #[serde(default)]
    pub timeout_ms: Option<u64>,

    /// Age threshold for probe pruning.
    #[serde(default)]
    pub prune_max_age_secs: Option<u64>,

    /// Additional process args appended after the typed service args.
    #[serde(default)]
    pub extra_args: Vec<String>,

    /// Extra environment variables.
    #[serde(default)]
    pub env: HashMap<String, String>,

    /// Mods to seed into a local master-server mod store before it starts.
    #[serde(default)]
    pub seed_mods: Vec<ServiceSeedModConfig>,
}

#[derive(Clone, serde::Deserialize)]
pub struct ServiceSeedModConfig {
    pub id: String,
    pub name: String,
    pub version: String,
    #[serde(default)]
    pub app: Option<String>,
    #[serde(default)]
    pub actver: Option<i32>,
    #[serde(rename = "vertag", default)]
    pub version_tag: Option<String>,
    pub folder: String,
    pub source: String,
    #[serde(default)]
    pub description: String,
    #[serde(default)]
    pub authors: Vec<String>,
    #[serde(default)]
    pub homepage_url: Option<String>,
}

fn default_client() -> String {
    "client".into()
}

const fn default_true() -> bool {
    true
}

impl InstanceConfig {
    pub(crate) fn is_server(&self) -> bool {
        self.instance_type == "server"
    }
}

#[allow(clippy::single_option_map)]
pub(super) fn resolve_mission_path(mission: Option<&str>) -> Option<String> {
    mission.map(|m| {
        let mp = Path::new(m);
        if mp.is_relative() {
            repo_root().join(mp).to_string_lossy().to_string()
        } else {
            m.to_string()
        }
    })
}

pub(super) fn repo_root() -> std::path::PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .and_then(Path::parent)
        .unwrap_or_else(|| Path::new(env!("CARGO_MANIFEST_DIR")))
        .to_path_buf()
}

fn reserve_local_port() -> Result<u16> {
    // The game binds the reserved port as UDP, so ask the OS for a UDP
    // ephemeral port directly — Windows UDP exclusion ranges (`netsh int
    // ipv4 show excludedportrange protocol=udp`) are then avoided by
    // construction. A TCP-probed ephemeral port can sit inside such a range
    // (TCP ephemeral allocation is sequential, and the excluded runs span
    // >1000 ports, so retrying a TCP probe can fail every attempt) and the
    // game's UDP bind then fails with nothing visible in netstat.
    let socket = std::net::UdpSocket::bind(("0.0.0.0", 0))
        .context("failed to reserve ephemeral UDP port")?;
    Ok(socket
        .local_addr()
        .context("failed to read reserved local address")?
        .port())
}

fn expand_placeholders(value: &str, values: &HashMap<String, String>) -> String {
    let mut expanded = value.to_string();
    for (key, replacement) in values {
        expanded = expanded.replace(&format!("${{{key}}}"), replacement);
    }
    expanded
}

fn expand_placeholders_vec(
    values: &[String],
    replacements: &HashMap<String, String>,
) -> Vec<String> {
    values
        .iter()
        .map(|value| expand_placeholders(value, replacements))
        .collect()
}

fn required_environment_replacements_with(
    required: &[String],
    mut lookup: impl FnMut(&str) -> Option<String>,
) -> Result<HashMap<String, String>> {
    let mut replacements = HashMap::new();
    for name in required {
        let valid_name = name.bytes().enumerate().all(|(index, ch)| {
            ch == b'_' || ch.is_ascii_alphabetic() || (index > 0 && ch.is_ascii_digit())
        });
        if name.is_empty() || !valid_name {
            anyhow::bail!("invalid required environment variable name '{name}'");
        }

        let value = lookup(name).filter(|value| !value.is_empty()).with_context(|| {
            format!(
                "required environment variable '{name}' is not set, is empty, or is not valid Unicode"
            )
        })?;
        replacements.insert(format!("env.{name}"), value);
    }
    Ok(replacements)
}

fn required_environment_replacements(required: &[String]) -> Result<HashMap<String, String>> {
    required_environment_replacements_with(required, |name| std::env::var(name).ok())
}

fn resolve_repo_path(value: &str, replacements: &HashMap<String, String>) -> PathBuf {
    let expanded = expand_placeholders(value, replacements);
    let path = PathBuf::from(expanded);
    if path.is_absolute() {
        path
    } else {
        repo_root().join(path)
    }
}

pub(super) fn resolve_data_dir_path(data_dir: &str) -> PathBuf {
    let data_path = PathBuf::from(data_dir);
    let data_path = if data_path.is_absolute() {
        data_path
    } else {
        repo_root().join(data_path)
    };
    std::fs::canonicalize(&data_path).unwrap_or(data_path)
}

pub(super) fn clear_legacy_network_asset_tmp(data_path: &Path) -> Result<()> {
    for relative in ["tmp/squads", "tmp/players"] {
        let path = data_path.join(relative);
        if path.exists() {
            std::fs::remove_dir_all(&path)
                .with_context(|| format!("failed to remove {}", path.display()))?;
        }
    }
    Ok(())
}

fn seed_mod_store(
    mods_dir: &Path,
    seeds: &[ServiceSeedModConfig],
    replacements: &HashMap<String, String>,
) -> Result<()> {
    for seed in seeds {
        let source = resolve_repo_path(&seed.source, replacements);
        let pbo = Pbo::pack_dir(&source, None)
            .with_context(|| format!("packing seed mod '{}' from {}", seed.id, source.display()))?;

        let seed_dir = mods_dir.join(&seed.id);
        std::fs::create_dir_all(&seed_dir)
            .with_context(|| format!("creating seed mod store {}", seed_dir.display()))?;

        let mut raw = Vec::new();
        pbo.write(&mut raw)
            .with_context(|| format!("serialising seed mod '{}'", seed.id))?;
        let archive_path = seed_dir.join(format!("{}.pbo.zst", seed.id));
        let mut archive = File::create(&archive_path)
            .with_context(|| format!("creating {}", archive_path.display()))?;
        {
            let mut encoder = zstd::stream::write::Encoder::new(&mut archive, 19)
                .with_context(|| format!("starting zstd encoder for {}", archive_path.display()))?;
            encoder
                .write_all(&raw)
                .with_context(|| format!("compressing seed mod '{}'", seed.id))?;
            encoder
                .finish()
                .with_context(|| format!("finishing zstd seed mod '{}'", seed.id))?;
        }
        let size_bytes = archive
            .metadata()
            .with_context(|| format!("sizing {}", archive_path.display()))?
            .len();

        let metadata = serde_json::json!({
            "modId": seed.id,
            "app": seed.app,
            "actver": seed.actver,
            "vertag": seed.version_tag,
            "name": seed.name,
            "version": seed.version,
            "folderName": seed.folder,
            "description": seed.description,
            "authors": seed.authors,
            "homepageUrl": seed.homepage_url,
            "downloadUrl": format!("/v1/mods/{}/download", seed.id),
            "sizeBytes": size_bytes
        });
        let metadata_path = seed_dir.join("mod.json");
        std::fs::write(&metadata_path, serde_json::to_vec_pretty(&metadata)?)
            .with_context(|| format!("writing {}", metadata_path.display()))?;
    }
    Ok(())
}

fn extract_mods_dir_arg(args: &[String]) -> Option<PathBuf> {
    args.windows(2)
        .find(|window| window[0] == "--mods-dir")
        .map(|window| PathBuf::from(&window[1]))
}

fn resolve_master_service_binary(config: &ServiceConfig) -> Result<PathBuf> {
    if let Some(binary) = &config.binary {
        let expanded = PathBuf::from(binary);
        if expanded.exists() {
            return Ok(expanded);
        }
        anyhow::bail!(
            "configured service binary does not exist: {}",
            expanded.display()
        );
    }

    if let Ok(binary) = std::env::var("PAPA_BEAR_MASTER_SERVICE") {
        let path = PathBuf::from(binary);
        if path.exists() {
            return Ok(path);
        }
    }

    let root = repo_root();
    let binary_name = format!("papa-bear-master-service{}", std::env::consts::EXE_SUFFIX);
    let candidates = [
        root.join("target/rwdi").join(&binary_name),
        root.join("target/debug").join(&binary_name),
        root.join("target/release").join(&binary_name),
        root.join("mserver/MasterService/target/debug")
            .join(&binary_name),
        root.join("mserver/MasterService/target/release")
            .join(&binary_name),
    ];
    candidates
        .into_iter()
        .find(|path| path.exists())
        .with_context(|| {
            "papa-bear-master-service was not found; build it with \
             `cargo build --workspace --profile rwdi`"
        })
}

struct RunningService {
    name: String,
    child: Child,
}

impl RunningService {
    async fn stop(&mut self, timeout: Duration) {
        if matches!(self.child.try_wait(), Ok(Some(_))) {
            return;
        }
        let _ = self.child.start_kill();
        let _ = tokio::time::timeout(timeout, self.child.wait()).await;
    }
}

fn service_url_from_listen(listen: &str) -> String {
    let host_port = listen
        .strip_prefix("0.0.0.0:")
        .map_or_else(|| listen.to_string(), |port| format!("127.0.0.1:{port}"));
    format!("http://{host_port}")
}

async fn wait_for_http_ok(url: &str, timeout: Duration) -> Result<()> {
    let deadline = Instant::now() + timeout;
    let client = reqwest::Client::new();
    let health_url = format!("{}/healthz", url.trim_end_matches('/'));
    loop {
        match client.get(&health_url).send().await {
            Ok(response) if response.status().is_success() => return Ok(()),
            Ok(_) | Err(_) => {}
        }
        if Instant::now() >= deadline {
            anyhow::bail!("timed out waiting for {health_url}");
        }
        tokio::time::sleep(Duration::from_millis(100)).await;
    }
}

fn spawn_service_process(
    test_name: &str,
    config: &ServiceConfig,
    args: &[String],
    output_dir: &Path,
    replacements: &HashMap<String, String>,
) -> Result<Child> {
    let binary = resolve_master_service_binary(config)?;
    let service_output = output_dir.join("services").join(&config.name);
    std::fs::create_dir_all(&service_output)
        .with_context(|| format!("failed to create {}", service_output.display()))?;
    let stdout = File::create(service_output.join("stdout.log"))
        .with_context(|| format!("failed to create stdout log for service '{}'", config.name))?;
    let stderr = File::create(service_output.join("stderr.log"))
        .with_context(|| format!("failed to create stderr log for service '{}'", config.name))?;

    let mut command = ProcessCommand::new(binary);
    command
        .args(args)
        .stdout(Stdio::from(stdout))
        .stderr(Stdio::from(stderr))
        .kill_on_drop(true);
    for (key, value) in &config.env {
        command.env(key, expand_placeholders(value, replacements));
    }

    tracing::debug!(
        "[{test_name}] spawning service '{}' ({})",
        config.name,
        args.join(" ")
    );
    command
        .spawn()
        .with_context(|| format!("failed to spawn service '{}'", config.name))
}

#[allow(clippy::too_many_lines)]
async fn start_services(
    test_name: &str,
    services: &[ServiceConfig],
    output_dir: &Path,
    replacements: &mut HashMap<String, String>,
    timeout: Duration,
) -> Result<Vec<RunningService>> {
    let mut running = Vec::new();
    for service in services {
        if let Some(after) = &service.after {
            let known_service = running
                .iter()
                .any(|item: &RunningService| &item.name == after);
            if !known_service {
                anyhow::bail!(
                    "service '{}' depends on '{}' which has not been started",
                    service.name,
                    after
                );
            }
        }

        match service.service_type.as_str() {
            "master-server" => {
                let port = match service.port.unwrap_or(0) {
                    0 => reserve_local_port()?,
                    port => port,
                };
                let listen = service.listen.as_deref().map_or_else(
                    || format!("127.0.0.1:{port}"),
                    |value| expand_placeholders(value, replacements),
                );
                let url = service_url_from_listen(&listen);
                let db = service.db.as_deref().map_or_else(
                    || {
                        output_dir
                            .join(format!("{}.sqlite3", service.name))
                            .to_string_lossy()
                            .to_string()
                    },
                    |value| expand_placeholders(value, replacements),
                );
                let mut args = vec![
                    "server".to_string(),
                    "--listen".to_string(),
                    listen.clone(),
                    "--db".to_string(),
                    db,
                ];
                args.extend(expand_placeholders_vec(&service.extra_args, replacements));
                if !service.seed_mods.is_empty() {
                    let mods_dir = extract_mods_dir_arg(&args).with_context(|| {
                        format!(
                            "service '{}' has seed_mods but no --mods-dir in extra_args",
                            service.name
                        )
                    })?;
                    seed_mod_store(&mods_dir, &service.seed_mods, replacements)?;
                }
                let child =
                    spawn_service_process(test_name, service, &args, output_dir, replacements)?;
                wait_for_http_ok(&url, timeout).await?;
                replacements.insert(format!("service.{}.url", service.name), url.clone());
                replacements.insert(format!("service.{}.port", service.name), port.to_string());
                replacements.insert(format!("{}.url", service.name), url.clone());
                replacements.insert(format!("ports.{}", service.name), port.to_string());
                if service.name == "master" {
                    replacements.insert("master.url".to_string(), url);
                    replacements.insert("ports.master".to_string(), port.to_string());
                }
                running.push(RunningService {
                    name: service.name.clone(),
                    child,
                });
            }
            "master-probe" => {
                let master_name = service.master.as_deref().unwrap_or("master");
                let master_url = replacements
                    .get(&format!("service.{master_name}.url"))
                    .or_else(|| replacements.get(&format!("{master_name}.url")))
                    .with_context(|| {
                        format!(
                            "service '{}' needs master service '{}' to be started first",
                            service.name, master_name
                        )
                    })?
                    .clone();
                let mut args = vec![
                    "probe".to_string(),
                    "--master-server".to_string(),
                    master_url,
                    "--interval-secs".to_string(),
                    service.interval_secs.unwrap_or(1).to_string(),
                    "--timeout-ms".to_string(),
                    service.timeout_ms.unwrap_or(500).to_string(),
                    "--prune-max-age-secs".to_string(),
                    service.prune_max_age_secs.unwrap_or(60).to_string(),
                ];
                args.extend(expand_placeholders_vec(&service.extra_args, replacements));
                let mut child =
                    spawn_service_process(test_name, service, &args, output_dir, replacements)?;
                tokio::time::sleep(Duration::from_millis(250)).await;
                if let Some(status) = child
                    .try_wait()
                    .with_context(|| format!("failed to inspect service '{}'", service.name))?
                {
                    anyhow::bail!("service '{}' exited early with {status}", service.name);
                }
                running.push(RunningService {
                    name: service.name.clone(),
                    child,
                });
            }
            other => anyhow::bail!("unsupported service type '{other}' for '{}'", service.name),
        }
    }
    Ok(running)
}

/// Check if a directory is a multi-instance test (`*.test/` with `test.toml` containing instances).
pub fn is_multi_test(p: &Path) -> bool {
    if !p.is_dir() {
        return false;
    }
    let name = p.file_name().unwrap_or_default().to_string_lossy();
    if !name.ends_with(".test") {
        return false;
    }
    let toml_path = p.join("test.toml");
    if !toml_path.exists() {
        return false;
    }
    // Quick check: must contain [[instances]]
    std::fs::read_to_string(&toml_path)
        .map(|c| c.contains("[[instances]]"))
        .unwrap_or(false)
}

/// Extract test name from a `*.test/` directory path.
pub fn multi_test_name(s: &str) -> String {
    s.strip_suffix(".test").unwrap_or(s).to_string()
}

pub(super) fn safe_output_name(name: &str) -> String {
    name.chars()
        .map(|ch| {
            if ch.is_ascii_alphanumeric() || matches!(ch, '-' | '_' | '.') {
                ch
            } else {
                '_'
            }
        })
        .collect()
}

fn checked_relative_file_path(value: &str) -> Result<&Path> {
    let path = Path::new(value);
    if path.is_absolute()
        || path.components().any(|component| {
            matches!(
                component,
                Component::ParentDir | Component::RootDir | Component::Prefix(_)
            )
        })
    {
        anyhow::bail!("path '{value}' must be a relative file path without parent traversal");
    }
    Ok(path)
}

/// Return the number of instances a multi-test directory needs.
/// Parses `test.toml` and counts `[[instances]]` entries (minimum 1).
pub fn instance_count(dir: &Path) -> u32 {
    let toml_path = dir.join("test.toml");
    std::fs::read_to_string(&toml_path)
        .ok()
        .and_then(|c| toml::from_str::<MultiTestConfig>(&c).ok())
        .map_or(1, |cfg| {
            u32::try_from(cfg.instances.len().max(1)).unwrap_or(1)
        })
}

/// Return the number of renderer processes a multi-test directory needs.
/// Dedicated servers are headless and do not consume a GUI scheduler slot.
pub fn gui_instance_count(dir: &Path) -> u32 {
    let toml_path = dir.join("test.toml");
    std::fs::read_to_string(&toml_path)
        .ok()
        .and_then(|c| toml::from_str::<MultiTestConfig>(&c).ok())
        .map_or(1, |cfg| {
            u32::try_from(
                cfg.instances
                    .iter()
                    .filter(|instance| !instance.is_server())
                    .count()
                    .max(1),
            )
            .unwrap_or(1)
        })
}

pub(super) fn configured_data_dir(dir: &Path) -> Result<Option<String>> {
    let toml_path = dir.join("test.toml");
    let content = std::fs::read_to_string(&toml_path)
        .with_context(|| format!("failed to read {}", toml_path.display()))?;
    let config: MultiTestConfig = toml::from_str(&content)
        .with_context(|| format!("failed to parse {}", toml_path.display()))?;
    Ok(config.data_dir)
}

fn time_left(deadline: Instant) -> Duration {
    deadline.saturating_duration_since(Instant::now())
}

pub(super) type RoleProgress = Arc<Mutex<HashMap<String, String>>>;
pub(super) type RoleBarriers = Arc<Mutex<HashMap<String, HashSet<String>>>>;
type HardFailureLogs = Arc<Vec<PathBuf>>;
type OrderedRoleTaskResult<T> = Result<(usize, T, Option<(String, String)>)>;

const ROLE_SHUTDOWN_DEADLINE: Duration = Duration::from_millis(500);

fn scan_hard_failure_logs(paths: &[PathBuf]) -> Option<String> {
    const PATTERNS: &[&str] = &[
        "CreatePlayer rejected by version/mod check",
        "Bad version",
        "server rejected connection",
        "Fehlerhafte Version",
        "Chybná verze",
        "Chybna verze",
    ];

    for path in paths {
        let Ok(content) = std::fs::read_to_string(path) else {
            continue;
        };
        for line in content.lines() {
            if PATTERNS.iter().any(|pattern| line.contains(pattern)) {
                return Some(format!(
                    "hard connection failure in {}: {}",
                    path.display(),
                    line
                ));
            }
        }
    }
    None
}

fn set_role_progress(progress: &RoleProgress, role_name: &str, stmt: &str) {
    if let Ok(mut progress) = progress.lock() {
        progress.insert(role_name.to_string(), stmt.to_string());
    }
}

fn clear_role_progress(progress: &RoleProgress, role_name: &str) {
    if let Ok(mut progress) = progress.lock() {
        progress.remove(role_name);
    }
}

fn participant_exit_failure(
    role_name: &str,
    result: anyhow::Result<i32>,
) -> Option<(String, String)> {
    match result {
        Ok(0) => None,
        Ok(exit_code) => Some((
            role_name.to_string(),
            format!("participant process exited with exit code {exit_code}"),
        )),
        Err(error) => Some((
            role_name.to_string(),
            format!("participant process did not exit cleanly: {error:#}"),
        )),
    }
}

fn describe_role_progress(progress: &RoleProgress) -> String {
    let Ok(progress) = progress.lock() else {
        return "scenario timeout".into();
    };
    if progress.is_empty() {
        return "scenario timeout".into();
    }
    let mut entries: Vec<_> = progress
        .iter()
        .map(|(role, stmt)| format!("{role}: {stmt}"))
        .collect();
    entries.sort();
    format!("scenario timeout; pending {}", entries.join(", "))
}

async fn collect_role_tasks<T: Send + 'static>(
    handles: &mut tokio::task::JoinSet<OrderedRoleTaskResult<T>>,
    deadline: Instant,
    progress: &RoleProgress,
    hard_failure_log_paths: &[PathBuf],
) -> (Vec<T>, Option<(String, String)>) {
    let mut returned = Vec::new();
    let mut failure = None;

    while !handles.is_empty() {
        let remaining = time_left(deadline);
        if remaining.is_zero() {
            let reason = scan_hard_failure_logs(hard_failure_log_paths)
                .unwrap_or_else(|| describe_role_progress(progress));
            failure = Some(("orchestrator".into(), reason));
            break;
        }

        match tokio::time::timeout(remaining, handles.join_next()).await {
            Ok(Some(Ok(Ok((order, value, Some(role_failure)))))) => {
                returned.push((order, value));
                failure = Some(role_failure);
                break;
            }
            Ok(Some(Ok(Ok((order, value, None))))) => returned.push((order, value)),
            Ok(Some(Ok(Err(error)))) => {
                failure = Some(("orchestrator".into(), format!("{error:#}")));
                break;
            }
            Ok(Some(Err(error))) => {
                failure = Some(("orchestrator".into(), format!("task panic: {error}")));
                break;
            }
            Ok(None) => break,
            Err(_) => {
                let reason = scan_hard_failure_logs(hard_failure_log_paths)
                    .unwrap_or_else(|| describe_role_progress(progress));
                failure = Some(("orchestrator".into(), reason));
                break;
            }
        }
    }

    if failure.is_some() {
        handles.abort_all();
        while handles.join_next().await.is_some() {}
    }

    returned.sort_by_key(|(order, _)| *order);
    (
        returned.into_iter().map(|(_, value)| value).collect(),
        failure,
    )
}

async fn sleep_until_poll_or_deadline(poll_interval: Duration, deadline: Instant) {
    let remaining = time_left(deadline);
    if !remaining.is_zero() {
        tokio::time::sleep(poll_interval.min(remaining)).await;
    }
}

fn mark_role_barrier(barriers: &RoleBarriers, barrier_name: &str, role_name: &str) {
    if let Ok(mut barriers) = barriers.lock() {
        barriers
            .entry(barrier_name.to_string())
            .or_default()
            .insert(role_name.to_string());
    }
}

fn role_barrier_count(barriers: &RoleBarriers, barrier_name: &str) -> usize {
    barriers
        .lock()
        .ok()
        .and_then(|barriers| barriers.get(barrier_name).map(HashSet::len))
        .unwrap_or(0)
}

fn normalize_mp_side(side: &str) -> Option<&'static str> {
    match side.trim().to_ascii_uppercase().as_str() {
        "WEST" => Some("WEST"),
        "EAST" => Some("EAST"),
        "RES" | "RESISTANCE" | "GUER" | "GUERRILA" | "GUERRILLA" => Some("RES"),
        "CIV" | "CIVILIAN" => Some("CIV"),
        _ => None,
    }
}

fn parse_mp_slot_spec(spec: &str, selected_side: &str) -> std::result::Result<String, String> {
    let trimmed = spec.trim();
    let (side, slot) = trimmed
        .split_once(':')
        .map_or((selected_side, trimmed), |(side, slot)| {
            (side.trim(), slot.trim())
        });
    let side = normalize_mp_side(side).ok_or_else(|| format!("unknown MP side '{side}'"))?;
    let slot = slot
        .parse::<u32>()
        .map_err(|e| format!("invalid MP slot '{slot}': {e}"))?;
    if slot == 0 {
        return Err("MP slot numbers are 1-based".into());
    }
    Ok(format!("{side}:{slot}"))
}

async fn eval_until_ok(
    client: &mut HarnessClient,
    stmt: &str,
    deadline: Instant,
    poll_interval: Duration,
) -> Result<std::result::Result<(), String>> {
    let mut last_result = String::new();
    loop {
        let remaining = time_left(deadline);
        if remaining.is_zero() {
            return Ok(Err(if last_result.is_empty() {
                "scenario timeout".into()
            } else {
                format!("scenario timeout; last result: {last_result}")
            }));
        }
        match tokio::time::timeout(remaining, client.eval(stmt)).await {
            Ok(Ok(result)) => {
                let result_str = result.clone().trim_matches('"').to_string();
                if result_str == "OK" {
                    return Ok(Ok(()));
                }
                last_result = result_str;
                sleep_until_poll_or_deadline(poll_interval, deadline).await;
            }
            Ok(Err(e)) => {
                return Ok(Err(if last_result.is_empty() {
                    format!("connection error: {e}")
                } else {
                    format!("connection error: {e}; last result: {last_result}")
                }));
            }
            Err(_) => {
                return Ok(Err(if last_result.is_empty() {
                    "scenario timeout".into()
                } else {
                    format!("scenario timeout; last result: {last_result}")
                }));
            }
        }
    }
}

/// Run a single role's SQF script against its harness client.
///
/// Supports orchestrator-side directives alongside eval'd SQF:
/// - `triWait <ms>` — sleep
/// - `triMpJoin <host> <port> [password] [modpath]` — direct harness remote join
/// - `triMpPickSide <side>` — remember WEST/EAST/RES/CIV for following slot verbs
/// - `triMpWaitServerPlayers <host> <port> <min>` — poll public server query player count
/// - `triMpPickSlot <slot|side:slot>` — assign this player to a role slot
/// - `triMpWaitSlotTaken <slot|side:slot>` — poll until a role slot has a human player
/// - `triMpReady [state]` — mark this client ready, defaulting to play (`14`)
/// - `triBarrier <name>` — wait until every role script reaches the same named barrier
/// - `triHoldKey <scancode>` — send key-down with hold
/// - `triReleaseKey <scancode>` — send key-up
/// - `triAssertPlayerMoves <scancode> <hold-ms> <min-distance>` — hold key and poll `play_state` for horizontal movement
/// - `triAssertVonReceived <min>` — poll for ≥ min `von_received` events
/// - `triResetVonReceived` — clear accumulated `von_received` events for this role
/// - `triAssertNgs <min>` / `triAssertNgsClient <min>` — poll harness `query ngs`
/// - `triLogPlayState [label]` — log one harness `query play_state` snapshot without asserting
/// - `triAssertPlayState` / `triAssertMissionPlayable` — poll harness `query play_state` for a playable MP snapshot
/// - `triExec <code>` — execute SQF via harness without waiting for an expression result
/// - `triAssert*` / `triMpAssignSelf*` / `triMpSlotTaken` — eval via harness, retry until "OK" or timeout
/// - anything else — eval via harness (fire-and-forget)
///
/// Returns the client back along with the result: `None` on success, `Some((role, reason))` on failure.
#[allow(clippy::too_many_lines, clippy::cognitive_complexity)]
#[allow(clippy::too_many_arguments)]
pub(super) async fn run_role_script(
    test_name: &str,
    role_name: &str,
    mut client: HarnessClient,
    statements: &[String],
    deadline: Instant,
    poll_interval: Duration,
    progress: RoleProgress,
    barriers: RoleBarriers,
    role_count: usize,
    hard_failure_logs: HardFailureLogs,
) -> Result<(HarnessClient, Option<(String, String)>)> {
    let mut von_event_total: usize = 0;
    let mut selected_mp_side = String::from("WEST");

    for stmt in statements {
        set_role_progress(&progress, role_name, stmt);
        if let Some(reason) = scan_hard_failure_logs(&hard_failure_logs) {
            clear_role_progress(&progress, role_name);
            return Ok((client, Some((role_name.into(), reason))));
        }
        if Instant::now() >= deadline {
            clear_role_progress(&progress, role_name);
            return Ok((client, Some((role_name.into(), "scenario timeout".into()))));
        }

        // triWait <ms> — orchestrator-side sleep
        if let Some(ms_str) = stmt.strip_prefix("triWait ") {
            if let Ok(ms) = ms_str.trim().parse::<u64>() {
                let wait = Duration::from_millis(ms);
                let remaining = time_left(deadline);
                tokio::time::sleep(wait.min(remaining)).await;
                if wait > remaining {
                    return Ok((
                        client,
                        Some((role_name.into(), format!("{stmt} — scenario timeout"))),
                    ));
                }
            }
            continue;
        }

        // triMpJoin <host> <port> [password] [modpath] — direct harness remote join.
        if let Some(args_str) = stmt.strip_prefix("triMpJoin ") {
            let args: Vec<&str> = args_str.split_whitespace().collect();
            if args.len() < 2 {
                return Ok((
                    client,
                    Some((role_name.into(), "triMpJoin requires host and port".into())),
                ));
            }
            let port = match args[1].parse::<u16>() {
                Ok(port) => port,
                Err(e) => {
                    return Ok((
                        client,
                        Some((role_name.into(), format!("triMpJoin invalid port: {e}"))),
                    ));
                }
            };
            let response = tokio::time::timeout(
                time_left(deadline),
                client.send(&Command::MpJoin {
                    what: "mp_join".into(),
                    address: args[0].into(),
                    port,
                    password: args.get(2).map(|value| (*value).into()),
                    modpath: args.get(3).map(|value| (*value).into()),
                }),
            )
            .await;
            match response {
                Ok(Ok(resp)) if resp.ok => {}
                Ok(Ok(resp)) => {
                    return Ok((
                        client,
                        Some((
                            role_name.into(),
                            format!(
                                "triMpJoin failed: {}",
                                resp.error.unwrap_or_else(|| "unknown error".into())
                            ),
                        )),
                    ));
                }
                Ok(Err(e)) => {
                    return Ok((client, Some((role_name.into(), format!("triMpJoin — {e}")))));
                }
                Err(_) => {
                    return Ok((
                        client,
                        Some((role_name.into(), "triMpJoin — scenario timeout".into())),
                    ));
                }
            }
            continue;
        }

        if let Some(side) = stmt.strip_prefix("triMpPickSide ") {
            match normalize_mp_side(side) {
                Some(side) => selected_mp_side = side.into(),
                None => {
                    return Ok((
                        client,
                        Some((
                            role_name.into(),
                            format!("triMpPickSide — unknown MP side '{side}'"),
                        )),
                    ));
                }
            }
            continue;
        }

        if let Some(spec) = stmt.strip_prefix("triMpWaitServerPlayers ") {
            let args: Vec<&str> = spec.split_whitespace().collect();
            if args.len() != 3 {
                return Ok((
                    client,
                    Some((
                        role_name.into(),
                        "triMpWaitServerPlayers requires <host> <port> <min>".into(),
                    )),
                ));
            }
            let host = args[0].to_string();
            let port: u16 = match args[1].parse() {
                Ok(port) => port,
                Err(e) => {
                    return Ok((
                        client,
                        Some((role_name.into(), format!("{stmt} — invalid port: {e}"))),
                    ));
                }
            };
            let min_players: i32 = match args[2].parse() {
                Ok(min_players) => min_players,
                Err(e) => {
                    return Ok((
                        client,
                        Some((
                            role_name.into(),
                            format!("{stmt} — invalid min players: {e}"),
                        )),
                    ));
                }
            };
            let addr = format!("{host}:{port}");
            let mut last_players = -1;
            loop {
                let query_addr = addr.clone();
                let status = tokio::task::spawn_blocking(move || {
                    query_server(query_addr.as_str(), Duration::from_millis(750))
                })
                .await
                .map_err(|e| anyhow::anyhow!("server query task failed: {e}"))?;
                if let Ok(Some(status)) = status {
                    last_players = status.session.num_players;
                    if last_players >= min_players {
                        break;
                    }
                }
                if Instant::now() >= deadline {
                    return Ok((
                        client,
                        Some((
                            role_name.into(),
                            format!("{stmt} — scenario timeout; last players={last_players}"),
                        )),
                    ));
                }
                sleep_until_poll_or_deadline(poll_interval, deadline).await;
            }
            continue;
        }

        if let Some(spec) = stmt.strip_prefix("triMpPickSlot ") {
            let slot = match parse_mp_slot_spec(spec, &selected_mp_side) {
                Ok(slot) => slot,
                Err(e) => return Ok((client, Some((role_name.into(), format!("{stmt} — {e}"))))),
            };
            let eval = format!("triMpAssignSelfSlot \"{slot}\"");
            if let Err(reason) = eval_until_ok(&mut client, &eval, deadline, poll_interval).await? {
                return Ok((
                    client,
                    Some((role_name.into(), format!("{stmt} — {reason}"))),
                ));
            }
            continue;
        }

        if let Some(spec) = stmt.strip_prefix("triMpWaitSlotTaken ") {
            let slot = match parse_mp_slot_spec(spec, &selected_mp_side) {
                Ok(slot) => slot,
                Err(e) => return Ok((client, Some((role_name.into(), format!("{stmt} — {e}"))))),
            };
            let eval = format!("triMpSlotTaken \"{slot}\"");
            if let Err(reason) = eval_until_ok(&mut client, &eval, deadline, poll_interval).await? {
                return Ok((
                    client,
                    Some((role_name.into(), format!("{stmt} — {reason}"))),
                ));
            }
            continue;
        }

        if let Some(state_str) = stmt.strip_prefix("triMpReady") {
            let state_str = state_str.trim();
            let state = if state_str.is_empty() {
                14
            } else {
                match state_str.parse::<i64>() {
                    Ok(state) => state,
                    Err(e) => {
                        return Ok((
                            client,
                            Some((role_name.into(), format!("triMpReady invalid state: {e}"))),
                        ));
                    }
                }
            };
            let eval = format!("triMpClientReady {state}");
            if let Err(reason) = eval_until_ok(&mut client, &eval, deadline, poll_interval).await? {
                return Ok((
                    client,
                    Some((role_name.into(), format!("{stmt} — {reason}"))),
                ));
            }
            continue;
        }

        if let Some(barrier_name) = stmt.strip_prefix("triBarrier ") {
            let barrier_name = barrier_name.trim();
            if barrier_name.is_empty() {
                return Ok((
                    client,
                    Some((role_name.into(), "triBarrier requires name".into())),
                ));
            }
            mark_role_barrier(&barriers, barrier_name, role_name);
            loop {
                if role_barrier_count(&barriers, barrier_name) >= role_count {
                    break;
                }
                if Instant::now() >= deadline {
                    return Ok((
                        client,
                        Some((role_name.into(), format!("{stmt} — scenario timeout"))),
                    ));
                }
                sleep_until_poll_or_deadline(poll_interval, deadline).await;
            }
            continue;
        }

        // triHoldKey <scancode> — key-down with hold flag
        if let Some(sc_str) = stmt.strip_prefix("triHoldKey ") {
            let sc: u32 = sc_str.trim().parse().unwrap_or(0);
            tracing::debug!("[{test_name}] {role_name}: hold key {sc}");
            match tokio::time::timeout(
                time_left(deadline),
                client.send(&Command::Key {
                    sc,
                    r#mod: None,
                    hold: Some(true),
                }),
            )
            .await
            {
                Ok(Ok(_)) => {}
                Ok(Err(e)) => {
                    return Ok((
                        client,
                        Some((role_name.into(), format!("triHoldKey {sc} — {e}"))),
                    ));
                }
                Err(_) => {
                    return Ok((
                        client,
                        Some((
                            role_name.into(),
                            format!("triHoldKey {sc} — scenario timeout"),
                        )),
                    ));
                }
            }
            continue;
        }

        // triReleaseKey <scancode> — key-up
        if let Some(sc_str) = stmt.strip_prefix("triReleaseKey ") {
            let sc: u32 = sc_str.trim().parse().unwrap_or(0);
            tracing::debug!("[{test_name}] {role_name}: release key {sc}");
            match tokio::time::timeout(time_left(deadline), client.send(&Command::KeyUp { sc }))
                .await
            {
                Ok(Ok(_)) => {}
                Ok(Err(e)) => {
                    return Ok((
                        client,
                        Some((role_name.into(), format!("triReleaseKey {sc} — {e}"))),
                    ));
                }
                Err(_) => {
                    return Ok((
                        client,
                        Some((
                            role_name.into(),
                            format!("triReleaseKey {sc} — scenario timeout"),
                        )),
                    ));
                }
            }
            continue;
        }

        // triAssertPlayerMoves <scancode> <hold-ms> <min-distance>
        if let Some(args) = stmt.strip_prefix("triAssertPlayerMoves ") {
            let parts: Vec<&str> = args.split_whitespace().collect();
            if parts.len() != 3 {
                return Ok((
                    client,
                    Some((
                        role_name.into(),
                        format!(
                            "{stmt} — expected: triAssertPlayerMoves <scancode> <hold-ms> <min-distance>"
                        ),
                    )),
                ));
            }
            let sc: u32 = parts[0].parse().unwrap_or(0);
            let hold_ms: u64 = parts[1].parse().unwrap_or(0);
            let min_distance: f64 = parts[2].parse().unwrap_or(0.0);
            let get_number = |state: &serde_json::Map<String, serde_json::Value>, name: &str| {
                state.get(name).and_then(serde_json::Value::as_f64)
            };

            let start = match tokio::time::timeout(
                Duration::from_secs(2),
                client.query("play_state"),
            )
            .await
            {
                Ok(Ok(response)) => response.data,
                Ok(Err(e)) => {
                    return Ok((
                        client,
                        Some((
                            role_name.into(),
                            format!("{stmt} — initial play_state error: {e}"),
                        )),
                    ));
                }
                Err(_) => {
                    return Ok((
                        client,
                        Some((
                            role_name.into(),
                            format!("{stmt} — initial play_state query timeout"),
                        )),
                    ));
                }
            };
            let start_x = get_number(&start, "player_x");
            let start_z = get_number(&start, "player_z");
            let (Some(start_x), Some(start_z)) = (start_x, start_z) else {
                return Ok((
                    client,
                    Some((
                        role_name.into(),
                        format!(
                            "{stmt} — initial play_state has no player position: {}",
                            serde_json::Value::Object(start)
                        ),
                    )),
                ));
            };

            match tokio::time::timeout(
                time_left(deadline),
                client.send(&Command::Key {
                    sc,
                    r#mod: None,
                    hold: Some(true),
                }),
            )
            .await
            {
                Ok(Ok(_)) => {}
                Ok(Err(e)) => {
                    return Ok((
                        client,
                        Some((role_name.into(), format!("{stmt} — hold key error: {e}"))),
                    ));
                }
                Err(_) => {
                    return Ok((
                        client,
                        Some((role_name.into(), format!("{stmt} — hold key timeout"))),
                    ));
                }
            }

            let move_deadline = Instant::now() + Duration::from_millis(hold_ms);
            let mut last_state = serde_json::Value::Object(serde_json::Map::new());
            let mut last_distance = 0.0;
            let mut moved = false;
            while Instant::now() < move_deadline && Instant::now() < deadline {
                match tokio::time::timeout(Duration::from_secs(2), client.query("play_state")).await
                {
                    Ok(Ok(response)) => {
                        let current_x = get_number(&response.data, "player_x").unwrap_or(start_x);
                        let current_z = get_number(&response.data, "player_z").unwrap_or(start_z);
                        last_distance =
                            ((current_x - start_x).powi(2) + (current_z - start_z).powi(2)).sqrt();
                        last_state = serde_json::Value::Object(response.data);
                        if last_distance >= min_distance {
                            moved = true;
                            break;
                        }
                    }
                    Ok(Err(e)) => {
                        let _ = client.send(&Command::KeyUp { sc }).await;
                        return Ok((
                            client,
                            Some((role_name.into(), format!("{stmt} — play_state error: {e}"))),
                        ));
                    }
                    Err(_) => {
                        last_state = serde_json::json!({"error": "play_state query timeout"});
                    }
                }
                sleep_until_poll_or_deadline(poll_interval, deadline).await;
            }

            let release_result =
                tokio::time::timeout(time_left(deadline), client.send(&Command::KeyUp { sc }))
                    .await;
            if !moved {
                return Ok((
                    client,
                    Some((
                        role_name.into(),
                        format!(
                            "{stmt} — moved {last_distance:.3}, expected >= {min_distance:.3}; last play_state={last_state}"
                        ),
                    )),
                ));
            }
            match release_result {
                Ok(Ok(_)) => {}
                Ok(Err(e)) => {
                    return Ok((
                        client,
                        Some((role_name.into(), format!("{stmt} — release key error: {e}"))),
                    ));
                }
                Err(_) => {
                    return Ok((
                        client,
                        Some((role_name.into(), format!("{stmt} — release key timeout"))),
                    ));
                }
            }
            continue;
        }

        // triAssertVonReceived <min> — poll drain_events until ≥ min von_received
        if let Some(min_str) = stmt.strip_prefix("triAssertVonReceived ") {
            let min_count: usize = min_str.trim().parse().unwrap_or(1);
            loop {
                let events = client.drain_events();
                let von_count = events.iter().filter(|e| e.event == "von_received").count();
                von_event_total += von_count;
                if von_event_total >= min_count {
                    tracing::debug!(
                        "[{test_name}] {role_name}: von events = {von_event_total} (>= {min_count})"
                    );
                    break;
                }
                if Instant::now() >= deadline {
                    return Ok((
                        client,
                        Some((
                            role_name.into(),
                            format!(
                                "triAssertVonReceived {min_count} — got {von_event_total} events"
                            ),
                        )),
                    ));
                }
                // Poke the harness with a ping to flush buffered events
                match tokio::time::timeout(time_left(deadline), client.ping()).await {
                    Ok(_) => {}
                    Err(_) => {
                        return Ok((
                            client,
                            Some((role_name.into(), format!("{stmt} — scenario timeout"))),
                        ));
                    }
                }
                sleep_until_poll_or_deadline(poll_interval, deadline).await;
            }
            continue;
        }

        if stmt.trim() == "triResetVonReceived" {
            let events = client.drain_events();
            let drained = events.iter().filter(|e| e.event == "von_received").count();
            if drained > 0 {
                tracing::debug!("[{test_name}] {role_name}: reset {drained} pending von events");
            }
            von_event_total = 0;
            continue;
        }

        // triAssertVonReceivedAtMost <max> — drain accumulated von_received
        // events and fail if more than <max> arrived.  Pair with a preceding
        // triWait long enough to cover the speaker's transmission so a muted
        // sender (which should yield 0) is distinguished from one not yet sent.
        if let Some(max_str) = stmt.strip_prefix("triAssertVonReceivedAtMost ") {
            let max_count: usize = max_str.trim().parse().unwrap_or(0);
            // Flush any buffered events, then count what has accumulated.
            match tokio::time::timeout(time_left(deadline), client.ping()).await {
                Ok(_) => {}
                Err(_) => {
                    return Ok((
                        client,
                        Some((role_name.into(), format!("{stmt} — scenario timeout"))),
                    ));
                }
            }
            let events = client.drain_events();
            von_event_total += events.iter().filter(|e| e.event == "von_received").count();
            if von_event_total > max_count {
                return Ok((
                    client,
                    Some((
                        role_name.into(),
                        format!(
                            "triAssertVonReceivedAtMost {max_count} — got {von_event_total} events"
                        ),
                    )),
                ));
            }
            tracing::debug!(
                "[{test_name}] {role_name}: von events = {von_event_total} (<= {max_count})"
            );
            continue;
        }

        if stmt.trim() == "triLogPlayState" || stmt.starts_with("triLogPlayState ") {
            let label = stmt
                .strip_prefix("triLogPlayState")
                .map(str::trim)
                .filter(|s| !s.is_empty())
                .unwrap_or("snapshot");
            match tokio::time::timeout(Duration::from_secs(2), client.query("play_state")).await {
                Ok(Ok(response)) => {
                    let data = serde_json::Value::Object(response.data);
                    let message = format!("triLogPlayState {label}: {data}");
                    tracing::info!("[{test_name}] {role_name}: {message}");
                    set_role_progress(&progress, role_name, &message);
                }
                Ok(Err(e)) => {
                    tracing::warn!("[{test_name}] {role_name}: triLogPlayState {label}: {e}");
                    set_role_progress(
                        &progress,
                        role_name,
                        &format!("triLogPlayState {label}: {e}"),
                    );
                }
                Err(_) => {
                    tracing::warn!(
                        "[{test_name}] {role_name}: triLogPlayState {label}: query timeout"
                    );
                    set_role_progress(
                        &progress,
                        role_name,
                        &format!("triLogPlayState {label}: query timeout"),
                    );
                }
            }
            continue;
        }

        // triAssertNgs* — poll harness-side ngs query instead of SQF eval so
        // dedicated-server roles can assert state without exposing `eval`.
        let ngs_assert = stmt.strip_prefix("triAssertNgsClient ").map_or_else(
            || {
                stmt.strip_prefix("triAssertNgs ")
                    .map(|expected_str| ("server_state", expected_str))
            },
            |expected_str| Some(("client_state", expected_str)),
        );
        if let Some((field, expected_str)) = ngs_assert {
            let expected: i64 = expected_str.trim().parse().unwrap_or(0);
            let mut timed_out = false;
            let last_state = loop {
                if let Some(reason) = scan_hard_failure_logs(&hard_failure_logs) {
                    return Ok((client, Some((role_name.into(), reason))));
                }
                match tokio::time::timeout(time_left(deadline), client.query("ngs")).await {
                    Ok(Ok(response)) => {
                        let state = response
                            .data
                            .get(field)
                            .and_then(serde_json::Value::as_i64)
                            .unwrap_or(-1);
                        if state >= expected {
                            break Some(state);
                        }
                        if Instant::now() >= deadline {
                            timed_out = true;
                            break Some(state);
                        }
                    }
                    Ok(Err(e)) => {
                        return Ok((
                            client,
                            Some((role_name.into(), format!("{stmt} — connection error: {e}"))),
                        ));
                    }
                    Err(_) => {
                        timed_out = true;
                        break None;
                    }
                }
                sleep_until_poll_or_deadline(poll_interval, deadline).await;
            };
            if timed_out {
                return Ok((
                    client,
                    Some((
                        role_name.into(),
                        format!(
                            "{stmt} — expected {field} >= {expected}, got {}",
                            last_state.unwrap_or(-1)
                        ),
                    )),
                ));
            }
            continue;
        }

        if stmt.trim() == "triAssertPlayState" || stmt.trim() == "triAssertMissionPlayable" {
            let mut timed_out = false;
            let mut first_playable_frame: Option<i64> = None;
            let mut first_playable_time: Option<i64> = None;
            let mut last_state: serde_json::Value;
            let last_state = loop {
                if let Some(reason) = scan_hard_failure_logs(&hard_failure_logs) {
                    return Ok((client, Some((role_name.into(), reason))));
                }
                let query_timeout = time_left(deadline).min(Duration::from_secs(2));
                match tokio::time::timeout(query_timeout, client.query("play_state")).await {
                    Ok(Ok(response)) => {
                        let data = serde_json::Value::Object(response.data.clone());
                        last_state = data.clone();
                        set_role_progress(
                            &progress,
                            role_name,
                            &format!("{stmt} last play_state={data}"),
                        );
                        let display = response
                            .data
                            .get("display")
                            .and_then(serde_json::Value::as_i64)
                            .unwrap_or(-1);
                        let client_state = response
                            .data
                            .get("client_state")
                            .and_then(serde_json::Value::as_i64)
                            .unwrap_or(-1);
                        let frame = response
                            .data
                            .get("frame")
                            .and_then(serde_json::Value::as_i64)
                            .unwrap_or(-1);
                        let time_ms = response
                            .data
                            .get("time_ms")
                            .and_then(serde_json::Value::as_i64)
                            .unwrap_or(-1);
                        let bool_field = |name: &str| {
                            response
                                .data
                                .get(name)
                                .and_then(serde_json::Value::as_bool)
                                .unwrap_or(false)
                        };
                        if client_state >= 14
                            && display == 46
                            && bool_field("in_gameplay")
                            && bool_field("has_role")
                            && bool_field("has_world")
                            && bool_field("has_player")
                            && bool_field("has_brain")
                            && bool_field("has_vehicle")
                            && bool_field("player_local")
                            && bool_field("vehicle_local")
                            && bool_field("player_active")
                            && !bool_field("player_destroyed")
                        {
                            match (first_playable_frame, first_playable_time) {
                                (Some(first_frame), Some(first_time))
                                    if frame > first_frame && time_ms > first_time =>
                                {
                                    break data;
                                }
                                _ => {
                                    first_playable_frame = Some(frame);
                                    first_playable_time = Some(time_ms);
                                }
                            }
                        }
                        if Instant::now() >= deadline {
                            timed_out = true;
                            break data;
                        }
                    }
                    Ok(Err(e)) => {
                        return Ok((
                            client,
                            Some((role_name.into(), format!("{stmt} — connection error: {e}"))),
                        ));
                    }
                    Err(_) => {
                        last_state = serde_json::json!({"error": "play_state query timeout"});
                        set_role_progress(
                            &progress,
                            role_name,
                            &format!("{stmt} last play_state={last_state}"),
                        );
                    }
                }
                if Instant::now() >= deadline {
                    timed_out = true;
                    break last_state;
                }
                sleep_until_poll_or_deadline(poll_interval, deadline).await;
            };
            if timed_out {
                return Ok((
                    client,
                    Some((
                        role_name.into(),
                        format!("{stmt} — last play_state={last_state}"),
                    )),
                ));
            }
            continue;
        }

        if let Some(code) = stmt.strip_prefix("triExec ") {
            match tokio::time::timeout(time_left(deadline), client.exec(code)).await {
                Ok(Ok(())) => {}
                Ok(Err(e)) => {
                    return Ok((client, Some((role_name.into(), format!("{stmt} — {e}")))));
                }
                Err(_) => {
                    return Ok((
                        client,
                        Some((role_name.into(), format!("{stmt} — scenario timeout"))),
                    ));
                }
            }
            continue;
        }

        // Standard eval: triAssert* and role assignment get retry-polled,
        // everything else is fire-and-forget.
        let is_assert = stmt.starts_with("triAssert")
            || stmt.starts_with("triMpAssignSelf")
            || stmt.starts_with("triMpSlotTaken");
        let mut last_result = String::new();
        let mut succeeded = false;

        loop {
            let remaining = time_left(deadline);
            if remaining.is_zero() {
                break;
            }
            match tokio::time::timeout(remaining, client.eval(stmt)).await {
                Ok(Ok(result)) => {
                    let result_str = result.clone().trim_matches('"').to_string();
                    if is_assert {
                        if result_str == "OK" {
                            succeeded = true;
                            break;
                        }
                        last_result = result_str;
                        if Instant::now() >= deadline {
                            break;
                        }
                        sleep_until_poll_or_deadline(poll_interval, deadline).await;
                    } else {
                        succeeded = true;
                        break;
                    }
                }
                Ok(Err(e)) => {
                    if stmt == "triEndTest" {
                        succeeded = true;
                        break;
                    }
                    last_result = if is_assert && !last_result.is_empty() {
                        format!("connection error: {e}; last result: {last_result}")
                    } else {
                        format!("connection error: {e}")
                    };
                    break;
                }
                Err(_) => {
                    if is_assert && !last_result.is_empty() {
                        last_result = format!("scenario timeout; last result: {last_result}");
                    } else {
                        last_result = "scenario timeout".into();
                    }
                    break;
                }
            }
        }

        if is_assert && !succeeded {
            tracing::debug!("[{test_name}] {role_name}: {stmt} FAILED — {last_result}");
            return Ok((
                client,
                Some((role_name.to_string(), format!("{stmt} — {last_result}"))),
            ));
        }
        if !succeeded {
            return Ok((
                client,
                Some((role_name.to_string(), format!("{stmt} — {last_result}"))),
            ));
        }
    }
    clear_role_progress(&progress, role_name);
    tracing::debug!("[{test_name}] {role_name}: all assertions passed");
    Ok((client, None))
}

/// Run a multi-instance test directory.
///
/// Flow:
/// 1. Parse `test.toml` for instance definitions
/// 2. Spawn instances in order, respecting `after` / `wait_ngs` dependencies
/// 3. Once all instances are up, exec each role's SQF script
/// 4. Wait for all to exit — test passes if every instance exits 0
#[allow(clippy::too_many_lines, clippy::cognitive_complexity)]
pub async fn run_multi_test(
    name: &str,
    test_dir: &Path,
    game_dir: &str,
    data_dir: Option<&str>,
    output_dir: &Path,
    render: Option<&str>,
    cli_game_args: &[String],
) -> Result<ScenarioResult> {
    let start = Instant::now();
    let test_output_dir = output_dir.join(safe_output_name(name));
    std::fs::create_dir_all(&test_output_dir)
        .with_context(|| format!("failed to create {}", test_output_dir.display()))?;

    // 1. Parse test.toml
    let toml_path = test_dir.join("test.toml");
    let toml_content = std::fs::read_to_string(&toml_path)
        .with_context(|| format!("failed to read {}", toml_path.display()))?;
    let config: MultiTestConfig = toml::from_str(&toml_content)
        .with_context(|| format!("failed to parse {}", toml_path.display()))?;

    let effective_data_dir = config.data_dir.as_deref().or(data_dir);

    if config.instances.is_empty() {
        return Ok(ScenarioResult {
            passed: false,
            message: "no instances defined in test.toml".into(),
            ..Default::default()
        });
    }

    let effective_cfg = crate::config::get().clone();
    let timeout = config
        .timeout
        .map_or(effective_cfg.timeout, Duration::from_secs);
    let scenario_deadline = start + timeout;
    let client_config = config.timeout.map_or_else(
        || effective_cfg.client_config(),
        |timeout_secs| crate::config::TridentConfig::new(timeout_secs).client_config(),
    );

    // Resolve mission path once
    let resolved_mission = resolve_mission_path(config.mission.as_deref());
    let game_port = if config.game_port == 0 {
        reserve_local_port()?
    } else {
        config.game_port
    };
    let mut replacements = match required_environment_replacements(&config.required_env) {
        Ok(replacements) => replacements,
        Err(error) => {
            return Ok(ScenarioResult {
                passed: false,
                message: format!("[config] {error:#} ({:.1}s)", start.elapsed().as_secs_f64()),
                duration: start.elapsed(),
            });
        }
    };
    replacements.insert(
        "output".to_string(),
        test_output_dir.to_string_lossy().to_string(),
    );
    replacements.insert("ports.game".to_string(), game_port.to_string());
    replacements.insert("game.port".to_string(), game_port.to_string());

    let mut services = match start_services(
        name,
        &config.services,
        &test_output_dir,
        &mut replacements,
        timeout,
    )
    .await
    {
        Ok(services) => services,
        Err(e) => {
            return Ok(ScenarioResult {
                passed: false,
                message: format!("[service] {e} ({:.1}s)", start.elapsed().as_secs_f64()),
                duration: start.elapsed(),
            });
        }
    };

    // 2. Spawn all instances in order, respecting dependencies
    let mut instances: Vec<(String, GameInstance)> = Vec::new();
    let mut failed_instance: Option<(String, String)> = None;
    let mut hard_failure_log_paths: Vec<PathBuf> = Vec::new();
    for inst in &config.instances {
        let harness_port = 0u16; // OS auto-assigns

        // Wait for dependency if specified
        if let Some(ref after_name) = inst.after {
            let target_ngs = inst.wait_ngs.or(inst.wait_ngs_client);
            let dep = instances
                .iter_mut()
                .find(|(n, _)| n == after_name)
                .map(|(_, g)| g);
            if dep.is_none() {
                let known_service = services.iter().any(|service| &service.name == after_name);
                if !known_service || target_ngs.is_some() {
                    failed_instance = Some((
                        inst.name.clone(),
                        format!("depends on '{after_name}' which hasn't been started as a game instance"),
                    ));
                    break;
                }
            }

            if let Some(target) = target_ngs {
                let dep = dep.expect("target_ngs requires a game instance dependency");
                let ngs_field = if inst.wait_ngs.is_some() {
                    "server_state"
                } else {
                    "client_state"
                };
                tracing::debug!("[{name}] waiting for {after_name} {ngs_field} >= {target}");
                loop {
                    match dep.client().query("ngs").await {
                        Ok(resp) => {
                            let state = resp
                                .data
                                .get(ngs_field)
                                .and_then(serde_json::Value::as_i64)
                                .unwrap_or(-1);
                            if state >= target {
                                tracing::debug!(
                                    "[{name}] {after_name} {ngs_field} = {state} (>= {target})"
                                );
                                break;
                            }
                            if Instant::now() >= scenario_deadline {
                                failed_instance = Some((
                                    inst.name.clone(),
                                    format!(
                                        "timeout: {after_name} {ngs_field} >= {target} (got {state})"
                                    ),
                                ));
                                break;
                            }
                        }
                        Err(e) => {
                            failed_instance = Some((
                                inst.name.clone(),
                                format!("dependency {after_name} query failed: {e}"),
                            ));
                            break;
                        }
                    }
                    tokio::time::sleep(effective_cfg.poll_interval).await;
                }
                if failed_instance.is_some() {
                    break;
                }
            }
        }

        if let Some(delay_ms) = inst.start_delay_ms {
            tracing::debug!("[{name}] delaying '{}' start by {delay_ms}ms", inst.name);
            tokio::time::sleep(Duration::from_millis(delay_ms)).await;
        }

        // Build CLI args
        let mut extra_args: Vec<String> = if inst.is_server() {
            // Dedicated server is its own binary (PoseidonServer, selected at spawn);
            // it is inherently a server, so no --server flag.
            vec!["--nosound".into(), "--focus".into()]
        } else {
            let mut args = vec![
                "--window".into(),
                "--no-splash".into(),
                "--nosound".into(),
                "--focus".into(),
            ];
            if !inst.no_autotest {
                args.push("--autotest".into());
            }
            args
        };
        extra_args.push("--port".into());
        extra_args.push(inst.network_port.unwrap_or(game_port).to_string());

        if let Some(connect_port) = inst.connect_port {
            extra_args.push("--connect-port".into());
            extra_args.push(connect_port.to_string());
        }

        let instance_mission = if inst.use_mission {
            inst.mission.as_deref().map_or_else(
                || resolved_mission.clone(),
                |mission| resolve_mission_path(Some(mission)),
            )
        } else {
            None
        };
        if let Some(ref mission) = instance_mission {
            if inst.is_server() {
                extra_args.push("--simulate".into());
            } else {
                extra_args.push("--test-mission".into());
            }
            extra_args.push(mission.clone());
        }
        if inst.connect.is_some() {
            extra_args.push("--connect".into());
            extra_args.push(inst.connect_host.clone().map_or_else(
                || "127.0.0.1".into(),
                |value| expand_placeholders(&value, &replacements),
            ));
        }
        if let Some(ref assign) = inst.mp_assign {
            extra_args.push("--mp-assign".into());
            extra_args.push(expand_placeholders(assign, &replacements));
        }
        if !inst.is_server() {
            if let Some(r) = config.render.as_deref().or(render) {
                extra_args.push("--render".into());
                extra_args.push(r.into());
            }
        }
        extra_args.extend(expand_placeholders_vec(&inst.extra_args, &replacements));
        if !inst.is_server() {
            extra_args.extend(cli_game_args.iter().cloned());
        }
        let extra_refs: Vec<&str> = extra_args.iter().map(String::as_str).collect();

        // Per-instance output subdir so logs don't overwrite each other
        let inst_output = test_output_dir.join(&inst.name);
        hard_failure_log_paths.push(inst_output.join("game_stdout.log"));
        hard_failure_log_paths.push(inst_output.join("game_stderr.log"));
        let user_dir = inst_output.join("user");
        std::fs::create_dir_all(&user_dir)
            .with_context(|| format!("failed to create {}", user_dir.display()))?;
        for copy in &inst.copy_files {
            let source_rel = checked_relative_file_path(&copy.source)?;
            let target_rel = checked_relative_file_path(&copy.target)?;
            let source = test_dir.join(source_rel);
            let target = user_dir.join(target_rel);
            if let Some(parent) = target.parent() {
                std::fs::create_dir_all(parent)
                    .with_context(|| format!("failed to create {}", parent.display()))?;
            }
            std::fs::copy(&source, &target).with_context(|| {
                format!(
                    "failed to copy {} to {}",
                    source.display(),
                    target.display()
                )
            })?;
        }
        let user_dir_str = user_dir.to_string_lossy().to_string();
        let output_str = inst_output.to_string_lossy().to_string();
        let machine_id = inst.machine_id.as_ref().map_or_else(
            || format!("trident:{name}:{}", inst.name),
            |value| expand_placeholders(value, &replacements),
        );
        let extra_env: Vec<(String, String)> = inst
            .env
            .iter()
            .map(|e| (e.name.clone(), expand_placeholders(&e.value, &replacements)))
            .collect();
        let mut env_vars: Vec<(&str, &str)> = vec![
            ("POSEIDON_USER_DIR", user_dir_str.as_str()),
            ("POSEIDON_CACHE_DIR", user_dir_str.as_str()),
            ("POSEIDON_TEMP_DIR", user_dir_str.as_str()),
            ("POSEIDON_TEST_MACHINE_ID", machine_id.as_str()),
            ("TRI_OUTPUT_DIR", output_str.as_str()),
            ("SDL_VIDEO_MINIMIZE_ON_FOCUS_LOSS", "0"),
        ];
        env_vars.extend(extra_env.iter().map(|(k, v)| (k.as_str(), v.as_str())));

        tracing::debug!(
            "[{name}] spawning '{}' (type={}, port={harness_port})",
            inst.name,
            inst.instance_type
        );

        let instance_cfg = inst.timeout.map_or_else(
            || client_config.clone(),
            |t| crate::config::TridentConfig::new(t).client_config(),
        );
        match GameInstance::spawn_with_env(
            game_dir,
            effective_data_dir,
            harness_port,
            &extra_refs,
            &env_vars,
            if inst.is_server() {
                // Dedicated server has its own binary + harness startup path.
                Some("PoseidonServer")
            } else {
                None
            },
            &instance_cfg,
        )
        .await
        {
            Ok(mut game) => {
                // A dedicated server has no display, so it emits no `ready` event —
                // a successful harness connection already means it finished init.
                if !inst.is_server() {
                    let ready_timeout = time_left(scenario_deadline);
                    match game.wait_ready(ready_timeout).await {
                        Ok(_) => tracing::debug!("[{name}] '{}' ready", inst.name),
                        Err(e) => {
                            failed_instance = Some((
                                inst.name.clone(),
                                format!("timeout waiting for ready: {e}"),
                            ));
                            break;
                        }
                    }
                }
                instances.push((inst.name.clone(), game));
            }
            Err(e) => {
                failed_instance = Some((inst.name.clone(), format!("spawn failed: {e:#}")));
                break;
            }
        }
    }

    // 3. Exec SQF scripts on all instances in parallel (if spawn phase succeeded)
    if failed_instance.is_none() {
        // Collect script data for each role
        let mut role_scripts: Vec<(String, Vec<String>)> = Vec::new();
        for (role_name, _) in &instances {
            let sqf_path = test_dir.join(format!("{role_name}.sqf"));
            if !sqf_path.exists() {
                continue;
            }
            let sqf_code = std::fs::read_to_string(&sqf_path)
                .with_context(|| format!("failed to read {}", sqf_path.display()))?;
            let sqf_code = expand_placeholders(&sqf_code, &replacements);
            let statements = super::integration::parse_statements(&sqf_code);
            role_scripts.push((role_name.clone(), statements));
        }

        // Run all role scripts concurrently — each task drives its own instance
        let poll_interval = effective_cfg.poll_interval;
        let progress: RoleProgress = Arc::new(Mutex::new(HashMap::new()));
        let barriers: RoleBarriers = Arc::new(Mutex::new(HashMap::new()));
        let hard_failure_logs: HardFailureLogs = Arc::new(hard_failure_log_paths.clone());
        let role_count = role_scripts.len();
        let mut handles = tokio::task::JoinSet::new();
        for (role_order, (role_name, statements)) in role_scripts.into_iter().enumerate() {
            let (_, game) = instances.iter_mut().find(|(n, _)| *n == role_name).unwrap();
            let client = game.take_client();
            let test_name = name.to_string();
            let progress = Arc::clone(&progress);
            let barriers = Arc::clone(&barriers);
            let hard_failure_logs = Arc::clone(&hard_failure_logs);
            handles.spawn(async move {
                let (client, failure) = run_role_script(
                    &test_name,
                    &role_name,
                    client,
                    &statements,
                    scenario_deadline,
                    poll_interval,
                    progress,
                    barriers,
                    role_count,
                    hard_failure_logs,
                )
                .await?;
                Ok((role_order, client, failure))
            });
        }

        // Observe completion order so one failed role cannot be hidden behind a
        // different role that is still waiting for its synchronization flag.
        let (mut returned_clients, role_failure) = collect_role_tasks(
            &mut handles,
            scenario_deadline,
            &progress,
            &hard_failure_log_paths,
        )
        .await;
        failed_instance = role_failure;

        // Keep shutdown bounded and reverse launch order so clients leave before
        // the dedicated server even when their role tasks completed out of order.
        for client in returned_clients.iter_mut().rev() {
            let _ = tokio::time::timeout(ROLE_SHUTDOWN_DEADLINE, client.exec("triEndTest")).await;
        }
    }

    let failed = failed_instance.is_some();
    if failed {
        for (_, inst) in instances.iter_mut().rev() {
            inst.kill_after_failure().await;
        }
    } else {
        for (_, inst) in instances.iter_mut().rev() {
            inst.request_end_test().await;
        }

        // 4. Wait for all processes to exit
        for (role_name, inst) in instances.iter_mut().rev() {
            let result = inst.wait_exit(time_left(scenario_deadline)).await;
            if failed_instance.is_none() {
                failed_instance = participant_exit_failure(role_name, result);
            }
        }
    }
    for service in services.iter_mut().rev() {
        service.stop(timeout).await;
    }

    let elapsed = start.elapsed();

    if let Some((role, reason)) = failed_instance {
        Ok(ScenarioResult {
            passed: false,
            message: format!("[{role}] {reason} ({:.1}s)", elapsed.as_secs_f64()),
            duration: elapsed,
        })
    } else {
        Ok(ScenarioResult {
            passed: true,
            message: format!(
                "exit 0, {:.1}s ({})",
                elapsed.as_secs_f64(),
                config
                    .instances
                    .iter()
                    .map(|i| i.name.as_str())
                    .collect::<Vec<_>>()
                    .join("+")
            ),
            duration: elapsed,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn expand_placeholders_replaces_known_values() {
        let mut values = HashMap::new();
        values.insert(
            "master.url".to_string(),
            "http://127.0.0.1:12345".to_string(),
        );
        values.insert("ports.game".to_string(), "23456".to_string());

        let expanded = expand_placeholders(
            "--master-server=${master.url} --port=${ports.game}",
            &values,
        );

        assert_eq!(
            expanded,
            "--master-server=http://127.0.0.1:12345 --port=23456"
        );
    }

    #[test]
    fn expand_placeholders_preserves_unknown_values() {
        let values = HashMap::new();

        assert_eq!(
            expand_placeholders("${missing.value}", &values),
            "${missing.value}"
        );
    }

    #[test]
    fn multi_test_config_accepts_data_dir_override() {
        let config: MultiTestConfig = toml::from_str(
            r#"
mission = "tests/integration/missions/jip_basic.Demo"
data_dir = "packages/Demo"
game_port = 0
required_env = ["CWR_MODS_SOURCE_DIR"]

[[instances]]
name = "server"
type = "server"
"#,
        )
        .unwrap();

        assert_eq!(config.data_dir.as_deref(), Some("packages/Demo"));
        assert_eq!(config.required_env, ["CWR_MODS_SOURCE_DIR"]);
    }

    #[test]
    fn required_environment_values_expand_only_when_declared() {
        let required = vec!["CWR_MODS_SOURCE_DIR".to_string()];
        let replacements = required_environment_replacements_with(&required, |name| {
            (name == "CWR_MODS_SOURCE_DIR").then(|| "C:/cwr mods".to_string())
        })
        .unwrap();

        assert_eq!(
            expand_placeholders("--mods-dir=${env.CWR_MODS_SOURCE_DIR}", &replacements),
            "--mods-dir=C:/cwr mods"
        );
        assert_eq!(
            expand_placeholders("${env.UNDECLARED}", &replacements),
            "${env.UNDECLARED}"
        );
    }

    #[test]
    fn required_environment_values_reject_missing_empty_and_invalid_names() {
        let missing = vec!["CWR_MODS_SOURCE_DIR".to_string()];
        let error = required_environment_replacements_with(&missing, |_| None).unwrap_err();
        assert!(error.to_string().contains("CWR_MODS_SOURCE_DIR"));

        let error =
            required_environment_replacements_with(&missing, |_| Some(String::new())).unwrap_err();
        assert!(error.to_string().contains("is empty"));

        for invalid in ["", "9INVALID", "INVALID-NAME"] {
            let error = required_environment_replacements_with(&[invalid.to_string()], |_| {
                Some("value".to_string())
            })
            .unwrap_err();
            assert!(error.to_string().contains("invalid required environment"));
        }
    }

    #[tokio::test]
    async fn role_failure_is_not_blocked_by_an_earlier_pending_role() {
        let mut handles = tokio::task::JoinSet::new();
        handles.spawn(async {
            tokio::time::sleep(Duration::from_secs(30)).await;
            Ok((0, "pending", None))
        });
        handles.spawn(async {
            Ok((
                1,
                "failed",
                Some(("client2".to_string(), "movement failed".to_string())),
            ))
        });

        let progress: RoleProgress = Arc::new(Mutex::new(HashMap::new()));
        let (returned, failure) = collect_role_tasks(
            &mut handles,
            Instant::now() + Duration::from_secs(2),
            &progress,
            &[],
        )
        .await;

        assert_eq!(returned, ["failed"]);
        assert_eq!(
            failure,
            Some(("client2".to_string(), "movement failed".to_string()))
        );
        assert!(handles.is_empty());
    }

    #[tokio::test]
    async fn role_clients_are_returned_in_launch_order() {
        let mut handles = tokio::task::JoinSet::new();
        handles.spawn(async {
            tokio::time::sleep(Duration::from_millis(25)).await;
            Ok((0, "server", None))
        });
        handles.spawn(async { Ok((1, "client1", None)) });
        handles.spawn(async {
            tokio::time::sleep(Duration::from_millis(5)).await;
            Ok((2, "client2", None))
        });

        let progress: RoleProgress = Arc::new(Mutex::new(HashMap::new()));
        let (returned, failure) = collect_role_tasks(
            &mut handles,
            Instant::now() + Duration::from_secs(1),
            &progress,
            &[],
        )
        .await;

        assert_eq!(returned, ["server", "client1", "client2"]);
        assert_eq!(failure, None);
    }

    #[test]
    fn gui_slot_count_ignores_headless_dedicated_participants() {
        let dir = tempfile::tempdir().unwrap();
        std::fs::write(
            dir.path().join("test.toml"),
            r#"
[[instances]]
name = "dedicated"
type = "server"
extra_args = ["--nosound"]

[[instances]]
name = "client1"
type = "client"

[[instances]]
name = "client2"
type = "client"
"#,
        )
        .unwrap();

        assert_eq!(instance_count(dir.path()), 3);
        assert_eq!(gui_instance_count(dir.path()), 2);
    }

    #[test]
    fn gui_slot_count_keeps_one_slot_for_server_only_tests() {
        let dir = tempfile::tempdir().unwrap();
        std::fs::write(
            dir.path().join("test.toml"),
            r#"
[[instances]]
name = "dedicated"
type = "server"
"#,
        )
        .unwrap();

        assert_eq!(gui_instance_count(dir.path()), 1);
    }

    #[test]
    fn legacy_network_cleanup_removes_only_shared_asset_caches() {
        let dir = tempfile::tempdir().unwrap();
        let squads = dir.path().join("tmp/squads");
        let players = dir.path().join("tmp/players");
        let preserved = dir.path().join("tmp/other");
        for path in [&squads, &players, &preserved] {
            std::fs::create_dir_all(path).unwrap();
            std::fs::write(path.join("asset.bin"), b"test").unwrap();
        }

        clear_legacy_network_asset_tmp(dir.path()).unwrap();

        assert!(!squads.exists());
        assert!(!players.exists());
        assert!(preserved.exists());
    }

    #[test]
    fn parse_mp_slot_spec_uses_selected_side_for_numbered_slots() {
        assert_eq!(parse_mp_slot_spec("2", "WEST").unwrap(), "WEST:2");
        assert_eq!(parse_mp_slot_spec("1", "RES").unwrap(), "RES:1");
    }

    #[test]
    fn parse_mp_slot_spec_accepts_explicit_side() {
        assert_eq!(parse_mp_slot_spec("east:3", "WEST").unwrap(), "EAST:3");
        assert_eq!(parse_mp_slot_spec("civilian:4", "WEST").unwrap(), "CIV:4");
    }

    #[test]
    fn parse_mp_slot_spec_rejects_bad_slots() {
        assert!(parse_mp_slot_spec("0", "WEST").is_err());
        assert!(parse_mp_slot_spec("WEST:nope", "WEST").is_err());
        assert!(parse_mp_slot_spec("UNKNOWN:1", "WEST").is_err());
    }

    #[test]
    fn abnormal_participant_exit_is_a_scenario_failure() {
        let failure = participant_exit_failure("client2", Ok(44)).unwrap();

        assert_eq!(failure.0, "client2");
        assert!(failure.1.contains("exit code 44"), "{}", failure.1);
    }

    #[test]
    fn participant_wait_error_is_a_scenario_failure() {
        let failure =
            participant_exit_failure("server", Err(anyhow::anyhow!("wait timed out"))).unwrap();

        assert_eq!(failure.0, "server");
        assert!(failure.1.contains("wait timed out"), "{}", failure.1);
    }

    #[test]
    fn clean_participant_exit_is_accepted() {
        assert!(participant_exit_failure("client1", Ok(0)).is_none());
    }
}
