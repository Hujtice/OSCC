# WebRTC Bandwidth Optimization with RL (ns3-gym)

基于 ns-3 和强化学习的 WebRTC 带宽优化项目，使用 Python (Stable-Baselines3) 动态学习最优带宽缩放因子 μ。

## 快速开始

### 1. 安装依赖

```bash
# 安装 ns3-gym
cd /home/hjt/OSCC/ns-allinone-3.31/ns-3.31/contrib
git clone https://github.com/tkn-tub/ns3-gym.git opengym
cd ../../
./waf configure --enable-examples --enable-tests
./waf build

# 安装 Python 依赖
cd scratch/webrtc-TFMN-AC-RL1/gym_agent
pip install -r requirements.txt
```

### 2. 运行训练

**终端 1 - ns-3 仿真**:
```bash
cd /home/hjt/OSCC/ns-allinone-3.31/ns-3.31
./waf --run "scratch/webrtc-TFMN-AC-RL1/webrtc-TFMN-AC-RL1 \
  --trace=traces/traces/AItrans/AItrans_1.log \
  --skip=true --mu=1.0 --ls=0.01 \
  --folder=trace_results/gym_training --it=gym_test"
```

**终端 2 - Python 智能体**:
```bash
cd scratch/webrtc-TFMN-AC-RL1/gym_agent
python3 train.py --algorithm PPO --timesteps 100000
```

## 架构

- **C++ (ns-3)**: 网络仿真环境 + GymMuLearner 适配器
- **Python (SB3)**: RL 算法（PPO/SAC/TD3）
- **ns3-gym**: C++/Python 通信桥梁（ZMQ/Protobuf）

## 文档

- [claude.md](claude.md) - 完整项目文档
- [gym_agent/README.md](gym_agent/README.md) - Python 端文档

## 特点

✅ 快速算法迭代（Python 无需重新编译 ns-3）  
✅ 支持多种 RL 算法（PPO/SAC/TD3/等）  
✅ TensorBoard 可视化训练过程  
✅ 成熟的 RL 生态系统（Stable-Baselines3）

## License

本项目遵循 ns-3 相同的开源协议。
