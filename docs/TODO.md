- Server GUI: `IsConnected`/settings availability is driven by the **Windows service** state (via `ServiceManager`), not by pipe reachability. A server daemon started as a **console process** (not registered as a service) will not show as connected in the GUI. Planned: add a pipe-liveness fallback so console/dev runs reflect connectivity.

- Security / runtime hygiene:
  - The server TLS private key lives in `<data>/tls/server.key` written 0600; Windows ignores most POSIX modes. Ideally set an explicit ACL (SYSTEM/Administrators only) on that directory. Same for the client identity key `client_sigkey.key` next to the client daemon.
  - Certificate/token rotation: regenerating the server cert requires clearing `[server] fingerprint` in the client `config.ini` (the client pins it). Tokens rotate with the server GUI's «Сбросить токен», and the client re-fetches automatically via the signed challenge when its token is cleared.
  - The reverse proxy/router exposes only TCP 1984; discovery uses UDP 42069 and is rate-limited per source.

## Feature ideas

Suggested starting order: whitelist validation and rule testing, then client policy status. Effort estimates are approximate.

- **Whitelist validator and rule tester** (small–medium): enter a domain or IP to see which rule matches; detect duplicate rules, malformed entries, and conflicting exceptions before saving.
- **Client policy status** (medium): show whether each client has applied the latest whitelist, its effective filtration state, and its IPv6 blocking preference.
- **Temporary access** (medium): allow a domain for a limited time, then revoke access automatically.
- **Whitelist history and rollback** (medium): review changes and restore a previous version when an edit breaks access.
- **Client groups and separate policies** (medium–large): assign different whitelists to departments, classrooms, or individual machines.
- **Full IPv6 whitelist support** (large): filter IPv6 destinations instead of choosing between blocking or allowing all IPv6 traffic.
- **Blocked traffic dashboard** (medium–large): aggregate blocked destinations by client, with counts and last occurrence, to simplify troubleshooting.
