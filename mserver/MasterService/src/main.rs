mod probe;

use std::fs;
use std::net::{IpAddr, Ipv4Addr, Ipv6Addr, SocketAddr};
use std::path::{Path, PathBuf};
use std::sync::Arc;
use std::time::Duration;

use anyhow::{Context, Result};
use clap::{Args, Parser, Subcommand};
use papa_bear_master_service::dev_seed::{seed_dev_data, seed_dev_mods};
use papa_bear_master_service::http::build_router_with_store;
use papa_bear_master_service::mods::{ModStore, S3StoreConfig};
use papa_bear_master_service::repository::{sqlite_url_for_path, SqliteServerDirectory};
use tracing::info;
use tracing_subscriber::EnvFilter;

#[derive(Parser, Debug)]
#[command(name = "papa-bear-master-service")]
struct Cli {
    #[command(subcommand)]
    command: Command,
}

#[derive(Subcommand, Debug)]
enum Command {
    /// Run the HTTP directory service (sole owner of the SQLite database).
    Server(ServerArgs),
    /// Run the reachability prober loop against a running service.
    Probe(ProbeArgs),
}

#[derive(Args, Debug)]
struct ServerArgs {
    #[arg(long, default_value = "127.0.0.1:8080")]
    listen: String,

    #[arg(long, default_value = "tmp/papa-bear-master-service.sqlite3")]
    db: String,

    #[arg(long)]
    admin_api_key: Option<String>,

    #[arg(long)]
    mods_dir: Option<String>,

    /// Trusted header with the real client IP (e.g. `X-Forwarded-For` behind an ingress).
    /// When set, register/heartbeat bind a server's address+id to it instead of the body.
    #[arg(long)]
    client_ip_header: Option<String>,

    /// Require the server token (issued on register) for heartbeat/unregister of an active
    /// row. A token is issued regardless; enable this once publishers send it back.
    #[arg(long)]
    require_server_token: bool,

    #[arg(long)]
    dev: bool,
}

#[derive(Args, Debug)]
struct ProbeArgs {
    /// Directory service to read from and report observations to.
    #[arg(long, default_value = "http://127.0.0.1:8080")]
    master_server: String,

    /// Seconds between probe rounds.
    #[arg(long, default_value_t = 60)]
    interval_secs: u64,

    /// Per-server UDP query timeout in milliseconds.
    #[arg(long, default_value_t = 1500)]
    timeout_ms: u64,

    /// Maximum servers probed concurrently.
    #[arg(long, default_value_t = 16)]
    concurrency: usize,

    /// Remove servers with no heartbeat for this many seconds each round (0 disables).
    /// Default 3600s matches the original PowerServer off-board window (60 min).
    #[arg(long, default_value_t = 3600)]
    prune_max_age_secs: u64,

    /// Admin key for `/observe` and `/prune` when the service is protected.
    #[arg(long)]
    admin_api_key: Option<String>,
}

fn init_tracing() {
    tracing_subscriber::fmt()
        .with_env_filter(
            EnvFilter::try_from_default_env().unwrap_or_else(|_| EnvFilter::new("info")),
        )
        .with_target(false)
        .compact()
        .init();
}

fn main() -> Result<()> {
    init_tracing();
    match Cli::parse().command {
        Command::Server(args) => tokio::runtime::Runtime::new()?.block_on(run_server(args)),
        Command::Probe(args) => probe::run(&probe::ProbeConfig {
            master_server: args.master_server,
            interval: Duration::from_secs(args.interval_secs),
            timeout: Duration::from_millis(args.timeout_ms),
            concurrency: args.concurrency,
            prune_max_age: (args.prune_max_age_secs > 0)
                .then(|| Duration::from_secs(args.prune_max_age_secs)),
            admin_api_key: args.admin_api_key,
        }),
    }
}

async fn run_server(args: ServerArgs) -> Result<()> {
    info!("Starting PAPA BEAR");

    // `--db` accepts either a sqlx URL (postgres://… or sqlite://…) or a bare file path,
    // which is treated as a local SQLite database (created if missing).
    let (db_url, db_path) = resolve_db_url(&args.db)?;
    if let Some(db_path) = &db_path {
        if let Some(parent) = ensure_parent_directory(db_path)? {
            info!("Database directory ready at {}", parent.display());
        }
    }

    let directory = Arc::new(SqliteServerDirectory::open(&db_url).await?);
    info!("Using database {}", redact_db_url(&db_url));

    let mods_path_hint = db_path.clone().unwrap_or_else(|| PathBuf::from("."));
    let mods_dir = resolve_mods_path(args.mods_dir.as_deref(), &mods_path_hint, args.dev)?;
    let mod_store = build_mod_store(mods_dir.as_deref())?;
    info!(
        "Mod store: {}",
        if mod_store.is_some() {
            "enabled"
        } else {
            "disabled"
        }
    );

    if args.dev {
        let seeded_servers = seed_dev_data(&directory, current_time_millis()?).await?;
        let seeded_mods = if let Some(store) = &mod_store {
            seed_dev_mods(store).await?
        } else {
            0
        };
        info!("Loaded dev seed ({seeded_servers} servers, {seeded_mods} mods)");
    }

    let app = build_router_with_store(
        directory,
        args.admin_api_key,
        mod_store,
        args.client_ip_header,
        args.require_server_token,
    );
    let listener = tokio::net::TcpListener::bind(&args.listen).await?;
    let local_addr = listener.local_addr()?;
    log_listen_urls(local_addr);
    axum::serve(
        listener,
        app.into_make_service_with_connect_info::<SocketAddr>(),
    )
    .await?;
    Ok(())
}

