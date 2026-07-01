//! Game instance management — spawn game binary with `--harness`, connect client.

use super::connection::ClientConfig;
use super::HarnessClient;
use crate::protocol::Event;
use anyhow::{Context, Result};
use std::path::{Path, PathBuf};
use std::process::Stdio;
use std::time::Duration;
use tokio::io::AsyncBufReadExt;
use tokio::process::{Child, Command};

/// A running game instance with a connected harness client.
pub struct GameInstance {
    process: Child,
    client: Option<HarnessClient>,
    port: u16,
    game_binary: PathBuf,
    work_dir: PathBuf,
    config: ClientConfig,
}

impl GameInstance {
    /// Spawn a game instance with `--harness <port>` and optional extra args.
    /// Uses the provided [`ClientConfig`] for connection timeouts and retry behaviour.
    ///
    /// `game_dir` is where the binary lives. If game data is in a different
    /// directory, pass `data_dir` — the process will use `-C <data_dir>` to set
    /// the game's working directory.
    pub async fn spawn(
        game_dir: &str,
        port: u16,
        extra_args: &[&str],
        config: &ClientConfig,
    ) -> Result<Self> {
        Self::spawn_with_data(game_dir, None, port, extra_args, config).await
    }

    /// Like [`Self::spawn`] but with an explicit data directory and environment overrides.
    pub async fn spawn_with_data(
        game_dir: &str,
        data_dir: Option<&str>,
        port: u16,
        extra_args: &[&str],
        config: &ClientConfig,
    ) -> Result<Self> {
        Self::spawn_with_env(game_dir, data_dir, port, extra_args, &[], None, config).await
    }

    /// Full-featured spawn: data directory, extra args, environment variable overrides,
    /// and optional binary-name preference.
    pub async fn spawn_with_env(
        game_dir: &str,
        data_dir: Option<&str>,
        port: u16,
        extra_args: &[&str],
        env_vars: &[(&str, &str)],
        binary_name: Option<&str>,
        config: &ClientConfig,
    ) -> Result<Self> {
        let game_dir = std::path::absolute(Path::new(game_dir))
            .with_context(|| format!("failed to resolve game_dir '{game_dir}'"))?;
        let binary = find_game_binary(&game_dir, binary_name)?;

        let work_dir = data_dir.map_or_else(|| game_dir.clone(), PathBuf::from);
        let work_dir = std::path::absolute(&work_dir)
            .with_context(|| format!("failed to resolve work_dir '{}'", work_dir.display()))?;

        let mut cmd = Command::new(&binary);
        cmd.arg("--harness").arg(port.to_string()); // port=0 → game auto-assigns

        // If data_dir differs from game_dir, pass -C to set game working directory
        if data_dir.is_some() {
            cmd.arg("-C").arg(&work_dir);
        }

        for &(key, val) in env_vars {
            cmd.env(key, val);
        }

        // When port=0 the game picks its own port and announces it on stdout.
        // We must pipe stdout to read that announcement; then save remainder to log.
        let output_dir = env_vars
            .iter()
            .find(|(k, _)| *k == "TRI_OUTPUT_DIR")
            .map(|(_, v)| std::path::Path::new(v).to_path_buf());

        let auto_port = port == 0;

        let stdout_log = output_dir.as_ref().and_then(|d| {
            let _ = std::fs::create_dir_all(d);
            std::fs::File::create(d.join("game_stdout.log")).ok()
        });

        let (stdout_cfg, stderr_cfg) = if auto_port {
            let stderr = output_dir.as_ref().and_then(|d| {
                let _ = std::fs::create_dir_all(d);
                std::fs::File::create(d.join("game_stderr.log")).ok()
            });
            (Stdio::piped(), stderr.map_or_else(Stdio::null, Stdio::from))
        } else {
            let log = output_dir.as_ref().and_then(|d| {
                let _ = std::fs::create_dir_all(d);
                std::fs::File::create(d.join(format!("game_{port}.log"))).ok()
            });
            let stderr = log.as_ref().and_then(|f| f.try_clone().ok());
            (
                log.map_or_else(Stdio::null, Stdio::from),
                stderr.map_or_else(Stdio::null, Stdio::from),
            )
        };

        cmd.args(extra_args)
            .current_dir(&work_dir)
            .stdin(Stdio::null())
            .stdout(stdout_cfg)
            .stderr(stderr_cfg)
            // Kill the game synchronously when the GameInstance Drop
            // runs.  Without this, retried/timed-out tests leak game
            // processes: Drop's `start_kill()` is fire-and-forget on
            // Windows (TerminateProcess is queued, kernel may process
            // it after tri exits), so the game survives as a parentless
            // orphan with stdio handles still open.  `kill_on_drop`
            // makes the std-lib internal Drop wait for the kill to
            // complete before returning.
            .kill_on_drop(true);

        tracing::debug!(
            "Spawning {} --harness {port} {}",
            binary.display(),
            extra_args.join(" ")
        );
        let mut process = cmd.spawn().map_err(|e| {
            anyhow::anyhow!(
                "failed to spawn game binary '{}' (cwd '{}'): {e}",
                binary.display(),
                work_dir.display()
            )
        })?;

        let actual_port = if auto_port {
            read_harness_port(&mut process, stdout_log, config.connect_timeout)
                .await
                .with_context(|| {
                    format!(
                        "while waiting for HARNESS_PORT from '{}' cwd='{}' args='--harness {port} {}'",
                        binary.display(),
                        work_dir.display(),
                        extra_args.join(" ")
                    )
                })?
        } else {
            port
        };

        let mut instance = Self {
            process,
            client: None,
            port: actual_port,
            game_binary: binary,
            work_dir,
            config: config.clone(),
        };

        instance.connect_with_retry().await?;
        Ok(instance)
    }

