//! Integration test runner — discovers and runs `*.test.sqf` scenarios.
//!
//! The entire .test.sqf file is sent to the game via exec.
//! SQF drives the full flow using trident* commands.
//! The runner boots the game, waits for menu, execs the file, waits for exit.
//!
//! Test metadata is provided via optional sidecar TOML files:
//!   - For `foo.test.sqf`, look for `foo.test.toml` in the same directory.
//!   - For mission dirs `foo.test.eden/`, look for `foo.test.eden.toml` in the parent dir.
//!
//! Supported TOML fields:
//!   `mission` = "path/to/mission.island"  — load mission (implies `no_menu`)
//!   `timeout` = 60                        — per-test timeout override (seconds)
//!   `test_type` = "autotest"              — pass `--test-type`
//!   `no_menu` = true                      — don't require main menu IDD 0
//!   `no_player` = true                    — mission has no player; skip player-ready wait
//!   `extra_args` = \["--flag", "value"\]  — additional CLI args

use crate::client::GameInstance;
use crate::scenarios::ScenarioResult;
use anyhow::{Context, Result};
use std::cmp::Reverse;
use std::collections::BTreeSet;
use std::path::{Path, PathBuf};
use std::time::Instant;

const MAIN_MENU_IDD: i32 = 0;

/// Resolve or create the output directory for test artifacts (screenshots, logs).
/// If `explicit` is given, use it. Otherwise create `tmp/tri/<timestamp>/`.
pub fn resolve_output_dir(explicit: Option<&str>) -> Result<PathBuf> {
    let dir = explicit.map_or_else(
        || {
            use std::time::SystemTime;
            let secs = SystemTime::now()
                .duration_since(SystemTime::UNIX_EPOCH)
                .unwrap_or_default()
                .as_secs();
            PathBuf::from(format!("tmp/tri/{secs}"))
        },
        PathBuf::from,
    );
    std::fs::create_dir_all(&dir)
        .with_context(|| format!("failed to create output dir '{}'", dir.display()))?;
    Ok(std::path::absolute(&dir).unwrap_or(dir))
}

/// A discovered integration test.
pub struct IntegrationTest {
    pub name: String,
    pub kind: TestKind,
    pub tags: Vec<String>,
}

impl IntegrationTest {
    /// Number of renderer processes this test needs (1 for Sqf/Mission/Seq).
    /// Seq runs its phases one at a time, so it never holds more than one instance.
    pub fn instance_count(&self) -> u32 {
        match &self.kind {
            TestKind::Sqf(_) | TestKind::Mission(_) | TestKind::Seq(_) => 1,
            TestKind::Multi(dir) => super::multi::gui_instance_count(dir),
        }
    }

    /// Semaphore slots this test reserves in the parallel runner.
    ///
    /// Multi-instance tests reserve one slot per renderer process. Tests that need
    /// full isolation can opt out of overlap with `tags = ["exclusive"]`.
    pub fn parallel_slots(&self, max_slots: u32) -> u32 {
        match &self.kind {
            _ if self.tags.iter().any(|tag| tag == "exclusive") => max_slots,
            TestKind::Sqf(_) | TestKind::Mission(_) | TestKind::Multi(_) | TestKind::Seq(_) => {
                self.instance_count().min(max_slots)
            }
        }
    }

    fn reserves_full_pool(&self, max_slots: u32) -> bool {
        max_slots > 1 && self.parallel_slots(max_slots) >= max_slots
    }
}

/// What drives the test.
#[derive(Clone)]
pub enum TestKind {
    /// A `.test.sqf` file — runner sends statements via harness.
    Sqf(PathBuf),
    /// A `.test.{island}` mission dir — mission scripts drive via tri* commands.
    Mission(PathBuf),
    /// A `.test/` dir with `test.toml` `[[instances]]` — multi-instance orchestration.
    Multi(PathBuf),
    /// A `.seq/` dir with ordered `*.test.sqf` phases — each phase is a fresh game
    /// launch, run sequentially against one shared `POSEIDON_USER_DIR` so later
    /// phases see the state earlier ones left behind.
    Seq(PathBuf),
}

fn scheduled_tests(tests: &[IntegrationTest], max_slots: u32) -> Vec<&IntegrationTest> {
    let mut ordered: Vec<_> = tests.iter().enumerate().collect();
    ordered.sort_by_key(|(index, test)| (test.reserves_full_pool(max_slots), *index));
    ordered.into_iter().map(|(_, test)| test).collect()
}

/// Discover tests recursively under `root`.
///
/// Finds `*.test.sqf` files and `*.test.{island}` directories (containing `mission.sqm`).
pub fn discover(root: &Path) -> Result<Vec<IntegrationTest>> {
    let mut tests = Vec::new();
    collect_tests(root, root, &mut tests)?;
    tests.sort_by(|a, b| a.name.cmp(&b.name));
    Ok(tests)
}

fn collect_tests(base: &Path, dir: &Path, out: &mut Vec<IntegrationTest>) -> Result<()> {
    if !dir.is_dir() {
        return Ok(());
    }
    for entry in std::fs::read_dir(dir)? {
        let entry = entry?;
        let path = entry.path();
        if path.is_dir() {
            if is_seq_dir(&path) {
                let rel = path
                    .strip_prefix(base)
                    .unwrap_or(&path)
                    .to_string_lossy()
                    .replace('\\', "/");
                let tags = load_test_tags(&path);
                out.push(IntegrationTest {
                    name: seq_test_name(&rel),
                    kind: TestKind::Seq(path),
                    tags,
                });
            } else if super::multi::is_multi_test(&path) {
                let rel = path
                    .strip_prefix(base)
                    .unwrap_or(&path)
                    .to_string_lossy()
                    .replace('\\', "/");
                let name = super::multi::multi_test_name(&rel);
                let tags = load_test_tags(&path);
                out.push(IntegrationTest {
                    name,
                    kind: TestKind::Multi(path),
                    tags,
                });
            } else if is_test_mission(&path) {
                let rel = path
                    .strip_prefix(base)
                    .unwrap_or(&path)
                    .to_string_lossy()
                    .replace('\\', "/");
                let name = mission_test_name(&rel);
                let tags = load_test_tags(&path);
                out.push(IntegrationTest {
                    name,
                    kind: TestKind::Mission(path),
                    tags,
                });
            } else {
                collect_tests(base, &path, out)?;
            }
        } else if is_test_sqf(&path) {
            let rel = path
                .strip_prefix(base)
                .unwrap_or(&path)
                .to_string_lossy()
                .replace('\\', "/");
            let name = strip_test_suffix(&rel);
            let tags = load_test_tags(&path);
            out.push(IntegrationTest {
                name,
                kind: TestKind::Sqf(path),
                tags,
            });
        }
    }
    Ok(())
}

/// Build a test from a single path (`.test.sqf` file, `.test.{island}` dir, or `.test/` dir).
pub fn from_path(path: &Path) -> Result<IntegrationTest> {
    let name_raw = path.file_name().unwrap_or_default().to_string_lossy();
    if path.is_dir() && is_seq_dir(path) {
        Ok(IntegrationTest {
            name: seq_test_name(&name_raw),
            kind: TestKind::Seq(path.to_path_buf()),
            tags: load_test_tags(path),
        })
    } else if path.is_dir() && super::multi::is_multi_test(path) {
        Ok(IntegrationTest {
            name: super::multi::multi_test_name(&name_raw),
            kind: TestKind::Multi(path.to_path_buf()),
            tags: load_test_tags(path),
        })
    } else if path.is_dir() && is_test_mission(path) {
        Ok(IntegrationTest {
            name: mission_test_name(&name_raw),
            kind: TestKind::Mission(path.to_path_buf()),
            tags: load_test_tags(path),
        })
    } else if path.is_file() {
        Ok(IntegrationTest {
            name: strip_test_suffix(&name_raw),
            kind: TestKind::Sqf(path.to_path_buf()),
            tags: load_test_tags(path),
        })
    } else {
        anyhow::bail!(
            "not a .test.sqf file or .test.{{island}} dir: {}",
            path.display()
        );
    }
}

fn is_test_sqf(p: &Path) -> bool {
    p.extension().is_some_and(|ext| ext == "sqf")
        && p.file_name()
            .unwrap_or_default()
            .to_string_lossy()
            .contains(".test.")
}

/// A dir like `foo.test.eden/` or `bar.test.intro/` with a `mission.sqm` inside.
pub fn is_test_mission(p: &Path) -> bool {
    let name = p.file_name().unwrap_or_default().to_string_lossy();
    name.contains(".test.") && p.join("mission.sqm").exists()
}

/// A `*.seq/` directory holding ordered `*.test.sqf` phases, run sequentially
/// against one shared user dir (each phase a fresh game launch).
pub fn is_seq_dir(p: &Path) -> bool {
    if !p.is_dir() {
        return false;
    }
    let name = p.file_name().unwrap_or_default().to_string_lossy();
    if !name.ends_with(".seq") {
        return false;
    }
    std::fs::read_dir(p).is_ok_and(|rd| {
        rd.filter_map(std::result::Result::ok)
            .any(|e| is_test_sqf(&e.path()))
    })
}

/// Extract the test name from a `*.seq/` directory path.
fn seq_test_name(s: &str) -> String {
    s.strip_suffix(".seq").unwrap_or(s).to_string()
}

fn strip_test_suffix(s: &str) -> String {
    s.find(".test.")
        .map_or_else(|| s.to_string(), |pos| s[..pos].to_string())
}

// ── Multi-path resolution ──────────────────────────────────────────────────

/// Resolve one or more path arguments into a deduplicated, sorted test list.
///
/// Each argument may be:
/// - An exact `.test.sqf` file, `.test.<island>` dir, or `.test/` multi dir
/// - A directory (recursive discovery)
/// - A glob pattern (contains `*`, `?`, or `[`)
/// - A path prefix (e.g. `tests/main_menu/ex` matches `exit.test.sqf`)
///
/// Duplicates are removed by canonical file path (first occurrence wins).
pub fn resolve_tests(paths: &[&str]) -> Result<Vec<IntegrationTest>> {
    let mut seen: std::collections::HashSet<PathBuf> = std::collections::HashSet::new();
    let mut tests: Vec<IntegrationTest> = Vec::new();

    for &arg in paths {
        for test in resolve_one(arg)? {
            let canonical = canonical_kind_path(&test.kind);
            if seen.insert(canonical) {
                tests.push(test);
            }
        }
    }

    tests.sort_by(|a, b| a.name.cmp(&b.name));
    Ok(tests)
}

fn canonical_kind_path(kind: &TestKind) -> PathBuf {
    let p = match kind {
        TestKind::Sqf(p) | TestKind::Mission(p) | TestKind::Multi(p) | TestKind::Seq(p) => p,
    };
    std::path::absolute(p).unwrap_or_else(|_| p.clone())
}

fn resolve_one(arg: &str) -> Result<Vec<IntegrationTest>> {
    let path = Path::new(arg);

    // Glob pattern
    if arg.contains('*') || arg.contains('?') || arg.contains('[') {
        return resolve_glob(arg);
    }

    // Exact test file/dir
    if path.is_file()
        || is_test_mission(path)
        || super::multi::is_multi_test(path)
        || is_seq_dir(path)
    {
        return Ok(vec![from_path(path)?]);
    }

    // Directory — recursive discovery
    if path.is_dir() {
        let found = discover(path)?;
        if found.is_empty() {
            anyhow::bail!("no tests found in '{arg}'");
        }
        return Ok(found);
    }

    // Prefix — walk up to find an existing ancestor, discover, filter
    resolve_prefix(path)
}

