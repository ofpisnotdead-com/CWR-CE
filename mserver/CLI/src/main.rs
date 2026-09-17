//! `papa` — the PapaBear workshop CLI: pack a folder into a PBO, inspect or extract one,
//! and publish a packed mod to a PapaBear master service.

use std::fs;
use std::io::{Cursor, Read, Seek, SeekFrom, Write};
use std::path::Path;
use std::sync::Arc;
use std::time::Duration;

use anyhow::{bail, Context, Result};
use clap::{Args, Parser, Subcommand};
use papa_bear_archive::Pbo;
use papa_bear_client::codec::SessionResponse;
use papa_bear_client::query::{query_server, ServerStatus};
use sha2::{Digest, Sha256};
use tempfile::NamedTempFile;

/// Default game host port for a direct `query` probe when none is given.
const DEFAULT_GAME_PORT: u16 = 2302;

/// Active-difficulty aid flags, in engine `DifficultyType` order — bit i of the
/// directory's `difficulty` mask is flag i.
const DIFFICULTY_FLAGS: [&str; 12] = [
    "Armor",
    "FriendlyTag",
    "EnemyTag",
    "HUD",
    "AutoSpot",
    "Map",
    "WeaponCursor",
    "AutoGuideAT",
    "ClockIndicator",
    "3rdPersonView",
    "Tracers",
    "UltraAI",
];

/// `RespawnMode` names, in enum order.
const RESPAWN_NAMES: [&str; 6] = ["none", "seagull", "instant", "base", "group", "friendly"];

/// Default master service — the production workshop. The C++ client uses the same host.
const DEFAULT_MASTER: &str = "https://papa-bear.cz";

#[derive(Parser, Debug)]
#[command(name = "papa", about = "PapaBear workshop CLI", version)]
struct Cli {
    #[command(subcommand)]
    command: Command,

    /// Base URL of the master service (also read from `PAPA_MASTER`). Used by publish/list.
    #[arg(long, global = true, env = "PAPA_MASTER", default_value = DEFAULT_MASTER)]
    master: String,
    /// Skip TLS certificate verification, like `curl -k`. Used by publish/list.
    #[arg(short = 'k', long, global = true)]
    insecure: bool,
}

#[derive(Subcommand, Debug)]
enum Command {
    /// Pack a folder into a PBO archive.
    Pack(PackArgs),
    /// Extract a PBO archive into a directory.
    Unpack(UnpackArgs),
    /// Print the entries and properties of a PBO archive.
    Info(InfoArgs),
    /// Publish a mod (a folder to pack, or an already-packed .pbo) to a workshop.
    Publish(Box<PublishArgs>),
    /// Set the game versions that can use one package revision.
    Compatibility(CompatibilityArgs),
    /// List the mods on a remote workshop.
    List,
    /// Download + unpack mods from a workshop into a mods directory (for servers).
    Install(InstallArgs),
    /// Probe a running server directly over UDP (no master server).
    Query(QueryArgs),
    /// List servers registered on the master server.
    Servers(ServersArgs),
    /// Show full details for one server from the master server.
    Server(ServerArgs),
}

#[derive(Args, Debug)]
struct PackArgs {
    /// Source folder to pack.
    source: String,
    /// Output `.pbo` path.
    #[arg(short, long)]
    output: String,
    /// Addon prefix property (e.g. `mymod\addon`).
    #[arg(long)]
    prefix: Option<String>,
}

#[derive(Args, Debug)]
struct UnpackArgs {
    /// PBO archive to extract.
    file: String,
    /// Output directory.
    #[arg(short, long)]
    output: String,
}

#[derive(Args, Debug)]
struct InfoArgs {
    /// PBO archive to inspect.
    file: String,
}

#[derive(Args, Clone, Debug)]
struct PublishArgs {
    /// A folder to pack, or an already-packed `.pbo` to upload as-is.
    source: String,
    /// Admin API key (also read from `PAPA_ADMIN_KEY`).
    #[arg(long, env = "PAPA_ADMIN_KEY")]
    admin_key: String,
    /// Stable workshop identity. Defaults to `modId` in the source `mod.json`.
    #[arg(long)]
    mod_id: Option<String>,
    /// Display name of the mod. Defaults to the source `mod.json`.
    #[arg(long)]
    name: Option<String>,
    /// Game application identifier, e.g. `CWR`.
    #[arg(long)]
    app: Option<String>,
    /// Compatible game data version, e.g. `305`.
    #[arg(long)]
    actver: Option<i32>,
    /// Additional compatible game data version (repeatable).
    #[arg(long = "compatible-actver")]
    compatible_actvers: Vec<i32>,
    /// Compatible build tag.
    #[arg(long = "vertag", visible_alias = "version-tag")]
    version_tag: Option<String>,
    /// Explicit version; defaults to the artifact content hash on the server.
    #[arg(long)]
    version: Option<String>,
    /// Short description.
    #[arg(long)]
    description: Option<String>,
    /// Author (repeatable).
    #[arg(long = "author")]
    authors: Vec<String>,
    /// Project homepage URL.
    #[arg(long)]
    homepage_url: Option<String>,
    /// Canonical install folder name (verbatim, incl. any `@` and case), e.g. `@CSLA`.
    /// Clients/servers install into this folder; falls back to `@<modId>` if unset.
    #[arg(long)]
    folder_name: Option<String>,
    /// Addon prefix property baked into the PBO.
    #[arg(long)]
    prefix: Option<String>,
}

#[derive(Args, Debug)]
struct CompatibilityArgs {
    /// Stable workshop identity.
    mod_id: String,
    /// Package revision to expose.
    revision: u64,
    /// Compatible game data version (repeatable).
    #[arg(long = "actver", required = true)]
    compatible_actvers: Vec<i32>,
    /// Admin API key (also read from `PAPA_ADMIN_KEY`).
    #[arg(long, env = "PAPA_ADMIN_KEY")]
    admin_key: String,
}

#[derive(Args, Debug)]
struct InstallArgs {
    /// Mods to install: modIds (`papa list`), folder names, or unique prefixes —
    /// space- and/or `;`-separated. e.g. `csla-2.2`, `CSLA`, or `"CSLA;@DVDcrcti"`.
    #[arg(required = true)]
    mod_ids: Vec<String>,
    /// Mods directory to install into (each mod lands in `<dir>/<folderName>`).
    #[arg(short, long, default_value = "mods")]
    dir: String,
}

#[derive(Args, Debug)]
struct QueryArgs {
    /// Server address as `host` or `host:port` (default port 2302).
    address: String,
    /// Probe timeout in milliseconds.
    #[arg(long, default_value_t = 2000)]
    timeout_ms: u64,
}

#[derive(Args, Debug)]
struct ServersArgs {
    /// Filter: substring match on server name.
    #[arg(long)]
    hostname: Option<String>,
    /// Filter: substring match on mission / game type.
    #[arg(long)]
    gametype: Option<String>,
    /// Filter: minimum current players.
    #[arg(long)]
    min_players: Option<i32>,
    /// Filter: maximum current players.
    #[arg(long)]
    max_players: Option<i32>,
    /// Include servers that are full.
    #[arg(long)]
    include_full: bool,
    /// Include password-protected servers.
    #[arg(long)]
    include_passworded: bool,
    /// Include stale / unverified servers.
    #[arg(long)]
    include_unverified: bool,
    /// Maximum rows to return.
    #[arg(long)]
    limit: Option<usize>,
}

#[derive(Args, Debug)]
struct ServerArgs {
    /// Server id (usually `<address>:<hostport>`), from `papa servers`.
    server_id: String,
}

