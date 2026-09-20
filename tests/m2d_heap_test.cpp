#include <cstdint>
#include <iostream>

#include "../algorithms/m2d/m2d.h"

int main() {
    myHeap heap(10);
    for (uint32_t flow = 100; flow < 110; ++flow) {
        heap.insert(flow, static_cast<double>(flow));
    }

    for (uint32_t flow = 100; flow < 110; ++flow) {
        const int index = heap.findFlowIndex(flow);
        if (index < 0 || heap.heap[index].second != flow) {
            std::cerr << "M2D heap lookup returned the wrong flow.\n";
            return 1;
        }
    }

    std::cout << "M2D heap test passed.\n";
    return 0;
}
