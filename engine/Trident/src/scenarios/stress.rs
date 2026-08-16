//! Long-running multiplayer stress runner built on Trident's harness plumbing.

use crate::client::{GameInstance, HarnessClient};
use crate::scenarios::ScenarioResult;
use anyhow::{Context, Result};
use serde::Serialize;
use std::collections::HashMap;
use std::io::Write;
use std::net::SocketAddr;
use std::path::{Path, PathBuf};
use std::process::Stdio;
use std::time::{Duration, Instant};
use tokio::process::{Child, Command};

const DEFAULT_TOXIPROXY_HOST: &str = "127.0.0.1";
const DEFAULT_TOXIPROXY_PORT: u16 = 8474;
const DEFAULT_PROBE_INTERVAL_MS: u64 = 1000;

#[derive(serde::Deserialize)]
struct StressConfig {
    #[serde(default)]
    mission: Option<String>,
    #[serde(default = "default_game_port")]
    game_port: u16,
    instances: Vec<super::multi::InstanceConfig>,
    phases: Vec<StressPhaseConfig>,
    #[serde(default)]
    probes: ProbeConfig,
    #[serde(default)]
    toxiproxy: ToxiproxyConfig,
}

const fn default_game_port() -> u16 {
    2302
}

#[derive(Default, serde::Deserialize)]
struct ProbeConfig {
    #[serde(default = "default_probe_interval_ms")]
    interval_ms: u64,
    #[serde(default = "default_probe_queries")]
    queries: Vec<String>,
    #[serde(default)]
    evals: Vec<EvalProbeConfig>,
}

const fn default_probe_interval_ms() -> u64 {
    DEFAULT_PROBE_INTERVAL_MS
}

fn default_probe_queries() -> Vec<String> {
    vec!["ngs".into(), "players".into(), "von_state".into()]
}

#[derive(serde::Deserialize)]
struct EvalProbeConfig {
    role: String,
    name: String,
    code: String,
    #[serde(default)]
    assert_ok: bool,
}

#[derive(serde::Deserialize)]
struct StressPhaseConfig {
    name: String,
    duration_ms: u64,
    #[serde(default)]
    before: Vec<PhaseHookConfig>,
    #[serde(default)]
    after: Vec<PhaseHookConfig>,
    #[serde(default)]
    before_actions: Vec<PhaseActionConfig>,
    #[serde(default)]
    after_actions: Vec<PhaseActionConfig>,
    #[serde(default)]
    toxics: Vec<ToxicConfig>,
}

#[derive(serde::Deserialize)]
struct PhaseHookConfig {
    role: String,
    script: String,
}

#[derive(serde::Deserialize)]
struct PhaseActionConfig {
    action: String,
    role: String,
    #[serde(default)]
    script: Option<String>,
}

#[derive(Default, serde::Deserialize)]
struct ToxiproxyConfig {
    #[serde(default)]
    enabled: bool,
    #[serde(default = "default_fault_proxy_backend")]
    backend: String,
    #[serde(default = "default_toxiproxy_host")]
    host: String,
    #[serde(default = "default_toxiproxy_port")]
    port: u16,
    #[serde(default)]
    links: Vec<ToxiproxyLinkConfig>,
}

fn default_toxiproxy_host() -> String {
    DEFAULT_TOXIPROXY_HOST.into()
}

fn default_fault_proxy_backend() -> String {
    "toxiproxy".into()
}

const fn default_toxiproxy_port() -> u16 {
    DEFAULT_TOXIPROXY_PORT
}

#[derive(Clone, serde::Deserialize)]
struct ToxiproxyLinkConfig {
    name: String,
    listen: String,
    upstream: String,
    #[serde(default = "default_true")]
    enabled: bool,
}

const fn default_true() -> bool {
    true
}

#[derive(serde::Deserialize)]
struct ToxicConfig {
    link: String,
    name: String,
    #[serde(rename = "type")]
    toxic_type: String,
    #[serde(default)]
    stream: Option<String>,
    #[serde(default = "default_toxicity")]
    toxicity: f32,
    #[serde(default)]
    attributes: HashMap<String, toml::Value>,
}

const fn default_toxicity() -> f32 {
    1.0
}

#[derive(Serialize)]
struct TimelineRecord<'a, T> {
    ts_ms: u128,
    event: &'a str,
    #[serde(flatten)]
    payload: T,
}

#[derive(Serialize)]
struct StressSummary {
    scenario: String,
    ok: bool,
    duration_ms: u128,
    phase_count: usize,
    probe_samples: usize,
    phases: Vec<PhaseSummary>,
    toxiproxy_enabled: bool,
    output_dir: String,
    failure: Option<String>,
}

#[derive(Serialize)]
struct PhaseSummary {
    name: String,
    duration_ms: u64,
    probe_samples: usize,
    toxics_applied: usize,
}

#[derive(Serialize)]
struct ProbeSnapshot {
    role: String,
    queries: HashMap<String, serde_json::Value>,
    evals: HashMap<String, String>,
}

pub fn is_stress_scenario(dir: &Path) -> bool {
    if !dir.is_dir() {
        return false;
    }
    let name = dir.file_name().unwrap_or_default().to_string_lossy();
    name.ends_with(".stress") && dir.join("stress.toml").exists()
}

pub fn stress_name(path: &Path) -> String {
    let raw = path.to_string_lossy().replace('\\', "/");
    raw.strip_suffix(".stress").unwrap_or(&raw).to_string()
}

