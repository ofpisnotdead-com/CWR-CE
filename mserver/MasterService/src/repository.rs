use std::borrow::Cow;

use anyhow::{Context, Result};
use sqlx::any::{AnyPoolOptions, AnyRow};
use sqlx::{AnyPool, Row};

use crate::model::{
    DirectoryServerRecord, ListServersQuery, RegisterServerRequest, ServerPopulationSample,
    ServerRecentSession, ServerVersionGroup, VerificationState,
};

/// Consecutive failed probes before a server is flipped to `unreachable` and hidden from
/// the default browse.
const UNREACHABLE_AFTER_FAILURES: i64 = 1;

/// Default browse hides a server whose last heartbeat is older than this (publisher
/// heartbeats every ~30s, so this is roughly two missed beats).
const HEARTBEAT_FRESH_MS: i64 = 60_000;

/// An `unreachable` verdict hides a row only while this fresh. A stale verdict (the prober
/// stopped) expires, so the server reappears on heartbeat alone — self-healing.
const UNREACHABLE_HIDE_MS: i64 = 60_000;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Dialect {
    Sqlite,
    Postgres,
}

/// Server/mod directory backed by either SQLite (local dev) or Postgres (deploy), selected
/// by the connection URL. Queries are written once against the sqlx `Any` driver; only the
/// schema DDL differs per dialect (column types). Booleans are stored as 0/1 integers in
/// both backends so a single query body binds/reads them uniformly.
pub struct ServerDirectory {
    pool: AnyPool,
    dialect: Dialect,
}

/// Back-compat alias: the directory used to be SQLite-only.
pub type SqliteServerDirectory = ServerDirectory;

/// Build a sqlx SQLite connection URL from a filesystem path that round-trips on every
/// platform. The sqlx `Any` layer first parses the string with the `url` crate, then the
/// SQLite backend re-serialises that `Url` back into a filename. An authority-style
/// `sqlite://C:\…?mode=rwc` makes the `url` crate read the Windows drive path as `host:port`
/// and drop it (→ `SQLITE_CANTOPEN`); a `sqlite:///C:/…` would leave a leading slash before
/// the drive letter. The no-authority `sqlite:` form with forward slashes parses identically
/// on Windows and Linux — the `url` crate keeps it as an opaque path and SQLite accepts `/`
/// as a separator on Windows. Spaces and other reserved chars survive the round trip because
/// the `url` crate percent-encodes them and sqlx percent-decodes the filename.
pub fn sqlite_url_for_path(path: impl AsRef<std::path::Path>) -> String {
    let normalized = path.as_ref().to_string_lossy().replace('\\', "/");
    format!("sqlite:{normalized}?mode=rwc")
}

impl ServerDirectory {
    /// Open the directory from a sqlx connection URL: `sqlite://<path>?mode=rwc` for local
    /// dev, `postgres://user:pass@host/db` for deployment.
    pub async fn open(url: &str) -> Result<Self> {
        sqlx::any::install_default_drivers();
        let dialect = if url.starts_with("postgres") {
            Dialect::Postgres
        } else {
            Dialect::Sqlite
        };
        let pool = AnyPoolOptions::new()
            .max_connections(if dialect == Dialect::Postgres { 8 } else { 1 })
            .connect(url)
            .await
            .with_context(|| format!("connecting to {url}"))?;
        let directory = Self { pool, dialect };
        directory.initialize().await?;
        Ok(directory)
    }

    /// Convenience for tests/callers that have a local SQLite file path rather than a URL.
    pub async fn open_path(path: impl AsRef<std::path::Path>) -> Result<Self> {
        Self::open(&sqlite_url_for_path(path)).await
    }

    async fn initialize(&self) -> Result<()> {
        sqlx::raw_sql(self.schema())
            .execute(&self.pool)
            .await
            .context("initializing schema")?;
        self.migrate_servers().await?;
        self.create_post_migration_indexes().await?;
        Ok(())
    }

