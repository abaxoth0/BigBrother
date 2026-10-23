## Existing issues

## Feature ideas

- **Add rules for ports** (medium)
- **Temporary access** (medium): allow a domain for a limited time, then revoke access automatically.
- **Client groups and separate policies** (medium–large): assign different whitelists to departments, classrooms, or individual machines.
- **Full IPv6 whitelist support** (large): filter IPv6 destinations instead of choosing between blocking or allowing all IPv6 traffic.

- **Rework client backend** (large): Rewrite in C++ and make it a hub for all client-based apps. So in future when will be added a new apps on client side - they will communicate only with client backend. They will no need to know about any other applications on client and how to communicate with them - need to just let backend do this part: it will orchestrate the whole communicattion pipeline as needed. This is will strongly improve architecture and make it much more modular and flexible on client side.
- **Add interactive visualizer for client-server network** (medium-large): this will help to find machines that looses connection. This must be interactive in a sense that user on server will be able to organize this scheme as they want, to match physical layout of machines in a network.
- **Blocked traffic dashboard** (medium–large): aggregate blocked destinations by client, with counts and last occurrence, to simplify troubleshooting.

- **Whitelist history and rollback** (medium): review changes and restore a previous version when an edit breaks access.
