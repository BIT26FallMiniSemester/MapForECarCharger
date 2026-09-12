<template>
  <div class="dashboard-shell">
    <header class="topbar">
      <div>
        <p>EV Charging Operation Center</p>
        <h1>电动汽车充电运营数据大屏</h1>
      </div>
      <div class="topbar-right">
        <span>{{ dataSource }} · 每 5 秒刷新</span>
        <strong>{{ currentTime }}</strong>
      </div>
    </header>

    <p v-if="error" role="alert" style="color: #fca5a5">{{ error }}（保留上次成功数据）</p>
    <section class="analytics-status" aria-live="polite">
      <strong>{{ analytics.label }}</strong>
      <span v-if="analytics.batch">快照：{{ new Date(analytics.batch.snapshot_at).toLocaleString('zh-CN') }} · {{ analytics.batch.input_orders }} 笔订单 · {{ analytics.batch.window_start }} 至 {{ analytics.batch.window_end }}</span>
      <span v-if="analytics.warning" class="analytics-warning">{{ analytics.warning }}</span>
    </section>
    <section class="metrics-grid">
      <MetricCard :title="analytics.batch ? '累计营收（批次）' : '累计营收'" :value="formatNumber(centsToYuan(overview.total_revenue_cents), 1)" unit="元" icon="revenue" />
      <MetricCard title="今日营收" :value="formatNumber(centsToYuan(overview.today_revenue_cents), 1)" unit="元" icon="today" />
      <MetricCard title="今日订单" :value="overview.today_order_count || 0" unit="单" icon="order" />
      <MetricCard title="今日充电量" :value="formatNumber(whToKwh(overview.today_energy_wh), 1)" unit="kWh" icon="energy" />
      <MetricCard title="空闲电桩" :value="availablePileText" unit="" icon="pile" />
    </section>

    <main class="command-grid">
      <PanelCard class="area-revenue" title="营收与订单趋势" :subtitle="`${analytics.batch ? '批处理' : 'Qt'} · 近 ${revenueTrend.days || 30} 日`">
        <RevenueTrend :data="revenueTrend" />
      </PanelCard>

      <PanelCard class="area-map command-center" title="充电桩分布态势图" subtitle="站点网络 · 调度中心 · 运行热度">
        <DistributionMap :stations="stationDistribution.items || []" :overview="overview" />
      </PanelCard>

      <PanelCard class="area-ranking" title="站点营收排行" :subtitle="`Top ${(stationRanking.items || []).length}`">
        <StationRanking :data="stationRanking" />
      </PanelCard>

      <PanelCard class="area-status" title="电桩状态分布" :subtitle="`总计 ${pileStatus.total || 0} 台`">
        <PileStatusChart :data="pileStatus" />
      </PanelCard>

      <PanelCard class="area-overview" title="平台运行总览" subtitle="核心设备与业务健康度">
        <div class="overview-panel compact-overview">
          <div class="health-ring">
            <strong>{{ formatNumber(overview.online_rate, 1) }}%</strong>
            <span>设备在线率</span>
          </div>
          <div class="health-items">
            <div v-for="item in topStations" :key="item.station_id">
              <span>{{ item.station_name }}</span>
              <strong>{{ formatNumber(item.utilization_rate, 1) }}%</strong>
              <div><i :style="{ width: item.utilization_rate + '%' }"></i></div>
            </div>
          </div>
        </div>
      </PanelCard>

      <PanelCard class="area-prediction" title="负荷预测趋势" :subtitle="`${loadPrediction.model_version || 'baseline-v1'} · ${loadPrediction.horizon_hours || 6}小时`">
        <LoadPrediction :data="loadPrediction" />
      </PanelCard>

      <PanelCard class="area-orders" title="实时充电订单" subtitle="最近 8 笔订单 · 额定功率">
        <RealtimeOrders :orders="realtimeOrders.items || []" />
      </PanelCard>

      <PanelCard class="area-warning" title="高峰预警与异常设备" subtitle="由统一接口数据推导">
        <WarningList :warnings="warnings" />
      </PanelCard>
    </main>
  </div>
</template>

<script setup>
import { computed, onBeforeUnmount, onMounted, ref } from 'vue'
import MetricCard from '../components/MetricCard.vue'
import PanelCard from '../components/PanelCard.vue'
import RevenueTrend from '../components/RevenueTrend.vue'
import PileStatusChart from '../components/PileStatusChart.vue'
import StationRanking from '../components/StationRanking.vue'
import LoadPrediction from '../components/LoadPrediction.vue'
import DistributionMap from '../components/DistributionMap.vue'
import RealtimeOrders from '../components/RealtimeOrders.vue'
import WarningList from '../components/WarningList.vue'
import { useDashboard } from '../composables/useDashboard'
import { centsToYuan, formatNumber, whToKwh } from '../utils/format'

const currentTime = ref('')
const { error, dataSource, overview, revenueTrend, pileStatus, stationRanking,
  stationDistribution, loadPrediction, realtimeOrders, analytics } = useDashboard()
let clock

const availablePileText = computed(() => `${overview.value.available_pile_count || 0}/${overview.value.pile_count || 0}`)
const topStations = computed(() => (stationRanking.value.items || []).slice(0, 3))
const warnings = computed(() => {
  const statusItems = pileStatus.value.items || []
  const offline = statusItems.find(item => item.status === 'OFFLINE')
  const fault = statusItems.find(item => item.status === 'FAULT')
  const peakPoint = (loadPrediction.value.points || []).find(item => Number(item.congestion_score) >= 0.8)
  const result = []
  if (error.value || dataSource.value === '正在连接') return [{ level: '中', title: '数据不可用', detail: error.value || '正在等待首次数据' }]

  if (peakPoint) {
    result.push({ level: '高', title: '高峰负荷预警', detail: `${peakPoint.label || new Date(peakPoint.predicted_for).toLocaleTimeString('zh-CN', { hour: '2-digit', minute: '2-digit' })} 预测拥堵指数 ${formatNumber(peakPoint.congestion_score * 100, 0)}%` })
  }
  if (offline && offline.count > 0) {
    result.push({ level: '中', title: '离线设备提醒', detail: `当前 OFFLINE 电桩 ${offline.count} 台，占比 ${formatNumber(offline.percentage, 1)}%` })
  }
  if (fault && fault.count > 0) {
    result.push({ level: '中', title: '故障设备提醒', detail: `当前 FAULT 电桩 ${fault.count} 台，需要运维关注` })
  }
  return result.length ? result : [{ level: '低', title: '运行状态平稳', detail: '当前未发现高峰拥堵或异常设备集中风险' }]
})

function updateClock() {
  currentTime.value = new Date().toLocaleString('zh-CN', { hour12: false })
}

onMounted(() => {
  updateClock()
  clock = setInterval(updateClock, 1000)
})

onBeforeUnmount(() => {
  clearInterval(clock)
})
</script>





