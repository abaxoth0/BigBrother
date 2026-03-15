# BigBrother - Distributed Firewall

A Windows kernel-level firewall using WinDivert that filters network traffic based on DNS responses, with centralized whitelist management. Originally designed for classroom environments.

## Architecture

![Architecture diagram](docs/assets/architecture.svg)

## Components

| Component | Description | Location |
|-----------|------------|----------|
| **Daemon** | Windows kernel-level firewall using WinDivert | `firewall/` |
| **Client Backend** | Syncs whitelists from server, sends to daemon via pipe | `client/backend` |
| **Client Frontend** | WPF UI for students | `client/frontend/` |
| **Server Backend** | REST API + SQLite for whitelist management | `server/backend` |
| **Server Frontend** | WPF UI for teachers | `server/frontend` |

---

## Development Stages

### Stage 1: Daemon IPC
**Goal:** Enable external control of firewall service

- Named pipe server: `\\.\pipe\BigBrother`
- Commands:
  | Command | Data | Response |
  |---------|------|----------|
  | `GET_WHITELIST` | - | Whitelist contents |
  | `SET_WHITELIST` | Domains (one per line) | `OK` or `ERROR:msg` |
  | `RELOAD` | - | `OK` |
  | `GET_STATUS` | - | JSON status |

- Hot-reload: re-read whitelist + clear IP allowlist
- Thread-safe command handling

### Stage 2: Client Backend
**Goal:** Build Agent that syncs whitelists from server

- Named pipe client to communicate with daemon
- CLI commands for testing: `status`, `reload`, `set-whitelist`
- Connection handling with retries
- Later: Windows service implementation
- Long-poll logic (every 30s)
- Server unavailable handling (use cached)
- Write to temp file → pipe to daemon

### Stage 3: Server Backend
**Goal:** Central whitelist management with REST API

**Database schema:**
```sql
-- rooms (one per classroom)
CREATE TABLE rooms (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL UNIQUE,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- whitelisted domains
CREATE TABLE whitelist (
    id INTEGER PRIMARY KEY,
    domain TEXT NOT NULL UNIQUE,
    added_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- connected clients
CREATE TABLE clients (
    id INTEGER PRIMARY KEY,
    hostname TEXT NOT NULL,
    room_id INTEGER REFERENCES rooms(id),
    last_seen TIMESTAMP,
    ip_address TEXT
);
```

**REST API:**
| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/whitelist` | Get all domains |
| PUT | `/api/whitelist` | Update domains |
| GET | `/api/clients` | List connected PCs |
| POST | `/api/register` | Client handshake |

**Authentication:**
- Password stored in server config (`config.json`)
- Same password for all clients
- Sent in HTTP header on Client Backend ↔ Server Backend requests

### Stage 4: Server Frontend
**Goal:** GUI for teachers

- Connect to Server Backend via named pipe (local only)
- Password authentication
- Connected PCs list (from `/api/clients`)
- Whitelist editor
- Status indicators

### Stage 5: Client Frontend
**Goal:** GUI for students

- Connect to Client Backend via named pipe
- Show firewall status only (running/stopped/failed)
- No configuration access

- Connect to Agent via named pipe
- Show firewall status only (running/stopped/failed)
- No configuration access

---

## Transport Layer

| Connection | Protocol |
|------------|----------|
| Server Frontend ↔ Server Backend | Named pipe (local) |
| Client Frontend ↔ Client Backend | Named pipe (local) |
| Client Backend ↔ Server Backend | HTTP |
| Client Backend ↔ Daemon | Named pipe (local) |

---

## Project Structure

```
BigBrother/
├── firewall/              # Daemon (WinDivert firewall)
│   ├── src/
│   └── include/
├── client/
│   ├── backend/          # Agent (syncs whitelists, Windows service)
│   └── frontend/         # Client Frontend (WPF)
├── server/
│   ├── backend/          # Server (REST API + SQLite)
│   └── frontend/         # Server Frontend (WPF)
```

---

## Current Status

### Completed
- DNS packet parsing
- Domain whitelist with pattern matching (exact, wildcard, substring)
- IP allowlist with TTL expiration
- Blocking logic for non-whitelisted destinations
- Standalone mode working
- Code review and bug fixes

### Pending
- Stage 1: Daemon IPC
- Stage 2: Client Backend
- Stage 3: Server Backend
- Stage 4: Server Frontend
- Stage 5: Client Frontend

---

## Build

```bash
cd firewall
make clean && make
```

Output: `firewall/build/firewall-service.exe`

---

## Usage (Standalone Mode)

1. Create `whitelist.txt` with domains (one per line):
   ```
   # Comments start with #
   google.com
   *.github.com
   "microsoft"
   ```

2. Run firewall:
   ```
   firewall-service.exe
   ```

---

## Future Considerations

1. **Client service user**: Run as `LocalSystem` or specific user?
2. **Whitelist default**: Start empty or with defaults?
3. **Logging**: Event log? File per PC? Sent to server?
4. **Offline mode**: Block all or allow all if server unreachable?

