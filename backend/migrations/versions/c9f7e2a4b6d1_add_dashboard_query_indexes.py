"""add dashboard query indexes

Revision ID: c9f7e2a4b6d1
Revises: d7bda1991e30
Create Date: 2026-09-02 12:00:00.000000

"""

from collections.abc import Sequence

from alembic import op

# revision identifiers, used by Alembic.
revision: str = "c9f7e2a4b6d1"
down_revision: str | Sequence[str] | None = "d7bda1991e30"
branch_labels: str | Sequence[str] | None = None
depends_on: str | Sequence[str] | None = None


def upgrade() -> None:
    """Create indexes used by dashboard time-window queries."""
    op.create_index(
        "idx_orders_status_started",
        "charging_orders",
        ["status", "started_at"],
        unique=False,
    )
    op.create_index(
        "idx_pile_logs_created",
        "pile_status_logs",
        ["created_at"],
        unique=False,
    )


def downgrade() -> None:
    """Remove dashboard query indexes."""
    op.drop_index("idx_pile_logs_created", table_name="pile_status_logs")
    op.drop_index("idx_orders_status_started", table_name="charging_orders")
