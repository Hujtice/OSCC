# WebRTC Bandwidth Optimization with RL (ns3-ai)

基于 ns-3 和强化学习的 WebRTC 带宽优化项目，使用 Python (Stable-Baselines3 PPO) 通过 **ns3-ai 共享内存** 与仿真交互，动态学习最优带宽缩放因子 μ∈{0.8, 0.9, 1.0, 1.1, 1.2}。

## 快速开始

### 1. 安装依赖

**ns-3 与 ns3-ai**（本仓库已包含 `src/ns3-ai`）：

```bash
cd /path/to/ns-3.31
./waf configure --enable-examples --enable-tests --disable-python
./waf build
```

**Python**：安装 ns3-ai 的 py_interface 与 SB3：

```bash
cd /path/to/ns-3.31/src/ns3-ai/py_interface
pip install --user .

cd /path/to/ns-3.31/scratch/webrtc-TFMN-AC-RL1/gym_agent
pip install -r requirements.txt
```

### 2. 运行训练

**终端 1 - ns-3 仿真**（先启动）：

```bash
cd /path/to/ns-3.31
./waf --run "scratch/webrtc-TFMN-AC-RL1/webrtc-TFMN-AC-RL1 \
  --trace=traces/traces/AItrans/AItrans_1.log \
  --skip=true --mu=1.0 --ls=0.01 \
  --folder=trace_results/ai_training --it=ai_test"
```

**终端 2 - Python 智能体**（后启动）：

```bash
cd scratch/webrtc-TFMN-AC-RL1/gym_agent
python3 train.py --algorithm PPO --timesteps 100000
```

## 架构

- **C++ (ns-3)**: 网络仿真 + **AiMuLearner**（ns3-ai 共享内存）
- **Python (SB3)**: Ns3AiGymEnv + PPO/SAC/TD3
- **ns3-ai**: 共享内存通信（无 ZMQ/Protobuf 依赖）

## RL 与奖励（Rt 组级）

- **状态**：`[norm_Rt, norm_loss]`，其中 `norm_loss` 为离散 5 级（L0-L4，阈值 1%/3%/6%/10%）。
- **动作**：`Discrete(5)` → μ∈{0.8, 0.9, 1.0, 1.1, 1.2}。仅 PPO 可用（SAC/TD3 不兼容离散动作）。
- **同一 (frame_id, Rt) 为一组**：组内所有包使用**同一个 μ**；仅在进入新组时调用一次 `Act()` 向 Python 要动作，训练步数与 Rt 组数同量级。
- **观测 loss**：仍来自 **50 包 seq 滑窗**，经 `DiscretizeLossLevel()` 离散化后归一化写入共享内存。
- **送给智能体的 reward**：在组结束时用组内累计的 **seq 实际丢包率**重算丢包惩罚项，再与组内平均带宽利用率、延迟惩罚、截止时间惩罚合成。
- **离散化配置**：Loss 等级阈值和 μ 动作表均可调，详见 [project_map.md](project_map.md) 中「状态/动作空间离散化配置」章节。

## 文档

- [project_map.md](project_map.md) - 完整项目地图（供 AI 与开发者）
- [INSTALL.md](INSTALL.md) - 详细安装与故障排除
- [gym_agent/README.md](gym_agent/README.md) - Python 端说明

## 特点

- 无 ZMQ/Protobuf，安装更简单  
- 共享内存低延迟，训练更快  
- 离散状态/动作空间，PPO 训练（SAC/TD3 保留选项但不兼容离散动作）  
- TensorBoard 可视化，与 Stable-Baselines3 生态兼容  

## License

本项目遵循 ns-3 相同的开源协议。