#[allow(clippy::too_many_lines)]
pub async fn run_stress_scenario(
    scenario_dir: &Path,
    game_dir: &str,
    data_dir: Option<&str>,
    output_dir: &Path,
    render: Option<&str>,
    cli_game_args: &[String],
) -> Result<ScenarioResult> {
    let start = Instant::now();
    let stress_toml = scenario_dir.join("stress.toml");
    let toml_content = std::fs::read_to_string(&stress_toml)
        .with_context(|| format!("failed to read {}", stress_toml.display()))?;
    let config: StressConfig = toml::from_str(&toml_content)
        .with_context(|| format!("failed to parse {}", stress_toml.display()))?;

    validate_config(&config)?;

    let scenario_name = stress_name(
        scenario_dir
            .strip_prefix(super::multi::repo_root())
            .unwrap_or(scenario_dir),
    );
    let scenario_output = output_dir.join(scenario_name.replace('/', "_"));
    std::fs::create_dir_all(&scenario_output)
        .with_context(|| format!("failed to create {}", scenario_output.display()))?;

    let timeline_path = scenario_output.join("timeline.jsonl");
    let summary_path = scenario_output.join("summary.json");
    let mut timeline = std::fs::File::create(&timeline_path)
        .with_context(|| format!("failed to create {}", timeline_path.display()))?;
    let timeline_zero = Instant::now();

    let resolved_mission = super::multi::resolve_mission_path(config.mission.as_deref());
    let effective_cfg = crate::config::get().clone();
    let timeout = effective_cfg.timeout;
    let client_config = effective_cfg.client_config();

    write_timeline(
        &mut timeline,
        timeline_zero,
        "scenario_start",
        serde_json::json!({
            "scenario": scenario_name,
            "output_dir": scenario_output,
            "toxiproxy_enabled": config.toxiproxy.enabled,
        }),
    )?;

    let mut toxiproxy = ToxiproxyManager::start(&config.toxiproxy, &scenario_output).await?;
    let mut instances: Vec<RunningInstance> = Vec::new();
    let mut phase_summaries = Vec::new();
    let mut probe_samples = 0usize;
    let result = async {
        spawn_instances(
            &config,
            scenario_name.as_str(),
            scenario_dir,
            resolved_mission.as_deref(),
            game_dir,
            data_dir,
            &scenario_output,
            render,
            cli_game_args,
            &client_config,
            timeout,
            &effective_cfg.poll_interval,
            &mut instances,
        )
        .await?;

        for phase in &config.phases {
            write_timeline(
                &mut timeline,
                timeline_zero,
                "phase_start",
                serde_json::json!({
                    "name": phase.name,
                    "duration_ms": phase.duration_ms,
                    "toxics": phase.toxics.len(),
                }),
            )?;

            toxiproxy.reset_toxics().await?;
            let toxics_applied = toxiproxy.apply_phase_toxics(&phase.toxics).await?;

            run_hooks(
                &scenario_name,
                &phase.before,
                scenario_dir,
                &mut instances,
                timeout,
                effective_cfg.poll_interval,
            )
            .await?;
            run_phase_actions(
                &config,
                &scenario_name,
                scenario_dir,
                &phase.before_actions,
                resolved_mission.as_deref(),
                game_dir,
                data_dir,
                &scenario_output,
                render,
                cli_game_args,
                &client_config,
                timeout,
                effective_cfg.poll_interval,
                &mut instances,
            )
            .await?;

            let phase_start = Instant::now();
            let phase_duration = Duration::from_millis(phase.duration_ms);
            let probe_interval = Duration::from_millis(config.probes.interval_ms.max(1));
            let mut phase_probe_samples = 0usize;

            while phase_start.elapsed() < phase_duration {
                let snapshot = collect_probe_snapshot(&config.probes, &mut instances).await?;
                probe_samples += 1;
                phase_probe_samples += 1;
                write_timeline(
                    &mut timeline,
                    timeline_zero,
                    "probe",
                    serde_json::json!({
                        "phase": phase.name,
                        "samples": snapshot,
                    }),
                )?;

                let remaining = phase_duration.saturating_sub(phase_start.elapsed());
                if remaining.is_zero() {
                    break;
                }
                tokio::time::sleep(probe_interval.min(remaining)).await;
            }

            run_hooks(
                &scenario_name,
                &phase.after,
                scenario_dir,
                &mut instances,
                timeout,
                effective_cfg.poll_interval,
            )
            .await?;
            run_phase_actions(
                &config,
                &scenario_name,
                scenario_dir,
                &phase.after_actions,
                resolved_mission.as_deref(),
                game_dir,
                data_dir,
                &scenario_output,
                render,
                cli_game_args,
                &client_config,
                timeout,
                effective_cfg.poll_interval,
                &mut instances,
            )
            .await?;

            write_timeline(
                &mut timeline,
                timeline_zero,
                "phase_end",
                serde_json::json!({
                    "name": phase.name,
                    "probe_samples": phase_probe_samples,
                }),
            )?;
            phase_summaries.push(PhaseSummary {
                name: phase.name.clone(),
                duration_ms: phase.duration_ms,
                probe_samples: phase_probe_samples,
                toxics_applied,
            });
        }

        Ok::<(), anyhow::Error>(())
    }
    .await;

    let failure = result.as_ref().err().map(ToString::to_string);
    shutdown_instances(&mut instances, timeout).await;
    toxiproxy.stop().await;

    let summary = StressSummary {
        scenario: scenario_name.clone(),
        ok: result.is_ok(),
        duration_ms: start.elapsed().as_millis(),
        phase_count: config.phases.len(),
        probe_samples,
        phases: phase_summaries,
        toxiproxy_enabled: config.toxiproxy.enabled,
        output_dir: scenario_output.display().to_string(),
        failure,
    };
    std::fs::write(
        &summary_path,
        format!("{}\n", serde_json::to_string_pretty(&summary)?),
    )
    .with_context(|| format!("failed to write {}", summary_path.display()))?;

    match result {
        Ok(()) => Ok(ScenarioResult {
            passed: true,
            message: format!(
                "stress ok, {} phases, {} probe samples ({:.1}s)",
                summary.phase_count,
                summary.probe_samples,
                start.elapsed().as_secs_f64()
            ),
            duration: start.elapsed(),
        }),
        Err(err) => Ok(ScenarioResult {
            passed: false,
            message: format!(
                "stress failed: {err} ({:.1}s)",
                start.elapsed().as_secs_f64()
            ),
            duration: start.elapsed(),
        }),
    }
}

