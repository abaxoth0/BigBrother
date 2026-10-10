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
| `GET_PENDING` | none | `name:addr` lines | Get pending users |
| `GET_WHITELISTS` | none | `name:count` lines | Get all whitelists |
| `GET_WHITELIST` | `name` | Entry values (one per line) | Get whitelist entries |
| `CREATE_WHITELIST` | `name` | none | Create new whitelist |
| `DELETE_WHITELIST` | `name` | none | Delete whitelist |
| `RENAME_WHITELIST` | `oldName`, `newName` | none | Rename whitelist |
| `SAVE_WHITELIST` | `name` + entries | none | Save/replace whitelist entries |

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
