//! Live server-status probe over UDP.
//!
//! Sends a framed `EnumPacket` (`[MsgHeader][body]`, CRC-checked — see `crate::framing`) and
//! decodes the framed `SessionPacket` reply.

use std::io;
use std::net::{SocketAddr, ToSocketAddrs, UdpSocket};
use std::time::{Duration, Instant};

use crate::codec::{self, SessionResponse, SESSION_PACKET_4};
use crate::framing::{self, MSG_HEADER_LEN};

/// A reachable server's status plus the measured round-trip.
#[derive(Debug, Clone)]
pub struct ServerStatus {
    pub session: SessionResponse,
    pub ping_ms: u32,
}

/// Probe one server. `Ok(Some(_))` if it answered with a well-formed status within
/// `timeout`; `Ok(None)` if it did not respond (unreachable / wrong protocol).
pub fn query_server<A: ToSocketAddrs>(
    addr: A,
    timeout: Duration,
) -> io::Result<Option<ServerStatus>> {
    let target = addr
        .to_socket_addrs()?
        .next()
        .ok_or_else(|| io::Error::new(io::ErrorKind::InvalidInput, "no address resolved"))?;

    let bind_any: SocketAddr = if target.is_ipv6() {
        "[::]:0".parse()
    } else {
        "0.0.0.0:0".parse()
    }
    .expect("static bind address");
    let socket = UdpSocket::bind(bind_any)?;
    socket.set_read_timeout(Some(timeout))?;

    let request = framing::frame_request(&codec::encode_enum_request(codec::MAGIC_APP), 1);
    let sent = Instant::now();
    socket.send_to(&request, target)?;

    let deadline = sent + timeout;
    let mut buf = [0u8; MSG_HEADER_LEN + SESSION_PACKET_4];
    loop {
        match socket.recv_from(&mut buf) {
            Ok((n, from)) => {
                // Ignore datagrams from a different host (basic anti-spoofing) or that
                // are not a valid response; keep waiting until the deadline.
                if from.ip() == target.ip() {
                    if let Some(session) = framing::unframe_response(&buf[..n])
                        .and_then(codec::decode_session_response)
                    {
                        let ping_ms = u32::try_from(sent.elapsed().as_millis()).unwrap_or(u32::MAX);
                        return Ok(Some(ServerStatus { session, ping_ms }));
                    }
                }
                match deadline.checked_duration_since(Instant::now()) {
                    Some(remaining) if !remaining.is_zero() => {
                        socket.set_read_timeout(Some(remaining))?;
                    }
                    _ => return Ok(None),
                }
            }
            Err(e)
                if e.kind() == io::ErrorKind::WouldBlock || e.kind() == io::ErrorKind::TimedOut =>
            {
                return Ok(None);
            }
            Err(e) => return Err(e),
        }
    }
}

#[cfg(test)]
mod tests {
    use std::net::UdpSocket;
    use std::thread;
    use std::time::Duration;

    use super::query_server;
    use crate::codec::{MAGIC_ENUM_REQUEST, MAGIC_ENUM_RESPONSE, SESSION_PACKET_3};
    use crate::framing;

    // Minimal valid full response with a known name at the right offset.
    fn response_packet(name: &str) -> Vec<u8> {
        let mut p = vec![0u8; SESSION_PACKET_3];
        p[0..4].copy_from_slice(&MAGIC_ENUM_RESPONSE.to_le_bytes());
        p[4..4 + name.len()].copy_from_slice(name.as_bytes());
        p
    }

    #[test]
    fn round_trips_against_a_mock_responder() {
        let server = UdpSocket::bind("127.0.0.1:0").unwrap();
        let server_addr = server.local_addr().unwrap();

        let responder = thread::spawn(move || {
            let mut buf = [0u8; 64];
            let (n, from) = server.recv_from(&mut buf).unwrap();
            // The probe must be a framed enum request: [MsgHeader][8-byte EnumPacket].
            let body = framing::unframe_response(&buf[..n]).expect("framed request");
            assert_eq!(body.len(), 8);
            assert_eq!(
                u32::from_le_bytes(body[0..4].try_into().unwrap()),
                MAGIC_ENUM_REQUEST
            );
            server
                .send_to(
                    &framing::frame_request(&response_packet("MockSrv"), 7),
                    from,
                )
                .unwrap();
        });

        let status = query_server(server_addr, Duration::from_secs(2)).unwrap();
        responder.join().unwrap();

        let status = status.expect("expected a response");
        assert_eq!(status.session.name, "MockSrv");
    }

    #[test]
    fn returns_none_when_no_one_answers() {
        // Bind a port and never reply -> the probe should time out cleanly.
        let silent = UdpSocket::bind("127.0.0.1:0").unwrap();
        let addr = silent.local_addr().unwrap();
        let status = query_server(addr, Duration::from_millis(200)).unwrap();
        assert!(status.is_none());
    }
}
