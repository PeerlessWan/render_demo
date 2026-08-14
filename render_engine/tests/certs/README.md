# Loopback TLS certs (unit tests)

Self-signed cert/key for `Net HTTPS self-signed loopback` when `ENGINE_WITH_OPENSSL=ON`.

- `loopback.crt` / `loopback.key` — CN=127.0.0.1, SAN localhost + 127.0.0.1
- Regenerate (Python): use `cryptography` to write PEM into this directory

These are **test-only**; do not use in production.
