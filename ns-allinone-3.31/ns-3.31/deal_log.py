from collections import Counter
import re
import pandas as pd
import matplotlib.pyplot as plt

import os

import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d import Axes3D

file_paths = ['webrtc_simulation1.log', 'webrtc_simulation2.log', 'webrtc_simulation3.log', 'webrtc_simulation4.log', 'webrtc_simulation5.log']   

def extract_and_count(file_path):

    # 定义正则表达式模式
    # 匹配格式：...当前frame_id: 0当前Rt: 0当前包数: 28当前mu: 1...
    pattern = re.compile(r'当前frame_id:\s*\d+.*当前Rt:\s*[\d.]+.*当前包数:\s*\d+当前mu:\s*([\d.]+)')

    matched_lines = []
    count = 0

    try:
        with open(file_path, 'r', encoding='utf-8') as f:
            lines = f.readlines()

            for line in lines:
                # 使用search查找是否包含模式
                if pattern.search(line):
                    matched_lines.append(line)
                    count += 1

        # 打印统计结果
        print(f"总共匹配到 {count} 行。")
        print("-" * 30)

        # 输出CSV文件，格式为frame_id, Rt, 包数, mu
        with open(file_path.replace('.log', '_count.csv'), 'w', encoding='utf-8') as out_file:
            out_file.write("frame_id,Rt,包数,mu\n")  # 写入CSV表头
            for line in matched_lines:
                # 提取frame_id, Rt, 包数, mu
                frame_id_match = re.search(r'当前frame_id:\s*(\d+)', line)
                rt_match = re.search(r'当前Rt:\s*(\d+)', line)
                packet_count_match = re.search(r'当前包数:\s*(\d+)', line)
                mu_match = re.search(r'当前mu:\s*([\d.]+)', line)

                if frame_id_match and rt_match and packet_count_match and mu_match:
                    frame_id = frame_id_match.group(1)
                    rt = rt_match.group(1)
                    packet_count = packet_count_match.group(1)
                    mu = mu_match.group(1)
                    out_file.write(f"{frame_id},{rt},{packet_count},{mu}\n")


    except FileNotFoundError:
        print(f"文件 {file_path} 未找到。")

def extract_and_loss(file_path):
    
    # 定义正则表达式模式
    # 匹配格式：...<QoEManager> 当前frame_id: 0 当前Rt: 0 当前observed_loss: 0.003 当前mu: 1.025...
    pattern = re.compile(r'<QoEManager> 当前frame_id:\s*\d+.*当前Rt:\s*[\d.]+.*当前observed_loss:\s*([\d.])+.*当前mu:\s*([\d.]+)')

    matched_lines = []
    count = 0

    try:
        with open(file_path, 'r', encoding='utf-8') as f:
            lines = f.readlines()

            for line in lines:
                # 使用search查找是否包含模式
                if pattern.search(line):
                    matched_lines.append(line)
                    count += 1

        # 打印统计结果
        print(f"总共匹配到 {count} 行。")
        print("-" * 30)

        # 输出CSV文件，格式为frame_id, Rt, observed_loss, mu
        with open(file_path.replace('.log', '_loss.csv'), 'w', encoding='utf-8') as out_file:
            out_file.write("frame_id,Rt,observed_loss,mu\n")  # 写入CSV表头
            for line in matched_lines:
                # 提取frame_id, Rt, observed_loss, mu
                frame_id_match = re.search(r'当前frame_id:\s*(\d+)', line)
                rt_match = re.search(r'当前Rt:\s*(\d+)', line)
                observed_loss_match = re.search(r'当前observed_loss:\s*([\d.]+)', line)
                mu_match = re.search(r'当前mu:\s*([\d.]+)', line)

                if frame_id_match and rt_match and observed_loss_match and mu_match:
                    frame_id = frame_id_match.group(1)
                    rt = rt_match.group(1)
                    observed_loss = observed_loss_match.group(1)
                    mu = mu_match.group(1)
                    out_file.write(f"{frame_id},{rt},{observed_loss},{mu}\n")


    except FileNotFoundError:
        print(f"文件 {file_path} 未找到。")