fn main() -> Result<()> {
    let cli = Cli::parse();
    match &cli.command {
        Command::Pack(args) => run_pack(args),
        Command::Unpack(args) => run_unpack(args),
        Command::Info(args) => run_info(args),
        Command::Publish(args) => run_publish(args, &cli.master, cli.insecure),
        Command::Compatibility(args) => run_compatibility(args, &cli.master, cli.insecure),
        Command::List => run_list(&cli.master, cli.insecure),
        Command::Install(args) => run_install(args, &cli.master, cli.insecure),
        Command::Query(args) => run_query(args),
        Command::Servers(args) => run_servers(args, &cli.master, cli.insecure),
        Command::Server(args) => run_server(args, &cli.master, cli.insecure),
    }
}

fn run_compatibility(args: &CompatibilityArgs, master: &str, insecure: bool) -> Result<()> {
    let base = master.trim_end_matches('/');
    let url = format!(
        "{base}/v1/mods/{}/revisions/{}/compatibility",
        args.mod_id, args.revision
    );
    let body = serde_json::to_string(&serde_json::json!({
        "compatibleActvers": args.compatible_actvers
    }))?;
    let response = http_agent(insecure)?
        .put(&url)
        .set("x-api-key", &args.admin_key)
        .set("content-type", "application/json")
        .send_string(&body);
    match response {
        Ok(_) => {
            println!(
                "set {} revision {} compatibility to {}",
                args.mod_id,
                args.revision,
                args.compatible_actvers
                    .iter()
                    .map(i32::to_string)
                    .collect::<Vec<_>>()
                    .join(",")
            );
            Ok(())
        }
        Err(ureq::Error::Status(code, response)) => {
            let detail = response.into_string().unwrap_or_default();
            bail!(
                "compatibility update rejected: HTTP {code}: {}",
                detail.trim()
            );
        }
        Err(error) => bail!("compatibility update failed: {error}"),
    }
}

fn run_pack(args: &PackArgs) -> Result<()> {
    let pbo = Pbo::pack_dir(&args.source, args.prefix.as_deref())
        .with_context(|| format!("packing {}", args.source))?;
    pbo.write_path(&args.output)
        .with_context(|| format!("writing {}", args.output))?;
    println!("packed {} file(s) into {}", pbo.entries.len(), args.output);
    Ok(())
}

fn run_unpack(args: &UnpackArgs) -> Result<()> {
    let pbo = Pbo::read_path(&args.file).with_context(|| format!("reading {}", args.file))?;
    pbo.unpack_to_dir(&args.output)
        .with_context(|| format!("extracting into {}", args.output))?;
    println!(
        "extracted {} file(s) into {}",
        pbo.entries.len(),
        args.output
    );
    Ok(())
}

fn run_info(args: &InfoArgs) -> Result<()> {
    let pbo = Pbo::read_path(&args.file).with_context(|| format!("reading {}", args.file))?;
    if !pbo.properties.is_empty() {
        println!("properties:");
        for (key, value) in &pbo.properties {
            println!("  {key} = {value}");
        }
    }
    println!("entries ({}):", pbo.entries.len());
    for entry in &pbo.entries {
        let tag = if entry.is_compressed() {
            "lzss "
        } else {
            "store"
        };
        println!("  {tag} {:>10}  {}", entry.unpacked_size(), entry.name);
    }
    Ok(())
}

fn run_publish(args: &PublishArgs, master: &str, insecure: bool) -> Result<()> {
    let args = publish_args_with_manifest(args)?;
    let mod_id = args.mod_id.as_deref();
    if let Some(mod_id) = mod_id {
        if mod_id.is_empty()
            || mod_id.contains('/')
            || mod_id.contains('\\')
            || mod_id.contains("..")
        {
            bail!("invalid mod id '{mod_id}'");
        }
    }
    let existing = mod_id
        .map(|id| remote_mod_exists(master, id, insecure))
        .transpose()?
        .unwrap_or(false);
    let name = args.name.as_deref().unwrap_or("");
    if !existing && name.is_empty() {
        bail!("new packages require --name or a name in the source mod.json");
    }
    run_upload(&args, mod_id.filter(|_| existing), master, insecure)
}

fn remote_mod_exists(master: &str, mod_id: &str, insecure: bool) -> Result<bool> {
    let base = master.trim_end_matches('/');
    match http_agent(insecure)?
        .get(&format!("{base}/v1/mods/{mod_id}"))
        .call()
    {
        Ok(_) => Ok(true),
        Err(ureq::Error::Status(404, _)) => Ok(false),
        Err(ureq::Error::Status(code, response)) => {
            let detail = response.into_string().unwrap_or_default();
            bail!("catalog lookup failed: HTTP {code}: {}", detail.trim());
        }
        Err(error) => bail!("catalog lookup failed: {error}"),
    }
}

fn run_upload(
    args: &PublishArgs,
    mod_id: Option<&str>,
    master: &str,
    insecure: bool,
) -> Result<()> {
    let boundary = "----PapaBearCLIboundaryVqZ9k2bX7nMpL4tD";
    let prelude = build_multipart_prelude(boundary, args);
    let trailer = format!("\r\n--{boundary}--\r\n").into_bytes();

    // Produce the raw PBO (folder is packed; an already-packed .pbo is validated then streamed
    // from disk), then zstd-compress it locally to a temp file. The service stores the uploaded
    // `.pbo.zst` verbatim, so the compression cost is borne here — and the upload is smaller.
    let raw: Box<dyn Read> = build_artifact_reader(args)?;
    let (artifact, artifact_len) = compress_artifact(raw)?;

    let total = prelude.len() as u64 + artifact_len + trailer.len() as u64;
    let upload = artifact
        .reopen()
        .context("reopening compressed artifact for upload")?;
    let body = Cursor::new(prelude)
        .chain(upload)
        .chain(Cursor::new(trailer));

    let base = master.trim_end_matches('/');
    let url = mod_id.map_or_else(
        || format!("{base}/v1/mods"),
        |mod_id| format!("{base}/v1/mods/{mod_id}/revisions"),
    );
    let response = http_agent(insecure)?
        .post(&url)
        .set("x-api-key", &args.admin_key)
        .set(
            "content-type",
            &format!("multipart/form-data; boundary={boundary}"),
        )
        .set("content-length", &total.to_string())
        .send(body);

    match response {
        Ok(ok) => {
            let entry: serde_json::Value = ok.into_json().context("parsing publish response")?;
            let response_mod_id = entry["modId"].as_str().unwrap_or("?");
            let response_name = upload_result_name(&entry, args.name.as_deref().unwrap_or(""));
            let version = entry["version"].as_str().unwrap_or("?");
            let revision = entry["packageRevision"].as_u64().unwrap_or(1);
            let download = entry["downloadUrl"].as_str().unwrap_or("?");
            let action = if mod_id.is_some() {
                "updated"
            } else {
                "published"
            };
            println!(
                "{action} '{response_name}' as {response_mod_id}, version {version}, package revision {revision} ({artifact_len} bytes compressed) -> {base}{download}"
            );
            Ok(())
        }
        Err(ureq::Error::Status(code, response)) => {
            let detail = response.into_string().unwrap_or_default();
            bail!("publish rejected: HTTP {code}: {}", detail.trim());
        }
        Err(error) => bail!("publish request failed: {error}"),
    }
}

fn upload_result_name<'a>(entry: &'a serde_json::Value, requested_name: &'a str) -> &'a str {
    entry["name"].as_str().unwrap_or(requested_name)
}