    /// Bring a pre-existing `servers` table forward to the current schema. Fresh databases
    /// already have every column from `CREATE TABLE` above, so this is a no-op for them;
    /// it only patches older deployments. Idempotent (gated on the live column set), so it
    /// is safe to run on every startup, and works on both SQLite and Postgres without
    /// relying on `ADD COLUMN IF NOT EXISTS`.
    async fn migrate_servers(&self) -> Result<()> {
        let columns = self.server_columns().await?;
        let has = |name: &str| columns.iter().any(|c| c == name);

        // The game-state field was renamed gstate -> state.
        if has("gstate") && !has("state") {
            sqlx::raw_sql("ALTER TABLE servers RENAME COLUMN gstate TO state")
                .execute(&self.pool)
                .await
                .context("renaming servers.gstate to state")?;
        }

        let int_type = match self.dialect {
            Dialect::Postgres => "BIGINT",
            Dialect::Sqlite => "INTEGER",
        };
        // (column, type) — booleans are stored as 0/1 integers, like the rest of the schema.
        let additions: [(&str, &str); 17] = [
            ("app_name", "TEXT"),
            ("version_tag", "TEXT"),
            ("time_left", int_type),
            ("state_elapsed_seconds", int_type),
            ("map_name", "TEXT"),
            ("cadet", int_type),
            ("difficulty", int_type),
            ("jip", int_type),
            ("disabled_ai", int_type),
            ("respawn", int_type),
            ("respawn_delay", int_type),
            ("locked", int_type),
            ("dedicated", int_type),
            ("description", "TEXT"),
            ("param1", "TEXT"),
            ("param2", "TEXT"),
            ("required_addons", "TEXT"),
        ];
        // Re-read in case the rename above changed the set.
        let columns = self.server_columns().await?;
        for (name, ty) in additions {
            if columns.iter().any(|c| c == name) {
                continue;
            }
            let default = if ty == "TEXT" { "''" } else { "0" };
            sqlx::raw_sql(&format!(
                "ALTER TABLE servers ADD COLUMN {name} {ty} NOT NULL DEFAULT {default}"
            ))
            .execute(&self.pool)
            .await
            .with_context(|| format!("adding servers.{name}"))?;
        }
        Ok(())
    }

    async fn create_post_migration_indexes(&self) -> Result<()> {
        sqlx::raw_sql(
            "CREATE INDEX IF NOT EXISTS idx_servers_app_version ON servers(app_name, actver, version_tag);",
        )
        .execute(&self.pool)
        .await
        .context("creating post-migration indexes")?;
        Ok(())
    }

    /// Current column names of the `servers` table, per dialect.
    async fn server_columns(&self) -> Result<Vec<String>> {
        let query = match self.dialect {
            Dialect::Sqlite => "SELECT name FROM pragma_table_info('servers')",
            // column_name is Postgres' internal `name` type, which the sqlx Any driver can't
            // decode — cast to text so it reads back as a plain string.
            Dialect::Postgres => {
                "SELECT column_name::text FROM information_schema.columns WHERE table_name = 'servers'"
            }
        };
        let rows = sqlx::query(query)
            .fetch_all(&self.pool)
            .await
            .context("reading servers columns")?;
        Ok(rows
            .iter()
            .filter_map(|row| row.try_get::<String, _>(0).ok())
            .collect())
    }

