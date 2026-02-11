# WebRTC MU Learning - Gym Agent

Python-based reinforcement learning agent for optimizing WebRTC bandwidth scaling factor (μ) using ns3-gym.

## Prerequisites

### 1. Install ns3-gym

```bash
cd /home/hjt/OSCC/ns-allinone-3.31/ns-3.31/contrib
git clone https://github.com/tkn-tub/ns3-gym.git opengym
cd ../../
./waf configure --enable-examples --enable-tests
./waf build
```

### 2. Install Python Dependencies

```bash
cd scratch/webrtc-TFMN-AC-RL1/gym_agent
pip install -r requirements.txt
```

## Quick Start

### Training from Scratch

```bash
# In terminal 1: Start ns-3 simulation (will wait for Python agent)
cd /home/hjt/OSCC/ns-allinone-3.31/ns-3.31
./waf --run "scratch/webrtc-TFMN-AC-RL1/webrtc-TFMN-AC-RL1 \
  --trace=/home/hjt/OSCC/ns-allinone-3.31/ns-3.31/traces/traces/AItrans/AItrans_1.log \
  --skip=true --mu=1.0 --ls=0.01 \
  --folder=trace_results/gym_training --it=gym_test"

# In terminal 2: Start Python training agent
cd scratch/webrtc-TFMN-AC-RL1/gym_agent
python3 train.py --algorithm PPO --timesteps 100000
```

### Training Parameters

- `--algorithm`: RL algorithm (PPO, SAC, TD3) - **default: PPO**
- `--timesteps`: Total training steps - **default: 100000**
- `--port`: ns3-gym communication port - **default: 5555**
- `--model-dir`: Directory to save models - **default: ./models**
- `--log-dir`: TensorBoard log directory - **default: ./logs**
- `--load-model`: Continue training from checkpoint

### Examples

```bash
# Train with SAC for 200k steps
python3 train.py --algorithm SAC --timesteps 200000

# Continue training from checkpoint
python3 train.py --load-model ./models/PPO_webrtc_mu_20260129_123456_final.zip

# Train with custom directories
python3 train.py --model-dir ./my_models --log-dir ./my_logs
```

## Monitoring Training

View training progress with TensorBoard:

```bash
tensorboard --logdir ./logs
# Open http://localhost:6006 in your browser
```

## Environment Specification

- **Observation Space**: Box([Rt, loss_rate], shape=(2,))
  - `Rt`: Transmission opportunities (normalized to [0, 1])
  - `loss_rate`: Packet loss rate (normalized to [0, 1])

- **Action Space**: Box([mu], low=0.5, high=1.5, shape=(1,))
  - `mu`: Bandwidth scaling factor

- **Reward**: Combined metric based on:
  - Bandwidth utilization
  - Delay penalty
  - Loss penalty
  - Deadline miss penalty

## Troubleshooting

### Connection Error

**Problem**: `Failed to connect to ns-3 simulation`

**Solution**: Make sure:
1. ns3-gym is installed in `ns-3.31/contrib/opengym`
2. ns-3 simulation is running before starting Python script
3. Port 5555 is not blocked by firewall

### Import Error

**Problem**: `ModuleNotFoundError: No module named 'ns3gym'`

**Solution**:
```bash
export PYTHONPATH="/home/hjt/OSCC/ns-allinone-3.31/ns-3.31/contrib/opengym/model/ns3gym:$PYTHONPATH"
```

## Architecture

```
┌─────────────────┐          ZMQ/Protobuf          ┌──────────────────┐
│   ns-3 (C++)    │ <────────────────────────────> │  Python Agent    │
│                 │                                 │                  │
│  GymMuLearner   │  Observation: [Rt, loss]       │  PPO/SAC/TD3     │
│  (adapter)      │  Action: [mu]                  │  (SB3 model)     │
│                 │  Reward: QoE metric            │                  │
└─────────────────┘                                 └──────────────────┘
```

## Files

- `train.py`: Main training script
- `requirements.txt`: Python dependencies
- `README.md`: This file

## Next Steps

1. Experiment with different algorithms (PPO, SAC, TD3)
2. Tune hyperparameters (learning rate, batch size, etc.)
3. Try different network traces
4. Visualize learned policy behavior