fn validate_config(config: &StressConfig) -> Result<()> {
    if config.instances.is_empty() {
        anyhow::bail!("stress.toml must define at least one [[instances]] entry");
    }
    if config.phases.is_empty() {
        anyhow::bail!("stress.toml must define at least one [[phases]] entry");
    }
    if config.toxiproxy.enabled {
        if config.toxiproxy.links.is_empty() {
            anyhow::bail!("toxiproxy.enabled=true requires at least one [[toxiproxy.links]] entry");
        }
        if config.toxiproxy.backend != "toxiproxy" && config.toxiproxy.backend != "udp_fault_proxy"
        {
            anyhow::bail!(
                "unsupported stress fault proxy backend '{}': expected 'toxiproxy' or 'udp_fault_proxy'",
                config.toxiproxy.backend
            );
        }
    } else if config.phases.iter().any(|phase| !phase.toxics.is_empty()) {
        anyhow::bail!("phase toxics require toxiproxy.enabled=true");
    }
    Ok(())
}

fn write_timeline<W: Write>(
    writer: &mut W,
    zero: Instant,
    event: &str,
    payload: serde_json::Value,
) -> Result<()> {
    let record = TimelineRecord {
        ts_ms: zero.elapsed().as_millis(),
        event,
        payload,
    };
    serde_json::to_writer(&mut *writer, &record)?;
    writer.write_all(b"\n")?;
    writer.flush()?;
    Ok(())
}

struct RunningInstance {
    name: String,
    game: GameInstance,
}

#[allow(clippy::too_many_arguments)]
async fn spawn_instances(
    config: &StressConfig,
    scenario_name: &str,
    scenario_dir: &Path,
    resolved_mission: Option<&str>,
    game_dir: &str,
    data_dir: Option<&str>,
    output_dir: &Path,
    render: Option<&str>,
    cli_game_args: &[String],
    client_config: &crate::client::ClientConfig,
    timeout: Duration,
    poll_interval: &Duration,
    instances: &mut Vec<RunningInstance>,
) -> Result<()> {
    for inst in &config.instances {
        if !inst.autostart {
            continue;
        }
        start_role(
            config,
            scenario_name,
            scenario_dir,
            &inst.name,
            resolved_mission,
            game_dir,
            data_dir,
            output_dir,
            render,
            cli_game_args,
            client_config,
            timeout,
            *poll_interval,
            instances,
        )
        .await?;
    }

    Ok(())
}

#[allow(clippy::too_many_arguments)]
async fn start_role(
    config: &StressConfig,
    scenario_name: &str,
    scenario_dir: &Path,
    role_name: &str,
    resolved_mission: Option<&str>,
    game_dir: &str,
    data_dir: Option<&str>,
    output_dir: &Path,
    render: Option<&str>,
    cli_game_args: &[String],
    client_config: &crate::client::ClientConfig,
    timeout: Duration,
    poll_interval: Duration,
    instances: &mut Vec<RunningInstance>,
) -> Result<()> {
    if instances.iter().any(|instance| instance.name == role_name) {
        return Ok(());
    }

    let inst = config
        .instances
        .iter()
        .find(|instance| instance.name == role_name)
        .with_context(|| format!("role '{role_name}' is not defined"))?;
    if let Some(after_name) = &inst.after {
        let dep = instances
            .iter_mut()
            .find(|instance| instance.name == *after_name)
            .with_context(|| {
                format!(
                    "instance '{}' depends on '{}' which has not been started",
                    inst.name, after_name
                )
            })?;
        wait_for_dependency(dep.game.client(), after_name, inst, timeout, poll_interval).await?;
    }

    let mut game = spawn_game_instance(
        config,
        inst,
        resolved_mission,
        game_dir,
        data_dir,
        output_dir,
        render,
        cli_game_args,
        client_config,
        timeout,
    )
    .await?;
    run_default_role_script(
        scenario_name,
        scenario_dir,
        role_name,
        &mut game,
        timeout,
        poll_interval,
    )
    .await?;
    instances.push(RunningInstance {
        name: role_name.to_string(),
        game,
    });
    Ok(())
}