fn resolve_glob(pattern: &str) -> Result<Vec<IntegrationTest>> {
    // Normalize separators for the glob crate
    let norm = pattern.replace('\\', "/");
    let entries = glob::glob(&norm).with_context(|| format!("invalid glob pattern '{pattern}'"))?;

    let mut tests = Vec::new();
    for entry in entries {
        let path = entry.with_context(|| format!("glob I/O error for '{pattern}'"))?;
        if path.is_file() && is_test_sqf(&path) {
            // A phase file inside a `.seq/` dir is only meaningful as part of the
            // sequence (shared user dir); a glob reaching into the dir must not
            // surface it as a standalone test with its own fresh dir.
            if path.parent().is_some_and(is_seq_dir) {
                continue;
            }
            tests.push(from_path(&path)?);
        } else if path.is_dir() {
            if is_test_mission(&path) || super::multi::is_multi_test(&path) || is_seq_dir(&path) {
                tests.push(from_path(&path)?);
            } else {
                tests.extend(discover(&path)?);
            }
        }
    }

    anyhow::ensure!(!tests.is_empty(), "no tests matched glob '{pattern}'");
    Ok(tests)
}

fn resolve_prefix(path: &Path) -> Result<Vec<IntegrationTest>> {
    let abs = std::path::absolute(path).unwrap_or_else(|_| path.to_path_buf());

    // A prefix is "<existing-dir>/<name-prefix>": discover from the immediate
    // parent and filter by the full prefix. Don't climb past the parent — for a
    // bogus deep path the climb lands on the drive root and discover() would scan
    // the entire drive (minutes) before failing.
    let base = match abs.parent() {
        Some(p) if p.is_dir() => p.to_path_buf(),
        _ => anyhow::bail!("no tests matched: '{}'", path.display()),
    };

    let needle = abs.to_string_lossy().replace('\\', "/");
    let matched: Vec<_> = discover(&base)?
        .into_iter()
        .filter(|t| {
            let p = match &t.kind {
                TestKind::Sqf(p) | TestKind::Mission(p) | TestKind::Multi(p) | TestKind::Seq(p) => {
                    p
                }
            };
            let hay = std::path::absolute(p)
                .unwrap_or_else(|_| p.clone())
                .to_string_lossy()
                .replace('\\', "/");
            hay.starts_with(&needle)
        })
        .collect();

    anyhow::ensure!(
        !matched.is_empty(),
        "no tests matched prefix '{}'",
        path.display()
    );
    Ok(matched)
}

/// For mission-dir tests, include the island suffix to disambiguate.
/// E.g. `benchmark_heli.test.eden` → `benchmark_heli_eden`.
fn mission_test_name(s: &str) -> String {
    s.find(".test.").map_or_else(
        || s.to_string(),
        |pos| {
            let prefix = &s[..pos];
            let island = &s[pos + 6..];
            format!("{prefix}_{island}")
        },
    )
}

/// Test metadata loaded from an optional sidecar `.toml` file.
#[derive(Default, serde::Deserialize)]
#[serde(default)]
struct TestMeta {
    /// Instance type: "client" (default) or "server"
    #[serde(rename = "type")]
    instance_type: Option<String>,
    mission: Option<String>,
    test_type: Option<String>,
    no_menu: bool,
    /// Set `true` for a mission with no playable player (e.g. an AI-only combat
    /// stress mission). Skips the `triMissionPlayerReady` handshake, which never
    /// completes without a player, so the SQF body can drive/sim the loaded mission.
    no_player: bool,
    extra_args: Vec<String>,
    timeout: Option<u64>,
    assert_timeout: Option<u64>,
    /// Binary-name override. When unset, the runner uses the default game binary.
    binary: Option<String>,
    /// Data-directory override for tests that need a package different from the default.
    /// When unset, the runner uses the `--data-dir` CLI / `OFPR_DATA_DIR` value.
    /// Resolved relative to the repo root, not to the test path.
    data_dir: Option<String>,
    /// Set `true` to suppress the default `--autotest` injection.
    /// Use only for tests that intentionally exercise error-recovery paths that
    /// would otherwise trip the hard-failure exit.
    no_autotest: bool,
    /// Startup UI/voice language passed to the game via `--lang`. When unset the
    /// harness forces `English` so tests are deterministic regardless of the host
    /// OS locale. Override per-scenario (e.g. `language = "Czech"`).
    language: Option<String>,
    tags: Vec<String>,
}

impl TestMeta {
    fn is_server(&self) -> bool {
        self.instance_type.as_deref() == Some("server")
    }

    /// Startup language to inject via `--lang`. Defaults to English.
    fn language(&self) -> &str {
        self.language.as_deref().unwrap_or("English")
    }
}

fn normalize_tag(tag: &str) -> String {
    tag.trim().to_ascii_lowercase()
}

fn load_test_tags(test_path: &Path) -> Vec<String> {
    load_test_meta(test_path)
        .tags
        .into_iter()
        .map(|tag| normalize_tag(&tag))
        .filter(|tag| !tag.is_empty())
        .collect()
}

pub fn filter_tests_by_tags(
    tests: Vec<IntegrationTest>,
    tags: &[String],
    skip_tags: &[String],
) -> Vec<IntegrationTest> {
    let wanted: Vec<String> = tags.iter().map(|tag| normalize_tag(tag)).collect();
    let skipped: Vec<String> = skip_tags.iter().map(|tag| normalize_tag(tag)).collect();

    tests
        .into_iter()
        .filter(|test| {
            let included = wanted.is_empty()
                || test
                    .tags
                    .iter()
                    .any(|tag| wanted.iter().any(|wanted| wanted == tag));
            let excluded = !skipped.is_empty()
                && test
                    .tags
                    .iter()
                    .any(|tag| skipped.iter().any(|skipped| skipped == tag));
            included && !excluded
        })
        .collect()
}

/// Load sidecar TOML metadata for a test.
///
/// For `foo.test.sqf`, looks for `foo.test.toml` in the same directory.
/// For mission dirs `foo.test.eden/`, looks for `foo.test.eden.toml` in the parent directory.
/// Returns `TestMeta::default()` if no sidecar file exists.
fn load_test_meta(test_path: &Path) -> TestMeta {
    let toml_path = if test_path.is_dir() {
        let inline_meta = test_path.join("test.toml");
        if inline_meta.exists() {
            inline_meta
        } else {
            // Mission dir: foo.test.eden/ → foo.test.eden.toml in parent
            let dir_name = test_path.file_name().unwrap_or_default().to_string_lossy();
            test_path.with_file_name(format!("{dir_name}.toml"))
        }
    } else {
        // SQF file: foo.test.sqf → foo.test.toml
        let name = test_path.file_name().unwrap_or_default().to_string_lossy();
        let toml_name = name.rfind('.').map_or_else(
            || format!("{name}.toml"),
            |pos| format!("{}.toml", &name[..pos]),
        );
        test_path.with_file_name(toml_name)
    };

    if !toml_path.exists() {
        return TestMeta::default();
    }

    match std::fs::read_to_string(&toml_path) {
        Ok(content) => match toml::from_str::<TestMeta>(&content) {
            Ok(meta) => {
                tracing::debug!("loaded test meta from {}", toml_path.display());
                meta
            }
            Err(e) => {
                tracing::warn!("failed to parse {}: {e}", toml_path.display());
                TestMeta::default()
            }
        },
        Err(e) => {
            tracing::warn!("failed to read {}: {e}", toml_path.display());
            TestMeta::default()
        }
    }
}

/// Expand `#include "path"` directives in SQF source before statement parsing.
///
/// `base_dir` is the directory of the file being expanded; include paths are
/// resolved relative to it.  Includes are expanded recursively (an included file
/// may itself contain `#include` directives resolved relative to that file's
/// own directory).  Directive lines are replaced by the contents of the included
/// file; all other lines are passed through unchanged.
///
/// Syntax: `#include "relative/path.sqf"` — the `#include` must be at the start
/// of the (optionally whitespace-trimmed) line and the path must be
/// double-quoted.  Lines that don't match that exact form are kept verbatim.
pub fn expand_includes(source: &str, base_dir: &std::path::Path) -> Result<String> {
    let mut result = String::with_capacity(source.len());
    for line in source.lines() {
        let trimmed = line.trim();
        if let Some(rest) = trimmed.strip_prefix("#include ") {
            let rest = rest.trim();
            if rest.starts_with('"') && rest.ends_with('"') && rest.len() >= 2 {
                let include_path = &rest[1..rest.len() - 1];
                let full_path = base_dir.join(include_path);
                let content = std::fs::read_to_string(&full_path)
                    .with_context(|| format!("failed to read include '{}'", full_path.display()))?;
                let include_base = full_path.parent().unwrap_or(base_dir);
                let expanded = expand_includes(&content, include_base)?;
                result.push_str(&expanded);
                if !result.ends_with('\n') {
                    result.push('\n');
                }
                continue;
            }
        }
        result.push_str(line);
        result.push('\n');
    }
    Ok(result)
}

/// Split SQF source into individual statements.
///
/// Rules:
/// - `//` strips the rest of the line (harness line comment), recognised only OUTSIDE
///   string literals, so a `//` inside an argument string passes through untouched.
/// - String literals ("…", with "" as an embedded quote) are copied verbatim, so a
///   `;` or `//` inside a string is never taken as a separator/comment — the game sees
///   the string exactly as written. (e.g. "#login", "a;b").
/// - Inside a `{…}` block (`brace_depth` > 0) the content is kept verbatim so that
///   multi-line `{ … }` blocks are sent as a single eval call.
/// - Outside any block or string, `;` and newlines act as statement separators —
///   exactly as the original game treats `;`, so the harness never re-interprets a
///   semicolon that belongs to a string or a sub-expression.
pub fn parse_statements(source: &str) -> Vec<String> {
    let mut statements = Vec::new();
    let mut current = String::new();
    let mut brace_depth: i32 = 0;
    let mut in_line_comment = false;
    let mut in_string = false;

    let chars: Vec<char> = source.chars().collect();
    let mut i = 0;
    while i < chars.len() {
        let c = chars[i];

        // Newline ends a line comment; outside a block/string it flushes the statement.
        if c == '\n' || c == '\r' {
            if c == '\n' {
                in_line_comment = false;
            }
            if brace_depth == 0 && !in_string {
                let trimmed = current.trim().to_string();
                if !trimmed.is_empty() {
                    statements.push(trimmed);
                }
                current.clear();
            } else {
                // Inside a block (or an unterminated string): preserve the newline.
                current.push(c);
            }
            i += 1;
            continue;
        }

        if in_line_comment {
            i += 1;
            continue;
        }

        // Inside a string literal: copy verbatim. "" is an embedded quote; a lone " ends it.
        if in_string {
            current.push(c);
            if c == '"' {
                if i + 1 < chars.len() && chars[i + 1] == '"' {
                    current.push('"');
                    i += 2;
                    continue;
                }
                in_string = false;
            }
            i += 1;
            continue;
        }

        // `//` line comment — only outside strings.
        if c == '/' && i + 1 < chars.len() && chars[i + 1] == '/' {
            in_line_comment = true;
            i += 2;
            continue;
        }

        if c == '"' {
            in_string = true;
            current.push(c);
            i += 1;
            continue;
        }

        if c == '{' {
            brace_depth += 1;
            current.push(c);
            i += 1;
            continue;
        }

        if c == '}' {
            brace_depth = (brace_depth - 1).max(0);
            current.push(c);
            i += 1;
            continue;
        }

        // `;` at top level (outside any block/string): statement separator.
        if c == ';' && brace_depth == 0 {
            let trimmed = current.trim().to_string();
            if !trimmed.is_empty() {
                statements.push(trimmed);
            }
            current.clear();
            i += 1;
            continue;
        }

        current.push(c);
        i += 1;
    }

    // Flush any remaining content after the last separator.
    let trimmed = current.trim().to_string();
    if !trimmed.is_empty() {
        statements.push(trimmed);
    }

    statements
}

