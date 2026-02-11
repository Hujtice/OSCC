#!/usr/bin/env python3
"""
WebRTC MU Learning - Gym-based Training Script

This script trains a reinforcement learning agent to optimize bandwidth scaling factor (mu)
for WebRTC video transmission using ns3-gym and Stable-Baselines3.
"""

import os
import sys
import argparse
from datetime import datetime

# Add ns3-gym to Python path (adjust path as needed)
sys.path.insert(0, '/home/hjt/OSCC/ns-allinone-3.31/ns-3.31/contrib/opengym/model/ns3gym')

try:
    import gym
    import ns3gym
    from stable_baselines3 import PPO, SAC, TD3
    from stable_baselines3.common.callbacks import CheckpointCallback, EvalCallback
    from stable_baselines3.common.monitor import Monitor
    import numpy as np
except ImportError as e:
    print(f"ERROR: Missing dependencies: {e}")
    print("\nPlease install required packages:")
    print("  pip install gym==0.21.0 stable-baselines3 ns3gym")
    sys.exit(1)


def create_env(port=5555):
    """Create ns3-gym environment for WebRTC mu learning."""
    try:
        env = ns3gym.Ns3Env(port=port, stepTime=0.1, startSim=False, simSeed=0,
                           simArgs={"--SimulatorImplementationType": "ns3::RealtimeSimulatorImpl"})
        print(f"[INFO] Connected to ns-3 simulation on port {port}")
        return env
    except Exception as e:
        print(f"[ERROR] Failed to connect to ns-3 simulation: {e}")
        print("\nMake sure:")
        print("  1. ns3-gym module is installed in ns-3")
        print("  2. ns-3 simulation is running (or will be started)")
        sys.exit(1)


def main():
    parser = argparse.ArgumentParser(description='Train WebRTC MU learning agent')
    parser.add_argument('--algorithm', type=str, default='PPO', 
                       choices=['PPO', 'SAC', 'TD3'],
                       help='RL algorithm to use (default: PPO)')
    parser.add_argument('--timesteps', type=int, default=100000,
                       help='Total training timesteps (default: 100000)')
    parser.add_argument('--port', type=int, default=5555,
                       help='ns3-gym port (default: 5555)')
    parser.add_argument('--model-dir', type=str, default='./models',
                       help='Directory to save models (default: ./models)')
    parser.add_argument('--log-dir', type=str, default='./logs',
                       help='Directory for tensorboard logs (default: ./logs)')
    parser.add_argument('--load-model', type=str, default=None,
                       help='Path to pre-trained model to continue training')
    
    args = parser.parse_args()
    
    # Create directories
    os.makedirs(args.model_dir, exist_ok=True)
    os.makedirs(args.log_dir, exist_ok=True)
    
    print("=" * 60)
    print("WebRTC MU Learning - Training Session")
    print("=" * 60)
    print(f"Algorithm:        {args.algorithm}")
    print(f"Total timesteps:  {args.timesteps}")
    print(f"Port:             {args.port}")
    print(f"Model directory:  {args.model_dir}")
    print(f"Log directory:    {args.log_dir}")
    print("=" * 60)
    
    # Create environment
    print("\n[1/4] Creating ns3-gym environment...")
    env = create_env(port=args.port)
    env = Monitor(env, args.log_dir)
    
    print(f"  Observation space: {env.observation_space}")
    print(f"  Action space:      {env.action_space}")
    
    # Create or load model
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    model_name = f"{args.algorithm}_webrtc_mu_{timestamp}"
    
    print(f"\n[2/4] {'Loading' if args.load_model else 'Creating'} {args.algorithm} model...")
    
    if args.load_model:
        if args.algorithm == 'PPO':
            model = PPO.load(args.load_model, env=env)
        elif args.algorithm == 'SAC':
            model = SAC.load(args.load_model, env=env)
        elif args.algorithm == 'TD3':
            model = TD3.load(args.load_model, env=env)
        print(f"  Loaded model from: {args.load_model}")
    else:
        if args.algorithm == 'PPO':
            model = PPO("MlpPolicy", env, verbose=1, tensorboard_log=args.log_dir)
        elif args.algorithm == 'SAC':
            model = SAC("MlpPolicy", env, verbose=1, tensorboard_log=args.log_dir)
        elif args.algorithm == 'TD3':
            model = TD3("MlpPolicy", env, verbose=1, tensorboard_log=args.log_dir)
        print(f"  Created new {args.algorithm} model")
    
    # Setup callbacks
    print("\n[3/4] Setting up training callbacks...")
    checkpoint_callback = CheckpointCallback(
        save_freq=10000,
        save_path=args.model_dir,
        name_prefix=model_name
    )
    print(f"  Checkpoint every 10000 steps to {args.model_dir}")
    
    # Start training
    print(f"\n[4/4] Starting training for {args.timesteps} timesteps...")
    print("-" * 60)
    
    try:
        model.learn(
            total_timesteps=args.timesteps,
            callback=checkpoint_callback,
            tb_log_name=model_name
        )
        
        # Save final model
        final_model_path = os.path.join(args.model_dir, f"{model_name}_final")
        model.save(final_model_path)
        
        print("\n" + "=" * 60)
        print("Training completed successfully!")
        print(f"Final model saved to: {final_model_path}")
        print("=" * 60)
        
        print("\nTo view training progress:")
        print(f"  tensorboard --logdir {args.log_dir}")
        
    except KeyboardInterrupt:
        print("\n[WARNING] Training interrupted by user")
        interrupted_model_path = os.path.join(args.model_dir, f"{model_name}_interrupted")
        model.save(interrupted_model_path)
        print(f"Model saved to: {interrupted_model_path}")
    except Exception as e:
        print(f"\n[ERROR] Training failed: {e}")
        sys.exit(1)
    finally:
        env.close()


if __name__ == "__main__":
    main()
