# 显式数据结构

这里定义 ODS 读取和 DWD 输出的 PySpark `StructType`。ODS 对容易污染的数字和时间字段优先按字符串读取，防止解析失败信息丢失。
