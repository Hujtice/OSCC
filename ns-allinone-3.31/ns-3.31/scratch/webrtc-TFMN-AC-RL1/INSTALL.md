# 安装指南（ns3-ai 版）

## 系统要求

- **操作系统**: Linux (Ubuntu 18.04+ 推荐)
- **ns-3 版本**: 3.31
- **Python**: 3.10+
- **编译器**: g++ 7+ 或 clang++ 6+ (支持 C++11)

## 第一步：ns-3 与 ns3-ai

本仓库已在 `src/ns3-ai` 中包含 ns3-ai v1.0.0，无需单独克隆。

```bash
cd /path/to/ns-3.31
./waf configure --enable-examples --enable-tests --disable-python
./waf build
```

**验证**：构建输出中应包含 `ns3-ai (no Python)`。

## 第二步：Python 依赖

### 2.1 安装 ns3-ai 的 py_interface（必须）

py_interface 提供共享内存 C 扩展 `shm_pool` 和 Python 封装 `py_interface`。

```bash
cd /path/to/ns-3.31/src/ns3-ai/py_interface
pip3 install --user .
```

若编译报错（如 `gcc` 未找到），可指定编译器：

```bash
CC=clang pip3 install --user .
# 或安装 gcc: sudo apt install build-essential
```

**验证**：

```bash
python3.12 -c "import py_interface; py_interface.Init(1234, 4096); py_interface.FreeMemory(); print('py_interface OK')"
```

> **注意**：`python3` 指向 Python 3.8（waf 构建系统需要），ML 包安装在 Python 3.12 下，
> 因此训练相关命令统一用 `python3.12`。`pip3` 已绑定 Python 3.12，直接使用即可。

### 2.2 安装 PyTorch (CPU) 和 SB3 等

```bash
# 先安装 CPU-only PyTorch（~180MB，避免下载 ~2GB+ 的 GPU 版）
pip3 install torch --index-url https://download.pytorch.org/whl/cpu

# 再安装其余依赖
cd /path/to/ns-3.31/scratch/webrtc-TFMN-AC-RL1/gym_agent
pip3 install -r requirements.txt
```

**验证**：

```bash
python3.12 -c "from ns3ai_env import Ns3AiGymEnv; print('Ns3AiGymEnv OK')"
```

（若未安装 py_interface，会提示先安装。）

## 第三步：准备 Trace 文件

确保有可用的网络 trace 文件，例如：

```bash
ls /path/to/ns-3.31/traces/traces/AItrans/AItrans_1.log
```

格式（每行 4 列，空格分隔）：`time(s)  bandwidth(Mbps)  rtt(ms)  loss(0-1)`。

## 第四步：测试运行

### 测试 1：仅启动 ns-3

```bash
cd /path/to/ns-3.31
./waf --run "scratch/webrtc-TFMN-AC-RL1/webrtc-TFMN-AC-RL1 \
  --trace=traces/traces/AItrans/AItrans_1.log \
  --mu=1.0 --ls=0.01 --folder=trace_results/test --it=test1"
```

**预期**：输出中出现 `AiMuLearner Initialized (ns3-ai)`，仿真会等待 Python 连接（第一次 Act 会阻塞在共享内存上）。

### 测试 2：完整训练流程

**终端 1**：

```bash
cd /path/to/ns-3.31
./waf --run "scratch/webrtc-TFMN-AC-RL1/webrtc-TFMN-AC-RL1 \
  --trace=traces/traces/AItrans/AItrans_1.log \
  --skip=true --mu=1.0 --ls=0.01 \
  --folder=trace_results/ai_test --it=ai1"
```

**终端 2**（在终端 1 启动后）：

```bash
cd /path/to/ns-3.31/scratch/webrtc-TFMN-AC-RL1/gym_agent
python3.12 train.py --algorithm PPO --timesteps 10000
```

**预期**：两端正常交换数据，Python 端有 step 与 reward 输出。

## 故障排除

### 问题 1：py_interface 安装失败（gcc 未找到）

**解决**：指定 CC 为 clang 或安装 build-essential：

```bash
CC=clang pip3 install --user .  # 在 py_interface 目录下
# 或安装 gcc:
sudo apt install build-essential
```

### 问题 2：Python 报 `ModuleNotFoundError: No module named 'py_interface'`

**解决**：确保已执行 `pip3 install --user .`（在 `src/ns3-ai/py_interface` 下），且当前 Python 能找到该包（`python3 -c "import sys; print(sys.path)"`）。

### 问题 3：Python 报 `No module named 'shm_pool'`

**解决**：py_interface 依赖 C 扩展 shm_pool，必须从 `src/ns3-ai/py_interface` 完整安装（`pip3 install --user .`），不能只复制 py_interface.py。

### 问题 4：仿真结束后 Python 不退出 / 共享内存残留

**解决**：仿真结束前会调用 `NotifySimulationEnd()`。若异常退出，可手动清理共享内存：

```bash
# 使用 ns3-ai 提供的脚本（若存在）
/path/to/ns-3.31/src/ns3-ai/freeshm.sh
# 或
ipcrm -M 0x4d2   # 1234 的十六进制，即默认 shm key
```

### 问题 5：多流（num>1）时 Python 只连一个

**说明**：当前默认单流，shm_id=1234。若仿真中创建多个 AiMuLearner（1234, 1235, ...），需多进程或多 env 对应多个 shm_id，或仅使用第一个流进行训练。

## 下一步

- [README.md](README.md) - 项目概述  
- [claude.md](claude.md) - 完整技术文档与数据流  
- [gym_agent/README.md](gym_agent/README.md) - Python 端参数与用法  

监控训练：`tensorboard --logdir gym_agent/logs`，访问 http://localhost:6006 。
