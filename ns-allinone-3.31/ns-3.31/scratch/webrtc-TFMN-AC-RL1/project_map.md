# WebRTC TFMN (ns3-ai RL) 项目地图

> **用途**: 此文件为 AI 编程助手提供项目全局视图，避免逐文件阅读。修改代码前请先参考此文件定位目标模块。

## 项目概述

基于 ns-3.31 + **ns3-ai** 的 WebRTC 带宽优化仿真项目。Python 端使用 Stable-Baselines3 训练 RL 智能体，通过**共享内存**与 ns-3 交换观测/动作/奖励，动态调整带宽缩放因子 μ∈[0.5, 1.5]。

**命名空间**: 所有 C++ 代码在 `namespace oscc` 中。

## 文件结构与职责

```
webrtc-TFMN-AC-RL1/
├── claude.md               # [本文件] 项目地图
├── README.md               # 用户级项目概述
├── INSTALL.md              # 详细安装指南（含故障排除）
│
├── common_types.h          # 公共数据结构、常量、ShmEnv/ShmAction（ns3-ai 共享内存布局）
├── mu_learner.h            # IMuLearner 纯虚接口类
├── ai_mu_learner.h/cc      # AiMuLearner - ns3-ai 共享内存适配器（Ns3AIRL<ShmEnv,ShmAction>）
├── rl_state_manager.h/cc   # RLStateManager - RL 状态计算、奖励函数、包/Rt分组记录
├── network_components.h/cc # BandwidthChanger（trace驱动带宽）+ TriggerRandomLoss（丢包模型）
├── qoe_manager.h/cc        # QoEIntegrationManager - 集成所有组件的中枢
├── webrtc_trace.h/cc       # FrameAwareWebrtcTrace - 增强的 WebrtcTrace（带宽记录+统计输出）
├── simulation.h/cc         # 仿真编排（网络拓扑、组件初始化、仿真循环、结果输出）
├── main.cc                 # 程序入口（参数解析 → run_single_trace_simulation）
│
└── gym_agent/              # Python RL 智能体
    ├── train.py            # 训练脚本（PPO/SAC/TD3，via SB3）
    ├── ns3ai_env.py        # Ns3AiGymEnv - Gym 环境包装（py_interface.Ns3AIRL ↔ SB3）
    ├── requirements.txt    # Python 依赖（SB3, torch, gymnasium；py_interface 需单独安装）
    └── README.md           # Python 端使用说明
```

## 文件依赖关系

```
main.cc
  └─ simulation.h/cc
       ├─ common_types.h
       ├─ mu_learner.h
       ├─ ai_mu_learner.h/cc ──→ mu_learner.h, common_types.h, [ns3-ai]
       ├─ rl_state_manager.h/cc ──→ common_types.h, mu_learner.h
       ├─ network_components.h/cc ──→ common_types.h
       ├─ qoe_manager.h/cc ──→ rl_state_manager, network_components, mu_learner
       │                        [ns3::WebrtcSender, FramePlayoutManager]
       └─ webrtc_trace.h/cc ──→ qoe_manager, rl_state_manager, network_components
                                [ns3::WebrtcTrace (基类)]
```

**外部 ns-3 模块依赖**（在 `ns-3.31/src/` 或 `contrib/` 中）：
- `ex-webrtc-module`: WebrtcSender, WebrtcReceiver, WebrtcSessionManager
- `frame-playout-manager`: FramePlayoutManager, FramePacketInfo, FrameStatistics
- `webrtc-defines`: TimeConollerType, webrtc_register_clock, set_webrtc_trace_folder
- **`ns3-ai`** (src/ns3-ai): Ns3AIRL, SharedMemoryPool, ShmEnv/ShmAction 与 Python py_interface 共享内存通信

## 核心数据类型 (common_types.h)

```cpp
// 常量
const uint32_t DEFAULT_PACKET_SIZE = 1500;
const uint32_t kBwUnit = 1000000;  // 1 Mbps

// RL 状态输入（传给 MuLearner）
struct MuState { double rt; double loss; };

// RL 动作输出
struct MuAction { double mu; double log_prob; };

// 经验样本（Rt分组级别更新）
struct MuExperience { MuState state; MuAction action; double reward; uint32_t frame_id; uint32_t rt_value; };

// 学习器配置
struct MuLearnerConfig {
    double mu_min = 0.5, mu_max = 1.5;
    double rt_max = 20.0, loss_max = 0.1;
    double learning_rate = 0.01, baseline_decay = 0.95, grad_clip = 1.0;
    double exploration_sigma = 0.05;
    bool exploration_enabled = true;
};

// Trace 文件每行对应的数据
struct TraceData { Time timestamp; double bandwidth; double rtt; double loss; };

// 包级别状态记录（用于输出 CSV）
struct PacketStateRecord { frame_id, packet_index, mu_used, Rt, loss_rate, reward, ... };

// Rt分组奖励记录
struct RtGroupRewardRecord { frame_id, Rt_value, loss_rate, mu_used, avg_reward, packet_count, ... };

// 完整 RL 状态
struct RLState { mu, reward, bandwidth_utilization, p_delay, p_loss, p_mddl, ... };

// ns3-ai 共享内存结构（与 Python ctypes 对齐）
struct ShmEnv { float norm_rt; float norm_loss; float reward; uint8_t done; };
struct ShmAction { float mu; };
```

