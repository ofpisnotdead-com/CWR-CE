//! TCP connection to a game harness port.

use crate::protocol::{Command, DescribeResponse, Event, Message, Response};
use anyhow::{Context, Result};
use std::collections::VecDeque;
use std::time::Duration;
use tokio::io::{AsyncBufReadExt, AsyncWriteExt, BufReader};
use tokio::net::TcpStream;

/// Configuration for [`HarnessClient`] timeouts and connection behaviour.
#[derive(Debug, Clone)]
pub struct ClientConfig {
    /// Timeout for TCP connection attempts.
    pub connect_timeout: Duration,
    /// Timeout waiting for command responses.
    pub command_timeout: Duration,
    /// Default timeout for event waits.
    pub event_timeout: Duration,
    /// Maximum connection retry attempts (used by [`GameInstance`](super::GameInstance)).
    pub max_retries: u32,
    /// Delay between connection retries.
    pub retry_delay: Duration,
}

impl Default for ClientConfig {
    fn default() -> Self {
        Self {
            connect_timeout: Duration::from_secs(5),
            command_timeout: Duration::from_secs(10),
            event_timeout: Duration::from_secs(10),
            max_retries: 10,
            retry_delay: Duration::from_secs(1),
        }
    }
}

/// Client connection to a single game harness instance.
pub struct HarnessClient {
    reader: BufReader<tokio::io::ReadHalf<TcpStream>>,
    writer: tokio::io::WriteHalf<TcpStream>,
    event_queue: VecDeque<Event>,
    config: ClientConfig,
}

impl HarnessClient {
    /// Connect to a running harness using default configuration.
    pub async fn connect(address: &str) -> Result<Self> {
        Self::connect_with_config(address, ClientConfig::default()).await
    }

    /// Connect to a running harness with explicit configuration.
    pub async fn connect_with_config(address: &str, config: ClientConfig) -> Result<Self> {
        let stream = tokio::time::timeout(config.connect_timeout, TcpStream::connect(address))
            .await
            .map_err(|_| {
                anyhow::anyhow!(
                    "connection to harness at {address} timed out after {:?}",
                    config.connect_timeout
                )
            })?
            .with_context(|| format!("failed to connect to harness at {address}"))?;
        let (read_half, write_half) = tokio::io::split(stream);
        Ok(Self {
            reader: BufReader::new(read_half),
            writer: write_half,
            event_queue: VecDeque::new(),
            config,
        })
    }

    /// Get a reference to the client configuration.
    pub const fn config(&self) -> &ClientConfig {
        &self.config
    }

    /// Send a command and wait for the response. Events received before the
    /// response are buffered in the event queue.
    pub async fn send(&mut self, cmd: &Command) -> Result<Response> {
        let mut json = serde_json::to_string(cmd)?;
        json.push('\n');
        self.writer
            .write_all(json.as_bytes())
            .await
            .context("broken pipe: failed to send command to harness")?;
        self.writer.flush().await?;

        // Read lines until we get a response (non-event)
        loop {
            let mut line = String::new();
            let n = self
                .reader
                .read_line(&mut line)
                .await
                .context("failed to read from harness (broken pipe?)")?;
            if n == 0 {
                anyhow::bail!("harness connection closed unexpectedly while awaiting response");
            }
            let line = line.trim();
            if line.is_empty() {
                continue;
            }
            match Message::parse(line)? {
                Message::Response(r) => return Ok(r),
                Message::Event(e) => self.event_queue.push_back(e),
            }
        }
    }

    /// Send a ping and verify we get ok=true.
    pub async fn ping(&mut self) -> Result<()> {
        let resp = self.send(&Command::Ping).await?;
        if resp.ok {
            Ok(())
        } else {
            anyhow::bail!("ping failed: {:?}", resp.error)
        }
    }

    /// Send describe command and parse the full protocol description.
    pub async fn describe(&mut self) -> Result<DescribeResponse> {
        let mut json = serde_json::to_string(&Command::Describe)?;
        json.push('\n');
        self.writer.write_all(json.as_bytes()).await?;
        self.writer.flush().await?;

        loop {
            let mut line = String::new();
            let n = self.reader.read_line(&mut line).await?;
            if n == 0 {
                anyhow::bail!("harness connection closed");
            }
            let line = line.trim();
            if line.is_empty() {
                continue;
            }
            // Try to parse as describe response first
            let v: serde_json::Value = serde_json::from_str(line)?;
            if v.get("event").is_some() {
                self.event_queue.push_back(serde_json::from_value(v)?);
                continue;
            }
            return Ok(serde_json::from_value(v)?);
        }
    }