#[allow(clippy::too_many_arguments)]
async fn spawn_game_instance(
    config: &StressConfig,
    inst: &super::multi::InstanceConfig,
    resolved_mission: Option<&str>,
    game_dir: &str,
    data_dir: Option<&str>,
    output_dir: &Path,
    render: Option<&str>,
    cli_game_args: &[String],
    client_config: &crate::client::ClientConfig,
    timeout: Duration,
) -> Result<GameInstance> {
    let mut extra_args: Vec<String> = if inst.is_server() {
        // PoseidonServer (selected at spawn) is inherently a server — no --server flag.
        vec!["--nosound".into(), "--focus".into()]
    } else {
        vec![
            "--window".into(),
            "--no-splash".into(),
            "--nosound".into(),
            "--focus".into(),
        ]
    };

    extra_args.push("--port".into());
    extra_args.push(inst.network_port.unwrap_or(config.game_port).to_string());

    if !inst.is_server() {
        extra_args.push("--name".into());
        extra_args.push(inst.name.clone());
    }

    if let Some(mission) = resolved_mission {
        if inst.is_server() {
            extra_args.push("--simulate".into());
        } else {
            extra_args.push("--test-mission".into());
        }
        extra_args.push(mission.to_string());
    }
    if inst.connect.is_some() {
        extra_args.push("--connect".into());
        extra_args.push(
            inst.connect_host
                .clone()
                .unwrap_or_else(|| "127.0.0.1".into()),
        );
        if let Some(connect_port) = inst.connect_port {
            extra_args.push("--connect-port".into());
            extra_args.push(connect_port.to_string());
        }
    }
    extra_args.push("--log-format".into());
    extra_args.push("jsonl".into());
    if let Some(assign) = &inst.mp_assign {
        extra_args.push("--mp-assign".into());
        extra_args.push(assign.clone());
    }
    if let Some(renderer) = render {
        extra_args.push("--render".into());
        extra_args.push(renderer.into());
    }
    extra_args.extend(inst.extra_args.iter().cloned());
    extra_args.extend(cli_game_args.iter().cloned());
    let extra_refs: Vec<&str> = extra_args.iter().map(String::as_str).collect();

    let inst_output = output_dir.join(&inst.name);
    let user_dir = inst_output.join("user");
    std::fs::create_dir_all(&user_dir)
        .with_context(|| format!("failed to create {}", user_dir.display()))?;
    let user_dir_str = user_dir.to_string_lossy().to_string();
    let output_str = inst_output.to_string_lossy().to_string();
    let machine_id = format!("trident:stress:{}", inst.name);
    let env_vars = [
        ("POSEIDON_USER_DIR", user_dir_str.as_str()),
        ("POSEIDON_CACHE_DIR", user_dir_str.as_str()),
        ("POSEIDON_TEMP_DIR", user_dir_str.as_str()),
        ("POSEIDON_TEST_MACHINE_ID", machine_id.as_str()),
        ("TRI_OUTPUT_DIR", output_str.as_str()),
        ("SDL_VIDEO_MINIMIZE_ON_FOCUS_LOSS", "0"),
    ];

    let instance_cfg = inst.timeout.map_or_else(
        || client_config.clone(),
        |secs| crate::config::TridentConfig::new(secs).client_config(),
    );
    let mut game = GameInstance::spawn_with_env(
        game_dir,
        data_dir,
        0,
        &extra_refs,
        &env_vars,
        if inst.is_server() {
            // Dedicated servers have their own entry point and harness startup path.
            Some("PoseidonServer")
        } else {
            None
        },
        &instance_cfg,
    )
    .await?;
    if !config.toxiproxy.enabled && !inst.is_server() {
        game.wait_ready(timeout).await?;
    }
    Ok(game)
}

async fn wait_for_dependency(
    client: &mut HarnessClient,
    after_name: &str,
    inst: &super::multi::InstanceConfig,
    timeout: Duration,
    poll_interval: Duration,
) -> Result<()> {
    let target = inst.wait_ngs.or(inst.wait_ngs_client);
    if target.is_none() {
        return Ok(());
    }

    let target = target.unwrap_or_default();
    let field = if inst.wait_ngs.is_some() {
        "server_state"
    } else {
        "client_state"
    };
    let deadline = Instant::now() + timeout;
    loop {
        let response = client.query("ngs").await?;
        let state = response
            .data
            .get(field)
            .and_then(serde_json::Value::as_i64)
            .unwrap_or(-1);
        if state >= target {
            return Ok(());
        }
        if Instant::now() >= deadline {
            anyhow::bail!("timeout: {after_name} {field} >= {target} (got {state})");
        }
        tokio::time::sleep(poll_interval).await;
    }
}

async fn run_hooks(
    scenario_name: &str,
    hooks: &[PhaseHookConfig],
    scenario_dir: &Path,
    instances: &mut [RunningInstance],
    timeout: Duration,
    poll_interval: Duration,
) -> Result<()> {
    for hook in hooks {
        let instance = instances
            .iter_mut()
            .find(|instance| instance.name == hook.role)
            .with_context(|| format!("hook role '{}' is not defined", hook.role))?;
        run_script_for_instance(
            scenario_name,
            &hook.role,
            &scenario_dir.join(&hook.script),
            &mut instance.game,
            timeout,
            poll_interval,
        )
        .await?;
    }
    Ok(())
}

#[allow(clippy::too_many_arguments)]
async fn run_phase_actions(
    config: &StressConfig,
    scenario_name: &str,
    scenario_dir: &Path,
    actions: &[PhaseActionConfig],
    resolved_mission: Option<&str>,
    game_dir: &str,
    data_dir: Option<&str>,
    output_dir: &Path,
    render: Option<&str>,
    cli_game_args: &[String],
    client_config: &crate::client::ClientConfig,
    timeout: Duration,
    poll_interval: Duration,
    instances: &mut Vec<RunningInstance>,
) -> Result<()> {
    let mut index = 0usize;
    while index < actions.len() {
        let action = &actions[index];
        match action.action.as_str() {
            "script" => {
                let batch_start = index;
                while index < actions.len() && actions[index].action == "script" {
                    index += 1;
                }
                run_script_action_batch(
                    scenario_name,
                    scenario_dir,
                    &actions[batch_start..index],
                    timeout,
                    poll_interval,
                    instances,
                )
                .await?;
                continue;
            }
            "start_role" => {
                start_role(
                    config,
                    scenario_name,
                    scenario_dir,
                    &action.role,
                    resolved_mission,
                    game_dir,
                    data_dir,
                    output_dir,
                    render,
                    cli_game_args,
                    client_config,
                    timeout,
                    poll_interval,
                    instances,
                )
                .await?;
            }
            "stop_role" => {
                stop_role(&action.role, instances, timeout).await?;
            }
            "restart_role" => {
                stop_role(&action.role, instances, timeout).await?;
                start_role(
                    config,
                    scenario_name,
                    scenario_dir,
                    &action.role,
                    resolved_mission,
                    game_dir,
                    data_dir,
                    output_dir,
                    render,
                    cli_game_args,
                    client_config,
                    timeout,
                    poll_interval,
                    instances,
                )
                .await?;
            }
            other => anyhow::bail!("unsupported phase action '{other}'"),
        }
        index += 1;
    }
    Ok(())
}