fn publish_args_with_manifest(args: &PublishArgs) -> Result<PublishArgs> {
    let manifest = read_source_manifest(Path::new(&args.source))?;
    let mut resolved = args.clone();
    if let Some(manifest) = manifest {
        resolved.mod_id = resolved.mod_id.or_else(|| json_string(&manifest, "modId"));
        resolved.name = resolved.name.or_else(|| json_string(&manifest, "name"));
        resolved.app = resolved.app.or_else(|| json_string(&manifest, "app"));
        resolved.actver = resolved.actver.or_else(|| {
            manifest["actver"]
                .as_i64()
                .and_then(|value| i32::try_from(value).ok())
        });
        if resolved.compatible_actvers.is_empty() {
            resolved.compatible_actvers = manifest["compatibleActvers"]
                .as_array()
                .into_iter()
                .flatten()
                .filter_map(serde_json::Value::as_i64)
                .filter_map(|value| i32::try_from(value).ok())
                .collect();
        }
        resolved.version_tag = resolved
            .version_tag
            .or_else(|| json_string(&manifest, "vertag"));
        resolved.version = resolved
            .version
            .or_else(|| json_string(&manifest, "version"));
        resolved.description = resolved
            .description
            .or_else(|| json_string(&manifest, "description"));
        resolved.homepage_url = resolved
            .homepage_url
            .or_else(|| json_string(&manifest, "homepageUrl"));
        resolved.folder_name = resolved
            .folder_name
            .or_else(|| json_string(&manifest, "folderName"));
        if resolved.authors.is_empty() {
            resolved.authors = manifest["authors"]
                .as_array()
                .into_iter()
                .flatten()
                .filter_map(|author| author.as_str().map(str::to_string))
                .collect();
        }
    }
    if resolved.mod_id.is_none() {
        resolved.mod_id = resolved
            .name
            .as_deref()
            .map(workshop_slug)
            .filter(|value| !value.is_empty());
    }
    Ok(resolved)
}

fn read_source_manifest(source: &Path) -> Result<Option<serde_json::Value>> {
    let bytes = if source.is_dir() {
        let path = source.join("mod.json");
        match fs::read(&path) {
            Ok(bytes) => Some(bytes),
            Err(error) if error.kind() == std::io::ErrorKind::NotFound => None,
            Err(error) => return Err(error).with_context(|| format!("reading {}", path.display())),
        }
    } else if source.is_file() {
        Pbo::read_path(source)
            .with_context(|| format!("'{}' is not a valid PBO", source.display()))?
            .read("mod.json")
            .transpose()?
    } else {
        None
    };
    bytes
        .map(|bytes| {
            serde_json::from_slice(&bytes)
                .with_context(|| format!("parsing mod.json in {}", source.display()))
        })
        .transpose()
}

fn json_string(value: &serde_json::Value, key: &str) -> Option<String> {
    value[key]
        .as_str()
        .map(str::trim)
        .filter(|value| !value.is_empty())
        .map(str::to_string)
}

fn workshop_slug(name: &str) -> String {
    use unicode_normalization::UnicodeNormalization;
    let mut out = String::new();
    let mut pending_dash = false;
    for ch in name.nfkd() {
        if ch.is_ascii_alphanumeric() {
            if pending_dash && !out.is_empty() {
                out.push('-');
            }
            pending_dash = false;
            out.push(ch.to_ascii_lowercase());
        } else if !('\u{0300}'..='\u{036f}').contains(&ch) {
            pending_dash = true;
        }
    }
    out
}

/// A streaming reader for the raw PBO. A folder is packed into memory (addon folders are small);
/// an already-packed `.pbo` is validated then streamed from disk so it is never held in memory.
fn build_artifact_reader(args: &PublishArgs) -> Result<Box<dyn Read>> {
    let source = Path::new(&args.source);
    if source.is_dir() {
        let pbo = Pbo::pack_dir(&args.source, args.prefix.as_deref())
            .with_context(|| format!("packing {}", args.source))?;
        let mut bytes = Vec::new();
        pbo.write(&mut bytes).context("serialising PBO")?;
        Ok(Box::new(Cursor::new(bytes)))
    } else if source.is_file() {
        if args.prefix.is_some() {
            bail!(
                "--prefix only applies when packing a folder; '{}' is already a packed PBO",
                args.source
            );
        }
        Pbo::read_path(&args.source)
            .with_context(|| format!("'{}' is not a valid PBO", args.source))?;
        let file = fs::File::open(source).with_context(|| format!("opening {}", args.source))?;
        Ok(Box::new(file))
    } else {
        bail!("source '{}' is neither a folder nor a file", args.source);
    }
}

/// zstd level for published mod artifacts. 19 is the ratio/decompress sweet spot for OFP mods —
/// big audio/redundant mods shrink ~3x, while clients decompress at 400+ MB/s regardless.
const MOD_ZSTD_LEVEL: i32 = 19;

/// Stream-compress `raw` into a temporary `.pbo.zst` file and return it with its compressed size.
/// Streamed so a multi-GB mod never lands fully in memory; the temp file is removed when dropped.
fn compress_artifact(mut raw: Box<dyn Read>) -> Result<(NamedTempFile, u64)> {
    let tmp = NamedTempFile::new().context("creating temp compressed artifact")?;
    {
        let mut encoder = zstd::stream::write::Encoder::new(tmp.as_file(), MOD_ZSTD_LEVEL)
            .context("starting zstd encoder")?;
        // Spread compression across cores — at level 19 a multi-GB mod is otherwise CPU-bound on
        // one thread for tens of minutes. Falls back to single-threaded if unsupported.
        let workers = std::thread::available_parallelism().map_or(1, std::num::NonZeroUsize::get);
        let _ = encoder.multithread(u32::try_from(workers).unwrap_or(u32::MAX));
        std::io::copy(&mut raw, &mut encoder).context("compressing artifact")?;
        encoder.finish().context("finishing zstd stream")?;
    }
    let len = tmp
        .as_file()
        .metadata()
        .context("sizing compressed artifact")?
        .len();
    Ok((tmp, len))
}

/// Build a ureq agent. By default native-tls validates against the OS trust store
/// (so system-installed internal CAs work); `insecure` skips verification (curl -k).
fn http_agent(insecure: bool) -> Result<ureq::Agent> {
    let mut builder = native_tls::TlsConnector::builder();
    if insecure {
        builder.danger_accept_invalid_certs(true);
        builder.danger_accept_invalid_hostnames(true);
    }
    let connector = builder.build().context("building TLS connector")?;
    Ok(ureq::builder().tls_connector(Arc::new(connector)).build())
}

fn run_list(master: &str, insecure: bool) -> Result<()> {
    let base = master.trim_end_matches('/');
    let url = format!("{base}/v1/mods");
    match http_agent(insecure)?.get(&url).call() {
        Ok(ok) => {
            let mods: Vec<serde_json::Value> =
                ok.into_json().context("parsing mod list response")?;
            if mods.is_empty() {
                println!("no mods on {base}");
                return Ok(());
            }
            println!("{} mod(s) on {base}:", mods.len());
            for entry in &mods {
                let mod_id = entry["modId"].as_str().unwrap_or("?");
                let name = entry["name"].as_str().unwrap_or("?");
                let version = entry["version"].as_str().unwrap_or("?");
                println!("  {mod_id}  {name}  (v{version})");
            }
            Ok(())
        }
        Err(ureq::Error::Status(code, response)) => {
            let detail = response.into_string().unwrap_or_default();
            bail!("list failed: HTTP {code}: {}", detail.trim());
        }
        Err(error) => bail!("list request failed: {error}"),
    }
}

fn entry_id(e: &serde_json::Value) -> &str {
    e["modId"].as_str().unwrap_or("")
}
fn entry_folder(e: &serde_json::Value) -> &str {
    e["folderName"].as_str().unwrap_or("")
}
fn entry_label(e: &serde_json::Value) -> String {
    let f = entry_folder(e);
    if f.is_empty() {
        entry_id(e).to_string()
    } else {
        f.to_string()
    }
}
fn ambiguous(token: &str, hits: &[&serde_json::Value]) -> anyhow::Error {
    anyhow::anyhow!(
        "'{token}' is ambiguous — matches: {}",
        hits.iter()
            .map(|e| entry_label(e))
            .collect::<Vec<_>>()
            .join(", ")
    )
}

