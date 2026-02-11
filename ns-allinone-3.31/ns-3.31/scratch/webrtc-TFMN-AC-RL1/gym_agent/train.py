#!/usr/bin/env python3
"""
WebRTC MU Learning - Training Script (ns3-ai)

Trains an RL agent to optimize bandwidth scaling factor (mu) for WebRTC
using ns3-ai shared memory and Stable-Baselines3. Start ns-3 simulation first,
then run this script.
"""

import os
import sys
import argparse
from datetime import datetime

# Add ns3-ai py_interface so we can import py_interface (requires shm_pool built/installed)
_NS3_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
_PY_INTERFACE = os.path.join(_NS3_ROOT, "src", "ns3-ai", "py_interface")
if os.path.isdir(_PY_INTERFACE):
    sys.path.insert(0, _PY_INTERFACE)

try:
    import numpy as np
    from stable_baselines3 import PPO, SAC, TD3
    from stable_baselines3.common.callbacks import CheckpointCallback
    from stable_baselines3.common.monitor import Monitor
except ImportError as e:
    print(f"ERROR: Missing dependency: {e}")
    print("  pip install stable-baselines3 torch tensorboard")
    sys.exit(1)

try:
    import py_interface
except ImportError as e:
    print("ERROR: py_interface (ns3-ai) not found:", e)
    print("Install the ns3-ai Python package:")
    print(f"  cd {_PY_INTERFACE} && pip install --user .")
    print("Or set PYTHONPATH to include the directory containing py_interface.py and shm_pool.")
    sys.exit(1)

from ns3ai_env import Ns3AiGymEnv

# POSIX 共享内存 key，传给 shmget()，必须与 ns-3 GlobalValue "SharedMemoryKey" 一致
SHM_KEY = 1234

# 共享内存池大小（字节），必须与 ns-3 GlobalValue "SharedMemoryPoolSize" 一致
SHM_POOL_SIZE = 4096

# 逻辑内存块 ID（池内偏移标识），必须与 C++ AiMuLearner::kDefaultShmId 一致
# 注意：SHM_KEY 和 DEFAULT_SHM_ID 碰巧值相同(1234)但用途完全不同
DEFAULT_SHM_ID = 1234


def create_env(shm_id=DEFAULT_SHM_ID):
    """Create ns3-ai Gym environment. Call after py_interface.Init()."""
    return Ns3AiGymEnv(shm_id=shm_id)


def main():
    parser = argparse.ArgumentParser(description="Train WebRTC MU learning agent (ns3-ai)")
    parser.add_argument("--algorithm", type=str, default="PPO", choices=["PPO", "SAC", "TD3"])
    parser.add_argument("--timesteps", type=int, default=100000)
    parser.add_argument("--shm-id", type=int, default=DEFAULT_SHM_ID,
                        help="Shared memory block id (must match ns-3 AiMuLearner, default 1234)")
    parser.add_argument("--model-dir", type=str, default="./models")
    parser.add_argument("--log-dir", type=str, default="./logs")
    parser.add_argument("--load-model", type=str, default=None)
    args = parser.parse_args()

    os.makedirs(args.model_dir, exist_ok=True)
    os.makedirs(args.log_dir, exist_ok=True)

    print("=" * 60)
    print("WebRTC MU Learning - Training (ns3-ai)")
    print("=" * 60)
    print(f"Algorithm:        {args.algorithm}")
    print(f"Timesteps:        {args.timesteps}")
    print(f"shm_id:           {args.shm_id}")
    print(f"Model dir:       {args.model_dir}")
    print("=" * 60)

    print("\n[1/4] Initializing shared memory and creating env...")
    py_interface.Init(SHM_KEY, SHM_POOL_SIZE)
    env = create_env(shm_id=args.shm_id)
    env = Monitor(env, args.log_dir)
    print(f"  Observation space: {env.observation_space}")
    print(f"  Action space:      {env.action_space}")

    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    model_name = f"{args.algorithm}_webrtc_mu_{timestamp}"

    print(f"\n[2/4] {'Loading' if args.load_model else 'Creating'} {args.algorithm} model...")
    if args.load_model:
        if args.algorithm == "PPO":
            model = PPO.load(args.load_model, env=env)
        elif args.algorithm == "SAC":
            model = SAC.load(args.load_model, env=env)
        else:
            model = TD3.load(args.load_model, env=env)
        print(f"  Loaded: {args.load_model}")
    else:
        if args.algorithm == "PPO":
            model = PPO("MlpPolicy", env, verbose=1, tensorboard_log=args.log_dir)
        elif args.algorithm == "SAC":
            model = SAC("MlpPolicy", env, verbose=1, tensorboard_log=args.log_dir)
        else:
            model = TD3("MlpPolicy", env, verbose=1, tensorboard_log=args.log_dir)
        print(f"  Created new {args.algorithm} model")

    print("\n[3/4] Callbacks...")
    checkpoint_callback = CheckpointCallback(
        save_freq=10000, save_path=args.model_dir, name_prefix=model_name
    )

    print(f"\n[4/4] Training for {args.timesteps} timesteps...")
    print("-" * 60)
    try:
        model.learn(
            total_timesteps=args.timesteps,
            callback=checkpoint_callback,
            tb_log_name=model_name,
        )
        path = os.path.join(args.model_dir, f"{model_name}_final")
        model.save(path)
        print("\nTraining completed. Model saved to:", path)
    except KeyboardInterrupt:
        path = os.path.join(args.model_dir, f"{model_name}_interrupted")
        model.save(path)
        print("\nInterrupted. Model saved to:", path)
    except Exception as e:
        print(f"\n[ERROR] {e}")
        sys.exit(1)
    finally:
        env.close()


if __name__ == "__main__":
    main()