async fn run_script_action_batch(
    scenario_name: &str,
    scenario_dir: &Path,
    actions: &[PhaseActionConfig],
    timeout: Duration,
    poll_interval: Duration,
    instances: &mut [RunningInstance],
) -> Result<()> {
    let mut handles = Vec::new();
    let barriers = std::sync::Arc::new(std::sync::Mutex::new(std::collections::HashMap::new()));
    let role_count = actions.len();

    for action in actions {
        let script = action.script.as_ref().with_context(|| {
            format!("script action for role '{}' is missing script", action.role)
        })?;
        let sqf_code = std::fs::read_to_string(scenario_dir.join(script))
            .with_context(|| format!("failed to read {}", scenario_dir.join(script).display()))?;
        let statements = super::integration::parse_statements(&sqf_code);
        if statements.is_empty() {
            continue;
        }

        let instance = instances
            .iter_mut()
            .find(|instance| instance.name == action.role)
            .with_context(|| format!("action role '{}' is not running", action.role))?;
        let client = instance.game.take_client();
        let role_name = action.role.clone();
        let scenario_name = scenario_name.to_string();
        let deadline = Instant::now() + timeout;
        let progress = std::sync::Arc::new(std::sync::Mutex::new(std::collections::HashMap::new()));
        let barriers = std::sync::Arc::clone(&barriers);

        handles.push(tokio::spawn(async move {
            let (client, failure) = super::multi::run_role_script(
                &scenario_name,
                &role_name,
                client,
                &statements,
                deadline,
                poll_interval,
                progress,
                barriers,
                role_count,
                std::sync::Arc::new(Vec::new()),
            )
            .await?;
            Ok::<_, anyhow::Error>((role_name, client, failure))
        }));
    }

    let mut first_failure = None;
    for handle in handles {
        let (role_name, client, failure) = handle.await??;
        let instance = instances
            .iter_mut()
            .find(|instance| instance.name == role_name)
            .with_context(|| format!("action role '{role_name}' is not running"))?;
        instance.game.restore_client(client);
        if first_failure.is_none() {
            first_failure = failure;
        }
    }

    if let Some((role, reason)) = first_failure {
        anyhow::bail!("[{role}] {reason}");
    }

    Ok(())
}

async fn stop_role(
    role_name: &str,
    instances: &mut Vec<RunningInstance>,
    timeout: Duration,
) -> Result<()> {
    let index = instances
        .iter()
        .position(|instance| instance.name == role_name)
        .with_context(|| format!("role '{role_name}' is not running"))?;
    let mut instance = instances.remove(index);
    let _ = instance.game.client().exec("triEndTest").await;
    let _ = instance.game.wait_exit(timeout).await;
    Ok(())
}

async fn run_default_role_script(
    scenario_name: &str,
    scenario_dir: &Path,
    role_name: &str,
    game: &mut GameInstance,
    timeout: Duration,
    poll_interval: Duration,
) -> Result<()> {
    let sqf_path = scenario_dir.join(format!("{role_name}.sqf"));
    if !sqf_path.exists() {
        return Ok(());
    }
    run_script_for_instance(
        scenario_name,
        role_name,
        &sqf_path,
        game,
        timeout,
        poll_interval,
    )
    .await
}

async fn run_script_for_instance(
    scenario_name: &str,
    role_name: &str,
    script_path: &Path,
    game: &mut GameInstance,
    timeout: Duration,
    poll_interval: Duration,
) -> Result<()> {
    let sqf_code = std::fs::read_to_string(script_path)
        .with_context(|| format!("failed to read {}", script_path.display()))?;
    let statements = super::integration::parse_statements(&sqf_code);
    if statements.is_empty() {
        return Ok(());
    }

    let client = game.take_client();
    let (client, failure) = super::multi::run_role_script(
        scenario_name,
        role_name,
        client,
        &statements,
        Instant::now() + timeout,
        poll_interval,
        std::sync::Arc::new(std::sync::Mutex::new(std::collections::HashMap::new())),
        std::sync::Arc::new(std::sync::Mutex::new(std::collections::HashMap::new())),
        1,
        std::sync::Arc::new(Vec::new()),
    )
    .await?;
    game.restore_client(client);
    if let Some((role, reason)) = failure {
        anyhow::bail!("[{role}] {reason}");
    }
    Ok(())
}

async fn collect_probe_snapshot(
    probes: &ProbeConfig,
    instances: &mut [RunningInstance],
) -> Result<Vec<ProbeSnapshot>> {
    let mut snapshots = Vec::new();
    for instance in instances {
        let mut queries = HashMap::new();
        for query_name in &probes.queries {
            let response = instance.game.client().query(query_name).await?;
            queries.insert(query_name.clone(), serde_json::Value::Object(response.data));
        }

        let mut evals = HashMap::new();
        for eval_probe in probes
            .evals
            .iter()
            .filter(|probe| probe.role == instance.name)
        {
            let result = instance.game.client().eval(&eval_probe.code).await?;
            if eval_probe.assert_ok && result.trim_matches('"') != "OK" {
                anyhow::bail!(
                    "probe '{}' on role '{}' returned '{}'",
                    eval_probe.name,
                    instance.name,
                    result
                );
            }
            evals.insert(eval_probe.name.clone(), result);
        }

        snapshots.push(ProbeSnapshot {
            role: instance.name.clone(),
            queries,
            evals,
        });
    }
    Ok(snapshots)
}

async fn shutdown_instances(instances: &mut [RunningInstance], timeout: Duration) {
    for instance in instances.iter_mut().rev() {
        let _ = instance.game.client().exec("triEndTest").await;
    }
    for instance in instances.iter_mut().rev() {
        let _ = instance.game.wait_exit(timeout).await;
    }
}

struct ToxiproxyManager {
    backend: FaultProxyBackend,
}

enum FaultProxyBackend {
    Disabled,
    Toxiproxy(ToxiproxyRuntime),
    UdpFaultProxy(UdpFaultProxyRuntime),
}