    fn schema(&self) -> &'static str {
        match self.dialect {
            Dialect::Sqlite => SCHEMA_SQLITE,
            Dialect::Postgres => SCHEMA_POSTGRES,
        }
    }

    /// Adapt a query body written with SQLite-style `?` placeholders to the active dialect.
    /// SQLite keeps `?`; Postgres needs `$1..$N`. The sqlx `Any` driver does not translate
    /// placeholders, so a `?` reaches Postgres verbatim and is rejected
    /// ("syntax error at end of input").
    fn bind_sql<'a>(&self, query: &'a str) -> Cow<'a, str> {
        rewrite_placeholders(self.dialect, query)
    }

    pub async fn register(
        &self,
        request: RegisterServerRequest,
        now_unix_ms: i64,
    ) -> Result<DirectoryServerRecord> {
        let record = request.into_record(now_unix_ms);
        sqlx::query(&self.bind_sql(
            "INSERT INTO servers (
                app_name, server_id, address, hostport, hostname, gametype, actver, reqver, version_tag, state,
                numplayers, maxplayers, password, mod_list, equal_mod_required, transport_impl,
                platform, last_seen_unix_ms, verification_state, last_observed_unix_ms, observed_reachable,
                time_left, state_elapsed_seconds,
                map_name, cadet, difficulty, jip, disabled_ai, respawn, respawn_delay, locked, dedicated,
                description, param1, param2, required_addons
            ) VALUES (
                ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?,
                ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?
            )
            ON CONFLICT(server_id) DO UPDATE SET
                app_name = excluded.app_name,
                address = excluded.address,
                hostport = excluded.hostport,
                hostname = excluded.hostname,
                gametype = excluded.gametype,
                actver = excluded.actver,
                reqver = excluded.reqver,
                version_tag = excluded.version_tag,
                state = excluded.state,
                numplayers = excluded.numplayers,
                maxplayers = excluded.maxplayers,
                password = excluded.password,
                mod_list = excluded.mod_list,
                equal_mod_required = excluded.equal_mod_required,
                transport_impl = excluded.transport_impl,
                platform = excluded.platform,
                last_seen_unix_ms = excluded.last_seen_unix_ms,
                time_left = excluded.time_left,
                state_elapsed_seconds = excluded.state_elapsed_seconds,
                map_name = excluded.map_name,
                cadet = excluded.cadet,
                difficulty = excluded.difficulty,
                jip = excluded.jip,
                disabled_ai = excluded.disabled_ai,
                respawn = excluded.respawn,
                respawn_delay = excluded.respawn_delay,
                locked = excluded.locked,
                dedicated = excluded.dedicated,
                description = excluded.description,
                param1 = excluded.param1,
                param2 = excluded.param2,
                required_addons = excluded.required_addons",
        ))
        .bind(&record.app_name)
        .bind(&record.server_id)
        .bind(&record.address)
        .bind(i64::from(record.hostport))
        .bind(&record.hostname)
        .bind(&record.gametype)
        .bind(i64::from(record.actver))
        .bind(i64::from(record.reqver))
        .bind(&record.version_tag)
        .bind(i64::from(record.state))
        .bind(i64::from(record.numplayers))
        .bind(i64::from(record.maxplayers))
        .bind(i64::from(record.password))
        .bind(&record.mod_list)
        .bind(i64::from(record.equal_mod_required))
        .bind(&record.transport_impl)
        .bind(&record.platform)
        .bind(record.last_seen_unix_ms)
        .bind(verification_state_as_str(record.verification_state))
        .bind(record.last_observed_unix_ms)
        .bind(record.observed_reachable.map(i64::from))
        .bind(i64::from(record.time_left))
        .bind(i64::from(record.state_elapsed_seconds))
        .bind(&record.map_name)
        .bind(i64::from(record.cadet))
        .bind(i64::from(record.difficulty))
        .bind(i64::from(record.jip))
        .bind(i64::from(record.disabled_ai))
        .bind(i64::from(record.respawn))
        .bind(i64::from(record.respawn_delay))
        .bind(i64::from(record.locked))
        .bind(i64::from(record.dedicated))
        .bind(&record.description)
        .bind(&record.param1)
        .bind(&record.param2)
        .bind(&record.required_addons)
        .execute(&self.pool)
        .await?;

        self.insert_sample(
            &record.server_id,
            record.last_seen_unix_ms,
            record.numplayers,
        )
        .await?;

        self.get(&record.server_id)
            .await?
            .with_context(|| format!("server {} missing after upsert", record.server_id))
    }

    pub async fn observe(
        &self,
        server_id: &str,
        observed_unix_ms: i64,
        reachable: bool,
    ) -> Result<Option<DirectoryServerRecord>> {
        // A single failed probe can be a transient blip, so we only flip a row to
        // `unreachable` (hidden from the default browse) after UNREACHABLE_AFTER_FAILURES
        // consecutive misses. A reachable probe clears the streak and re-verifies.
        // `reachable` is bound as 0/1 in three positions (Any uses positional `?`).
        let updated = sqlx::query(&self.bind_sql(
            "UPDATE servers
             SET observed_reachable = ?,
                 last_observed_unix_ms = ?,
                 consecutive_failures = CASE WHEN ? <> 0 THEN 0 ELSE consecutive_failures + 1 END,
                 verification_state = CASE
                     WHEN ? <> 0 THEN 'verified'
                     WHEN consecutive_failures + 1 >= ? THEN 'unreachable'
                     ELSE verification_state
                 END
             WHERE server_id = ?",
        ))
        .bind(i64::from(reachable))
        .bind(observed_unix_ms)
        .bind(i64::from(reachable))
        .bind(i64::from(reachable))
        .bind(UNREACHABLE_AFTER_FAILURES)
        .bind(server_id)
        .execute(&self.pool)
        .await?
        .rows_affected();
        if updated == 0 {
            return Ok(None);
        }
        self.get(server_id).await
    }

    pub async fn set_token_hash(&self, server_id: &str, token_hash: &str) -> Result<()> {
        sqlx::query(&self.bind_sql("UPDATE servers SET token_hash = ? WHERE server_id = ?"))
            .bind(token_hash)
            .bind(server_id)
            .execute(&self.pool)
            .await?;
        Ok(())
    }

    /// The stored token hash for a server, or `None` if the row or its token is absent.
    pub async fn server_token_hash(&self, server_id: &str) -> Result<Option<String>> {
        let row = sqlx::query(&self.bind_sql("SELECT token_hash FROM servers WHERE server_id = ?"))
            .bind(server_id)
            .fetch_optional(&self.pool)
            .await?;
        Ok(row.and_then(|row| row.try_get::<Option<String>, _>(0).ok().flatten()))
    }

    pub async fn get(&self, server_id: &str) -> Result<Option<DirectoryServerRecord>> {
        let row =
            sqlx::query(&self.bind_sql(&format!("{SELECT_SERVER_COLUMNS} WHERE server_id = ?")))
                .bind(server_id)
                .fetch_optional(&self.pool)
                .await?;
        row.map(|row| map_row(&row)).transpose()
    }

    pub async fn list(
        &self,
        query: &ListServersQuery,
        now_unix_ms: i64,
    ) -> Result<Vec<DirectoryServerRecord>> {
        let rows = sqlx::query(&format!(
            "{SELECT_SERVER_COLUMNS} ORDER BY last_seen_unix_ms DESC"
        ))
        .fetch_all(&self.pool)
        .await?;
        let mut records = rows.iter().map(map_row).collect::<Result<Vec<_>>>()?;

        let hostname_filter = query.hostname.as_ref().map(|value| value.to_lowercase());
        let mission_filter = query.gametype.as_ref().map(|value| value.to_lowercase());
        let app_filter = query.app_name.as_ref().map(|value| value.to_lowercase());
        let version_tag_filter = query.version_tag.as_ref().map(|value| value.to_lowercase());
        let include_unverified_servers = query.include_unverified_servers.unwrap_or(false);
        let include_full_servers = query.include_full_servers.unwrap_or(true);
        let include_passworded_servers = query.include_passworded_servers.unwrap_or(false);

        records.retain(|record| {
            if let Some(filter) = &app_filter {
                if record.app_name.to_lowercase() != *filter {
                    return false;
                }
            }

            if let Some(actver) = query.actver {
                if record.actver != actver {
                    return false;
                }
            }

            if let Some(filter) = &version_tag_filter {
                if record.version_tag.to_lowercase() != *filter {
                    return false;
                }
            }

            if let Some(filter) = &hostname_filter {
                if !record.hostname.to_lowercase().contains(filter) {
                    return false;
                }
            }

            if let Some(filter) = &mission_filter {
                if !record.gametype.to_lowercase().contains(filter) {
                    return false;
                }
            }

            if let Some(minplayers) = query.minplayers {
                if record.numplayers < minplayers {
                    return false;
                }
            }

            if let Some(maxplayers) = query.maxplayers {
                if record.numplayers > maxplayers {
                    return false;
                }
            }

            if let Some(verification_state) = query.verification_state {
                if record.verification_state != verification_state {
                    return false;
                }
            } else if !include_unverified_servers {
                // Additive default: heartbeats keep a server listed; the probe only hides
                // servers it has confirmed unreachable. self_reported (not yet/can't be
                // probed) stays visible, so a probe outage never empties the list.
                // Heartbeat is authoritative — hide servers that stopped checking in.
                if record.last_seen_unix_ms < now_unix_ms - HEARTBEAT_FRESH_MS {
                    return false;
                }
                // Hide only a *recently* confirmed-unreachable row; a stale verdict expires.
                if record.verification_state == VerificationState::Unreachable
                    && record
                        .last_observed_unix_ms
                        .is_some_and(|observed| observed >= now_unix_ms - UNREACHABLE_HIDE_MS)
                {
                    return false;
                }
            }

            if !include_full_servers && record.numplayers >= record.maxplayers {
                return false;
            }

            if !include_passworded_servers && record.password {
                return false;
            }

            if let Some(transport_impl) = &query.transport_impl {
                if &record.transport_impl != transport_impl {
                    return false;
                }
            }

            true
        });

        if let Some(limit) = query.limit {
            records.truncate(limit);
        }

        Ok(records)
    }

    pub async fn version_groups(
        &self,
        query: &ListServersQuery,
        now_unix_ms: i64,
    ) -> Result<Vec<ServerVersionGroup>> {
        let mut groups = Vec::<ServerVersionGroup>::new();
        for record in self.list(query, now_unix_ms).await? {
            if let Some(group) = groups.iter_mut().find(|group| {
                group.app_name == record.app_name
                    && group.actver == record.actver
                    && group.version_tag == record.version_tag
            }) {
                group.servers += 1;
            } else {
                groups.push(ServerVersionGroup {
                    app_name: record.app_name,
                    actver: record.actver,
                    version_tag: record.version_tag,
                    servers: 1,
                });
            }
        }
        groups.sort_by(|lhs, rhs| {
            lhs.app_name
                .cmp(&rhs.app_name)
                .then_with(|| rhs.actver.cmp(&lhs.actver))
                .then_with(|| rhs.version_tag.cmp(&lhs.version_tag))
        });
        Ok(groups)
    }

    pub async fn unregister(&self, server_id: &str) -> Result<bool> {
        let deleted = sqlx::query(&self.bind_sql("DELETE FROM servers WHERE server_id = ?"))
            .bind(server_id)
            .execute(&self.pool)
            .await?
            .rows_affected();
        self.remove_orphan_samples().await?;
        Ok(deleted > 0)
    }

    pub async fn prune_stale(&self, min_last_seen_unix_ms: i64) -> Result<usize> {
        let deleted =
            sqlx::query(&self.bind_sql("DELETE FROM servers WHERE last_seen_unix_ms < ?"))
                .bind(min_last_seen_unix_ms)
                .execute(&self.pool)
                .await?
                .rows_affected();
        self.remove_orphan_samples().await?;
        Ok(usize::try_from(deleted).unwrap_or(0))
    }

    pub async fn clear_samples(&self, server_id: &str) -> Result<()> {
        sqlx::query(&self.bind_sql("DELETE FROM server_samples WHERE server_id = ?"))
            .bind(server_id)
            .execute(&self.pool)
            .await?;
        Ok(())
    }

    pub async fn clear_sessions(&self, server_id: &str) -> Result<()> {
        sqlx::query(&self.bind_sql("DELETE FROM server_sessions WHERE server_id = ?"))
            .bind(server_id)
            .execute(&self.pool)
            .await?;
        Ok(())
    }

    pub async fn insert_sample(
        &self,
        server_id: &str,
        observed_unix_ms: i64,
        players: i32,
    ) -> Result<()> {
        sqlx::query(&self.bind_sql(
            "INSERT INTO server_samples (server_id, observed_unix_ms, numplayers)
             VALUES (?, ?, ?)
             ON CONFLICT(server_id, observed_unix_ms) DO UPDATE SET numplayers = excluded.numplayers",
        ))
        .bind(server_id)
        .bind(observed_unix_ms)
        .bind(i64::from(players))
        .execute(&self.pool)
        .await?;
        Ok(())
    }

    pub async fn insert_session(
        &self,
        server_id: &str,
        session: &ServerRecentSession,
    ) -> Result<()> {
        sqlx::query(&self.bind_sql(
            "INSERT INTO server_sessions (
                server_id, mission, label, played_minutes, peak_players, ended_unix_ms
            ) VALUES (?, ?, ?, ?, ?, ?)
            ON CONFLICT(server_id, ended_unix_ms) DO UPDATE SET
                mission = excluded.mission,
                label = excluded.label,
                played_minutes = excluded.played_minutes,
                peak_players = excluded.peak_players",
        ))
        .bind(server_id)
        .bind(&session.mission)
        .bind(&session.label)
        .bind(session.played_minutes)
        .bind(i64::from(session.peak_players))
        .bind(session.ended_unix_ms)
        .execute(&self.pool)
        .await?;
        Ok(())
    }

    pub async fn history(
        &self,
        server_id: &str,
        limit: usize,
    ) -> Result<Vec<ServerPopulationSample>> {
        let limit = i64::try_from(limit).context("converting history limit")?;
        let rows = sqlx::query(&self.bind_sql(
            "SELECT observed_unix_ms, numplayers
             FROM server_samples
             WHERE server_id = ?
             ORDER BY observed_unix_ms DESC
             LIMIT ?",
        ))
        .bind(server_id)
        .bind(limit)
        .fetch_all(&self.pool)
        .await?;
        let mut samples = rows
            .iter()
            .map(|row| {
                Ok(ServerPopulationSample {
                    observed_unix_ms: row.try_get::<i64, _>(0)?,
                    players: i32::try_from(row.try_get::<i64, _>(1)?).unwrap_or(0),
                })
            })
            .collect::<Result<Vec<_>>>()?;
        samples.reverse();
        Ok(samples)
    }

    pub async fn sessions(
        &self,
        server_id: &str,
        limit: usize,
    ) -> Result<Vec<ServerRecentSession>> {
        let limit = i64::try_from(limit).context("converting session limit")?;
        let rows = sqlx::query(&self.bind_sql(
            "SELECT mission, label, played_minutes, peak_players, ended_unix_ms
             FROM server_sessions
             WHERE server_id = ?
             ORDER BY ended_unix_ms DESC
             LIMIT ?",
        ))
        .bind(server_id)
        .bind(limit)
        .fetch_all(&self.pool)
        .await?;
        rows.iter()
            .map(|row| {
                Ok(ServerRecentSession {
                    mission: row.try_get::<String, _>(0)?,
                    label: row.try_get::<String, _>(1)?,
                    played_minutes: row.try_get::<i64, _>(2)?,
                    peak_players: i32::try_from(row.try_get::<i64, _>(3)?).unwrap_or(0),
                    ended_unix_ms: row.try_get::<i64, _>(4)?,
                })
            })
            .collect()
    }

    async fn remove_orphan_samples(&self) -> Result<()> {
        sqlx::query(
            "DELETE FROM server_samples WHERE server_id NOT IN (SELECT server_id FROM servers)",
        )
        .execute(&self.pool)
        .await?;
        sqlx::query(
            "DELETE FROM server_sessions WHERE server_id NOT IN (SELECT server_id FROM servers)",
        )
        .execute(&self.pool)
        .await?;
        Ok(())
    }
}

