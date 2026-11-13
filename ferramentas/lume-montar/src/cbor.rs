//! CBOR (RFC 8949), só o que os metadados dos plugins do Qt usam.

use serde_json::Value;

fn cabeca(tipo: u8, n: u64) -> Vec<u8> {
    if n < 24 {
        return vec![tipo << 5 | n as u8];
    }
    for (extra, tam) in [(24u8, 1usize), (25, 2), (26, 4), (27, 8)] {
        if tam == 8 || n < 1u64 << (8 * tam) {
            let mut r = vec![tipo << 5 | extra];
            r.extend_from_slice(&n.to_be_bytes()[8 - tam..]);
            return r;
        }
    }
    unreachable!()
}

pub fn cbor(v: &Value) -> Vec<u8> {
    match v {
        Value::Bool(b) => vec![if *b { 0xf5 } else { 0xf4 }],
        Value::Null => vec![0xf6],
        Value::Number(n) => {
            if let Some(u) = n.as_u64() {
                cabeca(0, u)
            } else if let Some(i) = n.as_i64() {
                cabeca(1, (-1 - i) as u64)
            } else {
                crate::util::sair(format!("cbor: número não inteiro nos metadados: {n}"))
            }
        }
        Value::String(t) => {
            let mut r = cabeca(3, t.len() as u64);
            r.extend_from_slice(t.as_bytes());
            r
        }
        Value::Array(a) => {
            let mut r = cabeca(4, a.len() as u64);
            for x in a {
                r.extend(cbor(x));
            }
            r
        }
        Value::Object(m) => {
            let mut r = cabeca(5, m.len() as u64);
            for (k, x) in m {
                r.extend(cbor(&Value::String(k.clone())));
                r.extend(cbor(x));
            }
            r
        }
    }
}