def extract_RL_log(file_path):
    # [RL-STEP] frame=0 | pkts=28 | state=(Rt=0, loss=0.0344828) | action=(mu=1) | reward=-0.246885 | baseline=-0.0123443 | advantage=-0.234541
    # [RL-STEP] frame=1 | pkts=3 | state=(Rt=0, loss=0) | action=(mu=0.95) | reward=0.0176641 | baseline=-0.0108438 | advantage=0.028508
    pattern = re.compile(
    r'\[RL-STEP\] frame=(\d+) \| pkts=(\d+) '
    r'\| state=\(Rt=(\d+), loss=([^)]+)\)'  # 匹配到 ) 为止
    r' \| action=\(mu=([^)]+)\)'           # 匹配到 ) 为止
    r' \| reward=(\S+)'                    # 匹配直到遇到空格
    r' \| baseline=(\S+)'
    r' \| advantage=(\S+)'
    )

    matched_lines = []
    count = 0

    try:
        with open(file_path, 'r', encoding='utf-8') as f:
            lines = f.readlines()

            for line in lines:
                if "RL-STEP" in line and not pattern.search(line):
                    print(line)
                if pattern.search(line):
                    matched_lines.append(line)
                    count += 1

        print(f"总共匹配到 {count} 行。")
        print("-" * 30)
        
        # 输出CSV文件，格式为frame,pkts,Rt,loss,mu,reward,baseline,advantage
        with open(file_path.replace('.log', '_rl_log.csv'), 'w', encoding='utf-8') as out_file:
            out_file.write("frame,pkts,Rt,loss,mu,reward,baseline,advantage\n")
            for line in matched_lines:
                frame, pkts, Rt, loss, mu, reward, baseline, advantage = pattern.search(line).groups()
        
                # 此时得到的都是字符串，如果需要计算，可以转为 float 或 int
                out_file.write(f"{frame},{pkts},{Rt},{loss},{mu},{reward},{baseline},{advantage}\n")

    except FileNotFoundError:
        print(f"文件 {file_path} 未找到。")

def extract_packet_log(file_path):
    # 包的帧id: 1 包seq: 31 Rt: 0 发送时间: 137 接收时间: 159
    pattern = re.compile(r'包的帧id:\s*\d+.*包seq:\s*\d+.*Rt:\s*[\d.]+.*发送时间:\s*\d+.*接收时间:\s*\d+')
    matched_lines = []
    count = 0

    try:
        with open(file_path, 'r', encoding='utf-8') as f:
            lines = f.readlines()

            for line in lines:
                if pattern.search(line):
                    matched_lines.append(line)
                    count += 1

        print(f"总共匹配到 {count} 行。")
        print("-" * 30)

        with open(file_path.replace('.log', '_pak_log.csv'), 'w', encoding='utf-8') as out_file:
            out_file.write("frame,seq,Rt,send_time,receive_time\n")
            for line in matched_lines:
                frame_match = re.search(r'包的帧id:\s*(\d+)', line)
                seq_match = re.search(r'包seq:\s*(\d+)', line)
                Rt_match = re.search(r'Rt:\s*(\d+)', line)
                send_time_match = re.search(r'发送时间:\s*(\d+)', line)
                receive_time_match = re.search(r'接收时间:\s*(\d+)', line)

                
                if frame_match and seq_match and Rt_match and send_time_match and receive_time_match:
                    frame = frame_match.group(1)
                    seq = seq_match.group(1)
                    Rt = Rt_match.group(1)
                    send_time = send_time_match.group(1)
                    receive_time = receive_time_match.group(1)
                    out_file.write(f"{frame},{seq},{Rt},{send_time},{receive_time}\n")

    except FileNotFoundError:
        print(f"文件 {file_path} 未找到。")

