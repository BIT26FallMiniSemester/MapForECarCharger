import { defineConfig, loadEnv } from 'vite'
import vue from '@vitejs/plugin-vue'

export default defineConfig(({ mode }) => {
  const env = loadEnv(mode, process.cwd(), '')
  const proxy = { '/api': { target: env.DASHBOARD_TARGET || 'http://127.0.0.1:9001', changeOrigin: true } }
  return { plugins: [vue()], server: { port: 5173, host: '0.0.0.0', proxy }, preview: { proxy } }
})
