// Shasta.
#include "Assembler.hpp"
#include "Align4.hpp"
#include "MemoryMappedAllocator.hpp"
#include "orderPairs.hpp"
using namespace shasta;

// Standard library.
#include "array.hpp"


// Python-callable version.
void Assembler::alignOrientedReads4(
    ReadId readId0, Strand strand0,
    ReadId readId1, Strand strand1,
    uint64_t deltaX,
    uint64_t deltaY,
    uint64_t minEntryCountPerCell,
    uint64_t maxDistanceFromBoundary,
    uint64_t minAlignedMarkerCount,
    double minAlignedFraction,
    uint64_t maxSkip,
    uint64_t maxDrift,
    uint64_t maxTrim,
    uint64_t maxBand,
    int64_t matchScore,
    int64_t mismatchScore,
    int64_t gapScore) const
{
    // Fill in the options.
    Align4::Options options;
    options.deltaX = deltaX;
    options.deltaY = deltaY;
    options.minEntryCountPerCell = minEntryCountPerCell;
    options.maxDistanceFromBoundary = maxDistanceFromBoundary;
    options.minAlignedMarkerCount = minAlignedMarkerCount;
    options.minAlignedFraction = minAlignedFraction;
    options.maxSkip = maxSkip;
    options.maxDrift = maxDrift;
    options.maxTrim = maxTrim;
    options.maxBand = maxBand;
    options.matchScore = matchScore;
    options.mismatchScore = mismatchScore;
    options.gapScore = gapScore;

    // Set up the memory allocator.
    MemoryMapped::ByteAllocator byteAllocator(
        largeDataName("tmp-ByteAllocator"), largeDataPageSize, 2ULL * 1024 * 1024 * 1024);


    // Compute the alignment.
    Alignment alignment;
    AlignmentInfo alignmentInfo;
    const bool debug = true;
    alignOrientedReads4(
        OrientedReadId(readId0, strand0),
        OrientedReadId(readId1, strand1),
        options, byteAllocator, alignment, alignmentInfo, debug);
    cout << "The alignment has " << alignmentInfo.markerCount << " markers." << endl;

}



// Intermediate level version used by the http server.
void Assembler::alignOrientedReads4(
    OrientedReadId orientedReadId0,
    OrientedReadId orientedReadId1,
    uint64_t deltaX,
    uint64_t deltaY,
    uint64_t minEntryCountPerCell,
    uint64_t maxDistanceFromBoundary,
    uint64_t minAlignedMarkerCount,
    double minAlignedFraction,
    uint64_t maxSkip,
    uint64_t maxDrift,
    uint64_t maxTrim,
    uint64_t maxBand,
    int64_t matchScore,
    int64_t mismatchScore,
    int64_t gapScore,
    Alignment& alignment,
    AlignmentInfo& alignmentInfo
    ) const
{
    // Fill in the options.
    Align4::Options options;
    options.deltaX = deltaX;
    options.deltaY = deltaY;
    options.minEntryCountPerCell = minEntryCountPerCell;
    options.maxDistanceFromBoundary = maxDistanceFromBoundary;
    options.minAlignedMarkerCount = minAlignedMarkerCount;
    options.minAlignedFraction = minAlignedFraction;
    options.maxSkip = maxSkip;
    options.maxDrift = maxDrift;
    options.maxTrim = maxTrim;
    options.maxBand = maxBand;
    options.matchScore = matchScore;
    options.mismatchScore = mismatchScore;
    options.gapScore = gapScore;

    // Set up the memory allocator.
    MemoryMapped::ByteAllocator byteAllocator(
        largeDataName("tmp-ByteAllocator"), largeDataPageSize, 2ULL * 1024 * 1024 * 1024);


    // Compute the alignment.
    const bool debug = false;
    alignOrientedReads4(
        orientedReadId0, orientedReadId1,
        options, byteAllocator, alignment, alignmentInfo, debug);

}



