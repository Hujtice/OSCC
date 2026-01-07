# WebRTC Sender 带宽缩放重构说明（方案A）

## 修改日期
2025-12-26

## 核心问题
原有代码在 `DeliveryPacket()` 中硬编码 10ms 间隔来实现带宽缩放，存在以下问题：
1. 硬编码 10ms 与实际 GCC 估计的带宽无关，违反了带宽控制的本质
2. 在错误的层面进行缩放，可能与 WebRTC 内部的 Pacer 冲突
3. 编码器无法感知带宽限制，导致码率控制不协调

## 解决方案（方案A）
**通过 BitrateConstraints 让 WebRTC 内部的 Pacer 感知缩放后的带宽限制**

### 修改文件
- `/home/hjt/OSCC/ns-allinone-3.31/ns-3.31/src/ex-webrtc/model/webrtc-sender.cc`

### 修改内容

#### 1. DeliveryPacket() - 恢复原始逻辑
**位置**: 第 343-372 行

**变化**:
```cpp
// ❌ 旧代码（错误）
double mu = GetCurrentBandwidthScaleFactor();
double base_interval_ms = 10.0;  // 硬编码！
Time interval = MilliSeconds(base_interval_ms / mu);
for (auto& packet : sendQ) {
    Simulator::ScheduleWithContext(m_context, accumulated_delay, ...);
    accumulated_delay += interval;
}

// ✅ 新代码（正确）
while(!sendQ.empty()){
    Ptr<Packet> packet = sendQ.front();
    sendQ.pop_front();
    SendToNetwork(packet);  // 立即发送
}
```

**原理**:
- 恢复到原始 GCC 的逻辑：立即发送包
- 由 WebRTC 内部的 Pacer 根据 BitrateConstraints 控制发送节奏
- 不再在此层添加人为间隔

#### 2. ApplyBandwidthScalingToController() - 实现 BitrateConstraints 缩放
**位置**: 第 252-306 行

**核心逻辑**:
```cpp
// 1. 获取 GCC 估计的带宽
uint32_t gcc_bandwidth = m_call->last_bandwidth_bps();

// 2. 计算缩放后的目标带宽
double current_mu = GetCurrentBandwidthScaleFactor();
uint32_t scaled_bandwidth = static_cast<uint32_t>(gcc_bandwidth * current_mu);

// 3. 创建带宽约束
webrtc::BitrateConstraints constraints;
constraints.min_bitrate_bps = 50000;        // 最小 50 kbps
constraints.start_bitrate_bps = scaled_bandwidth;
constraints.max_bitrate_bps = scaled_bandwidth;  // 关键：限制上限

// 4. 应用到 Transport Controller
transport_controller->SetSdpBitrateParameters(constraints);
```

**关键参数解释**:
- `min_bitrate_bps`: 最小带宽，保持低值允许自适应
- `start_bitrate_bps`: 初始带宽设置
- `max_bitrate_bps`: 最大带宽上限（**核心**），让 WebRTC 无法超过此限制

#### 3. StartApplication() - 更新调用时机
**位置**: 第 220-232 行

**变化**:
```cpp
// 延迟 2 秒调用，等待 GCC 有初始带宽估计
Simulator::Schedule(Time(MilliSeconds(2000)), 
                   &WebrtcSender::ApplyBandwidthScalingToController, this);
```

## 工作流程图

```
┌─────────────────────────────────────────────────────┐
│ WebRTC 编码器                                        │
└────────────────┬────────────────────────────────────┘
                 │
                 ▼
        ┌────────────────────┐
        │ GCC 拥塞控制       │
        │ (估计带宽)        │
        │ last_bandwidth_bps│
        └────────┬───────────┘
                 │ GCC带宽 (例如: 5 Mbps)
                 ▼
        ┌─────────────────────────────────────┐
        │ WebrtcSender::                      │
        │ ApplyBandwidthScalingToController() │
        │                                     │
        │ scaled_bw = GCC_bw × μ             │
        │ (例如: 5M × 0.8 = 4M)              │
        └────────┬────────────────────────────┘
                 │
                 ▼
        ┌──────────────────────────────┐
        │ SetSdpBitrateParameters()     │
        │ max_bitrate = scaled_bw      │
        └────────┬─────────────────────┘
                 │
                 ▼
        ┌──────────────────────┐
        │ WebRTC Pacer        │
        │ (内部节奏控制)      │
        │ 限制速率为 4 Mbps   │
        └────────┬────────────┘
                 │
                 ▼
        ┌──────────────────────────┐
        │ WebrtcSender::           │
        │ DeliveryPacket()         │
        │ (立即发送包)            │
        └────────┬─────────────────┘
                 │ 按 Pacer 确定的节奏发送
                 ▼
        ┌──────────────────────┐
        │ SendToNetwork()      │
        │ (发送包到网络)      │
        └──────────────────────┘
```

## 优势

| 方面 | 优势 |
|------|------|
| **与 WebRTC 协同** | ✅ 利用内置 Pacer，符合设计原理 |
| **编码器适配** | ✅ 编码器感知带宽限制，自动降低码率 |
| **丢包适应** | ✅ Pacer 会根据丢包调整速率 |
| **实现简洁** | ✅ 不需要复杂的时间间隔计算 |
| **RTT 适应** | ✅ Pacer 会考虑 RTT 调整发送节奏 |

## 验证方法

### 1. 编译验证
```bash
cd /home/hjt/OSCC/ns-allinone-3.31/ns-3.31
./waf build
```

### 2. 运行时验证
查看输出中的关键日志：
```
=== WebrtcSender: BitrateConstraints Applied (方案A) ===
Time: 2.0s
GCC Bandwidth: 5.0 Mbps
Bandwidth scale factor μ: 0.8
Scaled Bandwidth (max_bitrate): 4.0 Mbps
Min Bitrate: 50.0 kbps
```

### 3. 性能验证
- 检查 bandwidth_statistics.csv 中的实际发送速率是否符合 `GCC_bw × μ`
- 检查帧到达率是否保持稳定

## 兼容性

- ✅ 与现有的 `SetBandwidthScaleFactor(μ)` 接口兼容
- ✅ 与 `webrtc-TFMN(RTT).cc` 中的调用兼容
- ✅ 与 RL 状态管理器兼容

## 后续注意事项

1. **周期性更新**: 如果需要动态改变 μ 值，应该周期性调用 `ApplyBandwidthScalingToController()`
   ```cpp
   // 例如每 100ms 更新一次
   Simulator::Schedule(Time(MilliSeconds(100)), 
                      &WebrtcSender::ApplyBandwidthScalingToController, this);
   ```

2. **GCC 初始化**: 代码已处理 GCC 未初始化的情况，使用 2.5 Mbps 默认值

3. **最小/最大值约束**: 如需调整约束范围，修改以下常数：
   ```cpp
   constraints.min_bitrate_bps = 50000;   // 修改此值
   constraints.max_bitrate_bps = scaled_bandwidth;  // 此值由 μ 确定
   ```

## 相关文件

- Header: `/home/hjt/OSCC/ns-allinone-3.31/ns-3.31/src/ex-webrtc/model/webrtc-sender.h`
- Implementation: `/home/hjt/OSCC/ns-allinone-3.31/ns-3.31/src/ex-webrtc/model/webrtc-sender.cc`
- 使用文件: `/home/hjt/OSCC/ns-allinone-3.31/ns-3.31/scratch/webrtc-TFMN(RTT).cc`
- 批处理: `/home/hjt/OSCC/ns-allinone-3.31/ns-3.31/scratch/webrtc-FMN-TFMN(RTT).cc`