#[derive(Clone, Copy)]
enum WaitMode {
    RealTime,
    WaitFrames(u32),
    SimFrames(u32),
}

struct ConditionOutcome {
    succeeded: bool,
    last_result: String,
}

fn parse_condition_statement(stmt: &str, prefix: &str) -> Option<String> {
    let body = stmt.strip_prefix(prefix)?.trim();
    if body.starts_with('{') && body.ends_with('}') && body.len() >= 2 {
        let inner = body[1..body.len() - 1].trim();
        if inner.is_empty() {
            None
        } else {
            Some(inner.to_string())
        }
    } else if body.is_empty() {
        None
    } else {
        Some(body.to_string())
    }
}

fn condition_is_success(result: &str) -> bool {
    result == "OK" || result.eq_ignore_ascii_case("true")
}

fn normalize_condition_failure(result: &str) -> String {
    if result.starts_with("FAIL:") {
        result.to_string()
    } else if result.eq_ignore_ascii_case("false") {
        "FAIL:false".to_string()
    } else {
        format!("FAIL:{}", result.trim())
    }
}

fn parse_triscreenshot_label(stmt: &str) -> Option<String> {
    let rest = stmt.trim().strip_prefix("triScreenshot")?.trim_start();
    let mut chars = rest.chars();
    if chars.next()? != '"' {
        return None;
    }

    let mut label = String::new();
    while let Some(c) = chars.next() {
        if c == '"' {
            if matches!(chars.clone().next(), Some('"')) {
                chars.next();
                label.push('"');
                continue;
            }
            return Some(label);
        }
        label.push(c);
    }
    None
}

async fn wait_for_screenshot_file(
    name: &str,
    _game: &mut GameInstance,
    output_dir: &Path,
    seq: usize,
    label: &str,
    timeout: std::time::Duration,
    poll_interval: std::time::Duration,
) -> ConditionOutcome {
    let deadline = Instant::now() + timeout;
    let stem = format!("{seq:03}_{label}");
    let png = output_dir.join(format!("{stem}.png"));
    let bmp = output_dir.join(format!("{stem}.bmp"));

    loop {
        let png_ready = tokio::fs::metadata(&png)
            .await
            .map(|m| m.len() > 0)
            .unwrap_or(false);
        let bmp_ready = tokio::fs::metadata(&bmp)
            .await
            .map(|m| m.len() > 0)
            .unwrap_or(false);
        if png_ready || bmp_ready {
            tracing::debug!("[{name}]   → screenshot ready: {stem}");
            return ConditionOutcome {
                succeeded: true,
                last_result: stem,
            };
        }

        if Instant::now() >= deadline {
            return ConditionOutcome {
                succeeded: false,
                last_result: format!("FAIL:screenshot_not_written:{stem}"),
            };
        }

        tokio::time::sleep(poll_interval).await;
    }
}

async fn wait_for_condition(
    name: &str,
    game: &mut GameInstance,
    condition: &str,
    timeout: std::time::Duration,
    poll_interval: std::time::Duration,
    mode: WaitMode,
) -> ConditionOutcome {
    let deadline = Instant::now() + timeout;

    loop {
        match game.client().eval(condition).await {
            Ok(result) => {
                let result_str = result.trim_matches('"').to_string();
                if condition_is_success(&result_str) {
                    tracing::debug!("[{name}]   → condition OK: {condition}");
                    return ConditionOutcome {
                        succeeded: true,
                        last_result: result_str,
                    };
                }

                let last_result = normalize_condition_failure(&result_str);
                if Instant::now() >= deadline {
                    return ConditionOutcome {
                        succeeded: false,
                        last_result,
                    };
                }
            }
            Err(e) => {
                return ConditionOutcome {
                    succeeded: false,
                    last_result: format!("connection error: {e}"),
                };
            }
        }

        match mode {
            WaitMode::RealTime => tokio::time::sleep(poll_interval).await,
            WaitMode::WaitFrames(n) => {
                let wait_stmt = format!("triWaitFrames {n}");
                match game.client().eval(&wait_stmt).await {
                    Ok(result) => {
                        let result_str = result.trim_matches('"').to_string();
                        if !(result_str == "OK" || result_str.starts_with("OK:")) {
                            return ConditionOutcome {
                                succeeded: false,
                                last_result: normalize_condition_failure(&result_str),
                            };
                        }
                    }
                    Err(e) => {
                        return ConditionOutcome {
                            succeeded: false,
                            last_result: format!("connection error: {e}"),
                        };
                    }
                }
            }
            WaitMode::SimFrames(n) => {
                let sim_stmt = format!("triSimFrames {n}");
                match game.client().eval(&sim_stmt).await {
                    Ok(result) => {
                        let result_str = result.trim_matches('"').to_string();
                        if !(result_str == "OK" || result_str.starts_with("OK:")) {
                            return ConditionOutcome {
                                succeeded: false,
                                last_result: normalize_condition_failure(&result_str),
                            };
                        }
                    }
                    Err(e) => {
                        return ConditionOutcome {
                            succeeded: false,
                            last_result: format!("connection error: {e}"),
                        };
                    }
                }
            }
        }
    }
}

async fn finish_condition_failure(
    game: &mut GameInstance,
    start: Instant,
    stmt: &str,
    outcome: ConditionOutcome,
) -> Option<ScenarioResult> {
    if outcome.succeeded {
        return None;
    }

    game.kill_after_failure().await;
    let elapsed = start.elapsed();
    Some(ScenarioResult {
        passed: false,
        message: format!(
            "{stmt} — {} ({:.1}s)",
            outcome.last_result,
            elapsed.as_secs_f64()
        ),
        duration: elapsed,
    })
}

/// Run a single integration test with an ephemeral config directory.
#[allow(clippy::too_many_lines, clippy::cognitive_complexity)]
pub async fn run_test(
    test: &IntegrationTest,
    game_dir: &str,
    data_dir: Option<&str>,
    port: u16,
    output_dir: &Path,
    render: Option<&str>,
    cli_game_args: &[String],
) -> Result<ScenarioResult> {
    let test_output_dir = test_output_dir(output_dir, &test.name);
    if !matches!(&test.kind, TestKind::Multi(_)) {
        std::fs::create_dir_all(&test_output_dir)
            .with_context(|| format!("failed to create {}", test_output_dir.display()))?;
    }

    match &test.kind {
        TestKind::Sqf(sqf_file) => {
            run_sqf_test(
                &test.name,
                sqf_file,
                game_dir,
                data_dir,
                port,
                &test_output_dir,
                render,
                cli_game_args,
                None,
            )
            .await
        }
        TestKind::Seq(seq_dir) => {
            run_seq_test(
                &test.name,
                seq_dir,
                game_dir,
                data_dir,
                port,
                &test_output_dir,
                render,
                cli_game_args,
            )
            .await
        }
        TestKind::Mission(mission_dir) => {
            run_mission_test(
                &test.name,
                mission_dir,
                game_dir,
                data_dir,
                port,
                &test_output_dir,
                render,
                cli_game_args,
            )
            .await
        }
        TestKind::Multi(test_dir) => {
            super::multi::run_multi_test(
                &test.name,
                test_dir,
                game_dir,
                data_dir,
                output_dir,
                render,
                cli_game_args,
            )
            .await
        }
    }
}

fn test_output_dir(output_dir: &Path, test_name: &str) -> PathBuf {
    output_dir.join(super::multi::safe_output_name(test_name))
}

fn retry_output_dir(output_dir: &Path, attempt: usize) -> PathBuf {
    output_dir.join(format!("retry-{attempt}"))
}

fn insert_effective_data_dir(
    dirs: &mut BTreeSet<PathBuf>,
    override_dir: Option<&str>,
    default_dir: Option<&str>,
) {
    if let Some(data_dir) = override_dir.or(default_dir) {
        dirs.insert(super::multi::resolve_data_dir_path(data_dir));
    }
}

fn effective_data_dirs(
    tests: &[IntegrationTest],
    default_dir: Option<&str>,
) -> Result<Vec<PathBuf>> {
    let mut dirs = BTreeSet::new();
    for test in tests {
        match &test.kind {
            TestKind::Sqf(path) | TestKind::Mission(path) => {
                let meta = load_test_meta(path);
                insert_effective_data_dir(&mut dirs, meta.data_dir.as_deref(), default_dir);
            }
            TestKind::Multi(path) => {
                let configured = super::multi::configured_data_dir(path)?;
                insert_effective_data_dir(&mut dirs, configured.as_deref(), default_dir);
            }
            TestKind::Seq(path) => {
                for entry in std::fs::read_dir(path)
                    .with_context(|| format!("failed to read seq dir {}", path.display()))?
                {
                    let phase = entry?.path();
                    if is_test_sqf(&phase) {
                        let meta = load_test_meta(&phase);
                        insert_effective_data_dir(&mut dirs, meta.data_dir.as_deref(), default_dir);
                    }
                }
            }
        }
    }
    Ok(dirs.into_iter().collect())
}

fn clear_legacy_network_asset_dirs(
    tests: &[IntegrationTest],
    default_dir: Option<&str>,
) -> Result<()> {
    for data_dir in effective_data_dirs(tests, default_dir)? {
        super::multi::clear_legacy_network_asset_tmp(&data_dir)?;
    }
    Ok(())
}

/// Run a `.test.{island}` mission test — game loads the mission, mission scripts drive via tri*.
/// Supports optional TOML sidecar (e.g. `colorbar.test.intro.toml`) for `extra_args` and `timeout`.
#[allow(clippy::too_many_arguments)]
async fn run_mission_test(
    name: &str,
    mission_dir: &Path,
    game_dir: &str,
    data_dir: Option<&str>,
    _port: u16,
    output_dir: &Path,
    render: Option<&str>,
    cli_game_args: &[String],
) -> Result<ScenarioResult> {
    use std::process::Stdio;
    use tokio::process::Command;

    tracing::debug!("[{name}] mission test: {}", mission_dir.display());

    let meta = load_test_meta(mission_dir);
    let timeout = std::time::Duration::from_secs(meta.timeout.unwrap_or(120));
    let start = Instant::now();

    let tmp_dir = tempfile::tempdir().context("failed to create ephemeral config directory")?;
    let user_dir = tmp_dir.path().to_string_lossy().to_string();

    let resolved = if mission_dir.is_relative() {
        std::env::current_dir()
            .unwrap_or_default()
            .join(mission_dir)
            .to_string_lossy()
            .to_string()
    } else {
        mission_dir.to_string_lossy().to_string()
    };

    let game_dir_abs = std::path::absolute(Path::new(game_dir))
        .with_context(|| format!("failed to resolve game_dir '{game_dir}'"))?;
    let binary = crate::client::find_game_binary(&game_dir_abs, meta.binary.as_deref())?;

    // Per-test `data_dir` overrides the global package directory.
    // takes priority over the global --data-dir CLI value.
    let effective_data_dir = meta.data_dir.as_deref().or(data_dir);
    let work_dir = effective_data_dir.map_or_else(|| game_dir_abs.clone(), PathBuf::from);
    let work_dir = std::path::absolute(&work_dir)
        .with_context(|| format!("failed to resolve work_dir '{}'", work_dir.display()))?;

    let mut cmd = Command::new(&binary);
    cmd.args(["--window", "--no-splash", "--test-mission", &resolved]);
    cmd.args(["--lang", meta.language()]);
    if let Some(r) = render {
        cmd.args(["--render", r]);
    }
    // Automated tests run strict (the rwdi build players run defaults to --no-strict).
    // Injected before extra_args so a per-test `--no-strict` in the sidecar toml wins.
    cmd.args(["--strict"]);
    cmd.args(&meta.extra_args);
    cmd.args(cli_game_args);
    if effective_data_dir.is_some() {
        cmd.arg("-C").arg(&work_dir);
    }
    cmd.env("POSEIDON_USER_DIR", &user_dir)
        .env("POSEIDON_CACHE_DIR", &user_dir)
        .env("POSEIDON_TEMP_DIR", &user_dir)
        .env("TRI_OUTPUT_DIR", output_dir.as_os_str())
        .env("SDL_VIDEO_MINIMIZE_ON_FOCUS_LOSS", "0")
        .current_dir(&work_dir)
        .stdin(Stdio::null())
        .stdout(Stdio::null())
        .stderr(Stdio::null());

    tracing::debug!(
        "[{name}] spawning {} --test-mission {resolved} {}",
        binary.display(),
        meta.extra_args.join(" "),
    );
    let mut child = cmd
        .spawn()
        .with_context(|| format!("failed to spawn game binary '{}'", binary.display()))?;
    let pid = child.id();
    tracing::debug!("[{name}] pid={pid:?}");

    let exit_code = match tokio::time::timeout(timeout, child.wait()).await {
        Ok(Ok(status)) => status.code().unwrap_or(1),
        Ok(Err(e)) => {
            return Ok(ScenarioResult {
                passed: false,
                message: format!("wait error: {e}"),
                ..Default::default()
            });
        }
        Err(_) => {
            tracing::warn!("[{name}] timeout — killing pid {pid:?}");
            let _ = child.kill().await;
            return Ok(ScenarioResult {
                passed: false,
                message: format!("timeout after {:.1}s", timeout.as_secs_f64()),
                ..Default::default()
            });
        }
    };

    let elapsed = start.elapsed();
    if exit_code == 0 {
        Ok(ScenarioResult {
            passed: true,
            message: format!("exit 0, {:.1}s", elapsed.as_secs_f64()),
            ..Default::default()
        })
    } else {
        Ok(ScenarioResult {
            passed: false,
            message: format!("exit code {exit_code}, {:.1}s", elapsed.as_secs_f64()),
            ..Default::default()
        })
    }
}