/// Resolve a user token (modId, folderName, or a unique case-insensitive prefix of either)
/// to its catalog entry. Errors clearly when nothing or more than one thing matches.
fn resolve_token<'a>(
    catalog: &'a [serde_json::Value],
    token: &str,
) -> Result<&'a serde_json::Value> {
    // 1. exact modId, then 2. exact folderName (case-sensitive — folders carry meaningful case).
    if let Some(e) = catalog.iter().find(|e| entry_id(e) == token) {
        return Ok(e);
    }
    if let Some(e) = catalog.iter().find(|e| entry_folder(e) == token) {
        return Ok(e);
    }
    // 3. case-insensitive exact on folderName or modId.
    let ci: Vec<&serde_json::Value> = catalog
        .iter()
        .filter(|e| {
            entry_id(e).eq_ignore_ascii_case(token) || entry_folder(e).eq_ignore_ascii_case(token)
        })
        .collect();
    if ci.len() == 1 {
        return Ok(ci[0]);
    }
    if ci.len() > 1 {
        return Err(ambiguous(token, &ci));
    }
    // 4. unique case-insensitive prefix on folderName or modId.
    let lc = token.to_lowercase();
    let pf: Vec<&serde_json::Value> = catalog
        .iter()
        .filter(|e| {
            entry_id(e).to_lowercase().starts_with(&lc)
                || entry_folder(e).to_lowercase().starts_with(&lc)
        })
        .collect();
    match pf.len() {
        1 => Ok(pf[0]),
        0 => bail!("no mod matches '{token}' (by modId or folder name)"),
        _ => Err(ambiguous(token, &pf)),
    }
}

/// Stream-decode a zstd-wrapped PBO from `reader` into the temp file `tmp`, then unpack it into a
/// fresh `dest` directory. Returns the number of entries unpacked. The decode is streamed so a
/// multi-GB mod never lands fully in memory; `tmp` is removed on success.
fn decode_and_unpack(reader: impl Read, tmp: &Path, dest: &Path) -> Result<usize> {
    {
        let mut decoder =
            zstd::stream::read::Decoder::new(reader).context("opening zstd stream")?;
        let mut file =
            fs::File::create(tmp).with_context(|| format!("creating {}", tmp.display()))?;
        std::io::copy(&mut decoder, &mut file).context("decoding artifact")?;
    }
    let pbo = Pbo::read_path(tmp).context("reading decoded pbo")?;
    // Fresh dir so a re-install doesn't leave stale files behind.
    if dest.exists() {
        fs::remove_dir_all(dest).with_context(|| format!("clearing {}", dest.display()))?;
    }
    pbo.unpack_to_dir(dest)
        .with_context(|| format!("unpacking into {}", dest.display()))?;
    let _ = fs::remove_file(tmp);
    Ok(pbo.entries.len())
}

fn write_installed_mod_manifest(entry: &serde_json::Value, dest: &Path) -> Result<()> {
    let manifest =
        serde_json::to_vec_pretty(entry).context("serializing installed mod manifest")?;
    fs::write(dest.join("mod.json"), manifest)
        .with_context(|| format!("writing installed mod manifest to {}", dest.display()))
}

fn package_revision(entry: &serde_json::Value) -> u64 {
    entry["packageRevision"].as_u64().unwrap_or(1)
}

fn install_is_current(remote: &serde_json::Value, installed: &serde_json::Value) -> bool {
    if package_revision(remote) != package_revision(installed) {
        return false;
    }
    match (remote["sha256"].as_str(), installed["sha256"].as_str()) {
        (Some(remote), Some(installed)) => remote.eq_ignore_ascii_case(installed),
        _ => true,
    }
}

fn replace_install(stage: &Path, dest: &Path) -> Result<()> {
    let parent = dest
        .parent()
        .ok_or_else(|| anyhow::anyhow!("install destination has no parent"))?;
    let folder = dest
        .file_name()
        .and_then(|name| name.to_str())
        .ok_or_else(|| anyhow::anyhow!("install destination has no folder name"))?;
    let backup = parent.join(format!(".{folder}.papa-backup-{}", std::process::id()));
    if backup.exists() {
        bail!("backup path {} already exists", backup.display());
    }

    let had_previous = dest.exists();
    if had_previous {
        fs::rename(dest, &backup)
            .with_context(|| format!("moving previous install {} to backup", dest.display()))?;
    }
    if let Err(error) = fs::rename(stage, dest) {
        if had_previous {
            let _ = fs::rename(&backup, dest);
        }
        return Err(error)
            .with_context(|| format!("installing staged files to {}", dest.display()));
    }
    if had_previous {
        fs::remove_dir_all(&backup)
            .with_context(|| format!("removing install backup {}", backup.display()))?;
    }
    Ok(())
}

fn immutable_download_url(base: &str, entry: &serde_json::Value) -> String {
    match entry["downloadUrl"].as_str() {
        Some(url) if url.starts_with("http://") || url.starts_with("https://") => url.to_string(),
        Some(url) if url.starts_with('/') => format!("{base}{url}"),
        _ => format!(
            "{base}/v1/mods/{}/revisions/{}/download",
            entry_id(entry),
            package_revision(entry)
        ),
    }
}

fn download_and_replace(
    agent: &ureq::Agent,
    url: &str,
    entry: &serde_json::Value,
    dest: &Path,
    mods_dir: &Path,
) -> Result<usize> {
    let response = agent.get(url).call().map_err(|error| match error {
        ureq::Error::Status(code, response) => anyhow::anyhow!(
            "download HTTP {code}: {}",
            response.into_string().unwrap_or_default().trim()
        ),
        error @ ureq::Error::Transport(_) => anyhow::anyhow!("download request failed: {error}"),
    })?;
    let mut compressed = NamedTempFile::new_in(mods_dir)
        .with_context(|| format!("creating download in {}", mods_dir.display()))?;
    let mut reader = response.into_reader();
    let mut hasher = Sha256::new();
    let mut size = 0u64;
    let mut buffer = vec![0u8; 64 * 1024];
    loop {
        let read = reader.read(&mut buffer).context("reading download")?;
        if read == 0 {
            break;
        }
        compressed
            .write_all(&buffer[..read])
            .context("writing staged download")?;
        hasher.update(&buffer[..read]);
        size += read as u64;
    }
    if let Some(expected) = entry["sizeBytes"].as_u64() {
        if expected != size {
            bail!("download size mismatch: expected {expected}, got {size}");
        }
    }
    if let Some(expected) = entry["sha256"].as_str() {
        let actual = format!("{:x}", hasher.finalize());
        if !actual.eq_ignore_ascii_case(expected) {
            bail!("download SHA-256 mismatch: expected {expected}, got {actual}");
        }
    }
    compressed.as_file_mut().seek(SeekFrom::Start(0))?;

    let stage = tempfile::Builder::new()
        .prefix(".papa-stage-")
        .tempdir_in(mods_dir)
        .with_context(|| format!("creating install stage in {}", mods_dir.display()))?;
    let decoded = mods_dir.join(format!(".{}.pbo.decoded", entry_id(entry)));
    let count = decode_and_unpack(compressed.reopen()?, &decoded, stage.path())?;
    write_installed_mod_manifest(entry, stage.path())?;
    let stage = stage.keep();
    if let Err(error) = replace_install(&stage, dest) {
        let _ = fs::remove_dir_all(&stage);
        return Err(error);
    }
    Ok(count)
}

