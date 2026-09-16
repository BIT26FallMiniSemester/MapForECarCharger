<template>
  <div ref="chartRef" class="distribution-map"></div>
</template>

<script setup>
import { onBeforeUnmount, onMounted, ref, watch } from 'vue'
import * as echarts from 'echarts'
import { formatNumber } from '../utils/format'

const props = defineProps({
  stations: { type: Array, default: () => [] },
  overview: { type: Object, default: () => ({}) },
  metricLabel: { type: String, default: '利用率' }
})

const chartRef = ref(null)
let chart
let mapReady = false

function buildPoints() {
  return props.stations
    .filter(station => Number.isFinite(Number(station.longitude)) && Number.isFinite(Number(station.latitude)))
    .map(station => ({
      name: `站点 #${station.station_id ?? station.id ?? '—'}`,
      value: [
        Number(station.longitude),
        Number(station.latitude),
        station.utilization_rate || 0,
        station.order_count || 0,
        station.pile_count || station.total_piles || 0
      ],
      revenue: station.revenue_cents || 0,
      available: station.available_pile_count || station.available_piles || 0,
      pileCount: station.pile_count || station.total_piles || 0,
      operator: station.operator_name || '未知运营商'
    }))
}

async function loadMap() {
  if (mapReady) return true
  const response = await fetch('/maps/beijing.json')
  const geoJson = await response.json()
  echarts.registerMap('beijing', geoJson)
  mapReady = true
  return true
}

function render() {
  if (!chart || !mapReady) return
  const points = buildPoints()

  chart.setOption({
    backgroundColor: 'transparent',
    tooltip: {
      trigger: 'item',
      formatter: params => {
        if (!params.data || !Array.isArray(params.data.value)) return params.name
        const value = params.data.value
        return `${params.name}<br/>经纬度：${formatNumber(value[1], 5)}, ${formatNumber(value[0], 5)}<br/>${props.metricLabel}：${formatNumber(value[2], 1)}%<br/>空闲/总桩：${params.data.available}/${params.data.pileCount}`
      }
    },
    geo: {
      map: 'beijing',
      roam: false,
      // Beijing's north-east (Miyun) needs extra headroom. A lower layout centre
      // uses the panel's unused lower area without cropping the district outline.
      zoom: 0.92,
      center: [116.405, 40.12],
      layoutCenter: ['50%', '56%'],
      layoutSize: '96%',
      label: {
        show: true,
        color: 'rgba(220,236,255,.72)',
        fontSize: 11
      },
      itemStyle: {
        areaColor: 'rgba(13, 48, 73, .76)',
        borderColor: 'rgba(77, 208, 255, .55)',
        borderWidth: 1
      },
      emphasis: {
        label: { color: '#ffffff' },
        itemStyle: { areaColor: 'rgba(20, 88, 119, .95)' }
      }
    },
    graphic: [
      {
        type: 'text',
        right: 24,
        top: 18,
        style: {
          text: `${props.overview.station_count || points.length} 个站点 · ${props.overview.pile_count || 0} 个模拟电桩`,
          fill: '#8bdcff',
          fontSize: 14,
          fontWeight: 600
        }
      }
    ],
    series: [
      {
        name: '充电站点',
        type: 'scatter',
        coordinateSystem: 'geo',
        zlevel: 2,
        data: points,
        symbolSize: value => {
          return Math.max(3, Math.min(12, 3 + Math.sqrt(Number(value[4]) || 0)))
        },
        itemStyle: {
          color: 'rgba(45, 212, 191, .92)',
          shadowBlur: 8,
          shadowColor: 'rgba(45, 212, 191, .55)'
        },
        label: { show: false },
        emphasis: { label: { show: false } }
      },
      {
        name: `${props.metricLabel}高值站点`,
        type: 'effectScatter',
        coordinateSystem: 'geo',
        zlevel: 3,
        rippleEffect: { brushType: 'stroke', scale: 2.4 },
        symbolSize: value => {
          const utilization = Number(value[2]) || 0
          return Math.max(6, Math.min(14, 5 + utilization / 12))
        },
        itemStyle: {
          color: '#facc15',
          shadowBlur: 12,
          shadowColor: 'rgba(250, 204, 21, .7)'
        },
        data: points.filter(point => point.value[2] >= 50).slice(0, 28)
      }
    ]
  })
}

onMounted(async () => {
  chart = echarts.init(chartRef.value)
  await loadMap()
  render()
  window.addEventListener('resize', chart.resize)
})
watch(() => [props.stations, props.overview, props.metricLabel], render, { deep: true })
onBeforeUnmount(() => {
  window.removeEventListener('resize', chart?.resize)
  chart?.dispose()
})
</script>
