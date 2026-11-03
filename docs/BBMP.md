# Big Brother Messaging Protocol (BBMP)

## Overview

BBMP (Big Brother Messaging Protocol) is a simple text-based protocol used for communication between all components of the Big Brother system:

- **Client Backend** (`\\.\pipe\BigBrother.Client.Backend`) - Daemon client backend
- **Server Backend** (`\\.\pipe\BigBrother.Server.Backend`) - Server backend for clients
- **Server Frontend** (`\\.\pipe\BigBrother.Server.Frontend`) - Server frontend

The protocol uses **Type-Length-Value (TLV)** format for both requests and responses, with newline-delimited messages for easy debugging.

---

## Protocol Format

### Request Format

```
<command>\n
<arg1_len>\n<arg1_value>\n
<arg2_len>\n<arg2_value>\n
...\n
```

- First line: command name
- Following lines: TLV arguments (length on one line, value on next line)
- Empty line terminates the request

**Example - `APPROVE:username` (old format) becomes:**
```
APPROVE\n
8\n
username\n
\n
```

---

### Response Format

```
<status>\n
[<data_len>\n<data_value>\n]...
\n
```

- First line: status (`OK` or `ERROR`)
- For `OK`: optional data lines in TLV format
- For `ERROR`: error message in TLV format
- Empty line terminates the response

**Success Example (no data):**
```
OK\n
\n
```

**Success Example (with data - `GET_WHITELISTS`):**
```
OK\n
7\n
whitelist1\n
3\n
5\n
\n
```

**Error Example:**
```
ERROR\n
18\n
Missing whitelist name\n
\n
```

---

## TLV Format

Each TLV argument/value uses:
```
<length>\n
<value>\n
```

- `<length>`: Decimal string representing the length of `<value>`
- `<value>`: The actual value (can contain any characters, including `:`, newlines are handled by the length prefix)
- Newline after both length and value for debugging purposes

---

## Commands

### Client Backend (`\\.\pipe\BigBrother.Client.Backend`)

Used by the daemon client frontend to communicate with the client backend.

| Command | Arguments | Response Data | Description |
|---------|-----------|---------------|-------------|
| `GET_STATUS` | none | `STATUS:<name>:<ip>:<status>:<pid>:<revision>` | Get client status |
| `GET_WHITELIST` | none | Whitelist entries (one per line) | Get current whitelist |
| `RESTART_CLIENT` | none | none | Restart the client |
| `PING` | none | none | Ping the client |
| `GET_LOG_PATH` | none | `LOG_PATH:<client_log>\|<firewall_log>` | Get log file paths |
| `GET_CLIENT_UPDATE` | none | One JSON metadata value, or no data | Explicitly request the server update reference and release notes; never execute a shared file |
| `GET_SAVED_SERVER_FINGERPRINT` | none | saved SHA-256 fingerprint, or empty if unset | Read the persisted pin locally without connecting to the server or changing trust |
| `INSPECT_SERVER_CERTIFICATE` | none | address, port, saved pin, observed SHA-256 fingerprint | Inspect the configured server with TLS only; no credentials are sent and trust is unchanged |
| `UPDATE_SERVER_FINGERPRINT` | address, port, original pin, verified fingerprint | none | Recheck the peer and saved endpoint/pin, then persist the verified replacement; reject stale inspections |
| `GET_IPV6_BLOCK` | none | `0` or `1` | Get the persisted IPv6 blocking preference (default `1`) |
| `SET_IPV6_BLOCK` | `0` or `1` | none | Save the preference and apply it to the firewall; disabling it allows IPv6 without whitelist checks |

The IPv6 preference is stored in `config.ini` under `[filtration] block_ipv6`.
It applies only while filtration is enabled and survives client/firewall restarts.
If the firewall is stopped when the preference is saved, it is applied on startup.

---

### Server Backend (`\\.\pipe\BigBrother.Server.Backend`)

Used by Big Brother clients to communicate with the server.

