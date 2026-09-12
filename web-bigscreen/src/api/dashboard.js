import axios from 'axios'
import { adaptDashboard } from './qtDashboard.mjs'

export const USE_MOCK = import.meta.env.VITE_USE_MOCK === 'true'
export const USE_ANALYTICS = import.meta.env.VITE_USE_ANALYTICS !== 'false'
const request = axios.create({ baseURL: '/api', timeout: 10000 })

export async function getDashboard(signal) {
  if (USE_MOCK) {
    const mock = await import('../mock/dashboardMock')
    return { ...mock, source: '模拟数据' }
  }
  const { data } = await request.get('/dashboard', { signal })
  return { ...adaptDashboard(data), source: 'Qt 实时数据' }
}

export async function getAnalytics(signal) {
  const { data } = await request.get('/analytics', { signal })
  return data
}
