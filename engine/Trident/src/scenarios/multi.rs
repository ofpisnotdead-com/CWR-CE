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
use std::collections::HashMap;
use std::fs::File;
use std::io::Write;
use std::net::TcpListener;
use std::path::{Component, Path, PathBuf};
use std::process::Stdio;
use std::time::{Duration, Instant};
use tokio::process::{Child, Command as ProcessCommand};

/// Parsed multi-instance test configuration from `test.toml`.
#[derive(serde::Deserialize)]
pub struct MultiTestConfig {
    /// Shared mission path (relative to repo root)
    #[serde(default)]
    pub mission: Option<String>,

    /// Game network port (default 2302)
    #[serde(default = "default_game_port")]
    pub game_port: u16,

    /// Per-test timeout override in seconds.
    #[serde(default)]
    pub timeout: Option<u64>,

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

    /// Start this role automatically when the scenario begins.
    #[serde(default = "default_true")]
    pub autostart: bool,

    /// Set true to suppress the default `--autotest` injection for client instances.
    #[serde(default)]
    pub no_autotest: bool,
}

#[derive(Clone, serde::Deserialize)]
pub struct InstanceCopyFileConfig {
    pub source: String,
    pub target: String,
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

    /// SQLite database path for master-server.
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
    let listener =
        TcpListener::bind(("127.0.0.1", 0)).context("failed to reserve ephemeral port")?;
    Ok(listener
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

fn resolve_repo_path(value: &str, replacements: &HashMap<String, String>) -> PathBuf {
    let expanded = expand_placeholders(value, replacements);
    let path = PathBuf::from(expanded);
    if path.is_absolute() {
        path
    } else {
        repo_root().join(path)
    }
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

        let mod_dir = mods_dir.join(&seed.id);
        std::fs::create_dir_all(&mod_dir)
            .with_context(|| format!("creating seed mod store {}", mod_dir.display()))?;

        let mut raw = Vec::new();
        pbo.write(&mut raw)
            .with_context(|| format!("serialising seed mod '{}'", seed.id))?;
        let archive_path = mod_dir.join(format!("{}.pbo.zst", seed.id));
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
            "name": seed.name,
            "version": seed.version,
            "folderName": seed.folder,
            "description": seed.description,
            "authors": seed.authors,
            "homepageUrl": seed.homepage_url,
            "downloadUrl": format!("/v1/mods/{}/download", seed.id),
            "sizeBytes": size_bytes
        });
        let metadata_path = mod_dir.join("mod.json");
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
    let candidates = [
        root.join("mserver/MasterService/target/debug/papa-bear-master-service"),
        root.join("mserver/MasterService/target/release/papa-bear-master-service"),
    ];
    candidates
        .into_iter()
        .find(|path| path.exists())
        .with_context(|| {
            "papa-bear-master-service was not found; build it with \
             `cargo build --manifest-path mserver/MasterService/Cargo.toml`"
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
        .map(|port| format!("127.0.0.1:{port}"))
        .unwrap_or_else(|| listen.to_string());
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

async fn spawn_service_process(
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
                let listen = service
                    .listen
                    .as_deref()
                    .map(|value| expand_placeholders(value, replacements))
                    .unwrap_or_else(|| format!("127.0.0.1:{port}"));
                let url = service_url_from_listen(&listen);
                let db = service
                    .db
                    .as_deref()
                    .map(|value| expand_placeholders(value, replacements))
                    .unwrap_or_else(|| {
                        output_dir
                            .join(format!("{}.sqlite3", service.name))
                            .to_string_lossy()
                            .to_string()
                    });
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
                    spawn_service_process(test_name, service, &args, output_dir, replacements)
                        .await?;
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
                    spawn_service_process(test_name, service, &args, output_dir, replacements)
                        .await?;
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

fn safe_output_name(name: &str) -> String {
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
        anyhow::bail!(
            "path '{}' must be a relative file path without parent traversal",
            value
        );
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

/// Run a single role's SQF script against its harness client.
///
/// Supports orchestrator-side directives alongside eval'd SQF:
/// - `triWait <ms>` — sleep
/// - `triHoldKey <scancode>` — send key-down with hold
/// - `triReleaseKey <scancode>` — send key-up
/// - `triAssertVonReceived <min>` — poll for ≥ min `von_received` events
/// - `triAssertNgs <min>` / `triAssertNgsClient <min>` — poll harness `query ngs`
/// - `triAssert*` — eval via harness, retry until "OK" or timeout
/// - anything else — eval via harness (fire-and-forget)
///
/// Returns the client back along with the result: `None` on success, `Some((role, reason))` on failure.
#[allow(clippy::too_many_lines, clippy::cognitive_complexity)]
pub(super) async fn run_role_script(
    test_name: &str,
    role_name: &str,
    mut client: HarnessClient,
    statements: &[String],
    timeout: Duration,
    poll_interval: Duration,
) -> Result<(HarnessClient, Option<(String, String)>)> {
    let mut von_event_total: usize = 0;

    for stmt in statements {
        // triWait <ms> — orchestrator-side sleep
        if let Some(ms_str) = stmt.strip_prefix("triWait ") {
            if let Ok(ms) = ms_str.trim().parse::<u64>() {
                tokio::time::sleep(Duration::from_millis(ms)).await;
            }
            continue;
        }

        // triHoldKey <scancode> — key-down with hold flag
        if let Some(sc_str) = stmt.strip_prefix("triHoldKey ") {
            let sc: u32 = sc_str.trim().parse().unwrap_or(0);
            tracing::debug!("[{test_name}] {role_name}: hold key {sc}");
            if let Err(e) = client
                .send(&Command::Key {
                    sc,
                    r#mod: None,
                    hold: Some(true),
                })
                .await
            {
                return Ok((
                    client,
                    Some((role_name.into(), format!("triHoldKey {sc} — {e}"))),
                ));
            }
            continue;
        }

        // triReleaseKey <scancode> — key-up
        if let Some(sc_str) = stmt.strip_prefix("triReleaseKey ") {
            let sc: u32 = sc_str.trim().parse().unwrap_or(0);
            tracing::debug!("[{test_name}] {role_name}: release key {sc}");
            if let Err(e) = client.send(&Command::KeyUp { sc }).await {
                return Ok((
                    client,
                    Some((role_name.into(), format!("triReleaseKey {sc} — {e}"))),
                ));
            }
            continue;
        }

        // triAssertVonReceived <min> — poll drain_events until ≥ min von_received
        if let Some(min_str) = stmt.strip_prefix("triAssertVonReceived ") {
            let min_count: usize = min_str.trim().parse().unwrap_or(1);
            let deadline = Instant::now() + timeout;
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
                let _ = client.ping().await;
                tokio::time::sleep(poll_interval).await;
            }
            continue;
        }

        // triAssertVonReceivedAtMost <max> — drain accumulated von_received
        // events and fail if more than <max> arrived.  Pair with a preceding
        // triWait long enough to cover the speaker's transmission so a muted
        // sender (which should yield 0) is distinguished from one not yet sent.
        if let Some(max_str) = stmt.strip_prefix("triAssertVonReceivedAtMost ") {
            let max_count: usize = max_str.trim().parse().unwrap_or(0);
            // Flush any buffered events, then count what has accumulated.
            let _ = client.ping().await;
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
            let deadline = Instant::now() + timeout;
            let mut timed_out = false;
            let last_state = loop {
                match client.query("ngs").await {
                    Ok(response) => {
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
                    Err(e) => {
                        return Ok((
                            client,
                            Some((role_name.into(), format!("{stmt} — connection error: {e}"))),
                        ));
                    }
                }
                tokio::time::sleep(poll_interval).await;
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

        // Standard eval: triAssert* gets retry-polled, everything else fire-and-forget
        let is_assert = stmt.starts_with("triAssert");
        let assert_deadline = Instant::now() + timeout;
        let mut last_result = String::new();
        let mut succeeded = false;

        loop {
            match client.eval(stmt).await {
                Ok(result) => {
                    let result_str = result.clone().trim_matches('"').to_string();
                    if is_assert {
                        if result_str == "OK" {
                            succeeded = true;
                            break;
                        }
                        last_result = result_str;
                        if Instant::now() >= assert_deadline {
                            break;
                        }
                        tokio::time::sleep(poll_interval).await;
                    } else {
                        succeeded = true;
                        break;
                    }
                }
                Err(e) => {
                    if stmt == "triEndTest" {
                        succeeded = true;
                        break;
                    }
                    last_result = format!("connection error: {e}");
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
        .map(Duration::from_secs)
        .unwrap_or(effective_cfg.timeout);
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
    let mut replacements = HashMap::new();
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
                        format!(
                            "depends on '{}' which hasn't been started as a game instance",
                            after_name
                        ),
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
                let deadline = Instant::now() + timeout;
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
                            if Instant::now() >= deadline {
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
            if let Some(mission) = inst.mission.as_deref() {
                resolve_mission_path(Some(mission))
            } else {
                resolved_mission.clone()
            }
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
            extra_args.push(
                inst.connect_host
                    .clone()
                    .map(|value| expand_placeholders(&value, &replacements))
                    .unwrap_or_else(|| "127.0.0.1".into()),
            );
        }
        if let Some(ref assign) = inst.mp_assign {
            extra_args.push("--mp-assign".into());
            extra_args.push(expand_placeholders(assign, &replacements));
        }
        if let Some(r) = render {
            extra_args.push("--render".into());
            extra_args.push(r.into());
        }
        extra_args.extend(expand_placeholders_vec(&inst.extra_args, &replacements));
        extra_args.extend(cli_game_args.iter().cloned());
        let extra_refs: Vec<&str> = extra_args.iter().map(String::as_str).collect();

        // Per-instance output subdir so logs don't overwrite each other
        let inst_output = test_output_dir.join(&inst.name);
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
        let machine_id = format!("trident:{name}:{}", inst.name);
        let env_vars = [
            ("POSEIDON_USER_DIR", user_dir_str.as_str()),
            ("POSEIDON_CACHE_DIR", user_dir_str.as_str()),
            ("POSEIDON_TEST_MACHINE_ID", machine_id.as_str()),
            ("TRI_OUTPUT_DIR", output_str.as_str()),
            ("SDL_VIDEO_MINIMIZE_ON_FOCUS_LOSS", "0"),
        ];

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
            data_dir,
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
                    match game.wait_ready(timeout).await {
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
                failed_instance = Some((inst.name.clone(), format!("spawn failed: {e}")));
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
        let mut handles = Vec::new();
        for (role_name, statements) in role_scripts {
            let (_, game) = instances.iter_mut().find(|(n, _)| *n == role_name).unwrap();
            let client = game.take_client();
            let test_name = name.to_string();
            handles.push(tokio::spawn(async move {
                run_role_script(
                    &test_name,
                    &role_name,
                    client,
                    &statements,
                    timeout,
                    poll_interval,
                )
                .await
            }));
        }

        // Wait for all role tasks to complete, collecting returned clients
        let mut returned_clients: Vec<HarnessClient> = Vec::new();
        for handle in handles {
            match handle.await {
                Ok(Ok((client, Some((role, reason))))) => {
                    returned_clients.push(client);
                    if failed_instance.is_none() {
                        failed_instance = Some((role, reason));
                    }
                }
                Ok(Ok((client, None))) => {
                    returned_clients.push(client);
                }
                Ok(Err(e)) => {
                    if failed_instance.is_none() {
                        failed_instance = Some(("orchestrator".into(), format!("{e}")));
                    }
                }
                Err(e) => {
                    if failed_instance.is_none() {
                        failed_instance = Some(("orchestrator".into(), format!("task panic: {e}")));
                    }
                }
            }
        }

        // Send triEndTest via the returned clients (reverse order)
        for client in returned_clients.iter_mut().rev() {
            let _ = client.exec("triEndTest").await;
        }
    }

    for (_, inst) in instances.iter_mut().rev() {
        inst.request_end_test().await;
    }

    // 4. Wait for all processes to exit
    for (_, inst) in instances.iter_mut().rev() {
        let _ = inst.wait_exit(timeout).await;
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
}
