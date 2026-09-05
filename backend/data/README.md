# Backend source data

## Beijing public charging stations

Original snapshot:

- File: `raw/beijing_public_charging_stations.csv`
- Original title: 充电站基本信息（社会公用）
- Source: 北京市公共数据开放平台（user-provided export）
- Rows: 2,614
- SHA-256: `a80145b9314f4ee4da6132d7a179352b0307e1dd7a171a45a0ac14d1eb04fbeb`
- Snapshot received: 2026-09-01

Cleaned import dataset:

- File: `processed/beijing_public_charging_stations.json`
- Rows with coordinates: 2,614 / 2,614
- Coordinate enrichment: Tencent geocoding
- SHA-256: `bb42a7eb54f773876d87e0fae68f1553bd28c8f02f1009fdd30b499a892ca933`
- Received: 2026-09-03

The cleaned file keeps the public catalog identity and source fields while
adding latitude and longitude. Local surrogate IDs, generated timestamps and
32,247 simulated pile records from the supplied working file are deliberately
excluded. Importing this dataset never creates controllable charging piles.
Aggregate fast/slow connector counts remain station metadata, and a station is
bookable only after a price and managed pile are configured separately.

Re-import after a migration with:

```bash
cd backend
.venv/bin/python -m app.station_import
```

The import is idempotent by `(data_source, external_id)`. Source attributes and
cleaned coordinates are refreshed. A non-null source price may be applied, but
an absent source price never erases a price configured inside the platform.
