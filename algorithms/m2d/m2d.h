#ifndef M2D_H
#define M2D_H

#include <string.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <iostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <immintrin.h>

#include "../../utils/Bucket.h"
#include "../../utils/MurmurHash3.h"

class on_vhll {
    typedef std::pair<uint32_t, uint32_t> pii;

   public:
    int d;
    int w;
    uint32_t N[32] = {0};
    uint32_t** U = nullptr;
    uint32_t** M = nullptr;
    double alpha_d;
    double alpha_wd;
    uint32_t* hash_seeds = nullptr;

   public:
    uint32_t H(uint32_t t, uint32_t s = 31) {
        uint32_t hash_val = 0;
        char hash_input_str[5] = {0};
        memcpy(hash_input_str, &t, sizeof(uint32_t));
        MurmurHash3_x86_32(hash_input_str, 4, s, &hash_val);
        return hash_val;
    }

    on_vhll(int _d, int _w)
        : d(_d), w(_w) {
        N[0] = d * w;

        U = new uint32_t*[w];
        for (int i = 0; i < w; ++i) {
            U[i] = new uint32_t[32];
            memset(U[i], 0, 32 * sizeof(uint32_t));
            U[i][0] = d;
        }

        M = new uint32_t*[d];
        for (int i = 0; i < d; ++i) {
            M[i] = new uint32_t[w];
            memset(M[i], 0, w * sizeof(uint32_t));
        }

        alpha_d = 0.7213 / (1 + 1.079 / d);
        alpha_wd = 0.7213 / (1 + 1.079 / (d * w));
    }

    ~on_vhll() {
    }

    void set_hash_seeds(uint32_t* seeds) {
        hash_seeds = seeds;
    }

    void insert(pii data) {
        uint32_t flow = data.first;
        uint32_t element = data.second;

        int j = H(flow, hash_seeds[0]) % w;

        uint32_t q = H(flow ^ element, hash_seeds[1]);
        int rank = __builtin_clz(q) + 1;

        rank = std::min(rank, 31);
        int i = H(flow ^ element, hash_seeds[2]) % d;

        if (rank > M[i][j]) {
            int old_rank = M[i][j];
            U[j][old_rank] -= 1;
            U[j][rank] += 1;
            N[old_rank] -= 1;
            N[rank] += 1;
            M[i][j] = rank;
        }
    }

    double query(const uint32_t& flow) {
        double cd = 0;
        double cm = 0;
        int j = H(flow, hash_seeds[0]) % w;

        for (int x = 0; x < 32; ++x) {
            cd += U[j][x] * 1.0 / (1 << x);
            cm += N[x] * 1.0 / (1 << x);
        }

        cd = alpha_d * d * d / cd;
        cm = alpha_wd * w * d * w * d / cm;
        double cf = w / (w - 1) * (cd - 1.0 / w * cm);

        return std::max(1.0, cf);
    }
};

class myHeap {
   public:
    int heapSize, curSize;
    std::pair<double, uint32_t>* heap = nullptr;

   public:
    myHeap(int size)
        : heapSize(size) {
        heap = new std::pair<double, uint32_t>[size];
        memset(heap, 0, size * sizeof(std::pair<double, uint32_t>));

        curSize = 0;
    }

    ~myHeap() {
    }

    int findFlowIndex(const uint32_t& flow) {
        constexpr int blockSize = 8;
        const int simdEnd = curSize - curSize % blockSize;

        for (int i = 0; i < simdEnd; i += blockSize) {
            // Load 8 flow values into a SIMD register
            __m256i flowBatch = _mm256_set_epi32(
                heap[i + 7].second,
                heap[i + 6].second,
                heap[i + 5].second,
                heap[i + 4].second,
                heap[i + 3].second,
                heap[i + 2].second,
                heap[i + 1].second,
                heap[i].second);

            // Convert flow to SIMD register for comparison
            __m256i targetFlow = _mm256_set1_epi32(flow);

            // Compare flow values for equality
            __m256i cmpResult = _mm256_cmpeq_epi32(flowBatch, targetFlow);

            // Convert comparison result to bitmask
            int mask = _mm256_movemask_ps(_mm256_castsi256_ps(cmpResult));

            // Check if any of the elements match
            if (mask != 0) {
                // Find the index of the matching element
                return i + __builtin_ctz(mask);
            }
        }

        for (int i = simdEnd; i < curSize; ++i) {
            if (heap[i].second == flow) {
                return i;
            }
        }

        return -1;  // Flow not found
    }