struct ToxiproxyRuntime {
    child: Child,
    client: reqwest::Client,
    base_url: String,
    enabled_links: Vec<String>,
}

struct UdpFaultProxyRuntime {
    python: PathBuf,
    script: PathBuf,
    output_dir: PathBuf,
    links: Vec<ToxiproxyLinkConfig>,
    children: Vec<UdpFaultProxyChild>,
    current_profiles: HashMap<String, UdpFaultProfile>,
}

struct UdpFaultProxyChild {
    child: Child,
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
struct UdpFaultProfile {
    client_delay_ms: u64,
    server_delay_ms: u64,
}

impl ToxiproxyManager {
    async fn start(config: &ToxiproxyConfig, output_dir: &Path) -> Result<Self> {
        if !config.enabled {
            return Ok(Self {
                backend: FaultProxyBackend::Disabled,
            });
        }

        match config.backend.as_str() {
            "toxiproxy" => {
                let client = reqwest::Client::new();
                let binary = find_toxiproxy_binary()?;
                let toxiproxy_dir = output_dir.join("toxiproxy");
                std::fs::create_dir_all(&toxiproxy_dir)
                    .with_context(|| format!("failed to create {}", toxiproxy_dir.display()))?;
                let log_path = toxiproxy_dir.join("toxiproxy.log");
                let stdout = std::fs::File::create(&log_path)
                    .with_context(|| format!("failed to create {}", log_path.display()))?;
                let stderr = stdout
                    .try_clone()
                    .with_context(|| format!("failed to clone {}", log_path.display()))?;

                let mut command = Command::new(&binary);
                command
                    .arg("-host")
                    .arg(&config.host)
                    .arg("-port")
                    .arg(config.port.to_string())
                    .stdout(Stdio::from(stdout))
                    .stderr(Stdio::from(stderr))
                    .stdin(Stdio::null())
                    .kill_on_drop(true);
                let child = command
                    .spawn()
                    .with_context(|| format!("failed to start '{}'", binary.display()))?;

                let manager = Self {
                    backend: FaultProxyBackend::Toxiproxy(ToxiproxyRuntime {
                        child,
                        client,
                        base_url: format!("http://{}:{}", config.host, config.port),
                        enabled_links: config
                            .links
                            .iter()
                            .filter(|link| link.enabled)
                            .map(|link| link.name.clone())
                            .collect(),
                    }),
                };
                manager.wait_until_ready().await?;
                for link in config.links.iter().filter(|link| link.enabled) {
                    manager.create_link(link).await?;
                }
                Ok(manager)
            }
            "udp_fault_proxy" => {
                let python = find_python_binary()?;
                let script = super::multi::repo_root().join("tests/tools/udp_fault_proxy.py");
                anyhow::ensure!(
                    script.is_file(),
                    "udp fault proxy script was not found at {}",
                    script.display()
                );
                let output_dir = output_dir.join("udp_fault_proxy");
                std::fs::create_dir_all(&output_dir)
                    .with_context(|| format!("failed to create {}", output_dir.display()))?;
                let mut runtime = UdpFaultProxyRuntime {
                    python,
                    script,
                    output_dir,
                    links: config
                        .links
                        .iter()
                        .filter(|link| link.enabled)
                        .cloned()
                        .collect(),
                    children: Vec::new(),
                    current_profiles: HashMap::new(),
                };
                runtime.apply_phase_toxics(&[]).await?;
                Ok(Self {
                    backend: FaultProxyBackend::UdpFaultProxy(runtime),
                })
            }
            other => anyhow::bail!("unsupported stress fault proxy backend '{other}'"),
        }
    }

    async fn wait_until_ready(&self) -> Result<()> {
        let FaultProxyBackend::Toxiproxy(runtime) = &self.backend else {
            return Ok(());
        };
        let deadline = Instant::now() + Duration::from_secs(10);
        loop {
            let response = runtime
                .client
                .get(format!("{}/proxies", runtime.base_url))
                .send()
                .await;
            if let Ok(resp) = response {
                if resp.status().is_success() {
                    return Ok(());
                }
            }
            if Instant::now() >= deadline {
                anyhow::bail!("toxiproxy-server did not become ready within 10s");
            }
            tokio::time::sleep(Duration::from_millis(200)).await;
        }
    }

    async fn create_link(&self, link: &ToxiproxyLinkConfig) -> Result<()> {
        let FaultProxyBackend::Toxiproxy(runtime) = &self.backend else {
            return Ok(());
        };
        let response = runtime
            .client
            .post(format!("{}/proxies", runtime.base_url))
            .json(&serde_json::json!({
                "name": link.name,
                "listen": link.listen,
                "upstream": link.upstream,
                "enabled": true,
            }))
            .send()
            .await?;
        if !response.status().is_success() {
            anyhow::bail!(
                "failed to create toxiproxy link '{}': {}",
                link.name,
                response.text().await.unwrap_or_default()
            );
        }
        Ok(())
    }

    async fn reset_toxics(&self) -> Result<()> {
        match &self.backend {
            FaultProxyBackend::Disabled | FaultProxyBackend::UdpFaultProxy(_) => Ok(()),
            FaultProxyBackend::Toxiproxy(runtime) => {
                for link_name in &runtime.enabled_links {
                    let proxy: ProxyState = runtime
                        .client
                        .get(format!("{}/proxies/{link_name}", runtime.base_url))
                        .send()
                        .await?
                        .error_for_status()?
                        .json()
                        .await?;
                    for toxic in proxy.toxics {
                        runtime
                            .client
                            .delete(format!(
                                "{}/proxies/{}/toxics/{}",
                                runtime.base_url, link_name, toxic.name
                            ))
                            .send()
                            .await?
                            .error_for_status()?;
                    }
                }
                Ok(())
            }
        }
    }

