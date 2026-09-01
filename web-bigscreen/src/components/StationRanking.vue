<template>
  <div ref="chartRef" class="chart"></div>
</template>

<script setup>
import { onBeforeUnmount, onMounted, ref, watch } from 'vue'
import * as echarts from 'echarts'
import { centsToYuan } from '../utils/format'

const props = defineProps({ data: { type: Object, default: () => ({ items: [] }) } })
const chartRef = ref(null)
let chart

function render() {
  if (!chart) return
  const items = props.data.items || []
  chart.setOption({
    color: ['#2dd4bf'],
    tooltip: { trigger: 'axis', axisPointer: { type: 'shadow' } },
    grid: { left: 92, right: 46, top: 16, bottom: 30 },
    xAxis: { type: 'value', axisLabel: { color: '#9fb7d4' }, splitLine: { lineStyle: { color: 'rgba(120,160,200,.14)' } } },
    yAxis: { type: 'category', inverse: true, data: items.map(item => item.station_name), axisLabel: { color: '#dcecff', width: 84, overflow: 'truncate' } },
    series: [{
      name: '站点营收',
      type: 'bar',
      barWidth: 12,
      data: items.map(item => centsToYuan(item.revenue_cents)),
      label: { show: true, position: 'right', color: '#dcecff', formatter: '{c}元' }
    }]
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
