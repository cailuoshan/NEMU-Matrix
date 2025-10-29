import re

# 将 addr 对齐到 cacheline，并且提取 bank index
def align_addr_bankindex(addr):
    # addr[8:6] = bank_idx, addr[5:0] = offset => 0
    # 取十六进制地址字符串的后三位，转化为数值
    assert(len(addr) >= 3)
    last_3_digits = int(addr[-3:], 16)

    # 将最低6个二进制位清0
    cleared_bits = last_3_digits & ~0x3F
    
    # 获取 bank_idx
    bank_idx = (last_3_digits & 0x1C0) >> 6

    # 转化为字符串并拼接回地址
    line_address = addr[:-3] + f"{cleared_bits:03x}"

    return line_address, bank_idx

# 1. 转化为 cacheline 地址，并且如果该 cacheline 地址和上一个相同（且读/写也相同），则跳过该地址
# 2. 如果读到了 mst c，添加 fence 标记
# 3. 给所有 mld c 添加一个标记
def convert_to_cacheline_dedup(input_file_path, output_file_path):
    addr_line = re.compile(r'[2sf]([rw]) (0x[0-9a-fA-F]+)')
    last_addr = None
    last_op = None
    is_loadC = 0

    with open(input_file_path, "r") as input_file, open(output_file_path, "w") as output_file:
        for line in input_file:
            is_addr = addr_line.match(line)

            if is_addr:
                operation = is_addr.group(1)
                address = is_addr.group(2)

                line_address, bank_idx = align_addr_bankindex(address)

                # 写入输出文件
                if last_addr != line_address or last_op != operation:
                    output_file.write(f"{'m' if is_loadC else operation} {line_address} {bank_idx}\n")
                    # all_addrs.add(line_address)

                last_addr = line_address
                last_op = operation

            
            else:
                # 如果是 mld c 的开始，设置 is_loadC 标志； 结束时清除
                if re.compile(r'!!!! mld c START').match(line):
                    is_loadC = 1
                if re.compile(r'!!!! mld c END').match(line):
                    is_loadC = 0
                # 如果是 mst c 的开始，添加 fence 标记
                if re.compile(r'!!!! mst c START').match(line):
                    for x in range(8):
                        output_file.write(f"f 0x000000000000 {x}\n")
                
                # NO 如果没有匹配到，则直接写入输出文件
                output_file.write(line)


# def add_preload(input_file_path):
#     # 打开文件，读取所有内容
#     with open(input_file_path, 'r') as file:
#         original_content = file.read()
#     # 在内容前添加 preload 地址序列
#     with open(input_file_path, 'w') as file:
#         for addr in sorted(all_addrs):
#             line_address, bank_idx = align_addr_bankindex(addr)
#             file.write(f"p {line_address} {bank_idx}\n")
#         file.write(original_content)

# TODO: add multithread processing for speedup?
convert_to_cacheline_dedup("stderr.txt", "line_trace.txt")
# print(f"总共有 {len(all_addrs)} 个地址")
# print(sorted(all_addrs)[0:9])
# add_preload("line_trace.txt")