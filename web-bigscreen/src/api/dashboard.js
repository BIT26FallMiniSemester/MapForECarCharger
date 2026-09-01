import axios from 'axios'
import {
  overview,
  revenueTrend,
  pileStatus,
  stationRanking,
  stationDistribution,
  loadPrediction,
  realtimeOrders
} from '../mock/dashboardMock'

export const USE_MOCK = true

const request = axios.create({
  baseURL: 'http://localhost:8000/api/v1',
  timeout: 5000,
  headers: {
    'Content-Type': 'application/json; charset=utf-8'
  }
})

function mock(data) {
  return Promise.resolve({
    code: 0,
    message: 'success',
    data,
    request_id: 'mock-dashboard-request'
  })
}

async function unwrap(promise) {
  const response = await promise
  const payload = response.data
  if (payload.code !== 0) {
    throw new Error(payload.message || 'request failed')
  }
  return payload
}

export async function getOverview() {
  if (USE_MOCK) return mock(overview)
  return unwrap(request.get('/dashboard/overview'))
}

export async function getRevenueTrend(days = 30) {
  if (USE_MOCK) return mock({ ...revenueTrend, days, items: revenueTrend.items.slice(-days) })
  return unwrap(request.get('/dashboard/revenue-trend', { params: { days } }))
}

export async function getPileStatus() {
  if (USE_MOCK) return mock(pileStatus)
  return unwrap(request.get('/dashboard/pile-status'))
}

export async function getStationRanking(metric = 'revenue', days = 30, limit = 10) {
  if (USE_MOCK) return mock({ ...stationRanking, metric, days, items: stationRanking.items.slice(0, limit) })
  return unwrap(request.get('/dashboard/station-ranking', { params: { metric, days, limit } }))
}

export async function getStationDistribution(limit = 120) {
  if (USE_MOCK) return mock({ items: stationDistribution.items.slice(0, limit) })
  return unwrap(request.get('/stations', { params: { page: 1, page_size: limit } }))
}

export async function getLoadPrediction(horizonHours = 6, stationId = null) {
  if (USE_MOCK) return mock({ ...loadPrediction, horizon_hours: horizonHours })
  const params = { horizon_hours: horizonHours }
  if (stationId !== null && stationId !== undefined) params.station_id = stationId
  return unwrap(request.get('/predictions/load', { params }))
}

export async function getRealtimeOrders() {
  if (USE_MOCK) return mock(realtimeOrders)
  return unwrap(request.get('/dashboard/realtime-orders'))
}



