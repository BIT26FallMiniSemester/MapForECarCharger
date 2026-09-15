<template><div ref="element" class="topic-chart" role="img" :aria-label="label"></div></template>
<script setup>
import { onMounted, onBeforeUnmount, ref, watch } from 'vue'
import * as echarts from 'echarts'
const props = defineProps({ option: { type: Object, required: true }, label: { type: String, required: true } })
const element = ref(null)
let chart, observer
const render = () => chart?.setOption(props.option, true)
onMounted(() => { chart = echarts.init(element.value); render(); observer = new ResizeObserver(() => chart?.resize()); observer.observe(element.value) })
watch(() => props.option, render, { deep: true })
onBeforeUnmount(() => { observer?.disconnect(); chart?.dispose() })
</script>