def build_total_csv(log_path):
    """
    将 *_rl_log.csv, *_loss.csv, *_count.csv, *_pak_log.csv 按 frame+Rt 连接，
    输出 *_total.csv。
    """
    base = log_path.replace('.log', '')
    rl_path = f"{base}_rl_log.csv"
    loss_path = f"{base}_loss.csv"
    count_path = f"{base}_count.csv"
    pak_path = f"{base}_pak_log.csv"
    out_path = f"{base}_total.csv"

    required_files = [rl_path, loss_path, count_path, pak_path]
    missing_files = [p for p in required_files if not os.path.exists(p)]
    if missing_files:
        print("[build_total_csv] 缺少输入文件，跳过：")
        for p in missing_files:
            print(f"  - {p}")
        return

    rl_df = pd.read_csv(rl_path)
    loss_df = pd.read_csv(loss_path).rename(columns={"frame_id": "frame"})
    count_df = pd.read_csv(count_path).rename(columns={"frame_id": "frame", "包数": "pkts_count"})
    pak_df = pd.read_csv(pak_path)

    # 统一主键类型，避免字符串/数字导致连接失败
    key_cols = ["frame", "Rt"]
    for col in key_cols:
        rl_df[col] = pd.to_numeric(rl_df[col], errors="coerce")
        loss_df[col] = pd.to_numeric(loss_df[col], errors="coerce")
        count_df[col] = pd.to_numeric(count_df[col], errors="coerce")
        pak_df[col] = pd.to_numeric(pak_df[col], errors="coerce")

    rl_df["pkts"] = pd.to_numeric(rl_df["pkts"], errors="coerce")
    count_df["pkts_count"] = pd.to_numeric(count_df["pkts_count"], errors="coerce")
    pak_df["seq"] = pd.to_numeric(pak_df["seq"], errors="coerce")
    pak_df["send_time"] = pd.to_numeric(pak_df["send_time"], errors="coerce")
    pak_df["receive_time"] = pd.to_numeric(pak_df["receive_time"], errors="coerce")

    # 包级日志聚合：同一 frame+Rt 内按 seq 取首包/末包时间
    pak_agg = (
        pak_df.sort_values(["frame", "Rt", "seq"])
        .groupby(["frame", "Rt"], as_index=False)
        .agg(
            first_send_time=("send_time", "first"),
            first_receive_time=("receive_time", "first"),
            last_send_time=("send_time", "last"),
            last_receive_time=("receive_time", "last"),
        )
    )

    total_df = (
        rl_df
        .merge(loss_df[["frame", "Rt", "observed_loss"]], on=["frame", "Rt"], how="left")
        .merge(count_df[["frame", "Rt", "pkts_count"]], on=["frame", "Rt"], how="left")
        .merge(pak_agg, on=["frame", "Rt"], how="left")
    )

    # 一致性检查：rl_log 的 pkts 与 count 表中的包数
    mismatch_mask = total_df["pkts_count"].notna() & (total_df["pkts"] != total_df["pkts_count"])
    mismatch_count = int(mismatch_mask.sum())
    if mismatch_count > 0:
        print(f"[build_total_csv] 警告：发现 {mismatch_count} 行 pkts 与 count.包数 不一致")

    # 缺失检查（连接后未匹配）
    check_cols = ["observed_loss", "first_send_time", "first_receive_time", "last_send_time", "last_receive_time"]
    for col in check_cols:
        missing = int(total_df[col].isna().sum())
        if missing > 0:
            print(f"[build_total_csv] 警告：列 {col} 有 {missing} 行为空")

    out_cols = [
        "frame", "pkts", "Rt", "loss", "mu", "reward", "baseline", "advantage",
        "observed_loss", "first_send_time", "first_receive_time", "last_send_time", "last_receive_time"
    ]
    total_df[out_cols].to_csv(out_path, index=False)
    print(f"[build_total_csv] 已输出: {out_path}，共 {len(total_df)} 行")

# def plt_by_Rt_loss(file_path):
#     df = pd.read_csv(file_path)
    