/// Automatic I-20 post-check: eval `triGetGLErrorCount` and return the
/// FAIL string if any new HIGH-severity `KHR_debug` error fired during the
/// test.  Returns `None` on OK, FAIL skipped by missing connection, or
/// any other eval-side error (connection drop = game exited cleanly,
/// not the post-check's job to fail).
async fn run_i20_post_check(name: &str, game: &mut GameInstance) -> Option<String> {
    match game.client().eval("triGetGLErrorCount").await {
        Ok(result) => {
            let result_str = result.trim_matches('"').to_string();
            let count = result_str.trim().parse::<i64>().unwrap_or(0);
            if count == 0 {
                tracing::debug!("[{name}] I-20 post-check OK");
                None
            } else {
                let fail = format!("FAIL:expected == 0, got {count}");
                tracing::debug!("[{name}] I-20 post-check FAIL: {fail}");
                Some(fail)
            }
        }
        Err(e) => {
            tracing::debug!("[{name}] I-20 post-check skipped (game gone: {e})");
            None
        }
    }
}

/// Run a `*.seq/` directory: ordered `*.test.sqf` phases against one shared
/// `POSEIDON_USER_DIR`, each a fresh game launch, in sorted file-name order.
/// Passes only if every phase passes; stops at the first failure so a later
/// phase never runs against a half-set-up state.
#[allow(clippy::too_many_arguments)]
async fn run_seq_test(
    name: &str,
    seq_dir: &Path,
    game_dir: &str,
    data_dir: Option<&str>,
    port: u16,
    output_dir: &Path,
    render: Option<&str>,
    cli_game_args: &[String],
) -> Result<ScenarioResult> {
    let start = Instant::now();

    // Ordered phases: every *.test.sqf in the dir, sorted by file name.
    let mut phases: Vec<PathBuf> = std::fs::read_dir(seq_dir)
        .with_context(|| format!("failed to read seq dir {}", seq_dir.display()))?
        .filter_map(|e| e.ok().map(|e| e.path()))
        .filter(|p| is_test_sqf(p))
        .collect();
    phases.sort();

    if phases.is_empty() {
        return Ok(ScenarioResult {
            passed: false,
            message: "no .test.sqf phases in .seq dir".into(),
            ..Default::default()
        });
    }

    // One shared user dir for the whole sequence — held until every phase ran so
    // later boots see the profiles/prefs/configs earlier ones wrote.
    let shared = tempfile::tempdir().context("failed to create shared sequence dir")?;
    let shared_dir = shared.path().to_string_lossy().to_string();

    for (idx, phase) in phases.iter().enumerate() {
        let phase_name = phase
            .file_name()
            .unwrap_or_default()
            .to_string_lossy()
            .to_string();
        let label = format!("{name} [{}/{}] {phase_name}", idx + 1, phases.len());
        let result = run_sqf_test(
            &label,
            phase,
            game_dir,
            data_dir,
            port,
            output_dir,
            render,
            cli_game_args,
            Some(&shared_dir),
        )
        .await?;
        if !result.passed {
            return Ok(ScenarioResult {
                passed: false,
                message: format!("phase {phase_name}: {}", result.message),
                duration: start.elapsed(),
            });
        }
    }

    Ok(ScenarioResult {
        passed: true,
        message: format!(
            "{} phases, {:.1}s",
            phases.len(),
            start.elapsed().as_secs_f64()
        ),
        duration: start.elapsed(),
    })
}

