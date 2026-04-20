#!/bin/bash

# ============== 配置区 ==============
NS3_DIR="$HOME/OSCC/ns-allinone-3.31/ns-3.31"
GYM_DIR="${NS3_DIR}/scratch/webrtc-TFMN-AC-RL1/gym_agent"
MODELS_DIR="${GYM_DIR}/models"

TOTAL_RUNS=698
SLEEP_BEFORE_PYTHON=8   # 等待 ns-3 启动的秒数，根据实际情况调整
CHECK_INTERVAL=5        # 进程状态检测间隔（秒）
# ====================================

# Ctrl+C 时清理后台 ns-3 和 Python 进程
cleanup() {
    echo ""
    echo "[INFO] 收到中断信号，正在清理..."
    if [ -n "${NS3_PID}" ] && kill -0 ${NS3_PID} 2>/dev/null; then
        kill -9 ${NS3_PID} 2>/dev/null
    fi
    if [ -n "${PYTHON_PID}" ] && kill -0 ${PYTHON_PID} 2>/dev/null; then
        kill -9 ${PYTHON_PID} 2>/dev/null
    fi
    echo "[INFO] 已退出。"
    exit 1
}
trap cleanup SIGINT SIGTERM

echo "========================================"
echo "  共 ${TOTAL_RUNS} 轮实验"
echo "  开始时间: $(date '+%Y-%m-%d %H:%M:%S')"
echo "========================================"
echo ""

for i in $(seq 1 ${TOTAL_RUNS}); do
    echo "========================================"
    echo "  第 ${i} / ${TOTAL_RUNS} 轮"
    echo "  $(date '+%Y-%m-%d %H:%M:%S')"
    echo "========================================"

    # ---------- 查找最新模型 ----------
    LATEST_MODEL=$(ls -t "${MODELS_DIR}"/*.zip 2>/dev/null | head -n 1)

    if [ -z "$LATEST_MODEL" ]; then
        echo "[WARN] ${MODELS_DIR} 下未找到 .zip 模型文件，将不加载模型"
        LOAD_MODEL_ARG=""
    else
        echo "[INFO] 加载模型: ${LATEST_MODEL}"
        LOAD_MODEL_ARG="--load-model ${LATEST_MODEL}"
    fi

    # ---------- 启动 ns-3（后台） ----------
    echo "[INFO] 启动 ns-3 仿真..."
    cd "${NS3_DIR}"
    ./waf --run "scratch/webrtc-TFMN-AC-RL1/webrtc-TFMN-AC-RL1 \
        --trace=traces/traces/AItrans/AItrans_2.log \
        --skip=true --mu=1.0 --ls=0.01 \
        --folder=trace_results/ai_training --it=ai_test" &
    NS3_PID=$!

    echo "[INFO] ns-3 PID: ${NS3_PID}，等待 ${SLEEP_BEFORE_PYTHON} 秒让其初始化..."
    sleep ${SLEEP_BEFORE_PYTHON}

    # 检查 ns-3 是否还活着
    if ! kill -0 ${NS3_PID} 2>/dev/null; then
        echo "[ERROR] ns-3 在初始化阶段就退出了，第 ${i} 轮失败，终止脚本。"
        exit 1
    fi

    # ---------- 启动 Python 训练（后台） ----------
    echo "[INFO] 启动 Python 训练..."
    cd "${GYM_DIR}"
    
    # 将 Python 放入后台运行 (&)
    python3.12 train.py \
        --algorithm PPO \
        --timesteps 8000000 \
        ${LOAD_MODEL_ARG} &
    PYTHON_PID=$!

    echo "[INFO] Python PID: ${PYTHON_PID}。开始监控进程..."

    # ---------- 循环检测进程状态 ----------
    while true; do
        NS3_ALIVE=false
        PYTHON_ALIVE=false

        # 检测进程是否存在
        kill -0 ${NS3_PID} 2>/dev/null && NS3_ALIVE=true
        kill -0 ${PYTHON_PID} 2>/dev/null && PYTHON_ALIVE=true

        if [ "$NS3_ALIVE" = false ] && [ "$PYTHON_ALIVE" = false ]; then
            echo "[INFO] ns-3 和 Python 进程均已结束。"
            break
            
        elif [ "$NS3_ALIVE" = true ] && [ "$PYTHON_ALIVE" = false ]; then
            # 这是一个安全兜底机制：
            # 如果 Python 代码意外崩溃（如语法错误/OOM），而 ns-3 还在死等环境响应，
            # 必须主动杀掉 ns-3，否则脚本会永远卡在这一轮。
            echo "[WARN] Python 已提前退出，正在清理挂起的 ns-3 进程..."
            kill -9 ${NS3_PID} 2>/dev/null
            break
        fi

        # 默认情况下，如果 ns-3 结束了但 Python 还活着（正在抛出断开连接的异常并处理退出），
        # 循环会继续 sleep，等待下一次检测时两者均被判定为 false。
        
        sleep ${CHECK_INTERVAL}
    done

    # 将/home/hjt/OSCC/ns-allinone-3.31/ns-3.31/trace_results/ai_training下的全部分文件move到/mnt/nasDisk_ds3617/OSCC_project_backups/csvs/ai_training_{timestamp}，目的文件夹不存在则创建
    TIMESTAMP=$(date +%Y%m%d%H%M%S)
    if [ ! -d "/mnt/nasDisk_ds3617/OSCC_project_backups/csvs/ai_training_${TIMESTAMP}" ]; then
        mkdir -p /mnt/nasDisk_ds3617/OSCC_project_backups/csvs/ai_training_${TIMESTAMP}
    fi
    mv /home/hjt/OSCC/ns-allinone-3.31/ns-3.31/trace_results/ai_training /mnt/nasDisk_ds3617/OSCC_project_backups/csvs/ai_training_${TIMESTAMP}

    # 回收进程资源，防止产生僵尸进程
    wait ${NS3_PID} 2>/dev/null
    wait ${PYTHON_PID} 2>/dev/null
    NS3_PID=""
    PYTHON_PID=""

    echo "[INFO] 第 ${i} 轮完成！进入下一轮..."
    echo ""
done

echo "========================================"
echo "  全部 ${TOTAL_RUNS} 轮实验完成！"
echo "  结束时间: $(date '+%Y-%m-%d %H:%M:%S')"
echo "========================================"