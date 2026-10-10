## Existing issues

- **Server GUI connectivity**: `RefreshServerStatusAsync` already derives connectivity from a successful pipe request, but `StartServiceStatusPolling` also assigns `IsConnected` from the Windows service state and clears server data when that state changes to stopped/missing. These competing updates can misreport a reachable console-mode backend or a running service whose pipe is unavailable. Use pipe reachability for backend connectivity/settings availability and keep service status separate. Verify console-mode startup and service startup/shutdown transitions.

- **Private-key permissions**: still open. The server writes `<data>/tls/server.key` with Go mode `0600`, but does not explicitly restrict its Windows ACL; the client writes `client_sigkey.key` beside the daemon using `fopen`. Neither installer sets explicit key ACLs. Apply restrictive Windows ACLs for SYSTEM/Administrators, including existing key files, and verify that ordinary users cannot read or replace them. Review permissions on the token-bearing server database and client `config.ini` as well.

- **Token rotation recovery**: server-side rotation already exists, but client recovery is incomplete. `fetch_token_if_needed` skips fetching whenever either the persisted or in-memory token is nonempty; `CONNECT`/`SUBSCRIBE` authentication failures do not invalidate those caches. Clear both caches on an explicit invalid-token response and retry the signed challenge with a bounded retry policy. The server GUI still instructs users to enter the new token in client settings even though that field was removed; update that message. Verify reconnect after rotation without manual config edits.

- **Certificate rotation workflow**: fingerprint pinning already rejects a changed certificate, but recovery requires manual config edits. Clearing `[server] fingerprint` permits trust-on-first-use again; it does not verify the replacement server. Add an administrator-controlled way to inspect and verify the replacement fingerprint before updating the pin. Test planned rotation and unexpected certificate replacement.

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
