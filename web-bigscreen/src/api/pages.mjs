export const pages = [
  { id: 'overview', title: '综合态势', subtitle: '资源规模 · 实时业务 · 历史分析' },
  { id: 'stations', title: '站点运营', subtitle: '空间资源 · 繁忙程度 · 区域差异' },
  { id: 'piles', title: '电桩监控', subtitle: '设备状态矩阵 · 状态变化记录' },
  { id: 'orders', title: '订单运营', subtitle: '当日充电流程 · 订单状态 · 小时分布' },
  { id: 'users', title: '用户分析', subtitle: '用户增长 · 活跃度 · 充值与消费' },
  { id: 'energy', title: '能源营收', subtitle: '平台统计电量 · 营收 · 清洗后历史趋势' },
  { id: 'system', title: '系统监控', subtitle: '运行架构 · 数据库 · 数仓质量' }
]
export function pageFromHash(hash) {
  const id = hash.replace(/^#\/?/, '')
  return pages.find(page => page.id === id) || pages[0]
}
