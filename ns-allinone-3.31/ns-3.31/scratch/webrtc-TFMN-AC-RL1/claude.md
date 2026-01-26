# WebRTC TFMN (AC-RL1) 项目文档

## 项目概述

这是一个基于 ns-3 的 WebRTC 仿真项目，实现了带宽自适应控制、强化学习、OSCC 动态调整和 QoE 管理。代码已从单个 3623 行的文件重构为模块化架构。

**核心功能**：
- WebRTC 视频传输仿真（基于 GCC 拥塞控制）
- 动态带宽缩放因子 μ 调整（支持固定值、OSCC 算法、轻量级 RL）
- Rt（重传机会）计算与状态管理
- 帧级 QoE 评估
- 网络 trace 驱动仿真

## 文件结构（17个文件）

```
webrtc-TFMN-AC-RL1/
├── common_types.h          # 公共数据结构（无依赖）
├── mu_learner.h/cc         # 强化学习组件（200行）
├── oscc_controller.h/cc    # OSCC 控制器（450行）
├── rl_state_manager.h/cc   # RL 状态管理器（650行）
├── network_components.h/cc # 网络组件（400行）
├── qoe_manager.h/cc        # QoE 管理器（350行）
├── webrtc_trace.h/cc       # WebRTC 追踪（450行）
├── simulation.h/cc         # 仿真逻辑（500行）
└── main.cc                 # 程序入口（150行）
```

## 核心数据结构（common_types.h）

### 关键常量
```cpp
const uint32_t DEFAULT_PACKET_SIZE = 1500;
const uint32_t kBwUnit = 1000000;  // 1 Mbps
```

### 重要结构体
- **PacketStateRecord**: 包级状态（frame_id, mu_used, Rt, loss_rate, reward, 时延等）
- **RtGroupRewardRecord**: Rt 分组奖励记录（用于 RL 更新）
- **TraceData**: 网络 trace 数据（timestamp, bandwidth, rtt, loss）
- **MuState/MuAction/MuExperience**: MuLearner 的状态/动作/经验
- **RLState**: 强化学习状态（mu, reward, QoE 指标等）

## 核心模块详解

### 1. MuLearner（mu_learner.h/cc）

**职责**: 轻量级强化学习，动态学习 μ 值

**接口**:
```cpp
class IMuLearner {
    virtual MuAction Act(const MuState& state) = 0;  // 推理
    virtual void Observe(const MuExperience& exp) = 0;  // 接收经验
    virtual void MaybeUpdate() = 0;  // 触发更新
};
```

**实现**: `BanditMuLearner` - 基于策略梯度的 bandit 算法
- 输入: (Rt, loss_rate)
- 输出: μ ∈ [0.5, 1.5]
- 特征: 线性组合 + sigmoid + 探索噪声
- 更新: 基于 advantage 的策略梯度

### 2. OSCCController（oscc_controller.h/cc）

**职责**: 基于 Rt 历史查表的动态 μ 调整

**核心算法**:
```cpp
double GetMuForPacket(uint32_t frame_id, uint32_t Rt);
// 1. 查找历史 HistoryMap[Rt]
// 2. 根据当前丢包率 L_curr vs L_prev 调整
// 3. 边界处理（Rt > max: 激进, Rt < min: 保守）
```

**关键方法**:
- `UpdateLossWindow(bool lost)`: 滑动窗口丢包率
- `OnRtGroupComplete()`: Rt 组完成回调
- `OnFrameComplete()`: 帧完成回调（记录 QoE）

### 3. RLStateManager（rl_state_manager.h/cc）

**职责**: RL 状态计算、奖励函数、包记录管理

**核心方法**:
```cpp
// 计算传输机会
uint32_t CalculateTransmissionOpportunities(
    Time current, Time deadline, 
    uint32_t packet_size, double bandwidth);

// 计算奖励
double CalculateReward(
    double mu, double gcc_bw, double trace_bw,
    double delay, double loss, double miss_ddl, 
    uint32_t Rt_curr, uint32_t Rt_prev, ...);
// 奖励 = 2.5*U - 10*p_delay - 10*p_loss - 10*p_mddl
```