/// Download + unpack one or more mods into `<dir>/<folderName>` (server-side install).
/// Accepts modIds, folder names, or unique prefixes, separated by spaces and/or `;`
/// (so `papa install "CSLA;@DVDcrcti"` works). The install folder is the catalog's
/// `folderName` (verbatim, incl. `@`/case), or `@<modId>` when the entry has none.
fn run_install(args: &InstallArgs, master: &str, insecure: bool) -> Result<()> {
    let base = master.trim_end_matches('/');
    let agent = http_agent(insecure)?;

    // Accept space- and/or ';'-separated tokens (mirrors the engine's --mod syntax).
    let tokens: Vec<String> = args
        .mod_ids
        .iter()
        .flat_map(|a| a.split(';'))
        .map(|s| s.trim().to_string())
        .filter(|s| !s.is_empty())
        .collect();
    if tokens.is_empty() {
        bail!("no mods given");
    }

    // Fetch the catalog once and resolve every token (modId | folderName | prefix) against it.
    let catalog: Vec<serde_json::Value> = match agent.get(&format!("{base}/v1/mods")).call() {
        Ok(ok) => ok.into_json().context("parsing mod catalog")?,
        Err(ureq::Error::Status(code, r)) => {
            bail!(
                "catalog HTTP {code}: {}",
                r.into_string().unwrap_or_default().trim()
            )
        }
        Err(error) => bail!("catalog request failed: {error}"),
    };
    let mut mods: Vec<(String, String, serde_json::Value)> = Vec::new(); // (modId, folderName, catalog entry)
    for token in &tokens {
        let entry = resolve_token(&catalog, token)?;
        let mod_id = entry["modId"].as_str().unwrap_or_default().to_string();
        let folder = entry["folderName"]
            .as_str()
            .filter(|s| !s.trim().is_empty())
            .map_or_else(|| format!("@{mod_id}"), str::to_string);
        if !mods.iter().any(|(m, _, _)| m == &mod_id) {
            mods.push((mod_id, folder, entry.clone()));
        }
    }

    fs::create_dir_all(&args.dir).with_context(|| format!("creating mods dir {}", args.dir))?;
    let mut installed_ids: Vec<String> = Vec::new();
    for (mod_id, folder, entry) in &mods {
        if folder.is_empty()
            || folder.contains('/')
            || folder.contains('\\')
            || folder == "."
            || folder == ".."
        {
            bail!("catalog returned unsafe folder name '{folder}' for {mod_id}");
        }
        let dest = Path::new(&args.dir).join(folder);
        let installed = fs::read(dest.join("mod.json"))
            .ok()
            .and_then(|bytes| serde_json::from_slice::<serde_json::Value>(&bytes).ok());
        if let Some(installed) = installed.as_ref() {
            let local_revision = package_revision(installed);
            let remote_revision = package_revision(entry);
            if local_revision > remote_revision {
                println!(
                    "keeping {mod_id} package revision {local_revision}; catalog latest is {remote_revision}"
                );
                installed_ids.push(mod_id.clone());
                continue;
            }
            if install_is_current(entry, installed) {
                println!("{mod_id} package revision {remote_revision} is already installed");
                installed_ids.push(mod_id.clone());
                continue;
            }
        }

        let dl_url = immutable_download_url(base, entry);
        println!("installing {mod_id} -> {}", dest.display());
        let count = download_and_replace(&agent, &dl_url, entry, &dest, Path::new(&args.dir))
            .with_context(|| format!("installing {mod_id}"))?;
        println!("  done: {} ({count} entries)", dest.display());
        installed_ids.push(mod_id.clone());
    }
    println!(
        "installed {} mod(s) into {}.\nLaunch: --mods-dir {} --mod \"{}\"",
        installed_ids.len(),
        args.dir,
        args.dir,
        installed_ids.join(";")
    );
    Ok(())
}

/// Append `:DEFAULT_GAME_PORT` when the address carries no port. A bare IPv6 literal
/// (already containing `:`) is passed through unchanged — wrap it in `[...]:port` to add one.
fn with_default_port(address: &str) -> String {
    if address.contains(':') {
        address.to_string()
    } else {
        format!("{address}:{DEFAULT_GAME_PORT}")
    }
}

/// The enabled aid flags from a directory `difficulty` bitmask.
fn difficulty_flag_names(mask: i64) -> Vec<&'static str> {
    DIFFICULTY_FLAGS
        .iter()
        .enumerate()
        .filter(|(i, _)| mask & (1 << i) != 0)
        .map(|(_, name)| *name)
        .collect()
}

/// Probe one server directly over the UDP session-enum protocol and print its status.
fn run_query(args: &QueryArgs) -> Result<()> {
    let target = with_default_port(&args.address);
    let status = query_server(&target, Duration::from_millis(args.timeout_ms))
        .with_context(|| format!("probing {target}"))?;
    match status {
        Some(status) => {
            print_query_status(&target, &status);
            Ok(())
        }
        None => bail!("no response from {target} within {} ms", args.timeout_ms),
    }
}

fn print_query_status(target: &str, status: &ServerStatus) {
    let s: &SessionResponse = &status.session;
    println!("{target}  ({} ms)", status.ping_ms);
    println!("  name:     {}", s.name);
    println!("  mission:  {}", s.mission);
    println!("  players:  {}/{}", s.num_players, s.max_players);
    println!("  state:    {}", s.game_state);
    println!("  password: {}", yes_no(s.password));
    println!(
        "  version:  actual {}{} / required {}",
        s.actual_version,
        format_version_tag(s.version_tag.as_deref()),
        s.required_version
    );
    if let Some(mods) = &s.mod_list {
        println!("  mod:      {mods}");
        println!("  equalMod: {}", yes_no(s.equal_mod_required));
    }
}

fn format_version_tag(tag: Option<&str>) -> String {
    tag.map(str::trim)
        .filter(|tag| !tag.is_empty())
        .map_or_else(String::new, |tag| format!(" {tag}"))
}

const fn yes_no(value: bool) -> &'static str {
    if value {
        "yes"
    } else {
        "no"
    }
}

/// List servers from the master directory, applying the optional filters.
fn run_servers(args: &ServersArgs, master: &str, insecure: bool) -> Result<()> {
    let base = master.trim_end_matches('/');
    let url = format!("{base}/v1/servers");
    let mut req = http_agent(insecure)?.get(&url);
    if let Some(value) = &args.hostname {
        req = req.query("hostname", value);
    }
    if let Some(value) = &args.gametype {
        req = req.query("gametype", value);
    }
    if let Some(value) = args.min_players {
        req = req.query("minplayers", &value.to_string());
    }
    if let Some(value) = args.max_players {
        req = req.query("maxplayers", &value.to_string());
    }
    if args.include_full {
        req = req.query("includeFullServers", "true");
    }
    if args.include_passworded {
        req = req.query("includePasswordedServers", "true");
    }
    if args.include_unverified {
        req = req.query("includeUnverifiedServers", "true");
    }
    if let Some(value) = args.limit {
        req = req.query("limit", &value.to_string());
    }

    match req.call() {
        Ok(ok) => {
            let servers: Vec<serde_json::Value> =
                ok.into_json().context("parsing server list response")?;
            if servers.is_empty() {
                println!("no servers on {base}");
                return Ok(());
            }
            println!("{} server(s) on {base}:", servers.len());
            for s in &servers {
                let id = s["serverId"].as_str().unwrap_or("?");
                let name = s["hostname"].as_str().unwrap_or("?");
                let mission = s["gametype"].as_str().unwrap_or("");
                let map_name = s["mapname"].as_str().unwrap_or("");
                let players = s["numplayers"].as_i64().unwrap_or(0);
                let max_players = s["maxplayers"].as_i64().unwrap_or(0);
                let pw = if s["password"].as_bool().unwrap_or(false) {
                    " [pw]"
                } else {
                    ""
                };
                let map_tag = if map_name.is_empty() {
                    String::new()
                } else {
                    format!(" [{map_name}]")
                };
                println!("  {id}  {name}  {players}/{max_players}  {mission}{map_tag}{pw}");
            }
            Ok(())
        }
        Err(ureq::Error::Status(code, response)) => {
            bail!(
                "servers HTTP {code}: {}",
                response.into_string().unwrap_or_default().trim()
            )
        }
        Err(error) => bail!("servers request failed: {error}"),
    }
}