## 核心模块详解

### 1. IMuLearner (mu_learner.h) — 学习器接口

```cpp
class IMuLearner {
    virtual MuAction Act(const MuState& state) = 0;      // 推理：状态→动作
    virtual void Observe(const MuExperience& exp) = 0;    // 接收经验
    virtual void MaybeUpdate() = 0;                        // 触发更新
    virtual double CurrentMu() const = 0;
    virtual std::string GetStatusString() const = 0;
    virtual double GetThetaNorm() const = 0;
    virtual double GetBaseline() const = 0;
};
```

### 2. AiMuLearner (ai_mu_learner.h/cc) — ns3-ai 共享内存适配器

**依赖**: ns-3 模块 `ns3-ai`（src/ns3-ai），使用 `Ns3AIRL<ShmEnv, ShmAction, RLEmptyInfo>` 与 Python 通过共享内存通信。

**关键方法**:
- `Act(MuState)`: 将 state 写入 ShmEnv（norm_rt, norm_loss, 上步 reward/done），`SetCompleted()` 后 `ActionGetter()` 阻塞等待 Python 写入 ShmAction.mu，clip 到 [mu_min, mu_max] 返回
- `Observe(MuExperience)`: 更新 last_reward_/last_done_，下次 Act 时写入 ShmEnv
- `NotifySimulationEnd()`: 调用 `rl_->SetFinish()`，供仿真结束前显式调用（避免 _exit 导致 Python 端无法感知结束）

**共享内存规格**（与 Python ns3ai_env.py 中 ctypes 一致）:
- ShmEnv: norm_rt, norm_loss (float), reward (float), done (uint8)
- ShmAction: mu (float) ∈ [0.5, 1.5]
- 默认 shm_id=1234（单流）；多流时 1234+i

### 3. RLStateManager (rl_state_manager.h/cc) — 状态与奖励计算

**核心计算**:

**(a) 传输机会 Rt 计算** (`CalculateTransmissionOpportunities`):
```
T_remain = frame_deadline - current_time
packet_send_time = (packet_size * 8) / trace_bandwidth
available_time = T_remain - packet_send_time
Rt = floor(available_time / RTT)
```

**(b) 奖励函数** (`CalculateReward`):
```
reward = 2.5 * U - 10 * p_delay - 10 * p_loss - 10 * p_mddl

其中:
  U = clamp(mu_prev * gcc_bw / trace_bw, 0, 1)           -- 带宽利用率
  p_delay: 分段函数(delay_ms), 受 Rt 敏感度加权          -- 延迟惩罚
  p_loss = loss_rate / adaptive_tolerance(Rt, Lmax)       -- 丢包惩罚
  p_mddl = miss_deadline_time / ((ΔRt+1) * RTT)          -- 错过截止时间惩罚
```

**Rt 分组管理**: 每当 (frame_id, Rt) 变化时，FinalizeCurrentRtGroup() 会：
1. 计算组内平均奖励
2. 调用 `mu_learner_->Observe(exp)` 将经验发送给 Python（AiMuLearner 更新 last_reward_）
3. 调用 `mu_learner_->MaybeUpdate()`

**⚠️ 已知问题**: `OutputLearnerLog()` 中仍引用了 `BanditMuLearner` 和 `TorchMLPMuLearner`（已移除的旧组件），会导致编译错误，需要清理。

### 4. QoEIntegrationManager (qoe_manager.h/cc) — 集成中枢

**组件持有**: RLStateManager*, BandwidthChanger*, WebrtcSender*, IMuLearner*

**OnPacketReceived(FramePacketInfo, FrameStatistics) 实际流程**:
1. 计算实际延迟 delay_ms
2. 从 BandwidthChanger 获取 trace 网络信息（loss, RTT）
3. 更新 RLStateManager 网络状态
4. 获取平滑 GCC 带宽（滑动窗口 size=5）
5. 计算 Rt（调用 RLStateManager）
6. **若启用 MuLearner**: 调用 `mu_learner_->Act(MuState{Rt, loss})`（AiMuLearner 写 ShmEnv、读 ShmAction）获取 mu 并应用
7. 计算 reward（调用 RLStateManager.CalculateReward）
8. 记录包状态（触发 Rt 分组管理，间接调用 Observe 发送经验给 Python）

