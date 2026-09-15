<template>
  <div class="dashboard-shell">
    <header class="topbar">
      <div><p>EV Charging Operation Center · {{ page.subtitle }}</p><h1>充电运营平台 · {{ page.title }}</h1></div>
      <div class="topbar-right"><span>{{ dashboard.dataSource.value }} · 每 5 秒刷新</span><strong>{{ time }}</strong></div>
    </header>
    <nav class="screen-nav" aria-label="大屏专题导航"><a v-for="item in pages" :key="item.id" :href="`#/${item.id}`" :class="{ active: page.id === item.id }" :aria-current="page.id === item.id ? 'page' : undefined"><span>{{ String(pages.indexOf(item) + 1).padStart(2, '0') }}</span>{{ item.title }}</a></nav>
    <DashboardView v-if="page.id === 'overview'" />
    <TopicView v-else :page="page.id" />
  </div>
</template>

<script setup>
import { ref, provide, onMounted, onBeforeUnmount } from 'vue'
import DashboardView from './views/DashboardView.vue'
import TopicView from './views/TopicView.vue'
import { pages, pageFromHash } from './api/pages.mjs'
import { useDashboard } from './composables/useDashboard'
const dashboard = useDashboard(); provide('dashboard', dashboard)
const page = ref(pageFromHash(window.location.hash)), time = ref('')
const updatePage = () => { page.value = pageFromHash(window.location.hash); document.title = `${page.value.title} · 充电运营平台` }
let clock
onMounted(() => { updatePage(); window.addEventListener('hashchange', updatePage); const tick = () => { time.value = new Date().toLocaleString('zh-CN', { hour12: false }) }; tick(); clock = setInterval(tick, 1000) })
onBeforeUnmount(() => { window.removeEventListener('hashchange', updatePage); clearInterval(clock) })
</script>
