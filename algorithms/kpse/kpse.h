#ifndef KPSE_H
#define KPSE_H

#include <algorithm>
#include <utility>
#include <string.h>
#include <cmath>
#include <stdexcept>
#include <vector>

#include "../../utils/MurmurHash3.h"

typedef std::pair<uint32_t, uint32_t> pii;

// calculate SUM of bitmaps and estimate K-persistent spread
class CALC_KPSE {
   public:
    uint8_t k;
    uint8_t t;          // period
    uint32_t m;         // bitmap length
    uint8_t** bitmaps;  // t period bitmaps

    double* V;   // ratio of 0, 1, 2, ..., t

   public:
    CALC_KPSE(uint8_t t, uint8_t k, uint32_t m, uint8_t** bitmaps) {
        this->t = t;
        this->k = k;
        this->m = m;
        this->bitmaps = bitmaps;

        this->V = new double[t + 1];
        memset(this->V, 0, (t + 1) * sizeof(double));
    }

    CALC_KPSE(const CALC_KPSE&) = delete;
    CALC_KPSE& operator=(const CALC_KPSE&) = delete;

    ~CALC_KPSE() {
        delete[] this->V;
    }

    double C(int y, int x) {
        if (x < 0 || x > y) {
            return 0.0;
        }
        x = std::min(x, y - x);
        long double result = 1.0;
        for (int i = 1; i <= x; ++i) {
            result *= static_cast<long double>(y - x + i);
            result /= static_cast<long double>(i);
        }
        return static_cast<double>(result);
    }

    void sum_bitmaps() {

        memset(V, 0, (t + 1) * sizeof(double));

        for (uint32_t i = 0; i < m; i++) {
            uint32_t idx1 = i / 8;
            uint32_t idx2 = i % 8;

            uint8_t vals = 0;
            for (uint32_t j = 0; j < t; j++) {
                uint8_t val = (bitmaps[j][idx1] >> (7 - idx2)) & 1;
                vals += val;
            }

            V[vals] += 1;
        }

        for (uint32_t i = 0; i <= t; i++) {
            // printf("%.1f(%.3f) ", V[i], V[i] / m);
            V[i] /= m;
        }
        // printf("\n");
    }

    double total_kps() {
        if (!(V[0] > 0.0 && V[0] <= 1.0)) {
            throw std::runtime_error("KPSE virtual bitmap is saturated");
        }
        double N = log(V[0]) / log(1 - 1.0 / m);

        std::vector<double> nl(k, 0.0);

        std::vector<double> prj(k, 0.0);
        prj[0] = V[0];

        // printf("N: %.3f\n", N);

        for (int j = 1; j <= k - 1; j++) {
            double a_p = 0, b_p = 0;
            double a = 0, b = 0, c = 0;
            for (int l = 0; l <= j - 1; l++) {
                a_p += C(j, l) * prj[l];
            }
            for (int l = 1; l <= j - 1; l++) {
                b_p += nl[l];
                c += nl[l] * log(1 - (C(t, l) - C(j, l)) / (m * C(t, l)));
            }
            a = log(V[j] / C(t, j) + a_p);
            b = (N - b_p) * log(1 - 1.0 / m);

            // printf("a_p: %.3f b_p: %.3f a: %.3f b: %.3f c: %.3f ", a_p, b_p, a, b, c);

            double n_j = (a - b - c) / (log(1 - (C(t, j) - 1) / (m * C(t, j))) - log(1 - 1.0 / m));
            nl[j] = n_j;

            double temp = 1;
            for (int l = 1; l <= j; l++) {
                temp *= pow((1 - (C(t, l) - C(j, l)) / (m * C(t, l))), nl[l]);
            }
            prj[j] = pow(1 - 1.0 / m, (N - b_p - nl[j])) * temp - a_p;

            // printf("n_%d: %.3f ", j, nl[j]);
            // printf("prj_%d: %.3f\n", j, prj[j]);
        }

        double n = N;
        for (int j = 1; j <= k - 1; j++) {
            n -= nl[j];
        }
        if (!std::isfinite(n)) {
            throw std::runtime_error("KPSE estimate is not finite");
        }
        return n;
    }
};


// K-persistent spread estimation Algorithm
class KPSE {
   public:
    uint8_t k;
    uint8_t t;          
    uint32_t m;         
    uint32_t u;         
    uint8_t** bitmaps;  

    KPSE(uint8_t t, uint8_t k, uint32_t m, uint32_t u) {
        this->t = t;
        this->k = k;
        this->m = m;
        this->u = u;

        uint32_t len_bitmap = static_cast<uint32_t>(
            (static_cast<uint64_t>(u) + 7) / 8);

        this->bitmaps = new uint8_t*[t];
        for (int i = 0; i < t; i++) {
            bitmaps[i] = new uint8_t[len_bitmap];
            memset(bitmaps[i], 0, len_bitmap * sizeof(uint8_t));
        }
    }

    KPSE(const KPSE&) = delete;
    KPSE& operator=(const KPSE&) = delete;

    ~KPSE() {
        for (int i = 0; i < t; i++) {
            delete[] this->bitmaps[i];
        }
        delete[] this->bitmaps;
    }

    uint32_t H(uint32_t t, uint32_t s = 31) {
        uint32_t hash_val = 0;
        char hash_input_str[5] = {0};
        memcpy(hash_input_str, &t, sizeof(uint32_t));
        MurmurHash3_x86_32(hash_input_str, 4, s, &hash_val);
        return hash_val;
    }

    void insert(uint32_t t, uint32_t f, uint32_t e) {
        uint32_t hash_val = H(f ^ H(H(e) % m)) % u;
        uint32_t idx1 = hash_val / 8;
        uint32_t idx2 = hash_val % 8;
        bitmaps[t][idx1] |= (1 << (7 - idx2));
    }

    void get_bitmaps(uint32_t f, uint8_t** flow) {
        for (int i = 0; i < t; i++) {
            for (uint32_t j = 0; j < m; j++) {
                uint32_t idx1 = j / 8;
                uint32_t idx2 = j % 8;

                uint32_t hash_val = H(f ^ H(j)) % u;
                uint32_t idx11 = hash_val / 8;
                uint32_t idx12 = hash_val % 8;

                uint32_t val = (bitmaps[i][idx11] >> (7 - idx12)) & 1;

                flow[i][idx1] |= (val << (7 - idx2));
            }
        }
    }
};

#endif