/// Run a `.test.sqf` test — runner sends statements via harness.
#[allow(
    clippy::too_many_arguments,
    clippy::too_many_lines,
    clippy::cognitive_complexity
)]
async fn run_sqf_test(
    name: &str,
    sqf_file: &Path,
    game_dir: &str,
    data_dir: Option<&str>,
    port: u16,
    output_dir: &Path,
    render: Option<&str>,
    cli_game_args: &[String],
    user_dir_override: Option<&str>,
) -> Result<ScenarioResult> {
    let raw = std::fs::read_to_string(sqf_file)
        .with_context(|| format!("failed to read {}", sqf_file.display()))?;
    let base_dir = sqf_file
        .parent()
        .unwrap_or_else(|| std::path::Path::new("."));
    let sqf_code = expand_includes(&raw, base_dir)?;

    let meta = load_test_meta(sqf_file);

    // Use per-test timeout from TOML (if set) to derive client config,
    // so mission tests with long load times get enough connection retries.
    let effective_cfg = meta.timeout.map_or_else(
        || crate::config::get().clone(),
        crate::config::TridentConfig::new,
    );
    let config = effective_cfg.client_config();
    let timeout = effective_cfg.timeout;
    let assert_timeout = meta
        .assert_timeout
        .map_or(effective_cfg.assert_timeout, std::time::Duration::from_secs);
    let start = Instant::now();

    // Config dir for test isolation.  Default: ephemeral tempdir
    // auto-cleaned on drop.  An explicit override (sequence phases share one
    // dir so later boots see earlier state) wins; otherwise, if the parent
    // process already set POSEIDON_USER_DIR (e.g. a Pester wrapper that wants
    // to inspect audio.cfg / keybinds.cfg / etc. after tri exits), we honour
    // that — letting the caller own the lifecycle.
    let inherited_user_dir = user_dir_override
        .map(std::string::ToString::to_string)
        .or_else(|| {
            std::env::var("POSEIDON_USER_DIR")
                .ok()
                .filter(|s| !s.is_empty())
        });
    // _tmp_dir holds ownership of the temp directory when we created
    // one, so it survives until end of scope and isn't deleted while
    // the game is still running.  None when the parent provided a dir.
    let (_tmp_dir, user_dir) = if let Some(d) = inherited_user_dir {
        (None, d)
    } else {
        let tmp = tempfile::tempdir().context("failed to create ephemeral config directory")?;
        let p = tmp.path().to_string_lossy().to_string();
        (Some(tmp), p)
    };
    let output_str = output_dir.to_string_lossy().to_string();
    let env_vars = [
        ("POSEIDON_USER_DIR", user_dir.as_str()),
        ("POSEIDON_CACHE_DIR", user_dir.as_str()),
        ("POSEIDON_TEMP_DIR", user_dir.as_str()),
        ("TRI_OUTPUT_DIR", output_str.as_str()),
        ("SDL_VIDEO_MINIMIZE_ON_FOCUS_LOSS", "0"),
    ];

    // Build extra args from sidecar TOML metadata.
    // --autotest is injected for every non-server test so that SQF/SQS script
    // errors are treated as hard failures (exit code 2 + graceful shutdown)
    // rather than being silently shown on-screen and ignored.  Individual tests
    // that cannot run under --autotest can opt out via `no_autotest = true` in
    // their .toml sidecar.
    let mut extra_args: Vec<String> = if meta.is_server() {
        // The dedicated server is its own binary (PoseidonServer, selected below);
        // it is inherently a server, so no --server flag is needed.
        vec!["--nosound".into()]
    } else {
        let mut v = vec!["--window".into(), "--no-splash".into()];
        if !meta.no_autotest {
            v.push("--autotest".into());
        }
        v
    };
    // Force a deterministic startup language (English by default) so localized
    // text doesn't depend on the host OS locale; a scenario can override.
    extra_args.push("--lang".into());
    extra_args.push(meta.language().to_string());
    if let Some(ref mission) = meta.mission {
        let mission_path = Path::new(mission);
        let resolved = if mission_path.is_relative() {
            std::env::current_dir()
                .unwrap_or_default()
                .join(mission_path)
                .to_string_lossy()
                .to_string()
        } else {
            mission.clone()
        };
        if meta.is_server() {
            extra_args.push("--simulate".into());
        } else {
            extra_args.push("--test-mission".into());
        }
        extra_args.push(resolved);
    }
    if let Some(ref tt) = meta.test_type {
        extra_args.push("--test-type".into());
        extra_args.push(tt.clone());
    }
    // Inject --render if specified globally
    if let Some(r) = render {
        extra_args.push("--render".into());
        extra_args.push(r.into());
    }
    // Automated tests run strict (the rwdi build players run defaults to --no-strict).
    // Pushed before the sidecar toml's extra_args so a per-test `--no-strict` wins.
    extra_args.push("--strict".into());
    extra_args.extend(meta.extra_args.iter().cloned());
    extra_args.extend(cli_game_args.iter().cloned());
    tracing::debug!("[{name}] game extra_args: {:?}", extra_args);
    let extra_refs: Vec<&str> = extra_args.iter().map(String::as_str).collect();

    // Per-test `data_dir` override (e.g. viewer/*.test.toml point at
    // the test-local package) wins over the global --data-dir CLI value.
    let effective_data_dir = meta.data_dir.as_deref().or(data_dir);

    // 1. Boot game with ephemeral config. Server tests run the dedicated-server
    // binary (PoseidonServer, in a sibling app dir) instead of the client.
    let binary_name = if meta.is_server() {
        Some("PoseidonServer")
    } else {
        meta.binary.as_deref()
    };
    let mut game = match GameInstance::spawn_with_env(
        game_dir,
        effective_data_dir,
        port,
        &extra_refs,
        &env_vars,
        binary_name,
        &config,
    )
    .await
    {
        Ok(g) => g,
        Err(e) => {
            return Ok(ScenarioResult {
                passed: false,
                message: format!("spawn failed: {e}"),
                ..Default::default()
            });
        }
    };

    // 2. Wait for ready event, then ensure we're on main menu.
    // The dedicated server (PoseidonServer) has no display, so it emits no `ready`
    // event and has no main menu — a successful harness connection above already
    // means it finished init (its harness only starts after RunServerStages). Skip
    // the display-readiness handshake for server tests.
    if !meta.is_server() {
        match game.wait_ready(timeout).await {
            Ok(event) => {
                let idd = event
                    .data
                    .get("idd")
                    .and_then(serde_json::Value::as_i64)
                    .unwrap_or(-1);
                if !meta.no_menu && idd != i64::from(MAIN_MENU_IDD) {
                    tracing::debug!(
                        "[{name}] ready event IDD={idd}, waiting for main menu IDD={MAIN_MENU_IDD}"
                    );
                    let settle = std::time::Duration::from_secs(10);
                    match game.client().wait_for_display(MAIN_MENU_IDD, settle).await {
                        Ok(_) => {}
                        Err(_) => {
                            return Ok(ScenarioResult {
                                passed: false,
                                message: format!(
                                    "expected main menu IDD={MAIN_MENU_IDD}, got {idd} and didn't settle"
                                ),
                                ..Default::default()
                            });
                        }
                    }
                }
            }
            Err(e) => {
                return Ok(ScenarioResult {
                    passed: false,
                    message: format!("timeout waiting for ready: {e}"),
                    ..Default::default()
                });
            }
        }
    }

    if meta.mission.is_some() && !meta.is_server() && !meta.no_player {
        let outcome = wait_for_condition(
            name,
            &mut game,
            "triMissionPlayerReady",
            assert_timeout,
            effective_cfg.poll_interval,
            WaitMode::WaitFrames(5),
        )
        .await;
        if !outcome.succeeded {
            game.kill_after_failure().await;
            let elapsed = start.elapsed();
            return Ok(ScenarioResult {
                passed: false,
                message: format!(
                    "--test-mission player readiness — {} ({:.1}s)",
                    outcome.last_result,
                    elapsed.as_secs_f64()
                ),
                duration: elapsed,
            });
        }
    }

    // 3. Send each SQF statement individually via eval so display changes
    //    settle between frames. The game processes one harness command per frame,
    //    so each eval naturally gets its own frame.
    //    Assertion commands (triAssert*) return "OK" or "FAIL:..." — the runner
    //    retries FAIL results to let UI transitions settle.
    //    triWait <ms> is handled runner-side (sleep, not sent to game).
    //    If eval errors (connection dropped), the game exited — break to check exit code.
    //
    //    Automatic I-20 post-check: before any statement that ends the
    //    game's lifetime (`triEndTest`, `triClick 106` = exit-button),
    //    and once at end-of-loop, eval `triGetGLErrorCount`.
    //    A FAIL result fails the test with "I-20 post-check" detail — no
    //    per-test opt-in required for KHR_debug HIGH-severity gating.
    let statements = parse_statements(&sqf_code);
    let mut post_check_ran = false;
    let mut screenshot_seq = 0usize;
    for (i, stmt) in statements.iter().enumerate() {
        // Pre-shutdown injection: before sending a known game-exit
        // verb, run the I-20 post-check while the connection is live.
        let is_shutdown = stmt == "triEndTest" || stmt == "triClick 106";
        if is_shutdown && !post_check_ran {
            post_check_ran = true;
            if let Some(fail) = run_i20_post_check(name, &mut game).await {
                game.kill_after_failure().await;
                let elapsed = start.elapsed();
                return Ok(ScenarioResult {
                    passed: false,
                    message: format!("I-20 post-check — {fail} ({:.1}s)", elapsed.as_secs_f64()),
                    duration: elapsed,
                });
            }
        }

        tracing::debug!("[{}] step {}/{}: {}", name, i + 1, statements.len(), stmt);

        // triWait <ms> — runner-side sleep, not sent to game
        if let Some(ms_str) = stmt.strip_prefix("triWait ") {
            if let Ok(ms) = ms_str.trim().parse::<u64>() {
                tracing::debug!("[{}]   → sleep {}ms", name, ms);
                tokio::time::sleep(std::time::Duration::from_millis(ms)).await;
            }
            continue;
        }

        if let Some(condition) = parse_condition_statement(stmt, "triWaitUntil") {
            let outcome = wait_for_condition(
                name,
                &mut game,
                condition.as_str(),
                assert_timeout,
                effective_cfg.poll_interval,
                WaitMode::WaitFrames(10),
            )
            .await;
            if let Some(message) = finish_condition_failure(&mut game, start, stmt, outcome).await {
                return Ok(message);
            }
            continue;
        }

        if let Some(condition) = parse_condition_statement(stmt, "triSimUntil") {
            let outcome = wait_for_condition(
                name,
                &mut game,
                condition.as_str(),
                assert_timeout,
                effective_cfg.poll_interval,
                WaitMode::SimFrames(10),
            )
            .await;
            if let Some(message) = finish_condition_failure(&mut game, start, stmt, outcome).await {
                return Ok(message);
            }
            continue;
        }

        let is_assert = stmt.starts_with("triAssert");
        let screenshot_label = parse_triscreenshot_label(stmt);
        // Use the effective per-test config (honours TOML `timeout = N`)
        // rather than the global one — otherwise long tests with their
        // own larger timeout still polled for only the global default.
        let assert_deadline = Instant::now() + assert_timeout;
        let poll_interval = effective_cfg.poll_interval;
        let mut last_result = String::new();
        let mut succeeded = false;

        // Capybara-style synchronisation: any command that returns
        // "FAIL:..." gets retried until the assertion timeout.  This
        // includes triAssert* (existing), AND find-then-act commands
        // (triClickText, etc.) that auto-wait for their target to
        // appear.  Plain commands returning anything else (numbers,
        // bools, "OK") complete on the first response.
        loop {
            match game.client().eval(stmt).await {
                Ok(result) => {
                    let result_str = result.clone().trim_matches('"').to_string();
                    let is_retryable_fail = result_str.starts_with("FAIL:");
                    if is_assert || is_retryable_fail {
                        if result_str == "OK" {
                            tracing::debug!("[{name}]   → OK");
                            succeeded = true;
                            break;
                        }
                        last_result = result_str;
                        if Instant::now() >= assert_deadline {
                            break;
                        }
                        tokio::time::sleep(poll_interval).await;
                    } else {
                        tracing::debug!("[{name}]   → {result}");
                        succeeded = true;
                        break;
                    }
                }
                Err(e) => {
                    tracing::debug!("[{name}]   → connection error: {e}");
                    last_result = format!("connection error: {e}");
                    break;
                }
            }
        }

        // Failure if either:
        //   - triAssert* never returned "OK" within the deadline
        //   - any other command returned "FAIL:..." for the entire
        //     deadline (find-then-act auto-wait expired)
        if !succeeded && (is_assert || last_result.starts_with("FAIL:")) {
            tracing::debug!(
                "[{name}]   → FAIL after {:.1}s: {last_result}",
                assert_timeout.as_secs_f64()
            );
            // Kill the game and report failure.  `kill_after_failure`
            // sends triEndTest with a short deadline then force-kills.
            // Failed assertions do not need the full per-test timeout
            // again during cleanup.
            game.kill_after_failure().await;
            let elapsed = start.elapsed();
            return Ok(ScenarioResult {
                passed: false,
                message: format!("{} — {} ({:.1}s)", stmt, last_result, elapsed.as_secs_f64()),
                duration: elapsed,
            });
        }

        if succeeded {
            if let Some(label) = screenshot_label.as_deref() {
                let outcome = wait_for_screenshot_file(
                    name,
                    &mut game,
                    output_dir,
                    screenshot_seq,
                    label,
                    assert_timeout,
                    effective_cfg.poll_interval,
                )
                .await;
                if let Some(message) =
                    finish_condition_failure(&mut game, start, stmt, outcome).await
                {
                    return Ok(message);
                }
                screenshot_seq += 1;
            }
        }

        if !succeeded {
            // Connection dropped — game exited (triEndTest, quit, or crash)
            break;
        }
    }

    // 3b. End-of-loop I-20 post-check — catches tests that didn't hit
    // a known shutdown verb (server tests, viewer tests with no
    // explicit shutdown).  Silently skipped if the connection already
    // dropped or the pre-shutdown branch already ran.
    if !post_check_ran {
        if let Some(fail) = run_i20_post_check(name, &mut game).await {
            game.kill_after_failure().await;
            let elapsed = start.elapsed();
            return Ok(ScenarioResult {
                passed: false,
                message: format!("I-20 post-check — {fail} ({:.1}s)", elapsed.as_secs_f64()),
                duration: elapsed,
            });
        }
    }

    // 4. Wait for game to exit (SQF drives the flow including exit).
    // A timeout here means the test SQF didn't drive the game to a clean
    // shutdown within the budget — report it as a per-test failure rather
    // than propagating up via `?`, which would abort the whole suite (one
    // hung test would mask all subsequent test results).  wait_exit kills
    // the game on timeout, so the next test still gets a clean spawn.
    let exit_code = match game.wait_exit(timeout).await {
        Ok(code) => code,
        Err(e) => {
            let elapsed = start.elapsed();
            return Ok(ScenarioResult {
                passed: false,
                message: format!(
                    "game didn't exit cleanly: {e} ({:.1}s)",
                    elapsed.as_secs_f64()
                ),
                duration: elapsed,
            });
        }
    };
    let elapsed = start.elapsed();

    // tmp_dir is dropped here, cleaning up the ephemeral config

    if exit_code == 0 {
        Ok(ScenarioResult {
            passed: true,
            message: format!("exit 0, {:.1}s", elapsed.as_secs_f64()),
            duration: elapsed,
        })
    } else {
        Ok(ScenarioResult {
            passed: false,
            message: format!("exit code {exit_code}, {:.1}s", elapsed.as_secs_f64()),
            duration: elapsed,
        })
    }
}

/// ANSI color helpers
mod color {
    pub const RED: &str = "\x1b[31m";
    pub const GREEN: &str = "\x1b[32m";
    pub const YELLOW: &str = "\x1b[33m";
    pub const DIM: &str = "\x1b[2m";
    pub const BOLD: &str = "\x1b[1m";
    pub const RESET: &str = "\x1b[0m";
}

