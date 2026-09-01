# Backend source data

## Beijing public charging stations

- File: `raw/beijing_public_charging_stations.csv`
- Original title: 充电站基本信息（社会公用）
- Source: 北京市公共数据开放平台（user-provided export）
- Imported rows: 2,614
- SHA-256: `a80145b9314f4ee4da6132d7a179352b0307e1dd7a171a45a0ac14d1eb04fbeb`
- Encoding: UTF-8 with BOM
- Snapshot received: 2026-09-01

The source contains aggregate fast/slow connector counts, but no coordinates,
price or device identifiers. Importing it creates discoverable station catalog
records only. It does not create controllable charging piles and does not make
those stations bookable.

Re-import after a migration with:

```bash
cd backend
.venv/bin/python -m app.station_import
```

The import is idempotent by `(data_source, external_id)`. Source attributes and
connector counts are refreshed; coordinates, price and managed piles enriched
inside the platform are preserved.
