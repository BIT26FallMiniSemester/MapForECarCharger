<template>
  <div class="dashboard-shell">
    <header class="topbar">
      <div>
        <p>EV Charging Operation Center</p>
        <h1>电动汽车充电运营数据大屏</h1>
      </div>
      <div class="topbar-right">
        <span>接口基线 /api/v1 · 准实时刷新 5s</span>
        <strong>{{ currentTime }}</strong>
      </div>
    </header>

    <section class="metrics-grid">
      <MetricCard title="总营收" :value="formatNumber(centsToYuan(overview.total_revenue_cents), 1)" unit="元" icon="revenue" />
      <MetricCard title="今日营收" :value="formatNumber(centsToYuan(overview.today_revenue_cents), 1)" unit="元" icon="today" />
      <MetricCard title="今日订单" :value="overview.today_order_count || 0" unit="单" icon="order" />
      <MetricCard title="今日充电量" :value="formatNumber(whToKwh(overview.today_energy_wh), 1)" unit="kWh" icon="energy" />
      <MetricCard title="空闲电桩" :value="availablePileText" unit="" icon="pile" />
    </section>

    <main class="command-grid">
      <PanelCard class="area-revenue" title="营收与订单趋势" :subtitle="`近 ${revenueTrend.days || 30} 日`">
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

      <PanelCard class="area-orders" title="实时充电订单" subtitle="P1 接口：/dashboard/realtime-orders">
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
import { getLoadPrediction, getOverview, getPileStatus, getRealtimeOrders, getRevenueTrend, getStationDistribution, getStationRanking } from '../api/dashboard'
import { centsToYuan, formatNumber, whToKwh } from '../utils/format'

const currentTime = ref('')
const overview = ref({})
const revenueTrend = ref({ items: [] })
const pileStatus = ref({ items: [] })
const stationRanking = ref({ items: [] })
const stationDistribution = ref({ items: [] })
const loadPrediction = ref({ points: [] })
const realtimeOrders = ref({ items: [] })
let timer
let clock

const availablePileText = computed(() => `${overview.value.available_pile_count || 0}/${overview.value.pile_count || 0}`)
const topStations = computed(() => (stationRanking.value.items || []).slice(0, 3))
const warnings = computed(() => {
  const statusItems = pileStatus.value.items || []
  const offline = statusItems.find(item => item.status === 'OFFLINE')
  const fault = statusItems.find(item => item.status === 'FAULT')
  const peakPoint = (loadPrediction.value.points || []).find(item => Number(item.congestion_score) >= 0.8)
  const result = []

  if (peakPoint) {
    result.push({ level: '高', title: '高峰负荷预警', detail: `${new Date(peakPoint.predicted_for).toLocaleTimeString('zh-CN', { hour: '2-digit', minute: '2-digit' })} 预测拥堵指数 ${formatNumber(peakPoint.congestion_score * 100, 0)}%` })
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

async function loadDashboard() {
  const [overviewRes, trendRes, statusRes, rankingRes, distributionRes, predictionRes, ordersRes] = await Promise.all([
    getOverview(),
    getRevenueTrend(30),
    getPileStatus(),
    getStationRanking('revenue', 30, 10),
    getStationDistribution(120),
    getLoadPrediction(6),
    getRealtimeOrders()
  ])
  overview.value = overviewRes.data
  revenueTrend.value = trendRes.data
  pileStatus.value = statusRes.data
  stationRanking.value = rankingRes.data
  stationDistribution.value = distributionRes.data
  loadPrediction.value = predictionRes.data
  realtimeOrders.value = ordersRes.data
}

onMounted(() => {
  updateClock()
  loadDashboard()
  clock = setInterval(updateClock, 1000)
  timer = setInterval(loadDashboard, 5000)
})

onBeforeUnmount(() => {
  clearInterval(clock)
  clearInterval(timer)
})
</script>