/// Run tests with formatted output.
/// `jobs` controls parallelism: 1 = sequential, 0 = auto (CPU count), N = up to N parallel.
#[allow(
    clippy::too_many_arguments,
    clippy::too_many_lines,
    clippy::significant_drop_tightening
)]
pub async fn run_all(
    tests: &[IntegrationTest],
    game_dir: &str,
    data_dir: Option<&str>,
    jobs: usize,
    retries: usize,
    output_dir: &Path,
    render: Option<&str>,
    cli_game_args: &[String],
) -> Result<Vec<(String, ScenarioResult)>> {
    // Remove obsolete package-level network caches before any scenario starts.
    // Per-test package overrides are included and aliases are deduplicated, so
    // cleanup never races another scenario's network transfer.
    clear_legacy_network_asset_dirs(tests, data_dir)?;

    let total = tests.len();
    let concurrency = match jobs {
        0 => std::thread::available_parallelism()
            .map(usize::from)
            .unwrap_or(4)
            .min(total),
        n => n.min(total),
    };

    if concurrency <= 1 {
        return run_all_sequential(
            tests,
            game_dir,
            data_dir,
            output_dir,
            retries,
            render,
            cli_game_args,
        )
        .await;
    }

    let suite_start = Instant::now();
    let semaphore = std::sync::Arc::new(tokio::sync::Semaphore::new(concurrency));
    let out_dir = output_dir.to_path_buf();

    // --- Initial run ---
    let (mp, pb, running_pb) = make_progress_display(total, concurrency);

    let running_names: std::sync::Arc<std::sync::Mutex<Vec<String>>> =
        std::sync::Arc::new(std::sync::Mutex::new(Vec::new()));

    let mut handles = tokio::task::JoinSet::new();
    let max_slots = u32::try_from(concurrency).unwrap_or(u32::MAX);
    let initial_schedule = scheduled_tests(tests, max_slots);
    for test in initial_schedule {
        let name = test.name.clone();
        let kind = test.kind.clone();
        let tags = test.tags.clone();
        let slots = test.parallel_slots(max_slots);
        let game_dir = game_dir.to_string();
        let data_dir = data_dir.map(String::from);
        let render = render.map(String::from);
        let game_args = cli_game_args.to_vec();
        let sem = semaphore.clone();
        let out = out_dir.clone();
        let rn = running_names.clone();
        let rpb = running_pb.clone();

        handles.spawn(async move {
            let _permit = sem.acquire_many(slots).await.expect("semaphore closed");

            // Track this test as running after it has acquired runner slots.
            {
                let mut names = rn.lock().unwrap();
                names.push(name.clone());
                rpb.set_message(format_running(&names));
            }
            let test = IntegrationTest {
                name: name.clone(),
                kind,
                tags,
            };
            let result = run_test(
                &test,
                &game_dir,
                data_dir.as_deref(),
                0,
                &out,
                render.as_deref(),
                &game_args,
            )
            .await;
            // Remove from running
            {
                let mut names = rn.lock().unwrap();
                names.retain(|n| n != &name);
                rpb.set_message(format_running(&names));
            }
            (name, result)
        });
    }

    let mut results = Vec::with_capacity(total);
    while let Some(handle) = handles.join_next().await {
        let (name, result) = handle?;
        let result = result?;
        pb.inc(1);
        bar_tick_eta(&pb);
        results.push((name, result));
    }
    mp.clear().ok();

    // Semi-summary after initial run
    let initial_passed = results.iter().filter(|(_, r)| r.passed).count();
    let initial_failed = total - initial_passed;
    if initial_failed > 0 && retries > 0 {
        println!(
            "\n  {}{} passed, {} failed{} -- retrying {} failed tests (up to {} retries)",
            color::YELLOW,
            initial_passed,
            initial_failed,
            color::RESET,
            initial_failed,
            retries,
        );
        print_retry_test_list(&results);
        println!();
    }

    // --- Retry failed tests (failures printed only after final attempt) ---
    for attempt in 1..=retries {
        let failed: Vec<_> = results
            .iter()
            .enumerate()
            .filter(|(_, (_, r))| !r.passed)
            .map(|(i, (name, _))| (i, name.clone()))
            .collect();
        if failed.is_empty() {
            break;
        }

        // Reuse same progress display, just reset for retry batch
        pb.set_length(failed.len() as u64);
        pb.reset();
        update_bar_label(&pb, failed.len(), concurrency, Some((attempt, retries)));
        running_pb.reset();
        let is_last_attempt = attempt == retries;

        let retry_running: std::sync::Arc<std::sync::Mutex<Vec<String>>> =
            std::sync::Arc::new(std::sync::Mutex::new(Vec::new()));

        let test_map: std::collections::HashMap<_, _> =
            tests.iter().map(|t| (t.name.clone(), t)).collect();

        let mut retry_handles = tokio::task::JoinSet::new();
        let mut retry_schedule = failed;
        retry_schedule
            .sort_by_key(|(_, name)| test_map[name.as_str()].reserves_full_pool(max_slots));
        for (result_idx, name) in &retry_schedule {
            let test = test_map[name];
            let tname = name.clone();
            let tkind = test.kind.clone();
            let ttags = test.tags.clone();
            let slots = test.parallel_slots(max_slots);
            let gdir = game_dir.to_string();
            let ddir = data_dir.map(String::from);
            let render = render.map(String::from);
            let game_args = cli_game_args.to_vec();
            let sem = semaphore.clone();
            let out = out_dir.clone();
            let ridx = *result_idx;
            let rn = retry_running.clone();
            let rpb = running_pb.clone();

            retry_handles.spawn(async move {
                let _permit = sem.acquire_many(slots).await.expect("semaphore closed");

                {
                    let mut names = rn.lock().unwrap();
                    names.push(tname.clone());
                    rpb.set_message(format_running(&names));
                }
                let t = IntegrationTest {
                    name: tname.clone(),
                    kind: tkind,
                    tags: ttags,
                };
                let result = run_test(
                    &t,
                    &gdir,
                    ddir.as_deref(),
                    0,
                    &retry_output_dir(&out, attempt),
                    render.as_deref(),
                    &game_args,
                )
                .await;
                {
                    let mut names = rn.lock().unwrap();
                    names.retain(|n| n != &tname);
                    rpb.set_message(format_running(&names));
                }
                (ridx, tname, result)
            });
        }

        while let Some(handle) = retry_handles.join_next().await {
            let (idx, name, result) = handle?;
            let result = result?;
            pb.suspend(|| {
                print_retry_outcome(&pb, attempt, retries, &name, &result, is_last_attempt);
            });
            pb.inc(1);
            bar_tick_eta(&pb);
            results[idx] = (name, result);
        }
    }
    mp.clear().ok();

    // Print all final failures after retries
    let final_failures: Vec<_> = results
        .iter()
        .filter(|(_, r)| !r.passed)
        .map(|(name, r)| (name.clone(), r.message.clone()))
        .collect();
    print_failures(&final_failures);

    print_summary(&results, &suite_start);
    Ok(results)
}

/// Sequential runner -- original behavior for -j1 or single tests.
async fn run_all_sequential(
    tests: &[IntegrationTest],
    game_dir: &str,
    data_dir: Option<&str>,
    output_dir: &Path,
    retries: usize,
    render: Option<&str>,
    cli_game_args: &[String],
) -> Result<Vec<(String, ScenarioResult)>> {
    let total = tests.len();
    let suite_start = Instant::now();
    let mut results = Vec::new();

    let (mp, pb, running_pb) = make_progress_display(total, 1);

    for test in tests {
        running_pb.set_message(test.name.clone());
        let result = run_test(
            test,
            game_dir,
            data_dir,
            0,
            output_dir,
            render,
            cli_game_args,
        )
        .await?;
        if !result.passed {
            pb.suspend(|| {
                println!(
                    "  {}FAIL{} {} -- {}",
                    color::RED,
                    color::RESET,
                    test.name,
                    result.message
                );
            });
        }
        pb.inc(1);
        bar_tick_eta(&pb);
        results.push((test.name.clone(), result));
    }
    mp.clear().ok();

    // Semi-summary after initial run
    let initial_passed = results.iter().filter(|(_, r)| r.passed).count();
    let initial_failed = total - initial_passed;
    if initial_failed > 0 && retries > 0 {
        println!(
            "\n  {}{} passed, {} failed{} -- retrying {} failed tests (up to {} retries)",
            color::YELLOW,
            initial_passed,
            initial_failed,
            color::RESET,
            initial_failed,
            retries,
        );
        print_retry_test_list(&results);
        println!();
    }

    // Retry failed tests (failures printed only after final attempt)
    for attempt in 1..=retries {
        let failed: Vec<_> = results
            .iter()
            .enumerate()
            .filter(|(_, (_, r))| !r.passed)
            .map(|(i, (_, _))| i)
            .collect();
        if failed.is_empty() {
            break;
        }

        pb.set_length(failed.len() as u64);
        pb.reset();
        update_bar_label(&pb, failed.len(), 1, Some((attempt, retries)));
        running_pb.reset();
        let is_last_attempt = attempt == retries;

        for idx in failed {
            let test = &tests[idx];
            running_pb.set_message(test.name.clone());
            let result = run_test(
                test,
                game_dir,
                data_dir,
                0,
                &retry_output_dir(output_dir, attempt),
                render,
                cli_game_args,
            )
            .await?;
            pb.suspend(|| {
                print_retry_outcome(&pb, attempt, retries, &test.name, &result, is_last_attempt);
            });
            pb.inc(1);
            bar_tick_eta(&pb);
            results[idx] = (test.name.clone(), result);
        }
    }
    mp.clear().ok();

    let final_failures: Vec<_> = results
        .iter()
        .filter(|(_, r)| !r.passed)
        .map(|(name, r)| (name.clone(), r.message.clone()))
        .collect();
    print_failures(&final_failures);

    print_summary(&results, &suite_start);
    Ok(results)
}

/// Print the names of failing tests heading into the retry batch.
///
/// Without this, the names only appear transiently in the running-tests
/// progress message and are gone once the run completes — which makes it
/// hard to tell which tests are flaky vs. genuinely broken.
fn print_retry_test_list(results: &[(String, ScenarioResult)]) {
    let failed: Vec<&str> = results
        .iter()
        .filter(|(_, r)| !r.passed)
        .map(|(name, _)| name.as_str())
        .collect();
    if failed.is_empty() {
        return;
    }
    for name in &failed {
        println!("    - {name}");
    }
}

/// Print whether a single retry attempt recovered the test or not.
///
/// On non-final attempts a retry that still fails is logged as PEND
/// (we'll try again); only the final attempt's failure is reported as
/// FAIL — `print_failures()` then prints the captured message later.
fn print_retry_outcome(
    pb: &indicatif::ProgressBar,
    attempt: usize,
    max_attempts: usize,
    name: &str,
    result: &ScenarioResult,
    is_last_attempt: bool,
) {
    let _ = pb; // suspend already provided by caller; kept for future tweaks
    let label = format!("retry {attempt}/{max_attempts}");
    if result.passed {
        println!(
            "    {}OK  {}{} {} -- recovered",
            color::GREEN,
            color::RESET,
            label,
            name,
        );
    } else if is_last_attempt {
        println!(
            "    {}FAIL{} {} {} -- {}",
            color::RED,
            color::RESET,
            label,
            name,
            result.message,
        );
    } else {
        println!(
            "    {}PEND{} {} {} -- {} (will retry)",
            color::YELLOW,
            color::RESET,
            label,
            name,
            result.message,
        );
    }
}

/// Human-readable duration: `45s`, `3m20s`, `2h05m`, `1d3h`.
fn format_secs(s: u64) -> String {
    if s < 60 {
        format!("{s}s")
    } else if s < 3600 {
        format!("{}m{:02}s", s / 60, s % 60)
    } else if s < 86400 {
        format!("{}h{:02}m", s / 3600, (s % 3600) / 60)
    } else {
        format!("{}d{}h", s / 86400, (s % 86400) / 3600)
    }
}

/// Average-rate ETA. indicatif's built-in `{eta}` uses a short recent-sample
/// window that explodes to absurd values when progress stalls (a run stuck on a
/// few slow MP tests showed "~584942417355y left"); a plain elapsed/done average
/// stays sane and refines as tests complete.
#[allow(
    clippy::cast_precision_loss,
    clippy::cast_possible_truncation,
    clippy::cast_sign_loss
)]
fn format_eta(elapsed: std::time::Duration, done: u64, total: u64) -> String {
    if done == 0 || done >= total {
        return "--".to_string();
    }
    let secs = elapsed.as_secs_f64().max(0.001);
    let remaining = (total - done) as f64 * secs / done as f64;
    format_secs(remaining as u64)
}

/// Refresh the main bar's ETA message from the average completion rate so far.
fn bar_tick_eta(pb: &indicatif::ProgressBar) {
    let total = pb.length().unwrap_or(0);
    pb.set_message(format_eta(pb.elapsed(), pb.position(), total));
}

fn format_running(names: &[String]) -> String {
    match names.len() {
        0 => String::new(),
        1 => names[0].clone(),
        2 => format!("{}, {}", names[0], names[1]),
        n => format!("{}, {} +{}", names[0], names[1], n - 2),
    }
}

