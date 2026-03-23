# WebRTC MU Learning - Python Agent (ns3-ai)

Python 端强化学习智能体，通过 **ns3-ai 共享内存** 与 ns-3 仿真交互，优化 WebRTC 带宽缩放因子 μ∈{0.8, 0.9, 1.0, 1.1, 1.2}（离散 5 值）。

## 依赖

### 1. 安装 py_interface（ns3-ai Python 包）

```bash
cd /path/to/ns-3.31/src/ns3-ai/py_interface
pip3 install --user .
```

### 2. 安装 Python 依赖

```bash
cd scratch/webrtc-TFMN-AC-RL1/gym_agent
pip3 install -r requirements.txt
```

## 快速开始

**终端 1**：先启动 ns-3 仿真。

**终端 2**：再启动训练：

```bash
cd scratch/webrtc-TFMN-AC-RL1/gym_agent
python3 train.py --algorithm PPO --timesteps 100000
```

### 训练参数

- `--algorithm`: 算法 (PPO 推荐；SAC/TD3 不兼容离散动作，选择时报错)，默认 PPO
- `--timesteps`: 总步数，默认 100000
- `--shm-id`: 共享内存块 id，需与 ns-3 AiMuLearner 一致，默认 1234
- `--model-dir`: 模型保存目录，默认 ./models
- `--log-dir`: TensorBoard 日志目录，默认 ./logs
- `--load-model`: 从检查点继续训练

### 示例

```bash
python3 train.py --algorithm SAC --timesteps 200000
python3 train.py --load-model ./models/PPO_webrtc_mu_xxx_final.zip
```

## 环境规格

- **Observation**: Box(shape=(2,), low=0, high=1) — [norm_Rt, norm_loss]，其中 norm_loss 为离散 5 级（L0=0.0, L1=0.25, L2=0.5, L3=0.75, L4=1.0）
- **Action**: Discrete(5) — 索引 0-4 对应 μ∈{0.8, 0.9, 1.0, 1.1, 1.2}
- **Reward**: C++ 在上一 **Rt 组**结束时写入 ShmEnv；组级 QoE（reward 中丢包项用该组 **seq 累计实际丢包率** 重算，与逐包日志可能不一致）
- **步频说明**: ns-3 仅在每个 **新 (frame_id, Rt) 组** 调用一次 `Act()`，因此 SB3 的 `timesteps` 更接近「Rt 组数」而非「应用层包数」
- **离散化配置**: Loss 等级阈值和 μ 动作表均可调，详见 [../project_map.md](../project_map.md) 中「状态/动作空间离散化配置」章节

## 监控

```bash
tensorboard --logdir ./logs
# 浏览器打开 http://localhost:6006
```

## 故障排除

- **ModuleNotFoundError: py_interface**：在 `src/ns3-ai/py_interface` 下执行 `pip3 install --user .`
- **No module named 'shm_pool'**：需完整安装 py_interface（含 C 扩展），不能只复制 py_interface.py
- 仿真先启动、Python 后启动；shm_id 与 ns-3 端一致（默认 1234）

## 文件

- `train.py`: 训练入口
- `ns3ai_env.py`: Ns3AiGymEnv（Gym 包装 py_interface.Ns3AIRL）
- `requirements.txt`: 依赖列表
