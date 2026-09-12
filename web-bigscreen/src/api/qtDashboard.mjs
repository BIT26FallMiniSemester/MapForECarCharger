export function adaptDashboard(data) {
  if (!data?.generated_at || !Array.isArray(data.stations)) {
    throw new Error('Qt 大屏数据尚未就绪，请确认已运行合并版后端')
  }
  const total = data.pile_count || 0
  const counts = Object.fromEntries(data.pile_status.map(item => [item.name, item.value]))
  const stations = data.stations
  return {
    overview: {
      total_revenue_cents: data.total_revenue_cents,
      today_revenue_cents: data.today_revenue_cents,
      today_order_count: data.today_orders,
      today_energy_wh: data.trend.at(-1)?.energy_wh || 0,
      station_count: data.station_count,
      pile_count: total,
      available_pile_count: counts.IDLE || 0,
      online_rate: total ? 100 * (total - (counts.OFFLINE || 0)) / total : 0
    },
    revenueTrend: { days: 30, items: data.trend.map(item => ({ ...item, order_count: item.orders })) },
    pileStatus: { total, items: data.pile_status.map(item => ({ status: item.name, count: item.value, percentage: total ? 100 * item.value / total : 0 })) },
    stationRanking: { days: 30, items: data.station_ranking.map(item => ({ ...stations.find(s => s.station_id === item.station_id), ...item })) },
    stationDistribution: { items: stations },
    loadPrediction: {
      model_version: data.model, horizon_hours: 6,
      points: (data.forecast_points || []).slice(0, 6).map(item => ({
        label: item.time, energy_kwh: item.energy_kwh, orders: item.orders,
        congestion_score: item.utilization / 100
      }))
    },
    realtimeOrders: { items: data.realtime_orders }
  }
}
