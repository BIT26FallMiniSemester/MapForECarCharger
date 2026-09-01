<template>
  <div ref="chartRef" class="chart"></div>
</template>

<script setup>
import { onBeforeUnmount, onMounted, ref, watch } from 'vue'
import * as echarts from 'echarts'
import { centsToYuan, formatDateLabel } from '../utils/format'

const props = defineProps({ data: { type: Object, default: () => ({ items: [] }) } })
const chartRef = ref(null)
let chart

function render() {
  if (!chart) return
  const items = props.data.items || []
  chart.setOption({
    color: ['#38bdf8', '#34d399'],
    tooltip: { trigger: 'axis' },
    legend: { top: 0, right: 8, textStyle: { color: '#9fb7d4' } },
    grid: { left: 46, right: 24, top: 42, bottom: 34 },
    xAxis: { type: 'category', data: items.map(item => formatDateLabel(item.date)), axisLine: { lineStyle: { color: '#31506f' } }, axisLabel: { color: '#9fb7d4' } },
    yAxis: [
      { type: 'value', name: '元', nameTextStyle: { color: '#9fb7d4' }, splitLine: { lineStyle: { color: 'rgba(120,160,200,.14)' } }, axisLabel: { color: '#9fb7d4' } },
      { type: 'value', name: '单', nameTextStyle: { color: '#9fb7d4' }, splitLine: { show: false }, axisLabel: { color: '#9fb7d4' } }
    ],
    series: [
      { name: '营收', type: 'line', smooth: true, areaStyle: { opacity: 0.16 }, data: items.map(item => centsToYuan(item.revenue_cents)) },
      { name: '订单', type: 'bar', yAxisIndex: 1, barWidth: 12, data: items.map(item => item.order_count) }
    ]
  })
}

onMounted(() => {
  chart = echarts.init(chartRef.value)
  render()
  window.addEventListener('resize', chart.resize)
})
watch(() => props.data, render, { deep: true })
onBeforeUnmount(() => {
  window.removeEventListener('resize', chart?.resize)
  chart?.dispose()
})
</script>
