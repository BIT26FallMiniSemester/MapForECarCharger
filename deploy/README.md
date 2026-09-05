# Ubuntu deployment

Build and test first, then install into the service directory:

```bash
cmake -S server-qt -B server-qt/build -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release
cmake --build server-qt/build -j
ctest --test-dir server-qt/build --output-on-failure
cmake --install server-qt/build --prefix /opt/map-for-ecar
```

Create the dedicated account and writable runtime directory according to the machine's administration policy. Copy `server-qt/server.env.example` to `/etc/map-for-ecar/server.env`, set an absolute `DATABASE_PATH`, reachable listen address, and `TENCENT_MAP_KEY`, then restrict that file to the service account. Do not place the initial administrator password there.

Initialize or import the database before starting the service. Install `deploy/charger-server.service` under systemd, run `systemctl daemon-reload`, enable and start the service, then use a framed `system.health` Socket request for the health check. The service file allows writes only below `/var/lib/map-for-ecar`.

Use plain TCP only on the controlled course network. A public or untrusted-network deployment requires TLS Socket configuration and a separate review.
