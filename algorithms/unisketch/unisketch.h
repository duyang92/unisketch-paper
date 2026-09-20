#ifndef UNISKETCH_H
#define UNISKETCH_H

#include <limits.h>
#include <string.h>
#include <cmath>
#include <functional>
#include <iostream>
#include <random>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../../utils/Bucket.h"
#include "../../utils/MurmurHash3.h"

class uniSketch {
    typedef std::pair<uint32_t, uint32_t> pii;

   public:
    int m;
    int l;
    int m_;
    int s;
    int bucket_size;

    uint16_t* M = nullptr;
    uint16_t* mask = nullptr;

    Bucket** B = nullptr;
    uint32_t* U0s = nullptr;

    double p;
    double ns;

    uint32_t* hash_seeds = nullptr;

    std::function<double(double)> pLambda;
    double u;
    int level;

    uint32_t H(uint32_t t, uint32_t s = 31) {
        uint32_t hash_val = 0;
        char hash_input_str[5] = {0};
        memcpy(hash_input_str, &t, sizeof(uint32_t));
        MurmurHash3_x86_32(hash_input_str, 4, s, &hash_val);
        return hash_val;
    }

    uint32_t H64(uint32_t t1, uint32_t t2, uint32_t s = 31) {
        uint32_t hash_val = 0;
        uint64_t t = ((uint64_t)t1) << 32 | t2;
        char hash_input_str[9] = {0};
        memcpy(hash_input_str, &t, sizeof(uint64_t));
        MurmurHash3_x86_32(hash_input_str, 8, s, &hash_val);
        return hash_val;
    }

    uniSketch(int m, int l, int s, int m_, int level, int bucket_size, uint32_t* hash_seeds_)
        : m(m), l(l), s(s), m_(m_), level(level), bucket_size(bucket_size) {
        hash_seeds = hash_seeds_;

        M = new uint16_t[m];
        memset(M, 0, m * sizeof(uint16_t));

        mask = new uint16_t[16];
        memset(mask, 0, 16 * sizeof(uint16_t));
        for (size_t i = 0; i < 16; ++i) {
            mask[i] = 1 << (15 - i);
        }

        U0s = new uint32_t[l];
        memset(U0s, 0, l * sizeof(uint32_t));

        B = new Bucket*[m_];
        for (size_t i = 0; i < m_; ++i) {
            B[i] = new Bucket[bucket_size];
            memset(B[i], 0, bucket_size * sizeof(Bucket));
        }

        p = 1 - 1.0 / (1 << l);
        ns = 0;
        u = 2.6744;

        if (level == 4) {
            pLambda = [](double x) { return 0.5 * exp(-x / 2) + 0.25 * exp(-x / 4) + 0.125 * exp(-x / 8) + 0.0625 * exp(-x / 16); };
        } else if (level == 3) {
            pLambda = [](double x) { return 0.5 * exp(-x / 2) + 0.25 * exp(-x / 4) + 0.125 * exp(-x / 8); };
        } else if (level == 5) {
            pLambda = [](double x) { return 0.5 * exp(-x / 2) + 0.25 * exp(-x / 4) + 0.125 * exp(-x / 8) + 0.0625 * exp(-x / 16) + 0.03125 * exp(-x / 32); };
        } else if (level == 6) {
            pLambda = [](double x) { return 0.5 * exp(-x / 2) + 0.25 * exp(-x / 4) + 0.125 * exp(-x / 8) + 0.0625 * exp(-x / 16) + 0.03125 * exp(-x / 32) + 0.015625 * exp(-x / 64); };
        }
    }

    ~uniSketch() {
        if (M != nullptr) {
            delete[] M;
        }

        if (U0s != nullptr) {
            delete[] U0s;
        }

        if (mask != nullptr) {
            delete[] mask;
        }

        if (B != nullptr) {
            for (size_t i = 0; i < m_; ++i) {
                delete[] B[i];
            }
            delete[] B;
        }
    }

