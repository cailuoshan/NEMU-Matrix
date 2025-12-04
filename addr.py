import re
from math import log2

# CAgent/ULAgent/MAgent 共享的 L2 配置
BlockSize = 64
L2NBanks = 2
NumAgents = 2 + L2NBanks

def get_agent_id(op, bankIdx):
    """
    根据操作类型和bank index获取agent id。
    CAgent: 0
    ULAgent: 1
    MAgent: 2 + bankIdx
    """
    if op in ['LD', 'ST', 'EV']: # CAgent
        return 0
    elif op in ['IF', 'FR', 'WR']: # ULAgent
        return 1
    elif op in ['MR', 'MW']: # MAgent
        return 2 + bankIdx
    else:
        assert False, "Unknown operation"

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
    agentId = get_agent_id(op, bankIdx)

    return agentId, baseAddr


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
           "MW"] # 矩阵写

    with open(input_file, "r") as infile, open(output_file, "w") as outfile:
        for line in infile:
            # 如果出现任 ops 中的操作，则处理该行
            is_addr = re.search(r'(' + '|'.join(ops) + r') (0x[0-9a-fA-F]+)$', line)

            if is_addr:
                opStr = is_addr.group(1)
                addrStr = is_addr.group(2)
                agentId, baseAddr = get_l2_addr_agentId(opStr, addrStr)

                # TODO: 区分 C 矩阵读
                outfile.write(f"{opStr} 0x{baseAddr:012x} {agentId}\n")

                if (opStr in ["FR", "WR"]):
                    assert False, "TLB not supported yet"

            else:
                if re.compile(r'!!!! mst c (START|END)').match(line):
                    for x in range(NumAgents):
                        outfile.write(f"FE 0x000000000000 {x}\n") # 添加 fence 标记
                
                # 如果没有匹配到，则直接写入输出文件
                outfile.write(line)


convert_addr("stderr.txt", "line_trace.txt")