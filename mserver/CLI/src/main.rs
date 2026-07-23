//! `papa` — the PapaBear workshop CLI: pack a folder into a PBO, inspect or extract one,
//! and publish a packed mod to a PapaBear master service.

use std::fs;
use std::io::{Cursor, Read};
use std::path::Path;
use std::sync::Arc;
use std::time::Duration;

use anyhow::{bail, Context, Result};
use clap::{Args, Parser, Subcommand};
use papa_bear_archive::Pbo;
use papa_bear_client::codec::SessionResponse;
use papa_bear_client::query::{query_server, ServerStatus};
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
    Publish(PublishArgs),
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

#[derive(Args, Debug)]
struct PublishArgs {
    /// A folder to pack, or an already-packed `.pbo` to upload as-is.
    source: String,
    /// Admin API key (also read from `PAPA_ADMIN_KEY`).
    #[arg(long, env = "PAPA_ADMIN_KEY")]
    admin_key: String,
    /// Display name of the mod.
    #[arg(long)]
    name: String,
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
        Command::List => run_list(&cli.master, cli.insecure),
        Command::Install(args) => run_install(args, &cli.master, cli.insecure),
        Command::Query(args) => run_query(args),
        Command::Servers(args) => run_servers(args, &cli.master, cli.insecure),
        Command::Server(args) => run_server(args, &cli.master, cli.insecure),
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
    let url = format!("{base}/v1/mods");
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
            let mod_id = entry["modId"].as_str().unwrap_or("?");
            let download = entry["downloadUrl"].as_str().unwrap_or("?");
            println!(
                "published '{}' as {mod_id} ({artifact_len} bytes compressed) -> {base}{download}",
                args.name
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
        let dest = Path::new(&args.dir).join(folder);

        let dl_url = format!("{base}/v1/mods/{mod_id}/download");
        let tmp = Path::new(&args.dir).join(format!(".{mod_id}.pbo.download"));
        println!("installing {mod_id} -> {}", dest.display());
        let count = match agent.get(&dl_url).call() {
            Ok(ok) => decode_and_unpack(ok.into_reader(), &tmp, &dest)
                .with_context(|| format!("installing {mod_id}"))?,
            Err(ureq::Error::Status(code, r)) => {
                bail!(
                    "download HTTP {code}: {}",
                    r.into_string().unwrap_or_default().trim()
                )
            }
            Err(error) => bail!("download request failed: {error}"),
        };
        write_installed_mod_manifest(entry, &dest)?;
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
    match tag.map(str::trim).filter(|tag| !tag.is_empty()) {
        Some(tag) => format!(" {tag}"),
        None => String::new(),
    }
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
    let mut fields: Vec<(&str, &str)> = vec![("name", args.name.as_str())];
    if let Some(version) = &args.version {
        fields.push(("version", version));
    }
    if let Some(folder_name) = &args.folder_name {
        fields.push(("folderName", folder_name));
    }
    if let Some(description) = &args.description {
        fields.push(("description", description));
    }
    if let Some(homepage) = &args.homepage_url {
        fields.push(("homepageUrl", homepage));
    }
    for author in &args.authors {
        fields.push(("author", author));
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
        compress_artifact, decode_and_unpack, difficulty_flag_names, with_default_port,
        write_installed_mod_manifest,
    };
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
}