    /// Drain all buffered events.
    pub fn drain_events(&mut self) -> Vec<Event> {
        self.event_queue.drain(..).collect()
    }

    /// Wait for a specific event type, with timeout.
    pub async fn wait_for_event(
        &mut self,
        event_type: &str,
        timeout: std::time::Duration,
    ) -> Result<Event> {
        // Check buffered events first
        if let Some(pos) = self.event_queue.iter().position(|e| e.event == event_type) {
            return Ok(self.event_queue.remove(pos).unwrap());
        }

        let deadline = tokio::time::Instant::now() + timeout;
        loop {
            let remaining = deadline.saturating_duration_since(tokio::time::Instant::now());
            if remaining.is_zero() {
                anyhow::bail!("timeout waiting for event '{event_type}'");
            }
            let mut line = String::new();
            match tokio::time::timeout(remaining, self.reader.read_line(&mut line)).await {
                Ok(Ok(0)) => anyhow::bail!("harness connection closed"),
                Ok(Ok(_)) => {
                    let line = line.trim();
                    if line.is_empty() {
                        continue;
                    }
                    match Message::parse(line)? {
                        Message::Event(e) if e.event == event_type => return Ok(e),
                        Message::Event(e) => self.event_queue.push_back(e),
                        Message::Response(_) => {} // discard unexpected responses
                    }
                }
                Ok(Err(e)) => return Err(e.into()),
                Err(_) => anyhow::bail!("timeout waiting for event '{event_type}'"),
            }
        }
    }

    /// Send a key press (down + up).
    pub async fn key(&mut self, scancode: u32) -> Result<Response> {
        self.send(&Command::Key {
            sc: scancode,
            r#mod: None,
            hold: None,
        })
        .await
    }

    /// Send exit command.
    pub async fn exit(&mut self) -> Result<Response> {
        self.send(&Command::Exit).await
    }

    /// Query game state.
    pub async fn query(&mut self, what: &str) -> Result<Response> {
        self.send(&Command::Query {
            what: what.to_string(),
        })
        .await
    }

    pub async fn set_http_fixture(&mut self, url: &str, target: &str) -> Result<()> {
        let response = self
            .send(&Command::HttpFixture {
                url: url.to_string(),
                target: target.to_string(),
            })
            .await?;
        if response.ok {
            Ok(())
        } else {
            anyhow::bail!(
                "http_fixture failed: {}",
                response.error.as_deref().unwrap_or("unknown error")
            );
        }
    }

    // ── Convenience methods ────────────────────────────────────────────────

    /// Wait for the "ready" event (first display shown after launch).
    pub async fn wait_for_ready(&mut self, timeout: Duration) -> Result<Event> {
        self.wait_for_event("ready", timeout).await
    }

    /// Send `wait_display` command (blocks server-side until IDD appears).
    pub async fn wait_for_display(&mut self, idd: i32, timeout: Duration) -> Result<Response> {
        let cmd = Command::WaitDisplay {
            idd,
            timeout_ms: Some(u32::try_from(timeout.as_millis()).unwrap_or(u32::MAX)),
        };
        self.send(&cmd).await
    }

    /// Send click command for the given control ID.
    pub async fn click(&mut self, idc: i32) -> Result<Response> {
        self.send(&Command::Click { idc }).await
    }

    /// Send screenshot command, saving to `path`.
    pub async fn screenshot(&mut self, path: &str) -> Result<Response> {
        self.send(&Command::Screenshot {
            path: path.to_string(),
        })
        .await
    }

    /// Send exit and wait for the response (semantic alias for [`Self::exit`]).
    pub async fn exit_game(&mut self) -> Result<Response> {
        self.send(&Command::Exit).await
    }

    /// Evaluate an SQF expression and return the result string.
    pub async fn eval(&mut self, code: &str) -> Result<String> {
        let resp = self
            .send(&Command::Eval {
                code: code.to_string(),
            })
            .await?;
        if !resp.ok {
            anyhow::bail!(
                "eval failed: {}",
                resp.error.as_deref().unwrap_or("unknown error")
            );
        }
        Ok(resp
            .data
            .get("result")
            .and_then(|v| v.as_str())
            .unwrap_or("")
            .to_string())
    }

