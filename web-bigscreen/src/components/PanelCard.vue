<template>
  <section class="panel">
    <div class="panel-title">
      <h2>{{ title }}</h2>
      <span v-if="subtitle">{{ subtitle }}</span>
    </div>
    <slot />
  </section>
</template>

<script setup>
defineProps({
  title: { type: String, required: true },
  subtitle: { type: String, default: '' }
})
</script>
