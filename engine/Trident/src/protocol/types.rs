//! Protocol message types — shared between client and scenarios.

use serde::{Deserialize, Serialize};

/// Protocol version — must match engine/Trident/protocol/harness.schema.json
pub const PROTOCOL_VERSION: u32 = 1;

// ── Commands (Trident → Game) ──────────────────────────────────────────────

/// A command sent from Trident to the game harness.
#[derive(Debug, Clone, Serialize)]
#[serde(tag = "cmd")]
pub enum Command {
    #[serde(rename = "ping")]
    Ping,

    #[serde(rename = "describe")]
    Describe,

    #[serde(rename = "key")]
    Key {
        sc: u32,
        #[serde(skip_serializing_if = "Option::is_none")]
        r#mod: Option<u32>,
        #[serde(skip_serializing_if = "Option::is_none")]
        hold: Option<bool>,
    },

    #[serde(rename = "key_up")]
    KeyUp { sc: u32 },

    #[serde(rename = "click")]
    Click { idc: i32 },

    #[serde(rename = "query")]
    Query { what: String },

    #[serde(rename = "query")]
    MpJoin {
        what: String,
        address: String,
        port: u16,
        #[serde(skip_serializing_if = "Option::is_none")]
        password: Option<String>,
        #[serde(skip_serializing_if = "Option::is_none")]
        modpath: Option<String>,
    },

    #[serde(rename = "screenshot")]
    Screenshot { path: String },

    #[serde(rename = "wait_display")]
    WaitDisplay {
        idd: i32,
        #[serde(skip_serializing_if = "Option::is_none")]
        timeout_ms: Option<u32>,
    },

    #[serde(rename = "eval")]
    Eval { code: String },

    #[serde(rename = "exec")]
    Exec { code: String },

    #[serde(rename = "exit")]
    Exit,
}

// ── Responses (Game → Trident, in reply to a command) ──────────────────────

/// A response from the game harness to a command.
#[derive(Debug, Clone, Deserialize)]
pub struct Response {
    pub ok: bool,
    #[serde(default)]
    pub error: Option<String>,
    /// Arbitrary response data (varies by command)
    #[serde(flatten)]
    pub data: serde_json::Map<String, serde_json::Value>,
}

// ── Events (Game → Trident, pushed asynchronously) ─────────────────────────

/// An event pushed from the game harness to Trident.
#[derive(Debug, Clone, Deserialize)]
pub struct Event {
    pub event: String,
    #[serde(flatten)]
    pub data: serde_json::Map<String, serde_json::Value>,
}

/// A message from the game — either a response or an event.
#[derive(Debug, Clone)]
pub enum Message {
    Response(Response),
    Event(Event),
}

impl Message {
    /// Parse a JSON line from the harness into a Message.
    pub fn parse(line: &str) -> Result<Self, serde_json::Error> {
        let v: serde_json::Value = serde_json::from_str(line)?;
        if v.get("event").is_some() {
            Ok(Self::Event(serde_json::from_value(v)?))
        } else {
            Ok(Self::Response(serde_json::from_value(v)?))
        }
    }
}

/// Describe response — lists available commands and events.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DescribeResponse {
    pub ok: bool,
    pub version: u32,
    pub commands: Vec<CommandInfo>,
    pub events: Vec<EventInfo>,
}

#[derive(Debug, Clone, Deserialize, Serialize)]
pub struct CommandInfo {
    pub name: String,
    pub description: String,
    pub params: Vec<ParamInfo>,
}

#[derive(Debug, Clone, Deserialize, Serialize)]
pub struct ParamInfo {
    pub name: String,
    pub r#type: String,
    pub required: bool,
}

#[derive(Debug, Clone, Deserialize, Serialize)]
pub struct EventInfo {
    pub name: String,
    pub description: String,
    pub fields: Vec<FieldInfo>,
}

#[derive(Debug, Clone, Deserialize, Serialize)]
pub struct FieldInfo {
    pub name: String,
    pub r#type: String,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn serialize_ping() {
        let cmd = Command::Ping;
        let json = serde_json::to_string(&cmd).unwrap();
        assert_eq!(json, r#"{"cmd":"ping"}"#);
    }

