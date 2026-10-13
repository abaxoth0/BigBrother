## Existing issues

## Feature ideas

Suggested starting order: whitelist validation and rule testing, then client policy status. Effort estimates are approximate.

- **Whitelist validator and rule tester** (small–medium): enter a domain or IP to see which rule matches; detect duplicate rules, malformed entries, and conflicting exceptions before saving.
- **Client policy status** (medium): show whether each client has applied the latest whitelist, its effective filtration state, and its IPv6 blocking preference.
- **Temporary access** (medium): allow a domain for a limited time, then revoke access automatically.
- **Whitelist history and rollback** (medium): review changes and restore a previous version when an edit breaks access.
- **Client groups and separate policies** (medium–large): assign different whitelists to departments, classrooms, or individual machines.
- **Full IPv6 whitelist support** (large): filter IPv6 destinations instead of choosing between blocking or allowing all IPv6 traffic.
- **Blocked traffic dashboard** (medium–large): aggregate blocked destinations by client, with counts and last occurrence, to simplify troubleshooting.