    /// Retry connecting to the harness TCP port using settings from [`ClientConfig`].
    async fn connect_with_retry(&mut self) -> Result<()> {
        let addr = format!("127.0.0.1:{}", self.port);
        let max_retries = self.config.max_retries;
        let delay = self.config.retry_delay;

        for attempt in 1..=max_retries {
            // Check if the process has already crashed before retrying
            if let Some(status) = self.process.try_wait()? {
                anyhow::bail!(
                    "game process '{}' exited with {} before harness became reachable on {addr}",
                    self.game_binary.display(),
                    status
                );
            }

            match HarnessClient::connect_with_config(&addr, self.config.clone()).await {
                Ok(client) => {
                    tracing::debug!("Connected to harness on attempt {attempt}");
                    self.client = Some(client);
                    return Ok(());
                }
                Err(_) if attempt < max_retries => {
                    tracing::trace!(
                        "Harness not ready on attempt {attempt}/{max_retries}, retrying in {delay:?}"
                    );
                    tokio::time::sleep(delay).await;
                }
                Err(e) => {
                    return Err(e).context(format!(
                        "failed to connect to harness at {addr} after {max_retries} attempts \
                         (game binary: {})",
                        self.game_binary.display()
                    ));
                }
            }
        }
        unreachable!()
    }

    /// Get a mutable reference to the harness client.
    pub fn client(&mut self) -> &mut HarnessClient {
        self.client.as_mut().expect("harness client not connected")
    }

    /// Take ownership of the harness client (disconnects it from this instance).
    pub fn take_client(&mut self) -> HarnessClient {
        self.client.take().expect("harness client not connected")
    }

    /// Restore a previously detached harness client.
    pub fn restore_client(&mut self, client: HarnessClient) {
        self.client = Some(client);
    }

    /// Ask a still-attached harness client to end the test.
    pub async fn request_end_test(&mut self) {
        if let Some(client) = self.client.as_mut() {
            let _ = client.exec("triEndTest").await;
        }
    }

    /// Wait for the "ready" event from the harness (main menu loaded).
    pub async fn wait_ready(&mut self, timeout: Duration) -> Result<Event> {
        self.client().wait_for_ready(timeout).await
    }

    /// Wait for the game process to exit and return the exit code.
    ///
    /// The shutdown budget is decoupled from the per-test assertion
    /// timeout: even fast tests (`timeout = 30` in their TOML) get
    /// at least [`EXIT_GRACE`] for the engine to tear down GL,
    /// flush audio, save config, etc.  Under parallel load (3
    /// games sharing a GPU + audio device) shutdown can take
    /// 30+ seconds even for a test whose body ran in 5 s.
    pub async fn wait_exit(&mut self, timeout: Duration) -> Result<i32> {
        const EXIT_GRACE: Duration = Duration::from_secs(60);
        let effective = timeout.max(EXIT_GRACE);
        match tokio::time::timeout(effective, self.process.wait()).await {
            Ok(Ok(status)) => Ok(status.code().unwrap_or(-1)),
            Ok(Err(e)) => Err(e.into()),
            Err(_) => {
                tracing::warn!("Game didn't exit within timeout, killing");
                self.process.kill().await.ok();
                anyhow::bail!("game process didn't exit within {effective:?}")
            }
        }
    }