/// Show one server's full record (incl. difficulty + session config) plus its players,
/// mods and recent sessions, from the master detail endpoint.
fn run_server(args: &ServerArgs, master: &str, insecure: bool) -> Result<()> {
    let base = master.trim_end_matches('/');
    let url = format!("{base}/v1/servers/{}", args.server_id);
    match http_agent(insecure)?.get(&url).call() {
        Ok(ok) => {
            let detail: serde_json::Value = ok.into_json().context("parsing server detail")?;
            print_server_detail(&detail);
            Ok(())
        }
        Err(ureq::Error::Status(404, _)) => bail!("no such server: {}", args.server_id),
        Err(ureq::Error::Status(code, response)) => {
            bail!(
                "server HTTP {code}: {}",
                response.into_string().unwrap_or_default().trim()
            )
        }
        Err(error) => bail!("server request failed: {error}"),
    }
}

fn print_server_detail(detail: &serde_json::Value) {
    let s = &detail["server"];
    let text = |key: &str| s[key].as_str().unwrap_or("");
    let int = |key: &str| s[key].as_i64().unwrap_or(0);

    println!("{}", text("hostname"));
    println!("  id:        {}", text("serverId"));
    println!("  mission:   {}", text("gametype"));
    println!("  map:       {}", text("mapname"));
    if !text("description").is_empty() {
        println!("  desc:      {}", text("description"));
    }
    println!("  players:   {}/{}", int("numplayers"), int("maxplayers"));
    println!(
        "  state:     {} (in state {}s, {} min left)",
        int("state"),
        int("stateElapsedSeconds"),
        int("timeleft")
    );
    let mode = if s["cadet"].as_bool().unwrap_or(false) {
        "Cadet"
    } else {
        "Veteran"
    };
    let aids = difficulty_flag_names(int("difficulty"));
    let aids = if aids.is_empty() {
        "none".to_string()
    } else {
        aids.join(", ")
    };
    println!("  mode:      {mode} (aids: {aids})");
    println!("  flags:     {}", session_flags(s));
    for key in ["param1", "param2"] {
        let value = text(key);
        if !value.is_empty() {
            println!("  param:     {value}");
        }
    }
    println!(
        "  versions:  actual {}{} / required {}",
        int("actver"),
        format_version_tag(Some(text("vertag"))).as_str(),
        int("reqver")
    );
    println!("  transport: {} / {}", text("platform"), text("impl"));
    println!(
        "  password:  {}",
        yes_no(s["password"].as_bool().unwrap_or(false))
    );
    let reachable = match s["observedReachable"].as_bool() {
        Some(true) => " (reachable)",
        Some(false) => " (unreachable)",
        None => "",
    };
    let verification = text("verificationState");
    println!(
        "  verified:  {}{reachable}",
        if verification.is_empty() {
            "?"
        } else {
            verification
        }
    );
    println!("  mod:       {}", text("mod"));
    if !text("requiredAddons").is_empty() {
        println!("  addons:    {}", text("requiredAddons"));
    }

    print_server_mods(detail);
    print_server_players(detail);
    print_server_sessions(detail);
}

fn print_server_mods(detail: &serde_json::Value) {
    let Some(mods) = detail["mods"].as_array() else {
        return;
    };
    if mods.is_empty() {
        return;
    }
    println!("  mods ({}):", mods.len());
    for m in mods {
        let label = m["name"]
            .as_str()
            .or_else(|| m["modId"].as_str())
            .unwrap_or("?");
        let version = m["version"]
            .as_str()
            .map_or_else(String::new, |v| format!(" v{v}"));
        let known = if m["known"].as_bool().unwrap_or(false) {
            ""
        } else {
            " (unknown)"
        };
        println!("    {label}{version}{known}");
    }
}

fn print_server_players(detail: &serde_json::Value) {
    let Some(players) = detail["players"].as_array() else {
        return;
    };
    if players.is_empty() {
        return;
    }
    println!("  players ({}):", players.len());
    for p in players {
        let role = p["role"].as_str().unwrap_or("");
        let role = if role.is_empty() {
            String::new()
        } else {
            format!(" [{role}]")
        };
        println!("    {}{role}", p["name"].as_str().unwrap_or("?"));
    }
}

fn print_server_sessions(detail: &serde_json::Value) {
    let Some(sessions) = detail["recentSessions"].as_array() else {
        return;
    };
    if sessions.is_empty() {
        return;
    }
    println!("  recent sessions ({}):", sessions.len());
    for r in sessions {
        println!(
            "    {}  {} min, peak {}",
            r["mission"].as_str().unwrap_or("?"),
            r["playedMinutes"].as_i64().unwrap_or(0),
            r["peakPlayers"].as_i64().unwrap_or(0)
        );
    }
}

/// Compact one-line summary of the boolean / respawn session flags on a directory record.
fn session_flags(s: &serde_json::Value) -> String {
    let mut parts: Vec<String> = Vec::new();
    let flag = |key: &str| s[key].as_bool().unwrap_or(false);
    if flag("jip") {
        parts.push("JIP".to_string());
    }
    if flag("locked") {
        parts.push("locked".to_string());
    }
    if flag("dedicated") {
        parts.push("dedicated".to_string());
    }
    if flag("disabledAI") {
        parts.push("AI off".to_string());
    }
    if flag("equalModRequired") {
        parts.push("equalMod".to_string());
    }
    let respawn = s["respawn"].as_i64().unwrap_or(0);
    if respawn > 0 {
        let name = usize::try_from(respawn)
            .ok()
            .and_then(|i| RESPAWN_NAMES.get(i))
            .copied()
            .unwrap_or("?");
        let delay = s["respawnDelay"].as_i64().unwrap_or(0);
        if delay > 0 {
            parts.push(format!("respawn {name} {delay}s"));
        } else {
            parts.push(format!("respawn {name}"));
        }
    }
    if parts.is_empty() {
        "none".to_string()
    } else {
        parts.join(" · ")
    }
}

/// The multipart body up to (but excluding) the file bytes: the metadata fields plus the
/// `file` part header. The caller streams the artifact bytes after this, then the trailer.
fn build_multipart_prelude(boundary: &str, args: &PublishArgs) -> Vec<u8> {
    let mut fields: Vec<(&str, String)> = Vec::new();
    if let Some(mod_id) = &args.mod_id {
        fields.push(("modId", mod_id.clone()));
    }
    if let Some(name) = &args.name {
        fields.push(("name", name.clone()));
    }
    if let Some(app) = &args.app {
        fields.push(("app", app.clone()));
    }
    if let Some(actver) = args.actver {
        fields.push(("actver", actver.to_string()));
    }
    for actver in &args.compatible_actvers {
        fields.push(("compatibleActver", actver.to_string()));
    }
    if let Some(version_tag) = &args.version_tag {
        fields.push(("vertag", version_tag.clone()));
    }
    if let Some(version) = &args.version {
        fields.push(("version", version.clone()));
    }
    if let Some(folder_name) = &args.folder_name {
        fields.push(("folderName", folder_name.clone()));
    }
    if let Some(description) = &args.description {
        fields.push(("description", description.clone()));
    }
    if let Some(homepage) = &args.homepage_url {
        fields.push(("homepageUrl", homepage.clone()));
    }
    for author in &args.authors {
        fields.push(("author", author.clone()));
    }

    let mut body = Vec::new();
    for (name, value) in fields {
        body.extend_from_slice(format!("--{boundary}\r\n").as_bytes());
        body.extend_from_slice(
            format!("Content-Disposition: form-data; name=\"{name}\"\r\n\r\n").as_bytes(),
        );
        body.extend_from_slice(value.as_bytes());
        body.extend_from_slice(b"\r\n");
    }
    body.extend_from_slice(format!("--{boundary}\r\n").as_bytes());
    body.extend_from_slice(
        b"Content-Disposition: form-data; name=\"file\"; filename=\"mod.pbo\"\r\n\
          Content-Type: application/octet-stream\r\n\r\n",
    );
    body
}