// Align two reads using alignment method 4.
void Assembler::alignOrientedReads4(
    OrientedReadId orientedReadId0,
    OrientedReadId orientedReadId1,
    const Align4::Options& options,
    MemoryMapped::ByteAllocator& byteAllocator,
    Alignment& alignment,
    AlignmentInfo& alignmentInfo,
    bool debug) const
{
    // Access the markers for the two oriented reads.
    array<span<const CompressedMarker>, 2> orientedReadMarkers;
    orientedReadMarkers[0] = markers[orientedReadId0.getValue()];
    orientedReadMarkers[1] = markers[orientedReadId1.getValue()];



    // Align4 needs markers sorted by KmerId.
    // Use the ones from sortedMarkers if available, or else compute them.
    array<span< const pair<KmerId, uint32_t> >, 2> orientedReadSortedMarkersSpans;
    array<vector< pair<KmerId, uint32_t> >, 2> orientedReadSortedMarkers;
    if(sortedMarkers.isOpen()) {

        // Make the spans point to the stored sorted markers.
        if(debug) {
            cout << "Using stored sorted markers." << endl;
        }
        orientedReadSortedMarkersSpans[0] = sortedMarkers[orientedReadId0.getValue()];
        orientedReadSortedMarkersSpans[1] = sortedMarkers[orientedReadId1.getValue()];

    } else {

        // We don't have the sorted markers. we have to compute them here.
        if(debug) {
            cout << "Stored sorted markers are not available - computing them." << endl;
        }

        for(uint64_t i=0; i<2; i++) {

            // Unsorted markers for this oriented read.
            const span<const CompressedMarker>& um = orientedReadMarkers[i];

            // Sorted markers for this oriented read.
            vector<pair<KmerId, uint32_t> >& sm = orientedReadSortedMarkers[i];

            // Copy the unsorted markers.
            const uint64_t n = um.size();
            sm.resize(n);
            for(uint64_t ordinal=0; ordinal<n; ordinal++) {
                const CompressedMarker& cm = um[ordinal];
                sm[ordinal] = make_pair(cm.kmerId, uint32_t(ordinal));
            }

            // Sort them.
            sort(sm.begin(), sm.end(), OrderPairsByFirstOnly<KmerId, uint32_t>());

            // Make the span point to the data in the vector.
            const pair<KmerId, uint32_t> * const smBegin = &sm.front();
            orientedReadSortedMarkersSpans[i] =
                span< const pair<KmerId, uint32_t> >(smBegin, smBegin + n);
        }
    }



    // Compute the alignment.
    Align4::align(orientedReadMarkers, orientedReadSortedMarkersSpans,
        options, byteAllocator, alignment, alignmentInfo, debug);
}



// Compute sorted markers for all oriented reads.
void Assembler::computeSortedMarkers(uint64_t threadCount)
{
    // Check that we have what we need.
    checkMarkersAreOpen();
    const uint64_t orientedReadCount = markers.size();
    SHASTA_ASSERT(markerKmerIds.isOpen());
    SHASTA_ASSERT(markerKmerIds.size() == orientedReadCount);

    // Adjust the numbers of threads, if necessary.
    if(threadCount == 0) {
        threadCount = std::thread::hardware_concurrency();
    }

    // Do it.
    sortedMarkers.createNew(largeDataName("SortedMarkers"), largeDataPageSize);
    for(uint64_t i=0; i<orientedReadCount; i++) {
        sortedMarkers.appendVector(markers[i].size());
    }
    const uint64_t batchSize = 100;
    setupLoadBalancing(orientedReadCount, batchSize);
    runThreads(&Assembler::computeSortedMarkersThreadFunction, threadCount);
}



void Assembler::computeSortedMarkersThreadFunction(size_t threadId)
{
    // Loop over all batches assigned to this thread.
    uint64_t begin, end;
    while(getNextBatch(begin, end)) {

        // Loop over oriented reads in this batch.
        for(uint64_t i=begin; i!=end; i++) {

            // Access the marker KmerIs and sorted markers for this oriented read.
            const auto kmerIds = markerKmerIds[i];
            const uint64_t markerCount = kmerIds.size();
            const span< pair<KmerId, uint32_t> > sm = sortedMarkers[i];
            SHASTA_ASSERT(sm.size() == markerCount);

            // Copy the KmerId's and ordinals.
            for(uint32_t ordinal=0; ordinal<markerCount; ordinal++) {
                auto& p = sm[ordinal];
                p.first = kmerIds[ordinal];
                p.second = ordinal;
            }

            // Sort them by KmerId.
            sort(sm.begin(), sm.end(), OrderPairsByFirstOnly<KmerId, uint32_t>());
        }
    }

}



#if 0
void Assembler::computeSortedMarkersThreadFunction1(size_t threadId)
{
    // Loop over all batches assigned to this thread.
    uint64_t begin, end;
    while(getNextBatch(begin, end)) {

        // Loop over oriented reads in this batch.
        for(uint64_t i=begin; i!=end; i++) {

            // Set the number of sorted markers for this oriented read.
            // There is no need to use the multithreaded version
            // as only one thread works on each oriented read.
            sortedMarkers.incrementCount(i, markers.size(i));
        };
    }
}



void Assembler::computeSortedMarkersThreadFunction2(size_t threadId)
{
    // Loop over all batches assigned to this thread.
    uint64_t begin, end;
    while(getNextBatch(begin, end)) {

        // Loop over oriented reads in this batch.
        for(uint64_t i=begin; i!=end; i++) {

            // Access the markers and sorted markers for this oriented read.
            const span<CompressedMarker> m = markers[i];
            const span< pair<KmerId, uint32_t> > sm = sortedMarkers[i];
            const uint64_t markerCount = m.size();
            SHASTA_ASSERT(sm.size() == markerCount);

            // Copy the KmerId's and ordinals.
            for(uint32_t ordinal=0; ordinal<markerCount; ordinal++) {
                auto& p = sm[ordinal];
                p.first = m[ordinal].kmerId;
                p.second = ordinal;
            }

            // Sort them by KmerId.
            sort(sm.begin(), sm.end(), OrderPairsByFirstOnly<KmerId, uint32_t>());
        }
    }

}
#endif



bool Assembler::accessSortedMarkers()
{
    try {
        sortedMarkers.accessExistingReadOnly(largeDataName("SortedMarkers"));
        return true;
    } catch(exception&) {
        return false;
    }
}