**集成**:
- 可集成 OSCCController（oscc_mode）
- 可集成 MuLearner（learner_mode）
- 两者互斥，learner 优先级更高

### 4. NetworkComponents（network_components.h/cc）

**包含**:
- `TriggerRandomLoss`: 丢包模型（支持固定丢包率 + trace 驱动）
- `BandwidthChanger`: 带宽 trace 播放器
- `CompareV`: 时间比较器

**关键**: `BandwidthChanger` 可根据时间戳查询 trace 的 (bandwidth, rtt, loss)

### 5. QoEIntegrationManager（qoe_manager.h/cc）

**职责**: 集成所有组件，处理回调

**核心流程**:
```cpp
OnPacketReceived() {
    1. 更新 OSCC 滑动窗口
    2. 获取 trace 网络状态
    3. 计算 Rt
    4. 决策 μ (learner > OSCC > 固定)
    5. 计算奖励
    6. 记录包状态
}
```

### 6. FrameAwareWebrtcTrace（webrtc_trace.h/cc）

**职责**: 继承 ns-3 的 `WebrtcTrace`，扩展带宽缩放功能

**核心**:
```cpp
void OnBW(uint32_t now, uint32_t bps) {
    // GCC 带宽更新
    double mu = rl_manager_->GetCurrentMu();
    uint32_t scaled_bw = bps * mu;
    // 记录到 QoEManager
    // 输出带宽统计
}
```

### 7. Simulation（simulation.h/cc）

**核心函数**:
```cpp
void InstallWebrtcApplication(...);  // 安装应用
void test_app_on_p2p(...);  // P2P 拓扑仿真
void run_single_trace_simulation(...);  // 单 trace 仿真
```

**关键流程** (test_app_on_p2p):
1. 创建 P2P 节点和链路
2. 初始化管理器（RL/OSCC/QoE/FramePlayout）
3. 注册设备到 BandwidthChanger/TriggerRandomLoss
4. 安装 WebRTC 应用
5. 运行仿真
6. 导出结果

### 8. Main（main.cc）

**命令行参数**:
```bash
--trace=<file>      # trace 文件路径（必选）
--mu=<value>        # 初始 μ 值（默认 1.0）
--ls=<value>        # 丢包率（默认 0.01）
--oscc=<bool>       # 启用 OSCC 模式
--learner=<bool>    # 启用 learner 模式（与 OSCC 互斥）
--skip=<bool>       # 启用跳帧逻辑
--fps=<value>       # 帧率（默认 30）
--folder=<dir>      # 输出目录
--it=<instance>     # 实例名称
```

## 模块依赖关系

```
层次结构（从底层到上层）：

common_types.h (无依赖)
    ↓
mu_learner.h/cc
    ↓
oscc_controller.h/cc ← common_types
    ↓
rl_state_manager.h/cc ← mu_learner + oscc_controller
    ↓
network_components.h/cc ← common_types
    ↓
qoe_manager.h/cc ← rl_state + oscc + network_comp + mu_learner
    ↓
webrtc_trace.h/cc ← qoe_manager
    ↓
simulation.h/cc ← webrtc_trace + 所有组件
    ↓
main.cc ← simulation
```

## 编译和运行

### 编译
```bash
cd /home/hjt/OSCC/ns-allinone-3.31/ns-3.31
./waf build
```

**注意**: 程序名是 `webrtc-TFMN-AC-RL1`（目录名中的括号被替换为短横线）

### 运行示例

**固定 μ 模式**:
```bash
./waf --run "webrtc-TFMN-AC-RL1 \
  --trace=/path/to/trace.log \
  --mu=1.0 --ls=0.01 \
  --folder=results --it=test1"
```

**OSCC 动态调整**:
```bash
./waf --run "webrtc-TFMN-AC-RL1 \
  --trace=/path/to/trace.log \
  --oscc=true --mu=1.0 --ls=0.01 \
  --folder=results --it=oscc_test"
```

