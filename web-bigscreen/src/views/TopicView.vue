<template>
  <main class="topic-page" :class="`topic-${page}`">
    <div class="topic-meta">
      <p v-if="topicError" role="alert" class="topic-notice">{{ topicError }}</p>
      <p class="topic-source">{{ topics?.source || '等待 Spark 数据' }} · 清洗批次 {{ topics?.batch_id || '—' }} · 数据日期 {{ topics?.data_as_of || '—' }}</p>
    </div>

  <template v-if="page === 'stations'">
    <div class="metrics-grid"><MetricCard title="站点总量" :value="stations.length" unit="站"/><MetricCard title="平均站点桩数" :value="number(stations.length ? overview.pile_count / stations.length : 0)" unit="台"/><MetricCard title="繁忙站点" :value="stations.filter(s => s.utilization_rate >= 50).length" unit="站"/><MetricCard title="空闲电桩" :value="overview.available_pile_count || 0" unit="台"/><MetricCard title="覆盖区域" :value="districts.length" unit="个"/></div>
    <div class="topic-grid spatial-layout">
      <PanelCard title="区域资源分布" subtitle="Spark ADS · 各行政区电桩数量"><TopicChart :option="districtChart" label="区域电桩数量排名"/></PanelCard>
      <PanelCard title="站点空间态势" :subtitle="`Spark ADS · ${mapMode} · 站点以编号展示`" class="topic-map"><div class="mode-tabs"><button v-for="mode in ['繁忙率','空闲率','故障率']" :key="mode" :class="{ active: mapMode === mode }" @click="mapMode = mode">{{ mode }}</button></div><DistributionMap :stations="mapStations" :overview="overview" :metric-label="mapMode"/></PanelCard>
      <PanelCard title="资源紧张站点" subtitle="Spark ADS · 繁忙率 Top 10"><div class="rank-list"><div v-for="s in busiest" :key="s.station_id"><span>站点 #{{ s.station_id }} · {{ s.district }}</span><strong>{{ s.utilization_rate }}%</strong><small>空闲 {{ s.available_pile_count }}/{{ s.pile_count }}</small></div></div></PanelCard>
    </div>
    <PanelCard v-if="comparisons" class="topic-wide comparison-panel" title="清洗后空间运营对比" subtitle="Spark ADS · 近30日 · 区域×桩型 / 日类型×小时"><DimensionComparison :data="comparisons"/></PanelCard>
  </template>

  <template v-else-if="page === 'piles'">
    <div class="metrics-grid"><MetricCard v-for="item in pileStatus.items || []" :key="item.status" :title="statusLabel(item.status)" :value="item.count" unit="台"/></div>
    <div class="topic-grid device-layout"><PanelCard title="电桩状态矩阵" subtitle="Spark DWD 最新快照 · 支持筛选和翻页"><PileMatrix/></PanelCard><PanelCard title="设备状态占比" subtitle="Spark ADS 状态汇总"><PileStatusChart :data="pileStatus"/></PanelCard></div>
    <PanelCard class="topic-wide" title="电桩状态变化时间线" subtitle="Spark DWD · 最近20条"><div class="event-table"><table><thead><tr><th>时间</th><th>电桩编号</th><th>状态变化</th><th>原因</th></tr></thead><tbody><tr v-for="log in topics?.pile_logs || []" :key="log.id"><td>{{ timestamp(log.created_at) }}</td><td>{{ log.pile_no }}</td><td>{{ statusLabel(log.old_status) }} → {{ statusLabel(log.new_status) }}</td><td>{{ log.reason }}</td></tr></tbody></table></div></PanelCard>
  </template>

  <template v-else-if="page === 'orders'">
    <div class="metrics-grid"><MetricCard title="今日创建" :value="overview.today_order_count || 0" unit="单"/><MetricCard title="历史已完成" :value="orderCount('COMPLETED')" unit="单"/><MetricCard title="正在充电" :value="orderCount('CHARGING')" unit="单"/><MetricCard title="待支付" :value="orderCount('UNPAID')" unit="单"/><MetricCard title="平均充电时长" :value="number((orders.avg_duration_seconds || 0) / 60)" unit="分钟"/></div>
    <div class="topic-grid"><PanelCard title="今日订单业务历程" subtitle="Spark DWD · 按今日创建订单计数"><TopicChart :option="funnelChart" label="创建、预约、充电、结束和支付里程碑"/></PanelCard><PanelCard title="当前订单时间趋势" subtitle="Spark DWD · 北京时间，按小时创建"><TopicChart :option="hourChart" label="今日每小时订单数"/></PanelCard><PanelCard title="订单状态分布" subtitle="Spark DWD · 全部历史与进行中订单"><TopicChart :option="orderChart" label="订单状态数量"/></PanelCard></div>
    <PanelCard class="topic-wide" title="进行中订单" subtitle="Spark ADS · 最近8笔活跃订单"><RealtimeOrders :orders="realtimeOrders.items || []"/></PanelCard>
  </template>

  <template v-else-if="page === 'users'">
    <div class="metrics-grid"><MetricCard title="用户总量" :value="users.total_users || 0" unit="人"/><MetricCard title="今日新增" :value="users.today_new || 0" unit="人"/><MetricCard title="近30日活跃" :value="users.active_30d || 0" unit="人"/><MetricCard title="累计充值" :value="money(users.recharge_cents)" unit="元"/><MetricCard title="账户余额合计" :value="money(users.balance_cents)" unit="元"/></div>
    <div class="topic-grid"><PanelCard title="用户增长" subtitle="Spark DWD · 近30日每日新增"><TopicChart :option="growthChart" label="每日新增用户"/></PanelCard><PanelCard title="消费层级" subtitle="Spark ADS · 累计已支付金额 · 每25元一档"><TopicChart :option="spendChart" label="用户消费金额分布"/></PanelCard><PanelCard title="充值趋势" subtitle="Spark DWD · 近30日"><TopicChart :option="rechargeChart" label="每日充值金额"/></PanelCard></div>
    <div class="topic-grid two-columns topic-wide"><PanelCard title="用户消费 Top 10" subtitle="全历史已完成订单 · 用户以编号匿名展示"><div class="event-table"><table><thead><tr><th>用户</th><th>完成订单</th><th>消费金额</th></tr></thead><tbody><tr v-for="user in users.top || []" :key="user.user_id"><td>用户 #{{ user.user_id }}</td><td>{{ user.order_count }}</td><td>¥ {{ money(user.amount_cents) }}</td></tr></tbody></table></div></PanelCard><PanelCard title="用户账户状态" subtitle="真实业务字段，不推断年龄/性别/车型"><TopicChart :option="userStatusChart" label="正常和冻结用户数量"/></PanelCard></div>
  </template>

  <template v-else-if="page === 'energy'">
    <div class="metrics-grid"><MetricCard title="今日统计电量" :value="number((overview.today_energy_wh || 0) / 1000)" unit="kWh"/><MetricCard title="本月统计电量" :value="number((topics?.energy.month_energy_wh || 0) / 1000)" unit="kWh"/><MetricCard title="今日营收" :value="money(overview.today_revenue_cents)" unit="元"/><MetricCard title="本月营收" :value="money(topics?.energy.month_revenue_cents)" unit="元"/><MetricCard title="平均订单金额" :value="money(orders.avg_amount_cents)" unit="元"/></div>
    <div class="topic-grid energy-layout"><PanelCard title="能源与营收双轴趋势" subtitle="Spark ADS · 近30日"><TopicChart :option="energyChart" label="充电量和营收双轴趋势"/></PanelCard><PanelCard title="站点营收 Top 10" subtitle="Spark ADS · 近30日已支付"><TopicChart :option="revenueChart" label="站点营收排名"/></PanelCard></div>
    <PanelCard class="topic-wide" title="负荷预测" :subtitle="`独立ML结果 · ${loadPrediction.model_version || '未加载'} · 尚非设备电表遥测`"><LoadPrediction :data="loadPrediction"/></PanelCard>
  </template>

  <template v-else-if="page === 'system'">
    <div class="metrics-grid"><MetricCard title="数据日期" :value="topics?.data_as_of || '—'" unit=""/><MetricCard title="Spark 批次" :value="system.batch_id || '—'" unit=""/><MetricCard title="数仓质量评分" :value="overview.quality_score ?? analytics.batch?.quality_score ?? '—'" unit="分"/><MetricCard title="ADS 表数" :value="system.tables?.length || 0" unit="张"/><MetricCard title="展示数据源" :value="system.storage || 'Spark ADS'" unit=""/></div>
    <div class="topic-grid two-columns"><PanelCard title="系统运行架构" subtitle="Web 统计只使用 Spark 数据源"><div class="system-flow"><div>Python 当前日期数据生成器</div><span>↓ CSV 模拟数据</span><div>Spark ODS → 质量检测 → DWD</div><span>↓ 聚合计算</span><div>DWS → ADS</div><span>↓ 只读 JSON 快照</span><div>Flask API → Vue Web 大屏</div></div></PanelCard><PanelCard title="数仓表规模" subtitle="Spark DWD 清洗后记录数"><div class="event-table"><table><thead><tr><th>数仓表</th><th>记录数</th></tr></thead><tbody><tr v-for="table in system.tables || []" :key="table.name"><td>{{ table.name }}</td><td>{{ table.count.toLocaleString() }}</td></tr></tbody></table></div><p>ADS 快照 {{ timestamp(topics?.generated_at) }}</p></PanelCard></div>
    <div class="topic-grid two-columns topic-wide"><PanelCard title="数仓批次状态" subtitle="已发布 Spark 结果"><p>{{ analytics.label }}</p><p v-if="analytics.batch">{{ analytics.batch.window_start }} → {{ analytics.batch.window_end }}</p><p v-if="analytics.batch">输入订单 {{ analytics.batch.input_orders }} · 质量 {{ analytics.batch.quality_score ?? '—' }}</p><p>{{ analytics.warning || '批次读取正常' }}</p></PanelCard><PanelCard title="数据源约束" subtitle="Web 与统计接口"><p>主题页、设备分页、订单、站点、用户和能源数据均来自 Spark ADS。</p><p>Qt 的 SQLite 仅服务桌面端事务，不参与 Web 大屏统计。</p></PanelCard></div>
    </template>
  </main>
