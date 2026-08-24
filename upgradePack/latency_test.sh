#!/bin/bash

################################################################################
# 串口服务器实时性测试脚本 - 双向往返延迟测试
# 测试方向: Linux <-> 串口服务器 (RTT)
# 数据模式: 小包高频
# 测试时长: 5分钟
################################################################################

# 配置参数
SERIAL_DEVICE="/dev/ttyw0f"
BAUD_RATE="115200"
DATA_SIZE=8           # 每次发送的字节数
TEST_DURATION=60      # 测试时长（秒）
SEND_INTERVAL=1000     # 发送间隔（微秒，即1ms）
LOG_FILE="latency_test_$(date +%Y%m%d_%H%M%S).log"

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 检查依赖
check_dependencies() {
    echo -e "${YELLOW}检查依赖...${NC}"
    if ! command -v stty &> /dev/null; then
        echo -e "${RED}错误: 未找到 stty 命令${NC}"
        exit 1
    fi
    if ! command -v python3 &> /dev/null; then
        echo -e "${RED}错误: 未找到 python3${NC}"
        exit 1
    fi
    if ! python3 -c "import serial" 2>/dev/null; then
        echo -e "${RED}错误: 未安装 pyserial 库${NC}"
        echo "请运行: pip3 install pyserial"
        exit 1
    fi
    echo -e "${GREEN}依赖检查通过${NC}"
}

# 配置串口
configure_serial() {
    echo -e "${YELLOW}配置串口设备...${NC}"
    
    # 检查设备是否存在
    if [ ! -c "$SERIAL_DEVICE" ]; then
        echo -e "${RED}错误: 串口设备 $SERIAL_DEVICE 不存在${NC}"
        exit 1
    fi
    
    # 配置串口参数
    stty -F "$SERIAL_DEVICE" "$BAUD_RATE" cs8 -cstopb -parenb raw -echo -ixon -ixoff time 0 min 1
    
    if [ $? -ne 0 ]; then
        echo -e "${RED}错误: 串口配置失败${NC}"
        exit 1
    fi
    
    echo -e "${GREEN}串口配置完成: $SERIAL_DEVICE @ $BAUD_RATE bps${NC}"
}

