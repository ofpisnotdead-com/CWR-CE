//! Trident 🔱 — Playwright-style test orchestrator for OFPR
//!
//! Spawns game instances with `--harness <port>`, connects to their TCP control
//! channels, drives test scenarios, and makes assertions on game state.
//! Binary name: `tri`

// The protocol and client surface is broader than any single scenario exercises.
#![allow(dead_code)]

mod client;
mod config;
mod console;
mod protocol;
mod scenarios;

use anyhow::Context;
use clap::{builder::styling, Parser, Subcommand};

const STYLES: styling::Styles = styling::Styles::styled()
    .header(styling::AnsiColor::Cyan.on_default().bold())
    .usage(styling::AnsiColor::Cyan.on_default().bold())
    .literal(styling::AnsiColor::Cyan.on_default())
    .placeholder(styling::AnsiColor::Green.on_default());

#[derive(Parser)]
#[command(name = "tri", version, about = "OFPR test orchestrator 🔱", styles = STYLES)]
struct Cli {
    /// Global timeout in seconds for assertions, connections, and event waits.
    /// Per-test TOML `timeout = N` overrides this for that test.
    #[arg(long, default_value = "30", global = true)]
    timeout: u64,

    #[command(subcommand)]
    command: Commands,
}

#[derive(Subcommand)]
enum Commands {
    /// Describe protocol: connect to harness and print available commands
    Describe {
        /// Host:port of running harness instance
        #[arg(long, default_value = "127.0.0.1:9100")]
        address: String,
    },

    /// Ping a running harness instance
    Ping {
        /// Host:port of running harness instance
        #[arg(long, default_value = "127.0.0.1:9100")]
        address: String,
    },

    /// Run tests — folder (recursive discovery) or single .test.sqf / .test.{island}
    Test {
        /// Paths to run: directories, .test.sqf files, globs, or path prefixes.
        /// Multiple paths are accepted and deduplicated by file identity.
        #[arg(required = true)]
        paths: Vec<String>,

        /// Only run tests tagged with any of these TOML tags.
        /// Repeat for multiple tags: --tag gl-8 --tag editor
        #[arg(long = "tag")]
        tags: Vec<String>,

        /// Skip tests tagged with any of these TOML tags.
        /// Repeat for multiple tags: --skip-tag `full_game` --skip-tag exclusive
        #[arg(long = "skip-tag")]
        skip_tags: Vec<String>,

        /// Path to game directory
        #[arg(long, env = "OFPR_GAME_DIR")]
        game_dir: String,

        /// Separate game data directory
        #[arg(long, env = "OFPR_DATA_DIR")]
        data_dir: Option<String>,

        /// Number of parallel test jobs (0 = auto/CPU count, default = 1 sequential)
        #[arg(short = 'j', long = "jobs", default_value = "1")]
        jobs: usize,

        /// Retry failed tests up to N times (default = 3)
        #[arg(long, default_value = "3")]
        retries: usize,

        /// Output directory for screenshots and artifacts (default: tmp/tri/<timestamp>)
        #[arg(long, env = "TRI_OUTPUT_DIR")]
        output_dir: Option<String>,

        /// Renderer backend to use (e.g. gl33, dummy).
        /// Forwarded as --render <backend> to all spawned game instances.
        #[arg(long)]
        render: Option<String>,

        /// Extra CLI arguments forwarded to spawned game instances.
        /// Repeat for each arg: --game-arg --vec-fonts --game-arg --no-sound
        #[arg(long = "game-arg", allow_hyphen_values = true)]
        game_args: Vec<String>,

        /// After the suite completes, print the N slowest tests with
        /// their wall-clock times.  Pass `--profile` (no value) for
        /// the default of 10, or `--profile 20` for top 20.
        #[arg(
            long,
            num_args = 0..=1,
            default_missing_value = "10",
        )]
        profile: Option<usize>,
    },

    /// Run one long-running multiplayer stress scenario from a .stress directory
    Stress {
        /// Path to a single .stress scenario directory
        #[arg(required = true)]
        path: String,

        /// Path to game directory
        #[arg(long, env = "OFPR_GAME_DIR")]
        game_dir: String,

        /// Separate game data directory
        #[arg(long, env = "OFPR_DATA_DIR")]
        data_dir: Option<String>,

        /// Output directory for stress artifacts (default: tmp/tri/<timestamp>)
        #[arg(long, env = "TRI_OUTPUT_DIR")]
        output_dir: Option<String>,

        /// Renderer backend to use (e.g. gl33, dummy)
        #[arg(long)]
        render: Option<String>,

        /// Extra CLI arguments forwarded to spawned game instances.
        #[arg(long = "game-arg", allow_hyphen_values = true)]
        game_args: Vec<String>,
    },

    /// Interactive SQF REPL — evaluate expressions on a live game instance
    Console {
        /// Path to game directory (containing `PoseidonGame` binary)
        #[arg(long, env = "OFPR_GAME_DIR")]
        game_dir: String,

        /// Separate game data directory (if binary and data are split)
        #[arg(long, env = "OFPR_DATA_DIR")]
        data_dir: Option<String>,

        /// Harness port to use
        #[arg(long, default_value = "9100")]
        port: u16,

        /// Attach to an already-running game instance (host:port)
        #[arg(long)]
        attach: Option<String>,
    },

    /// Execute SQF code on a live game — boots game, runs code, prints result, exits
    Exec {
        /// SQF code to execute (if omitted, reads from stdin)
        #[arg()]
        code: Option<String>,

        /// Path to game directory
        #[arg(long, env = "OFPR_GAME_DIR")]
        game_dir: String,

        /// Separate game data directory
        #[arg(long, env = "OFPR_DATA_DIR")]
        data_dir: Option<String>,

        /// Harness port to use
        #[arg(long, default_value = "9100")]
        port: u16,
    },
}

