def generate_video_trace(filename="frame_trace_0.txt", fps=30, duration=100, frame_size=8000):
    """
    生成视频trace文件
    
    参数:
    filename: 输出文件名
    fps: 帧率 (默认30)
    duration: 视频时长(秒) (默认100)
    frame_size: 每帧大小(字节) (默认7500)
    """
    
    total_frames = fps * duration  # 总帧数
    frame_interval = 1.0 / fps  # 帧间隔时间
    
    with open(filename, 'w') as f:
        for frame_index in range(total_frames):
            # 计算时间戳：从0开始
            timestamp = frame_index * frame_interval
            
            # 确定帧类型：每30帧的第一个为关键帧(1)，其他为普通帧(0)
            frame_type = 1 if (frame_index % 30 == 0) else 0
            
            # 写入数据：时间戳 帧大小 帧类型
            f.write(f"{timestamp:.11f}\t{frame_size}\t{frame_type}\n")
    
    print(f"Trace文件已生成: {filename}")
    print(f"总帧数: {total_frames}")
    print(f"关键帧数: {total_frames // 30}")
    print(f"普通帧数: {total_frames - (total_frames // 30)}")
    print(f"文件时长: {duration}秒")
    print(f"帧率: {fps} FPS")
    print(f"每帧大小: {frame_size} 字节")


def verify_trace_file(filename="frame_trace_0.txt", sample_lines=5):
    """验证生成的trace文件"""
    print(f"\n验证文件: {filename}")
    
    try:
        with open(filename, 'r') as f:
            lines = f.readlines()
        
        total_frames = len(lines)
        key_frames = 0
        normal_frames = 0
        
        print(f"前{sample_lines}行样本:")
        for i in range(min(sample_lines, total_frames)):
            print(f"  {lines[i].strip()}")
        
        print(f"\n最后{sample_lines}行样本:")
        for i in range(max(0, total_frames-sample_lines), total_frames):
            print(f"  {lines[i].strip()}")
        
        for i, line in enumerate(lines):
            parts = line.strip().split('\t')
            if len(parts) == 3:
                timestamp, size, frame_type = parts
                if int(frame_type) == 1:
                    key_frames += 1
                else:
                    normal_frames += 1
        
        print(f"\n统计信息:")
        print(f"总帧数: {total_frames}")
        print(f"关键帧数: {key_frames}")
        print(f"普通帧数: {normal_frames}")
        
        # 检查时间戳序列
        print(f"\n时间戳检查:")
        first_timestamp = float(lines[0].split('\t')[0])
        second_timestamp = float(lines[1].split('\t')[0])
        frame_interval = second_timestamp - first_timestamp
        print(f"第一帧时间戳: {first_timestamp:.11f}")
        print(f"第二帧时间戳: {second_timestamp:.11f}")
        print(f"帧间隔: {frame_interval:.11f}秒 (应为{1/30:.11f}秒)")
        
        # 检查关键帧分布
        key_frame_indices = []
        for i, line in enumerate(lines):
            parts = line.strip().split('\t')
            if len(parts) == 3 and parts[2] == '1':
                key_frame_indices.append(i)
        
        print(f"\n关键帧检查:")
        print(f"第一个关键帧索引: {key_frame_indices[0]}")
        print(f"第二个关键帧索引: {key_frame_indices[1]}")
        print(f"关键帧间隔: {key_frame_indices[1] - key_frame_indices[0]}帧")
        
        # 验证所有间隔
        intervals = [key_frame_indices[i+1] - key_frame_indices[i] for i in range(len(key_frame_indices)-1)]
        if all(interval == 30 for interval in intervals):
            print("✓ 关键帧间隔正确 (每30帧一个关键帧)")
        else:
            print("✗ 关键帧间隔不正确")
            print(f"实际间隔: {intervals[:10]}...")
        
    except Exception as e:
        print(f"验证出错: {e}")


# 如果希望第一个时间戳从0.033333开始，可以使用这个版本
def generate_video_trace_offset(filename="frame_trace_0.txt", fps=30, duration=100, frame_size=8000, start_time=0.03333333333):
    """
    生成视频trace文件（带起始时间偏移）
    
    参数:
    filename: 输出文件名
    fps: 帧率 (默认30)
    duration: 视频时长(秒) (默认100)
    frame_size: 每帧大小(字节) (默认45000)
    start_time: 起始时间戳 (默认0.03333333333)
    """
    
    total_frames = fps * duration  # 总帧数
    frame_interval = 1.0 / fps  # 帧间隔时间
    
    with open(filename, 'w') as f:
        for frame_index in range(total_frames):
            # 计算时间戳：从start_time开始
            timestamp = start_time + frame_index * frame_interval
            
            # 确定帧类型：每30帧的第一个为关键帧(1)，其他为普通帧(0)
            frame_type = 1 if (frame_index % 30 == 0) else 0
            
            # 写入数据：时间戳 帧大小 帧类型
            f.write(f"{timestamp:.11f}\t{frame_size}\t{frame_type}\n")
    
    print(f"Trace文件已生成: {filename}")
    print(f"起始时间: {start_time:.11f}")
    print(f"总帧数: {total_frames}")
    print(f"文件时长: {duration}秒")


if __name__ == "__main__":
    # 方法1：从0开始（标准方式）
    print("=== 方法1：从0开始的时间戳 ===")
    generate_video_trace("frame_trace_from_zero.txt")
    verify_trace_file("frame_trace_from_zero.txt")
    
    print("\n" + "="*50 + "\n")
    
    # 方法2：从0.033333开始（如您要求）
    print("=== 方法2：从0.033333开始的时间戳 ===")
    generate_video_trace_offset("frame_trace_0.txt", start_time=0.03333333333)
    verify_trace_file("frame_trace_0.txt")