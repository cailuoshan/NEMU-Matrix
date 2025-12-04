#include "cachesim.h"
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>

// 辅助函数，用于计算以2为底的对数
static uint64_t log2_int(uint64_t n) {
    if (n == 0) return 0;
    uint64_t count = 0;
    while ((n = n >> 1) > 0) {
        count++;
    }
    return count;
}

// 新增：从地址中抽取 index (set)
static uint64_t extract_index(CacheSimulator* sim, uint64_t address) {
    if (sim->index_bits == 0) return 0;
    uint64_t index_mask = ((1ULL << sim->index_bits) - 1) << sim->offset_bits;
    return (address & index_mask) >> sim->offset_bits;
}

// 新增：从地址中抽取 tag
static uint64_t extract_tag(CacheSimulator* sim, uint64_t address) {
    return address >> (sim->offset_bits + sim->index_bits);
}

// 新增：根据 tag、index、offset 重建完整物理地址
// 返回值：完整的 64-bit 物理地址
static uint64_t reconstruct_address(CacheSimulator* sim, uint64_t tag, uint64_t index, uint64_t offset) {
    // 保护性处理，避免位移超出 64 位，并对 tag/index/offset 做掩码
    uint64_t offset_mask = (1ULL << sim->offset_bits) - 1;
    uint64_t index_mask  = (1ULL << sim->index_bits)  - 1;
    uint64_t tag_bits = 64 - sim->offset_bits - sim->index_bits;
    uint64_t tag_mask = (1ULL << tag_bits) - 1;

    offset &= offset_mask;
    index  &= index_mask;
    tag    &= tag_mask;

    // 组合： [tag][index][offset]
    uint64_t addr = (tag << (sim->offset_bits + sim->index_bits))
                  | (index << sim->offset_bits)
                  | offset;
    return addr;
}

// 辅助函数：在LRU队列中更新一个way的位置到MRU（最前）
static void update_lru(CacheSimulator* sim, uint64_t index, uint64_t hit_way) {
    uint64_t* lru_queue = sim->lru_queues[index];
    uint64_t way_pos = -1;

    // 找到hit_way在LRU队列中的位置
    for (uint64_t i = 0; i < sim->num_ways; ++i) {
        if (lru_queue[i] == hit_way) {
            way_pos = i;
            break;
        }
    }
    
    // 将way_pos之前的元素向后移动一位
    for (uint64_t i = way_pos; i > 0; --i) {
        lru_queue[i] = lru_queue[i - 1];
    }

    // 将hit_way放到队列头部 (MRU)
    lru_queue[0] = hit_way;
}

// 辅助：将指定 way 设置为 LRU （将该 way 移到该组的 LRU 队尾）
static void set_way_as_lru(CacheSimulator* sim, uint64_t index, uint64_t way) {
	// ...假设 sim 非空且 index/way 合法...
	int64_t pos = -1;
	uint64_t *lru = sim->lru_queues[index];

	for (uint64_t i = 0; i < sim->num_ways; ++i) {
		if (lru[i] == way) {
			pos = (int64_t)i;
			break;
		}
	}
	if (pos == -1) return;

	// 将 pos 之后的元素左移一位，最终把 way 放到队尾（LRU）
	for (uint64_t i = (uint64_t)pos; i + 1 < sim->num_ways; ++i) {
		lru[i] = lru[i + 1];
	}
	lru[sim->num_ways - 1] = way;
}

// 辅助函数：在指定组中查找给定 tag 的 way，找到返回 way（>=0），找不到返回 -1
static int64_t find_hit_way(CacheSimulator* sim, uint64_t index, uint64_t tag) {
    CacheLine* set = sim->cache_store[index];
    for (uint64_t i = 0; i < sim->num_ways; ++i) {
        if (set[i].valid && set[i].tag == tag) {
            return (int64_t)i;
        }
    }
    return -1;
}

