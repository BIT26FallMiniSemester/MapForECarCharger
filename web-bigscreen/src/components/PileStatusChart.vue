<template>
  <div ref="chartRef" class="chart"></div>
</template>

<script setup>
import { onBeforeUnmount, onMounted, ref, watch } from 'vue'
import * as echarts from 'echarts'

const props = defineProps({ data: { type: Object, default: () => ({ items: [] }) } })
const chartRef = ref(null)
let chart

const labels = {
  IDLE: '空闲',
  RESERVED: '已预约',
  CHARGING: '充电中',
  FAULT: '故障',
  OFFLINE: '离线'
}

function render() {
  if (!chart) return
  const data = (props.data.items || []).map(item => ({ name: labels[item.status] || item.status, value: item.count }))
  chart.setOption({
    color: ['#22c55e', '#facc15', '#38bdf8', '#fb7185', '#94a3b8'],
    tooltip: { trigger: 'item' },
    legend: { bottom: 0, textStyle: { color: '#9fb7d4' } },
    series: [{
      name: '电桩状态',
      type: 'pie',
      radius: ['48%', '70%'],
      center: ['50%', '44%'],
      avoidLabelOverlap: true,
      label: { color: '#dcecff', formatter: '{b}\n{d}%' },
      data
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