const SELECT_SERVER_COLUMNS: &str = "SELECT
    app_name, server_id, address, hostport, hostname, gametype, actver, reqver, version_tag, state,
    numplayers, maxplayers, password, mod_list, equal_mod_required, transport_impl,
    platform, last_seen_unix_ms, verification_state, last_observed_unix_ms, observed_reachable,
    time_left, state_elapsed_seconds,
    map_name, cadet, difficulty, jip, disabled_ai, respawn, respawn_delay, locked, dedicated,
    description, param1, param2, required_addons
FROM servers";

fn map_row(row: &AnyRow) -> Result<DirectoryServerRecord> {
    Ok(DirectoryServerRecord {
        app_name: row.try_get(0)?,
        server_id: row.try_get(1)?,
        address: row.try_get(2)?,
        hostport: u16::try_from(row.try_get::<i64, _>(3)?).unwrap_or(0),
        hostname: row.try_get(4)?,
        gametype: row.try_get(5)?,
        actver: i32::try_from(row.try_get::<i64, _>(6)?).unwrap_or(0),
        reqver: i32::try_from(row.try_get::<i64, _>(7)?).unwrap_or(0),
        version_tag: row.try_get(8)?,
        state: i32::try_from(row.try_get::<i64, _>(9)?).unwrap_or(0),
        numplayers: i32::try_from(row.try_get::<i64, _>(10)?).unwrap_or(0),
        maxplayers: i32::try_from(row.try_get::<i64, _>(11)?).unwrap_or(0),
        password: row.try_get::<i64, _>(12)? != 0,
        mod_list: row.try_get(13)?,
        equal_mod_required: row.try_get::<i64, _>(14)? != 0,
        transport_impl: row.try_get(15)?,
        platform: row.try_get(16)?,
        last_seen_unix_ms: row.try_get(17)?,
        verification_state: verification_state_from_str(&row.try_get::<String, _>(18)?)?,
        last_observed_unix_ms: row.try_get(19)?,
        observed_reachable: row.try_get::<Option<i64>, _>(20)?.map(|value| value != 0),
        time_left: i32::try_from(row.try_get::<i64, _>(21)?).unwrap_or(0),
        state_elapsed_seconds: i32::try_from(row.try_get::<i64, _>(22)?).unwrap_or(0),
        map_name: row.try_get(23)?,
        cadet: row.try_get::<i64, _>(24)? != 0,
        difficulty: i32::try_from(row.try_get::<i64, _>(25)?).unwrap_or(0),
        jip: row.try_get::<i64, _>(26)? != 0,
        disabled_ai: row.try_get::<i64, _>(27)? != 0,
        respawn: i32::try_from(row.try_get::<i64, _>(28)?).unwrap_or(0),
        respawn_delay: i32::try_from(row.try_get::<i64, _>(29)?).unwrap_or(0),
        locked: row.try_get::<i64, _>(30)? != 0,
        dedicated: row.try_get::<i64, _>(31)? != 0,
        description: row.try_get(32)?,
        param1: row.try_get(33)?,
        param2: row.try_get(34)?,
        required_addons: row.try_get(35)?,
        token: None,
    })
}

