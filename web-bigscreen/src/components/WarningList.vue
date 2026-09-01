<template>
  <div class="warning-list">
    <div v-for="item in warnings" :key="item.title" class="warning-item">
      <span :class="['warning-level', item.level === '高' ? 'high' : 'middle']">{{ item.level }}</span>
      <div>
        <strong>{{ item.title }}</strong>
        <p>{{ item.detail }}</p>
      </div>
    </div>
  </div>
</template>

<script setup>
defineProps({ warnings: { type: Array, default: () => [] } })
</script>