#[cfg(test)]
mod tests {
    use super::{
        build_multipart_prelude, compress_artifact, decode_and_unpack, difficulty_flag_names,
        install_is_current, publish_args_with_manifest, replace_install, upload_result_name,
        with_default_port, workshop_slug, write_installed_mod_manifest, Cli, Command, PublishArgs,
    };
    use clap::Parser;
    use papa_bear_archive::Pbo;
    use std::io::Cursor;
    use tempfile::tempdir;

    #[test]
    fn with_default_port_only_appends_when_absent() {
        assert_eq!(with_default_port("1.2.3.4"), "1.2.3.4:2302");
        assert_eq!(with_default_port("1.2.3.4:2402"), "1.2.3.4:2402");
        assert_eq!(with_default_port("host.example"), "host.example:2302");
    }

    #[test]
    fn difficulty_flag_names_decodes_bits_in_enum_order() {
        // bit 0 = Armor, bit 6 = WeaponCursor, bit 11 = UltraAI.
        assert_eq!(difficulty_flag_names(0), Vec::<&str>::new());
        assert_eq!(difficulty_flag_names(1 << 0), vec!["Armor"]);
        assert_eq!(
            difficulty_flag_names((1 << 0) | (1 << 6) | (1 << 11)),
            vec!["Armor", "WeaponCursor", "UltraAI"]
        );
        // Bits beyond the 12 known flags are ignored, not panicking.
        assert_eq!(difficulty_flag_names(1 << 20), Vec::<&str>::new());
    }

    // publish-side compress -> install-side decode, within the CLI: a packed PBO compressed by
    // compress_artifact is a valid zstd frame that decode_and_unpack recovers byte-identically.
    // Broken-state delta: if compress_artifact forgot encoder.finish(), the frame would be
    // truncated and decode_and_unpack would error.
    #[test]
    fn publish_compress_round_trips_through_install_decode() {
        let src = tempdir().unwrap();
        std::fs::write(src.path().join("config.cpp"), b"class CfgPatches {};").unwrap();
        std::fs::create_dir(src.path().join("data")).unwrap();
        std::fs::write(src.path().join("data").join("blob.bin"), vec![3u8; 300_000]).unwrap();
        let pbo = Pbo::pack_dir(src.path(), None).unwrap();
        let mut raw = Vec::new();
        pbo.write(&mut raw).unwrap();

        let (tmp, compressed_len) = compress_artifact(Box::new(Cursor::new(raw.clone()))).unwrap();
        assert!(compressed_len > 0 && compressed_len < raw.len() as u64);
        let compressed = std::fs::read(tmp.path()).unwrap();
        assert_eq!(compressed.len() as u64, compressed_len);

        let work = tempdir().unwrap();
        let dl = work.path().join(".m.pbo.download");
        let dest = work.path().join("@m");
        let count = decode_and_unpack(Cursor::new(compressed), &dl, &dest).unwrap();
        assert_eq!(count, pbo.entries.len());
        assert_eq!(
            std::fs::read(dest.join("config.cpp")).unwrap(),
            b"class CfgPatches {};"
        );
        assert_eq!(
            std::fs::read(dest.join("data").join("blob.bin")).unwrap(),
            vec![3u8; 300_000]
        );
    }

    // Mirror of the server's stored format (zstd-wrapped PBO) decoded by the client's exact
    // install path. Broken-state delta: if decode_and_unpack fed the raw download to Pbo instead
    // of decoding it, Pbo::read_path would fail on the zstd magic and the unpack would error.
    #[test]
    fn decode_and_unpack_recovers_a_zstd_wrapped_pbo() {
        // Build a real PBO from a source tree.
        let src = tempdir().unwrap();
        std::fs::write(src.path().join("config.cpp"), b"class CfgPatches {};").unwrap();
        std::fs::create_dir(src.path().join("data")).unwrap();
        std::fs::write(src.path().join("data").join("blob.bin"), vec![7u8; 200_000]).unwrap();
        let pbo = Pbo::pack_dir(src.path(), None).unwrap();

        // Serialize + zstd-compress it exactly like the service stores it.
        let mut raw = Vec::new();
        pbo.write(&mut raw).unwrap();
        let compressed = zstd::encode_all(raw.as_slice(), 19).unwrap();
        assert!(compressed.len() < raw.len());

        // Decode + unpack from an in-memory "download".
        let work = tempdir().unwrap();
        let tmp = work.path().join(".mod.pbo.download");
        let dest = work.path().join("@mod");
        let count = decode_and_unpack(Cursor::new(compressed), &tmp, &dest).unwrap();

        assert_eq!(count, pbo.entries.len());
        assert!(!tmp.exists(), "temp download was not cleaned up");
        assert_eq!(
            std::fs::read(dest.join("config.cpp")).unwrap(),
            b"class CfgPatches {};"
        );
        assert_eq!(
            std::fs::read(dest.join("data").join("blob.bin")).unwrap(),
            vec![7u8; 200_000]
        );
    }

    #[test]
    fn decode_and_unpack_rejects_non_zstd_input() {
        let work = tempdir().unwrap();
        let tmp = work.path().join(".x.download");
        let dest = work.path().join("@x");
        // Raw (uncompressed) bytes must fail at the zstd decode, not silently unpack.
        let err =
            decode_and_unpack(Cursor::new(b"not a zstd frame".to_vec()), &tmp, &dest).unwrap_err();
        assert!(!format!("{err:#}").is_empty());
    }

    #[test]
    fn installed_mod_manifest_preserves_catalog_identity_for_engine_resolution() {
        let work = tempdir().unwrap();
        let dest = work.path().join("carwars2.5");
        std::fs::create_dir(&dest).unwrap();
        let entry = serde_json::json!({
            "modId": "carwars-2.5",
            "name": "CarWars",
            "version": "2.5",
            "folderName": "carwars2.5"
        });

        write_installed_mod_manifest(&entry, &dest).unwrap();

        let manifest = std::fs::read_to_string(dest.join("mod.json")).unwrap();
        let saved: serde_json::Value = serde_json::from_str(&manifest).unwrap();
        assert_eq!(saved["modId"], "carwars-2.5");
        assert_eq!(saved["folderName"], "carwars2.5");
    }

    #[test]
    fn publish_multipart_omits_unspecified_metadata() {
        let args = PublishArgs {
            source: "fixture.pbo".to_string(),
            admin_key: "key".to_string(),
            mod_id: None,
            name: None,
            app: None,
            actver: None,
            compatible_actvers: Vec::new(),
            version_tag: None,
            version: None,
            description: None,
            authors: Vec::new(),
            homepage_url: None,
            folder_name: None,
            prefix: None,
        };
        let body = String::from_utf8(build_multipart_prelude("boundary", &args)).unwrap();
        assert!(!body.contains("name=\"name\""));
        assert!(!body.contains("name=\"app\""));
        assert!(!body.contains("name=\"actver\""));
        assert!(!body.contains("name=\"vertag\""));
        assert!(body.contains("name=\"file\""));
    }

    #[test]
    fn publish_multipart_includes_game_compatibility() {
        let args = PublishArgs {
            source: "fixture.pbo".to_string(),
            admin_key: "key".to_string(),
            mod_id: Some("fixture".to_string()),
            name: Some("Fixture".to_string()),
            app: Some("CWR".to_string()),
            actver: Some(305),
            compatible_actvers: vec![303, 305],
            version_tag: Some("release".to_string()),
            version: Some("1.0".to_string()),
            description: None,
            authors: Vec::new(),
            homepage_url: None,
            folder_name: None,
            prefix: None,
        };

        let body = String::from_utf8(build_multipart_prelude("boundary", &args)).unwrap();
        assert!(body.contains("name=\"app\"\r\n\r\nCWR"));
        assert!(body.contains("name=\"modId\"\r\n\r\nfixture"));
        assert!(body.contains("name=\"actver\"\r\n\r\n305"));
        assert!(body.contains("name=\"compatibleActver\"\r\n\r\n303"));
        assert!(body.contains("name=\"compatibleActver\"\r\n\r\n305"));
        assert!(body.contains("name=\"vertag\"\r\n\r\nrelease"));
    }