const fn verification_state_as_str(state: VerificationState) -> &'static str {
    match state {
        VerificationState::SelfReported => "self_reported",
        VerificationState::Verified => "verified",
        VerificationState::Unreachable => "unreachable",
    }
}

fn verification_state_from_str(value: &str) -> Result<VerificationState> {
    match value {
        "self_reported" => Ok(VerificationState::SelfReported),
        "verified" => Ok(VerificationState::Verified),
        "unreachable" => Ok(VerificationState::Unreachable),
        other => Err(anyhow::anyhow!("unknown verification_state '{other}'")),
    }
}

/// Convert each `SQLite`-style positional `?` placeholder into a Postgres `$1..$N`
/// placeholder, numbered left to right. `SQLite` queries are returned unchanged. None of the
/// directory query bodies contain a literal `?` inside a string, so a plain positional rewrite
/// is exact.
fn rewrite_placeholders(dialect: Dialect, query: &str) -> Cow<'_, str> {
    if dialect == Dialect::Sqlite || !query.contains('?') {
        return Cow::Borrowed(query);
    }
    let mut out = String::with_capacity(query.len() + 8);
    let mut index = 1u32;
    for ch in query.chars() {
        if ch == '?' {
            out.push('$');
            out.push_str(&index.to_string());
            index += 1;
        } else {
            out.push(ch);
        }
    }
    Cow::Owned(out)
}

