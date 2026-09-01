import seed from '../../data/stations_database.json'

const DEFAULT_PRICE_CENTS_PER_KWH = 125
const DAY_LABELS = ['08-26', '08-27', '08-28', '08-29', '08-30', '08-31', '09-01']
const ISO_DAYS = ['2026-08-26', '2026-08-27', '2026-08-28', '2026-08-29', '2026-08-30', '2026-08-31', '2026-09-01']

const stations = seed.stations || []
const chargingPiles = seed.charging_piles || []
const stationById = new Map(stations.map(station => [station.id, station]))

function priceOf(station) {
  return station?.price_cents_per_kwh || DEFAULT_PRICE_CENTS_PER_KWH
}

function groupPilesByStation() {
  const grouped = new Map()
  chargingPiles.forEach(pile => {
    if (!grouped.has(pile.station_id)) grouped.set(pile.station_id, [])
    grouped.get(pile.station_id).push(pile)
  })
  return grouped
}

const pilesByStation = groupPilesByStation()

function buildStationStats() {
  return stations.map(station => {
    const piles = pilesByStation.get(station.id) || []
    const total = piles.length
    const idle = piles.filter(pile => pile.status === 'IDLE').length
    const charging = piles.filter(pile => pile.status === 'CHARGING').length
    const active = piles.filter(pile => ['RESERVED', 'CHARGING'].includes(pile.status)).length
    const energyWh = active * 42000 + charging * 18000 + total * 1500
    const orderCount = active * 8 + Math.floor(total / 3)
    return {
      station_id: station.id,
      station_name: station.name,
      latitude: station.latitude,
      longitude: station.longitude,
      revenue_cents: Math.round((energyWh / 1000) * priceOf(station)),
      order_count: orderCount,
      energy_wh: energyWh,
      utilization_rate: total ? Math.round(((total - idle) / total) * 10000) / 100 : 0,
      pile_count: total,
      available_pile_count: idle,
      district: station.district,
      operator_name: station.operator_name
    }
  })
}

const stationStats = buildStationStats()
const topStationStats = [...stationStats]
  .filter(item => item.latitude !== null && item.longitude !== null)
  .sort((a, b) => b.revenue_cents - a.revenue_cents)

function countByStatus() {
  const counts = { IDLE: 0, RESERVED: 0, CHARGING: 0, FAULT: 0, OFFLINE: 0 }
  chargingPiles.forEach(pile => {
    if (counts[pile.status] !== undefined) counts[pile.status] += 1
  })
  return counts
}

const statusCounts = countByStatus()
const totalPileCount = chargingPiles.length
const onlinePileCount = statusCounts.IDLE + statusCounts.RESERVED + statusCounts.CHARGING + statusCounts.FAULT
const totalEnergyWh = stationStats.reduce((sum, item) => sum + item.energy_wh, 0)
const totalRevenueCents = stationStats.reduce((sum, item) => sum + item.revenue_cents, 0)
const totalOrderCount = stationStats.reduce((sum, item) => sum + item.order_count, 0)

export const overview = {
  today_revenue_cents: Math.round(totalRevenueCents * 0.08),
  total_revenue_cents: totalRevenueCents,
  today_order_count: Math.round(totalOrderCount * 0.08),
  total_order_count: totalOrderCount,
  today_energy_wh: Math.round(totalEnergyWh * 0.08),
  charging_order_count: statusCounts.CHARGING,
  station_count: stations.length,
  pile_count: totalPileCount,
  available_pile_count: statusCounts.IDLE,
  online_rate: totalPileCount ? Math.round((onlinePileCount / totalPileCount) * 10000) / 100 : 0,
  updated_at: seed.metadata?.generated_at || new Date().toISOString()
}

export const revenueTrend = {
  days: 30,
  items: ISO_DAYS.map((date, index) => {
    const factor = [0.72, 0.81, 0.77, 0.9, 0.86, 1, 0.84][index]
    return {
      date,
      revenue_cents: Math.round(overview.today_revenue_cents * factor),
      order_count: Math.round(overview.today_order_count * factor)
    }
  })
}

export const pileStatus = {
  total: totalPileCount,
  items: Object.entries(statusCounts).map(([status, count]) => ({
    status,
    count,
    percentage: totalPileCount ? Math.round((count / totalPileCount) * 10000) / 100 : 0
  }))
}

export const stationRanking = {
  metric: 'revenue',
  days: 30,
  items: topStationStats.slice(0, 10)
}

export const stationDistribution = {
  items: topStationStats.slice(0, 120)
}

export const loadPrediction = {
  station_id: null,
  horizon_hours: 6,
  model_version: 'seed-baseline-v1',
  generated_at: overview.updated_at,
  points: DAY_LABELS.slice(1).map((label, index) => {
    const hour = 7 + index
    const congestion = [0.44, 0.52, 0.61, 0.7, 0.83, 0.78][index]
    return {
      predicted_for: `2026-09-01T${String(hour).padStart(2, '0')}:00:00Z`,
      load_w: Math.round(totalPileCount * 60000 * congestion * 0.18),
      available_piles: Math.max(0, Math.round(statusCounts.IDLE * (1 - congestion * 0.65))),
      congestion_score: congestion
    }
  })
}

export const realtimeOrders = {
  items: chargingPiles
    .filter(pile => ['CHARGING', 'RESERVED'].includes(pile.status))
    .slice(0, 8)
    .map((pile, index) => {
      const station = stationById.get(pile.station_id)
      const powerW = pile.status === 'CHARGING' ? Math.round(pile.rated_power_w * (0.62 + (index % 4) * 0.08)) : 0
      return {
        id: 1000 + index + 1,
        order_no: `CO20260901${String(index + 1).padStart(6, '0')}`,
        station_id: pile.station_id,
        station_name: station?.name || '未知充电站',
        pile_id: pile.id,
        pile_no: pile.pile_no,
        status: pile.status,
        power_w: powerW,
        amount_cents: pile.status === 'CHARGING' ? Math.round((powerW / 1000) * priceOf(station) * 0.4) : 0
      }
    })
}