// Resolves when the process is asked to terminate (Ctrl-C anywhere; SIGTERM on
// Unix). Wrapping a game-spawning run in `select!` against this lets the command
// return early: the tokio runtime then shuts down, drops every in-flight
// GameInstance, and `kill_on_drop` reaps the game processes instead of orphaning
// them. Cross-platform — no process-group / signal machinery.
async fn shutdown_signal() {
    let ctrl_c = async {
        let _ = tokio::signal::ctrl_c().await;
    };

    #[cfg(unix)]
    let terminate = async {
        match tokio::signal::unix::signal(tokio::signal::unix::SignalKind::terminate()) {
            Ok(mut sig) => {
                sig.recv().await;
            }
            Err(_) => std::future::pending::<()>().await,
        }
    };
    #[cfg(not(unix))]
    let terminate = std::future::pending::<()>();

    tokio::select! {
        () = ctrl_c => {}
        () = terminate => {}
    }
}

#[tokio::main]
#[allow(clippy::too_many_lines)]
async fn main() -> anyhow::Result<()> {
    // Load .trident.env (project root) then .env (cwd) — sets env vars
    // before clap parses, so `env = "OFPR_GAME_DIR"` etc. just work.
    for name in [".trident.env", ".env"] {
        let _ = dotenvy::from_filename(name);
    }

    tracing_subscriber::fmt()
        .with_env_filter(
            tracing_subscriber::EnvFilter::try_from_default_env()
                .unwrap_or_else(|_| tracing_subscriber::EnvFilter::new("info")),
        )
        .init();

    let cli = Cli::parse();
    config::init(config::TridentConfig::new(cli.timeout));

    match cli.command {
        Commands::Describe { address } => {
            let mut conn = client::HarnessClient::connect(&address).await?;
            let desc = conn.describe().await?;
            println!("{}", serde_json::to_string_pretty(&desc)?);
        }
        Commands::Ping { address } => {
            let mut conn = client::HarnessClient::connect(&address).await?;
            conn.ping().await?;
            println!("pong");
        }
        Commands::Test {
            paths,
            tags,
            skip_tags,
            game_dir,
            data_dir,
            jobs,
            retries,
            output_dir,
            render,
            game_args,
            profile,
        } => {
            let out_dir = scenarios::integration::resolve_output_dir(output_dir.as_deref())?;
            tracing::info!("Output: {}", out_dir.display());

            let path_refs: Vec<&str> = paths.iter().map(String::as_str).collect();
            let tests = scenarios::integration::filter_tests_by_tags(
                scenarios::integration::resolve_tests(&path_refs)?,
                &tags,
                &skip_tags,
            );
            if tests.is_empty() {
                if tags.is_empty() {
                    println!("No tests found");
                } else {
                    println!("No tests found for tags: {}", tags.join(", "));
                }
                return Ok(());
            }

            println!(
                "\n  \x1b[36m\x1b[1m🔱 Trident\x1b[0m  {} tests discovered\n",
                tests.len()
            );

            let results = tokio::select! {
                result = scenarios::integration::run_all(
                    &tests,
                    &game_dir,
                    data_dir.as_deref(),
                    jobs,
                    retries,
                    &out_dir,
                    render.as_deref(),
                    &game_args,
                ) => result?,
                () = shutdown_signal() => {
                    anyhow::bail!("interrupted (SIGINT/SIGTERM) — shutting down and killing games");
                }
            };

            if let Some(n) = profile {
                scenarios::integration::print_profile(&results, n);
            }

            let failed = results.iter().filter(|(_, r)| !r.passed).count();
            if failed > 0 {
                std::process::exit(1);
            }
        }
        Commands::Stress {
            path,
            game_dir,
            data_dir,
            output_dir,
            render,
            game_args,
        } => {
            let out_dir = scenarios::integration::resolve_output_dir(output_dir.as_deref())?;
            tracing::info!("Output: {}", out_dir.display());

            let scenario_dir = std::path::absolute(std::path::Path::new(&path))
                .with_context(|| format!("failed to resolve stress scenario '{path}'"))?;
            if !scenarios::stress::is_stress_scenario(&scenario_dir) {
                anyhow::bail!(
                    "'{}' is not a .stress scenario directory containing stress.toml",
                    scenario_dir.display()
                );
            }

            let result = tokio::select! {
                result = scenarios::stress::run_stress_scenario(
                    &scenario_dir,
                    &game_dir,
                    data_dir.as_deref(),
                    &out_dir,
                    render.as_deref(),
                    &game_args,
                ) => result?,
                () = shutdown_signal() => {
                    anyhow::bail!("interrupted (SIGINT/SIGTERM) — shutting down and killing games");
                }
            };

            println!("{}", result.message);
            if !result.passed {
                std::process::exit(1);
            }
        }
        Commands::Console {
            game_dir,
            data_dir,
            port,
            attach,
        } => {
            console::run(&game_dir, data_dir.as_deref(), port, attach.as_deref()).await?;
        }
        Commands::Exec {
            code,
            game_dir,
            data_dir,
            port,
        } => {
            let sqf = if let Some(c) = code {
                c
            } else {
                use std::io::Read;
                let mut buf = String::new();
                std::io::stdin().read_to_string(&mut buf)?;
                buf
            };

            let config = config::get().client_config();
            let timeout = config::get().timeout;

            // Boot game with ephemeral config
            let tmp_dir = tempfile::tempdir()?;
            let user_dir = tmp_dir.path().to_string_lossy().to_string();
            let env_vars = [
                ("POSEIDON_USER_DIR", user_dir.as_str()),
                ("POSEIDON_CACHE_DIR", user_dir.as_str()),
            ];

            let mut game = client::GameInstance::spawn_with_env(
                &game_dir,
                data_dir.as_deref(),
                port,
                &["--window", "--no-splash"],
                &env_vars,
                None,
                &config,
            )
            .await?;

            game.wait_ready(timeout).await?;

            // Use eval (returns result) for single expressions
            let result = game.client().eval(&sqf).await?;
            println!("{result}");

            // Send exit to clean up
            let _ = game.client().exec("triEndTest").await;
            let _ = game.wait_exit(timeout).await;
        }
    }

    Ok(())
}
