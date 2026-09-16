export function mergeAnalytics(live, response, failure = '') {
  const batch = response?.available ? response.data : null
  if (!batch) return {
    ...live,
    analytics: { label: 'Spark ADS 尚未就绪', warning: failure || 'Spark 批次结果尚未就绪', batch: null }
  }
  if (batch.schema_version !== 1 || !['hadoop-mapreduce', 'local-mapreduce', 'spark-sql'].includes(batch.engine) ||
      !Array.isArray(batch.revenue_trend?.items) || batch.revenue_trend.items.length < 1 ||
      !Array.isArray(batch.station_ranking?.items) || !Number.isFinite(Date.parse(batch.snapshot_at))) {
    return mergeAnalytics(live, null, 'Spark 批处理结果格式异常')
  }
  const stations = new Map((live.stationDistribution?.items || []).map(row => [row.station_id, row]))
  return {
    ...live,
    overview: live.overview,
    revenueTrend: batch.revenue_trend,
    stationRanking: { ...batch.station_ranking, items: batch.station_ranking.items.map(row => ({ ...stations.get(row.station_id), ...row })) },
    analytics: {
      label: batch.engine === 'spark-sql' ? '历史统计：SparkSQL 离线数仓' : batch.engine === 'hadoop-mapreduce' ? '历史统计：Hadoop 批处理' : '历史统计：本地验证计算',
      warning: response.stale ? '批处理数据已过期，请重新运行统计作业' : '', batch
    }
  }
}
