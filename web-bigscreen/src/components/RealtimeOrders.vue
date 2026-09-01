<template>
  <div class="orders-list">
    <div v-for="order in orders" :key="order.id" class="order-row">
      <div>
        <strong>{{ order.station_name }}</strong>
        <span>{{ order.order_no }} · {{ order.pile_no }}</span>
      </div>
      <b :class="['status', order.status.toLowerCase()]">{{ statusText(order.status) }}</b>
      <em>{{ formatNumber(wToKw(order.power_w), 1) }}kW</em>
      <strong>{{ formatNumber(centsToYuan(order.amount_cents), 1) }}元</strong>
    </div>
  </div>
</template>

<script setup>
import { centsToYuan, formatNumber, wToKw } from '../utils/format'

defineProps({ orders: { type: Array, default: () => [] } })

function statusText(status) {
  const map = { PENDING: '待预约', RESERVED: '已预约', CHARGING: '充电中', UNPAID: '待结算', COMPLETED: '已完成', CANCELLED: '已取消' }
  return map[status] || status
}
</script>
