-- 创建充电平台基础表、索引、外键和初始字段。

CREATE TABLE users(id INTEGER PRIMARY KEY,phone TEXT NOT NULL UNIQUE CHECK(length(phone)=11),nickname TEXT NOT NULL,avatar_id TEXT,balance_cents INTEGER NOT NULL DEFAULT 0 CHECK(balance_cents>=0),status TEXT NOT NULL DEFAULT 'NORMAL' CHECK(status IN ('NORMAL','FROZEN')),created_at TEXT NOT NULL,updated_at TEXT NOT NULL);
CREATE TABLE admins(id INTEGER PRIMARY KEY,username TEXT NOT NULL UNIQUE,password_hash TEXT NOT NULL,display_name TEXT NOT NULL,status TEXT NOT NULL DEFAULT 'NORMAL' CHECK(status IN ('NORMAL','DISABLED')),last_login_at TEXT,created_at TEXT NOT NULL,updated_at TEXT NOT NULL);
CREATE TABLE stations(id INTEGER PRIMARY KEY,name TEXT NOT NULL,address TEXT NOT NULL,latitude REAL NOT NULL CHECK(latitude BETWEEN -90 AND 90),longitude REAL NOT NULL CHECK(longitude BETWEEN -180 AND 180),price_cents_per_kwh INTEGER CHECK(price_cents_per_kwh>0),operator_name TEXT,district TEXT,data_source TEXT,external_id TEXT,status TEXT NOT NULL DEFAULT 'ACTIVE' CHECK(status IN ('ACTIVE','INACTIVE')),created_at TEXT NOT NULL,updated_at TEXT NOT NULL,UNIQUE(data_source,external_id));
CREATE TABLE charging_piles(id INTEGER PRIMARY KEY,station_id INTEGER NOT NULL REFERENCES stations(id),pile_no TEXT NOT NULL UNIQUE,charge_type TEXT NOT NULL CHECK(charge_type IN ('FAST','SLOW')),rated_power_w INTEGER NOT NULL CHECK(rated_power_w>0),status TEXT NOT NULL DEFAULT 'IDLE' CHECK(status IN ('IDLE','RESERVED','CHARGING','FAULT','OFFLINE')),reserved_order_id INTEGER REFERENCES charging_orders(id) DEFERRABLE INITIALLY DEFERRED,created_at TEXT NOT NULL,updated_at TEXT NOT NULL);
CREATE TABLE charging_orders(id INTEGER PRIMARY KEY,order_no TEXT NOT NULL UNIQUE,user_id INTEGER NOT NULL REFERENCES users(id),station_id INTEGER NOT NULL REFERENCES stations(id),pile_id INTEGER NOT NULL REFERENCES charging_piles(id),status TEXT NOT NULL CHECK(status IN ('PENDING','RESERVED','CHARGING','UNPAID','COMPLETED','CANCELLED')),price_cents_per_kwh INTEGER NOT NULL CHECK(price_cents_per_kwh>0),reserved_at TEXT,expires_at TEXT,started_at TEXT,stopped_at TEXT,duration_seconds INTEGER CHECK(duration_seconds>=0),energy_wh INTEGER CHECK(energy_wh>=0),amount_cents INTEGER CHECK(amount_cents>=0),paid_at TEXT,cancelled_at TEXT,created_at TEXT NOT NULL,updated_at TEXT NOT NULL);
CREATE TABLE recharge_records(id INTEGER PRIMARY KEY,user_id INTEGER NOT NULL REFERENCES users(id),client_request_id TEXT NOT NULL UNIQUE,amount_cents INTEGER NOT NULL CHECK(amount_cents>0),balance_after_cents INTEGER NOT NULL CHECK(balance_after_cents>=0),created_at TEXT NOT NULL);
CREATE TABLE pile_status_logs(id INTEGER PRIMARY KEY,pile_id INTEGER NOT NULL REFERENCES charging_piles(id),order_id INTEGER REFERENCES charging_orders(id),old_status TEXT NOT NULL CHECK(old_status IN ('IDLE','RESERVED','CHARGING','FAULT','OFFLINE')),new_status TEXT NOT NULL CHECK(new_status IN ('IDLE','RESERVED','CHARGING','FAULT','OFFLINE')),reason TEXT NOT NULL,created_at TEXT NOT NULL);
CREATE TABLE operation_logs(id INTEGER PRIMARY KEY,admin_id INTEGER NOT NULL REFERENCES admins(id),action TEXT NOT NULL,target_type TEXT NOT NULL,target_id INTEGER NOT NULL,detail_json TEXT,created_at TEXT NOT NULL);
CREATE UNIQUE INDEX uq_user_active ON charging_orders(user_id) WHERE status IN ('PENDING','RESERVED','CHARGING','UNPAID');
CREATE UNIQUE INDEX uq_pile_active ON charging_orders(pile_id) WHERE status IN ('RESERVED','CHARGING');
CREATE INDEX idx_orders_user_created ON charging_orders(user_id,created_at);
CREATE INDEX idx_orders_paid ON charging_orders(status,paid_at);
CREATE INDEX idx_orders_expires ON charging_orders(status,expires_at);
CREATE INDEX idx_piles_station_status ON charging_piles(station_id,status);
CREATE INDEX idx_users_status ON users(status);
CREATE INDEX idx_stations_status ON stations(status);
CREATE INDEX idx_recharges_user ON recharge_records(user_id,created_at);
CREATE INDEX idx_pile_logs ON pile_status_logs(pile_id,created_at);
CREATE INDEX idx_operation_logs ON operation_logs(admin_id,created_at);
