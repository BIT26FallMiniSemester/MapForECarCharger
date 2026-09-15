import { computed, onBeforeUnmount, onMounted, ref } from 'vue'
import { getDashboard, getAnalytics, getComparisons, USE_MOCK, USE_ANALYTICS, USE_SPARK_COMPARISONS } from '../api/dashboard'
import { mergeAnalytics } from '../api/analytics.mjs'

export function useDashboard() {
  const live = ref({ overview: {}, revenueTrend: { items: [] }, pileStatus: { items: [] },
    stationRanking: { items: [] }, stationDistribution: { items: [] },
    loadPrediction: { points: [] }, realtimeOrders: { items: [] } })
  const batch = ref(null)
  const comparisons = ref(null)
  const comparisonError = ref('正在读取 Spark 双维对比')
  const batchError = ref('正在读取批处理结果')
  const error = ref('')
  const dataSource = ref('正在连接')
  let timer, controller, stopped = false
  const merged = computed(() => USE_MOCK ? { ...live.value,
    analytics: { label: '模拟数据', warning: '', batch: null } } : !USE_ANALYTICS ? {
      ...live.value, analytics: { label: '历史统计：Qt 实时汇总', warning: '', batch: null }
    } : mergeAnalytics(live.value, batch.value, batchError.value))
  async function refresh() {
    controller = new AbortController()
    await Promise.all([
      getDashboard(controller.signal).then(data => {
        if (stopped) return
        live.value = data
        dataSource.value = data.source
        error.value = ''
      }).catch(err => { if (!stopped) error.value = `实时数据更新失败：${err.message}` }),
      (USE_MOCK || !USE_ANALYTICS ? Promise.resolve() : getAnalytics(controller.signal).then(data => {
        if (stopped) return
        batch.value = data
        batchError.value = ''
      }).catch(() => {
        if (stopped) return
        batch.value = null
        batchError.value = '批处理结果不可用，历史图表暂用 Qt 汇总'
      })),
      (USE_MOCK || !USE_SPARK_COMPARISONS ? Promise.resolve() : getComparisons(controller.signal).then(data => {
        if (stopped) return
        comparisons.value = data
        comparisonError.value = ''
      }).catch(() => {
        if (!stopped) comparisonError.value = 'Spark 双维对比暂不可用'
      }))
    ])
    if (!stopped) timer = setTimeout(refresh, 5000)
  }
  onMounted(refresh)
  onBeforeUnmount(() => { stopped = true; clearTimeout(timer); controller?.abort() })
  return { error, dataSource, comparisons, comparisonError,
    ...Object.fromEntries(['overview', 'revenueTrend', 'pileStatus', 'stationRanking',
      'stationDistribution', 'loadPrediction', 'realtimeOrders', 'analytics'].map(key => [key, computed(() => merged.value[key])])) }
}
