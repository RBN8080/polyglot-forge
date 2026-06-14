// Servicio Rust: calcula SHA-256 del archivo recibido.
// 100% biblioteca estándar (sin cargo, sin crates). Compilar con:
//   rustc main.rs -O -o hash-rust.exe
//
// Expone un servidor HTTP mínimo sobre TcpListener:
//   GET  /health  -> {"status":"ok","service":"rust-hash"}
//   POST /        -> body = bytes crudos del archivo; responde JSON con el hash.

use std::io::{Read, Write};
use std::net::{TcpListener, TcpStream};

const PORT: u16 = 8003;

// ---------------------------------------------------------------------------
// Implementación de SHA-256 (FIPS 180-4) con solo std.
// ---------------------------------------------------------------------------
const K: [u32; 64] = [
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
];

fn sha256(data: &[u8]) -> String {
    let mut h: [u32; 8] = [
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
    ];

    // Padding
    let bit_len = (data.len() as u64) * 8;
    let mut msg = data.to_vec();
    msg.push(0x80);
    while msg.len() % 64 != 56 {
        msg.push(0x00);
    }
    msg.extend_from_slice(&bit_len.to_be_bytes());

    for chunk in msg.chunks(64) {
        let mut w = [0u32; 64];
        for i in 0..16 {
            w[i] = u32::from_be_bytes([
                chunk[i * 4],
                chunk[i * 4 + 1],
                chunk[i * 4 + 2],
                chunk[i * 4 + 3],
            ]);
        }
        for i in 16..64 {
            let s0 = w[i - 15].rotate_right(7) ^ w[i - 15].rotate_right(18) ^ (w[i - 15] >> 3);
            let s1 = w[i - 2].rotate_right(17) ^ w[i - 2].rotate_right(19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16]
                .wrapping_add(s0)
                .wrapping_add(w[i - 7])
                .wrapping_add(s1);
        }

        let (mut a, mut b, mut c, mut d, mut e, mut f, mut g, mut hh) =
            (h[0], h[1], h[2], h[3], h[4], h[5], h[6], h[7]);

        for i in 0..64 {
            let s1 = e.rotate_right(6) ^ e.rotate_right(11) ^ e.rotate_right(25);
            let ch = (e & f) ^ ((!e) & g);
            let t1 = hh
                .wrapping_add(s1)
                .wrapping_add(ch)
                .wrapping_add(K[i])
                .wrapping_add(w[i]);
            let s0 = a.rotate_right(2) ^ a.rotate_right(13) ^ a.rotate_right(22);
            let maj = (a & b) ^ (a & c) ^ (b & c);
            let t2 = s0.wrapping_add(maj);

            hh = g;
            g = f;
            f = e;
            e = d.wrapping_add(t1);
            d = c;
            c = b;
            b = a;
            a = t1.wrapping_add(t2);
        }

        h[0] = h[0].wrapping_add(a);
        h[1] = h[1].wrapping_add(b);
        h[2] = h[2].wrapping_add(c);
        h[3] = h[3].wrapping_add(d);
        h[4] = h[4].wrapping_add(e);
        h[5] = h[5].wrapping_add(f);
        h[6] = h[6].wrapping_add(g);
        h[7] = h[7].wrapping_add(hh);
    }

    let mut out = String::with_capacity(64);
    for v in h.iter() {
        out.push_str(&format!("{:08x}", v));
    }
    out
}

// ---------------------------------------------------------------------------
// Servidor HTTP mínimo
// ---------------------------------------------------------------------------
fn read_request(stream: &mut TcpStream) -> Option<(String, String, Vec<u8>)> {
    let mut buf: Vec<u8> = Vec::new();
    let mut tmp = [0u8; 4096];

    // Leer hasta encontrar el fin de cabeceras (\r\n\r\n)
    let header_end;
    loop {
        let n = stream.read(&mut tmp).ok()?;
        if n == 0 {
            return None;
        }
        buf.extend_from_slice(&tmp[..n]);
        if let Some(pos) = find_subslice(&buf, b"\r\n\r\n") {
            header_end = pos + 4;
            break;
        }
        if buf.len() > 1_000_000_0 {
            return None;
        }
    }

    let header_str = String::from_utf8_lossy(&buf[..header_end]).to_string();
    let mut lines = header_str.split("\r\n");
    let request_line = lines.next().unwrap_or("");
    let mut parts = request_line.split_whitespace();
    let method = parts.next().unwrap_or("").to_string();
    let path = parts.next().unwrap_or("").to_string();

    // Content-Length
    let mut content_length = 0usize;
    for line in lines {
        let lower = line.to_lowercase();
        if let Some(rest) = lower.strip_prefix("content-length:") {
            content_length = rest.trim().parse().unwrap_or(0);
        }
    }

    let mut body = buf[header_end..].to_vec();
    while body.len() < content_length {
        let n = stream.read(&mut tmp).ok()?;
        if n == 0 {
            break;
        }
        body.extend_from_slice(&tmp[..n]);
    }
    body.truncate(content_length);

    Some((method, path, body))
}

fn find_subslice(haystack: &[u8], needle: &[u8]) -> Option<usize> {
    haystack
        .windows(needle.len())
        .position(|window| window == needle)
}

fn send_json(stream: &mut TcpStream, status: &str, body: &str) {
    let response = format!(
        "HTTP/1.1 {}\r\nContent-Type: application/json\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{}",
        status,
        body.len(),
        body
    );
    let _ = stream.write_all(response.as_bytes());
    let _ = stream.flush();
}

fn handle(mut stream: TcpStream) {
    let (method, path, body) = match read_request(&mut stream) {
        Some(r) => r,
        None => return,
    };

    if method == "GET" && path == "/health" {
        send_json(
            &mut stream,
            "200 OK",
            "{\"status\":\"ok\",\"service\":\"rust-hash\"}",
        );
        return;
    }

    if method == "POST" {
        let hash = sha256(&body);
        let json = format!(
            "{{\"service\":\"rust-hash\",\"algo\":\"sha256\",\"hash\":\"{}\",\"bytes\":{}}}",
            hash,
            body.len()
        );
        send_json(&mut stream, "200 OK", &json);
        return;
    }

    send_json(
        &mut stream,
        "404 Not Found",
        "{\"error\":\"not found\"}",
    );
}

fn main() {
    // Test rápido de cordura del SHA-256 contra el vector conocido de "abc".
    debug_assert_eq!(
        sha256(b"abc"),
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
    );

    let addr = format!("127.0.0.1:{}", PORT);
    let listener = TcpListener::bind(&addr).expect("no se pudo enlazar el puerto");
    println!("[rust-hash] escuchando en http://{}  (SHA-256)", addr);

    for stream in listener.incoming() {
        match stream {
            Ok(s) => {
                // Un hilo por conexión: simple y suficiente para el esqueleto.
                std::thread::spawn(move || handle(s));
            }
            Err(e) => eprintln!("[rust-hash] error de conexión: {}", e),
        }
    }
}