/// Updates the progress bar style label (for reuse between initial run and retries)
fn update_bar_label(
    pb: &indicatif::ProgressBar,
    total: usize,
    concurrency: usize,
    retry: Option<(usize, usize)>,
) {
    use indicatif::ProgressStyle;

    let jobs_label = if concurrency > 1 {
        format!(", {concurrency} slots")
    } else {
        String::new()
    };

    let retry_label = match retry {
        Some((attempt, max)) => format!("  retry {attempt}/{max}"),
        None => String::new(),
    };

    let _ = total; // used indirectly via {len}
    #[allow(clippy::literal_string_with_formatting_args)]
    let bar_template = format!(
        "  {{bar:30.cyan/dim}}  {{pos}}/{{len}} done{jobs_label}{retry_label}  [{{elapsed}} elapsed, ~{{msg}} left]"
    );
    pb.set_style(
        ProgressStyle::with_template(&bar_template)
            .expect("valid template")
            .progress_chars("=> "),
    );
}

/// Creates a two-line progress display:
///   Line 1: progress bar with done/total, elapsed/eta
///   Line 2: spinner with currently running test name(s)
fn make_progress_display(
    total: usize,
    concurrency: usize,
) -> (
    indicatif::MultiProgress,
    indicatif::ProgressBar,
    indicatif::ProgressBar,
) {
    use indicatif::{MultiProgress, ProgressBar, ProgressStyle};

    let mp = MultiProgress::new();

    // Line 1: main progress bar
    let pb = mp.add(ProgressBar::new(total as u64));
    update_bar_label(&pb, total, concurrency, None);
    pb.set_message("--"); // ETA placeholder until the first test completes
    pb.enable_steady_tick(std::time::Duration::from_millis(200));

    // Line 2: spinner showing running test(s)
    let running_pb = mp.add(ProgressBar::new_spinner());
    #[allow(clippy::literal_string_with_formatting_args)]
    let running_tpl = "  {spinner:.dim} {msg:.dim}";
    running_pb.set_style(
        ProgressStyle::with_template(running_tpl)
            .expect("valid template")
            .tick_strings(&["-", "\\", "|", "/", ""]),
    );
    running_pb.enable_steady_tick(std::time::Duration::from_millis(150));

    (mp, pb, running_pb)
}

fn print_failures(failures: &[(String, String)]) {
    for (name, message) in failures {
        println!(
            "  {}FAIL{} {} -- {}",
            color::RED,
            color::RESET,
            name,
            message
        );
    }
}

/// Print the N slowest tests with their wall-clock times.  Sorted
/// descending by `ScenarioResult::duration`, which is stamped at
/// the end of each `run_test` (single attempt — does NOT include
/// retries on previous attempts).  Tests whose duration was never
/// stamped (e.g. early-spawn-failure paths) sort to the bottom.
pub fn print_profile(results: &[(String, ScenarioResult)], n: usize) {
    if results.is_empty() || n == 0 {
        return;
    }
    let mut sorted: Vec<&(String, ScenarioResult)> = results.iter().collect();
    sorted.sort_by_key(|result| Reverse(result.1.duration));
    let take = n.min(sorted.len());
    println!("\n  Top {take} slowest tests:");
    let max_name_len = sorted
        .iter()
        .take(take)
        .map(|(n, _)| n.len())
        .max()
        .unwrap_or(0);
    for (name, r) in sorted.iter().take(take) {
        let secs = r.duration.as_secs_f64();
        let mark = if r.passed { "  " } else { color::RED };
        let reset = if r.passed { "" } else { color::RESET };
        println!("    {mark}{secs:>6.2}s{reset}  {name:<max_name_len$}");
    }
}

