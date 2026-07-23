use anyhow::{Context, Result};
use serde::Deserialize;

use crate::model::{ModCatalogEntry, RegisterServerRequest, ServerRecentSession};
use crate::mods::ModStore;
use crate::repository::SqliteServerDirectory;

pub const DEV_SEED_SERVER_COUNT: usize = 16;
pub const DEV_SEED_MODS_CSV: &str = include_str!("../dev-data/dev_mods_seed.csv");

const DEV_SERVER_SEEDS: [DevServerSeed; DEV_SEED_SERVER_COUNT] = [
    DevServerSeed {
        server_id: "srv-dev-01",
        hostname: "Synthetic Lane 01",
        gametype: "coop",
        numplayers: 7,
        maxplayers: 16,
        password: false,
        mod_list: "synthetic-mod-a0000001",
        state: 14,
        hostport: 2302,
    },
    DevServerSeed {
        server_id: "srv-dev-02",
        hostname: "Synthetic Lane 02",
        gametype: "ctf",
        numplayers: 22,
        maxplayers: 32,
        password: false,
        mod_list: "synthetic-mod-002,synthetic-mod-003",
        state: 14,
        hostport: 2303,
    },
    DevServerSeed {
        server_id: "srv-dev-03",
        hostname: "Synthetic Lane 03",
        gametype: "coop",
        numplayers: 0,
        maxplayers: 12,
        password: false,
        mod_list: "synthetic-mod-004",
        state: 13,
        hostport: 2304,
    },
    DevServerSeed {
        server_id: "srv-dev-04",
        hostname: "Synthetic Lane 04",
        gametype: "coop",
        numplayers: 12,
        maxplayers: 12,
        password: false,
        mod_list: "synthetic-mod-005,synthetic-mod-006",
        state: 14,
        hostport: 2305,
    },
    DevServerSeed {
        server_id: "srv-dev-05",
        hostname: "Synthetic Lane 05",
        gametype: "dm",
        numplayers: 3,
        maxplayers: 10,
        password: true,
        mod_list: "",
        state: 14,
        hostport: 2306,
    },
    DevServerSeed {
        server_id: "srv-dev-06",
        hostname: "Synthetic Lane 06",
        gametype: "coop",
        numplayers: 18,
        maxplayers: 24,
        password: false,
        mod_list: "synthetic-mod-007,synthetic-mod-008",
        state: 14,
        hostport: 2307,
    },
    DevServerSeed {
        server_id: "srv-dev-07",
        hostname: "Synthetic Lane 07",
        gametype: "coop",
        numplayers: 24,
        maxplayers: 24,
        password: true,
        mod_list: "synthetic-mod-009",
        state: 14,
        hostport: 2308,
    },
    DevServerSeed {
        server_id: "srv-dev-08",
        hostname: "Synthetic Lane 08",
        gametype: "ctf",
        numplayers: 9,
        maxplayers: 20,
        password: false,
        mod_list: "synthetic-mod-010",
        state: 14,
        hostport: 2309,
    },
    DevServerSeed {
        server_id: "srv-dev-09",
        hostname: "Synthetic Lane 09",
        gametype: "coop",
        numplayers: 5,
        maxplayers: 8,
        password: false,
        mod_list: "synthetic-mod-011,synthetic-mod-012",
        state: 14,
        hostport: 2310,
    },
    DevServerSeed {
        server_id: "srv-dev-10",
        hostname: "Synthetic Lane 10",
        gametype: "coop",
        numplayers: 2,
        maxplayers: 16,
        password: false,
        mod_list: "synthetic-mod-013",
        state: 13,
        hostport: 2311,
    },
    DevServerSeed {
        server_id: "srv-dev-11",
        hostname: "Synthetic Lane 11",
        gametype: "ctf",
        numplayers: 16,
        maxplayers: 24,
        password: false,
        mod_list: "synthetic-mod-014",
        state: 14,
        hostport: 2312,
    },
    DevServerSeed {
        server_id: "srv-dev-12",
        hostname: "Mod Test Bed",
        gametype: "sandbox",
        numplayers: 1,
        maxplayers: 6,
        password: false,
        mod_list: "unknown-pack-00000000",
        state: 13,
        hostport: 2313,
    },
    DevServerSeed {
        server_id: "srv-dev-13",
        hostname: "Public PvP",
        gametype: "dm",
        numplayers: 11,
        maxplayers: 18,
        password: false,
        mod_list: "synthetic-mod-015,synthetic-mod-016",
        state: 14,
        hostport: 2314,
    },
    DevServerSeed {
        server_id: "srv-dev-14",
        hostname: "Locked Squad Run",
        gametype: "coop",
        numplayers: 8,
        maxplayers: 10,
        password: true,
        mod_list: "synthetic-mod-017",
        state: 14,
        hostport: 2315,
    },
    DevServerSeed {
        server_id: "srv-dev-15",
        hostname: "Campaign Replay",
        gametype: "coop",
        numplayers: 0,
        maxplayers: 8,
        password: false,
        mod_list: "synthetic-mod-018,synthetic-mod-019",
        state: 12,
        hostport: 2316,
    },
    DevServerSeed {
        server_id: "srv-dev-16",
        hostname: "Stress Arena",
        gametype: "dm",
        numplayers: 28,
        maxplayers: 32,
        password: false,
        mod_list: "synthetic-mod-020,synthetic-mod-021,synthetic-mod-022",
        state: 14,
        hostport: 2317,
    },
];

