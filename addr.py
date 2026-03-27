import re
import configparser
from pathlib import Path
from math import log2

# CAgent/ULAgent/MAgent 共享的 L2 配置
BlockSize = 64


def load_config():
    cfg_path = Path(__file__).resolve().parent.parent / "configuration.ini"
    parser = configparser.ConfigParser()
    parser.read(cfg_path)

    # 和 testtop 共用同一个 l2_banks 参数
    l2_n_banks = parser.getint("testtop", "l2_banks", fallback=1)
    mode = parser.get("nemu", "mode", fallback="CORE_MATRIX")

    assert l2_n_banks > 0, f"Invalid l2_banks: {l2_n_banks}"
    assert mode in ["CORE_MATRIX", "CORE_ALONE"], f"Invalid MODE {mode}"
    return l2_n_banks, mode

L2NBanks, MODE = load_config()

def get_agent_id(op, bankIdx):
    """
    根据操作类型和bank index获取agent id。
    """
    if MODE == 'CORE_ALONE':
        if op in ['LD', 'ST', 'EV', 'MR', 'MW']: # CAgent
            return 0
        elif op in ['IF', 'FR', 'WR']: # ULAgent
            return 1
        else:
            assert False, f"Unknown operation {op}"
    elif MODE == 'CORE_MATRIX':
        if op in ['LD', 'ST', 'EV']: # CAgent
            return 0
        elif op in ['IF', 'FR', 'WR']: # ULAgent
            return 1
        elif op in ['MR', 'MW']: # MAgent
            return 2 + bankIdx
        else:
            assert False, f"Unknown operation {op}"
    else:
        assert False, f"Invalid MODE {MODE}"

def get_l2_addr_agentId(op, addrStr):
    """
    获取L2缓存行地址的bank index。
    假设地址是十六进制字符串，格式为0xXXXXXX。
    """
    offsetbits = int(log2(BlockSize))  # 计算block的位数
    bankbits = int(log2(L2NBanks))  # 计算bank的位数
    addr = int(addrStr, 16)  # 将地址从十六进制字符串转换为整数

    # 地址最低的offsetbits位是block offset， 其次是bank index
    baseAddr = addr & (~((1 << offsetbits) - 1))
    bankIdx = (addr >> offsetbits) & ((1 << bankbits) - 1)

    # TODO: 将地址裁剪为 32 位
    baseAddr = baseAddr & 0xFFFFFFFF

    if MODE == 'CORE_ALONE':
        # CORE_ALONE 逻辑：MR 转 LD, MW 转 ST
        final_op = op
        if op == 'MR':
            final_op = 'LD'
        elif op == 'MW':
            final_op = 'ST'
        
        agentId = get_agent_id(op, bankIdx)
        return agentId, baseAddr, final_op
    elif MODE == 'CORE_MATRIX':
        # CORE_MATRIX 逻辑
        agentId = get_agent_id(op, bankIdx)
        return agentId, baseAddr, op
    else:
        assert False, f"Invalid MODE {MODE}"


def convert_addr(input_file, output_file):
    # 地址格式：<操作码> 0x<十六进制地址>
    # 分别是：指令读，数据读，数据写，L1D驱逐，fence，flush，writeback，miss写回
    ops = ["IF", # 指令读
           "LD", # 数据读
           "ST", # 数据写
           "EV", # L1D驱逐
           "FR", # IFetch_Read
           "WR", # Write_Read
           "MR", # 矩阵读
           "MW", # 矩阵写
           ] 

    is_loadC = 0
    with open(input_file, "r") as infile, open(output_file, "w") as outfile:
        for line in infile:
            # 如果出现任 ops 中的操作，则处理该行
            is_addr = re.search(r'(' + '|'.join(ops) + r') (0x[0-9a-fA-F]+)$', line)

            if is_addr:
                opStr = is_addr.group(1)
                addrStr = is_addr.group(2)
                agentId, baseAddr, finalOp = get_l2_addr_agentId(opStr, addrStr)
                
                # CORE_MATRIX 模式下恢复 CR 区分逻辑
                if MODE == 'CORE_MATRIX':
                    finalOp = "CR" if (opStr == "MR" and is_loadC == 1) else opStr

                outfile.write(f"{finalOp} 0x{baseAddr:012x} {agentId}\n")

                if (opStr in ["FR", "WR"]):
                    assert False, "TLB not supported yet"

            else:
                if re.compile(r'!!!! mst c (START|END)').match(line):
                    outfile.write(f"FE 0x000000000000 0\n") # 添加 fence 标记
                
                # 仅在 CORE_MATRIX 模式下追踪 C 矩阵加载状态
                if MODE == 'CORE_MATRIX':
                    if re.compile(r'!!!! mld c START').match(line):
                        is_loadC = 1
                    if re.compile(r'!!!! mld c END').match(line):
                        is_loadC = 0

                # 如果没有匹配到，则直接写入输出文件
                outfile.write(line)


convert_addr("stderr.txt", "line_trace.txt")