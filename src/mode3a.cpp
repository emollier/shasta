// Shasta.
#include "mode3a.hpp"
#include "MarkerGraph.hpp"
#include "mode3a-AssemblyGraph.hpp"
#include "mode3a-AssemblyGraphSnapshot.hpp"
#include "mode3a-BubbleCleaner.hpp"
#include "mode3a-PackedAssemblyGraph.hpp"
#include "mode3a-PackedMarkerGraph.hpp"
#include "performanceLog.hpp"
#include "Reads.hpp"
#include "SHASTA_ASSERT.hpp"
#include "timestamp.hpp"
using namespace shasta;
using namespace mode3a;

// Standard library.
#include "fstream.hpp"
#include <map>



Assembler::Assembler(
    uint64_t threadCount,
    uint64_t k, // Marker length
    const MappedMemoryOwner& mappedMemoryOwner,
    const Reads& reads,
    const MemoryMapped::VectorOfVectors<CompressedMarker, uint64_t>& markers,
    MarkerGraph& markerGraph) :
    MappedMemoryOwner(mappedMemoryOwner),
    reads(reads),
    markers(markers),
    markerGraph(markerGraph)
{
    // EXPOSE WHEN CODE STABILIZES.
#if 0
    // Jaccard graph.
    const double minJaccard = 0.65;
    const uint64_t minComponentSizeJaccard = 20;
    const uint64_t minPathLengthJaccard = 10;

    // Partial paths.
    const uint64_t segmentCoverageThreshold1ForPaths = 3;
    const uint64_t segmentCoverageThreshold2ForPaths = 6;
    const uint64_t minLinkCoverageForPaths = 3;

    const uint64_t detangleIterationCount = 1;
    // const uint64_t minDetangleCoverage = 3;

    const uint64_t minSegmentCoverageForPackedAssemblyGraph = 8;
    const uint64_t minLinkCoverage1ForPackedAssemblyGraph = 6;
    const uint64_t minLinkCoverage2ForPackedAssemblyGraph = 4;
    const uint64_t minMarkerCountForPackedAssemblyGraph = 1000;
#endif

    // This requires the marker length k to be even.
    SHASTA_ASSERT((k % 2) == 0);

    // This does not work with RLE.
    SHASTA_ASSERT(reads.representation == 0);

    // Adjust the numbers of threads, if necessary.
    if(threadCount == 0) {
        threadCount = std::thread::hardware_concurrency();
    }

    // Clear all the superbubble flags in marker graph edges -
    // just in case we already ran this before.
    for( MarkerGraph::Edge& edge: markerGraph.edges) {
        edge.isSuperBubbleEdge = 0;
    }

    // Create the initial PackedMarkerGraph.
    packedMarkerGraph = make_shared<PackedMarkerGraph>(
        MappedMemoryOwner(*this), "Mode3a-PackedMarkerGraph-Initial", k, reads, markers, markerGraph, false);
    cout << "The initial PackedMarkerGraph has " <<
        packedMarkerGraph->segments.size() << " segments, " <<
        packedMarkerGraph->links.size() << " links, and " <<
        packedMarkerGraph->totalSegmentLength() <<
        " bases of assembled sequence." << endl;
    packedMarkerGraph->writeGfa();

    // Clean up the bubbles causes by errors.
    // This keeps one branch of each bubble.
    // The marker graph edges of the remaining branches are flagged as removed.
    BubbleCleaner cleaner(*packedMarkerGraph);
    cleaner.cleanup(markerGraph);
    packedMarkerGraph->remove();
    packedMarkerGraph = 0;

    // Create the final PackedMarkerGraph.
    packedMarkerGraph = make_shared<PackedMarkerGraph>(
        MappedMemoryOwner(*this), "Mode3a-PackedMarkerGraph", k, reads, markers, markerGraph, false);
    cout << "After bubble cleanup, the PackedMarkerGraph has " <<
        packedMarkerGraph->segments.size() << " segments, " <<
        packedMarkerGraph->links.size() << " links, and " <<
        packedMarkerGraph->totalSegmentLength() <<
        " bases of assembled sequence." << endl;
    packedMarkerGraph->writeSegments();
    packedMarkerGraph->writeGfa();

    // For the final PackedMarkerGraph we also need to compute the oriented reads journeys.
    packedMarkerGraph->computeJourneys(threadCount);
    packedMarkerGraph->writeJourneys();

    // Create the AssemblyGraph.
    shared_ptr<AssemblyGraph> assemblyGraph = make_shared<AssemblyGraph>(*packedMarkerGraph);

#if 1
    AssemblyGraphSnapshot snapshot(
        *assemblyGraph,
        "Mode3a-AssemblyGraphSnapshot-0", *this);
    snapshot.write();
    /*
    assemblyGraph->computeJaccardGraph(
        threadCount,
        minJaccard,
        minComponentSizeJaccard,
        minPathLengthJaccard);
    */
#endif


#if 0
    // Detangle iterations.
    for(uint64_t detangleIteration=0; detangleIteration<detangleIterationCount; detangleIteration++) {
        performanceLog << timestamp << "Starting detangle iteration " << detangleIterationCount << endl;
        assemblyGraph->setDebugOutputPrefix("Mode3a-Iteration-" + to_string(detangleIteration) + "-");

        cout << "Before detangle iteration " << detangleIteration << " the AssemblyGraph has " <<
           num_vertices(*assemblyGraph) << " segments and " <<
           num_edges(*assemblyGraph) << " links." << endl;

        // Follow reads to compute partial paths.
        assemblyGraph->computePartialPaths(threadCount,
            segmentCoverageThreshold1ForPaths, segmentCoverageThreshold2ForPaths, minLinkCoverageForPaths);
        assemblyGraph->writePartialPaths();
        assemblyGraph->analyzePartialPaths(threadCount);

        // Find AssemblyPaths and assemble their sequence.
        assemblyGraph->computeAssemblyPaths(threadCount);
        assemblyGraph->assemble();

        // Create a snapshot of the assembly graph.
        AssemblyGraphSnapshot snapshot(
            *assemblyGraph,
            "Mode3a-AssemblyGraphSnapshot-" + to_string(detangleIteration), *this);
        snapshot.write();

        // Create a new AssemblyGraph using the TangledAssemblyPaths.
        shared_ptr<AssemblyGraph> newAssemblyGraph =
            make_shared<AssemblyGraph>(
                AssemblyGraph::DetangleUsingTangledAssemblyPaths(),
                *assemblyGraph);

#if 0
        // Create a new AssemblyGraph using tangle matrices of the current AssemblyGraph.
        shared_ptr<AssemblyGraph> newAssemblyGraph =
            make_shared<AssemblyGraph>(
                AssemblyGraph::DetangleUsingTangleMatrices(),
                *assemblyGraph, minDetangleCoverage);
#endif

#if 0
        // Create a new AssemblyGraph by local clustering on the current AssemblyGraph.
        shared_ptr<AssemblyGraph> newAssemblyGraph =
            make_shared<AssemblyGraph>(
                AssemblyGraph::DetangleUsingLocalClustering(),
                *assemblyGraph);
#endif

        // Replace the old AssemblyGraph with the new.
        // This also destroys the old AssemblyGraph.
        assemblyGraph = newAssemblyGraph;

    }

#if 0
    // Create the PackedAssemblyGraph.
    // This also stores information in the Assembly graph,
    // so the last AssembyGraphSnapshot should be done after this.
    PackedAssemblyGraph packedAssemblyGraph(
        *assemblyGraph,
        minSegmentCoverageForPackedAssemblyGraph,
        minLinkCoverage1ForPackedAssemblyGraph,
        minLinkCoverage2ForPackedAssemblyGraph,
        minMarkerCountForPackedAssemblyGraph,
        minJaccard,
        mForJaccard,
        threadCount);
#endif

    // Create a final snapshot of the assembly graph.
    cout << "The final AssemblyGraph has " <<
       num_vertices(*assemblyGraph) << " segments and " <<
       num_edges(*assemblyGraph) << " links." << endl;
    AssemblyGraphSnapshot snapshot(
        *assemblyGraph,
        "Mode3a-AssemblyGraphSnapshot-" + to_string(detangleIterationCount), *this);
    snapshot.write();
#endif

}