pub async fn seed_dev_data(directory: &SqliteServerDirectory, now_unix_ms: i64) -> Result<usize> {
    for (index, seed) in DEV_SERVER_SEEDS.iter().enumerate() {
        let current_unix_ms = now_unix_ms + i64::try_from(index).unwrap_or(0);
        let history = seed.history_samples();
        directory.clear_samples(seed.server_id).await?;
        directory.clear_sessions(seed.server_id).await?;
        for (sample_index, players) in history[..history.len() - 1].iter().enumerate() {
            let trailing = i64::try_from(history.len() - sample_index - 1).unwrap_or(0);
            directory
                .insert_sample(
                    seed.server_id,
                    current_unix_ms - trailing * 30 * 60 * 1000,
                    *players,
                )
                .await?;
        }
        for session in seed.recent_sessions(current_unix_ms) {
            directory.insert_session(seed.server_id, &session).await?;
        }
        directory
            .register(seed.to_request(), current_unix_ms)
            .await?;
        directory
            .observe(seed.server_id, current_unix_ms, true)
            .await?;
    }

    Ok(DEV_SEED_SERVER_COUNT)
}

pub async fn seed_dev_mods(store: &ModStore) -> Result<usize> {
    let mut seeded = 0;
    let mut reader = csv::Reader::from_reader(DEV_SEED_MODS_CSV.as_bytes());
    for row in reader.deserialize::<DevModSeedRow>() {
        let row = row.context("parsing embedded dev mods seed")?;
        let version = mod_version_from_id(&row.mod_id).to_string();
        let size_bytes = (row.mod_id.len() as u64) * 1_048_576;

        let metadata = ModCatalogEntry {
            mod_id: row.mod_id,
            app_name: Some("CWR".to_string()),
            actver: Some(303),
            version_tag: None,
            compatible: false,
            name: row.name,
            version,
            folder_name: None,
            description: row.description.trim_end_matches(" Website").to_string(),
            authors: Vec::new(),
            homepage_url: some_if_present(&row.homepage_url),
            download_url: some_if_present(&row.download_url),
            size_bytes: Some(size_bytes),
        };

        store.put_metadata(&metadata).await?;
        seeded += 1;
    }

    Ok(seeded)
}

struct DevServerSeed {
    server_id: &'static str,
    hostname: &'static str,
    gametype: &'static str,
    numplayers: i32,
    maxplayers: i32,
    password: bool,
    mod_list: &'static str,
    state: i32,
    hostport: u16,
}