CacheSimulator* cache_simulator_create(uint64_t sets, uint64_t ways, uint64_t cacheline_size, bool coherent) {
    // 1. 参数合法性检查
    if (sets == 0 || ways == 0 || cacheline_size == 0 ||
        (sets & (sets - 1)) != 0 || (cacheline_size & (cacheline_size - 1)) != 0) {
        fprintf(stderr, "错误: 缓存组数和缓存行大小必须是2的幂，且不能为0。\n");
        return NULL;
    }

    // 2. 为主结构体分配内存
    CacheSimulator* sim = (CacheSimulator*)malloc(sizeof(CacheSimulator));
    if (!sim) return NULL;

    sim->num_sets = sets;
    sim->num_ways = ways;
    sim->cacheline_size = cacheline_size;
    sim->offset_bits = log2_int(cacheline_size);
    sim->index_bits = log2_int(sets);
    sim->coherent = coherent;

    // 3. 为缓存存储(cache_store)分配二维数组内存
    sim->cache_store = (CacheLine**)malloc(sets * sizeof(CacheLine*));
    if (!sim->cache_store) {
        free(sim);
        return NULL;
    }
    for (uint64_t i = 0; i < sets; ++i) {
        sim->cache_store[i] = (CacheLine*)calloc(ways, sizeof(CacheLine)); // calloc自动初始化为0/false
        if (!sim->cache_store[i]) {
            // 清理已分配的内存
            for(uint64_t j = 0; j < i; ++j) free(sim->cache_store[j]);
            free(sim->cache_store);
            free(sim);
            return NULL;
        }
    }

    // 4. 为LRU队列分配二维数组内存
    sim->lru_queues = (uint64_t**)malloc(sets * sizeof(uint64_t*));
    if (!sim->lru_queues) {
        // 清理cache_store
        for(uint64_t i = 0; i < sets; ++i) free(sim->cache_store[i]);
        free(sim->cache_store);
        free(sim);
        return NULL;
    }
    for (uint64_t i = 0; i < sets; ++i) {
        sim->lru_queues[i] = (uint64_t*)malloc(ways * sizeof(uint64_t));
        if (!sim->lru_queues[i]) {
            // 清理已分配的内存
            for(uint64_t j = 0; j < i; ++j) free(sim->lru_queues[j]);
            free(sim->lru_queues);
            for(uint64_t j = 0; j < sets; ++j) free(sim->cache_store[j]);
            free(sim->cache_store);
            free(sim);
            return NULL;
        }
        // 初始化LRU顺序，0, 1, 2...
        for (uint64_t j = 0; j < ways; ++j) {
            sim->lru_queues[i][j] = j;
        }
    }

    return sim;
}

// TODO-AI: Unused for now
void cache_simulator_destroy(CacheSimulator* sim) {
    if (!sim) return;

    for (uint64_t i = 0; i < sim->num_sets; ++i) {
        free(sim->cache_store[i]);
        free(sim->lru_queues[i]);
    }
    free(sim->cache_store);
    free(sim->lru_queues);
    free(sim);
}

bool cache_simulator_access(CacheSimulator* sim, uint64_t address, uint64_t* miss_address) {
    // 1. 地址分解（使用抽象的辅助函数）
    uint64_t offset_mask = (1ULL << sim->offset_bits) - 1;
    uint64_t index = extract_index(sim, address);
    uint64_t tag = extract_tag(sim, address);

    // 2. 在对应的组（set）中查找
    CacheLine* set = sim->cache_store[index];
    int64_t hit_way = find_hit_way(sim, index, tag);

    // 3. 判断命中或未命中
    if (hit_way != -1) {
        // Cache Hit (缓存命中)
        update_lru(sim, index, (uint64_t)hit_way);
        return true;
    } else {
        // Cache Miss (缓存未命中)
        *miss_address = address & ~offset_mask;

        // 找到需要被替换的way (LRU队列的末尾)
        uint64_t victim_way = sim->lru_queues[index][sim->num_ways - 1];

        if (sim->coherent && set[victim_way].valid) {
            // 如果被替换的way是有效的，说明需要写回（这里简化处理，实际可能需要更复杂的写回逻辑）
            fprintf(stderr, "EV " "0x%012lx" "\n", reconstruct_address(sim, set[victim_way].tag, index, 0));
        }

        // 更新缓存行内容
        set[victim_way].valid = true;
        set[victim_way].tag = tag;

        // 将被替换的way更新为MRU
        update_lru(sim, index, victim_way);
        return false;
    }
}

// 将指定地址从缓存中驱逐（invalidate）。找到并使对应缓存行无效则返回 true，否则返回 false。
bool cache_simulator_evict(CacheSimulator* sim, uint64_t address) {
    // 使用抽象的辅助函数替代手工掩码计算
    uint64_t index = extract_index(sim, address);
    uint64_t tag = extract_tag(sim, address);

    CacheLine* set = sim->cache_store[index];
    int64_t way = find_hit_way(sim, index, tag);
    if (way >= 0) {
        set[way].valid = false;
        set_way_as_lru(sim, index, (uint64_t)way);
        return true;
    }
    return false;
}

// AI: we should distinguish read & write Perm, record it in directory