# Python 测试程序
create_test_program() {
    cat > /tmp/latency_test.py << 'PYEOF'
#!/usr/bin/env python3
import serial
import time
import struct
import sys
import signal
from collections import deque
import os

# 配置
SERIAL_DEVICE = os.environ.get('SERIAL_DEVICE', '/dev/ttyw0f')
BAUD_RATE = int(os.environ.get('BAUD_RATE', '115200'))
DATA_SIZE = int(os.environ.get('DATA_SIZE', '8'))
TEST_DURATION = int(os.environ.get('TEST_DURATION', '300'))
SEND_INTERVAL = int(os.environ.get('SEND_INTERVAL', '1000'))

# 统计变量
latencies = deque()
success_count = 0
timeout_count = 0
error_count = 0
test_running = True
start_time = None

def signal_handler(signum, frame):
    global test_running
    test_running = False
    print("\n\n接收到中断信号，停止测试...")

signal.signal(signal.SIGINT, signal_handler)
signal.signal(signal.SIGTERM, signal_handler)

def calculate_checksum(data):
    """计算简单的校验和"""
    return sum(data) & 0xFF

def format_bytes(data):
    """格式化字节数据为十六进制字符串"""
    return ' '.join(f'{b:02X}' for b in data)

def print_statistics():
    """打印统计信息"""
    if not latencies:
        print("\n没有有效的延迟数据")
        return
    
    sorted_latencies = sorted(latencies)
    total = len(sorted_latencies)
    
    min_latency = sorted_latencies[0]
    max_latency = sorted_latencies[-1]
    avg_latency = sum(sorted_latencies) / total
    median_latency = sorted_latencies[total // 2]
    
    # 计算 p95 和 p99
    p95_index = int(total * 0.95)
    p99_index = int(total * 0.99)
    p95_latency = sorted_latencies[p95_index]
    p99_latency = sorted_latencies[p99_index]
    
    print("\n" + "="*60)
    print("实时性测试统计报告")
    print("="*60)
    print(f"测试时长: {time.time() - start_time:.2f} 秒")
    print(f"成功次数: {success_count}")
    print(f"超时次数: {timeout_count}")
    print(f"错误次数: {error_count}")
    print(f"成功率: {success_count / (success_count + timeout_count + error_count) * 100:.2f}%")
    print("-"*60)
    print(f"最小延迟: {min_latency:.3f} ms")
    print(f"最大延迟: {max_latency:.3f} ms")
    print(f"平均延迟: {avg_latency:.3f} ms")
    print(f"中位延迟: {median_latency:.3f} ms")
    print(f"P95 延迟: {p95_latency:.3f} ms")
    print(f"P99 延迟: {p99_latency:.3f} ms")
    print(f"数据吞吐: {success_count / (time.time() - start_time):.2f} 次/秒")
    print("="*60)

def main():
    global start_time, success_count, timeout_count, error_count
    
    print(f"串口设备: {SERIAL_DEVICE}")
    print(f"波特率: {BAUD_RATE}")
    print(f"数据大小: {DATA_SIZE} 字节")
    print(f"测试时长: {TEST_DURATION} 秒")
    print(f"发送间隔: {SEND_INTERVAL} 微秒")
    print("-"*60)
    
    try:
        # 打开串口
        ser = serial.Serial(
            port=SERIAL_DEVICE,
            baudrate=BAUD_RATE,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=0.1,
            xonxoff=False,
            rtscts=False,
            dsrdtr=False
        )
        
        ser.reset_input_buffer()
        ser.reset_output_buffer()
        
        print("串口已打开，开始测试...")
        print("按 Ctrl+C 停止测试\n")
        
        start_time = time.time()
        end_time = start_time + TEST_DURATION
        seq_num = 0
        
        while test_running and time.time() < end_time:
            seq_num += 1
            
            # 构造测试数据包
            # 格式: [序号(4字节)][数据(4字节)][校验和(1字节)]
            payload = struct.pack('<I', seq_num) + struct.pack('<I', seq_num & 0xFFFFFFFF)
            checksum = calculate_checksum(payload)
            packet = payload + bytes([checksum])
            
            # 发送数据
            send_time = time.time()
            ser.write(packet)
            
            # 等待响应（假设设备会原样返回数据）
            response = ser.read(len(packet))
            
            if len(response) == len(packet):
                # 验证响应数据
                recv_checksum = response[-1]
                calc_checksum = calculate_checksum(response[:-1])
                
                if recv_checksum == calc_checksum:
                    recv_seq = struct.unpack('<I', response[0:4])[0]
                    
                    if recv_seq == seq_num:
                        # 计算往返延迟
                        rtt_time = (time.time() - send_time) * 1000  # 转换为毫秒
                        latencies.append(rtt_time)
                        success_count += 1
                        
                        # 实时显示（每100次）
                        if success_count % 100 == 0:
                            print(f"[{success_count:6d}] RTT: {rtt_time:.3f} ms", end='\r')
                    else:
                        error_count += 1
                        if error_count % 10 == 0:
                            print(f"\n序号不匹配: 期望 {seq_num}, 收到 {recv_seq}")
                else:
                    error_count += 1
                    if error_count % 10 == 0:
                        print(f"\n校验和错误: 期望 {calc_checksum:02X}, 收到 {recv_checksum:02X}")
            else:
                timeout_count += 1
                if timeout_count % 10 == 0:
                    print(f"\n超时: 期望 {len(packet)} 字节, 收到 {len(response)} 字节")
            
            # 控制发送间隔
            elapsed = (time.time() - send_time) * 1_000_000  # 转换为微秒
            if elapsed < SEND_INTERVAL:
                time.sleep((SEND_INTERVAL - elapsed) / 1_000_000)
        
        ser.close()
        print_statistics()
        
    except serial.SerialException as e:
        print(f"\n串口错误: {e}")
        sys.exit(1)
    except KeyboardInterrupt:
        print_statistics()
        sys.exit(0)
    except Exception as e:
        print(f"\n未知错误: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)

if __name__ == '__main__':
    main()
PYEOF
}

# 运行测试
run_test() {
    echo -e "${YELLOW}开始实时性测试...${NC}"
    echo "测试参数:"
    echo "  串口设备: $SERIAL_DEVICE"
    echo "  波特率: $BAUD_RATE"
    echo "  数据大小: $DATA_SIZE 字节"
    echo "  测试时长: $TEST_DURATION 秒"
    echo "  发送间隔: $SEND_INTERVAL 微秒"
    echo "  日志文件: $LOG_FILE"
    echo ""
    
    # 创建测试程序
    create_test_program
    
    # 运行测试（重定向到日志文件）
    export SERIAL_DEVICE="$SERIAL_DEVICE"
    export BAUD_RATE="$BAUD_RATE"
    export DATA_SIZE="$DATA_SIZE"
    export TEST_DURATION="$TEST_DURATION"
    export SEND_INTERVAL="$SEND_INTERVAL"
    
    python3 /tmp/latency_test.py 2>&1 | tee "$LOG_FILE"
    
    echo ""
    echo -e "${GREEN}测试完成！日志已保存到: $LOG_FILE${NC}"
}

# 主函数
main() {
    echo "=========================================="
    echo "  串口服务器实时性测试脚本"
    echo "=========================================="
    echo ""
    
    check_dependencies
    configure_serial
    run_test
}

# 运行主函数
main