impl DevServerSeed {
    fn to_request(&self) -> RegisterServerRequest {
        RegisterServerRequest {
            app_name: "CWR".to_string(),
            server_id: self.server_id.to_string(),
            address: "127.0.0.1".to_string(),
            hostport: self.hostport,
            hostname: self.hostname.to_string(),
            gametype: self.gametype.to_string(),
            actver: 196,
            reqver: 196,
            version_tag: "rc1".to_string(),
            state: self.state,
            numplayers: self.numplayers,
            maxplayers: self.maxplayers,
            password: self.password,
            mod_list: self.mod_list.to_string(),
            equal_mod_required: false,
            transport_impl: "papa-bear".to_string(),
            platform: "win".to_string(),
            time_left: 0,
            state_elapsed_seconds: 0,
            map_name: "Synthetic Testbed".to_string(),
            cadet: false,
            difficulty: 0,
            jip: true,
            disabled_ai: false,
            respawn: 1,
            respawn_delay: 5,
            locked: false,
            dedicated: true,
            description: "Dev seed server for browser testing.".to_string(),
            param1: "Time of day: Day".to_string(),
            param2: "Weather: Clear".to_string(),
            required_addons: "synthetic-core;@synthetic-effects".to_string(),
        }
    }

    fn history_samples(&self) -> [i32; 12] {
        let current = self.numplayers.max(0);
        let peak = (current + current.max(2) / 2).min(self.maxplayers);
        let mid = (current / 2)
            .max(i32::from(current > 0))
            .min(self.maxplayers);
        let low = (mid / 2).max(i32::from(current > 0));

        if current == 0 {
            [0, 0, low, mid, 1, 0, 0, low, 0, 0, 0, 0]
        } else {
            [
                0,
                low,
                mid,
                peak,
                (current - 2).max(1),
                0,
                low,
                mid,
                0,
                (current - 3).max(1),
                (current - 1).max(1),
                current,
            ]
        }
    }

    fn recent_sessions(&self, current_unix_ms: i64) -> [ServerRecentSession; 3] {
        let [first_mission, second_mission, third_mission] = mission_triplet(self.server_id);
        [
            ServerRecentSession {
                mission: first_mission.to_string(),
                label: "prime time".to_string(),
                played_minutes: 45,
                peak_players: self.numplayers.max(1).min(self.maxplayers),
                ended_unix_ms: current_unix_ms,
            },
            ServerRecentSession {
                mission: second_mission.to_string(),
                label: "night cycle".to_string(),
                played_minutes: 78,
                peak_players: (self.numplayers + 2).min(self.maxplayers).max(1),
                ended_unix_ms: current_unix_ms - 3 * 60 * 60 * 1000,
            },
            ServerRecentSession {
                mission: third_mission.to_string(),
                label: "afternoon fallback".to_string(),
                played_minutes: 62,
                peak_players: self.numplayers.max(1),
                ended_unix_ms: current_unix_ms - 24 * 60 * 60 * 1000,
            },
        ]
    }
}

fn mission_triplet(server_id: &str) -> [&'static str; 3] {
    match server_id {
        "srv-dev-01" => [
            "Synthetic Route 01A",
            "Synthetic Route 01B",
            "Synthetic Route 01C",
        ],
        "srv-dev-02" => [
            "Synthetic Route 02A",
            "Synthetic Route 02B",
            "Synthetic Route 02C",
        ],
        "srv-dev-03" => [
            "Synthetic Route 03A",
            "Synthetic Route 03B",
            "Synthetic Route 03C",
        ],
        "srv-dev-04" => [
            "Synthetic Route 04A",
            "Synthetic Route 04B",
            "Synthetic Route 04C",
        ],
        "srv-dev-05" => [
            "Synthetic Route 05A",
            "Synthetic Route 05B",
            "Synthetic Route 05C",
        ],
        "srv-dev-06" => [
            "Synthetic Route 06A",
            "Synthetic Route 06B",
            "Synthetic Route 06C",
        ],
        "srv-dev-07" => [
            "Synthetic Route 07A",
            "Synthetic Route 07B",
            "Synthetic Route 07C",
        ],
        "srv-dev-08" => [
            "Synthetic Route 08A",
            "Synthetic Route 08B",
            "Synthetic Route 08C",
        ],
        "srv-dev-09" => [
            "Synthetic Route 09A",
            "Synthetic Route 09B",
            "Synthetic Route 09C",
        ],
        "srv-dev-10" => [
            "Synthetic Route 10A",
            "Synthetic Route 10B",
            "Synthetic Route 10C",
        ],
        "srv-dev-11" => [
            "Synthetic Route 11A",
            "Synthetic Route 11B",
            "Synthetic Route 11C",
        ],
        "srv-dev-12" => [
            "Synthetic Route 12A",
            "Synthetic Route 12B",
            "Synthetic Route 12C",
        ],
        "srv-dev-13" => [
            "Synthetic Route 13A",
            "Synthetic Route 13B",
            "Synthetic Route 13C",
        ],
        "srv-dev-14" => [
            "Synthetic Route 14A",
            "Synthetic Route 14B",
            "Synthetic Route 14C",
        ],
        "srv-dev-15" => [
            "Synthetic Route 15A",
            "Synthetic Route 15B",
            "Synthetic Route 15C",
        ],
        "srv-dev-16" => [
            "Synthetic Route 16A",
            "Synthetic Route 16B",
            "Synthetic Route 16C",
        ],
        _ => [
            "Synthetic Route 00A",
            "Synthetic Route 00B",
            "Synthetic Route 00C",
        ],
    }
}

