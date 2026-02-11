# WebRTC MU Learning - Python Agent (ns3-ai)

Python 端强化学习智能体，通过 **ns3-ai 共享内存** 与 ns-3 仿真交互，优化 WebRTC 带宽缩放因子 μ。

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

- `--algorithm`: 算法 (PPO, SAC, TD3)，默认 PPO
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

- **Observation**: Box(shape=(2,), low=0, high=1) — [norm_Rt, norm_loss]
- **Action**: Box(shape=(1,), low=0.5, high=1.5) — [μ]
- **Reward**: QoE 综合指标（带宽利用率、延迟/丢包/截止时间惩罚）

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