    /// Aggressive shutdown after an assertion failure — sends a
    /// graceful `triEndTest` with a short deadline, then force-kills
    /// the game if it doesn't go quietly.  Avoids the 60-second
    /// `EXIT_GRACE` of `wait_exit`, which is sized for clean test
    /// completions under GPU/audio contention — on a known-failing
    /// test, waiting that long is pure latency, and parallel runners
    /// stack the latency to ~16 minutes for 14 failures.  Caller has
    /// already established the test failed; this just reclaims the
    /// slot.
    pub async fn kill_after_failure(&mut self) {
        // Try graceful first (5s budget) — many failures still leave
        // a healthy harness loop that can respond to triEndTest, and
        // a clean exit lets the engine save audio.cfg / etc.
        const GRACEFUL_DEADLINE: Duration = Duration::from_secs(5);
        if let Some(client) = self.client.as_mut() {
            let _ = tokio::time::timeout(GRACEFUL_DEADLINE, client.exec("triEndTest")).await;
        }
        if let Ok(Ok(_)) = tokio::time::timeout(GRACEFUL_DEADLINE, self.process.wait()).await {
            return;
        }
        // Wedged — TerminateProcess + wait synchronously so the OS
        // has actually reaped the process before we return.
        tracing::warn!("kill_after_failure: forcing termination");
        self.process.kill().await.ok();
        let _ = tokio::time::timeout(Duration::from_secs(5), self.process.wait()).await;
    }

    /// Get the port this instance is running on.
    pub const fn port(&self) -> u16 {
        self.port
    }

    /// Get the path to the game binary.
    pub fn binary_path(&self) -> &Path {
        &self.game_binary
    }

    /// Get the working directory used by this game instance.
    pub fn work_dir(&self) -> &Path {
        &self.work_dir
    }
}

// No manual Drop — the Command was built with `.kill_on_drop(true)`
// (see spawn_with_env), which is the canonical, synchronous cleanup
// path.  An explicit Drop calling `start_kill()` here would race with
// kill_on_drop's std-lib Drop and could TerminateProcess on a
// recycled PID if the child has already exited.

/// Find the game binary in a game directory.
///
/// When `named` is Some, looks for that specific binary name first.
/// When `None`, falls back to the default game-binary candidates.
pub fn find_game_binary(game_dir: &Path, named: Option<&str>) -> Result<PathBuf> {
    let ext = if cfg!(windows) { ".exe" } else { "" };
    let with_ext = |n: &str| {
        if n.ends_with(ext) {
            n.to_string()
        } else {
            format!("{n}{ext}")
        }
    };

    // Search game_dir and its bin/ first, then every sibling dir (and their bin/),
    // for `name`. The sibling sweep lets a stale or renamed dist dir still resolve
    // — a game_dir of dist/win-x64-clang-rwdi finds the binary in the actual
    // dist/x64-win-rwdi sitting next to it.
    let locate = |name: &str| -> Option<PathBuf> {
        let mut search = vec![game_dir.join(name), game_dir.join("bin").join(name)];
        if let Some(parent) = game_dir.parent() {
            if let Ok(entries) = std::fs::read_dir(parent) {
                for e in entries.flatten() {
                    let p = e.path();
                    if p.is_dir() && p != game_dir {
                        search.push(p.join(name));
                        search.push(p.join("bin").join(name));
                    }
                }
            }
        }
        search.into_iter().find(|p| p.exists())
    };

    // An explicit --binary takes priority (it may live in a sibling app dir, e.g.
    // PoseidonServer in apps/Server while game_dir points at apps/Game).
    if let Some(n) = named {
        if let Some(found) = locate(&with_ext(n)) {
            return Ok(found);
        }
    }

    let defaults: [&str; 2] = if cfg!(windows) {
        ["PoseidonGame.exe", "OFPR.exe"]
    } else {
        ["PoseidonGame", "OFPR"]
    };
    for name in &defaults {
        if let Some(found) = locate(&with_ext(name)) {
            return Ok(found);
        }
    }

    anyhow::bail!(
        "no game binary found in '{}' — looked for {:?} (also checked bin/ + sibling dirs)",
        game_dir.display(),
        defaults
    )
}