#[derive(Deserialize)]
struct DevModSeedRow {
    mod_id: String,
    name: String,
    description: String,
    homepage_url: String,
    #[serde(default)]
    download_url: String,
}

fn mod_version_from_id(mod_id: &str) -> &str {
    mod_id.rsplit_once('-').map_or("seed", |(_, suffix)| suffix)
}

fn some_if_present(value: &str) -> Option<String> {
    let value = value.trim();
    if value.is_empty() {
        None
    } else {
        Some(value.to_string())
    }
}

#[cfg(test)]
mod tests {
    use tempfile::tempdir;

    use super::{seed_dev_data, seed_dev_mods, DEV_SEED_SERVER_COUNT};
    use crate::model::{ListModsQuery, ListServersQuery};
    use crate::mods::ModStore;
    use crate::repository::SqliteServerDirectory;

    #[tokio::test]
    async fn dev_seed_is_idempotent_for_server_rows() {
        let temp_dir = tempdir().unwrap();
        let database_path = temp_dir.path().join("directory.sqlite");
        let directory = SqliteServerDirectory::open_path(&database_path)
            .await
            .unwrap();

        let first = seed_dev_data(&directory, 1000).await.unwrap();
        let second = seed_dev_data(&directory, 2000).await.unwrap();

        assert_eq!(first, DEV_SEED_SERVER_COUNT);
        assert_eq!(second, DEV_SEED_SERVER_COUNT);

        let rows = directory
            .list(
                &ListServersQuery {
                    include_unverified_servers: Some(true),
                    include_passworded_servers: Some(true),
                    ..Default::default()
                },
                2000,
            )
            .await
            .unwrap();
        assert_eq!(rows.len(), DEV_SEED_SERVER_COUNT);
        assert!(rows.iter().any(|row| row.server_id == "srv-dev-16"));
        assert!(rows.iter().any(|row| row.hostname == "Stress Arena"));
    }

    #[tokio::test]
    async fn dev_mod_seed_is_idempotent_and_populates_catalog() {
        let temp_dir = tempdir().unwrap();
        let store = ModStore::local(&temp_dir.path().join("mods")).unwrap();

        let first = seed_dev_mods(&store).await.unwrap();
        let second = seed_dev_mods(&store).await.unwrap();

        assert_eq!(first, second);

        let mods = store.list_mods(&ListModsQuery::default()).await.unwrap();
        assert_eq!(mods.len(), first);
        assert!(mods
            .iter()
            .any(|entry| entry.name == "@synthetic_alpha_core_001"));
        assert!(mods
            .iter()
            .any(|entry| entry.name == "@synthetic_ion_tools_009"));
        assert!(mods
            .iter()
            .any(|entry| entry.description.contains("Synthetic catalog entry 001")));
        assert!(mods.iter().any(|entry| entry.version.len() == 8));
        assert!(mods.iter().any(|entry| entry.download_url.is_some()));
        assert!(mods.iter().all(|entry| entry.actver == Some(303)));
    }
}
