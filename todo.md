


### 本地开发

研究c语言调试和分析工具
研究-ftrapv是否在cmake中生效


### 多线程
AI逻辑多线程执行
统一使用 C23 标准线程 API（`<threads.h>`），原子操作使用 `<stdatomic.h>`。

### 网络通信

高频数据（位置更新）通过 P2P 传输（降低延迟）
关键数据（击杀、得分）通过服务器验证（保证安全）

#### 网络库选型
- GameNetworkingSockets 网络加密，P2P以及NAT穿透
- ENet