pub mod dev_seed;
pub mod http;
pub mod model;
pub mod mods;
pub mod repository;
pub mod service;

#[cfg(test)]
mod tests {
    use std::fs;
    use std::net::{IpAddr, Ipv4Addr, SocketAddr};
    use std::sync::Arc;

    use axum::body::{to_bytes, Body};
    use axum::extract::connect_info::ConnectInfo;
    use axum::http::{Request, StatusCode};
    use serde_json::json;
    use serde_json::Value;
    use tempfile::tempdir;
    use tower::ServiceExt;

    use crate::http::{
        build_router, build_router_with_admin_key, build_router_with_options, openapi_document,
        openapi_yaml,
    };
    use crate::model::{
        DirectoryServerRecord, ListModsQuery, ListServersQuery, ModCatalogEntry, ModUsageServer,
        ObserveServerRequest, RegisterServerRequest, ServerDetail, ServiceMetadata, ServiceSummary,
        VerificationState,
    };
    use crate::repository::{sqlite_url_for_path, ServerDirectory, SqliteServerDirectory};

    fn sample_request(server_id: &str, hostname: &str, now_suffix: &str) -> RegisterServerRequest {
        RegisterServerRequest {
            app_name: "CWR".to_string(),
            server_id: server_id.to_string(),
            address: format!("192.168.1.{now_suffix}"),
            hostport: 2302,
            hostname: hostname.to_string(),
            gametype: "coop".to_string(),
            actver: 196,
            reqver: 196,
            version_tag: "rc1".to_string(),
            state: 14,
            numplayers: 3,
            maxplayers: 8,
            password: false,
            mod_list: "synthetic-core".to_string(),
            equal_mod_required: true,
            transport_impl: "papa-bear".to_string(),
            platform: "win".to_string(),
            time_left: 0,
            state_elapsed_seconds: 0,
            map_name: String::new(),
            cadet: false,
            difficulty: 0,
            jip: false,
            disabled_ai: false,
            respawn: 0,
            respawn_delay: 0,
            locked: false,
            dedicated: false,
            description: String::new(),
            param1: String::new(),
            param2: String::new(),
            required_addons: String::new(),
        }
    }

    fn write_mod_metadata(root: &std::path::Path, mod_id: &str, name: &str) {
        write_mod_metadata_with_version(root, mod_id, name, "1.0.0", None);
    }

    fn write_mod_metadata_with_version(
        root: &std::path::Path,
        mod_id: &str,
        name: &str,
        version: &str,
        homepage_url: Option<&str>,
    ) {
        write_mod_metadata_for_game(root, mod_id, name, version, homepage_url, "CWR", 303, None);
    }

    #[allow(clippy::too_many_arguments)]
    fn write_mod_metadata_for_game(
        root: &std::path::Path,
        mod_id: &str,
        name: &str,
        version: &str,
        homepage_url: Option<&str>,
        app: &str,
        actver: i32,
        vertag: Option<&str>,
    ) {
        let mod_dir = root.join(mod_id);
        fs::create_dir_all(&mod_dir).unwrap();
        let homepage_url = homepage_url.map_or_else(
            || format!("https://example.invalid/{mod_id}"),
            str::to_string,
        );
        fs::write(
            mod_dir.join("mod.json"),
            serde_json::to_vec(&json!({
                "modId": mod_id,
                "app": app,
                "actver": actver,
                "vertag": vertag,
                "name": name,
                "version": version,
                "description": format!("{name} description"),
                "authors": ["simi"],
                "homepageUrl": homepage_url,
                "downloadUrl": format!("https://example.invalid/{mod_id}.zip"),
            }))
            .unwrap(),
        )
        .unwrap();
    }

    // Regression: a Windows path must produce a sqlx URL that survives the `url`-crate parse in
    // the `Any` layer. The old `sqlite://{path}` form turned `C:\…` into a URL host:port and
    // every SQLite-backed test failed with SQLITE_CANTOPEN (code 14). The fix uses the
    // no-authority `sqlite:` form with forward slashes — identical on Windows and Linux.
    #[test]
    fn sqlite_url_for_path_is_authority_free_and_uses_forward_slashes() {
        let windows = sqlite_url_for_path(r"C:\Users\test\dir\directory.sqlite");
        assert_eq!(
            windows,
            "sqlite:C:/Users/test/dir/directory.sqlite?mode=rwc"
        );
        // No `//` authority: a Windows drive path there would be parsed as host:port.
        assert!(!windows.starts_with("sqlite://"));
        assert!(!windows.contains('\\'));

        let unix = sqlite_url_for_path("/tmp/papa/directory.sqlite");
        assert_eq!(unix, "sqlite:/tmp/papa/directory.sqlite?mode=rwc");
    }

    #[tokio::test]
    async fn migrate_servers_upgrades_a_pre_46_schema() {
        // A database created before #46: a `gstate` column and none of the new session
        // columns. migrate_servers must rename gstate->state and add the rest.
        let temp_dir = tempdir().unwrap();
        let database_path = temp_dir.path().join("legacy.sqlite");
        let url = sqlite_url_for_path(&database_path);

        sqlx::any::install_default_drivers();
        {
            let pool = sqlx::AnyPool::connect(&url).await.unwrap();
            sqlx::raw_sql(
                "CREATE TABLE servers (
                    server_id TEXT PRIMARY KEY, address TEXT NOT NULL, hostport INTEGER NOT NULL,
                    hostname TEXT NOT NULL, gametype TEXT NOT NULL, actver INTEGER NOT NULL,
                    reqver INTEGER NOT NULL, gstate INTEGER NOT NULL, numplayers INTEGER NOT NULL,
                    maxplayers INTEGER NOT NULL, password INTEGER NOT NULL, mod_list TEXT NOT NULL,
                    equal_mod_required INTEGER NOT NULL, transport_impl TEXT NOT NULL,
                    platform TEXT NOT NULL, last_seen_unix_ms INTEGER NOT NULL,
                    verification_state TEXT NOT NULL DEFAULT 'self_reported',
                    last_observed_unix_ms INTEGER, observed_reachable INTEGER,
                    consecutive_failures INTEGER NOT NULL DEFAULT 0, token_hash TEXT
                );
                INSERT INTO servers (
                    server_id, address, hostport, hostname, gametype, actver, reqver, gstate,
                    numplayers, maxplayers, password, mod_list, equal_mod_required, transport_impl,
                    platform, last_seen_unix_ms, verification_state, consecutive_failures
                ) VALUES (
                    'old-srv', '1.2.3.4', 2302, 'Old Server', 'coop', 196, 196, 14, 3, 8, 0, 'synthetic-core',
                    0, 'papa-bear', 'win', 1000, 'self_reported', 0
                );",
            )
            .execute(&pool)
            .await
            .unwrap();
            pool.close().await;
        }

        // open_path runs CREATE-IF-NOT-EXISTS (a no-op here) then migrate_servers.
        let directory = SqliteServerDirectory::open_path(&database_path)
            .await
            .unwrap();

        // Reading the legacy row needs the rename and every added column to exist — before
        // migrate_servers this SELECT errored on the missing `state` column.
        let row = directory
            .get("old-srv")
            .await
            .unwrap()
            .expect("legacy row present");
        assert_eq!(row.hostname, "Old Server");
        assert_eq!(row.state, 14); // carried across the gstate -> state rename
        assert_eq!(row.time_left, 0); // added column, defaulted
        assert!(!row.cadet);
        assert_eq!(row.description, "");
        assert_eq!(row.required_addons, "");
        assert_eq!(row.version_tag, ""); // added column, defaults empty on a pre-vertag row
        assert_eq!(row.app_name, ""); // legacy rows predate app identity