    async fn apply_phase_toxics(&mut self, toxics: &[ToxicConfig]) -> Result<usize> {
        match &mut self.backend {
            FaultProxyBackend::Disabled => Ok(0),
            FaultProxyBackend::Toxiproxy(runtime) => {
                for toxic in toxics {
                    let attributes = serde_json::to_value(&toxic.attributes)?;
                    let response = runtime
                        .client
                        .post(format!(
                            "{}/proxies/{}/toxics",
                            runtime.base_url, toxic.link
                        ))
                        .json(&serde_json::json!({
                            "name": toxic.name,
                            "type": toxic.toxic_type,
                            "stream": toxic.stream.clone().unwrap_or_else(|| "downstream".into()),
                            "toxicity": toxic.toxicity,
                            "attributes": attributes,
                        }))
                        .send()
                        .await?;
                    if !response.status().is_success() {
                        anyhow::bail!(
                            "failed to apply toxic '{}' on '{}': {}",
                            toxic.name,
                            toxic.link,
                            response.text().await.unwrap_or_default()
                        );
                    }
                }
                Ok(toxics.len())
            }
            FaultProxyBackend::UdpFaultProxy(runtime) => runtime.apply_phase_toxics(toxics).await,
        }
    }

    async fn stop(&mut self) {
        match &mut self.backend {
            FaultProxyBackend::Disabled => {}
            FaultProxyBackend::Toxiproxy(runtime) => {
                let _ = runtime.child.kill().await;
                let _ = runtime.child.wait().await;
            }
            FaultProxyBackend::UdpFaultProxy(runtime) => runtime.stop_children().await,
        }
    }
}

impl UdpFaultProxyRuntime {
    async fn apply_phase_toxics(&mut self, toxics: &[ToxicConfig]) -> Result<usize> {
        let mut desired_profiles = HashMap::new();
        for link in &self.links {
            desired_profiles.insert(link.name.clone(), UdpFaultProfile::for_link(link, toxics)?);
        }
        if desired_profiles == self.current_profiles {
            return Ok(toxics.len());
        }

        self.stop_children().await;
        for link in &self.links {
            let profile = desired_profiles
                .get(&link.name)
                .with_context(|| format!("missing udp fault profile for link '{}'", link.name))?;
            let child = self.start_link(link, profile).await?;
            self.children.push(child);
        }
        self.current_profiles = desired_profiles;
        Ok(toxics.len())
    }

    async fn stop_children(&mut self) {
        for child in &mut self.children {
            let _ = child.child.kill().await;
            let _ = child.child.wait().await;
        }
        self.children.clear();
        self.current_profiles.clear();
    }

    async fn start_link(
        &self,
        link: &ToxiproxyLinkConfig,
        profile: &UdpFaultProfile,
    ) -> Result<UdpFaultProxyChild> {
        let (listen_host, listen_port) = parse_host_port(&link.listen)?;
        let (target_host, target_port) = parse_host_port(&link.upstream)?;
        let log_path = self.output_dir.join(format!("{}.log", link.name));
        let stdout = std::fs::File::create(&log_path)
            .with_context(|| format!("failed to create {}", log_path.display()))?;
        let stderr = stdout
            .try_clone()
            .with_context(|| format!("failed to clone {}", log_path.display()))?;

        let mut command = Command::new(&self.python);
        command
            .arg(&self.script)
            .arg("--listen-host")
            .arg(&listen_host)
            .arg("--listen-port")
            .arg(listen_port.to_string())
            .arg("--target-host")
            .arg(&target_host)
            .arg("--target-port")
            .arg(target_port.to_string())
            .stdout(Stdio::from(stdout))
            .stderr(Stdio::from(stderr))
            .stdin(Stdio::null())
            .kill_on_drop(true);
        if profile.client_delay_ms > 0 {
            command
                .arg("--client-delay-ms")
                .arg(profile.client_delay_ms.to_string());
        }
        if profile.server_delay_ms > 0 {
            command
                .arg("--server-delay-ms")
                .arg(profile.server_delay_ms.to_string());
        }

        let mut child = command.spawn().with_context(|| {
            format!(
                "failed to start udp fault proxy '{}' via '{}'",
                self.script.display(),
                self.python.display()
            )
        })?;
        let deadline = Instant::now() + Duration::from_secs(5);
        loop {
            if let Some(status) = child.try_wait()? {
                anyhow::bail!(
                    "udp fault proxy link '{}' exited before ready: {}",
                    link.name,
                    status
                );
            }
            if std::fs::read_to_string(&log_path)
                .map(|log| log.contains("LISTENING"))
                .unwrap_or(false)
            {
                return Ok(UdpFaultProxyChild { child });
            }
            if Instant::now() >= deadline {
                anyhow::bail!(
                    "udp fault proxy link '{}' did not become ready within 5s",
                    link.name
                );
            }
            tokio::time::sleep(Duration::from_millis(100)).await;
        }
    }
}

impl UdpFaultProfile {
    fn for_link(link: &ToxiproxyLinkConfig, toxics: &[ToxicConfig]) -> Result<Self> {
        let mut profile = Self::default();
        for toxic in toxics.iter().filter(|toxic| toxic.link == link.name) {
            match toxic.toxic_type.as_str() {
                "latency" => {
                    let latency = toxic_attribute_u64(toxic, "latency")?;
                    match toxic.stream.as_deref().unwrap_or("downstream") {
                        "downstream" => profile.server_delay_ms += latency,
                        "upstream" => profile.client_delay_ms += latency,
                        other => anyhow::bail!(
                            "udp_fault_proxy backend does not support toxic stream '{other}'"
                        ),
                    }
                }
                other => {
                    anyhow::bail!("udp_fault_proxy backend does not support toxic type '{other}'")
                }
            }
        }
        Ok(profile)
    }
}

#[derive(serde::Deserialize)]
struct ProxyState {
    #[serde(default)]
    toxics: Vec<ToxicState>,
}

#[derive(serde::Deserialize)]
struct ToxicState {
    name: String,
}

fn find_toxiproxy_binary() -> Result<PathBuf> {
    let path = std::env::var_os("PATH").context("PATH is not set")?;
    let exe_names: &[&str] = if cfg!(windows) {
        &["toxiproxy-server.exe", "toxiproxy-server"]
    } else {
        &["toxiproxy-server"]
    };
    for dir in std::env::split_paths(&path) {
        for exe_name in exe_names {
            let candidate = dir.join(exe_name);
            if candidate.is_file() {
                return Ok(candidate);
            }
        }
    }
    anyhow::bail!("toxiproxy-server was not found on PATH")
}

fn find_python_binary() -> Result<PathBuf> {
    let path = std::env::var_os("PATH").context("PATH is not set")?;
    let exe_names: &[&str] = if cfg!(windows) {
        &["python.exe", "python3.exe", "py.exe", "py"]
    } else {
        &["python3", "python"]
    };
    for dir in std::env::split_paths(&path) {
        for exe_name in exe_names {
            let candidate = dir.join(exe_name);
            if candidate.is_file() {
                return Ok(candidate);
            }
        }
    }
    anyhow::bail!("python was not found on PATH")
}

fn toxic_attribute_u64(toxic: &ToxicConfig, name: &str) -> Result<u64> {
    let value = toxic
        .attributes
        .get(name)
        .with_context(|| format!("toxic '{}' is missing '{}' attribute", toxic.name, name))?;
    let raw = value.as_integer().with_context(|| {
        format!(
            "toxic '{}' attribute '{}' must be an integer",
            toxic.name, name
        )
    })?;
    u64::try_from(raw).with_context(|| {
        format!(
            "toxic '{}' attribute '{}' must be non-negative, got {}",
            toxic.name, name, raw
        )
    })
}

fn parse_host_port(value: &str) -> Result<(String, u16)> {
    if let Ok(addr) = value.parse::<SocketAddr>() {
        return Ok((addr.ip().to_string(), addr.port()));
    }
    let (host, port) = value
        .rsplit_once(':')
        .with_context(|| format!("expected host:port address, got '{value}'"))?;
    Ok((
        host.to_string(),
        port.parse()
            .with_context(|| format!("invalid port in address '{value}'"))?,
    ))
}

#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::TempDir;