| Command | Arguments | Response Data | Description |
|---------|-----------|---------------|-------------|
| `GET_WHITELIST` | `username` | Whitelist entries | Get user's whitelist entries |
| `REGISTER` | `name` | none | Register as pending user |
| `CONNECT` | `name` | none | Connect user |
| `DISCONNECT` | `name` | none | Disconnect user |
| `REFRESH` | `name` | none | Refresh connection TTL |
| `GET_STATUS` | none | `uptime:...`, `name:addr:status` lines | Get server status + connections |
| `GET_CLIENT_UPDATE` | `username`, `token` | One JSON metadata value, or no data | Get the small update reference without notes for legacy clients |
| `GET_CLIENT_UPDATE_DETAILS` | `username`, `token` | One JSON metadata value, or no data | Get the update reference and structured release notes over pinned TLS |
| `SUBSCRIBE` | `username`, `token` | Initial `OK`, then pushed events | Open an authenticated event subscription and send client heartbeats |

On an authenticated client subscription, a heartbeat is either `K\n` (legacy)
or one line of `POLICY <json>\n`. Policy JSON requires `whitelistRevision`
(64 lowercase SHA-256 hex characters, or empty when rules cannot be read),
`filtrationEnabled` (the effective firewall state) and `blockIpv6` (the saved
client preference); both flags are JSON booleans. Optional `canSetIpv6Block`
advertises support for targeted server control and defaults to `false` for older
clients. Clients report immediately
after initial synchronization and every ten seconds. The server revalidates the
subscription token before accepting each heartbeat and records its receipt time.
Reports belong to the authenticated client and are discarded on disconnection.
Changed reports publish `CLIENT_POLICY_CHANGED` with `name` so the administrator
frontend can refresh without waiting for its timer; identical reports only renew
activity and connection lifetime.

Subscriptions belong to a connection session. `SUBSCRIBE` establishes a session
when needed; disconnect, expiry, user deletion and shutdown close its event
streams. Heartbeats and cleanup from an older session cannot refresh or delete a
replacement session. An administrator's `DISCONNECT` sends a final targeted
`CLIENT_DISCONNECTED` event with `name`, then closes the stream (with a one-second
write deadline for stalled subscribers). Updated clients mark themselves offline
and pause automatic reconnect until an explicit client `CONNECT` or daemon
restart. A manual client disconnect also pauses reconnect; an unexpected network
failure continues to use automatic retry. Deploy both updated daemons for this
behavior.

Whitelist source text accepts a standalone `[N]` as its first non-comment, nonempty entry to set a default priority. An inline `[N]rule` prefix overrides that default for one rule; the prefix precedes `!` for exclusions. Priorities range from 0 to 2147483647; an omitted declaration means 0. Source order and the declaration are preserved by `SAVE_WHITELIST` / `GET_WHITELIST` on the administrator pipe, and declarations are excluded from rule counts.

The server resolves defaults independently for each source whitelist before composition. Client downloads and firewall readback contain only rules with effective priorities: nonzero priorities use `[N]rule`, zero priorities have no prefix, and default declarations are omitted. The firewall compares matching host/IP and destination-port rules by priority; an exclusion wins at equal priority. Exact allow/exclusion pairs remain in the composition for matching; conflict metadata includes `allowedPriority` / `excludedPriority` (omitted when zero). Priority changes are included in whitelist revisions and apply to existing DNS-to-IP associations.

The revision hashes the firewall's actual rules: trim whitespace, lowercase
ASCII, remove empty/comment lines, deduplicate, sort bytewise and hash each rule
followed by `\n`. Exception rules retain `!`. The empty whitelist has the SHA-256
digest of an empty byte sequence. The server compares this with the revision of
its composed active rules; downloading a whitelist alone does not acknowledge it.

An administrator can request a fresh check with `SYNC_WHITELISTS`. The server
composes the saved active lists once and publishes `WHITELIST_SYNC_REQUESTED`
with `revision` (the SHA-256 digest). Updated clients read the firewall's current
rules again; matching clients send a policy report without downloading anything.
Clients with mismatched or unreadable rules invalidate their last-download cache,
fetch the current server whitelist and replace the firewall rules. Failed fetches
or applications retain the existing rules and retry on the local health timer.
An empty composition is also applied when needed. Clients immediately report
their actual policy after handling the event. Older clients ignore this event;
deploy updated client daemons to enable manual repair.

---

### Server Frontend (`\\.\pipe\BigBrother.Server.Frontend`)

