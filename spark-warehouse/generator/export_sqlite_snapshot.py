"""Export a consistent, read-only business SQLite snapshot for Spark ODS."""
from __future__ import annotations

import argparse
from contextlib import closing
import csv
from datetime import UTC, datetime
import hashlib
import json
from pathlib import Path
import sqlite3
import sys
import tempfile
import uuid

WAREHOUSE_ROOT = Path(__file__).resolve().parents[1]
if str(WAREHOUSE_ROOT) not in sys.path:
    sys.path.insert(0, str(WAREHOUSE_ROOT))

from schemas.table_schemas import TABLE_COLUMNS


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _write_table(connection: sqlite3.Connection, target: Path, table: str) -> int:
    columns = TABLE_COLUMNS[table]
    database_columns = columns[1:]
    target.parent.mkdir(parents=True, exist_ok=True)
    count = 0
    with target.open("x", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=columns, lineterminator="\n")
        writer.writeheader()
        query = f"SELECT {','.join(database_columns)} FROM {table} ORDER BY id"
        for row in connection.execute(query):
            item = {column: "" if row[column] is None else row[column] for column in database_columns}
            item["row_id"] = f"{table}:{row['id']}"
            writer.writerow(item)
            count += 1
    return count


def export_snapshot(database: Path, output: Path) -> Path:
    database = database.resolve()
    if not database.is_file():
        raise FileNotFoundError(f"database does not exist: {database}")
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    batch_id = datetime.now(UTC).strftime("sqlite-%Y%m%dT%H%M%SZ-") + uuid.uuid4().hex[:12]
    destination = output / batch_id
    uri = database.as_uri() + "?mode=ro"
    with tempfile.TemporaryDirectory(prefix=".sqlite-export-", dir=output) as temporary:
        staging = Path(temporary) / batch_id
        dirty = staging / "dirty"
        with closing(sqlite3.connect(uri, uri=True, timeout=10)) as connection:
            connection.row_factory = sqlite3.Row
            connection.execute("PRAGMA query_only=ON")
            connection.execute("BEGIN")
            # Pin one WAL-consistent snapshot before reading any table.
            connection.execute("SELECT count(*) FROM charging_orders").fetchone()
            counts = {
                table: _write_table(connection, dirty / f"{table}.csv", table)
                for table in TABLE_COLUMNS
            }
            connection.rollback()
        files = {
            f"dirty/{table}.csv": {
                "rows": counts[table],
                "sha256": _sha256(dirty / f"{table}.csv"),
            }
            for table in TABLE_COLUMNS
        }
        metadata = {
            "schema_version": 1,
            "batch_id": batch_id,
            "source": "sqlite-readonly-snapshot",
            "source_database": database.name,
            "exported_at": datetime.now(UTC).isoformat(),
            "files": files,
        }
        (staging / "metadata.json").write_text(
            json.dumps(metadata, ensure_ascii=False, indent=2), encoding="utf-8"
        )
        staging.rename(destination)
    return destination


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--database", required=True, type=Path)
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parents[1] / "runtime/sqlite-snapshots")
    args = parser.parse_args()
    try:
        print(export_snapshot(args.database, args.output))
    except (OSError, sqlite3.Error, ValueError) as error:
        parser.exit(1, f"Export failed: {error}\n")


if __name__ == "__main__":
    main()
