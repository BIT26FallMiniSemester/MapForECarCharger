# Qt Socket protocol

Protocol version 1 uses a persistent TCP connection. Each message is `[4-byte unsigned big-endian JSON length][UTF-8 JSON]`; the length is 1 to 1,048,576 bytes. TCP reads must accumulate incomplete headers and bodies and loop over coalesced frames.

Request envelope:

```json
{"version":1,"request_id":"req_1","action":"orders.active","token":"<session_token>","data":{}}
```

Response envelope:

```json
{"version":1,"request_id":"req_1","action":"orders.active","code":0,"message":"success","data":null}
```

Clients correlate responses by `request_id`. A timeout or disconnect does not prove a write failed: reconnect, authenticate if the server restarted, then query current state. Reuse `client_request_id` when retrying a recharge. Stop and settle are idempotent by order state.

The authoritative field, role, enum, request, and response definitions for all 41 actions are in [schemas/socket.json](schemas/socket.json). The full charging sequence is in [examples/socket-charging-flow.json](examples/socket-charging-flow.json). HTTP paths, status codes, Authorization headers, and `openapi.yaml` describe only the retired Python transport.

The initial actions are grouped as follows:

- system and authentication: `system.health`, `auth.user.login`, `auth.admin.login`;
- user and wallet: `users.me.get`, `users.me.update`, `users.avatar.set/get`, `wallet.recharges.create/list`;
- map and station: `map.geocode`, `map.route`, `map.snapshot`, `stations.nearby/list/detail/piles.list`, `piles.detail`;
- charging: `orders.create/reserve/start/active/detail/stop/settle/cancel/list`;
- administration: overview, revenue trend, order list, pile status, user, station, and pile management under `admin.*`.

Collections return `{items,page,page_size,total}`. Times are ISO 8601 UTC, money is cents, energy Wh, power W, and duration seconds. Business errors are returned in `code`; malformed frames without a trustworthy request ID close the connection.