/// Resolve `--db` to a sqlx URL plus, for SQLite, the on-disk path (so its parent directory
/// can be created). A value containing `://` is taken as a URL verbatim; anything else is a
/// local SQLite file path turned into `sqlite://<abs-path>?mode=rwc`.
fn resolve_db_url(value: &str) -> Result<(String, Option<PathBuf>)> {
    if value.contains("://") {
        let path = value
            .strip_prefix("sqlite://")
            .map(|rest| PathBuf::from(rest.split('?').next().unwrap_or(rest)));
        return Ok((value.to_string(), path));
    }
    let path = if Path::new(value).is_absolute() {
        PathBuf::from(value)
    } else {
        std::env::current_dir()
            .context("resolving current directory for database path")?
            .join(value)
    };
    let url = sqlite_url_for_path(&path);
    Ok((url, Some(path)))
}

fn redact_db_url(db_url: &str) -> String {
    if let Some((scheme, rest)) = db_url.split_once("://") {
        if let Some(at) = rest.find('@') {
            if rest[..at].contains(':') {
                return format!("{scheme}://<credentials>@{}", &rest[at + 1..]);
            }
        }
    }
    db_url.to_string()
}

/// Build the mod artifact store: an S3 (MinIO-compatible) store when `MODS_S3_BUCKET` is set
/// — the deploy path, shared by all replicas — otherwise a local-filesystem store at
/// `mods_dir` for `cargo`-local dev, or none when mods are disabled.
fn build_mod_store(mods_dir: Option<&Path>) -> Result<Option<ModStore>> {
    if let Ok(bucket) = std::env::var("MODS_S3_BUCKET") {
        let endpoint = std::env::var("MODS_S3_ENDPOINT")
            .context("MODS_S3_ENDPOINT must be set alongside MODS_S3_BUCKET")?;
        let region = std::env::var("MODS_S3_REGION").unwrap_or_else(|_| "us-east-1".to_string());
        let access_key = std::env::var("MODS_S3_ACCESS_KEY")
            .context("MODS_S3_ACCESS_KEY must be set alongside MODS_S3_BUCKET")?;
        let secret_key = std::env::var("MODS_S3_SECRET_KEY")
            .context("MODS_S3_SECRET_KEY must be set alongside MODS_S3_BUCKET")?;
        let allow_http = endpoint.starts_with("http://");
        let virtual_hosted_style = env_bool("MODS_S3_VIRTUAL_HOSTED_STYLE", false);
        let prefix = std::env::var("MODS_S3_PREFIX").ok();
        let signed_downloads = env_bool("MODS_S3_SIGNED_DOWNLOADS", false);
        let signed_url_ttl = Duration::from_secs(env_u64("MODS_SIGNED_URL_TTL_SECS", 4 * 60 * 60));
        return Ok(Some(ModStore::s3(&S3StoreConfig {
            bucket: &bucket,
            endpoint: &endpoint,
            region: &region,
            access_key: &access_key,
            secret_key: &secret_key,
            allow_http,
            virtual_hosted_style,
            prefix: prefix.as_deref(),
            signed_downloads,
            signed_url_ttl,
        })?));
    }
    mods_dir.map(ModStore::local).transpose()
}

fn env_bool(name: &str, default: bool) -> bool {
    std::env::var(name)
        .ok()
        .and_then(|value| match value.trim().to_ascii_lowercase().as_str() {
            "1" | "true" | "yes" | "on" => Some(true),
            "0" | "false" | "no" | "off" => Some(false),
            _ => None,
        })
        .unwrap_or(default)
}

fn env_u64(name: &str, default: u64) -> u64 {
    std::env::var(name)
        .ok()
        .and_then(|value| value.trim().parse::<u64>().ok())
        .filter(|value| *value > 0)
        .unwrap_or(default)
}

fn ensure_parent_directory(path: &Path) -> Result<Option<PathBuf>> {
    let Some(parent) = path
        .parent()
        .filter(|parent| !parent.as_os_str().is_empty())
    else {
        return Ok(None);
    };

    fs::create_dir_all(parent)
        .with_context(|| format!("creating database directory {}", parent.display()))?;
    Ok(Some(parent.to_path_buf()))
}

fn log_listen_urls(local_addr: SocketAddr) {
    let base_url = primary_startup_url(local_addr);
    info!("PAPA BEAR listening on {base_url}");
    info!("OpenAPI: {base_url}/openapi/v1.yaml");
}

