#include "Assembler.hpp"
#include "mode3.hpp"
#include "Reads.hpp"
using namespace shasta;
using namespace mode3;



void Assembler::mode3Assembly(
    size_t threadCount)
{
    // Adjust the numbers of threads, if necessary.
    if(threadCount == 0) {
        threadCount = std::thread::hardware_concurrency();
    }

    DynamicAssemblyGraph g(
        reads->getFlags(),
        markers,
        markerGraph,
        largeDataFileNamePrefix,
        largeDataPageSize,
        threadCount);

}