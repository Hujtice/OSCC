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

import csv

try:
    import numpy as np
    import torch as th
    from stable_baselines3 import PPO, SAC, TD3
    from stable_baselines3.common.callbacks import BaseCallback, CheckpointCallback
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

from ns3ai_env import Ns3AiGymEnv, MU_ACTIONS

# Reward weights (must match C++ rl_state_manager.cc)
_W_U = 10.0
_W_DELAY = 2.5
_W_LOSS = 10.0
_W_MDDL = 10.0

_RT_MAX = 10.0
_NUM_LOSS_LEVELS = 5  # must match C++ kNumLossLevels

_CSV_HEADER = [
    # core (10)
    "core_timestep", "core_frame_id",
    "core_raw_rt", "core_raw_loss", "core_norm_rt", "core_norm_loss",
    "core_reward", "core_action_mu_raw", "core_action_mu", "core_value_estimate",
    # ext (5)
    "ext_action_prob", "ext_action_entropy", "ext_action_log_prob",
    "ext_episode_reward_cum", "ext_episode_length",
    # reward raw (5)
    "reward_raw_delay_ms", "reward_raw_loss_rate", "reward_raw_miss_deadline_s",
    "reward_gcc_bw_bps", "reward_trace_bw_bps",
    # reward unweighted (4)
    "reward_U", "reward_p_delay", "reward_p_loss", "reward_p_mddl",
    # reward weighted (5, computed in Python)
    "reward_weighted_U", "reward_weighted_delay",
    "reward_weighted_loss", "reward_weighted_mddl", "reward_total",
]


class StepLoggerCallback(BaseCallback):
    """Log per-timestep RL data to a CSV file (25 columns)."""

    def __init__(self, log_path: str, verbose: int = 0):
        super().__init__(verbose)
        self.log_path = log_path
        self._file = None
        self._writer = None
        self._ep_reward = 0.0
        self._ep_len = 0

    def _on_training_start(self) -> None:
        self._file = open(self.log_path, "w", newline="")
        self._writer = csv.writer(self._file)
        self._writer.writerow(_CSV_HEADER)

    def _on_step(self) -> bool:
        infos = self.locals.get("infos", [{}])
        info = infos[0] if infos else {}

        rewards = self.locals.get("rewards", np.array([0.0]))
        reward = float(rewards[0])
        actions = self.locals.get("actions", np.array([2]))
        action_index = int(actions[0])
        action_mu = MU_ACTIONS[action_index] if 0 <= action_index < len(MU_ACTIONS) else 1.0

        # --- core: value estimate (PPO stores it in locals) ---
        values_t = self.locals.get("values")
        if values_t is not None:
            value_est = float(values_t[0])
        else:
            value_est = float("nan")

        # --- ext: action_prob, action_entropy, log_prob (Categorical) ---
        log_probs_t = self.locals.get("log_probs")
        log_prob = float(log_probs_t[0]) if log_probs_t is not None else float("nan")

        obs_tensor = self.locals.get("obs_tensor")
        if obs_tensor is not None:
            with th.no_grad():
                dist = self.model.policy.get_distribution(obs_tensor)
                probs = dist.distribution.probs[0]
                action_prob = float(probs[action_index]) if action_index < len(probs) else float("nan")
                action_entropy = float(dist.distribution.entropy()[0])
        else:
            action_prob = float("nan")
            action_entropy = float("nan")

        # --- core: observation that led to this action ---
        if obs_tensor is not None:
            obs_np = obs_tensor[0].cpu().numpy()
            norm_rt = float(obs_np[0])
            norm_loss = float(obs_np[1])
        else:
            new_obs = self.locals.get("new_obs", np.array([[0.0, 0.0]]))
            norm_rt = float(new_obs[0][0])
            norm_loss = float(new_obs[0][1])

        # --- core: de-normalize to raw values ---
        raw_rt = norm_rt * _RT_MAX
        raw_loss = norm_loss * (_NUM_LOSS_LEVELS - 1)  # loss level 0-4

        # --- ext: episode cumulative tracking ---
        self._ep_reward += reward
        self._ep_len += 1

        # --- core: frame_id from C++ ---
        frame_id = info.get("core_frame_id", 0)

        # --- reward sub-items from C++ via info dict ---
        r_U = info.get("reward_U", 0.0)
        r_pd = info.get("reward_p_delay", 0.0)
        r_pl = info.get("reward_p_loss", 0.0)
        r_pm = info.get("reward_p_mddl", 0.0)
        r_raw_delay = info.get("reward_raw_delay_ms", 0.0)
        r_raw_loss = info.get("reward_raw_loss_rate", 0.0)
        r_raw_mddl = info.get("reward_raw_miss_deadline_s", 0.0)
        r_gcc = info.get("reward_gcc_bw_bps", 0.0)
        r_trace = info.get("reward_trace_bw_bps", 0.0)

        # weighted values (computed in Python)
        w_U = _W_U * r_U
        w_delay = -_W_DELAY * r_pd
        w_loss = -_W_LOSS * r_pl
        w_mddl = -_W_MDDL * r_pm
        w_total = w_U + w_delay + w_loss + w_mddl

        self._writer.writerow([
            self.num_timesteps, frame_id,
            f"{raw_rt:.2f}", f"{raw_loss:.1f}", f"{norm_rt:.6f}", f"{norm_loss:.6f}",
            f"{reward:.6f}", action_index, f"{action_mu:.2f}", f"{value_est:.6f}",
            f"{action_prob:.6f}", f"{action_entropy:.6f}", f"{log_prob:.6f}",
            f"{self._ep_reward:.6f}", self._ep_len,
            f"{r_raw_delay:.4f}", f"{r_raw_loss:.6f}", f"{r_raw_mddl:.6f}",
            f"{r_gcc:.2f}", f"{r_trace:.2f}",
            f"{r_U:.6f}", f"{r_pd:.6f}", f"{r_pl:.6f}", f"{r_pm:.6f}",
            f"{w_U:.6f}", f"{w_delay:.6f}", f"{w_loss:.6f}", f"{w_mddl:.6f}",
            f"{w_total:.6f}",
        ])
        if self.num_timesteps % 100 == 0:
            self._file.flush()

        # reset episode accumulators on done
        dones = self.locals.get("dones", np.array([False]))
        if bool(dones[0]):
            self._ep_reward = 0.0
            self._ep_len = 0

        return True

    def _on_training_end(self) -> None:
        if self._file:
            self._file.close()
            self._file = None


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

    if args.algorithm in ("SAC", "TD3"):
        print(f"ERROR: {args.algorithm} 不支持离散动作空间 (Discrete)。请使用 PPO。")
        sys.exit(1)

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
    step_csv_path = os.path.join(args.log_dir, "step_log.csv")
    step_logger = StepLoggerCallback(log_path=step_csv_path, verbose=0)
    print(f"  Step CSV: {step_csv_path}")

    print(f"\n[4/4] Training for {args.timesteps} timesteps...")
    print("-" * 60)
    try:
        model.learn(
            total_timesteps=args.timesteps,
            callback=[checkpoint_callback, step_logger],
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
        # [ERROR] ns3-ai finished (simulation ended) before first obs
        if "ns3-ai finished (simulation ended) before first obs" in str(e):
            path = os.path.join(args.model_dir, f"{model_name}_final")
            model.save(path)
            print("\nTraining completed. Model saved to:", path)
        sys.exit(1)
    finally:
        env.close()


if __name__ == "__main__":
    main()
