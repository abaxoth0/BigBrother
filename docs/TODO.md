## Existing issues

These issues are split into five independently testable phases. See [TODO implementation phases](todo-phases.md) for scope and VM checks. Phases 1–3 are merged. Phase 4 is implemented and awaiting VM verification; Phase 5 remains open.

- **Server GUI connectivity — Phase 1 implemented, VM verified**: backend connectivity/settings availability now follow a successful, bounded pipe status request. Windows service status is displayed separately; it no longer overwrites backend connectivity or clears data for a reachable console-mode backend. Verify console-mode startup, service startup/shutdown, and reconnection using the Phase 1 checklist.

- **Private-key permissions — Phase 4 implemented, awaiting VM verification**: Windows secret files/directories use protected ACLs for SYSTEM/Administrators and an Administrators owner. Client config and identity-key permissions are repaired on startup; newly created keys have a private ACL before writing and unreadable/corrupt existing keys are not regenerated. The server migrates its active database and TLS identity into a private `data` directory so SQLite sidecars inherit private permissions; restricted originals are retained. Installers run permission setup before starting services. Verify ownership and access denial on NTFS with a standard-user account; Wine cannot verify enforcement.

- **Token rotation recovery — Phase 2 merged**: explicit invalid-token responses invalidate both caches and trigger a signed challenge plus one request retry. Failed recovery is throttled; transport/TLS failures preserve credentials. Active subscriptions revalidate their token on heartbeat, and the server GUI explains automatic delivery. Failed config replacement preserves the original and lets the client retain a replacement in memory. Verify rotation, reconnect, wrong-identity rejection, and offline behavior using the Phase 2 checklist.

- **Certificate rotation workflow — Phase 3 merged**: the server GUI displays/copies the running certificate's SHA-256 fingerprint. The client can inspect a peer certificate without sending credentials or changing trust, then explicitly apply a fingerprint obtained through a trusted channel. Acceptance rechecks the peer and saved endpoint/pin, persists only verified trust, and reconnects existing subscriptions. Verify planned rotation, unexpected replacement, persistence, and stale inspection using the Phase 3 checklist.

- **Discovery rate-limit identity**: reply throttling exists, but its key is `rAddr.String()` (source IP plus UDP port). A sender can obtain a separate rate budget by changing its source port. Limit replies by source IP and verify that changing ports does not reset the budget.

## Deployment checks

- Confirm router/reverse-proxy exposure for each deployment; no router/proxy configuration in this repository establishes which ports are publicly exposed. The backend TCP port is configurable (default `1984`), and discovery uses UDP `42069`. Keep discovery on the intended LAN; review the installers' Windows Firewall rules, which currently allow the default ports on private/domain profiles and do not adapt to a changed backend port.

## Feature ideas

Suggested starting order: whitelist validation and rule testing, then client policy status. Effort estimates are approximate.

- **Whitelist validator and rule tester** (small–medium): enter a domain or IP to see which rule matches; detect duplicate rules, malformed entries, and conflicting exceptions before saving.
- **Client policy status** (medium): show whether each client has applied the latest whitelist, its effective filtration state, and its IPv6 blocking preference.
- **Temporary access** (medium): allow a domain for a limited time, then revoke access automatically.
- **Whitelist history and rollback** (medium): review changes and restore a previous version when an edit breaks access.
- **Client groups and separate policies** (medium–large): assign different whitelists to departments, classrooms, or individual machines.
- **Full IPv6 whitelist support** (large): filter IPv6 destinations instead of choosing between blocking or allowing all IPv6 traffic.
- **Blocked traffic dashboard** (medium–large): aggregate blocked destinations by client, with counts and last occurrence, to simplify troubleshooting.