const SCHEMA_SQLITE: &str = "
PRAGMA journal_mode = WAL;
PRAGMA synchronous = NORMAL;
CREATE TABLE IF NOT EXISTS servers (
    server_id TEXT PRIMARY KEY,
    app_name TEXT NOT NULL DEFAULT '',
    address TEXT NOT NULL,
    hostport INTEGER NOT NULL,
    hostname TEXT NOT NULL,
    gametype TEXT NOT NULL,
    actver INTEGER NOT NULL,
    reqver INTEGER NOT NULL,
    version_tag TEXT NOT NULL DEFAULT '',
    state INTEGER NOT NULL,
    numplayers INTEGER NOT NULL,
    maxplayers INTEGER NOT NULL,
    password INTEGER NOT NULL,
    mod_list TEXT NOT NULL,
    equal_mod_required INTEGER NOT NULL,
    transport_impl TEXT NOT NULL,
    platform TEXT NOT NULL,
    last_seen_unix_ms INTEGER NOT NULL,
    verification_state TEXT NOT NULL DEFAULT 'self_reported',
    last_observed_unix_ms INTEGER,
    observed_reachable INTEGER,
    time_left INTEGER NOT NULL DEFAULT 0,
    state_elapsed_seconds INTEGER NOT NULL DEFAULT 0,
    map_name TEXT NOT NULL DEFAULT '',
    cadet INTEGER NOT NULL DEFAULT 0,
    difficulty INTEGER NOT NULL DEFAULT 0,
    jip INTEGER NOT NULL DEFAULT 0,
    disabled_ai INTEGER NOT NULL DEFAULT 0,
    respawn INTEGER NOT NULL DEFAULT 0,
    respawn_delay INTEGER NOT NULL DEFAULT 0,
    locked INTEGER NOT NULL DEFAULT 0,
    dedicated INTEGER NOT NULL DEFAULT 0,
    description TEXT NOT NULL DEFAULT '',
    param1 TEXT NOT NULL DEFAULT '',
    param2 TEXT NOT NULL DEFAULT '',
    required_addons TEXT NOT NULL DEFAULT '',
    consecutive_failures INTEGER NOT NULL DEFAULT 0,
    token_hash TEXT
);
CREATE TABLE IF NOT EXISTS server_samples (
    server_id TEXT NOT NULL,
    observed_unix_ms INTEGER NOT NULL,
    numplayers INTEGER NOT NULL,
    PRIMARY KEY (server_id, observed_unix_ms)
);
CREATE TABLE IF NOT EXISTS server_sessions (
    server_id TEXT NOT NULL,
    mission TEXT NOT NULL DEFAULT '',
    label TEXT NOT NULL,
    played_minutes INTEGER NOT NULL,
    peak_players INTEGER NOT NULL,
    ended_unix_ms INTEGER NOT NULL,
    PRIMARY KEY (server_id, ended_unix_ms)
);
CREATE INDEX IF NOT EXISTS idx_servers_last_seen ON servers(last_seen_unix_ms DESC);
CREATE INDEX IF NOT EXISTS idx_servers_impl ON servers(transport_impl);
CREATE INDEX IF NOT EXISTS idx_servers_hostname ON servers(hostname);
CREATE INDEX IF NOT EXISTS idx_servers_gametype ON servers(gametype);
CREATE INDEX IF NOT EXISTS idx_server_samples_server_time
    ON server_samples(server_id, observed_unix_ms DESC);