Used by the server frontend (C# WPF app) to manage the server.

| Command | Arguments | Response Data | Description |
|---------|-----------|---------------|-------------|
| `GET_ACTIVE_WHITELIST` | none | Active whitelist name | Get active whitelist |
| `SET_ACTIVE_WHITELIST` | `name` | none | Set active whitelist |
| `GET_SERVER_STATUS` | none | none | Check if server is running |
| `APPROVE` | `name` | none | Approve pending user |
| `REJECT` | `name` | none | Reject pending user |
| `DISCONNECT` | `name` | none | Disconnect a user |
| `DELETE_USERS` | `name` | none | Delete user(s) |
| `CHANGE_NAME` | `oldName`, `newName` | none | Change user name |
| `GET_CLIENTS` | none | `name:addr:status` lines | Get connected clients |
| `GET_CLIENTS_DETAILS` | none | One JSON array | Get connected clients with policy reports and activity timestamps |
| `GET_REGISTERED_USERS` | none | One JSON array of `name`/`address` objects | List all registered users, including offline users, through the administrator pipe; credentials are excluded |
| `SET_CLIENT_IPV6_BLOCK` | `name`, `0` or `1` | none | Request a persisted IPv6 blocking preference for a connected client supporting remote control |
| `GET_PENDING` | none | `name:addr` lines | Get pending users |
| `GET_WHITELISTS` | none | `name:count:active` lines | Get all regular and temporary whitelists (legacy response) |
| `GET_WHITELISTS_DETAILS` | none | JSON array | Get names, active flags and optional `temporary` timer metadata (`durationSeconds`, `remainingSeconds`, `autoDelete`) |
| `CREATE_TEMPORARY_WHITELIST` | name, durationSeconds, autoDelete (0/1) | OK | Create an inactive temporary whitelist with a positive duration |
| `UPDATE_WHITELIST_TIMER` | name, durationSeconds, remainingSeconds or `unchanged`, autoDelete (0/1) | OK | Apply the initial-duration delta to remaining time, or set remaining time explicitly; zero expires immediately |
| `RESET_WHITELIST_TIMERS` | one or more names | OK | Reset temporary countdowns to their initial durations without changing activity |
| `GET_SAVE_TEMPORARY_WHITELIST_STATE` | none | 0/1 | Read shutdown-state persistence preference (default 0) |
| `SET_SAVE_TEMPORARY_WHITELIST_STATE` | enabled (0/1) | OK | Save or discard temporary activity/countdown snapshots at backend shutdown |
| `GET_WHITELIST` | `name` | Entry values (one per line) | Get whitelist entries |
| `CREATE_WHITELIST` | `name` | none | Create new whitelist |
| `DELETE_WHITELIST` | `name` | none | Delete whitelist |
| `RENAME_WHITELIST` | `oldName`, `newName` | none | Rename whitelist |
| `SAVE_WHITELIST` | `name` + entries | none | Save/replace whitelist entries |
| `SYNC_WHITELISTS` | none | none | Broadcast the saved composed whitelist revision for clients to check and repair differences |
| `GET_CLIENT_UPDATE` | none | One JSON metadata value, or no data | Read the published client update |
| `SET_CLIENT_UPDATE` | JSON metadata, or an empty string | none | Publish/withdraw an update reference through the administrator pipe only; persist before sending `CLIENT_UPDATE_CHANGED` |

Each `GET_CLIENTS_DETAILS` object contains `name`, `address`, `lastActivity`
(RFC 3339 timestamp), `policy` (the reported object above, or `null` before the
first report), and `whitelistCurrent` (`true`/`false`, or `null` when no revision
is available). The frontend converts timestamps to local time. Structured fields
preserve colons in client names and IPv6 addresses. `GET_CLIENTS` remains available
for older frontends.

The frontend combines `GET_REGISTERED_USERS`, live client details and pending
registrations for «Все пользователи». Registered users retain their row identity
and selection across connection changes; offline policy/activity is unknown.
Pending registrations remain distinct even when their name matches a registered
user. The registry command is unavailable to remote TCP clients.

`SET_CLIENT_IPV6_BLOCK` is restricted to the administrator pipe. It publishes
`CLIENT_IPV6_BLOCK_CHANGED` with `name` and `enabled` (`0`/`1`); clients ignore
events addressed to another name. The target persists the preference with the
same operation used by its settings UI, applies it when the firewall is
available, and immediately reports its actual policy even if saving failed.
`OK` acknowledges queuing the request; it does not change the cached client
report. The frontend waits up to twelve seconds for the reported preference to
match before logging success, otherwise reports an error. Disconnected clients
and clients without `canSetIpv6Block` are rejected.

Update metadata contains `version`, `file`, `location`, `size`, `sha256` and
optional `release_notes`. Notes contain a selected changelog `version` and
`sections`, each with a `kind` and string `items` array. Complete metadata is
limited to 128 KiB; the legacy response remains below 8 KiB and omits notes.
The client backend tries `GET_CLIENT_UPDATE_DETAILS` and falls back to
`GET_CLIENT_UPDATE` when an older server reports an unknown command.
The server stores the reference without opening `location`. See
[CLIENT-UPDATES.md](CLIENT-UPDATES.md) for package publication and local recovery.

---

## Implementation Notes

### Reading Responses (Client Side)

1. Read first line → status
2. If status is `ERROR`, read one TLV (error message), then empty line
3. If status is `OK`, keep reading TLV lines until empty line

### Writing Requests (Client Side)

1. Write command name + newline
2. For each argument: write length + newline, then value + newline
3. Write empty line to terminate request

### Newline Delimiter

All lines are terminated with `\n` (newline) for:
- Easy debugging with tools like `netcat`
- Simple parsing with `ReadLine()` / `bufio.Scanner`
- Clear visual separation between TLV components

---

## Error Handling

All errors return:
```
ERROR\n
<msg_len>\n<error_message>\n
\n
```

Common error messages:
- `Missing name`
- `Missing whitelist name`
- `Invalid format, use: COMMAND:arg1:arg2`
- `unknown command: <cmd>`
- `Invalid TLV format`
- `TLV length mismatch`

---

## Transport security (client <-> server)

The client<->server TCP channel (:1984) is **TLS 1.2+**:

- The server listener presents a self-signed EC certificate (generated on first run,
  stored under the data directory in `tls/`).
- The client pins the server certificate's SHA-256 fingerprint (trust-on-first-use,
  persisted in `config.ini` under `[server] fingerprint`).