fn print_summary(results: &[(String, ScenarioResult)], start: &Instant) {
    let passed = results.iter().filter(|(_, r)| r.passed).count();
    let failed = results.iter().filter(|(_, r)| !r.passed).count();
    let elapsed = start.elapsed();
    let total = results.len();

    if failed == 0 {
        println!(
            "\n  {}{}passed{} {passed}/{total}  ({:.1}s)",
            color::BOLD,
            color::GREEN,
            color::RESET,
            elapsed.as_secs_f64()
        );
    } else {
        println!(
            "\n  {}{}{passed} passed, {failed} failed{} / {total}  ({:.1}s)",
            color::BOLD,
            color::RED,
            color::RESET,
            elapsed.as_secs_f64()
        );
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;
    use tempfile::TempDir;

    #[test]
    fn eta_average_rate_is_sane() {
        use std::time::Duration;
        // 6/245 done in 300s → ~3h by average rate, NOT the centuries indicatif's
        // built-in {eta} produced when progress stalled ("~584942417355y left").
        let eta = format_eta(Duration::from_secs(300), 6, 245);
        assert!(eta.contains('h'), "expected hours-scale ETA, got {eta}");
        assert!(!eta.contains('y'), "ETA must never be years: {eta}");
        // No data yet, or finished → placeholder, never a bogus number.
        assert_eq!(format_eta(Duration::from_secs(10), 0, 245), "--");
        assert_eq!(format_eta(Duration::from_secs(10), 245, 245), "--");
        // Half done in 60s → ~60s left.
        assert_eq!(format_eta(Duration::from_secs(60), 5, 10), "1m00s");
    }

    #[test]
    fn format_secs_scales() {
        assert_eq!(format_secs(45), "45s");
        assert_eq!(format_secs(200), "3m20s");
        assert_eq!(format_secs(7500), "2h05m");
        assert_eq!(format_secs(90000), "1d1h");
    }

    #[test]
    fn scheduler_moves_exclusive_tests_after_parallel_tests() {
        let tests = vec![
            IntegrationTest {
                name: "mp/jip".to_string(),
                kind: TestKind::Multi(PathBuf::from("mp/jip.test")),
                tags: Vec::new(),
            },
            IntegrationTest {
                name: "ui/menu".to_string(),
                kind: TestKind::Sqf(PathBuf::from("ui/menu.test.sqf")),
                tags: Vec::new(),
            },
            IntegrationTest {
                name: "ui/display".to_string(),
                kind: TestKind::Sqf(PathBuf::from("ui/display.test.sqf")),
                tags: vec!["exclusive".to_string()],
            },
            IntegrationTest {
                name: "scripting/basic".to_string(),
                kind: TestKind::Sqf(PathBuf::from("scripting/basic.test.sqf")),
                tags: Vec::new(),
            },
        ];

        let names: Vec<_> = scheduled_tests(&tests, 5)
            .into_iter()
            .map(|test| test.name.as_str())
            .collect();

        assert_eq!(
            names,
            ["mp/jip", "ui/menu", "scripting/basic", "ui/display"]
        );
    }

    #[test]
    fn output_paths_isolate_tests_and_retry_attempts() {
        let output = Path::new("artifacts");

        assert_eq!(
            test_output_dir(output, "multiplayer/first join"),
            output.join("multiplayer_first_join")
        );
        assert_eq!(retry_output_dir(output, 1), output.join("retry-1"));
        assert_eq!(
            test_output_dir(&retry_output_dir(output, 2), "ui/menu"),
            output.join("retry-2").join("ui_menu")
        );
    }

    #[test]
    fn effective_data_dirs_include_default_and_each_test_override() {
        let dir = TempDir::new().unwrap();
        let default_data = dir.path().join("default-data");
        let sqf_data = dir.path().join("sqf-data");
        let multi_data = dir.path().join("multi-data");
        let seq_data = dir.path().join("seq-data");
        for data_dir in [&default_data, &sqf_data, &multi_data, &seq_data] {
            fs::create_dir_all(data_dir).unwrap();
        }

        sqf(dir.path(), "default.test.sqf");
        sqf(dir.path(), "override.test.sqf");
        toml(
            dir.path(),
            "override.test.toml",
            &format!(
                "data_dir = {}\n",
                toml::Value::String(sqf_data.to_string_lossy().into())
            ),
        );

        let multi = dir.path().join("network.test");
        fs::create_dir_all(&multi).unwrap();
        fs::write(
            multi.join("test.toml"),
            format!(
                "data_dir = {}\n[[instances]]\nname = \"server\"\ntype = \"server\"\n",
                toml::Value::String(multi_data.to_string_lossy().into())
            ),
        )
        .unwrap();

        let seq = dir.path().join("restart.seq");
        sqf(&seq, "01_boot.test.sqf");
        toml(
            &seq,
            "01_boot.test.toml",
            &format!(
                "data_dir = {}\n",
                toml::Value::String(seq_data.to_string_lossy().into())
            ),
        );

        let tests = vec![
            IntegrationTest {
                name: "default".into(),
                kind: TestKind::Sqf(dir.path().join("default.test.sqf")),
                tags: Vec::new(),
            },
            IntegrationTest {
                name: "override".into(),
                kind: TestKind::Sqf(dir.path().join("override.test.sqf")),
                tags: Vec::new(),
            },
            IntegrationTest {
                name: "network".into(),
                kind: TestKind::Multi(multi),
                tags: Vec::new(),
            },
            IntegrationTest {
                name: "restart".into(),
                kind: TestKind::Seq(seq),
                tags: Vec::new(),
            },
        ];

        let actual = effective_data_dirs(&tests, default_data.to_str()).unwrap();
        let expected: Vec<_> = BTreeSet::from([
            fs::canonicalize(default_data).unwrap(),
            fs::canonicalize(sqf_data).unwrap(),
            fs::canonicalize(multi_data).unwrap(),
            fs::canonicalize(seq_data).unwrap(),
        ])
        .into_iter()
        .collect();

        assert_eq!(actual, expected);
    }

    fn sqf(base: &Path, rel: &str) {
        let full = base.join(rel);
        fs::create_dir_all(full.parent().unwrap()).unwrap();
        fs::write(&full, "// test").unwrap();
    }

    fn toml(base: &Path, rel: &str, content: &str) {
        let full = base.join(rel);
        fs::create_dir_all(full.parent().unwrap()).unwrap();
        fs::write(&full, content).unwrap();
    }

    fn names(tests: &[IntegrationTest]) -> Vec<String> {
        let mut v: Vec<_> = tests.iter().map(|t| t.name.clone()).collect();
        v.sort();
        v
    }

    // ── multiple directories ────────────────────────────────────────────────

    #[test]
    fn multiple_directories() {
        let dir = TempDir::new().unwrap();
        sqf(dir.path(), "screenshots/render.test.sqf");
        sqf(dir.path(), "main_menu/exit.test.sqf");
        sqf(dir.path(), "main_menu/options.test.sqf");

        let s = dir.path().join("screenshots").to_string_lossy().to_string();
        let m = dir.path().join("main_menu").to_string_lossy().to_string();

        let tests = resolve_tests(&[s.as_str(), m.as_str()]).unwrap();
        assert_eq!(names(&tests), ["exit", "options", "render"]);
    }

    // ── deduplication by canonical file path ────────────────────────────────

    #[test]
    fn deduplication() {
        let dir = TempDir::new().unwrap();
        sqf(dir.path(), "main_menu/exit.test.sqf");

        let root = dir.path().to_string_lossy().to_string();
        let sub = dir.path().join("main_menu").to_string_lossy().to_string();

        // sub discovers "exit"; root discovers "main_menu/exit" — same file, should dedup
        let tests = resolve_tests(&[sub.as_str(), root.as_str()]).unwrap();
        assert_eq!(tests.len(), 1, "expected exactly 1 test after dedup");
    }

    // ── single file ─────────────────────────────────────────────────────────

    #[test]
    fn single_file() {
        let dir = TempDir::new().unwrap();
        sqf(dir.path(), "main_menu/exit.test.sqf");

        let file = dir
            .path()
            .join("main_menu/exit.test.sqf")
            .to_string_lossy()
            .to_string();
        let tests = resolve_tests(&[file.as_str()]).unwrap();
        assert_eq!(names(&tests), ["exit"]);
    }

    // ── glob pattern ────────────────────────────────────────────────────────

    #[test]
    fn glob_pattern() {
        let dir = TempDir::new().unwrap();
        sqf(dir.path(), "main_menu/exit.test.sqf");
        sqf(dir.path(), "main_menu/options.test.sqf");
        sqf(dir.path(), "editor/enter_editor.test.sqf");

        let sep = std::path::MAIN_SEPARATOR;
        let pattern = format!(
            "{}{}main_menu{}*.test.sqf",
            dir.path().to_string_lossy(),
            sep,
            sep,
        );
        let tests = resolve_tests(&[pattern.as_str()]).unwrap();
        assert_eq!(names(&tests), ["exit", "options"]);
    }

    // ── prefix path matching ────────────────────────────────────────────────

    #[test]
    fn prefix_match() {
        let dir = TempDir::new().unwrap();
        sqf(dir.path(), "main_menu/exit.test.sqf");
        sqf(dir.path(), "main_menu/options.test.sqf");
        sqf(dir.path(), "editor/enter_editor.test.sqf");

        // "main_menu/ex" doesn't exist but is a prefix of "main_menu/exit.test.sqf"
        let prefix = dir
            .path()
            .join("main_menu")
            .join("ex")
            .to_string_lossy()
            .to_string();
        let tests = resolve_tests(&[prefix.as_str()]).unwrap();
        assert_eq!(tests.len(), 1);
        assert_eq!(tests[0].name, "exit");
    }

    // ── file and directory mixed ────────────────────────────────────────────

    #[test]
    fn file_and_directory_mixed() {
        let dir = TempDir::new().unwrap();
        sqf(dir.path(), "main_menu/exit.test.sqf");
        sqf(dir.path(), "main_menu/options.test.sqf");
        sqf(dir.path(), "editor/enter_editor.test.sqf");

        let file = dir
            .path()
            .join("main_menu/exit.test.sqf")
            .to_string_lossy()
            .to_string();
        let sub_dir = dir.path().join("editor").to_string_lossy().to_string();
        let tests = resolve_tests(&[file.as_str(), sub_dir.as_str()]).unwrap();
        assert_eq!(names(&tests), ["enter_editor", "exit"]);
    }

    // ── no duplicate on same path given twice ───────────────────────────────

    #[test]
    fn same_path_twice() {
        let dir = TempDir::new().unwrap();
        sqf(dir.path(), "exit.test.sqf");

        let p = dir.path().to_string_lossy().to_string();
        let tests = resolve_tests(&[p.as_str(), p.as_str()]).unwrap();
        assert_eq!(tests.len(), 1);
    }

    #[test]
    fn loads_tags_from_sqf_sidecar() {
        let dir = TempDir::new().unwrap();
        sqf(dir.path(), "editor/cursor.test.sqf");
        toml(
            dir.path(),
            "editor/cursor.test.toml",
            "tags = [\"GL-8\", \"editor\", \" cursor \"]\n",
        );

        let tests = resolve_tests(&[dir.path().to_string_lossy().as_ref()]).unwrap();
        let test = tests.iter().find(|t| t.name == "editor/cursor").unwrap();
        assert_eq!(test.tags, ["gl-8", "editor", "cursor"]);
    }

    #[test]
    fn filters_by_tag_case_insensitively() {
        let dir = TempDir::new().unwrap();
        sqf(dir.path(), "editor/cursor.test.sqf");
        sqf(dir.path(), "audio/radio.test.sqf");
        toml(
            dir.path(),
            "editor/cursor.test.toml",
            "tags = [\"GL-8\", \"editor\"]\n",
        );
        toml(
            dir.path(),
            "audio/radio.test.toml",
            "tags = [\"GL-11\", \"radio\"]\n",
        );

        let tests = resolve_tests(&[dir.path().to_string_lossy().as_ref()]).unwrap();
        let filtered =
            filter_tests_by_tags(tests, &[String::from("gl-8"), String::from("EDITOR")], &[]);
        assert_eq!(names(&filtered), ["editor/cursor"]);
    }

    #[test]
    fn skips_tags_after_include_filter() {
        let dir = TempDir::new().unwrap();
        sqf(dir.path(), "mp/admin.test.sqf");
        sqf(dir.path(), "ui/menu.test.sqf");
        sqf(dir.path(), "render/scene.test.sqf");
        toml(
            dir.path(),
            "mp/admin.test.toml",
            "tags = [\"headless\", \"full_game\"]\n",
        );
        toml(dir.path(), "ui/menu.test.toml", "tags = [\"headless\"]\n");
        toml(
            dir.path(),
            "render/scene.test.toml",
            "tags = [\"headful\"]\n",
        );

        let tests = resolve_tests(&[dir.path().to_string_lossy().as_ref()]).unwrap();
        let filtered = filter_tests_by_tags(
            tests,
            &[String::from("HEADLESS")],
            &[String::from("full_game")],
        );
        assert_eq!(names(&filtered), ["ui/menu"]);
    }

    // ── error cases ─────────────────────────────────────────────────────────

    #[test]
    fn nonexistent_path_no_ancestor_with_tests() {
        // A completely made-up deep path must fail gracefully AND fast: it must bail
        // at the missing parent, not climb to the drive root and discover() the whole
        // drive (that scan took ~90s — this cap is the regression guard).
        let start = std::time::Instant::now();
        let result = resolve_tests(&["/nonexistent_ofpr_test_root_xyz/a/b/c"]);
        assert!(result.is_err());
        assert!(
            start.elapsed() < std::time::Duration::from_secs(5),
            "resolve_tests on a bogus path took {:?} — should bail at the missing parent, not scan the drive",
            start.elapsed()
        );
    }

    #[test]
    fn glob_no_match_error() {
        let dir = TempDir::new().unwrap();
        let pattern = format!("{}/nonexistent_*.test.sqf", dir.path().to_string_lossy());
        let result = resolve_tests(&[pattern.as_str()]);
        assert!(result.is_err());
    }

    // ── parse_statements: comment handling ──────────────────────────────────

    #[test]
    fn parse_statements_strips_line_comments() {
        let src = "\
// full-line comment with triAssertDisplay 9099 inside
triClickText \"OPTIONS\"
// double-slash comment also stripped
triSimFrames 60   // trailing comment after real statement
triAssertDisplay 9099
";
        let stmts = parse_statements(src);
        assert_eq!(
            stmts,
            vec![
                "triClickText \"OPTIONS\"",
                "triSimFrames 60",
                "triAssertDisplay 9099"
            ],
        );
    }

    #[test]
    fn parse_statements_keeps_inline_semicolon_split() {
        let src = "triSendKey 79; triSimFrames 2";
        let stmts = parse_statements(src);
        assert_eq!(stmts, vec!["triSendKey 79", "triSimFrames 2"]);
    }

    #[test]
    fn parse_statements_indented_line_comment() {
        let src = "    // indented comment\ntriEndTest";
        let stmts = parse_statements(src);
        assert_eq!(stmts, vec!["triEndTest"]);
    }

    // The parser is string-aware, so `;`, `//` and `#` inside a string literal are
    // NOT taken as separator/comment — the game sees the string verbatim, i.e.
    // semicolons behave exactly as in the original engine.
    #[test]
    fn parse_statements_keeps_semicolon_inside_string() {
        let src = "triFoo \"a;b;c\"";
        assert_eq!(parse_statements(src), vec!["triFoo \"a;b;c\""]);
    }

    #[test]
    fn parse_statements_keeps_hash_and_slashes_inside_string() {
        // "#login" / "a//b" must survive: # is not a comment, // inside a string is data.
        let src = "triNetCommand \"#login\"\ntriFoo \"http://x\"";
        assert_eq!(
            parse_statements(src),
            vec!["triNetCommand \"#login\"", "triFoo \"http://x\""],
        );
    }

    #[test]
    fn parse_statements_double_quote_escape_in_string() {
        // SQF embeds a quote as "" — it must not end the string early.
        let src = "triFoo \"say \"\"hi;there\"\" now\"";
        assert_eq!(
            parse_statements(src),
            vec!["triFoo \"say \"\"hi;there\"\" now\""]
        );
    }

    #[test]
    fn parse_condition_statement_rejects_empty_braced_body() {
        assert_eq!(
            parse_condition_statement("triWaitUntil {}", "triWaitUntil"),
            None
        );
        assert_eq!(
            parse_condition_statement("triWaitUntil {   }", "triWaitUntil"),
            None
        );
    }

    #[test]
    fn parse_condition_statement_trims_braced_body() {
        assert_eq!(
            parse_condition_statement("triSimUntil { triSceneReady }", "triSimUntil"),
            Some(String::from("triSceneReady"))
        );
    }

    #[test]
    fn parse_triscreenshot_label_reads_quoted_label() {
        assert_eq!(
            parse_triscreenshot_label("triScreenshot \"autotest_basic\""),
            Some(String::from("autotest_basic"))
        );
    }

    #[test]
    fn parse_triscreenshot_label_handles_doubled_quotes() {
        assert_eq!(
            parse_triscreenshot_label("triScreenshot \"say \"\"hi\"\"\""),
            Some(String::from("say \"hi\""))
        );
    }

    #[test]
    fn parse_triscreenshot_label_rejects_unquoted_label() {
        assert_eq!(
            parse_triscreenshot_label("triScreenshot autotest_basic"),
            None
        );
    }

    // ── expand_includes ─────────────────────────────────────────────────────

    #[test]
    fn expand_includes_inlines_helper() {
        let dir = TempDir::new().unwrap();
        let helper = dir.path().join("helper.sqf");
        fs::write(
            &helper,
            "triClickText \"OPTIONS\"\ntriAssertEq [(triDisplay), 9099]\n",
        )
        .unwrap();
        let source = "#include \"helper.sqf\"\ntriClick 1102\n".to_string();
        let expanded = expand_includes(&source, dir.path()).unwrap();
        let stmts = parse_statements(&expanded);
        assert_eq!(
            stmts,
            vec![
                "triClickText \"OPTIONS\"",
                "triAssertEq [(triDisplay), 9099]",
                "triClick 1102"
            ]
        );
    }

    #[test]
    fn expand_includes_non_directive_passes_through() {
        let dir = TempDir::new().unwrap();
        let source = "triEndTest\n";
        let expanded = expand_includes(source, dir.path()).unwrap();
        assert_eq!(parse_statements(&expanded), vec!["triEndTest"]);
    }

    #[test]
    fn expand_includes_recursive() {
        let dir = TempDir::new().unwrap();
        let inner = dir.path().join("inner.sqf");
        fs::write(&inner, "triClick 9099\n").unwrap();
        let outer = dir.path().join("outer.sqf");
        fs::write(&outer, "#include \"inner.sqf\"\ntriClick 1102\n").unwrap();
        let source = "#include \"outer.sqf\"\ntriEndTest\n";
        let expanded = expand_includes(source, dir.path()).unwrap();
        let stmts = parse_statements(&expanded);
        assert_eq!(stmts, vec!["triClick 9099", "triClick 1102", "triEndTest"]);
    }

    #[test]
    fn expand_includes_missing_file_errors() {
        let dir = TempDir::new().unwrap();
        let source = "#include \"nonexistent.sqf\"\n";
        let result = expand_includes(source, dir.path());
        assert!(result.is_err());
    }

    #[test]
    fn expand_includes_inline_comment_not_treated_as_directive() {
        // A #include inside a // comment line must not be expanded.
        let dir = TempDir::new().unwrap();
        let source = "// #include \"helper.sqf\"\ntriEndTest\n";
        let expanded = expand_includes(source, dir.path()).unwrap();
        // The comment line passes through (expand_includes is text-level, not
        // comment-aware; the comment is stripped later by parse_statements).
        let stmts = parse_statements(&expanded);
        assert_eq!(stmts, vec!["triEndTest"]);
    }
}