**注意**: Observe 不在此方法中直接调用，而是通过 RecordPacketState → AddPacketToRtGroup → FinalizeCurrentRtGroup 链式触发。

### 5. BandwidthChanger & TriggerRandomLoss (network_components.h/cc)

**BandwidthChanger**: 读取 4 列 trace 文件（time bandwidth rtt loss），按时间表切换 P2P 链路带宽。提供 `GetTraceBandwidthAtTime()`, `GetRTTAtTime()`, `GetLossAtTime()` 查询接口。

**TriggerRandomLoss**: 在接收端设备上配置 RateErrorModel。支持从 trace 动态更新丢包率（每100ms轮询 BandwidthChanger）。

### 6. FrameAwareWebrtcTrace (webrtc_trace.h/cc)

继承自 `ns3::WebrtcTrace`（基类在 ex-webrtc-module 中）。

**职责**: 拦截 GCC 带宽回调 (`OnBW`)，乘以 μ 得到 scaled 带宽，记录到 QoEIntegrationManager 的带宽历史中。输出带宽统计 CSV（10列：timestamp, trace_bw, gcc_bw, scaled_bw, mu 等）。

### 7. simulation.cc — 仿真编排

**核心函数**: `test_app_on_p2p()` 执行完整仿真：
1. 创建 2 节点 P2P 拓扑（带队列、流量控制）
2. 初始化 FramePlayoutManager、RLStateManager、**AiMuLearner**、QoEIntegrationManager
3. 创建 FrameAwareWebrtcTrace 并绑定回调
4. 安装 WebRTC 应用（sender + receiver）
5. 运行仿真
6. 结束前对每个 AiMuLearner 调用 `NotifySimulationEnd()`，再输出结果
7. 输出所有结果文件

**⚠️ 已知问题**: `InstallWebrtcApplication()` 中引用了 `qoe_manager->GetOSCCController()`，但 QoEIntegrationManager 头文件中无此方法，为旧代码残留。

## 数据流架构

```
                    ┌─────────────────── ns-3 Simulation (C++) ────────────────────┐
                    │                                                               │
Trace File ──→ BandwidthChanger ──→ P2P Link (带宽切换)                            │
                    │                                                               │
                    │   ┌───────── WebrtcSender/Receiver ─────────┐                │
                    │   │  GCC 估计带宽 → OnBW()                   │                │
                    │   │  包接收事件 → OnReceiptPktInfo()          │                │
                    │   └──────────────┬──────────────────────────┘                │
                    │                  │                                            │
                    │   FrameAwareWebrtcTrace                                      │
                    │     │ 记录 GCC 带宽 × μ → scaled_bw                          │
                    │     │ 更新 QoEManager 带宽历史                                │
                    │     ↓                                                        │
                    │   QoEIntegrationManager.OnPacketReceived()                   │
                    │     │ ① 计算 delay, 获取 trace 网络状态                       │
                    │     │ ② 计算 Rt                                              │
                    │     │ ③ AiMuLearner.Act({Rt,loss}) → 写 ShmEnv, 读 ShmAction  │
                    │     │ ④ 计算 reward                                           │
                    │     │ ⑤ RecordPacketState → Rt分组 → Observe(reward)          │
                    │     ↓                          ↕ 共享内存 (ns3-ai)           │
                    └────────────────────────────────┼─────────────────────────────┘
                                                     │
                                          ┌──────────▼──────────┐
                                          │  Python (SB3)       │
                                          │  Ns3AiGymEnv +      │
                                          │  PPO / SAC / TD3   │
                                          │  obs: [norm_Rt,    │
                                          │        norm_loss]  │
                                          │  action: [μ]       │
                                          │  reward: QoE score │
                                          └─────────────────────┘
```

## 命令行参数

### ns-3 仿真 (main.cc)

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `--trace=<file>` | trace 文件路径 | **必选** |
| `--mu=<value>` | 初始 μ 值 | 1.0 |
| `--ls=<value>` | 丢包率 | 0.01 |
| `--mb=<value>` | 最大带宽 (Mbps) | 10 |
| `--skip=<bool>` | 启用跳帧逻辑 | false |
| `--fps=<value>` | 帧率 | 30 |
| `--folder=<dir>` | 输出目录 | trace_results |
| `--it=<name>` | 实例名称 | default_instance |
| `--m=<mode>` | 仿真模式 (simu/emu) | simu |
| `--frame_trace=<file>` | 帧trace输出路径 | 自动生成 |

