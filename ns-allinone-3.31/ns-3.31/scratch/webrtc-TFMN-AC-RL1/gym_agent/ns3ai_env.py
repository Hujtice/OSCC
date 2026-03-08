"""
Gymnasium/gym wrapper for ns3-ai shared memory (WebRTC mu learning).
Bridges py_interface.Ns3AIRL with Stable-Baselines3 (step/observation_space/action_space).
"""
import numpy as np
from ctypes import Structure, c_float, c_uint8

try:
    import gymnasium as gym
    from gymnasium import spaces
except ImportError:
    import gym
    from gym import spaces

# ShmEnv / ShmAction must match C++ (common_types.h) and ns3-ai layout
class ShmEnv(Structure):
    _pack_ = 1
    _fields_ = [
        ("norm_rt", c_float),
        ("norm_loss", c_float),
        ("reward", c_float),
        ("done", c_uint8),
    ]


class ShmAction(Structure):
    _pack_ = 1
    _fields_ = [
        ("mu", c_float),
    ]


class Ns3AiGymEnv(gym.Env):
    """
    Gym environment that talks to ns-3 via ns3-ai shared memory.
    Call py_interface.Init(key, pool_size) before creating this env.
    """
    metadata = {"render_modes": []}

    def __init__(self, shm_id=1234, py_interface_module=None, **kwargs):
        super().__init__(**kwargs)
        self._shm_id = shm_id
        if py_interface_module is None:
            import py_interface as p
            self._py = p
        else:
            self._py = py_interface_module
        self._rl = self._py.Ns3AIRL(shm_id, ShmEnv, ShmAction)
        self._rl.SetCond(2, 1)  # wait for version % 2 == 1 (C++ wrote env)
        self._last_obs = np.zeros(2, dtype=np.float32)
        self._last_reward = 0.0
        self._last_done = False
        self._first_reset = True
        self.observation_space = spaces.Box(
            low=0.0, high=1.0, shape=(2,), dtype=np.float32
        )
        self.action_space = spaces.Box(
            low=0.5, high=1.5, shape=(1,), dtype=np.float32
        )

    def reset(self, seed=None, options=None):
        """Reset the environment and return the first observation.

        Reads the first env written by C++ (norm_rt, norm_loss) and replies
        with a default action mu=1.0 (no bandwidth scaling). The reward
        returned by the *next* step() call corresponds to this default action.
        """
        if seed is not None:
            np.random.seed(seed)
        self._first_reset = False
        # Wait for first observation from ns-3 (C++ writes env, we read and reply with action)
        with self._rl as data:
            if data is None:
                raise RuntimeError("ns3-ai finished (simulation ended) before first obs")
            self._last_obs = np.array([data.env.norm_rt, data.env.norm_loss], dtype=np.float32)
            data.act.mu = 1.0  # default first action
        return self._last_obs.copy(), {}

    def step(self, action):
        """Execute one step: send *action* to ns-3 and receive (obs, reward, done).

        The reward returned here is computed by C++ based on the *previous*
        action (the one sent in the prior step() or reset()). This is inherent
        to the shared-memory protocol: C++ observes the effect of the last mu,
        calculates the reward, and writes it together with the next observation.
        """
        # action is [mu] from SB3; clip to [0.5, 1.5]
        mu = float(np.clip(action[0], 0.8, 1.2))
        # Acquire: get obs/reward/done that C++ wrote (after our previous action); then write our action
        with self._rl as data:
            if data is None:
                return self._last_obs.copy(), self._last_reward, True, False, {}
            obs = np.array([data.env.norm_rt, data.env.norm_loss], dtype=np.float32)
            reward = float(data.env.reward)
            done = bool(data.env.done)
            data.act.mu = mu
        self._last_obs = obs
        self._last_reward = reward
        self._last_done = done
        return obs.copy(), reward, done, False, {}

    def close(self):
        """Release the shared memory pool.

        WARNING: FreeMemory() is a global operation — it destroys the entire
        shared memory pool identified by SHM_KEY, affecting *all* Ns3AIRL
        instances that share the same pool. Only call close() when the
        simulation is completely finished and no other env needs the pool.
        """
        self._py.FreeMemory()