    #[test]
    fn serialize_key() {
        let cmd = Command::Key {
            sc: 28,
            r#mod: None,
            hold: None,
        };
        let json = serde_json::to_string(&cmd).unwrap();
        assert_eq!(json, r#"{"cmd":"key","sc":28}"#);
    }

    #[test]
    fn serialize_key_with_mod() {
        let cmd = Command::Key {
            sc: 28,
            r#mod: Some(64),
            hold: Some(true),
        };
        let json = serde_json::to_string(&cmd).unwrap();
        assert!(json.contains(r#""mod":64"#));
        assert!(json.contains(r#""hold":true"#));
    }

    #[test]
    fn serialize_query() {
        let cmd = Command::Query {
            what: "display".into(),
        };
        let json = serde_json::to_string(&cmd).unwrap();
        assert_eq!(json, r#"{"cmd":"query","what":"display"}"#);
    }

    #[test]
    fn serialize_exit() {
        let cmd = Command::Exit;
        let json = serde_json::to_string(&cmd).unwrap();
        assert_eq!(json, r#"{"cmd":"exit"}"#);
    }

    #[test]
    fn parse_ok_response() {
        let line = r#"{"ok":true}"#;
        let msg = Message::parse(line).unwrap();
        match msg {
            Message::Response(r) => assert!(r.ok),
            Message::Event(_) => panic!("expected response"),
        }
    }

    #[test]
    fn parse_error_response() {
        let line = r#"{"ok":false,"error":"unknown command"}"#;
        let msg = Message::parse(line).unwrap();
        match msg {
            Message::Response(r) => {
                assert!(!r.ok);
                assert_eq!(r.error.as_deref(), Some("unknown command"));
            }
            Message::Event(_) => panic!("expected response"),
        }
    }

    #[test]
    fn parse_response_with_data() {
        let line = r#"{"ok":true,"idd":100,"name":"RscDisplayMain"}"#;
        let msg = Message::parse(line).unwrap();
        match msg {
            Message::Response(r) => {
                assert!(r.ok);
                assert_eq!(r.data["idd"], 100);
                assert_eq!(r.data["name"], "RscDisplayMain");
            }
            Message::Event(_) => panic!("expected response"),
        }
    }

    #[test]
    fn parse_event() {
        let line = r#"{"event":"ready","idd":100}"#;
        let msg = Message::parse(line).unwrap();
        match msg {
            Message::Event(e) => {
                assert_eq!(e.event, "ready");
                assert_eq!(e.data["idd"], 100);
            }
            Message::Response(_) => panic!("expected event"),
        }
    }

    #[test]
    fn parse_display_event() {
        let line = r#"{"event":"display","idd":200,"name":"RscDisplayMPSetup"}"#;
        let msg = Message::parse(line).unwrap();
        match msg {
            Message::Event(e) => {
                assert_eq!(e.event, "display");
                assert_eq!(e.data["idd"], 200);
            }
            Message::Response(_) => panic!("expected event"),
        }
    }

    #[test]
    fn parse_log_event() {
        let line = r#"{"event":"log","level":"info","cat":"Core","msg":"Initialized"}"#;
        let msg = Message::parse(line).unwrap();
        match msg {
            Message::Event(e) => {
                assert_eq!(e.event, "log");
                assert_eq!(e.data["cat"], "Core");
            }
            Message::Response(_) => panic!("expected event"),
        }
    }

    #[test]
    fn serialize_wait_display_with_timeout() {
        let cmd = Command::WaitDisplay {
            idd: 200,
            timeout_ms: Some(5000),
        };
        let json = serde_json::to_string(&cmd).unwrap();
        assert!(json.contains(r#""cmd":"wait_display""#));
        assert!(json.contains(r#""idd":200"#));
        assert!(json.contains(r#""timeout_ms":5000"#));
    }

    #[test]
    fn serialize_wait_display_without_timeout() {
        let cmd = Command::WaitDisplay {
            idd: 100,
            timeout_ms: None,
        };
        let json = serde_json::to_string(&cmd).unwrap();
        assert!(json.contains(r#""cmd":"wait_display""#));
        assert!(json.contains(r#""idd":100"#));
        assert!(!json.contains("timeout_ms"));
    }

    #[test]
    fn serialize_click() {
        let cmd = Command::Click { idc: 42 };
        let json = serde_json::to_string(&cmd).unwrap();
        assert_eq!(json, r#"{"cmd":"click","idc":42}"#);
    }

    #[test]
    fn serialize_screenshot() {
        let cmd = Command::Screenshot {
            path: "/tmp/shot.png".into(),
        };
        let json = serde_json::to_string(&cmd).unwrap();
        assert!(json.contains(r#""cmd":"screenshot""#));
        assert!(json.contains(r#""path":"/tmp/shot.png""#));
    }

    #[test]
    fn describe_response_deserialize() {
        let json = r#"{
            "ok": true,
            "version": 1,
            "commands": [
                {"name": "ping", "description": "Health check", "params": []}
            ],
            "events": [
                {"name": "ready", "description": "Game initialized", "fields": [
                    {"name": "idd", "type": "int"}
                ]}
            ]
        }"#;
        let desc: DescribeResponse = serde_json::from_str(json).unwrap();
        assert!(desc.ok);
        assert_eq!(desc.version, 1);
        assert_eq!(desc.commands.len(), 1);
        assert_eq!(desc.commands[0].name, "ping");
        assert_eq!(desc.events.len(), 1);
        assert_eq!(desc.events[0].fields[0].name, "idd");
    }

    #[test]
    fn serialize_eval() {
        let cmd = Command::Eval {
            code: "triVersion".into(),
        };
        let json = serde_json::to_string(&cmd).unwrap();
        assert!(json.contains(r#""cmd":"eval""#));
        assert!(json.contains(r#""code":"triVersion""#));
    }

    #[test]
    fn serialize_exec() {
        let cmd = Command::Exec {
            code: "hint 'hello'".into(),
        };
        let json = serde_json::to_string(&cmd).unwrap();
        assert!(json.contains(r#""cmd":"exec""#));
        assert!(json.contains(r#""code":"hint 'hello'""#));
    }
}