    /// Execute SQF code (fire-and-forget, no return value).
    pub async fn exec(&mut self, code: &str) -> Result<()> {
        let resp = self
            .send(&Command::Exec {
                code: code.to_string(),
            })
            .await?;
        if !resp.ok {
            anyhow::bail!(
                "exec failed: {}",
                resp.error.as_deref().unwrap_or("unknown error")
            );
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use tokio::io::AsyncReadExt;
    use tokio::net::TcpListener;

    #[tokio::test]
    async fn connect_and_ping() {
        // Start a mock harness server
        let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
        let addr = listener.local_addr().unwrap();

        let server = tokio::spawn(async move {
            let (mut stream, _) = listener.accept().await.unwrap();
            let mut buf = [0u8; 1024];
            let n = stream.read(&mut buf).await.unwrap();
            let received = std::str::from_utf8(&buf[..n]).unwrap();
            assert!(received.contains(r#""cmd":"ping""#));

            // Send response
            stream.write_all(b"{\"ok\":true}\n").await.unwrap();
        });

        let mut client = HarnessClient::connect(&addr.to_string()).await.unwrap();
        client.ping().await.unwrap();
        server.await.unwrap();
    }

    #[tokio::test]
    async fn connect_with_custom_config() {
        let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
        let addr = listener.local_addr().unwrap();

        let server = tokio::spawn(async move {
            let (mut stream, _) = listener.accept().await.unwrap();
            let mut buf = [0u8; 1024];
            let n = stream.read(&mut buf).await.unwrap();
            let received = std::str::from_utf8(&buf[..n]).unwrap();
            assert!(received.contains(r#""cmd":"ping""#));
            stream.write_all(b"{\"ok\":true}\n").await.unwrap();
        });

        let config = ClientConfig {
            connect_timeout: Duration::from_secs(5),
            command_timeout: Duration::from_secs(15),
            ..ClientConfig::default()
        };
        let mut client = HarnessClient::connect_with_config(&addr.to_string(), config)
            .await
            .unwrap();
        assert_eq!(client.config().command_timeout, Duration::from_secs(15));
        client.ping().await.unwrap();
        server.await.unwrap();
    }

    #[tokio::test]
    async fn wait_for_event_buffers_others() {
        let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
        let addr = listener.local_addr().unwrap();

        let server = tokio::spawn(async move {
            let (mut stream, _) = listener.accept().await.unwrap();
            // Send log event, then ready event
            stream
                .write_all(
                    b"{\"event\":\"log\",\"level\":\"info\",\"cat\":\"Core\",\"msg\":\"init\"}\n",
                )
                .await
                .unwrap();
            stream
                .write_all(b"{\"event\":\"ready\",\"idd\":100}\n")
                .await
                .unwrap();
        });

        let mut client = HarnessClient::connect(&addr.to_string()).await.unwrap();
        let event = client
            .wait_for_event("ready", std::time::Duration::from_secs(5))
            .await
            .unwrap();
        assert_eq!(event.event, "ready");
        assert_eq!(event.data["idd"], 100);

        // Log event should be buffered
        let buffered = client.drain_events();
        assert_eq!(buffered.len(), 1);
        assert_eq!(buffered[0].event, "log");

        server.await.unwrap();
    }

    #[tokio::test]
    async fn wait_for_ready_delegates_to_wait_for_event() {
        let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
        let addr = listener.local_addr().unwrap();

        let server = tokio::spawn(async move {
            let (mut stream, _) = listener.accept().await.unwrap();
            stream
                .write_all(b"{\"event\":\"ready\",\"idd\":42}\n")
                .await
                .unwrap();
        });

        let mut client = HarnessClient::connect(&addr.to_string()).await.unwrap();
        let event = client.wait_for_ready(Duration::from_secs(5)).await.unwrap();
        assert_eq!(event.event, "ready");
        assert_eq!(event.data["idd"], 42);
        server.await.unwrap();
    }

    #[tokio::test]
    async fn click_sends_correct_command() {
        let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
        let addr = listener.local_addr().unwrap();

        let server = tokio::spawn(async move {
            let (mut stream, _) = listener.accept().await.unwrap();
            let mut buf = [0u8; 1024];
            let n = stream.read(&mut buf).await.unwrap();
            let received = std::str::from_utf8(&buf[..n]).unwrap();
            assert!(received.contains(r#""cmd":"click""#));
            assert!(received.contains(r#""idc":101"#));
            stream.write_all(b"{\"ok\":true}\n").await.unwrap();
        });

        let mut client = HarnessClient::connect(&addr.to_string()).await.unwrap();
        let resp = client.click(101).await.unwrap();
        assert!(resp.ok);
        server.await.unwrap();
    }

    #[tokio::test]
    async fn screenshot_sends_correct_command() {
        let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
        let addr = listener.local_addr().unwrap();

        let server = tokio::spawn(async move {
            let (mut stream, _) = listener.accept().await.unwrap();
            let mut buf = [0u8; 1024];
            let n = stream.read(&mut buf).await.unwrap();
            let received = std::str::from_utf8(&buf[..n]).unwrap();
            assert!(received.contains(r#""cmd":"screenshot""#));
            assert!(received.contains(r#""path":"/tmp/shot.png""#));
            stream.write_all(b"{\"ok\":true}\n").await.unwrap();
        });

        let mut client = HarnessClient::connect(&addr.to_string()).await.unwrap();
        let resp = client.screenshot("/tmp/shot.png").await.unwrap();
        assert!(resp.ok);
        server.await.unwrap();
    }

    #[tokio::test]
    async fn exit_game_sends_exit_command() {
        let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
        let addr = listener.local_addr().unwrap();

        let server = tokio::spawn(async move {
            let (mut stream, _) = listener.accept().await.unwrap();
            let mut buf = [0u8; 1024];
            let n = stream.read(&mut buf).await.unwrap();
            let received = std::str::from_utf8(&buf[..n]).unwrap();
            assert!(received.contains(r#""cmd":"exit""#));
            stream.write_all(b"{\"ok\":true}\n").await.unwrap();
        });

        let mut client = HarnessClient::connect(&addr.to_string()).await.unwrap();
        let resp = client.exit_game().await.unwrap();
        assert!(resp.ok);
        server.await.unwrap();
    }

    #[tokio::test]
    async fn wait_for_display_sends_timeout() {
        let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
        let addr = listener.local_addr().unwrap();

        let server = tokio::spawn(async move {
            let (mut stream, _) = listener.accept().await.unwrap();
            let mut buf = [0u8; 1024];
            let n = stream.read(&mut buf).await.unwrap();
            let received = std::str::from_utf8(&buf[..n]).unwrap();
            assert!(received.contains(r#""cmd":"wait_display""#));
            assert!(received.contains(r#""idd":200"#));
            assert!(received.contains(r#""timeout_ms":5000"#));
            stream.write_all(b"{\"ok\":true}\n").await.unwrap();
        });

        let mut client = HarnessClient::connect(&addr.to_string()).await.unwrap();
        let resp = client
            .wait_for_display(200, Duration::from_secs(5))
            .await
            .unwrap();
        assert!(resp.ok);
        server.await.unwrap();
    }

    #[tokio::test]
    async fn eval_returns_result() {
        let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
        let addr = listener.local_addr().unwrap();

        let server = tokio::spawn(async move {
            let (mut stream, _) = listener.accept().await.unwrap();
            let mut buf = [0u8; 1024];
            let n = stream.read(&mut buf).await.unwrap();
            let received = std::str::from_utf8(&buf[..n]).unwrap();
            assert!(received.contains(r#""cmd":"eval""#));
            assert!(received.contains(r#""code":"triVersion""#));
            stream
                .write_all(b"{\"ok\":true,\"result\":\"tri/1\"}\n")
                .await
                .unwrap();
        });

        let mut client = HarnessClient::connect(&addr.to_string()).await.unwrap();
        let result = client.eval("triVersion").await.unwrap();
        assert_eq!(result, "tri/1");
        server.await.unwrap();
    }

    #[tokio::test]
    async fn exec_sends_code() {
        let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
        let addr = listener.local_addr().unwrap();

        let server = tokio::spawn(async move {
            let (mut stream, _) = listener.accept().await.unwrap();
            let mut buf = [0u8; 1024];
            let n = stream.read(&mut buf).await.unwrap();
            let received = std::str::from_utf8(&buf[..n]).unwrap();
            assert!(received.contains(r#""cmd":"exec""#));
            assert!(received.contains(r#""code":"hint 'hello'""#));
            stream.write_all(b"{\"ok\":true}\n").await.unwrap();
        });

        let mut client = HarnessClient::connect(&addr.to_string()).await.unwrap();
        client.exec("hint 'hello'").await.unwrap();
        server.await.unwrap();
    }
}