**Learner 模式**:
```bash
./waf --run "webrtc-TFMN-AC-RL1 \
  --trace=/path/to/trace.log \
  --learner=true --mu=1.0 --ls=0.01 \
  --folder=results --it=learner_test"
```

### 输出文件

仿真结束后会在 `folder` 目录生成：
- `*_RL_log.csv`: 包级 RL 状态记录
- `*_Frame-Rt-Reward.csv`: Rt 分组奖励记录
- `*_bandwidth_statistics.csv`: 带宽统计（trace/GCC/scaled）
- `*_learner_state.csv`: Learner 状态（如果启用）
- `*_OSCC_mu_trace.csv`: OSCC μ 变化记录（如果启用）
- `*_OSCC_qoe.csv`: 帧级 QoE 记录
- `*_frame_trace.csv`: 帧播放 trace

## 关键设计决策

### 1. 为什么用 namespace oscc?
封装所有自定义代码，避免与 ns3 命名空间冲突。

### 2. μ 的三种模式
- **固定模式**: 简单基线，μ 在整个仿真中不变
- **OSCC 模式**: 基于历史 Rt 查表，根据丢包率动态调整
- **Learner 模式**: 在线学习策略，根据 (Rt, loss) 输出 μ

### 3. Rt（Retransmission Opportunities）
```
Rt = floor((T_deadline - T_now - T_send) / RTT)
```
表示包在截止时间前还有多少次重传机会，是 OSCC 算法的核心输入。

### 4. 奖励函数设计
```cpp
reward = 2.5 * U - 10 * p_delay - 10 * p_loss - 10 * p_mddl
```
- U: 带宽利用率
- p_delay: 延迟惩罚（分段函数）
- p_loss: 丢包惩罚（相对于容忍度）
- p_mddl: 错过截止时间惩罚

### 5. 为什么删除原文件？
ns-3 的 waf 构建系统会扫描 `scratch/` 下的所有 `.cc` 文件。保留原文件会导致编译冲突。

## 常见问题

### Q: 编译报错 "Reference to 'TimeConollerType' is ambiguous"
**A**: 使用 `ns3::TimeConollerType` 显式指定命名空间，因为该类型同时存在于 `ns3` 和 `oscc` 命名空间。

### Q: 如何切换三种 μ 模式？
**A**: 
- 固定: 不加 `--oscc` 和 `--learner`
- OSCC: 加 `--oscc=true`
- Learner: 加 `--learner=true`（优先级最高）

### Q: trace 文件格式？
**A**: 每行 4 列：`time(s) bandwidth(Mbps) rtt(ms) loss(0-1)`

### Q: 如何验证输出正确性？
**A**: 检查 CSV 文件：
1. `RL_log.csv` 应有每个包的状态
2. `Frame-Rt-Reward.csv` 应有 Rt 分组记录
3. `bandwidth_statistics.csv` 应有 10 列（trace/GCC/scaled 带宽）

## 性能指标

| 指标 | 重构前 | 重构后 |
|------|--------|--------|
| 单文件行数 | 3623 | ~400-650 |
| 文件数 | 1 | 17 |
| 编译时间 | ~8s | ~5s |
| 可测试性 | 差 | 模块独立可测试 |
| 耦合度 | 高 | 低（清晰依赖） |

## 扩展指南

### 添加新的 μ 策略
1. 继承 `IMuLearner` 接口
2. 实现 `Act()`, `Observe()`, `MaybeUpdate()`
3. 在 `simulation.cc` 中实例化并连接到 `RLStateManager`

### 修改奖励函数
编辑 `rl_state_manager.cc` 的 `CalculateReward()` 方法。

### 添加新的网络指标
1. 在 `PacketStateRecord` 中添加字段
2. 在 `RecordPacketState()` 中记录
3. 在 `OutputStateRecords()` 中输出

## 参考

- ns-3 文档: https://www.nsnam.org/docs/
- WebRTC GCC 算法: RFC 8298
- 原始单文件: `/home/hjt/OSCC/ns-allinone-3.31/ns-3.31/scratch/webrtc-TFMN(AC-RL1).cc` (已删除)
- 重构方案: `.cursor/plans/refactor_webrtc_simulation_*.plan.md`