/// Read game stdout looking for `HARNESS_PORT=N` announced by the game after binding.
/// Remaining stdout lines are saved to `log_file` (if provided) so game logs are not lost.
async fn read_harness_port(
    process: &mut Child,
    log_file: Option<std::fs::File>,
    timeout: Duration,
) -> Result<u16> {
    let stdout = process
        .stdout
        .take()
        .context("stdout not piped for harness port discovery")?;
    let mut lines = tokio::io::BufReader::new(stdout).lines();
    let deadline = tokio::time::Instant::now() + timeout;
    let mut early_stdout = Vec::new();
    loop {
        let line = tokio::time::timeout_at(deadline, lines.next_line())
            .await
            .map_err(|_| anyhow::anyhow!("timeout waiting for HARNESS_PORT announcement"))?
            .context("I/O error reading game stdout")?;
        match line {
            None => {
                let excerpt = if early_stdout.is_empty() {
                    "<no stdout before exit>".to_string()
                } else {
                    early_stdout.join("\n")
                };
                anyhow::bail!(
                    "game closed stdout before announcing HARNESS_PORT; early stdout:\n{excerpt}"
                );
            }
            Some(line) => {
                if let Some(rest) = line.trim().strip_prefix("HARNESS_PORT=") {
                    if let Ok(port) = rest.parse::<u16>() {
                        // Drain remaining stdout to log file (or discard) so the pipe doesn't block
                        tokio::spawn(async move {
                            let mut writer = log_file.map(tokio::fs::File::from_std);
                            while let Ok(Some(l)) = lines.next_line().await {
                                if let Some(w) = writer.as_mut() {
                                    use tokio::io::AsyncWriteExt;
                                    let _ = w.write_all(format!("{l}\n").as_bytes()).await;
                                }
                            }
                        });
                        return Ok(port);
                    }
                }
                if early_stdout.len() < 80 {
                    early_stdout.push(line);
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::TempDir;

    #[test]
    fn find_binary_not_found() {
        let dir = TempDir::new().unwrap();
        // Nested dir so the sibling sweep only sees the (empty) TempDir, not
        // whatever else happens to be in the system temp root.
        let game = dir.path().join("game");
        std::fs::create_dir(&game).unwrap();
        let result = find_game_binary(&game, None);
        assert!(result.is_err());
        let msg = result.unwrap_err().to_string();
        assert!(msg.contains("no game binary found"));
    }

    #[test]
    fn find_binary_in_sibling_dir() {
        // A game_dir whose own binary is missing but a *sibling* dir has it (the
        // stale/renamed dist case, e.g. win-x64-clang-rwdi vs x64-win-rwdi) must
        // still resolve via the sibling sweep. Before the fix this bailed.
        let root = TempDir::new().unwrap();
        let wrong = root.path().join("win-x64-clang-rwdi");
        std::fs::create_dir(&wrong).unwrap();
        let right = root.path().join("x64-win-rwdi");
        std::fs::create_dir(&right).unwrap();
        let binary = right.join(if cfg!(windows) {
            "PoseidonGame.exe"
        } else {
            "PoseidonGame"
        });
        std::fs::write(&binary, "").unwrap();

        let result = find_game_binary(&wrong, None).unwrap();
        assert_eq!(result, binary);
    }

    #[test]
    fn find_binary_direct() {
        let dir = TempDir::new().unwrap();
        let binary = dir.path().join(if cfg!(windows) {
            "PoseidonGame.exe"
        } else {
            "PoseidonGame"
        });
        std::fs::write(&binary, "").unwrap();
        let result = find_game_binary(dir.path(), None).unwrap();
        assert_eq!(result, binary);
    }

    #[test]
    fn find_binary_in_bin_subdir() {
        let dir = TempDir::new().unwrap();
        let bin_dir = dir.path().join("bin");
        std::fs::create_dir(&bin_dir).unwrap();
        let binary = bin_dir.join(if cfg!(windows) {
            "PoseidonGame.exe"
        } else {
            "PoseidonGame"
        });
        std::fs::write(&binary, "").unwrap();
        let result = find_game_binary(dir.path(), None).unwrap();
        assert_eq!(result, binary);
    }

    #[test]
    fn global_config_derives_client_config() {
        let cfg = crate::config::TridentConfig::new(5);
        let cc = cfg.client_config();
        assert_eq!(cc.max_retries, 10); // 5s * 2
        assert_eq!(cc.retry_delay, Duration::from_millis(500));
        assert_eq!(cc.command_timeout, Duration::from_secs(5));
    }
}