    #[test]
    fn detects_stress_directory() {
        let temp = TempDir::new().unwrap();
        let scenario = temp.path().join("basic.stress");
        std::fs::create_dir(&scenario).unwrap();
        std::fs::write(scenario.join("stress.toml"), "phases=[]\ninstances=[]\n").unwrap();
        assert!(is_stress_scenario(&scenario));
        assert!(!is_stress_scenario(temp.path()));
    }

    #[test]
    fn parses_toxiproxy_stress_config() {
        let parsed: StressConfig = toml::from_str(
            r#"
game_port = 2302

[probes]
interval_ms = 500
queries = ["ngs"]

[toxiproxy]
enabled = true
backend = "udp_fault_proxy"

[[toxiproxy.links]]
name = "server-link"
listen = "127.0.0.1:2302"
upstream = "127.0.0.1:2303"

[[instances]]
name = "server"
type = "server"
network_port = 2303

[[instances]]
name = "client"
type = "client"
connect = "server"
connect_host = "127.0.0.1"
autostart = false

[[phases]]
name = "steady"
duration_ms = 1000

[[phases.before_actions]]
action = "start_role"
role = "client"

[[phases.toxics]]
link = "server-link"
name = "latency"
type = "latency"
[phases.toxics.attributes]
latency = 250
"#,
        )
        .unwrap();

        assert!(parsed.toxiproxy.enabled);
        assert_eq!(parsed.toxiproxy.backend, "udp_fault_proxy");
        assert_eq!(parsed.instances[0].network_port, Some(2303));
        assert!(!parsed.instances[1].autostart);
        assert_eq!(parsed.probes.interval_ms, 500);
        assert_eq!(parsed.phases[0].before_actions.len(), 1);
        assert_eq!(parsed.phases[0].toxics.len(), 1);
    }

    #[test]
    fn maps_udp_fault_proxy_latency_to_directional_delay() {
        let link = ToxiproxyLinkConfig {
            name: "server-link".into(),
            listen: "127.0.0.1:2302".into(),
            upstream: "127.0.0.1:2303".into(),
            enabled: true,
        };
        let toxics = vec![
            ToxicConfig {
                link: "server-link".into(),
                name: "latency-down".into(),
                toxic_type: "latency".into(),
                stream: Some("downstream".into()),
                toxicity: 1.0,
                attributes: HashMap::from([("latency".into(), toml::Value::Integer(250))]),
            },
            ToxicConfig {
                link: "server-link".into(),
                name: "latency-up".into(),
                toxic_type: "latency".into(),
                stream: Some("upstream".into()),
                toxicity: 1.0,
                attributes: HashMap::from([("latency".into(), toml::Value::Integer(75))]),
            },
        ];

        let profile = UdpFaultProfile::for_link(&link, &toxics).unwrap();
        assert_eq!(profile.server_delay_ms, 250);
        assert_eq!(profile.client_delay_ms, 75);
    }

    #[test]
    fn finds_toxiproxy_binary_on_path() {
        let temp = TempDir::new().unwrap();
        let binary_name = if cfg!(windows) {
            "toxiproxy-server.exe"
        } else {
            "toxiproxy-server"
        };
        let binary = temp.path().join(binary_name);
        std::fs::write(&binary, "").unwrap();

        let old_path = std::env::var_os("PATH");
        std::env::set_var("PATH", temp.path());
        let found = find_toxiproxy_binary().unwrap();
        if let Some(old_path) = old_path {
            std::env::set_var("PATH", old_path);
        }

        assert_eq!(found, binary);
    }
}
