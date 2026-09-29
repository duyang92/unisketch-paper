#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <type_traits>

#include "../algorithms/kpse/kpse.h"

int main() {
    uint8_t empty_bitmap = 0;
    uint8_t* empty_bitmaps[] = {&empty_bitmap};
    CALC_KPSE combinations(1, 1, 8, empty_bitmaps);
    if (combinations.C(20, 10) != 184756.0 ||
        combinations.C(30, 15) != 155117520.0) {
        return 1;
    }

    uint8_t full_bitmap = 0xff;
    uint8_t* full_bitmaps[] = {&full_bitmap};
    CALC_KPSE saturated(1, 1, 8, full_bitmaps);
    saturated.sum_bitmaps();
    try {
        (void)saturated.total_kps();
        return 2;
    } catch (const std::runtime_error&) {
    }

    static_assert(!std::is_copy_constructible<CALC_KPSE>::value,
                  "CALC_KPSE must not copy owned storage");
    static_assert(!std::is_copy_assignable<CALC_KPSE>::value,
                  "CALC_KPSE must not copy owned storage");
    static_assert(!std::is_copy_constructible<KPSE>::value,
                  "KPSE must not copy owned storage");
    static_assert(!std::is_copy_assignable<KPSE>::value,
                  "KPSE must not copy owned storage");
    return 0;
}