    bool insert(const uint32_t& flow, double cf) {
        bool isInsert = false;

        int idx = findFlowIndex(flow);
        if (idx != -1) {
            heap[idx].first = cf;
            std::make_heap(heap, heap + curSize, std::less<>());
            isInsert = true;
        }

        if (!isInsert) {
            if (heapSize == curSize) {
                if (heap[0].first < cf) {
                    std::pop_heap(heap, heap + curSize, std::less<>());
                    heap[heapSize - 1] = {cf, flow};
                    std::push_heap(heap, heap + curSize, std::less<>());
                    isInsert = true;
                }
            } else {
                heap[curSize] = {cf, flow};
                std::push_heap(heap, heap + curSize + 1, std::less<>());
                curSize += 1;
                isInsert = true;
            }
        }

        return isInsert;
    }
};

class M2D {
    typedef std::pair<uint32_t, uint32_t> pii;

   public:
    int l;

    int dw_size;
    pii* dw = nullptr;

    uint32_t* hash_seeds = nullptr;

    std::vector<on_vhll> onvhlls;
    std::vector<myHeap> minHeaps;

    int heapSize;
    double ps;
    std::unordered_map<uint32_t, std::unordered_set<uint32_t>> hashTable;

   public:
    uint32_t H(uint32_t t, uint32_t s = 31) {
        uint32_t hash_val = 0;
        char hash_input_str[5] = {0};
        memcpy(hash_input_str, &t, sizeof(uint32_t));
        MurmurHash3_x86_32(hash_input_str, 4, s, &hash_val);
        return hash_val;
    }

    M2D(int _l, pii* _dw, int _dw_size, int _heapSize, double _ps, uint32_t* _hash_seeds)
        : l(_l), heapSize(_heapSize), ps(_ps), dw_size(_dw_size) {
        dw = _dw;
        hash_seeds = _hash_seeds;

        for (int i = 0; i < dw_size; ++i) {
            uint32_t c_d = dw[i].first;
            uint32_t c_w = dw[i].second;

            on_vhll temp = on_vhll(c_d, c_w);
            temp.set_hash_seeds(hash_seeds);

            onvhlls.push_back(temp);
            minHeaps.push_back(myHeap(heapSize));
        }

        minHeaps.push_back(myHeap(heapSize));
    }

    ~M2D() {
    }

    void insert(uint32_t& flow, uint32_t& element) {
        uint32_t q = H(flow, hash_seeds[3]);
        int zeros = __builtin_clz(q) + 1;
        double cf = 1.0;
        int j_ = std::min(int(zeros / (std::log2(1.0 / ps))), l);

        if (j_ == l) {
            hashTable[flow].insert(element);
            cf = hashTable[flow].size();
        } else {
            onvhlls[j_].insert({flow, element});
            cf = onvhlls[j_].query(flow);
        }

        for (int j = j_; j >= 0; --j) {
            if (!minHeaps[j].insert(flow, cf)) {
                break;
            }
        }
    }

    double query_per_flow(const uint32_t& flow) {
        uint32_t q = H(flow, hash_seeds[3]);
        int zeros = __builtin_clz(q) + 1;
        int j_ = std::min(int(zeros / (std::log2(1.0 / ps))), l);
        double cf = 1.0;
        if (j_ == l) {
            cf = hashTable[flow].size();
        } else {
            cf = onvhlls[j_].query(flow);
        }
        return cf;
    }

    std::unordered_map<uint32_t, double> query_top_k() {
        std::unordered_map<uint32_t, double> top_k_table;
        for (int j = l; j >= 0; --j) {
            for (int i = 0; i < minHeaps[j].heapSize; ++i) {
                auto [cf, flow] = minHeaps[j].heap[i];
                top_k_table[flow] = cf;
            }
        }
        return top_k_table;
    }
};

#endif