fn primary_startup_url(local_addr: SocketAddr) -> String {
    match local_addr.ip() {
        IpAddr::V4(ip) if ip == Ipv4Addr::UNSPECIFIED => {
            format!("http://127.0.0.1:{}", local_addr.port())
        }
        IpAddr::V6(ip) if ip == Ipv6Addr::UNSPECIFIED => {
            format!("http://localhost:{}", local_addr.port())
        }
        _ => socket_addr_url(local_addr),
    }
}

#[cfg(test)]
fn startup_urls(local_addr: SocketAddr) -> Vec<String> {
    vec![
        primary_startup_url(local_addr),
        format!("{}/openapi/v1.yaml", primary_startup_url(local_addr)),
    ]
}

fn socket_addr_url(socket_addr: SocketAddr) -> String {
    match socket_addr.ip() {
        IpAddr::V4(_) => format!("http://{socket_addr}"),
        IpAddr::V6(_) => format!("http://[{}]:{}", socket_addr.ip(), socket_addr.port()),
    }
}

fn current_time_millis() -> Result<i64> {
    let duration = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .context("reading current time for dev seed")?;
    i64::try_from(duration.as_millis()).context("converting current time to milliseconds")
}

fn resolve_mods_path(mods_dir: Option<&str>, db_path: &Path, dev: bool) -> Result<Option<PathBuf>> {
    if let Some(mods_dir) = mods_dir {
        return resolve_user_path(mods_dir).map(Some);
    }

    if dev {
        let parent = db_path.parent().unwrap_or_else(|| Path::new("."));
        return Ok(Some(parent.join("papa-bear-master-service-dev-mods")));
    }

    Ok(None)
}

fn resolve_user_path(path: &str) -> Result<PathBuf> {
    let path = PathBuf::from(path);
    if path.is_absolute() {
        Ok(path)
    } else {
        Ok(std::env::current_dir()
            .context("resolving current directory for mods path")?
            .join(path))
    }
}

#[cfg(test)]
mod tests {
    use std::net::{Ipv4Addr, SocketAddr, SocketAddrV4};

    use tempfile::tempdir;

    use super::{
        ensure_parent_directory, redact_db_url, resolve_db_url, resolve_mods_path, startup_urls,
    };

    // Regression: a bare file path (the default `--db`) must become an authority-free `sqlite:`
    // URL. The old `sqlite://{abs-path}` form broke on Windows — the `url` crate read the drive
    // path as host:port and the service failed to open its database (SQLITE_CANTOPEN).
    #[test]
    fn resolve_db_url_builds_authority_free_sqlite_url_from_bare_path() {
        let temp_dir = tempdir().unwrap();
        let db_path = temp_dir.path().join("papa-bear.sqlite3");

        let (url, resolved) = resolve_db_url(db_path.to_str().unwrap()).unwrap();

        assert!(url.starts_with("sqlite:"));
        assert!(!url.starts_with("sqlite://"));
        assert!(!url.contains('\\'));
        assert!(url.ends_with("?mode=rwc"));
        assert_eq!(resolved.as_deref(), Some(db_path.as_path()));
    }

    // An explicit sqlx URL (e.g. a deploy's `postgres://…`) is passed through verbatim.
    #[test]
    fn resolve_db_url_passes_explicit_urls_through_verbatim() {
        let (url, resolved) = resolve_db_url("postgres://user:pw@db.host/papa").unwrap();
        assert_eq!(url, "postgres://user:pw@db.host/papa");
        assert_eq!(resolved, None);
    }

    #[test]
    fn redact_db_url_hides_credentials() {
        assert_eq!(
            redact_db_url("postgres://user:pw@db.host/papa"),
            "postgres://<credentials>@db.host/papa"
        );
        assert_eq!(
            redact_db_url("sqlite:/tmp/papa-bear.sqlite3?mode=rwc"),
            "sqlite:/tmp/papa-bear.sqlite3?mode=rwc"
        );
    }

    #[test]
    fn ensure_parent_directory_creates_missing_directory() {
        let temp_dir = tempdir().unwrap();
        let db_path = temp_dir.path().join("nested/path/papa-bear.sqlite3");

        let created_parent = ensure_parent_directory(&db_path).unwrap();

        let created_parent = created_parent.unwrap();
        assert_eq!(created_parent, temp_dir.path().join("nested/path"));
        assert!(created_parent.exists());
    }

    #[test]
    fn startup_urls_expand_unspecified_bind_to_local_browser_hints() {
        let urls = startup_urls(SocketAddr::V4(SocketAddrV4::new(
            Ipv4Addr::UNSPECIFIED,
            8080,
        )));

        assert_eq!(
            urls,
            vec![
                "http://127.0.0.1:8080".to_string(),
                "http://127.0.0.1:8080/openapi/v1.yaml".to_string(),
            ]
        );
    }

    #[test]
    fn resolve_mods_path_defaults_for_dev_mode() {
        let temp_dir = tempdir().unwrap();
        let db_path = temp_dir
            .path()
            .join("data/papa-bear-master-service.sqlite3");

        let mods_path = resolve_mods_path(None, &db_path, true).unwrap();

        assert_eq!(
            mods_path,
            Some(
                temp_dir
                    .path()
                    .join("data/papa-bear-master-service-dev-mods")
            )
        );
    }
}