    #[test]
    fn publish_accepts_identity_and_game_compatibility_flags() {
        let publish = Cli::try_parse_from([
            "papa",
            "publish",
            "--admin-key",
            "key",
            "--name",
            "Fixture",
            "--mod-id",
            "fixture",
            "--app",
            "CWR",
            "--actver",
            "305",
            "--vertag",
            "release",
            "fixture.pbo",
        ])
        .unwrap();
        let Command::Publish(publish) = publish.command else {
            panic!("expected publish command");
        };
        assert_eq!(publish.app.as_deref(), Some("CWR"));
        assert_eq!(publish.actver, Some(305));
        assert_eq!(publish.version_tag.as_deref(), Some("release"));
        assert_eq!(publish.mod_id.as_deref(), Some("fixture"));
    }

    #[test]
    fn source_manifest_supplies_publish_identity_and_metadata() {
        let source = tempdir().unwrap();
        std::fs::write(
            source.path().join("mod.json"),
            r#"{
                "modId":"csla-2.2",
                "name":"CSLA",
                "app":"CWR",
                "actver":305,
                "compatibleActvers":[303,305],
                "vertag":"release",
                "version":"2.2",
                "folderName":"CSLA",
                "description":"fixture",
                "authors":["Author One","Author Two"],
                "homepageUrl":"https://example.invalid"
            }"#,
        )
        .unwrap();
        let args = PublishArgs {
            source: source.path().to_string_lossy().into_owned(),
            admin_key: "key".to_string(),
            mod_id: None,
            name: None,
            app: None,
            actver: None,
            compatible_actvers: Vec::new(),
            version_tag: None,
            version: None,
            description: None,
            authors: Vec::new(),
            homepage_url: None,
            folder_name: None,
            prefix: None,
        };

        let resolved = publish_args_with_manifest(&args).unwrap();
        assert_eq!(resolved.mod_id.as_deref(), Some("csla-2.2"));
        assert_eq!(resolved.name.as_deref(), Some("CSLA"));
        assert_eq!(resolved.app.as_deref(), Some("CWR"));
        assert_eq!(resolved.actver, Some(305));
        assert_eq!(resolved.compatible_actvers, [303, 305]);
        assert_eq!(resolved.version_tag.as_deref(), Some("release"));
        assert_eq!(resolved.version.as_deref(), Some("2.2"));
        assert_eq!(resolved.folder_name.as_deref(), Some("CSLA"));
        assert_eq!(resolved.authors, ["Author One", "Author Two"]);
    }

    #[test]
    fn packed_source_manifest_supplies_publish_identity() {
        let work = tempdir().unwrap();
        let source = work.path().join("source");
        std::fs::create_dir(&source).unwrap();
        std::fs::write(
            source.join("mod.json"),
            r#"{"modId":"packed-fixture","name":"Packed Fixture"}"#,
        )
        .unwrap();
        std::fs::write(source.join("config.cpp"), b"class CfgPatches {};").unwrap();
        let packed = work.path().join("fixture.pbo");
        Pbo::pack_dir(&source, None)
            .unwrap()
            .write_path(&packed)
            .unwrap();
        let args = PublishArgs {
            source: packed.to_string_lossy().into_owned(),
            admin_key: "key".to_string(),
            mod_id: None,
            name: None,
            app: None,
            actver: None,
            compatible_actvers: Vec::new(),
            version_tag: None,
            version: None,
            description: None,
            authors: Vec::new(),
            homepage_url: None,
            folder_name: None,
            prefix: None,
        };

        let resolved = publish_args_with_manifest(&args).unwrap();
        assert_eq!(resolved.mod_id.as_deref(), Some("packed-fixture"));
        assert_eq!(resolved.name.as_deref(), Some("Packed Fixture"));
    }

    #[test]
    fn command_line_metadata_overrides_source_manifest() {
        let source = tempdir().unwrap();
        std::fs::write(
            source.path().join("mod.json"),
            r#"{"modId":"old-id","name":"Old","actver":303}"#,
        )
        .unwrap();
        let args = PublishArgs {
            source: source.path().to_string_lossy().into_owned(),
            admin_key: "key".to_string(),
            mod_id: Some("new-id".to_string()),
            name: Some("New".to_string()),
            app: Some("CWR".to_string()),
            actver: Some(305),
            compatible_actvers: Vec::new(),
            version_tag: None,
            version: None,
            description: None,
            authors: Vec::new(),
            homepage_url: None,
            folder_name: None,
            prefix: None,
        };

        let resolved = publish_args_with_manifest(&args).unwrap();
        assert_eq!(resolved.mod_id.as_deref(), Some("new-id"));
        assert_eq!(resolved.name.as_deref(), Some("New"));
        assert_eq!(resolved.actver, Some(305));
    }

    #[test]
    fn name_supplies_stable_identity_without_manifest() {
        let source = tempdir().unwrap();
        let args = PublishArgs {
            source: source.path().to_string_lossy().into_owned(),
            admin_key: "key".to_string(),
            mod_id: None,
            name: Some("FDF Mod".to_string()),
            app: None,
            actver: None,
            compatible_actvers: Vec::new(),
            version_tag: None,
            version: None,
            description: None,
            authors: Vec::new(),
            homepage_url: None,
            folder_name: None,
            prefix: None,
        };

        let resolved = publish_args_with_manifest(&args).unwrap();
        assert_eq!(resolved.mod_id.as_deref(), Some("fdf-mod"));
    }

    #[test]
    fn workshop_slug_matches_server_identity_rules() {
        assert_eq!(workshop_slug("FDF Mod"), "fdf-mod");
        assert_eq!(workshop_slug("\u{010c}SLA"), "csla");
    }

    #[test]
    fn compatibility_command_accepts_repeated_versions() {
        let cli = Cli::try_parse_from([
            "papa",
            "compatibility",
            "fixture",
            "1",
            "--actver",
            "303",
            "--actver",
            "305",
            "--admin-key",
            "key",
        ])
        .unwrap();
        let Command::Compatibility(args) = cli.command else {
            panic!("expected compatibility command");
        };
        assert_eq!(args.mod_id, "fixture");
        assert_eq!(args.revision, 1);
        assert_eq!(args.compatible_actvers, [303, 305]);
    }

    #[test]
    fn publish_result_uses_server_name() {
        let response = serde_json::json!({"name": "Fixture"});
        assert_eq!(upload_result_name(&response, ""), "Fixture");
    }

    #[test]
    fn revision_comparison_defaults_to_one_and_checks_hashes() {
        let remote = serde_json::json!({"packageRevision": 2, "sha256": "abcd"});
        let same = serde_json::json!({"packageRevision": 2, "sha256": "ABCD"});
        let corrupt = serde_json::json!({"packageRevision": 2, "sha256": "ffff"});
        let legacy = serde_json::json!({});
        assert!(install_is_current(&remote, &same));
        assert!(!install_is_current(&remote, &corrupt));
        assert!(install_is_current(&legacy, &legacy));
        assert!(!install_is_current(&remote, &legacy));
    }

    #[test]
    fn staged_replace_removes_stale_files() {
        let root = tempdir().unwrap();
        let dest = root.path().join("@fixture");
        let stage = root.path().join("stage");
        std::fs::create_dir_all(&dest).unwrap();
        std::fs::create_dir_all(&stage).unwrap();
        std::fs::write(dest.join("stale.txt"), b"old").unwrap();
        std::fs::write(stage.join("current.txt"), b"new").unwrap();

        replace_install(&stage, &dest).unwrap();

        assert!(!dest.join("stale.txt").exists());
        assert_eq!(std::fs::read(dest.join("current.txt")).unwrap(), b"new");
    }
}
