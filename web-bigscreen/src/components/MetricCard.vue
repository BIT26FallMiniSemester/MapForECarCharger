<template>
  <article class="metric-card">
    <div class="metric-icon">
      <component :is="iconComponent" :size="22" />
    </div>
    <div>
      <p>{{ title }}</p>
      <strong>{{ displayValue }}<span>{{ unit }}</span></strong>
    </div>
  </article>
</template>

<script setup>
import { computed } from 'vue'
import { BatteryCharging, CircleDollarSign, ClipboardList, Gauge, PlugZap, WalletCards } from 'lucide-vue-next'

const props = defineProps({
  title: { type: String, required: true },
  value: { type: [Number, String], required: true },
  unit: { type: String, default: '' },
  icon: { type: String, default: 'gauge' }
})

const icons = {
  revenue: CircleDollarSign,
  today: WalletCards,
  order: ClipboardList,
  energy: BatteryCharging,
  pile: PlugZap,
  gauge: Gauge
}

const iconComponent = computed(() => icons[props.icon] || Gauge)
const displayValue = computed(() => {
  if (typeof props.value !== 'number') return props.value
  return props.value.toLocaleString('zh-CN', { maximumFractionDigits: 1 })
})
</script>