CREATE INDEX IF NOT EXISTS idx_server_sessions_server_time
    ON server_sessions(server_id, ended_unix_ms DESC);
";

const SCHEMA_POSTGRES: &str = "
CREATE TABLE IF NOT EXISTS servers (
    server_id TEXT PRIMARY KEY,
    app_name TEXT NOT NULL DEFAULT '',
    address TEXT NOT NULL,
    hostport BIGINT NOT NULL,
    hostname TEXT NOT NULL,
    gametype TEXT NOT NULL,
    actver BIGINT NOT NULL,
    reqver BIGINT NOT NULL,
    version_tag TEXT NOT NULL DEFAULT '',
    state BIGINT NOT NULL,
    numplayers BIGINT NOT NULL,
    maxplayers BIGINT NOT NULL,
    password BIGINT NOT NULL,
    mod_list TEXT NOT NULL,
    equal_mod_required BIGINT NOT NULL,
    transport_impl TEXT NOT NULL,
    platform TEXT NOT NULL,
    last_seen_unix_ms BIGINT NOT NULL,
    verification_state TEXT NOT NULL DEFAULT 'self_reported',
    last_observed_unix_ms BIGINT,
    observed_reachable BIGINT,
    time_left BIGINT NOT NULL DEFAULT 0,
    state_elapsed_seconds BIGINT NOT NULL DEFAULT 0,
    map_name TEXT NOT NULL DEFAULT '',
    cadet BIGINT NOT NULL DEFAULT 0,
    difficulty BIGINT NOT NULL DEFAULT 0,
    jip BIGINT NOT NULL DEFAULT 0,
    disabled_ai BIGINT NOT NULL DEFAULT 0,
    respawn BIGINT NOT NULL DEFAULT 0,
    respawn_delay BIGINT NOT NULL DEFAULT 0,
    locked BIGINT NOT NULL DEFAULT 0,
    dedicated BIGINT NOT NULL DEFAULT 0,
    description TEXT NOT NULL DEFAULT '',
    param1 TEXT NOT NULL DEFAULT '',
    param2 TEXT NOT NULL DEFAULT '',
    required_addons TEXT NOT NULL DEFAULT '',
    consecutive_failures BIGINT NOT NULL DEFAULT 0,
    token_hash TEXT
);
CREATE TABLE IF NOT EXISTS server_samples (
    server_id TEXT NOT NULL,
    observed_unix_ms BIGINT NOT NULL,
    numplayers BIGINT NOT NULL,
    PRIMARY KEY (server_id, observed_unix_ms)
);
CREATE TABLE IF NOT EXISTS server_sessions (
    server_id TEXT NOT NULL,
    mission TEXT NOT NULL DEFAULT '',
    label TEXT NOT NULL,
    played_minutes BIGINT NOT NULL,
    peak_players BIGINT NOT NULL,
    ended_unix_ms BIGINT NOT NULL,
    PRIMARY KEY (server_id, ended_unix_ms)
);
CREATE INDEX IF NOT EXISTS idx_servers_last_seen ON servers(last_seen_unix_ms DESC);
CREATE INDEX IF NOT EXISTS idx_servers_impl ON servers(transport_impl);
CREATE INDEX IF NOT EXISTS idx_servers_hostname ON servers(hostname);
CREATE INDEX IF NOT EXISTS idx_servers_gametype ON servers(gametype);
CREATE INDEX IF NOT EXISTS idx_server_samples_server_time
    ON server_samples(server_id, observed_unix_ms DESC);
