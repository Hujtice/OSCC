# 安装指南

## 系统要求

- **操作系统**: Linux (Ubuntu 18.04+推荐)
- **ns-3 版本**: 3.31
- **Python**: 3.7+
- **编译器**: g++ 7+ (支持 C++14)

## 第一步：安装 ns3-gym

ns3-gym 是连接 ns-3 (C++) 和 Python RL 算法的关键中间件。

```bash
# 进入 ns-3.31 的 contrib 目录
cd /home/hjt/OSCC/ns-allinone-3.31/ns-3.31/contrib

# 克隆 ns3-gym
git clone https://github.com/tkn-tub/ns3-gym.git opengym

# 返回 ns-3.31 根目录
cd ../..

# 重新配置和编译 ns-3
./waf configure --enable-examples --enable-tests
./waf build
```

**验证安装**:
```bash
# 检查目录是否存在
ls contrib/opengym

# 应该看到：
# model/  examples/  doc/  ...
```

## 第二步：安装 Python 依赖

```bash
# 进入 gym_agent 目录
cd scratch/webrtc-TFMN-AC-RL1/gym_agent

# 安装依赖
pip install -r requirements.txt

# 或手动安装
pip install gym==0.21.0 stable-baselines3 torch tensorboard protobuf==3.20.1 pyzmq
```

**验证安装**:
```bash
# 测试导入
python3 -c "import gym; import stable_baselines3; import ns3gym; print('All dependencies OK!')"
```

## 第三步：编译项目

```bash
# 返回 ns-3.31 根目录
cd /home/hjt/OSCC/ns-allinone-3.31/ns-3.31

# 编译项目
./waf build
```

**注意**：
- 如果看到 `NS3_OPENGYM` 相关的警告，说明 ns3-gym 可能未正确安装
- gym_mu_learner.cc 中有条件编译，会在没有 ns3-gym 时给出友好错误提示

## 第四步：准备 Trace 文件

确保有可用的网络 trace 文件：

```bash
ls /home/hjt/OSCC/ns-allinone-3.31/ns-3.31/traces/traces/AItrans/AItrans_1.log
```

Trace 文件格式（每行4列）：
```
time(s)  bandwidth(Mbps)  rtt(ms)  loss(0-1)
0.0      10.0             30.0     0.01
0.1      9.5              32.0     0.02
...
```

## 第五步：测试运行

### 测试1：只启动 ns-3 仿真

```bash
cd /home/hjt/OSCC/ns-allinone-3.31/ns-3.31
./waf --run "scratch/webrtc-TFMN-AC-RL1/webrtc-TFMN-AC-RL1 \
  --trace=traces/traces/AItrans/AItrans_1.log \
  --mu=1.0 --ls=0.01 --folder=trace_results/test --it=test1"
```

**预期输出**:
```
=== WebRTC TraceAll-Frame Starting ===
...
GymMuLearner initialized (awaiting Python agent connection)
...
```

仿真会等待 Python 智能体连接。

### 测试2：完整训练流程

**终端 1**:
```bash
cd /home/hjt/OSCC/ns-allinone-3.31/ns-3.31
./waf --run "scratch/webrtc-TFMN-AC-RL1/webrtc-TFMN-AC-RL1 \
  --trace=traces/traces/AItrans/AItrans_1.log \
  --skip=true --mu=1.0 --ls=0.01 \
  --folder=trace_results/gym_test --it=gym1"
```

**终端 2** (在终端1启动后):
```bash
cd /home/hjt/OSCC/ns-allinone-3.31/ns-3.31/scratch/webrtc-TFMN-AC-RL1/gym_agent
python3 train.py --algorithm PPO --timesteps 10000
```

**预期输出** (终端2):
```
==============================
WebRTC MU Learning - Training Session
==============================
Algorithm:        PPO
Total timesteps:  10000
...
[INFO] Connected to ns-3 simulation on port 5555
...
```

## 故障排除

### 问题1：ns3-gym 编译失败

**症状**: `waf build` 时出现 opengym 相关错误

**解决**:
```bash
# 检查 protobuf 版本
protoc --version  # 应该是 3.x

# 如果版本不对，重新安装
pip install protobuf==3.20.1
```

### 问题2：Python 无法导入 ns3gym

**症状**: `ModuleNotFoundError: No module named 'ns3gym'`

**解决**:
```bash
# 添加到 PYTHONPATH
export PYTHONPATH="/home/hjt/OSCC/ns-allinone-3.31/ns-3.31/contrib/opengym/model/ns3gym:$PYTHONPATH"

# 或写入 ~/.bashrc
echo 'export PYTHONPATH="/home/hjt/OSCC/ns-allinone-3.31/ns-3.31/contrib/opengym/model/ns3gym:$PYTHONPATH"' >> ~/.bashrc
source ~/.bashrc
```

### 问题3：连接超时

**症状**: `Failed to connect to ns-3 simulation`

**解决**:
1. 确保先启动 ns-3 仿真（终端1）
2. 再启动 Python 脚本（终端2）
3. 检查端口占用：`lsof -i:5555`
4. 如果端口被占用，可以用 `--port` 参数换一个端口

### 问题4：Gym 版本冲突

**症状**: `gym.error.NameNotFound: Environment 'Ns3-v0' not found`

**解决**:
```bash
# 确保使用正确的 gym 版本
pip install gym==0.21.0 --force-reinstall
```

## 下一步

安装完成后，请阅读：
- [README.md](README.md) - 项目概述
- [claude.md](claude.md) - 完整技术文档
- [gym_agent/README.md](gym_agent/README.md) - Python 端详细说明

开始训练：
```bash
# 终端1：ns-3
./waf --run "scratch/webrtc-TFMN-AC-RL1/webrtc-TFMN-AC-RL1 --trace=<your_trace> ..."

# 终端2：Python
cd scratch/webrtc-TFMN-AC-RL1/gym_agent
python3 train.py --algorithm PPO --timesteps 100000
```

监控训练进度：
```bash
tensorboard --logdir ./logs
# 访问 http://localhost:6006
```
