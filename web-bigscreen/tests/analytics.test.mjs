import test from 'node:test'
import assert from 'node:assert/strict'
import { mergeAnalytics } from '../src/api/analytics.mjs'

const live = { overview: { total_revenue_cents: 200, today_order_count: 8 },
  stationDistribution: { items: [{ station_id: 1, utilization_rate: 20 }] },
  revenueTrend: { items: [] }, stationRanking: { items: [] }, realtimeOrders: { items: [{ id: 1 }] } }
const batch = { schema_version: 1, engine: 'hadoop-mapreduce', snapshot_at: '2026-09-12T00:00:00Z',
  total_revenue_cents: 100, revenue_trend: { items: Array.from({length:30}, () => ({})) },
  station_ranking: { items: [{ station_id: 1, revenue_cents: 100 }] } }
test('batch replaces historical totals while preserving realtime orders and current status', () => {
  const result = mergeAnalytics(live, { available: true, stale: false, data: batch })
  assert.equal(result.overview.total_revenue_cents, 100)
  assert.equal(result.overview.today_order_count, 8)
  assert.equal(result.realtimeOrders, live.realtimeOrders)
  assert.equal(result.stationRanking.items[0].utilization_rate, 20)
  assert.equal(live.overview.total_revenue_cents, 200)
})
test('missing batch explicitly falls back to Qt', () => {
  const result = mergeAnalytics(live, null, '连接失败')
  assert.equal(result.revenueTrend, live.revenueTrend)
  assert.equal(result.analytics.warning, '连接失败')
  assert.equal(result.analytics.batch, null)
})
test('stale batch remains visible with warning', () => {
  assert.match(mergeAnalytics(live, {available:true,stale:true,data:batch}).analytics.warning, /过期/)
})
test('invalid result does not overwrite live data', () => {
  assert.equal(mergeAnalytics(live, {available:true,data:{...batch,schema_version:2}}).overview, live.overview)
})
test('local validation results are not labelled Hadoop', () => {
  assert.match(mergeAnalytics(live, {available:true,data:{...batch,engine:'local-mapreduce'}}).analytics.label, /本地验证/)
})
