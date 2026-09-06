# Qt Socket backend

The runtime backend is a headless C++17/Qt 6 application. It uses `QTcpServer` and length-prefixed UTF-8 JSON for client messages, Qt SQL with the QSQLITE driver, and `QNetworkAccessManager` for Tencent Map HTTPS requests.

## Build and test on Ubuntu

```bash
cmake -S server-qt -B server-qt/build -DBUILD_TESTING=ON
cmake --build server-qt/build -j
ctest --test-dir server-qt/build --output-on-failure
```

Required Qt modules: Core, Network, Sql, Test, plus the QSQLITE plugin. CMake is the only supported build entry point.

## Qt-only demonstration

Create an isolated demonstration database and start the server:

```bash
server-qt/build/charger-server --database runtime/demo.db --seed-demo \
  --host 0.0.0.0 --port 9000
```

The seed is idempotent and intentionally refuses to mix demo records into a non-demo business database. Credentials are `admin / admin123` for the admin client and `13900000000` for the user client. Geocoding, nearby route distance and route planning all require a Tencent Map WebService key. A missing, disabled or rejected key returns business code `50301`; the server does not fabricate local distances.

For the course showcase, prefer the real Beijing catalog instead of `--seed-demo`:

```bash
server-qt/build/charger-server --database runtime/showcase.db --migrate-only
server-qt/build/charger-server --database runtime/showcase.db \
  --import-catalog backend/data/processed/beijing_public_charging_stations.json
server-qt/build/charger-server --database runtime/showcase.db \
  --seed-showcase --migrate-only
```

`--seed-showcase` requires an otherwise empty business database. It keeps all 2,614 real catalog stations, adds only user `13900000000`, and attaches one clearly named course test pile to a real station. No other users, fake stations, orders or device states are generated.

## Database initialization and migration

Create a new target database:

```bash
server-qt/build/charger-server --database /absolute/path/charger.db --migrate-only
```

Import a supported Python database into an empty migrated target. The source is opened read-only and must remain separate from the running Python service:

```bash
server-qt/build/charger-server --database /absolute/path/charger-qt.db \
  --import-legacy /absolute/path/map_for_ecar_charger.db
```

Import or refresh the cleaned public station catalog:

```bash
server-qt/build/charger-server --database /absolute/path/charger-qt.db \
  --import-catalog backend/data/processed/beijing_public_charging_stations.json \
  --catalog-id-source /absolute/path/map_for_ecar_charger.db
```

`--catalog-id-source` opens the legacy database read-only and preserves every existing `(data_source, external_id) → stations.id` mapping while excluding unrelated demo stations. Omit it only for a database that has never assigned station IDs.

Create the first administrator without putting its password in command history:

```bash
read -rsp 'Initial administrator password: ' INITIAL_ADMIN_PASSWORD
export INITIAL_ADMIN_PASSWORD
server-qt/build/charger-server --database /absolute/path/charger-qt.db --create-admin admin
unset INITIAL_ADMIN_PASSWORD
```

The operation preserves an existing administrator and never resets its password.

## Run

```bash
export SERVER_HOST=127.0.0.1
export SERVER_PORT=9000
export DATABASE_PATH=/absolute/path/charger-qt.db
export TENCENT_MAP_KEY='configured-outside-git'
server-qt/build/charger-server
```

For the course development host, keep the secret outside Git in `/etc/map-for-ecar/server.env`, load it with `set -a; . /etc/map-for-ecar/server.env; set +a`, then start the server. The Key must have **WebService API** enabled in the Tencent Location console. Enabling the product alone does not allocate request capacity: in **Quota Management → Account Quota**, allocate daily and concurrency quota to this Key for address geocoding, distance matrix and driving directions. Tencent status `121` means the Key has no remaining daily quota.

Use a reachable `SERVER_HOST` for LAN integration. Plain TCP is for the controlled course network; use TLS before an untrusted-network deployment. Runtime database, avatar files, logs, keys, and build output are excluded from Git.

The wire contract is `contracts/socket-protocol.md`; its machine-readable action schemas are in `contracts/schemas/socket.json`.