    void insert(uint32_t& flow, uint32_t& element) {
        uint32_t fix_hash = H64(flow, element, hash_seeds[1]);

        uint32_t q = fix_hash;

        int v = __builtin_clz(q);

        uint32_t virtual_s1_index = fix_hash % s;
        uint32_t i = H64(flow, virtual_s1_index, hash_seeds[4]) % m;

        double delta = 0;
        if (v < l && (M[i] & mask[v]) == 0) {
            size_t j = H(flow, hash_seeds[0]) % m_;
            int bucket_i = isInBucket(flow, j);
            if (bucket_i != -1) {
                delta = VM(B[j][bucket_i].C, i, v);
                B[j][bucket_i].C += delta;
                B[j][bucket_i].F = flow;
            } else {
                int small_i = findSmallBucket(j);
                delta = VM(0, i, v);
                double r = (fix_hash & 0xffff) / 65535.0;
                if (delta * B[j][small_i].C != 0 && r <= delta / B[j][small_i].C) {
                    B[j][small_i].F = flow;
                }
            }
        }
    }

    double query_per_flow(const uint32_t& flow) {
        uint32_t j = H(flow, hash_seeds[0]) % m_;
        for (size_t i = 0; i < bucket_size; ++i) {
            if (B[j][i].F == flow) {
                return std::max(1.0, (double)(B[j][i].C));
            }
        }
        int small_i = findSmallBucket(j);
        return std::max(1.0, std::min(query_MRBM(flow), (double)B[j][small_i].C));
    }

    int isInBucket(const uint32_t& f, int j) {
        for (size_t i = 0; i < bucket_size; ++i) {
            if (B[j][i].F == f || B[j][i].F == 0) {
                return i;
            }
        }
        return -1;
    }

    double VM(int nf, size_t i, int v) {
        double delta = 0;
        int gama = std::ceil(std::log2(std::max(1.0, std::ceil((nf + ns) / (u * s)))));
        if (gama <= v && v < gama + level) {
            double x = (nf + ns) / ((1 << gama) * s);
            double p = pLambda(x);
            delta = 1.0 / p * (1 << gama);
        }
        ns += 1.0 * s / m / p;
        p -= 1.0 / (1<<(v+1)) / m;
        M[i] |= mask[v];
        return delta;
    }

    int findSmallBucket(int j) {
        int temp_C = 1e9;
        int temp_i = -1;
        for (size_t i = 0; i < bucket_size; ++i) {
            if (B[j][i].C < temp_C) {
                temp_C = B[j][i].C;
                temp_i = i;
            }
        }
        return temp_i;
    }

    double query_MRBM(const uint32_t& flow) {
        memset(U0s, 0, l * sizeof(uint32_t));

        for (size_t virtual_i = 0; virtual_i < s; ++virtual_i) {
            size_t i = H64(flow, virtual_i, hash_seeds[4]) % m;
            uint16_t cur = M[i];
            for (size_t j = 0; j < l; ++j) {
                if ((cur & mask[j]) == 0) {
                    U0s[j]++;
                }
            }
        }

        double sum_n = 0;
        int gama = -1;
        for (size_t i = 0; i < l; ++i) {
            double cur = 1.0 * U0s[i] / s;
            if (cur >= 0.2) {
                sum_n += -s * std::log(cur);
                if (gama == -1) {
                    gama = i;
                }
            }
        }
        double hat_n = sum_n * (1 << gama);
        hat_n -= ns;
        return std::max(1.0, hat_n);
    }

    std::unordered_map<uint32_t, double> query_top_k() {
        std::unordered_map<uint32_t, double> top_k_table;
        for (size_t i = 0; i < m_; ++i) {
            for (size_t j = 0; j < bucket_size; ++j) {
                if (B[i][j].F != 0) {
                    top_k_table[B[i][j].F] = std::max(1.0, (double)(B[i][j].C));
                }
            }
        }

        return top_k_table;
    }

    void get_layer_bitmap(int layer, uint8_t* bitmap) {
        uint32_t val = mask[layer], offset = 15 - layer;
        for (int i = 0; i < m; i++) {
            uint32_t idx1 = i / 8;
            uint32_t idx2 = i % 8;
            bitmap[idx1] |= ((M[i] & val) >> offset) << idx2;
        }
    }

    void get_layer_flow_bitmap(int layer, uint32_t flow, uint8_t* bitmap) {
        uint32_t val = mask[layer], offset = 15 - layer;
        for (size_t virtual_i = 0; virtual_i < s; ++virtual_i) {
            size_t i = H(flow ^ virtual_i, hash_seeds[4]) % m;

            uint32_t idx1 = virtual_i / 8;
            uint32_t idx2 = virtual_i % 8;

            bitmap[virtual_i] |= ((M[i] & val) >> offset) << idx2;
        }
    }
};

#endif
