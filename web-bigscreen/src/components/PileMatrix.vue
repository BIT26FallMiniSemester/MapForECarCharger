<template>
  <div class="pile-toolbar">
    <label>状态 <select v-model="status"><option value="">全部</option><option v-for="(label, key) in labels" :key="key" :value="key">{{ label }}</option></select></label>
    <span>已接入（模拟） · 每格一个电桩 · 额定功率</span>
  </div>
  <p v-if="error" role="alert">{{ error }}</p>
  <div class="pile-matrix">
    <div v-for="pile in data.items" :key="pile.id" class="pile-cell" :class="pile.status.toLowerCase()" :title="`${pile.pile_no} · 站点 #${pile.station_id} · ${pile.rated_power_w / 1000} kW · ${labels[pile.status]}`">
      <span>#{{ pile.id }}</span><strong>{{ labels[pile.status] }}</strong>
    </div>
  </div>
  <p v-if="!error && !data.items.length">当前筛选无电桩</p>
  <div class="pile-pager"><button :disabled="page <= 1 || loading" @click="page--">上一页</button><span>第 {{ page }} / {{ pages }} 页 · {{ data.total }} 个电桩</span><button :disabled="page >= pages || loading" @click="page++">下一页</button></div>
</template>
<script setup>
import { ref, computed, watch, onMounted, onBeforeUnmount } from 'vue'
import { getPiles } from '../api/dashboard'
const labels = { IDLE: '空闲', RESERVED: '预约', CHARGING: '充电', FAULT: '故障', OFFLINE: '离线' }
const data = ref({ items: [], total: 0 }), page = ref(1), status = ref(''), error = ref(''), loading = ref(false)
const pages = computed(() => Math.max(1, Math.ceil(data.value.total / 120)))
let timer, controller, stopped = false, sequence = 0
async function refresh() {
  clearTimeout(timer); controller?.abort(); controller = new AbortController()
  const current = ++sequence; loading.value = true
  try {
    const result = await getPiles(page.value, status.value, controller.signal)
    if (stopped || current !== sequence) return
    data.value = result; error.value = ''
    if (page.value > pages.value) page.value = pages.value
  } catch (err) { if (!stopped && current === sequence && err.code !== 'ERR_CANCELED') error.value = '电桩读取失败，请检查 Flask 服务' }
  finally { if (!stopped && current === sequence) { loading.value = false; timer = setTimeout(refresh, 5000) } }
}
watch(status, () => { if (page.value !== 1) page.value = 1; else refresh() })
watch(page, refresh)
onMounted(refresh)
onBeforeUnmount(() => { stopped = true; clearTimeout(timer); controller?.abort() })
</script>
