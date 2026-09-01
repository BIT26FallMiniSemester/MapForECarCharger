<template>
  <div ref="chartRef" class="chart"></div>
</template>

<script setup>
import { onBeforeUnmount, onMounted, ref, watch } from 'vue'
import * as echarts from 'echarts'
import { formatTimeLabel, wToKw } from '../utils/format'

const props = defineProps({ data: { type: Object, default: () => ({ points: [] }) } })
const chartRef = ref(null)
let chart

function render() {
  if (!chart) return
  const points = props.data.points || []
  chart.setOption({
    color: ['#f59e0b', '#60a5fa'],
    tooltip: { trigger: 'axis' },
    legend: { top: 0, right: 8, textStyle: { color: '#9fb7d4' } },
    grid: { left: 44, right: 44, top: 42, bottom: 34 },
    xAxis: { type: 'category', data: points.map(item => formatTimeLabel(item.predicted_for)), axisLabel: { color: '#9fb7d4' }, axisLine: { lineStyle: { color: '#31506f' } } },
    yAxis: { type: 'value', axisLabel: { color: '#9fb7d4' }, splitLine: { lineStyle: { color: 'rgba(120,160,200,.14)' } } },
    series: [
      { name: '预测负荷kW', type: 'line', smooth: true, data: points.map(item => wToKw(item.load_w)) },
      { name: '预测空闲桩', type: 'line', smooth: true, data: points.map(item => item.available_piles) }
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