        // A fresh register still round-trips through the upgraded table.
        let fresh = directory
            .register(sample_request("new-srv", "New", "9"), 2000)
            .await
            .unwrap();
        assert_eq!(fresh.server_id, "new-srv");
    }

    #[tokio::test]
    async fn sqlite_directory_registers_lists_updates_and_prunes() {
        let temp_dir = tempdir().unwrap();
        let database_path = temp_dir.path().join("directory.sqlite");
        let directory = SqliteServerDirectory::open_path(&database_path)
            .await
            .unwrap();

        let first = directory
            .register(sample_request("srv-1", "Alpha", "10"), 1000)
            .await
            .unwrap();
        assert_eq!(first.server_id, "srv-1");
        // The host's build tag round-trips through the upsert (sample_request sets "rc1");
        // without the version_tag column/bind/select it would be absent or empty.
        assert_eq!(first.version_tag, "rc1");
        assert_eq!(first.app_name, "CWR");

        let listed = directory
            .list(
                &ListServersQuery {
                    include_unverified_servers: Some(true),
                    ..Default::default()
                },
                1000,
            )
            .await
            .unwrap();
        assert_eq!(listed.len(), 1);
        assert_eq!(listed[0].hostname, "Alpha");
        // The served list row carries the tag the publisher registered with.
        assert_eq!(listed[0].version_tag, "rc1");
        assert_eq!(listed[0].app_name, "CWR");

        let updated = directory
            .register(sample_request("srv-1", "Bravo", "11"), 2000)
            .await
            .unwrap();
        assert_eq!(updated.hostname, "Bravo");
        assert_eq!(updated.last_seen_unix_ms, 2000);

        let filtered = directory
            .list(
                &ListServersQuery {
                    hostname: Some("brav".to_string()),
                    transport_impl: Some("papa-bear".to_string()),
                    include_full_servers: Some(true),
                    include_passworded_servers: Some(true),
                    include_unverified_servers: Some(true),
                    ..ListServersQuery::default()
                },
                2000,
            )
            .await
            .unwrap();
        assert_eq!(filtered.len(), 1);
        assert_eq!(filtered[0].hostname, "Bravo");

        let removed = directory.prune_stale(2500).await.unwrap();
        assert_eq!(removed, 1);
        assert!(directory
            .list(
                &ListServersQuery {
                    include_unverified_servers: Some(true),
                    ..Default::default()
                },
                2500,
            )
            .await
            .unwrap()
            .is_empty());
    }

    // Regression (Postgres deploy): every directory query is written with SQLite `?`
    // placeholders, but deploy runs on Postgres through the sqlx `Any` pool, which does not
    // translate placeholders. Without the rewrite Postgres rejected every write with
    // "syntax error at end of input", so `register` returned 500 and the public list stayed
    // empty. This drives register -> get -> prune against a real Postgres to prove the
    // `$1..$N` rewrite lands. Skipped unless PAPABEAR_TEST_POSTGRES_URL points at a reachable
    // Postgres (e.g. `postgres://user:pass@host:5432/db`); it operates only on an isolated
    // row with an ancient timestamp so the prune threshold can never touch real rows.
    #[tokio::test]
    async fn postgres_directory_registers_and_prunes_with_rewritten_placeholders() {
        let Ok(url) = std::env::var("PAPABEAR_TEST_POSTGRES_URL") else {
            eprintln!("skipping: PAPABEAR_TEST_POSTGRES_URL not set (no Postgres to test against)");
            return;
        };
        let directory = match ServerDirectory::open(&url).await {
            Ok(directory) => directory,
            Err(error) => {
                eprintln!(
                    "skipping: cannot reach Postgres at PAPABEAR_TEST_POSTGRES_URL: {error:#}"
                );
                return;
            }
        };

        let server_id = "pg-placeholder-regression";
        // Start clean even if a prior aborted run left the row behind.
        directory.unregister(server_id).await.unwrap();

        let registered = directory
            .register(sample_request(server_id, "PgAlpha", "77"), 1_000)
            .await
            .expect("register must succeed on Postgres after the placeholder rewrite");
        assert_eq!(registered.server_id, server_id);
        assert_eq!(registered.hostname, "PgAlpha");

        // Upsert path (ON CONFLICT DO UPDATE) must bind under Postgres too.
        let updated = directory
            .register(sample_request(server_id, "PgBravo", "78"), 1_500)
            .await
            .unwrap();
        assert_eq!(updated.hostname, "PgBravo");
        assert_eq!(updated.last_seen_unix_ms, 1_500);

        let fetched = directory.get(server_id).await.unwrap();
        assert_eq!(
            fetched.map(|record| record.hostname),
            Some("PgBravo".to_string())
        );

        // Ancient last_seen (1500 ms) → this threshold removes the test row but no real row
        // (real rows carry millisecond timestamps in the 2026 range).
        let removed = directory.prune_stale(2_000).await.unwrap();
        assert!(removed >= 1, "prune must remove the ancient test row");
        assert!(directory.get(server_id).await.unwrap().is_none());
    }

    #[tokio::test]
    async fn observe_flips_to_unreachable_after_one_failed_probe() {
        let temp_dir = tempdir().unwrap();
        let directory = SqliteServerDirectory::open_path(temp_dir.path().join("observe.sqlite"))
            .await
            .unwrap();
        directory
            .register(sample_request("srv-1", "Alpha", "10"), 1000)
            .await
            .unwrap();

        let verified = directory
            .observe("srv-1", 1100, true)
            .await
            .unwrap()
            .unwrap();
        assert_eq!(verified.verification_state, VerificationState::Verified);

        // One failed probe flips it to unreachable so it disappears on the next browse.
        let down = directory
            .observe("srv-1", 1200, false)
            .await
            .unwrap()
            .unwrap();
        assert_eq!(down.verification_state, VerificationState::Unreachable);
        assert_eq!(down.observed_reachable, Some(false));

        // A reachable probe clears the streak; a later miss hides it again.
        let recovered = directory
            .observe("srv-1", 1300, true)
            .await
            .unwrap()
            .unwrap();
        assert_eq!(recovered.verification_state, VerificationState::Verified);
        let after = directory
            .observe("srv-1", 1400, false)
            .await
            .unwrap()
            .unwrap();
        assert_eq!(after.verification_state, VerificationState::Unreachable);
    }

    #[tokio::test]
    async fn default_list_hides_stale_heartbeats() {
        let temp_dir = tempdir().unwrap();
        let directory = SqliteServerDirectory::open_path(temp_dir.path().join("fresh.sqlite"))
            .await
            .unwrap();
        let t = 1_000_000;
        directory
            .register(sample_request("srv-1", "Alpha", "1"), t)
            .await
            .unwrap();

        // Fresh heartbeat -> listed.
        assert_eq!(
            directory
                .list(&ListServersQuery::default(), t)
                .await
                .unwrap()
                .len(),
            1
        );
        // No heartbeat for > 60s -> dropped from the default list (still in DB until prune).
        assert!(directory
            .list(&ListServersQuery::default(), t + 61_000)
            .await
            .unwrap()
            .is_empty());
    }

    #[tokio::test]
    async fn default_list_hides_passworded_servers_unless_requested() {
        let temp_dir = tempdir().unwrap();
        let directory = SqliteServerDirectory::open_path(temp_dir.path().join("passworded.sqlite"))
            .await
            .unwrap();
        let t = 1_000_000;
        directory
            .register(sample_request("srv-open", "Open", "1"), t)
            .await
            .unwrap();

        let mut locked = sample_request("srv-locked", "Locked", "2");
        locked.password = true;
        directory.register(locked, t).await.unwrap();

        let default_rows = directory
            .list(&ListServersQuery::default(), t)
            .await
            .unwrap();
        assert_eq!(default_rows.len(), 1);
        assert_eq!(default_rows[0].server_id, "srv-open");

        let all_rows = directory
            .list(
                &ListServersQuery {
                    include_passworded_servers: Some(true),
                    ..Default::default()
                },
                t,
            )
            .await
            .unwrap();
        assert_eq!(all_rows.len(), 2);
        assert!(all_rows.iter().any(|row| row.server_id == "srv-locked"));
    }

    #[tokio::test]
    async fn directory_filters_by_app_version_and_tag() {
        let temp_dir = tempdir().unwrap();
        let directory = SqliteServerDirectory::open_path(temp_dir.path().join("compat.sqlite"))
            .await
            .unwrap();
        let t = 1_000_000;

        directory
            .register(sample_request("cwr-a", "CWR A", "1"), t)
            .await
            .unwrap();

        let mut other_tag = sample_request("cwr-b", "CWR B", "2");
        other_tag.version_tag = "rc2".to_string();
        directory.register(other_tag, t).await.unwrap();

        let mut other_app = sample_request("other-a", "Other A", "3");
        other_app.app_name = "OTHER".to_string();
        directory.register(other_app, t).await.unwrap();

        let rows = directory
            .list(
                &ListServersQuery {
                    app_name: Some("CWR".to_string()),
                    actver: Some(196),
                    version_tag: Some("rc1".to_string()),
                    include_unverified_servers: Some(true),
                    ..Default::default()
                },
                t,
            )
            .await
            .unwrap();
        assert_eq!(rows.len(), 1);
        assert_eq!(rows[0].server_id, "cwr-a");
    }

    #[tokio::test]
    async fn directory_groups_live_server_versions() {
        let temp_dir = tempdir().unwrap();
        let directory = SqliteServerDirectory::open_path(temp_dir.path().join("groups.sqlite"))
            .await
            .unwrap();
        let t = 1_000_000;
        directory
            .register(sample_request("cwr-a", "CWR A", "1"), t)
            .await
            .unwrap();
        directory
            .register(sample_request("cwr-b", "CWR B", "2"), t)
            .await
            .unwrap();
        let mut other = sample_request("other", "Other", "3");
        other.app_name = "OTHER".to_string();
        other.version_tag = "x1".to_string();
        directory.register(other, t).await.unwrap();

        let groups = directory
            .version_groups(
                &ListServersQuery {
                    include_unverified_servers: Some(true),
                    ..Default::default()
                },
                t,
            )
            .await
            .unwrap();
        assert_eq!(groups.len(), 2);
        assert!(groups.iter().any(|group| group.app_name == "CWR"
            && group.actver == 196
            && group.version_tag == "rc1"
            && group.servers == 2));
        assert!(groups.iter().any(|group| group.app_name == "OTHER"
            && group.actver == 196
            && group.version_tag == "x1"
            && group.servers == 1));
    }

    #[tokio::test]
    async fn default_list_expires_stale_unreachable_verdicts() {
        let temp_dir = tempdir().unwrap();
        let directory = SqliteServerDirectory::open_path(temp_dir.path().join("expire.sqlite"))
            .await
            .unwrap();
        let t = 1_000_000;
        directory
            .register(sample_request("srv-1", "Alpha", "1"), t)
            .await
            .unwrap();
        directory.observe("srv-1", t, false).await.unwrap();

        // Recently confirmed unreachable + fresh heartbeat -> hidden.
        assert!(directory
            .list(&ListServersQuery::default(), t)
            .await
            .unwrap()
            .is_empty());

        // A fresh heartbeat later, with a now-stale unreachable verdict (prober gone),
        // self-heals: the row reappears on heartbeat alone.
        directory
            .register(sample_request("srv-1", "Alpha", "1"), t + 61_000)
            .await
            .unwrap();
        let rows = directory
            .list(&ListServersQuery::default(), t + 61_000)
            .await
            .unwrap();
        assert_eq!(rows.len(), 1);
    }

    #[tokio::test]
    async fn sqlite_directory_persists_records_across_reopen_and_continues_pruning() {
        let temp_dir = tempdir().unwrap();
        let database_path = temp_dir.path().join("directory.sqlite");

        {
            let directory = SqliteServerDirectory::open_path(&database_path)
                .await
                .unwrap();
            let registered = directory
                .register(sample_request("srv-restart", "Warm Start", "14"), 1000)
                .await
                .unwrap();
            assert_eq!(registered.server_id, "srv-restart");
        }

        let reopened = SqliteServerDirectory::open_path(&database_path)
            .await
            .unwrap();
        let listed = reopened
            .list(
                &ListServersQuery {
                    include_unverified_servers: Some(true),
                    ..Default::default()
                },
                1000,
            )
            .await
            .unwrap();
        assert_eq!(listed.len(), 1);
        assert_eq!(listed[0].hostname, "Warm Start");
        assert_eq!(listed[0].last_seen_unix_ms, 1000);

        let updated = reopened
            .register(sample_request("srv-restart", "After Restart", "15"), 3000)
            .await
            .unwrap();
        assert_eq!(updated.hostname, "After Restart");
        assert_eq!(updated.last_seen_unix_ms, 3000);

        let removed = reopened.prune_stale(3500).await.unwrap();
        assert_eq!(removed, 1);
        assert!(reopened
            .list(
                &ListServersQuery {
                    include_unverified_servers: Some(true),
                    ..Default::default()
                },
                3500,
            )
            .await
            .unwrap()
            .is_empty());
    }

    #[tokio::test]
    async fn sqlite_directory_observations_update_state_and_survive_heartbeats() {
        let temp_dir = tempdir().unwrap();
        let database_path = temp_dir.path().join("directory.sqlite");
        let directory = SqliteServerDirectory::open_path(&database_path)
            .await
            .unwrap();

        let first = directory
            .register(sample_request("srv-observe", "Observer Target", "21"), 1000)
            .await
            .unwrap();
        assert_eq!(first.verification_state, VerificationState::SelfReported);
        assert_eq!(first.last_observed_unix_ms, None);
        assert_eq!(first.observed_reachable, None);

        // Additive default lists the self_reported server (only unreachable rows are hidden).
        let default_listed = directory
            .list(&ListServersQuery::default(), 1000)
            .await
            .unwrap();
        assert_eq!(default_listed.len(), 1);
        assert_eq!(
            default_listed[0].verification_state,
            VerificationState::SelfReported
        );

        let observed = directory
            .observe("srv-observe", 1500, true)
            .await
            .unwrap()
            .unwrap();
        assert_eq!(observed.verification_state, VerificationState::Verified);
        assert_eq!(observed.last_observed_unix_ms, Some(1500));
        assert_eq!(observed.observed_reachable, Some(true));

        let updated = directory
            .register(
                sample_request("srv-observe", "Observer Target 2", "22"),
                2000,
            )
            .await
            .unwrap();
        assert_eq!(updated.hostname, "Observer Target 2");
        assert_eq!(updated.verification_state, VerificationState::Verified);
        assert_eq!(updated.last_observed_unix_ms, Some(1500));
        assert_eq!(updated.observed_reachable, Some(true));

        let verified = directory
            .list(
                &ListServersQuery {
                    verification_state: Some(VerificationState::Verified),
                    ..Default::default()
                },
                2000,
            )
            .await
            .unwrap();
        assert_eq!(verified.len(), 1);
        assert_eq!(verified[0].server_id, "srv-observe");

        let self_reported = directory
            .list(
                &ListServersQuery {
                    verification_state: Some(VerificationState::SelfReported),
                    ..Default::default()
                },
                2000,
            )
            .await
            .unwrap();
        assert!(self_reported.is_empty());
    }

    #[tokio::test]
    async fn http_routes_register_heartbeat_list_and_unregister() {
        let temp_dir = tempdir().unwrap();
        let database_path = temp_dir.path().join("directory.sqlite");
        let directory = Arc::new(
            SqliteServerDirectory::open_path(&database_path)
                .await
                .unwrap(),
        );
        let app = build_router(directory);

        let register_request = Request::builder()
            .method("POST")
            .uri("/v1/servers/register")
            .header("content-type", "application/json")
            .body(Body::from(
                serde_json::to_vec(&sample_request("srv-http", "HTTP Server", "12")).unwrap(),
            ))
            .unwrap();
        let register_response = app.clone().oneshot(register_request).await.unwrap();
        assert_eq!(register_response.status(), StatusCode::OK);

        let heartbeat_request = Request::builder()
            .method("POST")
            .uri("/v1/servers/heartbeat")
            .header("content-type", "application/json")
            .body(Body::from(
                serde_json::to_vec(&sample_request("srv-http", "Heartbeat Server", "13")).unwrap(),
            ))
            .unwrap();
        let heartbeat_response = app.clone().oneshot(heartbeat_request).await.unwrap();
        assert_eq!(heartbeat_response.status(), StatusCode::OK);

        let list_response = app
            .clone()
            .oneshot(
                Request::builder()
                    .method("GET")
                    .uri("/v1/servers?hostname=Heart&includeUnverifiedServers=true")
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(list_response.status(), StatusCode::OK);
        let list_body = to_bytes(list_response.into_body(), usize::MAX)
            .await
            .unwrap();
        let servers: Vec<DirectoryServerRecord> = serde_json::from_slice(&list_body).unwrap();
        assert_eq!(servers.len(), 1);
        assert_eq!(servers[0].hostname, "Heartbeat Server");
        assert_eq!(servers[0].address, "192.168.1.13");
        // The publisher's build tag survives the register/heartbeat POST and reappears in the
        // served list (sample_request sets "rc1"). The served JSON key is the wire name "vertag".
        assert_eq!(servers[0].version_tag, "rc1");
        let raw: Vec<Value> = serde_json::from_slice(&list_body).unwrap();
        assert_eq!(raw[0]["vertag"], "rc1");

        let mut locked_request = sample_request("srv-locked-http", "Locked HTTP Server", "14");
        locked_request.password = true;
        let locked_response = app
            .clone()
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/v1/servers/register")
                    .header("content-type", "application/json")
                    .body(Body::from(serde_json::to_vec(&locked_request).unwrap()))
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(locked_response.status(), StatusCode::OK);

        let default_list_response = app
            .clone()
            .oneshot(
                Request::builder()
                    .method("GET")
                    .uri("/v1/servers?includeUnverifiedServers=true")
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(default_list_response.status(), StatusCode::OK);
        let default_list_body = to_bytes(default_list_response.into_body(), usize::MAX)
            .await
            .unwrap();
        let default_servers: Vec<DirectoryServerRecord> =
            serde_json::from_slice(&default_list_body).unwrap();
        assert_eq!(default_servers.len(), 1);
        assert_eq!(default_servers[0].server_id, "srv-http");

        let include_passworded_response = app
            .clone()
            .oneshot(
                Request::builder()
                    .method("GET")
                    .uri("/v1/servers?includeUnverifiedServers=true&includePasswordedServers=true")
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(include_passworded_response.status(), StatusCode::OK);
        let include_passworded_body = to_bytes(include_passworded_response.into_body(), usize::MAX)
            .await
            .unwrap();
        let include_passworded_servers: Vec<DirectoryServerRecord> =
            serde_json::from_slice(&include_passworded_body).unwrap();
        assert_eq!(include_passworded_servers.len(), 2);
        assert!(include_passworded_servers
            .iter()
            .any(|server| server.server_id == "srv-locked-http"));

        let prune_response = app
            .clone()
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/v1/servers/prune")
                    .header("content-type", "application/json")
                    .body(Body::from(
                        serde_json::to_vec(&json!({ "maxAgeMs": 0 })).unwrap(),
                    ))
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(prune_response.status(), StatusCode::OK);

        let delete_response = app
            .oneshot(
                Request::builder()
                    .method("DELETE")
                    .uri("/v1/servers/srv-http")
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();
        assert!(matches!(
            delete_response.status(),
            StatusCode::NO_CONTENT | StatusCode::NOT_FOUND
        ));
    }

    // Wire contract with the C++ publisher: BuildMasterServerServiceRegistrationJson emits the
    // build tag under the key "vertag". A registration body with {"vertag":"rc2"} must ingest it
    // and the served list row must carry it back under the same key. Broken state: without the
    // model field + repository column/bind/select the served "vertag" is absent (defaults empty).
    #[tokio::test]
    async fn http_register_ingests_vertag_from_raw_publisher_json() {
        let temp_dir = tempdir().unwrap();
        let database_path = temp_dir.path().join("directory.sqlite");
        let directory = Arc::new(
            SqliteServerDirectory::open_path(&database_path)
                .await
                .unwrap(),
        );
        let app = build_router(directory);

        // Exactly the field set the C++ client sends, with vertag present.
        let body = json!({
            "serverId": "10.0.0.5:2302",
            "address": "10.0.0.5",
            "hostport": 2302,
            "hostname": "Raw Server",
            "gametype": "coop",
            "app": "CWR",
            "actver": 196,
            "reqver": 196,
            "vertag": "rc2",
            "state": 14,
            "numplayers": 1,
            "maxplayers": 8,
            "password": false,
            "mod": "synthetic-core",
            "equalModRequired": false,
            "impl": "papa-bear",
            "platform": "win"
        });
        let register = app
            .clone()
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/v1/servers/register")
                    .header("content-type", "application/json")
                    .body(Body::from(serde_json::to_vec(&body).unwrap()))
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(register.status(), StatusCode::OK);

        let list = app
            .oneshot(
                Request::builder()
                    .method("GET")
                    .uri("/v1/servers?includeUnverifiedServers=true")
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(list.status(), StatusCode::OK);
        let list_body = to_bytes(list.into_body(), usize::MAX).await.unwrap();
        let rows: Vec<Value> = serde_json::from_slice(&list_body).unwrap();
        assert_eq!(rows.len(), 1);
        assert_eq!(rows[0]["app"], "CWR");
        assert_eq!(rows[0]["vertag"], "rc2");
    }

    #[tokio::test]
    async fn http_list_filters_by_explicit_app_version_query() {
        let temp_dir = tempdir().unwrap();
        let directory = Arc::new(
            SqliteServerDirectory::open_path(temp_dir.path().join("directory.sqlite"))
                .await
                .unwrap(),
        );
        let app = build_router(directory);

        let register = |request: RegisterServerRequest| {
            Request::builder()
                .method("POST")
                .uri("/v1/servers/register")
                .header("content-type", "application/json")
                .body(Body::from(serde_json::to_vec(&request).unwrap()))
                .unwrap()
        };

        app.clone()
            .oneshot(register(sample_request("cwr-a", "CWR A", "1")))
            .await
            .unwrap();
        let mut other_tag = sample_request("cwr-b", "CWR B", "2");
        other_tag.version_tag = "rc2".to_string();
        app.clone().oneshot(register(other_tag)).await.unwrap();
        let mut other_app = sample_request("other-a", "Other A", "3");
        other_app.app_name = "OTHER".to_string();
        app.clone().oneshot(register(other_app)).await.unwrap();

        let response = app
            .oneshot(
                Request::builder()
                    .method("GET")
                    .uri("/v1/servers?includeUnverifiedServers=true&app=CWR&actver=196&vertag=rc1")
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(response.status(), StatusCode::OK);
        let body = to_bytes(response.into_body(), usize::MAX).await.unwrap();
        let servers: Vec<DirectoryServerRecord> = serde_json::from_slice(&body).unwrap();
        assert_eq!(servers.len(), 1);
        assert_eq!(servers[0].server_id, "cwr-a");
    }

    #[tokio::test]
    async fn http_list_ignores_user_agent_compatibility_without_explicit_query() {
        let temp_dir = tempdir().unwrap();
        let directory = Arc::new(
            SqliteServerDirectory::open_path(temp_dir.path().join("directory.sqlite"))
                .await
                .unwrap(),
        );
        let app = build_router(directory);

        let mut other_tag = sample_request("cwr-b", "CWR B", "2");
        other_tag.version_tag = "rc2".to_string();
        for request in [sample_request("cwr-a", "CWR A", "1"), other_tag] {
            app.clone()
                .oneshot(
                    Request::builder()
                        .method("POST")
                        .uri("/v1/servers/register")
                        .header("content-type", "application/json")
                        .body(Body::from(serde_json::to_vec(&request).unwrap()))
                        .unwrap(),
                )
                .await
                .unwrap();
        }

        let response = app
            .oneshot(
                Request::builder()
                    .method("GET")
                    .uri("/v1/servers?includeUnverifiedServers=true")
                    .header("user-agent", "CWR/196 (tag=rc1; role=client)")
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(response.status(), StatusCode::OK);
        let body = to_bytes(response.into_body(), usize::MAX).await.unwrap();
        let servers: Vec<DirectoryServerRecord> = serde_json::from_slice(&body).unwrap();
        assert_eq!(servers.len(), 2);
        assert!(servers.iter().any(|server| server.server_id == "cwr-a"));
        assert!(servers.iter().any(|server| server.server_id == "cwr-b"));
    }

    #[tokio::test]
    async fn http_list_returns_303_server_to_302_discovery_client() {
        let temp_dir = tempdir().unwrap();
        let directory = Arc::new(
            SqliteServerDirectory::open_path(temp_dir.path().join("directory.sqlite"))
                .await
                .unwrap(),
        );
        let app = build_router(directory);

        let mut server = sample_request("cwr-303", "CWR 3.03 Server", "3");
        server.actver = 303;
        server.reqver = 303;
        server.version_tag = "rc303".to_string();
        let registered = app
            .clone()
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/v1/servers/register")
                    .header("content-type", "application/json")
                    .body(Body::from(serde_json::to_vec(&server).unwrap()))
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(registered.status(), StatusCode::OK);

        let response = app
            .oneshot(
                Request::builder()
                    .method("GET")
                    .uri("/v1/servers?includeUnverifiedServers=true&app=CWR")
                    .header("user-agent", "CWR/302 (tag=rc302; role=client)")
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(response.status(), StatusCode::OK);
        let body = to_bytes(response.into_body(), usize::MAX).await.unwrap();
        let servers: Vec<DirectoryServerRecord> = serde_json::from_slice(&body).unwrap();
        assert_eq!(servers.len(), 1);
        assert_eq!(servers[0].server_id, "cwr-303");
        assert_eq!(servers[0].actver, 303);
        assert_eq!(servers[0].reqver, 303);
        assert_eq!(servers[0].version_tag, "rc303");
    }

    #[tokio::test]
    async fn http_versions_groups_are_available_for_browser_selector() {
        let temp_dir = tempdir().unwrap();
        let directory = Arc::new(
            SqliteServerDirectory::open_path(temp_dir.path().join("directory.sqlite"))
                .await
                .unwrap(),
        );
        let app = build_router(directory);

        for request in [
            sample_request("cwr-a", "CWR A", "1"),
            sample_request("cwr-b", "CWR B", "2"),
        ] {
            app.clone()
                .oneshot(
                    Request::builder()
                        .method("POST")
                        .uri("/v1/servers/register")
                        .header("content-type", "application/json")
                        .body(Body::from(serde_json::to_vec(&request).unwrap()))
                        .unwrap(),
                )
                .await
                .unwrap();
        }

        let response = app
            .oneshot(
                Request::builder()
                    .method("GET")
                    .uri("/v1/servers/versions?includeUnverifiedServers=true")
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(response.status(), StatusCode::OK);
        let body = to_bytes(response.into_body(), usize::MAX).await.unwrap();
        let groups: Vec<crate::model::ServerVersionGroup> = serde_json::from_slice(&body).unwrap();
        assert_eq!(groups.len(), 1);
        assert_eq!(groups[0].app_name, "CWR");
        assert_eq!(groups[0].actver, 196);
        assert_eq!(groups[0].version_tag, "rc1");
        assert_eq!(groups[0].servers, 2);
    }

    #[tokio::test]
    async fn http_healthz_returns_ok_without_touching_the_database() {
        let temp_dir = tempdir().unwrap();
        let database_path = temp_dir.path().join("directory.sqlite");
        let directory = Arc::new(
            SqliteServerDirectory::open_path(&database_path)
                .await
                .unwrap(),
        );
        let app = build_router(directory);

        let response = app
            .oneshot(
                Request::builder()
                    .method("GET")
                    .uri("/healthz")
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(response.status(), StatusCode::OK);
        let body = to_bytes(response.into_body(), usize::MAX).await.unwrap();
        let payload: Value = serde_json::from_slice(&body).unwrap();
        assert_eq!(payload, json!({ "status": "ok" }));
    }

    fn multipart_body(
        boundary: &str,
        fields: &[(&str, &str)],
        file: Option<(&str, &[u8])>,
    ) -> Vec<u8> {
        let mut body = Vec::new();
        for (name, value) in fields {
            body.extend_from_slice(format!("--{boundary}\r\n").as_bytes());
            body.extend_from_slice(
                format!("Content-Disposition: form-data; name=\"{name}\"\r\n\r\n").as_bytes(),
            );
            body.extend_from_slice(value.as_bytes());
            body.extend_from_slice(b"\r\n");
        }
        if let Some((name, bytes)) = file {
            body.extend_from_slice(format!("--{boundary}\r\n").as_bytes());
            body.extend_from_slice(
                format!(
                    "Content-Disposition: form-data; name=\"{name}\"; filename=\"upload.pbo\"\r\n\
                     Content-Type: application/octet-stream\r\n\r\n"
                )
                .as_bytes(),
            );
            body.extend_from_slice(bytes);
            body.extend_from_slice(b"\r\n");
        }
        body.extend_from_slice(format!("--{boundary}--\r\n").as_bytes());
        body
    }

    #[tokio::test]
    async fn http_mod_publish_requires_admin_key_then_lists_and_downloads() {
        let temp_dir = tempdir().unwrap();
        let directory = Arc::new(
            SqliteServerDirectory::open_path(temp_dir.path().join("d.sqlite"))
                .await
                .unwrap(),
        );
        let mods_dir = temp_dir.path().join("mods");
        let app = build_router_with_options(
            directory,
            Some("secret-key".to_string()),
            Some(mods_dir.clone()),
            None,
            false,
        );

        let boundary = "papabearboundary";
        // Arbitrary bytes stand in for a packed PBO; download must return them verbatim.
        let artifact: &[u8] = b"\x00PBO-ish bytes \x01\x02\xff payload";
        let content_type = format!("multipart/form-data; boundary={boundary}");
        let make_body = || {
            multipart_body(
                boundary,
                &[
                    ("name", "Synthetic Upload"),
                    ("app", "CWR"),
                    ("actver", "302"),
                    ("vertag", "rc1"),
                    ("author", "simi"),
                    ("description", "a mod"),
                ],
                Some(("file", artifact)),
            )
        };

        // No admin key -> 401, nothing written.
        let unauth = app
            .clone()
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/v1/mods")
                    .header("content-type", &content_type)
                    .body(Body::from(make_body()))
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(unauth.status(), StatusCode::UNAUTHORIZED);
        assert!(!mods_dir.exists() || fs::read_dir(&mods_dir).unwrap().next().is_none());

        // Wrong key -> 401.
        let wrong = app
            .clone()
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/v1/mods")
                    .header("content-type", &content_type)
                    .header("x-api-key", "nope")
                    .body(Body::from(make_body()))
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(wrong.status(), StatusCode::UNAUTHORIZED);

        // Correct key -> 200, returns the stored catalog entry.
        let ok = app
            .clone()
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/v1/mods")
                    .header("content-type", &content_type)
                    .header("x-api-key", "secret-key")
                    .body(Body::from(make_body()))
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(ok.status(), StatusCode::OK);
        let entry: ModCatalogEntry =
            serde_json::from_slice(&to_bytes(ok.into_body(), usize::MAX).await.unwrap()).unwrap();
        assert_eq!(entry.name, "Synthetic Upload");
        assert!(
            entry.mod_id.starts_with("synthetic-upload-"),
            "unexpected mod_id {}",
            entry.mod_id
        );
        assert_eq!(entry.version.len(), 8); // default version = sha256[..8] hex
        assert_eq!(entry.app_name.as_deref(), Some("CWR"));
        assert_eq!(entry.actver, Some(302));
        assert_eq!(entry.version_tag.as_deref(), Some("rc1"));
        assert_eq!(entry.authors, vec!["simi".to_string()]);
        assert_eq!(
            entry.download_url.as_deref(),
            Some(format!("/v1/mods/{}/download", entry.mod_id).as_str())
        );
        assert!(mods_dir.join(&entry.mod_id).join("mod.json").is_file());
        assert!(mods_dir
            .join(&entry.mod_id)
            .join(format!("{}.pbo.zst", entry.mod_id))
            .is_file());

        // The catalog scan now lists it.
        let list = app
            .clone()
            .oneshot(
                Request::builder()
                    .method("GET")
                    .uri("/v1/mods?app=CWR&actver=302&vertag=rc1")
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(list.status(), StatusCode::OK);
        let mods: Vec<ModCatalogEntry> =
            serde_json::from_slice(&to_bytes(list.into_body(), usize::MAX).await.unwrap()).unwrap();
        let listed = mods.iter().find(|m| m.mod_id == entry.mod_id).unwrap();
        assert!(listed.compatible);

        // The publisher uploads an already-compressed artifact; the service stores and serves it
        // verbatim, so the download is byte-for-byte what was uploaded.
        let download = app
            .clone()
            .oneshot(
                Request::builder()
                    .method("GET")
                    .uri(format!("/v1/mods/{}/download", entry.mod_id))
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(download.status(), StatusCode::OK);
        let downloaded = to_bytes(download.into_body(), usize::MAX).await.unwrap();
        assert_eq!(downloaded.as_ref(), artifact);
    }

    #[tokio::test]
    async fn http_mod_delete_is_admin_gated_and_removes_from_store() {
        let temp_dir = tempdir().unwrap();
        let directory = Arc::new(
            SqliteServerDirectory::open_path(temp_dir.path().join("d.sqlite"))
                .await
                .unwrap(),
        );
        let mods_dir = temp_dir.path().join("mods");
        let app = build_router_with_options(
            directory,
            Some("secret-key".to_string()),
            Some(mods_dir.clone()),
            None,
            false,
        );

        // Publish a mod (admin) to delete.
        let boundary = "pbdelboundary";
        let publish = app
            .clone()
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/v1/mods")
                    .header(
                        "content-type",
                        format!("multipart/form-data; boundary={boundary}"),
                    )
                    .header("x-api-key", "secret-key")
                    .body(Body::from(multipart_body(
                        boundary,
                        &[("name", "Doomed Mod")],
                        Some(("file", b"artifact-bytes")),
                    )))
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(publish.status(), StatusCode::OK);
        let entry: ModCatalogEntry =
            serde_json::from_slice(&to_bytes(publish.into_body(), usize::MAX).await.unwrap())
                .unwrap();
        let delete_uri = format!("/v1/mods/{}", entry.mod_id);

        // No key -> 401, mod still present on disk.
        let unauth = app
            .clone()
            .oneshot(
                Request::builder()
                    .method("DELETE")
                    .uri(&delete_uri)
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(unauth.status(), StatusCode::UNAUTHORIZED);
        assert!(mods_dir.join(&entry.mod_id).join("mod.json").is_file());

        // Wrong key -> 401.
        let wrong = app
            .clone()
            .oneshot(
                Request::builder()
                    .method("DELETE")
                    .uri(&delete_uri)
                    .header("x-api-key", "nope")
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(wrong.status(), StatusCode::UNAUTHORIZED);

        // Correct key -> 204, and the artifact + mod.json are gone from the store.
        let ok = app
            .clone()
            .oneshot(
                Request::builder()
                    .method("DELETE")
                    .uri(&delete_uri)
                    .header("x-api-key", "secret-key")
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(ok.status(), StatusCode::NO_CONTENT);
        assert!(!mods_dir.join(&entry.mod_id).join("mod.json").exists());
        assert!(!mods_dir
            .join(&entry.mod_id)
            .join(format!("{}.pbo", entry.mod_id))
            .exists());

        // Gone from the catalog, and download now 404s.
        let download = app
            .clone()
            .oneshot(
                Request::builder()
                    .method("GET")
                    .uri(format!("/v1/mods/{}/download", entry.mod_id))
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(download.status(), StatusCode::NOT_FOUND);

        // Deleting again -> 404 (nothing to delete).
        let again = app
            .clone()
            .oneshot(
                Request::builder()
                    .method("DELETE")
                    .uri(&delete_uri)
                    .header("x-api-key", "secret-key")
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(again.status(), StatusCode::NOT_FOUND);
    }

    #[tokio::test]
    async fn mod_download_url_is_absolutised_from_request_headers() {
        // The stored manifest keeps a server-relative downloadUrl; the catalog endpoints
        // must serve it absolute (libcurl rejects a relative URL), derived from the
        // request Host + X-Forwarded-Proto so it works behind a TLS-terminating proxy.
        let temp_dir = tempdir().unwrap();
        let directory = Arc::new(
            SqliteServerDirectory::open_path(temp_dir.path().join("d.sqlite"))
                .await
                .unwrap(),
        );
        let mods_dir = temp_dir.path().join("mods");
        let app = build_router_with_options(
            directory,
            Some("secret-key".to_string()),
            Some(mods_dir.clone()),
            None,
            false,
        );

        let boundary = "papabearboundary";
        let artifact: &[u8] = b"payload";
        let content_type = format!("multipart/form-data; boundary={boundary}");
        let publish = app
            .clone()
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/v1/mods")
                    .header("content-type", &content_type)
                    .header("x-api-key", "secret-key")
                    .body(Body::from(multipart_body(
                        boundary,
                        &[("name", "Absolute Test")],
                        Some(("file", artifact)),
                    )))
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(publish.status(), StatusCode::OK);
        let published: ModCatalogEntry =
            serde_json::from_slice(&to_bytes(publish.into_body(), usize::MAX).await.unwrap())
                .unwrap();
        let mod_id = published.mod_id;

        let list_mods = |host: Option<&'static str>, proto: Option<&'static str>| {
            let app = app.clone();
            async move {
                let mut builder = Request::builder().method("GET").uri("/v1/mods");
                if let Some(host) = host {
                    builder = builder.header("host", host);
                }
                if let Some(proto) = proto {
                    builder = builder.header("x-forwarded-proto", proto);
                }
                let response = app
                    .oneshot(builder.body(Body::empty()).unwrap())
                    .await
                    .unwrap();
                assert_eq!(response.status(), StatusCode::OK);
                let mods: Vec<ModCatalogEntry> = serde_json::from_slice(
                    &to_bytes(response.into_body(), usize::MAX).await.unwrap(),
                )
                .unwrap();
                mods.into_iter().next().unwrap().download_url
            }
        };

        // Host header alone -> http base.
        assert_eq!(
            list_mods(Some("papa-bear.cz"), None).await.as_deref(),
            Some(format!("http://papa-bear.cz/v1/mods/{mod_id}/download").as_str())
        );

        // X-Forwarded-Proto from the proxy -> https base.
        assert_eq!(
            list_mods(Some("papa-bear.cz"), Some("https"))
                .await
                .as_deref(),
            Some(format!("https://papa-bear.cz/v1/mods/{mod_id}/download").as_str())
        );

        // No Host header (direct/unknown) -> left relative, as stored.
        assert_eq!(
            list_mods(None, None).await.as_deref(),
            Some(format!("/v1/mods/{mod_id}/download").as_str())
        );

        // The detail endpoint absolutises too.
        let detail = app
            .clone()
            .oneshot(
                Request::builder()
                    .method("GET")
                    .uri(format!("/v1/mods/{mod_id}"))
                    .header("host", "papa-bear.cz")
                    .header("x-forwarded-proto", "https")
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(detail.status(), StatusCode::OK);
        let detail: ModCatalogEntry =
            serde_json::from_slice(&to_bytes(detail.into_body(), usize::MAX).await.unwrap())
                .unwrap();
        assert_eq!(
            detail.download_url.as_deref(),
            Some(format!("https://papa-bear.cz/v1/mods/{mod_id}/download").as_str())
        );
    }

    #[tokio::test]
    async fn register_binds_identity_to_forwarded_client_ip() {
        let temp_dir = tempdir().unwrap();
        let directory = Arc::new(
            SqliteServerDirectory::open_path(temp_dir.path().join("b.sqlite"))
                .await
                .unwrap(),
        );
        let app = build_router_with_options(
            directory,
            None,
            None,
            Some("x-forwarded-for".to_string()),
            false,
        );

        // The body claims a different id/address; the trusted header must win (anti-spoof).
        let register = app
            .clone()
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/v1/servers/register")
                    .header("content-type", "application/json")
                    .header("x-forwarded-for", "203.0.113.9, 10.0.0.1")
                    .body(Body::from(
                        serde_json::to_vec(&sample_request("claimed-id", "Spoofy", "42")).unwrap(),
                    ))
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(register.status(), StatusCode::OK);
        let body = to_bytes(register.into_body(), usize::MAX).await.unwrap();
        let record: DirectoryServerRecord = serde_json::from_slice(&body).unwrap();
        assert_eq!(record.address, "203.0.113.9");
        assert_eq!(record.server_id, "203.0.113.9:2302");

        let mut public_body = sample_request("138.68.88.29:1985", "Public Body", "43");
        public_body.address = "138.68.88.29".to_string();
        public_body.hostport = 1985;
        let private_forwarded = app
            .clone()
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/v1/servers/register")
                    .header("content-type", "application/json")
                    .header("x-forwarded-for", "10.114.0.3, 10.244.0.1")
                    .body(Body::from(serde_json::to_vec(&public_body).unwrap()))
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(private_forwarded.status(), StatusCode::OK);
        let body = to_bytes(private_forwarded.into_body(), usize::MAX)
            .await
            .unwrap();
        let record: DirectoryServerRecord = serde_json::from_slice(&body).unwrap();
        assert_eq!(record.address, "138.68.88.29");
        assert_eq!(record.server_id, "138.68.88.29:1985");

        let mut peer_body = sample_request("claimed-peer", "Peer", "44");
        peer_body.hostport = 1986;
        let peer_register = app
            .clone()
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/v1/servers/register")
                    .header("content-type", "application/json")
                    .extension(ConnectInfo(SocketAddr::new(
                        IpAddr::V4(Ipv4Addr::new(198, 51, 100, 12)),
                        52_000,
                    )))
                    .body(Body::from(serde_json::to_vec(&peer_body).unwrap()))
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(peer_register.status(), StatusCode::OK);
        let body = to_bytes(peer_register.into_body(), usize::MAX)
            .await
            .unwrap();
        let record: DirectoryServerRecord = serde_json::from_slice(&body).unwrap();
        assert_eq!(record.address, "198.51.100.12");
        assert_eq!(record.server_id, "198.51.100.12:1986");

        let private_only_delete = app
            .clone()
            .oneshot(
                Request::builder()
                    .method("DELETE")
                    .uri("/v1/servers/138.68.88.29:1985")
                    .header("x-forwarded-for", "10.114.0.3, 10.244.0.1")
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(private_only_delete.status(), StatusCode::FORBIDDEN);

        // A DELETE from a different IP is rejected; from the owning IP it succeeds.
        let forbidden = app
            .clone()
            .oneshot(
                Request::builder()
                    .method("DELETE")
                    .uri("/v1/servers/203.0.113.9:2302")
                    .header("x-forwarded-for", "198.51.100.7")
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(forbidden.status(), StatusCode::FORBIDDEN);

        let removed = app
            .oneshot(
                Request::builder()
                    .method("DELETE")
                    .uri("/v1/servers/203.0.113.9:2302")
                    .header("x-forwarded-for", "203.0.113.9")
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(removed.status(), StatusCode::NO_CONTENT);
    }

    #[tokio::test]
    async fn server_token_gates_heartbeat_and_delete_when_enforced() {
        let temp_dir = tempdir().unwrap();
        let directory = Arc::new(
            SqliteServerDirectory::open_path(temp_dir.path().join("t.sqlite"))
                .await
                .unwrap(),
        );
        let app = build_router_with_options(directory, None, None, None, true); // enforcement on

        let register_body = serde_json::to_vec(&sample_request("srv-tok", "Alpha", "1")).unwrap();
        let post = |uri: &str, token: Option<&str>| {
            let mut builder = Request::builder()
                .method("POST")
                .uri(uri.to_string())
                .header("content-type", "application/json");
            if let Some(token) = token {
                builder = builder.header("authorization", format!("Bearer {token}"));
            }
            builder.body(Body::from(register_body.clone())).unwrap()
        };

        // Register (new) -> 200 and a token is issued.
        let registered = app
            .clone()
            .oneshot(post("/v1/servers/register", None))
            .await
            .unwrap();
        assert_eq!(registered.status(), StatusCode::OK);
        let body = to_bytes(registered.into_body(), usize::MAX).await.unwrap();
        let record: DirectoryServerRecord = serde_json::from_slice(&body).unwrap();
        let token = record.token.expect("register issues a token");

        // Heartbeat on the fresh row without / with a wrong token -> 401.
        let no_token = app
            .clone()
            .oneshot(post("/v1/servers/heartbeat", None))
            .await
            .unwrap();
        assert_eq!(no_token.status(), StatusCode::UNAUTHORIZED);
        let wrong = app
            .clone()
            .oneshot(post("/v1/servers/heartbeat", Some("deadbeef")))
            .await
            .unwrap();
        assert_eq!(wrong.status(), StatusCode::UNAUTHORIZED);

        // Heartbeat with the issued token -> 200, and no new token is minted.
        let ok = app
            .clone()
            .oneshot(post("/v1/servers/heartbeat", Some(&token)))
            .await
            .unwrap();
        assert_eq!(ok.status(), StatusCode::OK);
        let ok_body = to_bytes(ok.into_body(), usize::MAX).await.unwrap();
        let ok_record: DirectoryServerRecord = serde_json::from_slice(&ok_body).unwrap();
        assert_eq!(ok_record.token, None);

        // DELETE requires the token too.
        let delete = |token: Option<&str>| {
            let mut builder = Request::builder()
                .method("DELETE")
                .uri("/v1/servers/srv-tok");
            if let Some(token) = token {
                builder = builder.header("authorization", format!("Bearer {token}"));
            }
            builder.body(Body::empty()).unwrap()
        };
        let delete_no_token = app.clone().oneshot(delete(None)).await.unwrap();
        assert_eq!(delete_no_token.status(), StatusCode::UNAUTHORIZED);
        let delete_ok = app.oneshot(delete(Some(&token))).await.unwrap();
        assert_eq!(delete_ok.status(), StatusCode::NO_CONTENT);
    }

    #[tokio::test]
    async fn http_prune_requires_admin_api_key_when_configured() {
        let temp_dir = tempdir().unwrap();
        let database_path = temp_dir.path().join("directory.sqlite");
        let directory = Arc::new(
            SqliteServerDirectory::open_path(&database_path)
                .await
                .unwrap(),
        );
        let app = build_router_with_admin_key(directory, Some("bear-secret".to_string()));

        let unauthorized_response = app
            .clone()
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/v1/servers/prune")
                    .header("content-type", "application/json")
                    .body(Body::from(
                        serde_json::to_vec(&json!({ "maxAgeMs": 0 })).unwrap(),
                    ))
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(unauthorized_response.status(), StatusCode::UNAUTHORIZED);

        let authorized_response = app
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/v1/servers/prune")
                    .header("content-type", "application/json")
                    .header("x-api-key", "bear-secret")
                    .body(Body::from(
                        serde_json::to_vec(&json!({ "maxAgeMs": 0 })).unwrap(),
                    ))
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(authorized_response.status(), StatusCode::OK);
    }

    #[tokio::test]
    async fn http_observe_updates_server_verification_state() {
        let temp_dir = tempdir().unwrap();
        let database_path = temp_dir.path().join("directory.sqlite");
        let directory = Arc::new(
            SqliteServerDirectory::open_path(&database_path)
                .await
                .unwrap(),
        );
        let app = build_router_with_admin_key(directory, Some("bear-secret".to_string()));

        let register_response = app
            .clone()
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/v1/servers/register")
                    .header("content-type", "application/json")
                    .body(Body::from(
                        serde_json::to_vec(&sample_request(
                            "srv-observe-http",
                            "HTTP Observe",
                            "31",
                        ))
                        .unwrap(),
                    ))
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(register_response.status(), StatusCode::OK);

        let public_list_response = app
            .clone()
            .oneshot(
                Request::builder()
                    .method("GET")
                    .uri("/v1/servers")
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(public_list_response.status(), StatusCode::OK);
        let public_list_body = to_bytes(public_list_response.into_body(), usize::MAX)
            .await
            .unwrap();
        let public_rows: Vec<DirectoryServerRecord> =
            serde_json::from_slice(&public_list_body).unwrap();
        // Additive default: a freshly registered (self_reported) server is listed; only
        // probe-confirmed unreachable rows are hidden.
        assert_eq!(public_rows.len(), 1);
        assert_eq!(
            public_rows[0].verification_state,
            VerificationState::SelfReported
        );

        let observe_response = app
            .clone()
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/v1/servers/srv-observe-http/observe")
                    .header("content-type", "application/json")
                    .header("x-api-key", "bear-secret")
                    .body(Body::from(
                        serde_json::to_vec(&ObserveServerRequest {
                            reachable: true,
                            observed_unix_ms: Some(4321),
                        })
                        .unwrap(),
                    ))
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(observe_response.status(), StatusCode::OK);
        let observe_body = to_bytes(observe_response.into_body(), usize::MAX)
            .await
            .unwrap();
        let observed: DirectoryServerRecord = serde_json::from_slice(&observe_body).unwrap();
        assert_eq!(observed.verification_state, VerificationState::Verified);
        assert_eq!(observed.last_observed_unix_ms, Some(4321));
        assert_eq!(observed.observed_reachable, Some(true));

        let list_response = app
            .oneshot(
                Request::builder()
                    .method("GET")
                    .uri("/v1/servers?verificationState=verified")
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(list_response.status(), StatusCode::OK);
        let list_body = to_bytes(list_response.into_body(), usize::MAX)
            .await
            .unwrap();
        let rows: Vec<DirectoryServerRecord> = serde_json::from_slice(&list_body).unwrap();
        assert_eq!(rows.len(), 1);
        assert_eq!(rows[0].server_id, "srv-observe-http");
    }

    #[tokio::test]
    async fn local_mods_catalog_lists_filters_and_limits_entries() {
        let temp_dir = tempdir().unwrap();
        let mods_root = temp_dir.path().join("mods");
        write_mod_metadata(&mods_root, "synthetic-core", "Synthetic Core Pack");
        write_mod_metadata(&mods_root, "effects-pack", "Effects Pack");

        let directory_path = temp_dir.path().join("directory.sqlite");
        let directory = Arc::new(
            SqliteServerDirectory::open_path(&directory_path)
                .await
                .unwrap(),
        );
        let service = crate::service::PapaBearService::with_mods_root(directory, Some(mods_root));

        let filtered = service
            .list_mods(&ListModsQuery {
                q: Some("synthetic".to_string()),
                ..ListModsQuery::default()
            })
            .await
            .unwrap();
        assert_eq!(filtered.len(), 1);
        assert_eq!(filtered[0].mod_id, "synthetic-core");

        let limited = service
            .list_mods(&ListModsQuery {
                limit: Some(1),
                ..ListModsQuery::default()
            })
            .await
            .unwrap();
        assert_eq!(limited.len(), 1);
    }

    #[tokio::test]
    async fn local_mods_catalog_filters_and_marks_current_game_compatibility() {
        let temp_dir = tempdir().unwrap();
        let mods_root = temp_dir.path().join("mods");
        write_mod_metadata_for_game(
            &mods_root,
            "cwr-effects",
            "CWR Effects",
            "1.0.0",
            None,
            "CWR",
            303,
            Some("rc1"),
        );
        write_mod_metadata_for_game(
            &mods_root,
            "ofp-effects",
            "OFP Effects",
            "1.0.0",
            None,
            "OFP",
            196,
            None,
        );
        write_mod_metadata_for_game(
            &mods_root,
            "future-effects",
            "Future Effects",
            "1.0.0",
            None,
            "CWR",
            304,
            None,
        );

        let directory_path = temp_dir.path().join("directory.sqlite");
        let directory = Arc::new(
            SqliteServerDirectory::open_path(&directory_path)
                .await
                .unwrap(),
        );
        let service = crate::service::PapaBearService::with_mods_root(directory, Some(mods_root));

        let mods = service
            .list_mods(&ListModsQuery {
                app_name: Some("CWR".to_string()),
                actver: Some(303),
                version_tag: Some("rc1".to_string()),
                q: Some("effects".to_string()),
                ..ListModsQuery::default()
            })
            .await
            .unwrap();

        assert_eq!(mods.len(), 1);
        assert_eq!(mods[0].mod_id, "cwr-effects");
        assert!(mods[0].compatible);
    }

    #[tokio::test]
    async fn http_routes_list_and_fetch_mods() {
        let temp_dir = tempdir().unwrap();
        let database_path = temp_dir.path().join("directory.sqlite");
        let mods_root = temp_dir.path().join("mods");
        write_mod_metadata(&mods_root, "synthetic-core", "Synthetic Core Pack");
        write_mod_metadata(&mods_root, "effects-pack", "Effects Pack");
        write_mod_metadata_with_version(
            &mods_root,
            "effects-pack-classic",
            "Effects Pack",
            "0.9.0",
            Some("https://example.invalid/effects-pack"),
        );

        let directory = Arc::new(
            SqliteServerDirectory::open_path(&database_path)
                .await
                .unwrap(),
        );
        let app = build_router_with_options(directory, None, Some(mods_root), None, false);

        let list_response = app
            .clone()
            .oneshot(
                Request::builder()
                    .method("GET")
                    .uri("/v1/mods?q=effects")
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(list_response.status(), StatusCode::OK);
        let list_body = to_bytes(list_response.into_body(), usize::MAX)
            .await
            .unwrap();
        let mods: Vec<ModCatalogEntry> = serde_json::from_slice(&list_body).unwrap();
        assert_eq!(mods.len(), 2);
        assert_eq!(mods[0].mod_id, "effects-pack");
        assert_eq!(mods[1].mod_id, "effects-pack-classic");

        let detail_response = app
            .clone()
            .oneshot(
                Request::builder()
                    .method("GET")
                    .uri("/v1/mods/synthetic-core")
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(detail_response.status(), StatusCode::OK);
        let detail_body = to_bytes(detail_response.into_body(), usize::MAX)
            .await
            .unwrap();
        let detail: ModCatalogEntry = serde_json::from_slice(&detail_body).unwrap();
        assert_eq!(detail.name, "Synthetic Core Pack");
        assert_eq!(
            detail.download_url.as_deref(),
            Some("https://example.invalid/synthetic-core.zip")
        );

        let versions_response = app
            .clone()
            .oneshot(
                Request::builder()
                    .method("GET")
                    .uri("/v1/mods/effects-pack/versions")
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(versions_response.status(), StatusCode::OK);
        let versions_body = to_bytes(versions_response.into_body(), usize::MAX)
            .await
            .unwrap();
        let versions: Vec<ModCatalogEntry> = serde_json::from_slice(&versions_body).unwrap();
        assert_eq!(versions.len(), 2);
        assert_eq!(versions[0].mod_id, "effects-pack");
        assert_eq!(versions[1].mod_id, "effects-pack-classic");

        let usage_response = app
            .oneshot(
                Request::builder()
                    .method("GET")
                    .uri("/v1/mods/effects-pack/servers")
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(usage_response.status(), StatusCode::OK);
        let usage_body = to_bytes(usage_response.into_body(), usize::MAX)
            .await
            .unwrap();
        let usage: Vec<ModUsageServer> = serde_json::from_slice(&usage_body).unwrap();
        assert!(usage.is_empty());
    }

    #[tokio::test]
    async fn http_routes_fetch_server_detail_with_players_and_resolved_mods() {
        let temp_dir = tempdir().unwrap();
        let database_path = temp_dir.path().join("directory.sqlite");
        let mods_root = temp_dir.path().join("mods");
        write_mod_metadata(&mods_root, "effects-pack", "Effects Pack");

        let directory = Arc::new(
            SqliteServerDirectory::open_path(&database_path)
                .await
                .unwrap(),
        );
        directory
            .register(
                RegisterServerRequest {
                    mod_list: "effects-pack,unknown-pack".to_string(),
                    numplayers: 2,
                    ..sample_request("srv-detail", "Detail Server", "21")
                },
                1000,
            )
            .await
            .unwrap();
        directory
            .insert_session(
                "srv-detail",
                &crate::model::ServerRecentSession {
                    mission: "Ambush at Dusk".to_string(),
                    label: "Detail Server patrol".to_string(),
                    played_minutes: 52,
                    peak_players: 2,
                    ended_unix_ms: 1000,
                },
            )
            .await
            .unwrap();
        directory
            .register(
                RegisterServerRequest {
                    mod_list: String::new(),
                    numplayers: 0,
                    ..sample_request("srv-empty", "Empty Server", "22")
                },
                1001,
            )
            .await
            .unwrap();

        let app = build_router_with_options(directory, None, Some(mods_root), None, false);

        let detail_response = app
            .clone()
            .oneshot(
                Request::builder()
                    .method("GET")
                    .uri("/v1/servers/srv-detail")
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(detail_response.status(), StatusCode::OK);
        let detail_body = to_bytes(detail_response.into_body(), usize::MAX)
            .await
            .unwrap();
        let detail: ServerDetail = serde_json::from_slice(&detail_body).unwrap();
        assert_eq!(detail.players.len(), 2);
        assert_eq!(detail.mods.len(), 2);
        assert!(!detail.player_history.is_empty());
        assert!(!detail.recent_sessions.is_empty());
        assert!(detail.mods[0].known);
        assert!(!detail.mods[1].known);
        assert_eq!(
            detail.mods[0].download_url.as_deref(),
            Some("https://example.invalid/effects-pack.zip")
        );
        assert_eq!(detail.mods[1].download_url, None);

        let empty_response = app
            .oneshot(
                Request::builder()
                    .method("GET")
                    .uri("/v1/servers/srv-empty")
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(empty_response.status(), StatusCode::OK);
        let empty_body = to_bytes(empty_response.into_body(), usize::MAX)
            .await
            .unwrap();
        let empty_detail: ServerDetail = serde_json::from_slice(&empty_body).unwrap();
        assert!(empty_detail.players.is_empty());
        assert!(empty_detail.mods.is_empty());
    }

    #[tokio::test]
    async fn http_router_reuses_existing_sqlite_directory_after_restart() {
        let temp_dir = tempdir().unwrap();
        let database_path = temp_dir.path().join("directory.sqlite");

        {
            let directory = Arc::new(
                SqliteServerDirectory::open_path(&database_path)
                    .await
                    .unwrap(),
            );
            let app = build_router(directory);
            let register_response = app
                .oneshot(
                    Request::builder()
                        .method("POST")
                        .uri("/v1/servers/register")
                        .header("content-type", "application/json")
                        .body(Body::from(
                            serde_json::to_vec(&sample_request(
                                "srv-http-restart",
                                "Before Restart",
                                "16",
                            ))
                            .unwrap(),
                        ))
                        .unwrap(),
                )
                .await
                .unwrap();
            assert_eq!(register_response.status(), StatusCode::OK);
        }

        let reopened_directory = Arc::new(
            SqliteServerDirectory::open_path(&database_path)
                .await
                .unwrap(),
        );
        let reopened_app = build_router(reopened_directory);

        let list_response = reopened_app
            .clone()
            .oneshot(
                Request::builder()
                    .method("GET")
                    .uri("/v1/servers?hostname=Before&includeUnverifiedServers=true")
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(list_response.status(), StatusCode::OK);
        let list_body = to_bytes(list_response.into_body(), usize::MAX)
            .await
            .unwrap();
        let servers: Vec<DirectoryServerRecord> = serde_json::from_slice(&list_body).unwrap();
        assert_eq!(servers.len(), 1);
        assert_eq!(servers[0].server_id, "srv-http-restart");
        assert_eq!(servers[0].hostname, "Before Restart");

        let heartbeat_response = reopened_app
            .clone()
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/v1/servers/heartbeat")
                    .header("content-type", "application/json")
                    .body(Body::from(
                        serde_json::to_vec(&sample_request(
                            "srv-http-restart",
                            "After Restart",
                            "17",
                        ))
                        .unwrap(),
                    ))
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(heartbeat_response.status(), StatusCode::OK);

        let updated_response = reopened_app
            .clone()
            .oneshot(
                Request::builder()
                    .method("GET")
                    .uri("/v1/servers?hostname=After&includeUnverifiedServers=true")
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(updated_response.status(), StatusCode::OK);
        let updated_body = to_bytes(updated_response.into_body(), usize::MAX)
            .await
            .unwrap();
        let updated_servers: Vec<DirectoryServerRecord> =
            serde_json::from_slice(&updated_body).unwrap();
        assert_eq!(updated_servers.len(), 1);
        assert_eq!(updated_servers[0].address, "192.168.1.17");
        assert_eq!(updated_servers[0].hostname, "After Restart");
    }

    #[tokio::test]
    async fn http_routes_expose_service_metadata() {
        let temp_dir = tempdir().unwrap();
        let database_path = temp_dir.path().join("directory.sqlite");
        let directory = Arc::new(
            SqliteServerDirectory::open_path(&database_path)
                .await
                .unwrap(),
        );
        let app = build_router(directory);

        let alpha_response = app
            .clone()
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/v1/servers/register")
                    .header("content-type", "application/json")
                    .body(Body::from(
                        serde_json::to_vec(&sample_request("srv-alpha", "Alpha", "21")).unwrap(),
                    ))
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(alpha_response.status(), StatusCode::OK);

        let mut bravo_request = sample_request("srv-bravo", "Bravo", "22");
        bravo_request.numplayers = 6;
        bravo_request.maxplayers = 6;
        bravo_request.password = true;
        let bravo_response = app
            .clone()
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/v1/servers/register")
                    .header("content-type", "application/json")
                    .body(Body::from(serde_json::to_vec(&bravo_request).unwrap()))
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(bravo_response.status(), StatusCode::OK);

        let metadata_response = app
            .clone()
            .oneshot(
                Request::builder()
                    .method("GET")
                    .uri("/v1/meta/service")
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(metadata_response.status(), StatusCode::OK);
        let metadata_body = to_bytes(metadata_response.into_body(), usize::MAX)
            .await
            .unwrap();
        let metadata: ServiceMetadata = serde_json::from_slice(&metadata_body).unwrap();
        assert_eq!(metadata.product_name, "PAPA BEAR");
        assert_eq!(metadata.openapi_url, "/openapi/v1.yaml");

        let summary_response = app
            .oneshot(
                Request::builder()
                    .method("GET")
                    .uri("/v1/meta/summary")
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(summary_response.status(), StatusCode::OK);
        let summary_body = to_bytes(summary_response.into_body(), usize::MAX)
            .await
            .unwrap();
        let summary: ServiceSummary = serde_json::from_slice(&summary_body).unwrap();
        assert_eq!(summary.public_verified_servers, 0);
        assert_eq!(summary.public_players, 0);
        assert_eq!(summary.self_reported_servers, 1);
        assert_eq!(summary.unreachable_servers, 0);
        assert_eq!(summary.mod_count, 0);
    }

    #[tokio::test]
    async fn http_routes_serve_openapi_document() {
        let temp_dir = tempdir().unwrap();
        let database_path = temp_dir.path().join("directory.sqlite");
        let directory = Arc::new(
            SqliteServerDirectory::open_path(&database_path)
                .await
                .unwrap(),
        );
        let app = build_router(directory);

        let response = app
            .oneshot(
                Request::builder()
                    .method("GET")
                    .uri("/openapi/v1.yaml")
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(response.status(), StatusCode::OK);
        let body = to_bytes(response.into_body(), usize::MAX).await.unwrap();
        let text = String::from_utf8(body.to_vec()).unwrap();
        assert_eq!(text, openapi_yaml().unwrap());
        assert!(text.contains("/v1/servers/register:"));
        assert!(text.contains("/v1/servers/heartbeat:"));
        assert!(text.contains("/v1/servers/{serverId}/observe:"));
        assert!(text.contains("/v1/servers/{serverId}:"));
        assert!(text.contains("/v1/mods:"));
        assert!(text.contains("/v1/mods/{modId}:"));
        assert!(text.contains("x-api-key"));
        assert!(text.contains("/v1/meta/service:"));
        assert!(text.contains("/v1/meta/summary:"));
        assert!(!text.contains("/v1/summary:"));
    }

    #[test]
    fn generated_openapi_contract_covers_current_public_paths() {
        let openapi = serde_json::to_value(openapi_document()).unwrap();
        let paths = openapi.get("paths").and_then(Value::as_object).unwrap();

        assert!(paths.contains_key("/v1/servers"));
        assert!(paths.contains_key("/v1/servers/register"));
        assert!(paths.contains_key("/v1/servers/heartbeat"));
        assert!(paths.contains_key("/v1/servers/{serverId}/observe"));
        assert!(paths.contains_key("/v1/servers/prune"));
        assert!(paths.contains_key("/v1/servers/{serverId}"));
        assert!(paths.contains_key("/v1/mods"));
        assert!(paths.contains_key("/v1/mods/{modId}"));
        assert!(paths.contains_key("/v1/mods/{modId}/versions"));
        assert!(paths.contains_key("/v1/mods/{modId}/servers"));
        assert!(paths.contains_key("/v1/meta/service"));
        assert!(paths.contains_key("/v1/meta/summary"));
        assert!(paths.contains_key("/openapi/v1.yaml"));
        assert_eq!(
            openapi
                .pointer("/paths/~1v1~1servers~1prune/post/parameters/0/name")
                .and_then(Value::as_str),
            Some("x-api-key")
        );
        assert_eq!(
            openapi
                .pointer("/paths/~1v1~1servers~1{serverId}~1observe/post/parameters/1/name")
                .and_then(Value::as_str),
            Some("x-api-key")
        );
    }

    #[test]
    fn generated_openapi_contract_covers_current_public_schemas() {
        let openapi = serde_json::to_value(openapi_document()).unwrap();

        let schemas = openapi
            .pointer("/components/schemas")
            .and_then(Value::as_object)
            .unwrap();
        assert!(schemas.contains_key("RegisterServerRequest"));
        assert!(schemas.contains_key("ObserveServerRequest"));
        assert!(schemas.contains_key("DirectoryServerRecord"));
        assert!(schemas.contains_key("ServerDetail"));
        assert!(schemas.contains_key("ServerModReference"));
        assert!(schemas.contains_key("ServerPopulationSample"));
        assert!(schemas.contains_key("ServerPlayer"));
        assert!(schemas.contains_key("ServerRecentSession"));
        assert!(schemas.contains_key("ModUsageServer"));
        assert!(schemas.contains_key("ModCatalogEntry"));
        assert!(schemas.contains_key("PruneServersRequest"));
        assert!(schemas.contains_key("PruneServersResponse"));
        assert!(schemas.contains_key("ServiceMetadata"));
        assert!(schemas.contains_key("ServiceSummary"));
        assert!(schemas.contains_key("VerificationState"));
    }

    #[test]
    fn generated_openapi_contract_keeps_register_required_fields() {
        let openapi = serde_json::to_value(openapi_document()).unwrap();

        let register_required = openapi
            .pointer("/components/schemas/RegisterServerRequest/required")
            .and_then(Value::as_array)
            .unwrap();
        assert!(register_required.contains(&Value::String("serverId".to_string())));
        assert!(register_required.contains(&Value::String("impl".to_string())));
    }

    #[test]
    fn generated_openapi_contract_keeps_server_detail_shape() {
        let openapi = serde_json::to_value(openapi_document()).unwrap();

        assert_eq!(
            openapi
                .pointer("/components/schemas/ServerRecentSession/properties/mission/type")
                .and_then(Value::as_str),
            Some("string")
        );
        assert_eq!(
            openapi
                .pointer("/components/schemas/ServerRecentSession/properties/label/type")
                .and_then(Value::as_str),
            Some("string")
        );
        assert_eq!(
            openapi
                .pointer("/components/schemas/ServerDetail/properties/recentSessions/items/$ref")
                .and_then(Value::as_str),
            Some("#/components/schemas/ServerRecentSession")
        );
        assert_eq!(
            openapi
                .pointer("/paths/~1v1~1servers~1{serverId}/get/responses/200/content/application~1json/schema/$ref")
                .and_then(Value::as_str),
            Some("#/components/schemas/ServerDetail")
        );
        assert_eq!(
            openapi
                .pointer(
                    "/components/schemas/DirectoryServerRecord/properties/verificationState/$ref"
                )
                .and_then(Value::as_str),
            Some("#/components/schemas/VerificationState")
        );
        assert!(openapi
            .pointer("/components/schemas/DirectoryServerRecord/properties/lastObservedUnixMs")
            .is_some());
        assert!(openapi
            .pointer("/components/schemas/DirectoryServerRecord/properties/observedReachable")
            .is_some());
    }

    #[test]
    fn generated_openapi_contract_keeps_mod_versions_response_shape() {
        let openapi = serde_json::to_value(openapi_document()).unwrap();

        assert_eq!(
            openapi
                .pointer(
                    "/paths/~1v1~1mods~1{modId}~1versions/get/responses/200/content/application~1json/schema/type",
                )
                .and_then(Value::as_str),
            Some("array")
        );
        assert_eq!(
            openapi
                .pointer(
                    "/paths/~1v1~1mods~1{modId}~1versions/get/responses/200/content/application~1json/schema/items/$ref",
                )
                .and_then(Value::as_str),
            Some("#/components/schemas/ModCatalogEntry")
        );
        assert!(openapi
            .pointer("/components/schemas/ModCatalogEntry/properties/downloadUrl")
            .is_some());
    }

    async fn test_app() -> axum::Router {
        let temp_dir = tempdir().unwrap();
        let database_path = temp_dir.path().join("directory.sqlite");
        let directory = Arc::new(
            SqliteServerDirectory::open_path(&database_path)
                .await
                .unwrap(),
        );
        build_router(directory)
    }

    #[tokio::test]
    async fn http_routes_serve_browser_shell_pages() {
        let app = test_app().await;

        let landing_response = app
            .clone()
            .oneshot(
                Request::builder()
                    .method("GET")
                    .uri("/")
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(landing_response.status(), StatusCode::OK);
        let landing_body = to_bytes(landing_response.into_body(), usize::MAX)
            .await
            .unwrap();
        let landing_text = String::from_utf8(landing_body.to_vec()).unwrap();
        // The top nav is rendered from papa-bear.js into this shared placeholder, so the
        // page ships the empty <nav> rather than hand-coded HOME/SERVERS/MODS links.
        assert!(landing_text.contains("<nav class=\"top-menu\"></nav>"));
        assert!(landing_text.contains("id=\"landing-summary\""));
        assert!(landing_text.contains("id=\"landing-self-reported-count\""));
        assert!(landing_text.contains("id=\"landing-unreachable-count\""));
        assert!(landing_text.contains("id=\"landing-table\""));
        assert!(landing_text.contains("data-page=\"landing\""));
        assert!(landing_text.contains("<h1 class=\"title\">PAPA BEAR</h1>"));
        assert!(!landing_text.contains("id=\"landing-service\""));
        assert!(!landing_text.contains("OPEN VERIFIED SERVERS"));
        assert!(!landing_text.contains("BROWSE MODS"));

        let browser_response = app
            .clone()
            .oneshot(
                Request::builder()
                    .method("GET")
                    .uri("/browser")
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(browser_response.status(), StatusCode::OK);
        let browser_body = to_bytes(browser_response.into_body(), usize::MAX)
            .await
            .unwrap();
        let browser_text = String::from_utf8(browser_body.to_vec()).unwrap();
        assert!(browser_text.contains("server browser BETA"));
        assert!(browser_text.contains("id=\"browser-table\""));
        assert!(browser_text.contains("id=\"browser-verification\""));
        assert!(browser_text.contains("<nav class=\"top-menu\"></nav>"));

        let browser_detail_response = app
            .clone()
            .oneshot(
                Request::builder()
                    .method("GET")
                    .uri("/browser/demo-server")
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(browser_detail_response.status(), StatusCode::OK);
        let browser_detail_body = to_bytes(browser_detail_response.into_body(), usize::MAX)
            .await
            .unwrap();
        let browser_detail_text = String::from_utf8(browser_detail_body.to_vec()).unwrap();
        assert!(browser_detail_text.contains("data-page=\"browser-detail\""));
        assert!(browser_detail_text.contains(">BACK<"));
        assert!(browser_detail_text.contains("id=\"detail-players\""));
    }

    #[tokio::test]
    async fn http_routes_serve_mod_shell_pages() {
        let app = test_app().await;

        let mods_response = app
            .clone()
            .oneshot(
                Request::builder()
                    .method("GET")
                    .uri("/mods")
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(mods_response.status(), StatusCode::OK);
        let mods_body = to_bytes(mods_response.into_body(), usize::MAX)
            .await
            .unwrap();
        let mods_text = String::from_utf8(mods_body.to_vec()).unwrap();
        assert!(mods_text.contains("mods browser BETA"));
        assert!(mods_text.contains("id=\"mods-game\""));
        assert!(mods_text.contains("id=\"mods-version\""));
        assert!(mods_text.contains("id=\"mods-table\""));

        let mod_detail_response = app
            .clone()
            .oneshot(
                Request::builder()
                    .method("GET")
                    .uri("/mods/demo-mod")
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(mod_detail_response.status(), StatusCode::OK);
        let mod_detail_body = to_bytes(mod_detail_response.into_body(), usize::MAX)
            .await
            .unwrap();
        let mod_detail_text = String::from_utf8(mod_detail_body.to_vec()).unwrap();
        assert!(mod_detail_text.contains("data-page=\"mod-detail\""));
        assert!(mod_detail_text.contains(">BACK<"));
        assert!(mod_detail_text.contains("id=\"mod-detail-add\""));
        assert!(mod_detail_text.contains("id=\"mod-servers-rows\""));
    }

    #[tokio::test]
    async fn http_routes_serve_web_shell_assets() {
        let app = test_app().await;

        let css_response = app
            .clone()
            .oneshot(
                Request::builder()
                    .method("GET")
                    .uri("/assets/site.css")
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(css_response.status(), StatusCode::OK);

        let js_response = app
            .oneshot(
                Request::builder()
                    .method("GET")
                    .uri("/assets/papa-bear.js")
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(js_response.status(), StatusCode::OK);
        let js_body = to_bytes(js_response.into_body(), usize::MAX).await.unwrap();
        let js_text = String::from_utf8(js_body.to_vec()).unwrap();
        assert!(js_text.contains("loadBrowserList"));
        assert!(js_text.contains("if (serviceElement)"));
        assert!(js_text.contains("loadServerDetailPage"));
        assert!(js_text.contains("loadModsList"));
        assert!(js_text.contains("currentModsTextFilter"));
        assert!(js_text.contains("modCompatibilityGroups"));
        assert!(js_text.contains("updateModsUrl"));
        assert!(js_text.contains("loadModDetailPage"));
        assert!(!js_text.contains("loadLandingSummary"));
        // Shared top nav lives only in the JS, so every page gets the same HOME/SERVERS/MODS links.
        assert!(js_text.contains("renderCommunityBanner"));
        assert!(js_text
            .contains("github.com/ofpisnotdead-com/CWR-CE/discussions/new?category=papa-bear-cz"));
        assert!(js_text.contains("Share your ideas, issues, and mod requests"));
        assert!(js_text.contains("GitHub Discussions</a>."));
        assert!(js_text.contains("renderSiteFooter"));
        assert!(js_text.contains(
            "Community project of <a href=\"https://ofpisnotdead.com\">ofpisnotdead.com</a> group &lt;3."
        ));
        assert!(js_text.contains("renderTopMenu"));
        assert!(js_text.contains("\"HOME\""));
        assert!(js_text.contains("\"SERVERS\""));
        assert!(js_text.contains("\"MODS\""));
    }
}