### Python 训练 (gym_agent/train.py)

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `--algorithm` | RL 算法 (PPO/SAC/TD3) | PPO |
| `--timesteps` | 训练步数 | 100000 |
| `--shm-id` | 共享内存块 id（需与 ns-3 AiMuLearner 一致） | 1234 |
| `--model-dir` | 模型保存目录 | ./models |
| `--log-dir` | TensorBoard 日志目录 | ./logs |
| `--load-model` | 加载已有模型继续训练 | None |

## 输出文件

仿真完成后在 `folder` 目录生成：

| 文件名模式 | 内容 |
|-----------|------|
| `*_RL_log.csv` | 包级 RL 状态（frame_id, Rt, mu, reward, delay 等14列） |
| `*_Frame-Rt-Reward.csv` | Rt 分组奖励（frame_id, Rt, mu, avg_reward 等8列） |
| `*_bandwidth_statistics.csv` | 带宽统计（trace_bw, gcc_bw, scaled_bw, mu 等10列） |
| `*_bandwidth_history.csv` | 带宽变化历史 |
| `*_learner_state.csv` | 学习器最终状态 |
| `*_frame_playout_trace.csv` | 帧播放 trace |

Python 端生成：`models/`（模型检查点）、`logs/`（TensorBoard 日志）

## 运行方式

### 前置：安装 ns3-ai（仅首次需要）

**C++ 端**：ns3-ai 已随本仓库放在 `ns-3.31/src/ns3-ai`（v1.0.0），构建 ns-3 时会自动编译。

```bash
cd /home/hjt/OSCC/ns-allinone-3.31/ns-3.31
./waf configure --enable-examples --enable-tests --disable-python
./waf build
```

**Python 端**（注意：waf 依赖 Python 3.8 的 `imp` 模块，训练脚本需 Python 3.12，所以用 `python3.12`/`pip3` 安装和运行）：

安装 py_interface（ns3-ai 的 Python 包，含共享内存 C 扩展）：

```bash
cd /home/hjt/OSCC/ns-allinone-3.31/ns-3.31/src/ns3-ai/py_interface
CC=clang pip3 install --user .   # 本机无 gcc，用 clang 编译 C 扩展
```

再安装 CPU 版 PyTorch 和 SB3 等：

```bash
# 先装 CPU-only PyTorch（~180MB，GPU 版 ~2GB+）
pip3 install torch --index-url https://download.pytorch.org/whl/cpu
# 再装其余依赖
pip3 install -r scratch/webrtc-TFMN-AC-RL1/gym_agent/requirements.txt
```

### 启动仿真与训练

```bash
# 终端1 - ns-3 仿真（先启动，等待 Python 通过共享内存连接）
cd /home/hjt/OSCC/ns-allinone-3.31/ns-3.31
nohup ./waf --run "scratch/webrtc-TFMN-AC-RL1/webrtc-TFMN-AC-RL1 \
  --trace=traces/traces/AItrans/AItrans_6.log \
  --skip=true --mu=1.0 --ls=0.01 \
  --folder=trace_results/ai_training --it=ai_test" \
  > /dev/null 2>&1 &

# 终端2 - Python 训练（后启动，shm_id 默认 1234）
# 注意：必须用 python3.12，因为 python3 是 3.8（给 waf 用），ML 包装在 3.12 下
cd scratch/webrtc-TFMN-AC-RL1/gym_agent
nohup python3.12 train.py --algorithm PPO --timesteps 500000 > train_output.log 2>&1 &

# 断点接训
nohup python3.12 train.py --algorithm PPO --timesteps 100000 --load-model ./models/PPO_webrtc_mu_20260306_164444_830000_steps.zip > train_output.log 2>&1 &
```



## Trace 文件格式

每行 4 列，空格分隔：`time(s) bandwidth(Mbps) rtt(ms) loss(0-1)`

```
0.0  10.0  30.0  0.01
0.1   9.5  32.0  0.02
```

## ⚠️ 已知问题 / 待清理

1. **rl_state_manager.cc:406-428** — `OutputLearnerLog()` 中 `dynamic_cast<BanditMuLearner*>` 和 `TorchMLPMuLearner*` 引用了已移除的旧类，需要清理或用 `#ifdef` 保护
2. **simulation.cc:148-153** — `qoe_manager->GetOSCCController()` 调用了 QoEIntegrationManager 未定义的方法，为旧代码残留
3. **main.cc:20-22** — `std::cout` 被重定向到 `webrtc_simulation.log` 文件，所有 cout 输出不会显示在终端
4. **simulation.cc** — 仿真结束前已对 AiMuLearner 调用 `NotifySimulationEnd()`，再 `_exit(0)`，以便 Python 端能检测结束并释放共享内存
