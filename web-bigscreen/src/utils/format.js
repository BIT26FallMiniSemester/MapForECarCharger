export function centsToYuan(value) {
  return Number(value || 0) / 100
}

export function whToKwh(value) {
  return Number(value || 0) / 1000
}

export function wToKw(value) {
  return Number(value || 0) / 1000
}

export function formatNumber(value, digits = 1) {
  return Number(value || 0).toLocaleString('zh-CN', {
    minimumFractionDigits: 0,
    maximumFractionDigits: digits
  })
}

export function formatDateLabel(dateText) {
  if (!dateText) return ''
  const date = new Date(dateText)
  if (Number.isNaN(date.getTime())) return String(dateText).slice(5)
  return `${String(date.getMonth() + 1).padStart(2, '0')}-${String(date.getDate()).padStart(2, '0')}`
}

export function formatTimeLabel(dateText) {
  if (!dateText) return ''
  const date = new Date(dateText)
  if (Number.isNaN(date.getTime())) return String(dateText)
  return `${String(date.getHours()).padStart(2, '0')}:00`
}