CREATE INDEX IF NOT EXISTS idx_server_sessions_server_time
    ON server_sessions(server_id, ended_unix_ms DESC);
";

#[cfg(test)]
mod tests {
    use super::{rewrite_placeholders, Dialect};

    // Regression: the directory was switched to a multi-dialect sqlx `Any` pool (SQLite for
    // dev, Postgres for deploy), but every query body keeps SQLite-style `?` placeholders.
    // The `Any` driver does not translate placeholders, so against Postgres the `?` reached
    // the server verbatim and was rejected ("syntax error at end of input") — register,
    // heartbeat, prune and every other write 500'd. `rewrite_placeholders` adapts `?` to
    // `$1..$N` for Postgres while leaving SQLite untouched.
    #[test]
    fn postgres_placeholders_are_numbered_left_to_right() {
        let rewritten = rewrite_placeholders(
            Dialect::Postgres,
            "UPDATE servers SET token_hash = ? WHERE server_id = ?",
        );
        assert_eq!(
            rewritten,
            "UPDATE servers SET token_hash = $1 WHERE server_id = $2"
        );
    }

    #[test]
    fn postgres_rewrites_every_placeholder_in_a_multi_value_insert() {
        let rewritten = rewrite_placeholders(
            Dialect::Postgres,
            "INSERT INTO server_samples (server_id, observed_unix_ms, numplayers) VALUES (?, ?, ?)",
        );
        assert_eq!(
            rewritten,
            "INSERT INTO server_samples (server_id, observed_unix_ms, numplayers) VALUES ($1, $2, $3)"
        );
        // The placeholder count must match the bind count, or Postgres rejects the statement.
        assert_eq!(rewritten.matches('$').count(), 3);
    }

    #[test]
    fn sqlite_keeps_question_mark_placeholders_verbatim() {
        let sql = "DELETE FROM servers WHERE last_seen_unix_ms < ?";
        assert_eq!(rewrite_placeholders(Dialect::Sqlite, sql), sql);
    }

    #[test]
    fn placeholderless_query_is_borrowed_unchanged_for_both_dialects() {
        let sql =
            "DELETE FROM server_samples WHERE server_id NOT IN (SELECT server_id FROM servers)";
        assert!(matches!(
            rewrite_placeholders(Dialect::Postgres, sql),
            std::borrow::Cow::Borrowed(_)
        ));
        assert!(matches!(
            rewrite_placeholders(Dialect::Sqlite, sql),
            std::borrow::Cow::Borrowed(_)
        ));
    }
}
