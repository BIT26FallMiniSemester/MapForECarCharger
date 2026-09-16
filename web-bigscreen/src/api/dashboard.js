import axios from 'axios'
import { adaptDashboard } from './qtDashboard.mjs'

export const USE_MOCK = import.meta.env.VITE_USE_MOCK === 'true'
export const USE_ANALYTICS = import.meta.env.VITE_USE_ANALYTICS !== 'false'
export const USE_SPARK_COMPARISONS = import.meta.env.VITE_USE_SPARK_COMPARISONS === 'true'
const request = axios.create({ baseURL: '/api', timeout: 10000 })

export async function getDashboard(signal) {
  if (USE_MOCK) {
    const mock = await import('../mock/dashboardMock')
    return { ...mock, source: '模拟数据' }
  }
  const { data } = await request.get('/dashboard', { signal })
  return { ...adaptDashboard(data), source: 'Spark ADS' }
}

export async function getAnalytics(signal) {
  const { data } = await request.get('/analytics', { signal })
  return data
}

export async function getComparisons(signal) {
  const { data } = await request.get('/v1/comparisons', { signal })
  return data
}

export async function getTopics(signal) {
  const { data } = await request.get('/v1/topics', { signal })
  return data
}

export async function getPiles(page, status, signal) {
  const { data } = await request.get('/v1/piles', { params: { page, page_size: 120, status }, signal })
  return data
}