#     # 绘制三维分布图，x轴为Rt，y轴为observed_loss，z轴为mu
#     fig = plt.figure()
#     ax = fig.add_subplot(111, projection='3d')
#     ax.scatter(df['Rt'], df['observed_loss'], df['mu'])
#     ax.set_xlabel('Rt')
#     ax.set_ylabel('observed_loss')
#     ax.set_zlabel('mu')
#     plt.show()


def plt_with_linear_fitting(file_path):
    df = pd.read_csv(file_path)
    
    # 1. 准备数据
    X = df['Rt'].values
    Y = df['observed_loss'].values
    Z = df['mu'].values

    # 2. 线性拟合 (求解 z = Ax + By + C)
    # 构造矩阵 [x, y, 1]
    tmp_A = np.c_[X, Y, np.ones(X.shape[0])]
    # 使用最小二乘法求解系数 C_coeffs = [A, B, C]
    C_coeffs, _, _, _ = np.linalg.lstsq(tmp_A, Z, rcond=None)
    
    print(f"拟合方程: mu = {C_coeffs[0]:.4f}*Rt + {C_coeffs[1]:.4f}*loss + {C_coeffs[2]:.4f}")

    # 3. 绘图
    fig = plt.figure(figsize=(10, 8))
    ax = fig.add_subplot(111, projection='3d')

    # 绘制原始散点 (降低透明度以便看清平面)
    ax.scatter(X, Y, Z, c='steelblue', marker='o', alpha=0.4, label='Data Points')

    # 4. 创建拟合平面的网格
    x_range = np.linspace(X.min(), X.max(), 10)
    y_range = np.linspace(Y.min(), Y.max(), 10)
    x_mesh, y_mesh = np.meshgrid(x_range, y_range)
    z_mesh = C_coeffs[0] * x_mesh + C_coeffs[1] * y_mesh + C_coeffs[2]

    # 打印拟合的平面方程
    print(f"拟合平面方程: mu = {C_coeffs[0]:.4f}*Rt + {C_coeffs[1]:.4f}*loss + {C_coeffs[2]:.4f}")

    # 绘制平面
    surf = ax.plot_surface(x_mesh, y_mesh, z_mesh, color='orange', alpha=0.5, label='Fitting Plane')
    
    # 注意：matplotlib 的 plot_surface 默认不支持直接加 label，这里手动处理图例颜色
    import matplotlib.lines as mlines
    blue_dot = mlines.Line2D([], [], color='steelblue', marker='o', linestyle='None', label='Raw Data')
    orange_patch = mlines.Line2D([], [], color='orange', marker='s', linestyle='None', label='Linear Fit')
    ax.legend(handles=[blue_dot, orange_patch])

    ax.set_xlabel('Rt')
    ax.set_ylabel('observed_loss')
    ax.set_zlabel('mu')
    plt.title('3D Linear Regression Fit')
    plt.savefig(file_path.replace('.csv', '.png'))


if __name__ == '__main__':
    # file_paths = [f for f in os.listdir('./webrtc_simulation_logs') if f.endswith('_1775148251.log')]
    file_paths = [f for f in os.listdir('./webrtc_simulation_logs') if f.endswith('.log')]
    for file_path in file_paths:
            print(f"正在处理文件: {file_path}")
            # extract_and_count(f'./webrtc_simulation_logs/{file_path}')
            extract_and_count(f'./webrtc_simulation_logs/{file_path}')
            extract_and_loss(f'./webrtc_simulation_logs/{file_path}')
            extract_packet_log(f'./webrtc_simulation_logs/{file_path}')
            extract_RL_log(f'./webrtc_simulation_logs/{file_path}')
            build_total_csv(f'./webrtc_simulation_logs/{file_path}')

            # 输出了4个csv文件，我要像sql一样进行连接，最后输出为*_total.csv
            # 表头如下
            # frame,pkts,Rt,loss,mu,reward,baseline,advantage,observed_loss,第一个包的发包时间，第一个包的收包时间，最后一个包的发包时间，最后一个包的收包时间，