- The client authenticates itself with a **per-user token** sent with every
  authenticated command (`CONNECT`, `DISCONNECT`, `REFRESH`, `GET_WHITELIST`,
  `SUBSCRIBE`). `REGISTER` stays open for onboarding.

### Token bootstrap (no manual configuration)

1. The client generates a persistent ECDSA-P256 identity key and sends its public
   key with `REGISTER` (`name`, `ip`, `sign_public_hex`). The server binds it at
   approval.
2. To fetch its token the client runs:
   - `GET_TOKEN_CHALLENGE <name>` → returns a single-use nonce (60s TTL).
   - `GET_TOKEN <name> <nonce> <signature_hex>` — the signature is ECDSA-P256/SHA-256
     over the raw bytes `name||nonce`, in DER ASN.1. Returns the token (or
     `NOT_SET` before approval).
3. `CONNECT`/`SUBSCRIBE` then proceed with the token.

### Resilience limits

- TCP listener caps concurrent connections (default 256) and rate-limits
  connections per source IP (default 10/s, burst 20).
- The event bus caps push subscribers (128); exceeding it returns an error.
- Discovery (UDP 42069) rate-limits responses per source (2/s).

Local pipes (`BigBrother.Client.Backend`, `BigBrother.Firewall`,
`BigBrother.Server.Frontend`) are DACL-restricted to SYSTEM/Administrators/Owner,
and the client backend additionally verifies the peer executable is the client GUI.

---

## Version History

| Version | Date | Changes |
|--------|------|---------|
| 1.0 | 2026-05-06 | Initial protocol specification with TLV format |


### Verified certificate rotation

The server administrator pipe supports `GET_CERTIFICATE_FINGERPRINT`, returning the SHA-256 fingerprint of the leaf certificate configured on the running TCP listener. Server settings display/copy this value. Replacing files on disk does not change the running value until restart.

Obtain the replacement fingerprint from server settings through a trusted channel, inspect the saved endpoint in client settings, and explicitly apply the independently verified matching value. Inspection uses a TLS handshake without application commands and never writes a pin. Acceptance probes again, verifies the supplied fingerprint, rejects endpoint/pin changes, and writes the new pin atomically. Existing subscriptions reconnect after a successful pin update. Never clear a pin simply to accept an unexpected certificate.
