-- 为充电站目录补充服务类型、区域、位置类型及快慢接口数量。

ALTER TABLE stations ADD COLUMN service_type TEXT;
ALTER TABLE stations ADD COLUMN region_scope TEXT;
ALTER TABLE stations ADD COLUMN location_type TEXT;
ALTER TABLE stations ADD COLUMN fast_connector_count INTEGER NOT NULL DEFAULT 0 CHECK(fast_connector_count>=0);
ALTER TABLE stations ADD COLUMN slow_connector_count INTEGER NOT NULL DEFAULT 0 CHECK(slow_connector_count>=0);
