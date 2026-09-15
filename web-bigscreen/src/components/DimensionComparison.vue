<template>
  <div class="comparison-grid">
    <div><h3>区域 × 快/慢充：每站完成订单数</h3><div ref="districtRef" class="chart"></div></div>
    <div><h3>工作日/周末 × 开始小时：日均完成订单数</h3><div ref="hourRef" class="chart"></div></div>
  </div>
</template>

<script setup>
import { onBeforeUnmount, onMounted, ref, watch } from 'vue'
import * as echarts from 'echarts'

const props = defineProps({ data: { type: Object, required: true } })
const districtRef = ref(null), hourRef = ref(null)
let districtChart, hourChart

function render() {
  if (!districtChart || !hourChart) return
  const districtRows = props.data.district_charge_type || []
  const totals = new Map()
  for (const row of districtRows) totals.set(row.district, (totals.get(row.district) || 0) + Number(row.orders_per_station || 0))
  const districts = [...totals.keys()].sort((a, b) => totals.get(b) - totals.get(a)).slice(0, 10)
  const counts = new Map(districtRows.map(row => [`${row.district}:${row.charge_type}`, Number(row.orders_per_station || 0)]))
  const hours = props.data.day_type_hour || []
  const hourCounts = new Map(hours.map(row => [`${row.day_type}:${row.biz_hour}`, Number(row.avg_orders_per_day || 0)]))
  const labels = { WEEKDAY: '工作日', WEEKEND: '周末' }
  const common = { tooltip: { trigger: 'axis' }, legend: { top: 0, textStyle: { color: '#9fb7d4' } },
    grid: { left: 48, right: 18, top: 42, bottom: 50 },
    yAxis: { type: 'value', axisLabel: { color: '#9fb7d4' }, splitLine: { lineStyle: { color: 'rgba(120,160,200,.14)' } } } }
  districtChart.setOption({ ...common,
    xAxis: { type: 'category', data: districts, axisLabel: { color: '#9fb7d4', rotate: 25 } },
    series: ['FAST', 'SLOW'].map((type, index) => ({ name: type === 'FAST' ? '快充' : '慢充', type: 'bar', stack: 'orders',
      itemStyle: { color: index ? '#34d399' : '#38bdf8' }, data: districts.map(district => counts.get(`${district}:${type}`) || 0) })) })
  hourChart.setOption({ ...common,
    xAxis: { type: 'category', data: Array.from({ length: 24 }, (_, hour) => `${hour}:00`), axisLabel: { color: '#9fb7d4', interval: 2 } },
    series: ['WEEKDAY', 'WEEKEND'].map((type, index) => ({ name: labels[type], type: 'line', smooth: true,
      itemStyle: { color: index ? '#34d399' : '#38bdf8' }, data: Array.from({ length: 24 }, (_, hour) => hourCounts.get(`${type}:${hour}`) || 0) })) })
}

function resize() { districtChart?.resize(); hourChart?.resize() }
onMounted(() => {
  districtChart = echarts.init(districtRef.value)
  hourChart = echarts.init(hourRef.value)
  render()
  window.addEventListener('resize', resize)
})
watch(() => props.data, render, { deep: true })
onBeforeUnmount(() => { window.removeEventListener('resize', resize); districtChart?.dispose(); hourChart?.dispose() })
</script>