</template>
<script setup>
import { computed, inject, ref } from 'vue'
import MetricCard from '../components/MetricCard.vue'
import PanelCard from '../components/PanelCard.vue'
import TopicChart from '../components/TopicChart.vue'
import PileMatrix from '../components/PileMatrix.vue'
import PileStatusChart from '../components/PileStatusChart.vue'
import DistributionMap from '../components/DistributionMap.vue'
import RealtimeOrders from '../components/RealtimeOrders.vue'
import DimensionComparison from '../components/DimensionComparison.vue'
import LoadPrediction from '../components/LoadPrediction.vue'
defineProps({ page: { type: String, required: true } })
const { topics, topicError, overview, pileStatus, realtimeOrders, revenueTrend, analytics, comparisons, loadPrediction } = inject('dashboard')
const orders = computed(() => topics.value?.orders || {}), users = computed(() => topics.value?.users || {}), system = computed(() => topics.value?.system || {})
const stations = computed(() => topics.value?.stations || []), mapMode = ref('繁忙率')
const number = n => Number(n || 0).toLocaleString('zh-CN', { maximumFractionDigits: 1 })
const money = n => number(Number(n || 0) / 100)
const timestamp = value => value ? new Date(value).toLocaleString('zh-CN', { timeZone: 'Asia/Shanghai', hour12: false }) : '—'
const labels = { IDLE: '空闲', RESERVED: '预约', CHARGING: '充电中', FAULT: '故障', OFFLINE: '离线', PENDING: '待预约', UNPAID: '待支付', COMPLETED: '已完成', CANCELLED: '已取消', NORMAL: '正常', FROZEN: '冻结' }
const statusLabel = s => labels[s] || s
const orderCount = s => orders.value.statuses?.find(row => row.status === s)?.count || 0
function chart(names, values, name, type = 'bar', unit = '') {
  return { color: ['#38bdf8', '#34d399'], tooltip: { trigger: 'axis' }, grid: { left: 64, right: 28, top: 35, bottom: 65 }, xAxis: { type: 'category', data: names, axisLabel: { color: '#9fb7d4', rotate: names.length > 12 ? 30 : 0 } }, yAxis: { type: 'value', name: unit, nameTextStyle: { color: '#9fb7d4' }, axisLabel: { color: '#9fb7d4' }, splitLine: { lineStyle: { color: '#183348' } } }, series: [{ name, type, data: values, smooth: true, areaStyle: type === 'line' ? { opacity: 0.12 } : undefined }] }
}
const districts = computed(() => { const data = new Map(); for (const s of stations.value) data.set(s.district, (data.get(s.district) || 0) + s.pile_count); return [...data].sort((a,b) => b[1]-a[1]) })
const districtChart = computed(() => chart(districts.value.map(r => r[0]), districts.value.map(r => r[1]), '电桩', 'bar', '台'))
const busiest = computed(() => [...stations.value].filter(s => s.station_status === 'ACTIVE').sort((a,b) => b.utilization_rate-a.utilization_rate || a.station_id-b.station_id).slice(0,10))
const mapStations = computed(() => stations.value.map(s => ({ ...s, utilization_rate: mapMode.value === '空闲率' ? (s.pile_count ? 100*s.available_pile_count/s.pile_count : 0) : mapMode.value === '故障率' ? (s.pile_count ? 100*s.fault_count/s.pile_count : 0) : s.utilization_rate })))
const orderChart = computed(() => chart((orders.value.statuses || []).map(r => statusLabel(r.status)), (orders.value.statuses || []).map(r => r.count), '订单', 'bar', '单'))
const hourChart = computed(() => chart(Array.from({length:24},(_,i) => `${i}:00`), Array.from({length:24},(_,i) => orders.value.hourly?.find(r => r.hour === i)?.count || 0), '今日创建', 'line', '单'))
const funnelChart = computed(() => { const keys = ['created','reserved','started','stopped','paid'], names = ['创建订单','已预约','已开始','已结束','已支付']; return { color: ['#38bdf8','#22d3ee','#34d399','#60a5fa','#a78bfa'], tooltip: { trigger: 'item' }, series: [{ type: 'funnel', sort: 'none', left: '15%', width: '70%', top: 20, bottom: 25, label: { formatter: '{b}: {c}' }, data: keys.map((k,i) => ({ name: names[i], value: orders.value.funnel?.[k] || 0 })) }] } })
function calendarSeries(rows, field) { const dates = Array.from({length:30},(_,i) => { const date = new Date(); date.setDate(date.getDate()-29+i); return new Intl.DateTimeFormat('sv-SE',{timeZone:'Asia/Shanghai'}).format(date) }); return { dates, values: dates.map(date => rows?.find(r => r.date === date)?.[field] || 0) } }
const growthChart = computed(() => { const s=calendarSeries(users.value.growth,'count'); return chart(s.dates,s.values,'新增用户','line','人') })
const rechargeChart = computed(() => { const s=calendarSeries(users.value.recharges,'amount_cents'); return chart(s.dates,s.values.map(v => v/100),'充值','line','元') })
const spendChart = computed(() => chart((users.value.spending || []).map(r => r.band), (users.value.spending || []).map(r => r.count),'用户','bar','人'))
const userStatusChart = computed(() => chart((users.value.statuses || []).map(r => statusLabel(r.status)), (users.value.statuses || []).map(r => r.count),'用户','bar','人'))
const energyChart = computed(() => { const rows=revenueTrend.value.items || []; const base=chart(rows.map(r => r.biz_date || r.date),rows.map(r => Number(r.energy_wh || 0)/1000),'电量','line','kWh'); base.legend={ textStyle:{color:'#9fb7d4'} }; base.yAxis=[base.yAxis,{...base.yAxis,name:'元',position:'right'}]; base.series.push({name:'营收',type:'bar',yAxisIndex:1,data:rows.map(r => Number(r.revenue_cents || 0)/100)}); return base })
const revenueChart = computed(() => { const rows=[...stations.value].sort((a,b) => b.revenue_cents-a.revenue_cents).slice(0,10); return chart(rows.map(s => `#${s.station_id}`),rows.map(s => s.revenue_cents/100),'已支付营收','bar','元') })
</script>
