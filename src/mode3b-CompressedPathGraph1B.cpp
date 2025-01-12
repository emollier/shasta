// Shasta.
#include "mode3b-CompressedPathGraph1B.hpp"
#include "mode3b-AssemblyPath.hpp"
#include "mode3b-PathGraph1.hpp"
#include "Assembler.hpp"
#include "deduplicate.hpp"
#include "dominatorTree.hpp"
#include "enumeratePaths.hpp"
#include "findLinearChains.hpp"
#include "orderPairs.hpp"
#include "timestamp.hpp"
using namespace shasta;
using namespace mode3b;

// Boost libraries.
#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/graph/filtered_graph.hpp>
#include <boost/graph/adj_list_serialize.hpp>
#include <boost/pending/disjoint_sets.hpp>

// Standard library.
#include "fstream.hpp"
#include <queue>
#include "tuple.hpp"



void GlobalPathGraph1::assemble2(
    const Assembler& assembler,
    uint64_t threadCount0,
    uint64_t threadCount1)
{
    // PARAMETERS TO BE EXPOSED WHEN CODE STABILIZES
    const uint64_t minPrimaryCoverage = 8;
    const uint64_t maxPrimaryCoverage = 60;
    const uint64_t minEdgeCoverage = 1;
    const double minCorrectedJaccard = 0.;
    const uint64_t minComponentSize = 3;
    const uint64_t transitiveReductionDistance = 5000;
    const uint64_t transitiveReductionMaxCoverage = 100;
    const uint64_t crossEdgesLowCoverageThreshold = 2;
    const uint64_t crossEdgesHighCoverageThreshold = 6;
    const uint64_t crossEdgesMinOffset = 1000;


    GlobalPathGraph1 graph(assembler);
    graph.createVertices(minPrimaryCoverage, maxPrimaryCoverage);
    graph.computeOrientedReadJourneys();
    graph.createEdges0(1, minEdgeCoverage, minCorrectedJaccard);

    graph.createComponents(minCorrectedJaccard, minComponentSize);

    // Assemble each connected component separately.
    for(uint64_t componentId=0; componentId<graph.components.size(); componentId++) {
        graph.assemble2(
            componentId,
            transitiveReductionDistance,
            transitiveReductionMaxCoverage,
            crossEdgesLowCoverageThreshold,
            crossEdgesHighCoverageThreshold,
            crossEdgesMinOffset,
            threadCount0,
            threadCount1);
    }
}



void GlobalPathGraph1::assemble2(
    uint64_t componentId,
    uint64_t transitiveReductionDistance,
    uint64_t transitiveReductionMaxCoverage,
    uint64_t crossEdgesLowCoverageThreshold,
    uint64_t crossEdgesHighCoverageThreshold,
    uint64_t crossEdgesMinOffset,
    uint64_t threadCount0,
    uint64_t threadCount1)
{
    cout << "Assembly begins for connected component " << componentId << endl;
    PathGraph1& component = *components[componentId];

    // Local transitive reduction.
    component.localTransitiveReduction(
        transitiveReductionDistance,
        transitiveReductionMaxCoverage);

    // Remove cross-edges.
    component.removeCrossEdges(
        crossEdgesLowCoverageThreshold,
        crossEdgesHighCoverageThreshold,
        crossEdgesMinOffset);

    // Graphviz output.
    GlobalPathGraph1DisplayOptions options;
    options.showNonTransitiveReductionEdges = false;
    component.writeGraphviz(verticesVector,
        "PathGraph" + to_string(componentId), options);
    options.makeCompact();
    component.writeGraphviz(verticesVector,
        "PathGraphCompact" + to_string(componentId), options);

    CompressedPathGraph1B cGraph(component, componentId, assembler, threadCount0, threadCount1);
}



void GlobalPathGraph1::loadAndAssemble(
    const Assembler& assembler,
    const string& fileName,
    uint64_t threadCount0,
    uint64_t threadCount1)
{
    CompressedPathGraph1B cGraph(fileName, assembler,threadCount0, threadCount1);
}



// Create from a PathGraph1, then call run.
CompressedPathGraph1B::CompressedPathGraph1B(
    const PathGraph1& graph,
    uint64_t componentId,
    const Assembler& assembler,
    uint64_t threadCount0,
    uint64_t threadCount1) :
    componentId(componentId),
    assembler(assembler)
{
    create(graph);

    // Serialize it so we can restore it to facilitate debugging.
    save("CompressedPathGraph1B-" + to_string(componentId) + ".data");

    run(threadCount0, threadCount1);
}



// Load it from a binary archive, then call run.
CompressedPathGraph1B::CompressedPathGraph1B(
    const string& fileName,
    const Assembler& assembler,
    uint64_t threadCount0,
    uint64_t threadCount1) :
    assembler(assembler)
{
    load(fileName);
    run(threadCount0, threadCount1);
}



void CompressedPathGraph1B::run(
    uint64_t threadCount0,
    uint64_t threadCount1)
{
    // *** EXPOSE WHEN CODE STABILIZES
    vector< pair<uint64_t, uint64_t> > superbubbleRemovalMaxOffsets =
    {{300, 1000}, {1000, 3000}, {3000, 10000}, {10000, 30000}};
    const uint64_t detangleToleranceHigh = 3;
    const uint64_t phasingThresholdLow = 1;
    const uint64_t phasingThresholdHigh = 6;
    const uint64_t longBubbleThreshold = 5000;
    const uint64_t optimizeChainsMinCommon = 3;
    const uint64_t optimizeChainsK = 6;


    write("Initial");

    detangleVertices(false, 0, detangleToleranceHigh);
    compress();

    for(const auto& p: superbubbleRemovalMaxOffsets) {
        removeShortSuperbubbles(false, p.first, p.second);
        compress();
    }

    detangleEdges(0, detangleToleranceHigh);
    detangleEdges(0, detangleToleranceHigh);
    detangleEdges(1, detangleToleranceHigh);
    detangleVertices(false, 0, detangleToleranceHigh);

    detangleBackEdges(1, detangleToleranceHigh);
    compress();

    detangleVerticesGeneral(false, 1, detangleToleranceHigh);
    compress();

    phaseBubbleChains(false, 1, phasingThresholdLow, phasingThresholdHigh, longBubbleThreshold);
    compress();

    phaseBubbleChains(false, 1, phasingThresholdLow, phasingThresholdHigh, longBubbleThreshold);
    compress();

    detangleShortSuperbubbles(100000, 1, detangleToleranceHigh);
    compress();

    detangleShortSuperbubblesGeneral(100000, 1, detangleToleranceHigh);
    compress();

    phaseBubbleChains(false, 1, phasingThresholdLow, phasingThresholdHigh, longBubbleThreshold);
    compress();

    removeShortSuperbubbles(false, 30000, 100000);
    compress();

    phaseBubbleChains(false, 1, phasingThresholdLow, phasingThresholdHigh, longBubbleThreshold);
    compress();

    removeShortSuperbubbles(false, 30000, 100000);
    compress();

    phaseBubbleChains(false, 1, phasingThresholdLow, phasingThresholdHigh, longBubbleThreshold);
    compress();

    removeShortSuperbubbles(false, 30000, 100000);
    compress();

    phaseBubbleChains(false, 1, 1, 4, longBubbleThreshold);
    compress();

    removeShortSuperbubbles(false, 30000, 100000);
    compress();

    phaseBubbleChains(false, 10, 1, 4, longBubbleThreshold);

    // Before final output, renumber the edges contiguously.
    renumberEdges();

    // Optimize the chains and assemble sequence.
    optimizeChains(
        false,
        optimizeChainsMinCommon,
        optimizeChainsK);

    assembleChains(threadCount0, threadCount1);

    // Final output.
    write("Final");
    writeGfaExpanded("Final", true);
    writeFastaExpanded("Final");
}



// Initial creation from the PathGraph1.
// Each linear chain of edges in the PathGraph1 after transitive reduction generates
// a CompressedPathGraph1BEdge (BubbleChain) consisting of a single haploid bubble.
void CompressedPathGraph1B::create(const PathGraph1& graph)
{
    CompressedPathGraph1B& cGraph = *this;

    // Create a filtered version of the PathGraph1, containing only the
    // transitive reduction edges.
    class EdgePredicate {
    public:
        bool operator()(const PathGraph1::edge_descriptor e) const
        {
            return not (*graph)[e].isNonTransitiveReductionEdge;
        }
        EdgePredicate(const PathGraph1& graph) : graph(&graph) {}
        EdgePredicate() : graph(0) {}
    private:
        const PathGraph1* graph;
    };
    using FilteredPathGraph1 = boost::filtered_graph<PathGraph1, EdgePredicate>;
    FilteredPathGraph1 filteredGraph(graph, EdgePredicate(graph));

    // Find linear chains in the PathGraph1 after transitive reduction.
    vector< vector<PathGraph1::edge_descriptor> > inputChains;
    findLinearChains(filteredGraph, 0, inputChains);

    // Each chain generates an edge.
    // Vertices are added as needed.
    std::map<MarkerGraphEdgeId, vertex_descriptor> vertexMap;
    for(const vector<PathGraph1::edge_descriptor>& inputChain: inputChains) {
        const PathGraph1::vertex_descriptor v0 = source(inputChain.front(), graph);
        const PathGraph1::vertex_descriptor v1 = target(inputChain.back(), graph);
        const MarkerGraphEdgeId markerGraphEdgeId0 = graph[v0].edgeId;
        const MarkerGraphEdgeId markerGraphEdgeId1 = graph[v1].edgeId;
        const vertex_descriptor cv0 = getVertex(markerGraphEdgeId0, vertexMap);
        const vertex_descriptor cv1 = getVertex(markerGraphEdgeId1, vertexMap);

        // Create an edge for this input chain.
        edge_descriptor ce;
        tie(ce, ignore) = add_edge(cv0, cv1, cGraph);
        CompressedPathGraph1BEdge& edge = cGraph[ce];
        edge.id = nextEdgeId++;

        // The edge is a degenerate BubbleChain consisting of a single haploid bubble.
        edge.resize(1);                 // BubbleChain has length 1.
        Bubble& bubble = edge.front();
        bubble.resize(1);               // Bubble is haploid.

        // Store the chain.
        Chain& chain = bubble.front();
        for(const PathGraph1::edge_descriptor e: inputChain) {
            const PathGraph1::vertex_descriptor v = source(e, graph);
            chain.push_back(graph[v].edgeId);
        }
        const PathGraph1::edge_descriptor eLast = inputChain.back();
        const PathGraph1::vertex_descriptor vLast = target(eLast, graph);
        chain.push_back(graph[vLast].edgeId);
    }
}



// Return the vertex corresponding to a given MarkerGraphEdgeId,
// creating it if it is not in the given vertexMap
CompressedPathGraph1B::vertex_descriptor CompressedPathGraph1B::getVertex(
    MarkerGraphEdgeId markerGraphEdgeId,
    std::map<MarkerGraphEdgeId, vertex_descriptor>& vertexMap)
{
    CompressedPathGraph1B& cGraph = *this;

    auto it = vertexMap.find(markerGraphEdgeId);
    if(it == vertexMap.end()) {
        const vertex_descriptor cv = add_vertex({markerGraphEdgeId}, cGraph);
        vertexMap.insert({markerGraphEdgeId, cv});
        return cv;
    } else {
        return it->second;
    }
}



// Create a new vertex with a given MarkerGraphEdgeId.
CompressedPathGraph1B::vertex_descriptor CompressedPathGraph1B::createVertex(
    MarkerGraphEdgeId markerGraphEdgeId)
{
    return add_vertex({markerGraphEdgeId}, *this);
}



void CompressedPathGraph1B::removeVertex(vertex_descriptor cv)
{
    CompressedPathGraph1B& cGraph = *this;

    SHASTA_ASSERT(in_degree(cv, cGraph) == 0);
    SHASTA_ASSERT(out_degree(cv, cGraph) == 0);

    boost::remove_vertex(cv, cGraph);
}



// Compute vertexIndex for every vertex.
// This numbers vertices consecutively starting at zero.
// This numbering becomes invalid as soon as a vertex is added or removed.
void CompressedPathGraph1B::numberVertices()
{
    CompressedPathGraph1B& cGraph = *this;
    uint64_t index = 0;
    BGL_FORALL_VERTICES(cv, cGraph, CompressedPathGraph1B) {
        cGraph[cv].index = index++;
    }
}



void CompressedPathGraph1B::clearVertexNumbering()
{
    CompressedPathGraph1B& cGraph = *this;
    BGL_FORALL_VERTICES(cv, cGraph, CompressedPathGraph1B) {
        cGraph[cv].index = invalid<uint64_t>;
    }

}


void CompressedPathGraph1B::renumberEdges()
{
    CompressedPathGraph1B& cGraph = *this;
    nextEdgeId = 0;

    BGL_FORALL_EDGES(ce, cGraph, CompressedPathGraph1B) {
        cGraph[ce].id = nextEdgeId++;
    }
}



// Compress parallel edges into bubbles, where possible.
bool CompressedPathGraph1B::compressParallelEdges()
{
    CompressedPathGraph1B& cGraph = *this;
    bool changesWereMade = false;

    // Look for sets of parallel edges v0->v1.
    vector<vertex_descriptor> childrenVertices;
    vector<edge_descriptor> edgesToBeRemoved;
    Bubble newBubble;
    BGL_FORALL_VERTICES(v0, cGraph, CompressedPathGraph1B) {
        if(out_degree(v0, cGraph) < 2) {
            continue;
        }

        // Find distinct children vertices of v0.
        childrenVertices.clear();
        BGL_FORALL_OUTEDGES(v0, e, cGraph, CompressedPathGraph1B) {
            childrenVertices.push_back(target(e, cGraph));
        }
        deduplicate(childrenVertices);

        // Handle the children vertices one at a time.
        for(const vertex_descriptor v1: childrenVertices) {

            // Create the new bubble using parallel edges v0->v1.
            newBubble.clear();
            edgesToBeRemoved.clear();
            BGL_FORALL_OUTEDGES(v0, e, cGraph, CompressedPathGraph1B) {
                if(target(e, cGraph) != v1) {
                    continue;
                }
                CompressedPathGraph1BEdge& edge = cGraph[e];

                // The BubbleChain must have length 1.
                if(edge.size() > 1) {
                    continue;
                }
                const Bubble& oldBubble = edge.front();

                copy(oldBubble.begin(), oldBubble.end(), back_inserter(newBubble));
                edgesToBeRemoved.push_back(e);
            }
            if(edgesToBeRemoved.size() < 2) {
                continue;
            }

            // Create the new edge.
            changesWereMade = true;
            edge_descriptor eNew;
            tie(eNew, ignore) = add_edge(v0, v1, cGraph);
            CompressedPathGraph1BEdge& newEdge = cGraph[eNew];
            newEdge.id = nextEdgeId++;
            newEdge.resize(1);  // Make it a single bubble.
            Bubble& newEdgeBubble = newEdge.front();
            newEdgeBubble = newBubble;

            // Remove the old edges.
            for(const edge_descriptor e: edgesToBeRemoved) {
                boost::remove_edge(e, cGraph);
            }

        }
    }
    return changesWereMade;
}



// Compress linear sequences of edges (BubbleChains) into longer BubbleChains.
bool CompressedPathGraph1B::compressSequentialEdges()
{
    CompressedPathGraph1B& cGraph = *this;
    bool changesWereMade = false;

    // Find linear chains of edges.
    vector< vector<edge_descriptor> > linearChains;
    findLinearChains(cGraph, 0, linearChains);



    // Each linear chain of more than one edge gets compressed into a single edge (BubbleChain).
    for(const vector<edge_descriptor>& linearChain: linearChains) {
        if(linearChain.size() < 2) {
            continue;
        }

        // Create the new edge.
        changesWereMade = true;
        const vertex_descriptor v0 = source(linearChain.front(), cGraph);
        const vertex_descriptor v1 = target(linearChain.back(), cGraph);
        edge_descriptor ceNew;
        tie(ceNew, ignore) = add_edge(v0, v1, cGraph);
        CompressedPathGraph1BEdge& newEdge = cGraph[ceNew];
        newEdge.id = nextEdgeId++;
        for(const edge_descriptor ce: linearChain) {
            const CompressedPathGraph1BEdge& oldEdge = cGraph[ce];
            copy(oldEdge.begin(), oldEdge.end(), back_inserter(newEdge));
        }

        // Remove the old edges.
        for(const edge_descriptor ce: linearChain) {
            boost::remove_edge(ce, cGraph);
        }

        // Remove the vertices internal to the old edge.
        for(uint64_t i=1; i<linearChain.size(); i++) {
            const vertex_descriptor cv = source(linearChain[i], cGraph);
            cGraph.removeVertex(cv);
        }
    }
    return changesWereMade;
}



// Call compressParallelEdges and compressSequentialEdges iteratively until nothing changes.
void CompressedPathGraph1B::compress()
{
    while(true) {
        const bool compressParallelChanges = compressParallelEdges();
        const bool compressSequentialChanges = compressSequentialEdges();
        if(not(compressParallelChanges or compressSequentialChanges)) {
            break;
        }
    }
}



void CompressedPathGraph1B::write(const string& name) const
{
    const string fileNamePrefix = name + "-" + to_string(componentId);

    cout << fileNamePrefix << ": " << num_vertices(*this) <<
        " vertices, " << num_edges(*this) << " edges. Next edge id " << nextEdgeId << endl;

    writeCsv(fileNamePrefix);
    writeGraphviz(fileNamePrefix, true);
    writeGraphviz(fileNamePrefix, false);
    writeGfa(fileNamePrefix);
}



void CompressedPathGraph1B::writeCsv(const string& fileNamePrefix) const
{
    writeBubbleChainsCsv(fileNamePrefix);
    writeBubblesCsv(fileNamePrefix);
    writeChainsCsv(fileNamePrefix);
    writeChainsDetailsCsv(fileNamePrefix);
}



void CompressedPathGraph1B::writeBubbleChainsCsv(const string& fileNamePrefix) const
{
    const CompressedPathGraph1B& cGraph = *this;

    ofstream csv(fileNamePrefix + "-BubbleChains.csv");
    csv << "Id,ComponentId,BubbleChainId,v0,v1,BubbleCount,AverageOffset,MinOffset,MaxOffset,\n";

    BGL_FORALL_EDGES(ce, cGraph, CompressedPathGraph1B) {
        const vertex_descriptor cv0 = source(ce, cGraph);
        const vertex_descriptor cv1 = target(ce, cGraph);
        const BubbleChain& bubbleChain = cGraph[ce];

        uint64_t averageOffset;
        uint64_t minOffset;
        uint64_t maxOffset;
        bubbleChainOffset(bubbleChain, averageOffset, minOffset, maxOffset);

        csv << bubbleChainStringId(ce) << ",";
        csv << componentId << ",";
        csv << cGraph[ce].id << ",";
        csv << cGraph[cv0].edgeId << ",";
        csv << cGraph[cv1].edgeId << ",";
        csv << bubbleChain.size() << ",";
        csv << averageOffset << ",";
        csv << minOffset << ",";
        csv << maxOffset << ",";
        csv << "\n";
    }
}



void CompressedPathGraph1B::writeBubblesCsv(const string& fileNamePrefix) const
{
    const CompressedPathGraph1B& cGraph = *this;

    ofstream csv(fileNamePrefix + "-Bubbles.csv");
    csv << "Id,ComponentId,BubbleChainId,Position in bubble chain,v0,v1,Ploidy,AverageOffset,MinOffset,MaxOffset,\n";

    BGL_FORALL_EDGES(ce, cGraph, CompressedPathGraph1B) {
        const BubbleChain& bubbleChain = cGraph[ce];

        for(uint64_t positionInBubbleChain=0; positionInBubbleChain<bubbleChain.size(); positionInBubbleChain++) {
            const Bubble& bubble = bubbleChain[positionInBubbleChain];
            const Chain& firstChain = bubble.front();

            // Check that all the chains begins/end in the same place.
            for(const Chain& chain: bubble) {
                SHASTA_ASSERT(chain.front() == firstChain.front());
                SHASTA_ASSERT(chain.back() == firstChain.back());
            }

            uint64_t averageOffset;
            uint64_t minOffset;
            uint64_t maxOffset;
            bubbleOffset(bubble, averageOffset, minOffset, maxOffset);

            csv << bubbleStringId(ce, positionInBubbleChain) << ",";
            csv << componentId << ",";
            csv << cGraph[ce].id << ",";
            csv << positionInBubbleChain << ",";
            csv << firstChain.front() << ",";
            csv << firstChain.back() << ",";
            csv << bubble.size() << ",";
            csv << averageOffset << ",";
            csv << minOffset << ",";
            csv << maxOffset << ",";
            csv << "\n";
        }
    }

}


void CompressedPathGraph1B::writeChainsCsv(const string& fileNamePrefix) const
{
    const CompressedPathGraph1B& cGraph = *this;

    ofstream csv(fileNamePrefix + "-Chains.csv");
    csv << "Id,ComponentId,BubbleChainId,Position in bubble chain,Index in bubble,Length,Offset\n";

    BGL_FORALL_EDGES(ce, cGraph, CompressedPathGraph1B) {
        const BubbleChain& bubbleChain = cGraph[ce];

        for(uint64_t positionInBubbleChain=0; positionInBubbleChain<bubbleChain.size(); positionInBubbleChain++) {
            const Bubble& bubble = bubbleChain[positionInBubbleChain];
            const uint64_t ploidy = bubble.size();

            for(uint64_t indexInBubble=0; indexInBubble<ploidy; indexInBubble++) {
                const Chain& chain = bubble[indexInBubble];
                SHASTA_ASSERT(chain.size() >= 2);

                csv << chainStringId(ce, positionInBubbleChain, indexInBubble) << ",";
                csv << componentId << ",";
                csv << cGraph[ce].id << ",";
                csv << positionInBubbleChain << ",";
                csv << indexInBubble << ",";
                csv << chain.size() << ",";
                csv << chainOffset(chain) << ",";
                csv << "\n";
            }
        }
    }

}



void CompressedPathGraph1B::writeChainsDetailsCsv(const string& fileNamePrefix) const
{
    const CompressedPathGraph1B& cGraph = *this;

    ofstream csv(fileNamePrefix + "-ChainsDetails.csv");
    csv << "Id,ComponentId,BubbleChainId,Position in bubble chain,"
        "Index in bubble,Position in chain,MarkerGraphEdgeId,Coverage,Common,Offset\n";

    BGL_FORALL_EDGES(ce, cGraph, CompressedPathGraph1B) {
        const BubbleChain& bubbleChain = cGraph[ce];

        for(uint64_t positionInBubbleChain=0; positionInBubbleChain<bubbleChain.size(); positionInBubbleChain++) {
            const Bubble& bubble = bubbleChain[positionInBubbleChain];
            const uint64_t ploidy = bubble.size();

            for(uint64_t indexInBubble=0; indexInBubble<ploidy; indexInBubble++) {
                const Chain& chain = bubble[indexInBubble];
                SHASTA_ASSERT(chain.size() >= 2);

                for(uint64_t positionInChain=0; positionInChain<chain.size(); positionInChain++) {
                    const MarkerGraphEdgeId markerGraphEdgeId = chain[positionInChain];
                    const uint64_t coverage = assembler.markerGraph.edgeCoverage(markerGraphEdgeId);
                    csv << chainStringId(ce, positionInBubbleChain, indexInBubble) << ",";
                    csv << componentId << ",";
                    csv << cGraph[ce].id << ",";
                    csv << positionInBubbleChain << ",";
                    csv << indexInBubble << ",";
                    csv << positionInChain << ",";
                    csv << markerGraphEdgeId << ",";
                    csv << coverage << ",";

                    if(positionInChain != 0) {
                        const MarkerGraphEdgeId previousMarkerGraphEdgeId = chain[positionInChain - 1];
                        MarkerGraphEdgePairInfo info;
                        SHASTA_ASSERT(assembler.analyzeMarkerGraphEdgePair(
                            previousMarkerGraphEdgeId, markerGraphEdgeId, info));
                        csv << info.common << ",";
                        if(info.common != 0) {
                            csv << info.offsetInBases << ",";
                        }
                    }
                    csv << "\n";
                }
            }
        }
    }

}



void CompressedPathGraph1B::writeGraphviz(
    const string& fileNamePrefix,
    bool labels) const
{
    const CompressedPathGraph1B& cGraph = *this;

    ofstream dot;
    if(labels) {
        dot.open(fileNamePrefix + ".dot");
    } else {
        dot.open(fileNamePrefix + "-NoLabels.dot");
    }

    dot << "digraph Component_" << componentId << "{\n";

    BGL_FORALL_VERTICES(cv, cGraph, CompressedPathGraph1B) {
        dot << cGraph[cv].edgeId << ";\n";
    }



    BGL_FORALL_EDGES(ce, cGraph, CompressedPathGraph1B) {
        const vertex_descriptor cv0 = source(ce, cGraph);
        const vertex_descriptor cv1 = target(ce, cGraph);

        uint64_t averageOffset;
        uint64_t minOffset;
        uint64_t maxOffset;
        bubbleChainOffset(cGraph[ce], averageOffset, minOffset, maxOffset);

        dot << cGraph[cv0].edgeId << "->" << cGraph[cv1].edgeId;
        if(labels) {
            dot << " [label=\"" <<
                bubbleChainStringId(ce) << "\\n" <<
                averageOffset << "\"]";
        }
        dot << ";\n";
    }

    dot << "}\n";
}



void CompressedPathGraph1B::writeGfa(const string& fileNamePrefix) const
{
    const CompressedPathGraph1B& cGraph = *this;

    ofstream gfa(fileNamePrefix + ".gfa");

    // Write the header line.
    gfa << "H\tVN:Z:1.0\n";

    // Write a segment for each edge.
    BGL_FORALL_EDGES(ce, cGraph, CompressedPathGraph1B) {

        uint64_t averageOffset;
        uint64_t minOffset;
        uint64_t maxOffset;
        bubbleChainOffset(cGraph[ce], averageOffset, minOffset, maxOffset);

        // Record type.
        gfa << "S\t";

        // Name.
        gfa << bubbleChainStringId(ce) << "\t";

        // Sequence.
        gfa << "*\t";

        // Sequence length in bases.
        gfa << "LN:i:" << averageOffset << "\n";
    }

    // For each vertex, write links between each pair of incoming/outgoing edges.
    BGL_FORALL_VERTICES(cv, cGraph, CompressedPathGraph1A) {
        BGL_FORALL_INEDGES(cv, ceIn, cGraph, CompressedPathGraph1B) {
            BGL_FORALL_OUTEDGES(cv, ceOut, cGraph, CompressedPathGraph1B) {
                gfa <<
                    "L\t" <<
                    bubbleChainStringId(ceIn) << "\t+\t" <<
                    bubbleChainStringId(ceOut) << "\t+\t*\n";
            }
        }
    }
}



// This version writes each chain as a segment, so it shows the
// details of the BubbleChains.
void CompressedPathGraph1B::writeGfaExpanded(
    const string& fileNamePrefix,
    bool includeSequence) const
{
    const CompressedPathGraph1B& cGraph = *this;

    ofstream gfa(fileNamePrefix + "-" + to_string(componentId) + "-Expanded.gfa");

    // Write the header line.
    gfa << "H\tVN:Z:1.0\n";

    // Loop over BubbleChains. Each Chain of each Bubble generates a GFA segment.
    BGL_FORALL_EDGES(ce, cGraph, CompressedPathGraph1B) {
        const BubbleChain& bubbleChain = cGraph[ce];

        // Loop over Bubbles of this chain.
        for(uint64_t positionInBubbleChain=0; positionInBubbleChain<bubbleChain.size();
            ++positionInBubbleChain) {
            const Bubble& bubble = bubbleChain[positionInBubbleChain];

            // Loop over chains of this bubble.
            for(uint64_t indexInBubble=0; indexInBubble<bubble.size(); indexInBubble++) {
                const Chain& chain = bubble[indexInBubble];
                const uint64_t offset = chainOffset(chain);

                // Record type.
                gfa << "S\t";

                // Name.
                gfa << chainStringId(ce, positionInBubbleChain, indexInBubble) << "\t";

                if(includeSequence) {
                    using shasta::Base;
                    const vector<Base>& sequence = chain.sequence;

                    // Sequence.
                    copy(sequence.begin(), sequence.end(), ostream_iterator<Base>(gfa));
                    gfa << "\t";

                    // Sequence length in bases.
                    gfa << "LN:i:" << sequence.size() << "\n";

                } else {

                    // Sequence.
                    gfa << "*\t";

                    // Sequence length in bases.
                    gfa << "LN:i:" << offset << "\n";
                }
            }
        }
    }



    // Write links between adjacent Chains of each BubbleChain.
    BGL_FORALL_EDGES(ce, cGraph, CompressedPathGraph1B) {
        const BubbleChain& bubbleChain = cGraph[ce];

        // Loop over Bubbles of this chain.
        for(uint64_t positionInBubbleChain=1; positionInBubbleChain<bubbleChain.size();
            ++positionInBubbleChain) {
            const Bubble& bubble0 = bubbleChain[positionInBubbleChain - 1];
            const Bubble& bubble1 = bubbleChain[positionInBubbleChain];

            for(uint64_t indexInBubble0=0; indexInBubble0<bubble0.size(); indexInBubble0++) {
                const string chain0StringId = chainStringId(ce, positionInBubbleChain-1, indexInBubble0);

                for(uint64_t indexInBubble1=0; indexInBubble1<bubble1.size(); indexInBubble1++) {
                   const string chain1StringId = chainStringId(ce, positionInBubbleChain, indexInBubble1);

                   gfa <<
                       "L\t" <<
                       chain0StringId << "\t+\t" <<
                       chain1StringId << "\t+\t*\n";
                }
            }
        }
    }



    // Write links between Chains in different bubble chains.
    BGL_FORALL_VERTICES(cv, cGraph, CompressedPathGraph1A) {
        BGL_FORALL_INEDGES(cv, ce0, cGraph, CompressedPathGraph1B) {
            const BubbleChain& bubbleChain0 = cGraph[ce0];
            const Bubble& bubble0 = bubbleChain0.back();
            BGL_FORALL_OUTEDGES(cv, ce1, cGraph, CompressedPathGraph1B) {
                const BubbleChain& bubbleChain1 = cGraph[ce1];
                const Bubble& bubble1 = bubbleChain1.front();

                for(uint64_t indexInBubble0=0; indexInBubble0<bubble0.size(); indexInBubble0++) {
                    const string chain0StringId = chainStringId(ce0, bubbleChain0.size()-1, indexInBubble0);

                    for(uint64_t indexInBubble1=0; indexInBubble1<bubble1.size(); indexInBubble1++) {
                       const string chain1StringId = chainStringId(ce1, 0, indexInBubble1);

                       gfa <<
                           "L\t" <<
                           chain0StringId << "\t+\t" <<
                           chain1StringId << "\t+\t*\n";
                    }
                }
            }
        }
    }

}



void CompressedPathGraph1B::writeFastaExpanded(
    const string& fileNamePrefix) const
{
    const CompressedPathGraph1B& cGraph = *this;

    ofstream fasta(fileNamePrefix + "-" + to_string(componentId) + "-Expanded.fasta");

    // Loop over BubbleChains. Each Chain of each Bubble generates a GFA segment.
    BGL_FORALL_EDGES(ce, cGraph, CompressedPathGraph1B) {
        const BubbleChain& bubbleChain = cGraph[ce];

        // Loop over Bubbles of this chain.
        for(uint64_t positionInBubbleChain=0; positionInBubbleChain<bubbleChain.size();
            ++positionInBubbleChain) {
            const Bubble& bubble = bubbleChain[positionInBubbleChain];

            // Loop over chains of this bubble.
            for(uint64_t indexInBubble=0; indexInBubble<bubble.size(); indexInBubble++) {
                const Chain& chain = bubble[indexInBubble];

                using shasta::Base;
                const vector<Base>& sequence = chain.sequence;

                fasta << ">" << chainStringId(ce, positionInBubbleChain, indexInBubble) <<
                    " " << sequence.size() << "\n";
                copy(sequence.begin(), sequence.end(), ostream_iterator<Base>(fasta));
                fasta << "\n";



            }
        }
    }
}



string CompressedPathGraph1B::bubbleChainStringId(edge_descriptor ce) const
{
    const CompressedPathGraph1B& cGraph = *this;
    const CompressedPathGraph1BEdge& edge = cGraph[ce];
    return to_string(componentId) + "-" + to_string(edge.id);
}



string CompressedPathGraph1B::bubbleStringId(
    edge_descriptor ce,
    uint64_t positionInBubbleChain) const
{
    const CompressedPathGraph1B& cGraph = *this;
    const CompressedPathGraph1BEdge& edge = cGraph[ce];

    return
        to_string(componentId) + "-" +
        to_string(edge.id) + "-" +
        to_string(positionInBubbleChain);
}



string CompressedPathGraph1B::chainStringId(
    edge_descriptor ce,
    uint64_t positionInBubbleChain,
    uint64_t indexInBubble) const
{
    const CompressedPathGraph1B& cGraph = *this;
    const CompressedPathGraph1BEdge& edge = cGraph[ce];

    return
        to_string(componentId) + "-" +
        to_string(edge.id) + "-" +
        to_string(positionInBubbleChain) + "-" +
        to_string(indexInBubble);
}



uint64_t CompressedPathGraph1B::chainOffset(const Chain& chain) const
{
    const uint64_t length = chain.size();
    SHASTA_ASSERT(length >= 2);

    uint64_t offset = 0;
    for(uint64_t i=1; i<length; i++) {
        const MarkerGraphEdgeId edgeId0 = chain[i-1];
        const MarkerGraphEdgeId edgeId1 = chain[i];

        MarkerGraphEdgePairInfo info;
        SHASTA_ASSERT(assembler.analyzeMarkerGraphEdgePair(edgeId0, edgeId1, info));
        if(info.common == 0) {
            cout << "No common oriented reads for " << edgeId0 << " " << edgeId1 << endl;
        }
        SHASTA_ASSERT(info.common > 0);
        if(info.offsetInBases > 0) {
            offset += info.offsetInBases;
        }
    }
    return offset;
}



void CompressedPathGraph1B::bubbleOffset(
    const Bubble& bubble,
    uint64_t& averageOffset,
    uint64_t& minOffset,
    uint64_t& maxOffset
    ) const
{
    averageOffset = 0;
    minOffset = std::numeric_limits<uint64_t>::max();
    maxOffset = 0;

    for(const Chain& chain: bubble) {
        const uint64_t offset = chainOffset(chain);

        averageOffset += offset;
        minOffset = min(minOffset, offset);
        maxOffset = max(maxOffset, offset);
    }
    averageOffset /= bubble.size();
}



void CompressedPathGraph1B::bubbleChainOffset(
    const BubbleChain& bubbleChain,
    uint64_t& averageOffset,
    uint64_t& minOffset,
    uint64_t& maxOffset
    ) const
{
    averageOffset = 0;
    minOffset = 0;
    maxOffset = 0;

    for(const Bubble& bubble: bubbleChain) {
        uint64_t bubbleAverageOffset;
        uint64_t bubbleMinOffset;
        uint64_t bubbleMaxOffset;
        bubbleOffset(bubble, bubbleAverageOffset, bubbleMinOffset, bubbleMaxOffset);

        averageOffset += bubbleAverageOffset;
        minOffset += bubbleMinOffset;
        maxOffset += bubbleMaxOffset;
    }
}



CompressedPathGraph1B::Superbubbles::Superbubbles(
    CompressedPathGraph1B& cGraph,
    uint64_t maxOffset1     // Used to define superbubbles
    ) :
    cGraph(cGraph)
{
    cGraph.numberVertices();
    const uint64_t vertexCount = num_vertices(cGraph);

    vector<uint64_t> rank(vertexCount);
    vector<uint64_t> parent(vertexCount);
    boost::disjoint_sets<uint64_t*, uint64_t*> disjointSets(&rank[0], &parent[0]);

    // Compute connected components, using only edges with average offset up to maxOffset1.
    for(uint64_t i=0; i<vertexCount; i++) {
        disjointSets.make_set(i);
    }
    BGL_FORALL_EDGES(ce, cGraph, CompressedPathGraph1B) {
        uint64_t averageOffset;
        uint64_t minOffset;
        uint64_t maxOffset;
        cGraph.bubbleChainOffset(cGraph[ce], averageOffset, minOffset, maxOffset);
        if(averageOffset <= maxOffset1) {
            const vertex_descriptor cv0 = source(ce, cGraph);
            const vertex_descriptor cv1 = target(ce, cGraph);
            disjointSets.union_set(cGraph[cv0].index, cGraph[cv1].index);
        }
    }

    // Gather the vertices in each connected component.
    vector< vector<vertex_descriptor> > components(vertexCount);
    BGL_FORALL_VERTICES(cv, cGraph, CompressedPathGraph1B) {
        const uint64_t componentId = disjointSets.find_set(cGraph[cv].index);
        components[componentId].push_back(cv);
    }

    // The superbubbles are the components with size at least 2.
    for(uint64_t componentId=0; componentId<components.size(); componentId++) {
        const vector<vertex_descriptor> component = components[componentId];
        if(components[componentId].size() > 1) {
            superbubbles.emplace_back(Superbubble(component));
        }
    }

    // Store superbubble ids in the vertices.
    BGL_FORALL_VERTICES(cv, cGraph, CompressedPathGraph1B) {
        cGraph[cv].superbubbleId = invalid<uint64_t>;
    }
    for(uint64_t superbubbleId=0; superbubbleId<superbubbles.size(); superbubbleId++) {
        const vector<vertex_descriptor>& superbubble = getSuperbubble(superbubbleId);
        for(const vertex_descriptor cv: superbubble) {
            cGraph[cv].superbubbleId = superbubbleId;
        }
    }



    // Find entrances and exists of each superbubble.
    for(uint64_t superbubbleId=0; superbubbleId<superbubbles.size(); superbubbleId++) {
        Superbubble& superbubble = getSuperbubble(superbubbleId);

        // Find entrances. These are superbubble vertices with in-edges
        // from outside the superbubble.
        for(const vertex_descriptor cv0: superbubble) {
            BGL_FORALL_INEDGES(cv0, ce, cGraph, CompressedPathGraph1B) {
                const vertex_descriptor cv1 = source(ce, cGraph);
                if(not isInSuperbubble(superbubbleId, cv1)) {
                    superbubble.entrances.push_back(cv0);
                    break;
                }
            }
        }

        // Find exits. These are superbubble vertices with out-edges
        // to outside the superbubble.
        vector<vertex_descriptor> exits;
        for(const vertex_descriptor cv0: superbubble) {
            BGL_FORALL_OUTEDGES(cv0, ce, cGraph, CompressedPathGraph1B) {
                const vertex_descriptor cv1 = target(ce, cGraph);
                if(not isInSuperbubble(superbubbleId, cv1)) {
                    superbubble.exits.push_back(cv0);
                    break;
                }
            }
        }
     }

}



CompressedPathGraph1B::Superbubbles::~Superbubbles()
{
    cGraph.clearVertexNumbering();
}



// Remove short superbubbles with one entry and one exit.
void CompressedPathGraph1B::removeShortSuperbubbles(
    bool debug,
    uint64_t maxOffset1,    // Used to define superbubbles
    uint64_t maxOffset2)    // Compared against the offset between entry and exit
{
    CompressedPathGraph1B& cGraph = *this;

    // Find the superbubbles.
    Superbubbles superbubbles(cGraph, maxOffset1);

    // Loop over the superbubbles.
    for(uint64_t superbubbleId=0; superbubbleId<superbubbles.size(); superbubbleId++) {
        Superbubble& superbubble = superbubbles.getSuperbubble(superbubbleId);
        SHASTA_ASSERT(superbubble.size() > 1);

        if(debug) {
            cout << "Found a superbubble with " << superbubble.size() << " vertices:";
            for(const vertex_descriptor v: superbubble) {
                cout << " " << cGraph[v].edgeId;
            }
            cout << endl;
        }


        // Skip it if it has more than one entrance or exit.
        if(not(superbubble.entrances.size()==1 and superbubble.exits.size()==1)) {
            if(debug) {
                cout << "This superbubble will not be removed because it has " <<
                    superbubble.entrances.size() << " entrances and " <<
                    superbubble.exits.size() << " exits." << endl;
            }
            continue;
        }

        const vertex_descriptor entrance = superbubble.entrances.front();
        const vertex_descriptor exit = superbubble.exits.front();
        if(entrance == exit) {
            if(debug) {
                cout << "This superbubble will not be removed because it the entrance vertex"
                    " is the same as the exit vertex." << endl;
            }
            continue;
        }

        // Check the base offset between the entrance and the exit.
        MarkerGraphEdgePairInfo info;
        SHASTA_ASSERT(assembler.analyzeMarkerGraphEdgePair(cGraph[entrance].edgeId, cGraph[exit].edgeId, info));
        if(info.common == 0) {
            if(debug) {
                cout << "This superbubble will not be removed because "
                    "there are no common oriented reads betwene the entrance and the exit." << endl;
            }
            continue;
        }
        if(info.offsetInBases > int64_t(maxOffset2)) {
            if(debug) {
                cout << "This superbubble will not be removed because offsetInBases is " <<
                    info.offsetInBases << endl;
            }
            continue;
        }

        if(debug) {
            cout << "This superbubble will be removed." << endl;
        }

        // Remove all vertices and edges internal to the superbubble.
        for(const vertex_descriptor cv: superbubble) {
            if(cv!=entrance and cv!=exit) {
                boost::clear_vertex(cv, cGraph);
                cGraph.removeVertex(cv);
            }
        }
        // We must also remove edges between the entrance and the exit.
        vector<edge_descriptor> entranceToExitEdges;
        BGL_FORALL_OUTEDGES(entrance, ce, cGraph, CompressedPathGraph1B) {
            if(target(ce, cGraph) == exit) {
                entranceToExitEdges.push_back(ce);
            }
        }
        for(const edge_descriptor ce: entranceToExitEdges) {
            boost::remove_edge(ce, cGraph);
        }
        vector<edge_descriptor> exitToEntranceEdges;
        BGL_FORALL_OUTEDGES(exit, ce, cGraph, CompressedPathGraph1B) {
            if(target(ce, cGraph) == entrance) {
                exitToEntranceEdges.push_back(ce);
            }
        }
        for(const edge_descriptor ce: exitToEntranceEdges) {
            boost::remove_edge(ce, cGraph);
        }

        // Generate an edge between the entrance and the exit.
        // This will be a BubbleChain consisting of a single haploid Bubble.
        edge_descriptor eNew;
        tie(eNew, ignore) = add_edge(entrance, exit, cGraph);
        CompressedPathGraph1BEdge& newEdge = cGraph[eNew];
        newEdge.id = nextEdgeId++;
        BubbleChain& bubbleChain = newEdge;
        bubbleChain.resize(1);
        Bubble& bubble = bubbleChain.front();
        bubble.resize(1);
        Chain& chain = bubble.front();
        chain.push_back(cGraph[entrance].edgeId);
        chain.push_back(cGraph[exit].edgeId);

    }
}



#if 0
bool CompressedPathGraph1B::detangleVerticesStrict(bool debug)
{
    if(debug) {
        cout << "Detangling vertices." << endl;
    }
    CompressedPathGraph1B& cGraph = *this;

    vector<vertex_descriptor> allVertices;
    BGL_FORALL_VERTICES(cv, cGraph, CompressedPathGraph1B) {
        allVertices.push_back(cv);
    }

    uint64_t detangledCount = 0;
    for(const vertex_descriptor cv: allVertices) {
        if(detangleVertexStrict(cv, debug)) {
            ++detangledCount;
        }
    }

    if(debug) {
        cout << "Detangled " << detangledCount << " vertices." << endl;

    }

    return detangledCount > 0;
}
#endif



bool CompressedPathGraph1B::detangleVertices(
    bool debug,
    uint64_t detangleToleranceLow,
    uint64_t detangleToleranceHigh)
{
    if(debug) {
        cout << "Detangling vertices." << endl;
    }
    CompressedPathGraph1B& cGraph = *this;

    vector<vertex_descriptor> allVertices;
    BGL_FORALL_VERTICES(cv, cGraph, CompressedPathGraph1B) {
        allVertices.push_back(cv);
    }

    uint64_t detangledCount = 0;
    for(const vertex_descriptor cv: allVertices) {
        if(detangleVertex(cv, debug, detangleToleranceLow, detangleToleranceHigh)) {
            ++detangledCount;
        }
    }

    if(true) {
        cout << "Detangled " << detangledCount << " vertices." << endl;

    }

    return detangledCount > 0;
}



bool CompressedPathGraph1B::detangleVerticesGeneral(
    bool debug,
    uint64_t detangleToleranceLow,
    uint64_t detangleToleranceHigh)
{
    if(debug) {
        cout << "Detangling vertices (general detangling)." << endl;
    }
    CompressedPathGraph1B& cGraph = *this;

    vector<vertex_descriptor> allVertices;
    BGL_FORALL_VERTICES(cv, cGraph, CompressedPathGraph1B) {
        allVertices.push_back(cv);
    }

    uint64_t detangledCount = 0;
    for(const vertex_descriptor cv: allVertices) {
        if(detangleVertexGeneral(cv, debug, detangleToleranceLow, detangleToleranceHigh)) {
            ++detangledCount;
        }
    }

    if(true) {
        cout << "Detangled " << detangledCount << " vertices." << endl;

    }

    return detangledCount > 0;
}


// Compute the tangle matrix given in-edges and out-edges.
// The last bubble of each in-edge and the first bubble
// of each out-edge must be haploid.
void CompressedPathGraph1B::computeTangleMatrix(
    const vector<edge_descriptor>& inEdges,
    const vector<edge_descriptor>& outEdges,
    vector< vector<uint64_t> >& tangleMatrix,
    bool setToZeroForComplementaryPairs
    ) const
{
    const CompressedPathGraph1B& cGraph = *this;

    tangleMatrix.clear();
    tangleMatrix.resize(inEdges.size(), vector<uint64_t>(outEdges.size()));

    for(uint64_t i0=0; i0<inEdges.size(); i0++) {
        const edge_descriptor ce0 = inEdges[i0];
        const BubbleChain& bubbleChain0 = cGraph[ce0];
        const Bubble& bubble0 = bubbleChain0.lastBubble();
        SHASTA_ASSERT(bubble0.isHaploid());
        const Chain& chain0 = bubble0.front();
        SHASTA_ASSERT(chain0.size() >= 2);
        const MarkerGraphEdgeId markerGraphEdgeId0 = chain0[chain0.size() - 2];  // Exclude last

        for(uint64_t i1=0; i1<outEdges.size(); i1++) {
            const edge_descriptor ce1 = outEdges[i1];
            const BubbleChain& bubbleChain1 = cGraph[ce1];
            const Bubble& bubble1 = bubbleChain1.firstBubble();
            SHASTA_ASSERT(bubble1.isHaploid());
            const Chain& chain1 = bubble1.front();
            SHASTA_ASSERT(chain1.size() >= 2);
            const MarkerGraphEdgeId markerGraphEdgeId1 = chain1[1];  // Exclude first

            if(setToZeroForComplementaryPairs and
                assembler.markerGraph.reverseComplementEdge[markerGraphEdgeId0] == markerGraphEdgeId1) {
                tangleMatrix[i0][i1] = 0;
            } else {
                MarkerGraphEdgePairInfo info;
                SHASTA_ASSERT(assembler.analyzeMarkerGraphEdgePair(markerGraphEdgeId0, markerGraphEdgeId1, info));
                tangleMatrix[i0][i1] = info.common;
            }
        }
    }
}



#if 0
// This works if the following is true:
// - For all incoming edges (bubble chains) of cv, the last bubble is haploid.
// - For all outgoing edges (bubble chains) of cv, the first bubble is haploid.
bool CompressedPathGraph1B::detangleVertexStrict(
    vertex_descriptor cv, bool debug)
{
    CompressedPathGraph1B& cGraph = *this;

    // Gather the in-edges and check that the last bubble is haploid.
    vector<edge_descriptor> inEdges;
    BGL_FORALL_INEDGES(cv, ce, cGraph, CompressedPathGraph1B) {
        const BubbleChain& bubbleChain = cGraph[ce];
        if(not bubbleChain.lastBubble().isHaploid()) {
            return false;
        }
        inEdges.push_back(ce);
    }

    // Gather the out-edges and check that the first bubble is haploid.
    vector<edge_descriptor> outEdges;
    BGL_FORALL_OUTEDGES(cv, ce, cGraph, CompressedPathGraph1B) {
        const BubbleChain& bubbleChain = cGraph[ce];
        if(not bubbleChain.firstBubble().isHaploid()) {
            return false;
        }
        outEdges.push_back(ce);
    }

    if(inEdges.size() == 1 and outEdges.size() == 1) {
        return false;
    }

    // Compute the tangle matrix.
    vector< vector<uint64_t> > tangleMatrix;
    computeTangleMatrix(inEdges, outEdges, tangleMatrix, false);

    if(debug) {
        cout << "Tangle matrix for vertex " << cGraph[cv].edgeId << endl;
        for(uint64_t i0=0; i0<inEdges.size(); i0++) {
            for(uint64_t i1=0; i1<outEdges.size(); i1++) {
                cout << bubbleChainStringId(inEdges[i0]) << " " <<
                    bubbleChainStringId(outEdges[i1]) << " " <<
                    tangleMatrix[i0][i1] << endl;
            }
        }
    }

    // If the tangle matrix contains no zeros, there is nothing to do.
    bool foundZero = false;
    for(uint64_t i0=0; i0<inEdges.size(); i0++) {
        for(uint64_t i1=0; i1<outEdges.size(); i1++) {
            if(tangleMatrix[i0][i1] == 0) {
                foundZero = true;
                break;
            }
        }
        if(foundZero) {
            break;
        }
    }
    if(not foundZero) {
        return false;
    }

    // To avoid breaking contiguity, we require each column and each row of the
    // tangle matrix to have at least one non-zero element.
    // This means that each in-edge will be "merged" with at least one out-edge,
    // and each out-edge will be "merged" with at least one in-edge.
    for(uint64_t i0=0; i0<inEdges.size(); i0++) {
        bool foundNonZero = false;
        for(uint64_t i1=0; i1<outEdges.size(); i1++) {
            if(tangleMatrix[i0][i1] != 0) {
                foundNonZero = true;
                break;
            }
        }
        if(not foundNonZero) {
            return false;
        }
    }
    for(uint64_t i1=0; i1<outEdges.size(); i1++) {
        bool foundNonZero = false;
        for(uint64_t i0=0; i0<inEdges.size(); i0++) {
            if(tangleMatrix[i0][i1] != 0) {
                foundNonZero = true;
                break;
            }
        }
        if(not foundNonZero) {
            return false;
        }
    }

    if(debug) {
        cout << "This vertex will be detangled " << inEdges.size() << " by " << outEdges.size() << endl;
    }



    // Each non-zero element of the tangle matrix generates a new edge,
    // obtained by "merging" an in-edge with an out-edge.
    for(uint64_t i0=0; i0<inEdges.size(); i0++) {
        const edge_descriptor ce0 = inEdges[i0];
        const BubbleChain& bubbleChain0 = cGraph[ce0];
        const Bubble& bubble0 = bubbleChain0.lastBubble();
        SHASTA_ASSERT(bubble0.isHaploid());
        const Chain& chain0 = bubble0.front();
        SHASTA_ASSERT(chain0.size() >= 2);
        for(uint64_t i1=0; i1<outEdges.size(); i1++) {
            if(tangleMatrix[i0][i1] == 0) {
                continue;
            }
            const edge_descriptor ce1 = outEdges[i1];
            const BubbleChain& bubbleChain1 = cGraph[ce1];
            const Bubble& bubble1 = bubbleChain1.firstBubble();
            SHASTA_ASSERT(bubble1.isHaploid());
            const Chain& chain1 = bubble1.front();
            SHASTA_ASSERT(chain1.size() >= 2);

            edge_descriptor eNew;
            tie(eNew, ignore) = add_edge(source(ce0, cGraph), target(ce1, graph), cGraph);
            CompressedPathGraph1BEdge& newEdge = cGraph[eNew];
            newEdge.id = nextEdgeId++;
            BubbleChain& newBubbleChain = newEdge;

            if(debug) {
                cout << "Merging " <<
                    bubbleChainStringId(ce0) << " " <<
                    bubbleChainStringId(ce1) << " into " <<
                    bubbleChainStringId(eNew) << endl;
            }

            // Create the new BubbleChain. It is obtained by joining
            // bubbleChain0 and bubbleChain1, with vertex cv
            // removed from the end of bubbleChain0
            // and from the beginning of bubbleChain1.
            // Here we use the above assumption that
            // the last bubble of bubbleChain0 and the first bubble of bubbleChain1
            // are haploid.
            newBubbleChain = bubbleChain0;

            // Remove cv from the end.
            Bubble& newBubbleLast = newBubbleChain.back();
            SHASTA_ASSERT(newBubbleLast.size() == 1);
            Chain& newChainLast = newBubbleLast.front();
            SHASTA_ASSERT(newChainLast.back() == cGraph[cv].edgeId);
            newChainLast.resize(newChainLast.size() - 1);

            // Append chain1, except for cv.
            SHASTA_ASSERT(chain1.front() == cGraph[cv].edgeId);
            copy(chain1.begin() + 1, chain1.end(), back_inserter(newChainLast));

            // Append the rest of bubbleChain1.
            copy(bubbleChain1.begin() + 1, bubbleChain1.end(), back_inserter(newBubbleChain));
        }

    }

    // Now we can remove cv and all of its in-edges and out-edges.
    clear_vertex(cv, cGraph);
    cGraph.removeVertex(cv);

    return true;
}
#endif



bool CompressedPathGraph1B::detangleVertex(
    vertex_descriptor cv,
    bool debug,
    uint64_t detangleToleranceLow,
    uint64_t detangleToleranceHigh)
{
    CompressedPathGraph1B& cGraph = *this;

    if(debug) {
        cout << "Attempting to detangle vertex " << cGraph[cv].edgeId << endl;
    }


    // Gather the in-edges and check that the last bubble is haploid.
    vector<edge_descriptor> inEdges;
    BGL_FORALL_INEDGES(cv, ce, cGraph, CompressedPathGraph1B) {
        const BubbleChain& bubbleChain = cGraph[ce];
        if(not bubbleChain.lastBubble().isHaploid()) {
            if(debug) {
                cout << "Not detangled because the last bubble of in-edge " <<
                    bubbleChainStringId(ce) << " is not haploid." << endl;
            }
            return false;
        }
        inEdges.push_back(ce);
    }

    // Gather the out-edges and check that the first bubble is haploid.
    vector<edge_descriptor> outEdges;
    BGL_FORALL_OUTEDGES(cv, ce, cGraph, CompressedPathGraph1B) {
        const BubbleChain& bubbleChain = cGraph[ce];
        if(not bubbleChain.firstBubble().isHaploid()) {
            if(debug) {
                cout << "Not detangled because the first bubble of out-edge " <<
                    bubbleChainStringId(ce) << " is not haploid." << endl;
            }
            return false;
        }
        outEdges.push_back(ce);
    }

    if(inEdges.size() == 0 or outEdges.size() == 0) {
        if(debug) {
            cout << "Not detangling due to degree (case 1)." << endl;
        }
        return false;
    }
    if(inEdges.size() < 2 and outEdges.size() < 2) {
        if(debug) {
            cout << "Not detangling due to degree (case 2)." << endl;
        }
        return false;
    }

    // Compute the tangle matrix.
    vector< vector<uint64_t> > tangleMatrix;
    computeTangleMatrix(inEdges, outEdges, tangleMatrix, false);

    if(debug) {
        cout << "Tangle matrix for vertex " << cGraph[cv].edgeId << endl;

        cout << "In-edges: ";
        for(const edge_descriptor ce: inEdges) {
            cout << " " << bubbleChainStringId(ce);
        }
        cout << endl;

        cout << "Out-edges: ";
        for(const edge_descriptor ce: outEdges) {
            cout << " " << bubbleChainStringId(ce);
        }
        cout << endl;

        for(uint64_t i0=0; i0<inEdges.size(); i0++) {
            for(uint64_t i1=0; i1<outEdges.size(); i1++) {
                cout << bubbleChainStringId(inEdges[i0]) << " " <<
                    bubbleChainStringId(outEdges[i1]) << " " <<
                    tangleMatrix[i0][i1] << endl;
            }
        }
    }

    // Count the number of significant, ambiguous, and negligible elements
    // in the tangle matrix.
    uint64_t significantCount = 0;
    uint64_t ambiguousCount = 0;
    uint64_t negligibleCount = 0;
    for(uint64_t i0=0; i0<inEdges.size(); i0++) {
        for(uint64_t i1=0; i1<outEdges.size(); i1++) {
            const uint64_t t = tangleMatrix[i0][i1];
            if(t <= detangleToleranceLow) {
                ++negligibleCount;
            } else if(t >= detangleToleranceHigh) {
                ++significantCount;
            } else {
                ++ambiguousCount;
            }
        }
    }

    // If the tangle matrix contains any ambiguous elements, do nothing.
    if(ambiguousCount > 0) {
        return false;
    }

    // There are no ambiguous elements.
    // If there are no negligible element, that is all elements of the tangle matrix are significant,
    // there is nothing to do.
    if(negligibleCount == 0) {
        return false;
    }

    // To avoid breaking contiguity, we require each column and each row of the
    // tangle matrix to have at least one significant element.
    // This means that each in-edge will be "merged" with at least one out-edge,
    // and each out-edge will be "merged" with at least one in-edge.
    for(uint64_t i0=0; i0<inEdges.size(); i0++) {
        bool foundSignificant = false;
        for(uint64_t i1=0; i1<outEdges.size(); i1++) {
            if(tangleMatrix[i0][i1] >= detangleToleranceHigh) {
                foundSignificant = true;
                break;
            }
        }
        if(not foundSignificant) {
            return false;
        }
    }
    for(uint64_t i1=0; i1<outEdges.size(); i1++) {
        bool foundSignificant = false;
        for(uint64_t i0=0; i0<inEdges.size(); i0++) {
            if(tangleMatrix[i0][i1] >= detangleToleranceHigh) {
                foundSignificant = true;
                break;
            }
        }
        if(not foundSignificant) {
            return false;
        }
    }

    if(debug) {
        cout << "This vertex will be detangled " << inEdges.size() << " by " << outEdges.size() << endl;
    }


    // Create truncated versions of the inEdges and outEdges.
    vector<vertex_descriptor> inVertices;
    for(const edge_descriptor ce: inEdges) {
        inVertices.push_back(cloneAndTruncateAtEnd(ce));
    }
    vector<vertex_descriptor> outVertices;
    for(const edge_descriptor ce: outEdges) {
        outVertices.push_back(cloneAndTruncateAtBeginning(ce));
    }

    // Each significant element of the tangle matrix generates a new edge.
    for(uint64_t i0=0; i0<inEdges.size(); i0++) {
        for(uint64_t i1=0; i1<outEdges.size(); i1++) {
            if(tangleMatrix[i0][i1] >= detangleToleranceHigh) {
                connect(inVertices[i0], outVertices[i1]);
            }
        }
    }


#if 0
    // Each significant element of the tangle matrix generates a new edge,
    // obtained by "merging" an in-edge with an out-edge.
    for(uint64_t i0=0; i0<inEdges.size(); i0++) {
        const edge_descriptor ce0 = inEdges[i0];
        const BubbleChain& bubbleChain0 = cGraph[ce0];
        const Bubble& bubble0 = bubbleChain0.lastBubble();
        SHASTA_ASSERT(bubble0.isHaploid());
        const Chain& chain0 = bubble0.front();
        SHASTA_ASSERT(chain0.size() >= 2);
        for(uint64_t i1=0; i1<outEdges.size(); i1++) {
            if(tangleMatrix[i0][i1] < detangleToleranceHigh) {
                continue;
            }
            const edge_descriptor ce1 = outEdges[i1];
            const BubbleChain& bubbleChain1 = cGraph[ce1];
            const Bubble& bubble1 = bubbleChain1.firstBubble();
            SHASTA_ASSERT(bubble1.isHaploid());
            const Chain& chain1 = bubble1.front();
            SHASTA_ASSERT(chain1.size() >= 2);

            edge_descriptor eNew;
            tie(eNew, ignore) = add_edge(source(ce0, cGraph), target(ce1, graph), cGraph);
            CompressedPathGraph1BEdge& newEdge = cGraph[eNew];
            newEdge.id = nextEdgeId++;
            BubbleChain& newBubbleChain = newEdge;

            if(debug) {
                cout << "Merging " <<
                    bubbleChainStringId(ce0) << " " <<
                    bubbleChainStringId(ce1) << " into " <<
                    bubbleChainStringId(eNew) << endl;
            }

            // Create the new BubbleChain. It is obtained by joining
            // bubbleChain0 and bubbleChain1, with vertex cv
            // removed from the end of bubbleChain0
            // and from the beginning of bubbleChain1.
            // Here we use the above assumption that
            // the last bubble of bubbleChain0 and the first bubble of bubbleChain1
            // are haploid.
            newBubbleChain = bubbleChain0;

            // Remove cv from the end.
            Bubble& newBubbleLast = newBubbleChain.back();
            SHASTA_ASSERT(newBubbleLast.size() == 1);
            Chain& newChainLast = newBubbleLast.front();
            SHASTA_ASSERT(newChainLast.back() == cGraph[cv].edgeId);
            newChainLast.resize(newChainLast.size() - 1);

            // Append chain1, except for cv.
            SHASTA_ASSERT(chain1.front() == cGraph[cv].edgeId);
            copy(chain1.begin() + 1, chain1.end(), back_inserter(newChainLast));

            // Append the rest of bubbleChain1.
            copy(bubbleChain1.begin() + 1, bubbleChain1.end(), back_inserter(newBubbleChain));
        }

    }
#endif

    // Now we can remove cv and all of its in-edges and out-edges.
    clear_vertex(cv, cGraph);
    cGraph.removeVertex(cv);

    return true;
}



// Ths version can handle the case where the last bubble of an in-edge
// or the first bubble of an out-edge is not haploid.
// It works like this:
// - Compute a generalized tangle matrix taking using the next to last
//   MarkerGraphEdgeId of each incoming chain
//   and the second MarkerGraphEdgeId of each outgoing chain.
// - If detangling is possible based on this generalized tangle matrix,
//   split the last bubble of each incoming edge and the first
//   bubble of each outgoing edge. After this operation,
//   the last bubble of each in-edge is haploid and the first bubble
//   of each out-edge is haploid.
// - Call detangleVertex to do the detangling.
bool CompressedPathGraph1B::detangleVertexGeneral(
    vertex_descriptor cv,
    bool debug,
    uint64_t detangleToleranceLow,
    uint64_t detangleToleranceHigh)
{
    CompressedPathGraph1B& cGraph = *this;

#if 0
    // Use detangleVertex, if possible.
    bool involvesNonHaploidBubbles = false;
    BGL_FORALL_INEDGES(cv, ce, cGraph, CompressedPathGraph1B) {
        const BubbleChain& bubbleChain = cGraph[ce];
        if(not bubbleChain.lastBubble().isHaploid()) {
            involvesNonHaploidBubbles = true;
        }
    }
    BGL_FORALL_OUTEDGES(cv, ce, cGraph, CompressedPathGraph1B) {
        const BubbleChain& bubbleChain = cGraph[ce];
        if(not bubbleChain.firstBubble().isHaploid()) {
            involvesNonHaploidBubbles = true;
        }
    }
    if(not involvesNonHaploidBubbles) {
        if(debug) {
            cout << "No non-haploid bubbles involved, using detangleVertex." << endl;
        }
        return detangleVertex(cv, debug, detangleToleranceLow, detangleToleranceHigh);
    }
#endif

    if(in_degree(cv, cGraph) < 2 or out_degree(cv, cGraph) < 2) {
        return false;
    }

    if(debug) {
        cout << "Attempting general detangling for vertex " << cGraph[cv].edgeId << endl;
    }

    class ChainInfo {
    public:
        edge_descriptor ce;
        uint64_t indexInBubble;
        MarkerGraphEdgeId edgeId;
    };
    vector<ChainInfo> inChains;
    BGL_FORALL_INEDGES(cv, ce, cGraph, CompressedPathGraph1B) {
        const BubbleChain& bubbleChain = cGraph[ce];
        const Bubble& bubble = bubbleChain.lastBubble();
        for(uint64_t indexInBubble=0; indexInBubble<bubble.size(); indexInBubble++) {
            const Chain& chain = bubble[indexInBubble];
            inChains.push_back({ce, indexInBubble, chain[chain.size() - 2]});
        }
    }
    vector<ChainInfo> outChains;
    BGL_FORALL_OUTEDGES(cv, ce, cGraph, CompressedPathGraph1B) {
        const BubbleChain& bubbleChain = cGraph[ce];
        const Bubble& bubble = bubbleChain.firstBubble();
        for(uint64_t indexInBubble=0; indexInBubble<bubble.size(); indexInBubble++) {
            const Chain& chain = bubble[indexInBubble];
            outChains.push_back({ce, indexInBubble, chain[1]});
        }
    }

    if(debug) {

        cout << "In:" << endl;
        for(const ChainInfo& chainInfo: inChains) {
            cout << bubbleChainStringId(chainInfo.ce) << " " <<
                chainInfo.indexInBubble << " " <<
                chainInfo.edgeId << endl;
        }

        cout << "Out:" << endl;
        for(const ChainInfo& chainInfo: outChains) {
            cout << bubbleChainStringId(chainInfo.ce) << " " <<
                chainInfo.indexInBubble << " " <<
                chainInfo.edgeId << endl;
        }
    }

    // Compute a generalized tangle matrix.
    vector<vector<uint64_t> > tangleMatrix(inChains.size(), vector<uint64_t>(outChains.size()));
    for(uint64_t i0=0; i0<inChains.size(); i0++) {
        const MarkerGraphEdgeId markerGraphEdgeId0 = inChains[i0].edgeId;

        for(uint64_t i1=0; i1<outChains.size(); i1++) {
            const MarkerGraphEdgeId markerGraphEdgeId1 = outChains[i1].edgeId;

            MarkerGraphEdgePairInfo info;
            SHASTA_ASSERT(assembler.analyzeMarkerGraphEdgePair(markerGraphEdgeId0, markerGraphEdgeId1, info));
            tangleMatrix[i0][i1] = info.common;
        }
    }

    if(debug) {
        cout << "Tangle matrix:" << endl;
        for(uint64_t i0=0; i0<inChains.size(); i0++) {
            const ChainInfo& chainInfo0 = inChains[i0];
            for(uint64_t i1=0; i1<outChains.size(); i1++) {
                const ChainInfo& chainInfo1 = outChains[i1];

                cout <<
                    bubbleChainStringId(chainInfo0.ce) << " " <<
                    chainInfo0.indexInBubble << " " <<
                    chainInfo0.edgeId << " " <<
                    bubbleChainStringId(chainInfo1.ce) << " " <<
                    chainInfo1.indexInBubble << " " <<
                    chainInfo1.edgeId << " " <<
                    tangleMatrix[i0][i1] << endl;
            }
        }

    }

    // Count the number of significant, ambiguous, and negligible elements
    // in the tangle matrix.
    uint64_t significantCount = 0;
    uint64_t ambiguousCount = 0;
    uint64_t negligibleCount = 0;
    for(uint64_t i0=0; i0<inChains.size(); i0++) {
        for(uint64_t i1=0; i1<outChains.size(); i1++) {
            const uint64_t t = tangleMatrix[i0][i1];
            if(t <= detangleToleranceLow) {
                ++negligibleCount;
            } else if(t >= detangleToleranceHigh) {
                ++significantCount;
            } else {
                ++ambiguousCount;
            }
        }
    }

    // If the tangle matrix contains any ambiguous elements, do nothing.
    if(ambiguousCount > 0) {
        if(debug) {
            cout << "Tangle matrix is ambiguous." << endl;
        }
        return false;
    }
    // There are no ambiguous elements.
    // If there are no negligible element, that is all elements of the tangle matrix are significant,
    // there is nothing to do.
    if(negligibleCount == 0) {
        return false;
    }

    // To avoid breaking contiguity, we require each column and each row of the
    // tangle matrix to have at least one significant element.
    // This means that each in-edge will be "merged" with at least one out-edge,
    // and each out-edge will be "merged" with at least one in-edge.
    for(uint64_t i0=0; i0<inChains.size(); i0++) {
        bool foundSignificant = false;
        for(uint64_t i1=0; i1<outChains.size(); i1++) {
            if(tangleMatrix[i0][i1] >= detangleToleranceHigh) {
                foundSignificant = true;
                break;
            }
        }
        if(not foundSignificant) {
            return false;
        }
    }
    for(uint64_t i1=0; i1<outChains.size(); i1++) {
        bool foundSignificant = false;
        for(uint64_t i0=0; i0<inChains.size(); i0++) {
            if(tangleMatrix[i0][i1] >= detangleToleranceHigh) {
                foundSignificant = true;
                break;
            }
        }
        if(not foundSignificant) {
            return false;
        }
    }

    if(debug) {
        cout << "This vertex can be detangled after some splitting of bubble chains." << endl;
    }


    // Make sure the last bubble of all in-edges is haploid.
    in_edge_iterator itIn, itInEnd;
    tie(itIn, itInEnd) = in_edges(cv, cGraph);
    while(itIn != itInEnd) {
        const edge_descriptor ce = *itIn;
        ++itIn; // Increment before possibly removing this edge!
        const BubbleChain& bubbleChain = cGraph[ce];
        if(not bubbleChain.lastBubble().isHaploid()) {
            if(debug) {
                cout << "In-edge " << bubbleChainStringId(ce) <<
                    " needs to be split at the end." << endl;
            }
            splitBubbleChainAtEnd(ce);
        }
    }

    // Make sure the first bubble of all out-edges is haploid.
    out_edge_iterator itOut, itOutEnd;
    tie(itOut, itOutEnd) = out_edges(cv, cGraph);
    while(itOut != itOutEnd) {
        const edge_descriptor ce = *itOut;
        ++itOut; // Increment before possibly removing this edge!
        const BubbleChain& bubbleChain = cGraph[ce];
        if(not bubbleChain.firstBubble().isHaploid()) {
            if(debug) {
                cout << "Out-edge " << bubbleChainStringId(ce) <<
                    " needs to be split at the beginning." << endl;
            }
            splitBubbleChainAtBeginning(ce);
        }
    }

    // Now we can detangle using detangleVertex.
    if(debug) {
        cout << "Calling detangleVertex." << endl;
    }
    return detangleVertex(cv, debug, detangleToleranceLow, detangleToleranceHigh);
}



// Split the first bubble of a bubble chain.
// Used by detangleVertexGeneral to eliminate
// non-haploid bubbles adjacent to a vertex to be detangled.
void CompressedPathGraph1B::splitBubbleChainAtBeginning(edge_descriptor ce)
{
    CompressedPathGraph1B& cGraph = *this;

    const BubbleChain& bubbleChain = cGraph[ce];
    const Bubble& firstBubble = bubbleChain.firstBubble();
    SHASTA_ASSERT(not firstBubble.isHaploid());

    const vertex_descriptor cv0 = source(ce, cGraph);
    const vertex_descriptor cv1 = target(ce, cGraph);



    // General case where the bubble chain has more than one bubble.
    // Generate a new edge containing the bubble chain except for
    // the first bubble, plus one new edge for each chain in the firstBubble.
    if(bubbleChain.size() > 1) {

        // Generate one new edge containing the bubble chain except for
        // the first bubble.
        CompressedPathGraph1BEdge newEdge;
        newEdge.id = nextEdgeId++;
        copy(bubbleChain.begin() + 1, bubbleChain.end(), back_inserter(newEdge));
        const vertex_descriptor cv2 = createVertex(newEdge.front().front().front());
        boost::add_edge(cv2, cv1, newEdge, cGraph);

        // Generate a  new edge for each chain in the firstBubble.
        for(const Chain& chain: firstBubble) {
            CompressedPathGraph1BEdge newEdge;
            newEdge.resize(1);  // The new edge has only one bubble.
            Bubble& newBubble = newEdge.front();
            newEdge.id = nextEdgeId++;
            newBubble.push_back(chain);
            boost::add_edge(cv0, cv2, newEdge, cGraph);
        }
    }


    // Special case where the bubble chain has one bubble.
    // We generate one new edge for each chain in the firstBubble.
    else {

        // Generate a new edge for each chain in the firstBubble.
        for(const Chain& chain: firstBubble) {
            CompressedPathGraph1BEdge newEdge;
            newEdge.resize(1);  // The new edge has only one bubble.
            Bubble& newBubble = newEdge.front();
            newEdge.id = nextEdgeId++;
            newBubble.push_back(chain);
            boost::add_edge(cv0, cv1, newEdge, cGraph);
        }
    }

    // Now we can remove the original bubble chain.
    boost::remove_edge(ce, cGraph);
}



// Split the last bubble of a bubble chain.
// Used by detangleVertexGeneral to eliminate
// non-haploid bubbles adjacent to a vertex to be detangled.
void CompressedPathGraph1B::splitBubbleChainAtEnd(edge_descriptor ce)
{
    CompressedPathGraph1B& cGraph = *this;

    const BubbleChain& bubbleChain = cGraph[ce];
    const Bubble& lastBubble = bubbleChain.lastBubble();
    SHASTA_ASSERT(not lastBubble.isHaploid());

    const vertex_descriptor cv0 = source(ce, cGraph);
    const vertex_descriptor cv1 = target(ce, cGraph);



    // General case where the bubble chain has more than one bubble.
    // Generate a new edge containing the bubble chain except for
    // the last bubble, plus one new edge for each chain in the lastBubble.
    if(bubbleChain.size() > 1) {

        // Generate one new edge containing the bubble chain except for
        // the last bubble.
        CompressedPathGraph1BEdge newEdge;
        newEdge.id = nextEdgeId++;
        copy(bubbleChain.begin(), bubbleChain.end()-1, back_inserter(newEdge));
        const vertex_descriptor cv2 = createVertex(newEdge.back().front().back());
        boost::add_edge(cv0, cv2, newEdge, cGraph);

        // Generate a  new edge for each chain in the lastBubble.
        for(const Chain& chain: lastBubble) {
            CompressedPathGraph1BEdge newEdge;
            newEdge.resize(1);  // The new edge has only one bubble.
            Bubble& newBubble = newEdge.front();
            newEdge.id = nextEdgeId++;
            newBubble.push_back(chain);
            boost::add_edge(cv2, cv1, newEdge, cGraph);
        }
    }


    // Special case where the bubble chain has one bubble.
    // We generate one new edge for each chain in the lastBubble.
    else {

        // Generate a new edge for each chain in the lastBubble.
        for(const Chain& chain: lastBubble) {
            CompressedPathGraph1BEdge newEdge;
            newEdge.resize(1);  // The new edge has only one bubble.
            Bubble& newBubble = newEdge.front();
            newEdge.id = nextEdgeId++;
            newBubble.push_back(chain);
            boost::add_edge(cv0, cv1, newEdge, cGraph);
        }
    }

    // Now we can remove the original bubble chain.
    boost::remove_edge(ce, cGraph);
}



bool CompressedPathGraph1B::detangleEdges(
    uint64_t detangleToleranceLow,
    uint64_t detangleToleranceHigh)
{
    const bool debug = false;
    if(debug) {
        cout << "Detangling edges." << endl;
    }

    CompressedPathGraph1B& cGraph = *this;

    // To safely iterate over edges while removing edges we must use edge ids
    // as unique identifiers, because edge descriptors can be reused as edges are
    // deleted ndw new edges are created.
    std::map<uint64_t, edge_descriptor> edgeMap;
    BGL_FORALL_EDGES(ce, cGraph, CompressedPathGraph1B) {
        edgeMap.insert({cGraph[ce].id, ce});
    }

    uint64_t detangleCount = 0;;
    for(auto it=edgeMap.begin(); it!=edgeMap.end(); /* Incremented safely by detangleEdgeStrict */) {
        if(detangleEdge(edgeMap, it, detangleToleranceLow, detangleToleranceHigh)) {
            ++detangleCount;
        }
    }

    if(true) {
        cout << "Detangled " << detangleCount << " edges." << endl;
    }

    return detangleCount > 0;
}



bool CompressedPathGraph1B::detangleEdge(
    std::map<uint64_t, edge_descriptor>& edgeMap,
    std::map<uint64_t, edge_descriptor>::iterator& it,
    uint64_t detangleToleranceLow,
    uint64_t detangleToleranceHigh)
{
    CompressedPathGraph1B& cGraph = *this;
    const edge_descriptor ce = it->second;
    ++it;
    // edgeMap.erase(cGraph[ce].id);

    const bool debug = false;

    // Tangle matrix elements <= detangleToleranceLow are treated as negigible.
    // Tangle matrix elements >= detangleToleranceHigh are treated as significant.
    // Tangle matrix elements in between are considered ambiguous.
    SHASTA_ASSERT(detangleToleranceHigh > detangleToleranceLow);

    const vertex_descriptor cv0 = source(ce, cGraph);
    const vertex_descriptor cv1 = target(ce, cGraph);

    if(out_degree(cv0, cGraph) != 1) {
        return false;
    }
    if(in_degree(cv1, cGraph) != 1) {
        return false;
    }

    if(debug) {
        cout << "Attempting to detangle edge " << bubbleChainStringId(ce) << endl;
    }

    // Gather the in-edges and check that the last bubble is haploid.
    // Ignore in-edges coming from cv1 (back-edges).
    vector<edge_descriptor> inEdges;
    vector<edge_descriptor> backEdges;
    BGL_FORALL_INEDGES(cv0, ce, cGraph, CompressedPathGraph1B) {
        const BubbleChain& bubbleChain = cGraph[ce];
        if(not bubbleChain.lastBubble().isHaploid()) {
            if(debug) {
                cout << "Not detangling because the last bubble of in-edge " <<
                    bubbleChainStringId(ce) << " is not haploid." << endl;
            }
            return false;
        }
        if(source(ce, cGraph) != cv1) {
            inEdges.push_back(ce);
        } else {
            backEdges.push_back(ce);
        }
    }

    // Gather the out-edges and check that the first bubble is haploid.
    // Ignore out-edges going to cv0 (back-edges).
    vector<edge_descriptor> outEdges;
    BGL_FORALL_OUTEDGES(cv1, ce, cGraph, CompressedPathGraph1B) {
        const BubbleChain& bubbleChain = cGraph[ce];
        if(not bubbleChain.firstBubble().isHaploid()) {
            if(debug) {
                cout << "Not detangling because the first bubble of out-edge " <<
                    bubbleChainStringId(ce) << " is not haploid." << endl;
            }
            return false;
        }
        if(target(ce, cGraph) != cv0) {
            outEdges.push_back(ce);
        }
    }

    if(inEdges.size() == 0 or outEdges.size() == 0) {
        if(debug) {
            cout << "Not detangling due to degree (case 1)." << endl;
        }
        return false;
    }
    if(inEdges.size() < 2 and outEdges.size() < 2) {
        if(debug) {
            cout << "Not detangling due to degree (case 2)." << endl;
        }
        return false;
    }

    // Compute the tangle matrix.
    vector< vector<uint64_t> > tangleMatrix;
    computeTangleMatrix(inEdges, outEdges, tangleMatrix, false);

    if(debug) {
        cout << "Computing tangle matrix for edge " << bubbleChainStringId(ce) << endl;

        cout << "In-edges: ";
        for(const edge_descriptor ce: inEdges) {
            cout << " " << bubbleChainStringId(ce);
        }
        cout << endl;

        cout << "Out-edges: ";
        for(const edge_descriptor ce: outEdges) {
            cout << " " << bubbleChainStringId(ce);
        }
        cout << endl;

        cout << "Tangle matrix:" << endl;
        for(uint64_t i0=0; i0<inEdges.size(); i0++) {
            const edge_descriptor ce0 = inEdges[i0];
            for(uint64_t i1=0; i1<outEdges.size(); i1++) {
                const edge_descriptor ce1 = outEdges[i1];
                cout <<
                    bubbleChainStringId(ce0) << " " <<
                    bubbleChainStringId(ce1) << " " <<
                    tangleMatrix[i0][i1];
                if(tangleMatrix[i0][i1] == 0) {
                    cout << " zero tangle matrix element";
                }
                cout << endl;
            }
        }
    }

    // Count the number of significant, ambiguous, and negligible elements
    // in the tangle matrix.
    uint64_t significantCount = 0;
    uint64_t ambiguousCount = 0;
    uint64_t negligibleCount = 0;
    for(uint64_t i0=0; i0<inEdges.size(); i0++) {
        for(uint64_t i1=0; i1<outEdges.size(); i1++) {
            const uint64_t t = tangleMatrix[i0][i1];
            if(t <= detangleToleranceLow) {
                ++negligibleCount;
            } else if(t >= detangleToleranceHigh) {
                ++significantCount;
            } else {
                ++ambiguousCount;
            }
        }
    }

    // If the tangle matrix contains any ambiguous elements, do nothing.
    if(ambiguousCount > 0) {
        return false;
    }

    // There are no ambiguous elements.
    // If there are no negligible element, that is all elements of the tangle matrix are significant,
    // there is nothing to do.
    if(negligibleCount == 0) {
        return false;
    }

    // To avoid breaking contiguity, we require each column and each row of the
    // tangle matrix to have at least one significant element.
    // This means that each in-edge will be "merged" with at least one out-edge,
    // and each out-edge will be "merged" with at least one in-edge.
    for(uint64_t i0=0; i0<inEdges.size(); i0++) {
        bool foundSignificant = false;
        for(uint64_t i1=0; i1<outEdges.size(); i1++) {
            if(tangleMatrix[i0][i1] >= detangleToleranceHigh) {
                foundSignificant = true;
                break;
            }
        }
        if(not foundSignificant) {
            return false;
        }
    }
    for(uint64_t i1=0; i1<outEdges.size(); i1++) {
        bool foundSignificant = false;
        for(uint64_t i0=0; i0<inEdges.size(); i0++) {
            if(tangleMatrix[i0][i1] >= detangleToleranceHigh) {
                foundSignificant = true;
                break;
            }
        }
        if(not foundSignificant) {
            return false;
        }
    }

#if 0
    // In an in-edge is also an out-edge, don't detangle.
    for(const edge_descriptor ce: inEdges) {
        if(find(outEdges.begin(), outEdges.end(), ce) != outEdges.end()) {
            if(debug) {
                cout << "Not degangled because an in-edge is also an out-edge." << endl;
            }
            return false;
        }
    }
#endif

    if(debug) {
        cout << "This edge will be detangled " << inEdges.size() << " by " << outEdges.size() << endl;
    }

    // Create truncated versions of the inEdges and outEdges.
    vector<vertex_descriptor> inVertices;
    for(const edge_descriptor ce: inEdges) {
        inVertices.push_back(cloneAndTruncateAtEnd(ce));
    }
    vector<vertex_descriptor> outVertices;
    for(const edge_descriptor ce: outEdges) {
        outVertices.push_back(cloneAndTruncateAtBeginning(ce));
    }


    // Each significant element of the tangle matrix generates a new edge.
    for(uint64_t i0=0; i0<inEdges.size(); i0++) {
        for(uint64_t i1=0; i1<outEdges.size(); i1++) {
            if(tangleMatrix[i0][i1] >= detangleToleranceHigh) {
                connect(inVertices[i0], outVertices[i1]);
            }
        }
    }


    // Now we can remove cv0, cv1, ce, and all of the in-edges and out-edges.
    // We have to do this while safely incrementing the edge iterator to point to the
    // next edge that was not removed.
    // We already incremented the iterator to point past ce.
    boost::remove_edge(ce, cGraph);
    for(const edge_descriptor ce: inEdges) {
        if(it != edgeMap.end() and cGraph[ce].id == it->first) {
            ++it;
        }
        edgeMap.erase(cGraph[ce].id);
        boost::remove_edge(ce, cGraph);
    }
    for(const edge_descriptor ce: outEdges) {
        if(it != edgeMap.end() and cGraph[ce].id == it->first) {
            ++it;
        }
        edgeMap.erase(cGraph[ce].id);
        boost::remove_edge(ce, cGraph);
    }
    for(const edge_descriptor ce: backEdges) {
        if(it != edgeMap.end() and cGraph[ce].id == it->first) {
            ++it;
        }
        edgeMap.erase(cGraph[ce].id);
        boost::remove_edge(ce, cGraph);
    }
    cGraph.removeVertex(cv0);
    cGraph.removeVertex(cv1);

    return true;
}



// Detangle short superbubbles with any number of entrances and exits.
bool CompressedPathGraph1B::detangleShortSuperbubbles(
    uint64_t maxOffset1,    // Used to define superbubbles
    uint64_t detangleToleranceLow,
    uint64_t detangleToleranceHigh)
{
    CompressedPathGraph1B& cGraph = *this;

    // Find the superbubbles.
    Superbubbles superbubbles(cGraph, maxOffset1);

    // Loop over the superbubbles.
    bool changesWereMade = false;
    for(uint64_t superbubbleId=0; superbubbleId<superbubbles.size(); superbubbleId++) {
        if(detangleShortSuperbubble(
            superbubbles, superbubbleId, detangleToleranceLow, detangleToleranceHigh)) {
            changesWereMade = true;
        }
    }

    return changesWereMade;
}



bool CompressedPathGraph1B::detangleShortSuperbubble(
    const Superbubbles& superbubbles,
    uint64_t superbubbleId,
    uint64_t detangleToleranceLow,
    uint64_t detangleToleranceHigh)
{
    CompressedPathGraph1B& cGraph = *this;
    const Superbubble& superbubble = superbubbles.getSuperbubble(superbubbleId);
    const bool debug = false;

    if(debug) {
        cout << "Found a superbubble with " << superbubble.size() <<
            " vertices:";
        for(const vertex_descriptor cv: superbubble) {
            cout << " " << cGraph[cv].edgeId;
        }
        cout << endl;
    }

    // Fill in the in-edges and out-edges.
    // These cannot be computed while constructing the superbubbles
    // as they can change when other superbubbles are detangled.
    vector<edge_descriptor> inEdges;
    vector<edge_descriptor> outEdges;
    for(const vertex_descriptor cv0: superbubble) {
        BGL_FORALL_INEDGES(cv0, ce, cGraph, CompressedPathGraph1B) {
            const vertex_descriptor cv1 = source(ce, cGraph);
            if(not superbubbles.isInSuperbubble(superbubbleId, cv1)) {
                inEdges.push_back(ce);
            }
        }
        BGL_FORALL_OUTEDGES(cv0, ce, cGraph, CompressedPathGraph1B) {
            const vertex_descriptor cv1 = target(ce, cGraph);
            if(not superbubbles.isInSuperbubble(superbubbleId, cv1)) {
                outEdges.push_back(ce);
            }
        }
    }
    const uint64_t inDegree = inEdges.size();
    const uint64_t outDegree = outEdges.size();

    if(debug) {
        cout << inDegree << " in-edges:";
        for(const edge_descriptor ce: inEdges) {
            cout << " " << bubbleChainStringId(ce);
        }
        cout << endl;
        cout << outDegree << " out-edges:";
        for(const edge_descriptor ce: outEdges) {
            cout << " " << bubbleChainStringId(ce);
        }
        cout << endl;
    }

    if(inDegree == 0 or outDegree == 0) {
        if(debug) {
            cout << "Not detangling due to degree (case 1)." << endl;
        }
        return false;
    }
    if(inDegree < 2 and outDegree < 2) {
        if(debug) {
            cout << "Not detangling due to degree (case 2)." << endl;
        }
        return false;
    }

    // This requires the last bubble of each in-edge
    // and the first bubble of each out-edge to be haploid.
    bool canDo = true;
    for(const edge_descriptor ce: inEdges) {
        const BubbleChain& bubbleChain = cGraph[ce];
        if(not bubbleChain.lastBubble().isHaploid()) {
            if(debug) {
                cout << "Not detangling because the last bubble of in-edge " <<
                    bubbleChainStringId(ce) << " is not haploid." << endl;
            }
            canDo = false;
            break;
        }
    }
    for(const edge_descriptor ce: outEdges) {
        const BubbleChain& bubbleChain = cGraph[ce];
        if(not bubbleChain.firstBubble().isHaploid()) {
            if(debug) {
                cout << "Not detangling because the first bubble of out-edge " <<
                    bubbleChainStringId(ce) << " is not haploid." << endl;
            }
            canDo = false;
            break;
        }
    }
    if(not canDo) {
        return false;
    }


    // Compute the tangle matrix.
    vector< vector<uint64_t> > tangleMatrix;
    computeTangleMatrix(inEdges, outEdges, tangleMatrix, true);

    if(debug) {
        cout << "Tangle matrix:" << endl;
        for(uint64_t i0=0; i0<inDegree; i0++) {
            const edge_descriptor inEdge = inEdges[i0];
            for(uint64_t i1=0; i1<outDegree; i1++) {
                const edge_descriptor outEdge = outEdges[i1];

                cout << bubbleChainStringId(inEdge) << " " <<
                    bubbleChainStringId(outEdge) << " " << tangleMatrix[i0][i1];

                cout << endl;
            }
        }
    }

    // Count the number of significant, ambiguous, and negligible elements
    // in the tangle matrix.
    uint64_t significantCount = 0;
    uint64_t ambiguousCount = 0;
    uint64_t negligibleCount = 0;
    for(uint64_t i0=0; i0<inDegree; i0++) {
        for(uint64_t i1=0; i1<outDegree; i1++) {
            const uint64_t t = tangleMatrix[i0][i1];
            if(t <= detangleToleranceLow) {
                ++negligibleCount;
            } else if(t >= detangleToleranceHigh) {
                ++significantCount;
            } else {
                ++ambiguousCount;
            }
        }
    }

    // If the tangle matrix contains any ambiguous elements, do nothing.
    if(ambiguousCount > 0) {
        if(debug) {
            cout << "Not detangled because the tangle matrix contains ambiguous elements." << endl;
        }
        return false;
    }

    // There are no ambiguous elements.
    // If there are no negligible element, that is all elements of the tangle matrix are significant,
    // there is nothing to do.
    if(negligibleCount == 0) {
        if(debug) {
            cout << "Not detangled because the tangle matrix contains no negligible elements." << endl;
        }
        return false;
    }

    // To avoid breaking contiguity, we require each column and each row of the
    // tangle matrix to have at least one significant element.
    // This means that each in-edge will be "merged" with at least one out-edge,
    // and each out-edge will be "merged" with at least one in-edge.
    bool ok = true;
    for(uint64_t i0=0; i0<inDegree; i0++) {
        bool foundSignificant = false;
        for(uint64_t i1=0; i1<outDegree; i1++) {
            if(tangleMatrix[i0][i1] >= detangleToleranceHigh) {
                foundSignificant = true;
                break;
            }
        }
        if(not foundSignificant) {
            ok = false;
            break;
        }
    }
    for(uint64_t i1=0; i1<outDegree; i1++) {
        bool foundSignificant = false;
        for(uint64_t i0=0; i0<inDegree; i0++) {
            if(tangleMatrix[i0][i1] >= detangleToleranceHigh) {
                foundSignificant = true;
                break;
            }
        }
        if(not foundSignificant) {
            ok = false;
            break;
        }
    }
    if(not ok) {
        if(debug) {
            cout << "Not detangled to avoid breaking contiguity." << endl;
        }
        return false;
    }

    if(debug) {
        cout << "This superbubble will be detangled." << endl;
    }

    // Create truncated versions of the inEdges and outEdges.
    vector<vertex_descriptor> inVertices;
    for(const edge_descriptor ce: inEdges) {
        inVertices.push_back(cloneAndTruncateAtEnd(ce));
    }
    vector<vertex_descriptor> outVertices;
    for(const edge_descriptor ce: outEdges) {
        outVertices.push_back(cloneAndTruncateAtBeginning(ce));
    }



    // Each significant element of the tangle matrix generates a new edge.
    for(uint64_t i0=0; i0<inEdges.size(); i0++) {
        for(uint64_t i1=0; i1<outEdges.size(); i1++) {
            if(tangleMatrix[i0][i1] >= detangleToleranceHigh) {
                connect(inVertices[i0], outVertices[i1]);
            }
        }
    }


#if 0
    // Each significant element of the tangle matrix generates a new edge,
    // obtained by "merging" an in-edge with an out-edge.
    for(uint64_t i0=0; i0<inEdges.size(); i0++) {
        const edge_descriptor ce0 = inEdges[i0];
        const BubbleChain& bubbleChain0 = cGraph[ce0];
        const Bubble& bubble0 = bubbleChain0.lastBubble();
        SHASTA_ASSERT(bubble0.isHaploid());
        const Chain& chain0 = bubble0.front();
        SHASTA_ASSERT(chain0.size() >= 2);
        for(uint64_t i1=0; i1<outEdges.size(); i1++) {
            if(tangleMatrix[i0][i1] < detangleToleranceHigh) {
                continue;
            }
            const edge_descriptor ce1 = outEdges[i1];
            const BubbleChain& bubbleChain1 = cGraph[ce1];
            const Bubble& bubble1 = bubbleChain1.firstBubble();
            SHASTA_ASSERT(bubble1.isHaploid());
            const Chain& chain1 = bubble1.front();
            SHASTA_ASSERT(chain1.size() >= 2);

            edge_descriptor eNew;
            tie(eNew, ignore) = add_edge(source(ce0, cGraph), target(ce1, cGraph), cGraph);
            CompressedPathGraph1BEdge& newEdge = cGraph[eNew];
            newEdge.id = nextEdgeId++;
            BubbleChain& newBubbleChain = newEdge;

            if(debug) {
                cout << "Merging " <<
                    bubbleChainStringId(ce0) << " " <<
                    bubbleChainStringId(ce1) << " into " <<
                    bubbleChainStringId(eNew) << endl;
            }

            // Create the new BubbleChain. It is obtained by joining
            // bubbleChain0 and bubbleChain1, with vertex cv
            // removed from the end of bubbleChain0
            // and from the beginning of bubbleChain1.
            // Here we use the above assumption that
            // the last bubble of bubbleChain0 and the first bubble of bubbleChain1
            // are haploid.
            newBubbleChain = bubbleChain0;

            // Remove the last marker graph edge, which is in the superbubble.
            Bubble& newBubbleLast = newBubbleChain.back();
            SHASTA_ASSERT(newBubbleLast.size() == 1);
            Chain& newChainLast = newBubbleLast.front();
            newChainLast.resize(newChainLast.size() - 1);

            // Append chain1, except for the first marker graph edge, which is in the superbubble.
            copy(chain1.begin() + 1, chain1.end(), back_inserter(newChainLast));

            // Append the rest of bubbleChain1.
            copy(bubbleChain1.begin() + 1, bubbleChain1.end(), back_inserter(newBubbleChain));
        }

    }
#endif

    // Now we can remove all the vertices in the superbubble.
    for(const vertex_descriptor cv: superbubble) {
        clear_vertex(cv, cGraph);
        remove_vertex(cv, cGraph);
    }

    return true;
}



// The above versions require the last bubble of superbubble in-edges
// and the first bubble of superbubble out-edges to be haploid.
// This version does not.
bool CompressedPathGraph1B::detangleShortSuperbubblesGeneral(
    uint64_t maxOffset1,    // Used to define superbubbles
    uint64_t detangleToleranceLow,
    uint64_t detangleToleranceHigh)
{
    CompressedPathGraph1B& cGraph = *this;

    // Find the superbubbles.
    Superbubbles superbubbles(cGraph, maxOffset1);

    // Loop over the superbubbles.
    bool changesWereMade = false;
    for(uint64_t superbubbleId=0; superbubbleId<superbubbles.size(); superbubbleId++) {
        if(detangleShortSuperbubbleGeneral(
            superbubbles, superbubbleId, detangleToleranceLow, detangleToleranceHigh)) {
            changesWereMade = true;
        }
    }

    return changesWereMade;

}



bool CompressedPathGraph1B::detangleShortSuperbubbleGeneral(
    const Superbubbles& superbubbles,
    uint64_t superbubbleId,
    uint64_t detangleToleranceLow,
    uint64_t detangleToleranceHigh)
{
    CompressedPathGraph1B& cGraph = *this;
    const Superbubble& superbubble = superbubbles.getSuperbubble(superbubbleId);
    const bool debug = false;

    if(debug) {
        cout << "General detangling of a superbubble with " << superbubble.size() <<
            " vertices:";
        for(const vertex_descriptor cv: superbubble) {
            cout << " " << cGraph[cv].edgeId;
        }
        cout << endl;
    }

    // Gather the in-edges and out-edges.
    // These cannot be computed while constructing the superbubbles
    // as they can change when other superbubbles are detangled.
    vector<edge_descriptor> inEdges;
    vector<edge_descriptor> outEdges;
    for(const vertex_descriptor cv0: superbubble) {
        BGL_FORALL_INEDGES(cv0, ce, cGraph, CompressedPathGraph1B) {
            const vertex_descriptor cv1 = source(ce, cGraph);
            if(not superbubbles.isInSuperbubble(superbubbleId, cv1)) {
                inEdges.push_back(ce);
            }
        }
        BGL_FORALL_OUTEDGES(cv0, ce, cGraph, CompressedPathGraph1B) {
            const vertex_descriptor cv1 = target(ce, cGraph);
            if(not superbubbles.isInSuperbubble(superbubbleId, cv1)) {
                outEdges.push_back(ce);
            }
        }
    }

    if(debug) {
        cout << inEdges.size() << " in-edges:";
        for(const edge_descriptor ce: inEdges) {
            cout << " " << bubbleChainStringId(ce);
        }
        cout << endl;
        cout << outEdges.size() << " out-edges:";
        for(const edge_descriptor ce: outEdges) {
            cout << " " << bubbleChainStringId(ce);
        }
        cout << endl;
    }



    // See if we can use detangleShortSuperbubble.
    // This is the case if:
    // - The last bubble of every in-edge is haploid.
    // - The first bubble of every out-edge is haploid.
    bool isSimpleCase = true;
    for(const edge_descriptor ce: inEdges) {
        const BubbleChain& bubbleChain = cGraph[ce];
        if(not bubbleChain.lastBubble().isHaploid()) {
            isSimpleCase = false;
        }
    }
    for(const edge_descriptor ce: outEdges) {
        const BubbleChain& bubbleChain = cGraph[ce];
        if(not bubbleChain.firstBubble().isHaploid()) {
            isSimpleCase = false;
        }
    }
    if(isSimpleCase) {
        if(debug) {
            cout << "Simple case: calling detangleShortSuperbubble." << endl;
        }
        return detangleShortSuperbubble(superbubbles, superbubbleId,
            detangleToleranceLow, detangleToleranceHigh);
    }
    if(debug) {
        cout << "General case." << endl;
    }


    // We will compute a generalized tangle matrix that takes into
    // account all incoming and outgoing chains.

    // Gather the second to last marker graph edge of each chain of
    // the last bubble of each in-edge.
    vector<MarkerGraphEdgeId> in;
    for(const edge_descriptor ce: inEdges) {
        const BubbleChain& bubbleChain = cGraph[ce];
        const Bubble& lastBubble = bubbleChain.lastBubble();
        for(const Chain& chain: lastBubble) {
            in.push_back(chain.secondToLast());
        }
    }

    // Gather the second marker graph edge of each chain of
    // the first bubble of each out-edge.
    vector<MarkerGraphEdgeId> out;
    for(const edge_descriptor ce: outEdges) {
        const BubbleChain& bubbleChain = cGraph[ce];
        const Bubble& firstBubble = bubbleChain.firstBubble();
        for(const Chain& chain: firstBubble) {
            out.push_back(chain.second());
        }
    }

    if(debug) {
        cout << in.size() << " incoming marker graph edges:";
        for(const MarkerGraphEdgeId edgeId: in) {
            cout << " " << edgeId;
        }
        cout << endl;
        cout << out.size() << " outgoing marker graph edges:";
        for(const MarkerGraphEdgeId edgeId: out) {
            cout << " " << edgeId;
        }
        cout << endl;
    }

    // Compute the tangle matrix.
    vector< vector<uint64_t> > tangleMatrix(in.size(), vector<uint64_t>(out.size()));
    for(uint64_t i0=0; i0<in.size(); i0++) {
        const MarkerGraphEdgeId edgeId0 = in[i0];
        for(uint64_t i1=0; i1<out.size(); i1++) {
            const MarkerGraphEdgeId edgeId1 = out[i1];

            if(edgeId1 == assembler.markerGraph.reverseComplementEdge[edgeId0]) {
                tangleMatrix[i0][i1] = 0;
            } else {
                MarkerGraphEdgePairInfo info;
                SHASTA_ASSERT(assembler.analyzeMarkerGraphEdgePair(edgeId0, edgeId1, info));
                tangleMatrix[i0][i1] = info.common;
            }
        }
    }

    if(debug) {
        cout << "Tangle matrix:" << endl;
        for(uint64_t i0=0; i0<in.size(); i0++) {
            const MarkerGraphEdgeId edgeId0 = in[i0];
            for(uint64_t i1=0; i1<out.size(); i1++) {
                const MarkerGraphEdgeId edgeId1 = out[i1];
                cout << edgeId0 << " " << edgeId1 << " " << tangleMatrix[i0][i1] << endl;
            }
        }
    }

    // Count the number of significant, ambiguous, and negligible elements
    // in the tangle matrix.
    uint64_t significantCount = 0;
    uint64_t ambiguousCount = 0;
    uint64_t negligibleCount = 0;
    for(uint64_t i0=0; i0<in.size(); i0++) {
        for(uint64_t i1=0; i1<out.size(); i1++) {
            const uint64_t t = tangleMatrix[i0][i1];
            if(t <= detangleToleranceLow) {
                ++negligibleCount;
            } else if(t >= detangleToleranceHigh) {
                ++significantCount;
            } else {
                ++ambiguousCount;
            }
        }
    }

    // If the tangle matrix contains any ambiguous elements, do nothing.
    if(ambiguousCount > 0) {
        if(debug) {
            cout << "Not detangled because the tangle matrix contains ambiguous elements." << endl;
        }
        return false;
    }

    // There are no ambiguous elements.
    // If there are no negligible element, that is all elements of the tangle matrix are significant,
    // there is nothing to do.
    if(negligibleCount == 0) {
        if(debug) {
            cout << "Not detangled because the tangle matrix contains no negligible elements." << endl;
        }
        return false;
    }

    // To avoid breaking contiguity, we require each column and each row of the
    // tangle matrix to have at least one significant element.
    // This means that each in-edge will be "merged" with at least one out-edge,
    // and each out-edge will be "merged" with at least one in-edge.
    bool ok = true;
    for(uint64_t i0=0; i0<in.size(); i0++) {
        bool foundSignificant = false;
        for(uint64_t i1=0; i1<out.size(); i1++) {
            if(tangleMatrix[i0][i1] >= detangleToleranceHigh) {
                foundSignificant = true;
                break;
            }
        }
        if(not foundSignificant) {
            ok = false;
            break;
        }
    }
    for(uint64_t i1=0; i1<out.size(); i1++) {
        bool foundSignificant = false;
        for(uint64_t i0=0; i0<in.size(); i0++) {
            if(tangleMatrix[i0][i1] >= detangleToleranceHigh) {
                foundSignificant = true;
                break;
            }
        }
        if(not foundSignificant) {
            ok = false;
            break;
        }
    }
    if(not ok) {
        if(debug) {
            cout << "Not detangled to avoid breaking contiguity." << endl;
        }
        return false;
    }

    if(debug) {
        cout << "This superbubble will be detangled but splitting of some edges is required." << endl;
    }

    // Make sure the last bubble of every in-edge is haploid.
    for(const edge_descriptor ce: inEdges) {
        const BubbleChain& bubbleChain = cGraph[ce];
        if(not bubbleChain.lastBubble().isHaploid()) {
            if(debug) {
                cout << "In-edge " << bubbleChainStringId(ce) <<
                    " needs to be split at the end." << endl;
            }
            splitBubbleChainAtEnd(ce);
        }
    }

    // Make sure the first bubble of every out-edge is haploid.
    for(const edge_descriptor ce: outEdges) {
        const BubbleChain& bubbleChain = cGraph[ce];
        if(not bubbleChain.firstBubble().isHaploid()) {
            if(debug) {
                cout << "Out-edge " << bubbleChainStringId(ce) <<
                    " needs to be split at the beginning." << endl;
            }
            splitBubbleChainAtBeginning(ce);
        }
    }

    // Now we can call detangleShortSuperbubble.
    if(debug) {
        cout << "Calling detangleShortSuperbubble after splitting some edges." << endl;
    }
    return detangleShortSuperbubble(superbubbles, superbubbleId,
        detangleToleranceLow, detangleToleranceHigh);
}



// Special treatment to detangle back edges that were too long
// to be handled by detangleEdges.
bool CompressedPathGraph1B::detangleBackEdges(
    uint64_t detangleToleranceLow,
    uint64_t detangleToleranceHigh)
{
    cout << "Detangling back edges." << endl;
    CompressedPathGraph1B& cGraph = *this;

    // To safely iterate over edges while removing edges we must use edge ids
    // as unique identifiers, because edge descriptors can be reused as edges are
    // deleted a new edges are created.
    std::map<uint64_t, edge_descriptor> edgeMap;
    BGL_FORALL_EDGES(ce, cGraph, CompressedPathGraph1B) {
        edgeMap.insert({cGraph[ce].id, ce});
    }

    uint64_t detangleCount = 0;;
    for(auto it=edgeMap.begin(); it!=edgeMap.end(); /* Incremented safely by detangleEdgeStrict */) {
        if(detangleBackEdge(edgeMap, it, detangleToleranceLow, detangleToleranceHigh)) {
            ++detangleCount;
        }
    }
    cout << "Detangled " << detangleCount << " back edges." << endl;

    return detangleCount > 0;

}



// Special treatment to detangle back edges that were too long
// to be handled by detangleEdge.
bool CompressedPathGraph1B::detangleBackEdge(
    std::map<uint64_t, edge_descriptor>& edgeMap,
    std::map<uint64_t, edge_descriptor>::iterator& it,
    uint64_t detangleToleranceLow,
    uint64_t detangleToleranceHigh)
{
    CompressedPathGraph1B& cGraph = *this;
    const edge_descriptor ce = it->second;
    ++it;
    // edgeMap.erase(cGraph[ce].id);

    const bool debug = false;

    // Tangle matrix elements <= detangleToleranceLow are treated as negligible.
    // Tangle matrix elements >= detangleToleranceHigh are treated as significant.
    // Tangle matrix elements in between are considered ambiguous.
    SHASTA_ASSERT(detangleToleranceHigh > detangleToleranceLow);

    const vertex_descriptor cv0 = source(ce, cGraph);
    const vertex_descriptor cv1 = target(ce, cGraph);

    // Check the degrees.
    if(out_degree(cv0, cGraph) != 1) {
        return false;
    }
    if(in_degree(cv1, cGraph) != 1) {
        return false;
    }

    // Look for a back edge.
    vector<edge_descriptor> backEdges;
    BGL_FORALL_OUTEDGES(cv1, ce, cGraph, CompressedPathGraph1B) {
        if(target(ce, cGraph) == cv0) {
            backEdges.push_back(ce);
        }
    }
    if(backEdges.empty()) {
        return false;
    }

    // Only attempt to handle the case with a single back-edge.
    if(backEdges.size() != 1) {
        return false;
    }
    const edge_descriptor ceBack = backEdges.front();

    if(debug) {
        cout << "Attempting to detangle edge " << bubbleChainStringId(ce) <<
            " with back-edge " << bubbleChainStringId(ceBack) << endl;
    }

    // The back-edge is both an in-edge and an out-edge.
    // Store it at the first position of both inEdges and outEdges.

    // Gather the in-edges.
    vector<edge_descriptor> inEdges(1, ceBack);
    BGL_FORALL_INEDGES(cv0, ce, cGraph, CompressedPathGraph1B) {
        if(ce == ceBack) {
            continue;
        }
        const BubbleChain& bubbleChain = cGraph[ce];
        if(not bubbleChain.lastBubble().isHaploid()) {
            if(debug) {
                cout << "Not detangling because the last bubble of in-edge " <<
                    bubbleChainStringId(ce) << " is not haploid." << endl;
            }
            return false;
        }
        inEdges.push_back(ce);
    }

    // Gather the out-edges.
    vector<edge_descriptor> outEdges(1, ceBack);
    BGL_FORALL_OUTEDGES(cv1, ce, cGraph, CompressedPathGraph1B) {
        if(ce == ceBack) {
            continue;
        }
        const BubbleChain& bubbleChain = cGraph[ce];
        if(not bubbleChain.firstBubble().isHaploid()) {
            if(debug) {
                cout << "Not detangling because the first bubble of out-edge " <<
                    bubbleChainStringId(ce) << " is not haploid." << endl;
            }
            return false;
        }
        outEdges.push_back(ce);
    }


    if(debug) {

        // Position 0 of the inEdges and outEdges stores the back-edge.

        cout << "In-edges: ";
        for(uint64_t i=1; i<inEdges.size(); i++) {
            const edge_descriptor ce = inEdges[i];
            cout << " " << bubbleChainStringId(ce);
        }
        cout << endl;

        cout << "Out-edges: ";
        for(uint64_t i=1; i<outEdges.size(); i++) {
            const edge_descriptor ce = outEdges[i];
            cout << " " << bubbleChainStringId(ce);
        }
        cout << endl;
    }
    // Compute the tangle matrix.
    vector< vector<uint64_t> > tangleMatrix;
    computeTangleMatrix(inEdges, outEdges, tangleMatrix, false);

    if(debug) {
        cout << "Tangle matrix:" << endl;
        for(uint64_t i0=0; i0<inEdges.size(); i0++) {
            const edge_descriptor ce0 = inEdges[i0];
            for(uint64_t i1=0; i1<outEdges.size(); i1++) {
                const edge_descriptor ce1 = outEdges[i1];
                cout <<
                    bubbleChainStringId(ce0) << " " <<
                    bubbleChainStringId(ce1) << " " <<
                    tangleMatrix[i0][i1];
                cout << endl;
            }
        }
    }

    return false;
}



void CompressedPathGraph1B::phaseBubbleChains(
    bool debug,
    uint64_t n, // Maximum number of Chain MarkerGraphEdgeIds to use when computing tangle matrices.
    uint64_t lowThreshold,
    uint64_t highThreshold,
    uint64_t longBubbleThreshold)
{
    CompressedPathGraph1B& cGraph = *this;

    vector<edge_descriptor> allEdges;
    BGL_FORALL_EDGES(ce, cGraph, CompressedPathGraph1B) {
        allEdges.push_back(ce);
    }

    for(const edge_descriptor ce: allEdges) {
        phaseBubbleChain(ce, n, lowThreshold, highThreshold, longBubbleThreshold, debug);
    }
}



void CompressedPathGraph1B::phaseBubbleChain(
    edge_descriptor ce,
    uint64_t n, // Maximum number of Chain MarkerGraphEdgeIds to use when computing tangle matrices.
    uint64_t lowThreshold,
    uint64_t highThreshold,
    uint64_t longBubbleThreshold,
    bool debug)
{
    CompressedPathGraph1B& cGraph = *this;
    BubbleChain& bubbleChain = cGraph[ce];

    // debug = debug and (cGraph[ce].id == 500048);

    if(debug) {
        cout << "Phasing " << bubbleChainStringId(ce) << endl;
    }

    const bool detailedDebug = debug; // (cGraph[ce].id == 49557);

    // Table to contain the Phasing graph vertex corresponding to each diploid bubble.
    // Indexed by the bubble position in the bubble chains, and contains
    // PhasingGraph::null_vertex() for non-diploid bubbles.
    vector<PhasingGraph::vertex_descriptor> vertexTable(bubbleChain.size(), PhasingGraph::null_vertex());

    // Create the PhasingGraph and its vertices, one for
    // each diploid bubble in the bubble chain.
    PhasingGraph phasingGraph;
    for(uint64_t i=0; i<bubbleChain.size(); i++) {
        if(bubbleChain[i].isDiploid()) {
            vertexTable[i] = add_vertex({i, 0}, phasingGraph);
        }
    }

    // Write a histogram of the bubbles in this bubble chain by ploidy.
    if(debug) {
        cout << "Phasing a bubble chain with " << bubbleChain.size() << " bubbles." << endl;
        vector<uint64_t> histogram;
        for(const Bubble& bubble: bubbleChain) {
            const uint64_t ploidy = bubble.size();
            if(histogram.size() <= ploidy) {
                histogram.resize(ploidy + 1);
            }
            ++histogram[ploidy];
        }
        for(uint64_t ploidy=1; ploidy<histogram.size(); ploidy++) {
            const uint64_t frequency = histogram[ploidy];
            if(frequency) {
                cout << frequency << " bubbles of ploidy " << ploidy << endl;
            }
        }
    }



    // Add edges of the phasing graph.
    for(uint64_t i0=0; i0<bubbleChain.size()-1; i0++) {
        const PhasingGraph::vertex_descriptor pv0 = vertexTable[i0];
        if(pv0 == PhasingGraph::null_vertex()) {
            continue;
        }

        // Gather the next-to-last two marker graph edges for the two chains
        // of this bubble.
        const Bubble& bubble0 = bubbleChain[i0];
        SHASTA_ASSERT(bubble0.size() == 2);
        const Chain& chain00 = bubble0[0];
        const Chain& chain01 = bubble0[1];
        const array<MarkerGraphEdgeId, 2> edges0 =
            {chain00[chain00.size()-2], chain01[chain01.size()-2]};

        for(uint64_t i1=i0+1; i1<bubbleChain.size(); i1++) {
            const PhasingGraph::vertex_descriptor pv1 = vertexTable[i1];
            if(pv1 == PhasingGraph::null_vertex()) {
                continue;
            }

            // Gather the next-to-last two marker graph edges for the two chains
            // of this bubble.
            const Bubble& bubble1 = bubbleChain[i1];
            SHASTA_ASSERT(bubble1.size() == 2);
            const Chain& chain10 = bubble1[0];
            const Chain& chain11 = bubble1[1];
            const array<MarkerGraphEdgeId, 2> edges1 =
                {chain10[1], chain11[1]};

            // Compute the tangle matrix.
            TangleMatrix tangleMatrix;
            if(n == 1) {
                for(uint64_t j0=0; j0<2; j0++) {
                    for(uint64_t j1=0; j1<2; j1++) {
                        MarkerGraphEdgePairInfo info;
                        SHASTA_ASSERT(assembler.analyzeMarkerGraphEdgePair(
                            edges0[j0], edges1[j1], info));
                        tangleMatrix[j0][j1] = info.common;
                    }
                }
            } else {
                computeTangleMatrix(
                    {&chain00, &chain01},
                    {&chain10, &chain11},
                    n, tangleMatrix);
            }

            // Analyze the tangle matrix.
            int64_t phase;
            uint64_t minConcordant;
            uint64_t maxDiscordant;
            uint64_t total;
            tangleMatrix.analyze(
                lowThreshold,
                highThreshold,
                phase,
                minConcordant,
                maxDiscordant,
                total);

            if(detailedDebug) {
                cout << "Tangle matrix " << i0 << " " << i1 << ": " <<
                    tangleMatrix[0][0] << " " <<
                    tangleMatrix[0][1] << " " <<
                    tangleMatrix[1][0] << " " <<
                    tangleMatrix[1][1] << endl;
                cout << "minConcordant " << minConcordant << endl;
                cout << "maxDiscordant " << maxDiscordant << endl;
            }

            // If not ambiguous, add an edge to the PhasingGraph.
            if(phase != 0) {
                boost::add_edge(pv0, pv1, {phase, minConcordant, maxDiscordant}, phasingGraph);

                if(detailedDebug) {
                    cout << " Added phasing graph edge " <<
                        phasingGraph[pv0].positionInBubbleChain << " " <<
                        phasingGraph[pv1].positionInBubbleChain << " with minConcordant " <<
                        minConcordant << ", maxDiscordant " << maxDiscordant << endl;
                }
            }

            // If no common reads, stop the loop on i1.
            if(total == 0) {
                break;
            }
        }
    }

    if(debug) {
        const uint64_t vertexCount = num_vertices(phasingGraph);
        const uint64_t edgeCount = num_edges(phasingGraph);
        const double connectivity = 2. * double(edgeCount) / double(vertexCount);
        cout << "The phasing graph has " << vertexCount <<
            " vertices and " << edgeCount << " edges."
            " Average connectivity " << connectivity << endl;
    }

    phasingGraph.phase1(false);



    // Use the PhasingGraph to create a new BubbleChain that will replace the existing one.
    BubbleChain newBubbleChain;
    if(debug) {
        cout << "Creating the new bubble chain for " << bubbleChainStringId(ce) << endl;
    }

    // Loop over the phased components stored in the phased graph.
    for(uint64_t i=0; /* Check later */; i++) {

        // Bubbles in-between phased components, or before the first phased component,
        // or after the last phased component.
        {
            const uint64_t beginPositionInBubbleChain =
                (i == 0) ? 0 : phasingGraph.phasedComponents[i-1]->maxPositionInBubbleChain + 1;
            const uint64_t endPositionInBubbleChain =
                (i == phasingGraph.phasedComponents.size()) ?
                bubbleChain.size() :
                phasingGraph.phasedComponents[i]->minPositionInBubbleChain;


            if(debug) {
                cout << "Adding unphased bubbles at positions [" <<
                    beginPositionInBubbleChain << "," << endPositionInBubbleChain << ")" << endl;
            }

            for(uint64_t i=beginPositionInBubbleChain; i<endPositionInBubbleChain; i++) {
                const Bubble& bubble = bubbleChain[i];

                // This unphased bubble will be copied verbatim to the new chain if it is
                // haploid or if it is long.
                bool copyVerbatim = bubble.isHaploid();
                if(not copyVerbatim) {
                    uint64_t averageOffset;
                    uint64_t minOffset;
                    uint64_t maxOffset;
                    bubbleOffset(bubble, averageOffset, minOffset, maxOffset);
                    copyVerbatim = maxOffset >= longBubbleThreshold;
                }

                if(copyVerbatim) {
                    newBubbleChain.push_back(bubble);
                } else {
                    // Just add a simple haploid bubble with only the source
                    // and target MarkerGraphEdgeIds.
                    Bubble newBubble;
                    newBubble.resize(1);    // Make it haploid
                    Chain& newChain = newBubble.front();    // Its only chain.
                    newChain.push_back(bubble.front().front()); // Source MarkerGraphEdgeId
                    newChain.push_back(bubble.front().back());  // Target MarkerGraphEdgeId
                    newBubbleChain.push_back(newBubble);
                }
            }
        }



        // If we are past the last phased component, we are done.
        if(i == phasingGraph.phasedComponents.size()) {
            break;
        }

        // Add a diploid bubble for the i-th phased component.
        const PhasedComponent& phasedComponent = *phasingGraph.phasedComponents[i];
        const uint64_t minPositionInBubbleChain = phasedComponent.minPositionInBubbleChain;
        const uint64_t maxPositionInBubbleChain = phasedComponent.maxPositionInBubbleChain;
        if(debug) {
            cout << "Adding phased bubbles at positions " <<
                minPositionInBubbleChain << "-" << maxPositionInBubbleChain << endl;
        }
        newBubbleChain.emplace_back();
        Bubble& newBubble = newBubbleChain.back();
        newBubble.resize(2);    // Make it diploid.
        Chain& newChain0 = newBubble[0];    // The first haplotype after phasing.
        Chain& newChain1 = newBubble[1];    // The second haplotype after phasing.

        // Add the source MarkerGraphEdgeId.
        newChain0.push_back(bubbleChain[minPositionInBubbleChain].front().front());
        newChain1.push_back(bubbleChain[minPositionInBubbleChain].front().front());

        // Add the internal MarkerGraphEdgeIds of all phased diploid bubbles in this PhasedComponent.
        for(const auto& p: phasedComponent) {
            const uint64_t positionInBubbleChain = p.first;
            const int64_t phase = p.second;
            SHASTA_ASSERT(phase==1 or phase==-1);
            const Bubble& bubble = bubbleChain[positionInBubbleChain];
            SHASTA_ASSERT(bubble.isDiploid());
            const Chain& chain0 = (phase==1) ? bubble[0] : bubble[1];
            const Chain& chain1 = (phase==1) ? bubble[1] : bubble[0];
            copy(chain0.begin()+1, chain0.end()-1, back_inserter(newChain0));
            copy(chain1.begin()+1, chain1.end()-1, back_inserter(newChain1));
        }

        // Add the target MarkerGraphEdgeId.
        newChain0.push_back(bubbleChain[maxPositionInBubbleChain].front().back());
        newChain1.push_back(bubbleChain[maxPositionInBubbleChain].front().back());
    }

    // Replace the old BubbleChain with the new one, leaving the id of the edge unchanged.
    newBubbleChain.compress();
    bubbleChain = newBubbleChain;
}



// Compute the tangle matrix between two incoming chains
// and two outgoing chains, taking into account up to
// n MarkergraphEdgeIds for each Chain.
void CompressedPathGraph1B::computeTangleMatrix(
    const array<const Chain*, 2> inChains,
    const array<const Chain*, 2> outChains,
    uint64_t n,
    TangleMatrix& tangleMatrix) const
{
    // Gather the OrientedReadIds near the end of the inChains.
    array<vector<OrientedReadId>, 2> allOrientedReadIdsIn;
    for(uint64_t i=0; i<2; i++) {
        gatherOrientedReadIdsAtEnd(*inChains[i], n, allOrientedReadIdsIn[i]);

    }

    // Gather the OrientedReadIds near the beginning of the outChains.
    array<vector<OrientedReadId>, 2> allOrientedReadIdsOut;
    for(uint64_t i=0; i<2; i++) {
        gatherOrientedReadIdsAtBeginning(*outChains[i], n, allOrientedReadIdsOut[i]);
    }

    // Discard OrientedReadIds that appear in both inChains.
    array<vector<OrientedReadId>, 2> orientedReadIdsIn;
    for(uint64_t i=0; i<2; i++) {
        std::set_difference(
            allOrientedReadIdsIn[i]  .begin(), allOrientedReadIdsIn[i]  .end(),
            allOrientedReadIdsIn[1-i].begin(), allOrientedReadIdsIn[1-i].end(),
            back_inserter(orientedReadIdsIn[i]));
    }

    // Discard OrientedReadIds that appear in both outChains.
    array<vector<OrientedReadId>, 2> orientedReadIdsOut;
    for(uint64_t i=0; i<2; i++) {
        std::set_difference(
            allOrientedReadIdsOut[i]  .begin(), allOrientedReadIdsOut[i]  .end(),
            allOrientedReadIdsOut[1-i].begin(), allOrientedReadIdsOut[1-i].end(),
            back_inserter(orientedReadIdsOut[i]));
    }

    // Now we can compute the tangle matrix.
    vector<OrientedReadId> commonOrientedReads;
    for(uint64_t i0=0; i0<2; i0++) {
        for(uint64_t i1=0; i1<2; i1++) {
            commonOrientedReads.clear();
            set_intersection(
                orientedReadIdsIn[i0] .begin(), orientedReadIdsIn[i0] .end(),
                orientedReadIdsOut[i1].begin(), orientedReadIdsOut[i1].end(),
                back_inserter(commonOrientedReads));
            tangleMatrix[i0][i1] = commonOrientedReads.size();
        }
    }
}



// Gather OrientedReadIds from up to n MarkergraphEdgeIds
// near the end of a chain.
void CompressedPathGraph1B::gatherOrientedReadIdsAtEnd(
    const Chain& chain,
    uint64_t n,
    vector<OrientedReadId>& orientedReadIds) const
{

    const uint64_t last = chain.size() - 2;                     // Exclude last MarkergraphEdgeId.
    const uint64_t first = (last > (n-1)) ? last + 1 - n : 0;   // Use up to n.

    SHASTA_ASSERT(first < chain.size());
    SHASTA_ASSERT(last < chain.size());

    orientedReadIds.clear();
    for(uint64_t i=first; i<=last; i++) {
        const MarkerGraphEdgeId markerGraphEdgeId = chain[i];
        const auto& markerIntervals =
            assembler.markerGraph.edgeMarkerIntervals[markerGraphEdgeId];
        for(const MarkerInterval& markerInterval: markerIntervals) {
            orientedReadIds.push_back(markerInterval.orientedReadId);
        }
    }
    deduplicate(orientedReadIds);
}



// Gather OrientedReadIds from up to n MarkergraphEdgeIds
// near the beginning of a chain.
void CompressedPathGraph1B::gatherOrientedReadIdsAtBeginning(
    const Chain& chain,
    uint64_t n,
    vector<OrientedReadId>& orientedReadIds) const
{

    const uint64_t first = 1;   // / Exclude first MarkergraphEdgeId.
    const uint64_t last = (chain.size() > (n+1)) ? n : chain.size() - 1;

    SHASTA_ASSERT(first < chain.size());
    SHASTA_ASSERT(last < chain.size());

    orientedReadIds.clear();
    for(uint64_t i=first; i<=last; i++) {
        const MarkerGraphEdgeId markerGraphEdgeId = chain[i];
        const auto& markerIntervals =
            assembler.markerGraph.edgeMarkerIntervals[markerGraphEdgeId];
        for(const MarkerInterval& markerInterval: markerIntervals) {
            orientedReadIds.push_back(markerInterval.orientedReadId);
        }
    }
    deduplicate(orientedReadIds);
}



// To phase the PhasingGraph, we create an optimal spanning tree
// using edges in order of decreasing "significance".
void CompressedPathGraph1B::PhasingGraph::phase(bool debug)
{
    PhasingGraph& phasingGraph = *this;

    // Gather edges by maxDiscordant and minConcordant.
    // edgeTable[maxDiscordant][minConcordant] contains the
    // edges with those values of maxDiscordant and minConcordant.
    // This allows the code later ot process edges in order
    // of increasing maxDiscordant and decreasing minConcordant.
    vector< vector< vector<edge_descriptor> > > edgeTable;
    BGL_FORALL_EDGES(pe, phasingGraph, PhasingGraph) {
        const PhasingGraphEdge& edge = phasingGraph[pe];
        const uint64_t maxDiscordant = edge.maxDiscordant;
        const uint64_t minConcordant = edge.minConcordant;
        if(edgeTable.size() <= maxDiscordant) {
            edgeTable.resize(maxDiscordant + 1);
        }
        vector< vector<edge_descriptor> >& v = edgeTable[maxDiscordant];
        if(v.size() <= minConcordant) {
            v.resize(minConcordant + 1);
        }
        v[minConcordant].push_back(pe);
    }

    // Map vertices to integers.
    std::map<vertex_descriptor, uint64_t> vertexIndexMap;
    uint64_t vertexIndex = 0;
    BGL_FORALL_VERTICES(pv, phasingGraph, PhasingGraph) {
        vertexIndexMap.insert({pv, vertexIndex++});
    }
    const uint64_t vertexCount = vertexIndexMap.size();



    // Compute optimal spanning tree and connected components.
    vector<uint64_t> rank(vertexCount);
    vector<uint64_t> parent(vertexCount);
    boost::disjoint_sets<uint64_t*, uint64_t*> disjointSets(&rank[0], &parent[0]);
    for(uint64_t i=0; i<vertexCount; i++) {
        disjointSets.make_set(i);
    }
    uint64_t spanningTreeEdgeCount = 0;
    for(uint64_t maxDiscordant=0; maxDiscordant<edgeTable.size(); maxDiscordant++) {
        const vector< vector<edge_descriptor> >& v = edgeTable[maxDiscordant];
        for(int64_t minConcordant=v.size()-1; minConcordant>=0; minConcordant--) {
            const vector<edge_descriptor>& vv = v[minConcordant];
            if(false) {
                cout << "Processing " << vv.size() << " phasing graph edges with maxDiscordant=" <<
                    maxDiscordant << ", minConcordant=" << minConcordant << endl;
            }
            for(const edge_descriptor e: vv) {
                PhasingGraphEdge& edge = phasingGraph[e];
                const vertex_descriptor pv0 = source(e, phasingGraph);
                const vertex_descriptor pv1 = target(e, phasingGraph);
                const uint64_t vertexIndex0 = vertexIndexMap[pv0];
                const uint64_t vertexIndex1 = vertexIndexMap[pv1];
                const uint64_t componentId0 = disjointSets.find_set(vertexIndex0);
                const uint64_t componentId1 = disjointSets.find_set(vertexIndex1);
                if(componentId0 != componentId1) {
                    disjointSets.union_set(vertexIndex0, vertexIndex1);
                    edge.isSpanningTreeEdge = true;
                    ++spanningTreeEdgeCount;
                }
            }
            if(false) {
                cout << "Found " << spanningTreeEdgeCount << " spanning tree edges so far." << endl;
            }
        }
    }

    // Gather the vertices in each connected component.
    vector< vector<vertex_descriptor> > components(vertexCount);
    BGL_FORALL_VERTICES(pv, phasingGraph, PhasingGraph) {
        const uint64_t componentId = disjointSets.find_set(vertexIndexMap[pv]);
        components[componentId].push_back(pv);
    }

    // Write a histogram of component sizes.
    if(debug) {
        vector<uint64_t> histogram;
        for(const vector<vertex_descriptor>& component: components) {
            const uint64_t componentSize = component.size();
            if(histogram.size() <= componentSize) {
                histogram.resize(componentSize + 1, 0);
            }
            ++histogram[componentSize];
        }

        cout << "Histogram of component sizes:" << endl;
        cout << "Size,Frequency,Vertices" << endl;
        for(uint64_t componentSize=1; componentSize<histogram.size(); componentSize++) {
            const uint64_t frequency = histogram[componentSize];
            if(frequency) {
                cout << componentSize << "," << frequency << ","  << componentSize*frequency << endl;
            }
        }
    }

    // Gather the non-trivial component and sort them by decreasing size.
    vector< pair<uint64_t, uint64_t> > componentTable; // (componentId, componentSize)
    for(uint64_t componentId=0; componentId<vertexCount; componentId++) {
        const vector<vertex_descriptor>& component = components[componentId];
        if(component.size() > 1) {
            componentTable.push_back({componentId, component.size()});
        }
    }
    sort(componentTable.begin(), componentTable.end(),
        OrderPairsBySecondOnlyGreater<uint64_t, uint64_t>());



    // Process the non-trivial components in order of decreasing size.
    phasedComponents.clear();
    for(const  pair<uint64_t, uint64_t>& p: componentTable) {
        const uint64_t componentId = p.first;
        const vector<vertex_descriptor>& component = components[componentId];
        SHASTA_ASSERT(component.size() == p.second);
        if(debug) {
            cout << "Processing a phasing component with " << component.size() <<
                " vertices." << endl;
        }

        // Use a BFS on the spanning tree to phase the vertices in this component.
        // Use the spanning tree to phase vertices in the largest component.
        // It does not matter which vertex we start from.
        const vertex_descriptor vFirst = component.front();
        phasingGraph[vFirst].phase = +1;
        std::queue<vertex_descriptor> q;
        q.push(vFirst);
        while(not q.empty()) {
            const vertex_descriptor v0 = q.front();
            q.pop();
            BGL_FORALL_OUTEDGES(v0, e, phasingGraph, PhasingGraph) {
                PhasingGraphEdge& edge = phasingGraph[e];
                if(not edge.isSpanningTreeEdge) {
                    continue;
                }
                const PhasingGraphVertex& vertex0 = phasingGraph[v0];
                const vertex_descriptor v1 = target(e, phasingGraph);
                PhasingGraphVertex& vertex1 = phasingGraph[v1];
                if(vertex1.phase == 0) {
                    vertex1.phase = vertex0.phase;
                    if(edge.phase == -1) {
                        vertex1.phase = - vertex1.phase;
                    }
                    q.push(v1);
                }
            }
        }

        // Count inconsistent edges in this component.
        if(debug) {
            uint64_t inconsistentCount = 0;
            uint64_t totalCount = 0;
            for(const vertex_descriptor v: component) {
                BGL_FORALL_OUTEDGES(v, e, phasingGraph, PhasingGraph) {
                    totalCount++;
                    if(not isConsistent(e)) {
                        ++inconsistentCount;
                    }
                }
            }
            // This counts edges twice.
            inconsistentCount /= 2;
            totalCount /= 2;
            cout << inconsistentCount << " inconsistent edges in this component out of " <<
                totalCount << " total." << endl;
        }


        // Create the PhasedComponent corresponding to this component.
        // Don't include any vertices that overlap previous PhasedComponent.
        shared_ptr<PhasedComponent> phasedComponentPointer = make_shared<PhasedComponent>();
        PhasedComponent& phasedComponent = *phasedComponentPointer;
        for(const vertex_descriptor pv: component) {
            const PhasingGraphVertex& vertex = phasingGraph[pv];
            const uint64_t positionInBubbleChain = vertex.positionInBubbleChain;
            bool overlapsPrevious = false;
            for(const auto& phasedComponent: phasedComponents) {
                if(
                    positionInBubbleChain >= phasedComponent->minPositionInBubbleChain and
                    positionInBubbleChain <= phasedComponent->maxPositionInBubbleChain) {
                    overlapsPrevious = true;
                    break;
                }
            }
            if(not overlapsPrevious) {
                phasedComponent.push_back({vertex.positionInBubbleChain, vertex.phase});
            }
        }
        if(phasedComponent.size() < 2) {
            if(debug) {
                cout << "This component will be discarded due to overlap with previous components." << endl;
            }
            continue;
        }
        phasedComponent.sort();
        if(debug) {
            cout << "Phasing range for this component " << phasedComponent.minPositionInBubbleChain <<
                " " << phasedComponent.maxPositionInBubbleChain << endl;
        }
        phasedComponents.push_back(phasedComponentPointer);
    }

    // Sort the phased components in order of increasing position.
    class SortHelper {
    public:
        bool operator()(
            const shared_ptr<PhasedComponent>& p0,
            const shared_ptr<PhasedComponent>& p1
            ) const
        {
            return p0->minPositionInBubbleChain < p1->minPositionInBubbleChain;
        }
    };
    sort(phasedComponents.begin(), phasedComponents.end(), SortHelper());

    if(debug) {
        cout << "Kept " << phasedComponents.size() << " phased components:" << endl;
        for(const auto& phasedComponent: phasedComponents) {
            cout  << phasedComponent->size() << " diploid bubbles at positions " <<
                phasedComponent->minPositionInBubbleChain << "..." <<
                phasedComponent->maxPositionInBubbleChain << " in bubble chain." << endl;

        }
        phasingGraph.writeGraphviz("PhasingGraph.dot");
    }
}



// To phase the PhasingGraph, we create an optimal spanning tree
// using edges in order of decreasing "significance".
// We do this iteratively. At each iteration we process the largest
// connected component of the surviving PhasingGraph.
void CompressedPathGraph1B::PhasingGraph::phase1(bool debug)
{
    PhasingGraph& phasingGraph = *this;
    phasedComponents.clear();

    if(debug) {
        cout << "Beginning phasing for a PhasingGraph with " << num_vertices(phasingGraph) <<
            " vertices." << endl;
    }

    // Main iteration loop.
    while(true) {

        // Clear the isSpanningTreeEdge flag of all edges.
        BGL_FORALL_EDGES(pe, phasingGraph, PhasingGraph) {
            phasingGraph[pe].isSpanningTreeEdge = false;
        }

        // Gather edges by maxDiscordant and minConcordant.
        // edgeTable[maxDiscordant][minConcordant] contains the
        // edges with those values of maxDiscordant and minConcordant.
        // This allows the code later to process edges in order
        // of increasing maxDiscordant and decreasing minConcordant.
        vector< vector< vector<edge_descriptor> > > edgeTable;
        BGL_FORALL_EDGES(pe, phasingGraph, PhasingGraph) {
            const PhasingGraphEdge& edge = phasingGraph[pe];
            const uint64_t maxDiscordant = edge.maxDiscordant;
            const uint64_t minConcordant = edge.minConcordant;
            if(edgeTable.size() <= maxDiscordant) {
                edgeTable.resize(maxDiscordant + 1);
            }
            vector< vector<edge_descriptor> >& v = edgeTable[maxDiscordant];
            if(v.size() <= minConcordant) {
                v.resize(minConcordant + 1);
            }
            v[minConcordant].push_back(pe);
        }

        // Map vertices to integers.
        // This is needed for the computation of the spanning tree and
        // connected components.
        std::map<vertex_descriptor, uint64_t> vertexIndexMap;
        uint64_t vertexIndex = 0;
        BGL_FORALL_VERTICES(pv, phasingGraph, PhasingGraph) {
            vertexIndexMap.insert({pv, vertexIndex++});
        }
        const uint64_t vertexCount = vertexIndexMap.size();

        if(debug) {
            cout << "Beginning a new phasing iteration. The phasing graph has " <<
                vertexCount << " vertices left." << endl;
        }



        // Compute optimal spanning tree and connected components.
        vector<uint64_t> rank(vertexCount);
        vector<uint64_t> parent(vertexCount);
        boost::disjoint_sets<uint64_t*, uint64_t*> disjointSets(&rank[0], &parent[0]);
        for(uint64_t i=0; i<vertexCount; i++) {
            disjointSets.make_set(i);
        }
        uint64_t spanningTreeEdgeCount = 0;
        // Process edges in order of decreasing significance.
        for(uint64_t maxDiscordant=0; maxDiscordant<edgeTable.size(); maxDiscordant++) {
            const vector< vector<edge_descriptor> >& v = edgeTable[maxDiscordant];
            for(int64_t minConcordant=v.size()-1; minConcordant>=0; minConcordant--) {
                const vector<edge_descriptor>& vv = v[minConcordant];
                if(false) {
                    cout << "Processing " << vv.size() << " phasing graph edges with maxDiscordant=" <<
                        maxDiscordant << ", minConcordant=" << minConcordant << endl;
                }
                for(const edge_descriptor e: vv) {
                    PhasingGraphEdge& edge = phasingGraph[e];
                    const vertex_descriptor pv0 = source(e, phasingGraph);
                    const vertex_descriptor pv1 = target(e, phasingGraph);
                    const uint64_t vertexIndex0 = vertexIndexMap[pv0];
                    const uint64_t vertexIndex1 = vertexIndexMap[pv1];
                    const uint64_t componentId0 = disjointSets.find_set(vertexIndex0);
                    const uint64_t componentId1 = disjointSets.find_set(vertexIndex1);
                    if(componentId0 != componentId1) {
                        disjointSets.union_set(vertexIndex0, vertexIndex1);
                        edge.isSpanningTreeEdge = true;
                        ++spanningTreeEdgeCount;
                    }
                }
                if(false) {
                    cout << "Found " << spanningTreeEdgeCount << " spanning tree edges so far." << endl;
                }
            }
        }

        // Gather the vertices in each connected component.
        vector< vector<vertex_descriptor> > components(vertexCount);
        BGL_FORALL_VERTICES(pv, phasingGraph, PhasingGraph) {
            const uint64_t componentId = disjointSets.find_set(vertexIndexMap[pv]);
            components[componentId].push_back(pv);
        }

        // Find the largest connected component.
        uint64_t largestComponentId = invalid<uint64_t>;
        uint64_t largestComponentSize = 0;
        for(uint64_t componentId=0; componentId<vertexCount; componentId++) {
            const uint64_t componentSize = components[componentId].size();
            if(componentSize > largestComponentSize) {
                largestComponentSize = componentSize;
                largestComponentId = componentId;
            }
        }

        // If the largest component has less than two vertices, we are done.
        if(largestComponentSize < 2) {
            if(debug) {
                cout << "Phasing terminates because  only trivial connected components were found." << endl;
            }
            break;
        }

        // Access the largest connected component, which we will be working on
        // for the rest of this iteration.
        const vector<vertex_descriptor>& component = components[largestComponentId];
        SHASTA_ASSERT(component.size() == largestComponentSize);
        if(debug) {
            cout << "The largest component of the current PhasingGraph has " <<
                largestComponentSize << " vertices." << endl;
        }

        // Use a BFS on the spanning tree to phase the vertices in this component.
        // It does not matter which vertex we start from.
        const vertex_descriptor vFirst = component.front();
        phasingGraph[vFirst].phase = +1;
        std::queue<vertex_descriptor> q;
        q.push(vFirst);
        while(not q.empty()) {
            const vertex_descriptor v0 = q.front();
            q.pop();
            BGL_FORALL_OUTEDGES(v0, e, phasingGraph, PhasingGraph) {
                PhasingGraphEdge& edge = phasingGraph[e];
                if(not edge.isSpanningTreeEdge) {
                    continue;
                }
                const PhasingGraphVertex& vertex0 = phasingGraph[v0];
                const vertex_descriptor v1 = target(e, phasingGraph);
                PhasingGraphVertex& vertex1 = phasingGraph[v1];
                if(vertex1.phase == 0) {
                    vertex1.phase = vertex0.phase;
                    if(edge.phase == -1) {
                        vertex1.phase = - vertex1.phase;
                    }
                    q.push(v1);
                }
            }
        }

        // Count inconsistent edges in this component.
        if(debug) {
            uint64_t inconsistentCount = 0;
            uint64_t totalCount = 0;
            for(const vertex_descriptor v: component) {
                BGL_FORALL_OUTEDGES(v, e, phasingGraph, PhasingGraph) {
                    totalCount++;
                    if(not isConsistent(e)) {
                        ++inconsistentCount;
                    }
                }
            }
            // This counts edges twice.
            inconsistentCount /= 2;
            totalCount /= 2;
            cout << inconsistentCount << " inconsistent edges in this component out of " <<
                totalCount << " total." << endl;
        }

        // All vertices in this component have been phased.
        // However, when creating the PhasedComponent, we have to make sure that adjacent
        // phased vertices have common reads.
        // To guarantee this, we find a longest path in this component, in order of increasing
        // positionInBubbleChain. Only vertices in this longest path are then included in the
        // PhasedComponent.

        // To find this longest path, we use an algorithm similar to the one in longestPath.cpp,
        // using the topological ordering induced by positionInBubbleChain.

        // Table of the vertices in order of increasing positionInBubbleChain.
        vector< pair<vertex_descriptor, uint64_t> > vertexTable;
        for(const vertex_descriptor v: component) {
            vertexTable.push_back({v, phasingGraph[v].positionInBubbleChain});
        }
        sort(vertexTable.begin(), vertexTable.end(), OrderPairsBySecondOnly<vertex_descriptor, uint64_t>());

        // The length of the longest path ending at each vertex.
        std::map<vertex_descriptor, uint64_t> lengthMap;
        for(const vertex_descriptor v: component) {
            lengthMap.insert(make_pair(v, 0));
        }

        // Process the vertices in order of increasing positionInBubbleChain.
        for(const auto& p: vertexTable) {
            const vertex_descriptor v0 = p.first;
            const uint64_t positionInBubbleChain0 = phasingGraph[v0].positionInBubbleChain;

            uint64_t maximumLength = 0;
            BGL_FORALL_OUTEDGES_T(v0, e, phasingGraph, PhasingGraph) {
                const vertex_descriptor v1 = target(e, phasingGraph);
                const uint64_t positionInBubbleChain1 = phasingGraph[v1].positionInBubbleChain;

                if(positionInBubbleChain1 < positionInBubbleChain0) {
                    maximumLength = max(maximumLength, lengthMap[v1]);
                }
            }
            lengthMap[v0] = maximumLength + 1;
        }

        // Find the vertex with the longest length.
        // This will be the end of the longest path.
        vertex_descriptor v = PhasingGraph::null_vertex();
        uint64_t maximumLength = 0;
        for(const auto& p: lengthMap) {
            if(p.second > maximumLength) {
                v = p.first;
                maximumLength = p.second;
            }
        }

        // Constuct the path, moving backward from here.
        vector<vertex_descriptor> longestPath;
        longestPath.push_back(v);
        while(true) {
            vertex_descriptor vPrevious = PhasingGraph::null_vertex();
            uint64_t maximumLength = 0;
            BGL_FORALL_OUTEDGES(v, e, phasingGraph, PhasingGraph) {
                const vertex_descriptor v0 = target(e, phasingGraph);
                if(phasingGraph[v0].positionInBubbleChain < phasingGraph[v].positionInBubbleChain) {
                    const uint64_t length = lengthMap[v0];
                    if(length > maximumLength) {
                        vPrevious = v0;
                        maximumLength = length;
                    }
                }
            }
            if(vPrevious == PhasingGraph::null_vertex()) {
                break;
            }
            v = vPrevious;
            longestPath.push_back(v);

        }
        std::reverse(longestPath.begin(), longestPath.end());

        if(debug) {
            cout << "The longest path contains " << longestPath.size() << " vertices." << endl;
        }



        // If the longest path is non-trivial, use it to create a new PhasedComponent.
        if(longestPath.size() > 1) {
            if(debug) {
                cout << "Creating a new PhasedComponent." << endl;
            }
            shared_ptr<PhasedComponent> phasedComponentPointer = make_shared<PhasedComponent>();
            phasedComponents.push_back(phasedComponentPointer);
            PhasedComponent& phasedComponent = *phasedComponentPointer;

            for(const vertex_descriptor v: longestPath) {
                const PhasingGraphVertex& vertex = phasingGraph[v];
                phasedComponent.push_back({vertex.positionInBubbleChain, vertex.phase});
            }
            phasedComponent.minPositionInBubbleChain = phasingGraph[longestPath.front()].positionInBubbleChain;
            phasedComponent.maxPositionInBubbleChain = phasingGraph[longestPath.back()].positionInBubbleChain;
            if(debug) {
                cout << "Phasing range for this component " << phasedComponent.minPositionInBubbleChain <<
                    " " << phasedComponent.maxPositionInBubbleChain << endl;
            }

            // Now remove from the PhasingGraph all vertices of this component
            // plus any vertices with a positionInBubbleChain
            // that overlaps this phased component.
            vector<vertex_descriptor> verticesToBeRemoved = component;
            BGL_FORALL_VERTICES(v, phasingGraph, PhasingGraph) {
                const uint64_t positionInBubbleChain = phasingGraph[v].positionInBubbleChain;
                if( positionInBubbleChain >= phasedComponent.minPositionInBubbleChain and
                    positionInBubbleChain <= phasedComponent.maxPositionInBubbleChain) {
                    verticesToBeRemoved.push_back(v);
                }
            }
            deduplicate(verticesToBeRemoved);
            for(const vertex_descriptor v: verticesToBeRemoved) {
                clear_vertex(v, phasingGraph);
                remove_vertex(v, phasingGraph);
            }
        } else {

            // Now remove from the PhasingGraph all vertices of this component.
            for(const vertex_descriptor v: component) {
                clear_vertex(v, phasingGraph);
                remove_vertex(v, phasingGraph);
            }
        }
    }



    // Sort the phased components in order of increasing position.
    class SortHelper {
    public:
        bool operator()(
            const shared_ptr<PhasedComponent>& p0,
            const shared_ptr<PhasedComponent>& p1
            ) const
        {
            return p0->minPositionInBubbleChain < p1->minPositionInBubbleChain;
        }
    };
    sort(phasedComponents.begin(), phasedComponents.end(), SortHelper());

    if(debug) {
        cout << phasedComponents.size() << " phased components:" << endl;
        for(const auto& phasedComponent: phasedComponents) {
            cout  << phasedComponent->size() << " diploid bubbles at positions " <<
                phasedComponent->minPositionInBubbleChain << "..." <<
                phasedComponent->maxPositionInBubbleChain << " in bubble chain." << endl;

        }
        // phasingGraph.writeGraphviz("PhasingGraph.dot");
    }
}



void CompressedPathGraph1B::PhasedComponent::sort()
{
    SHASTA_ASSERT(size() > 1);
    std::sort(begin(), end(), OrderPairsByFirstOnly<uint64_t, int64_t>());
    minPositionInBubbleChain = front().first;
    maxPositionInBubbleChain = back().first;
}



bool CompressedPathGraph1B::PhasingGraph::isConsistent(edge_descriptor e) const
{
    const PhasingGraph& phasingGraph = *this;
    const vertex_descriptor v0 = source(e, phasingGraph);
    const vertex_descriptor v1 = target(e, phasingGraph);
    const int64_t phase0 = phasingGraph[v0].phase;
    const int64_t phase1 = phasingGraph[v1].phase;
    const int64_t phase = phasingGraph[e].phase;

    SHASTA_ASSERT(phase0==+1 or phase0==-1);
    SHASTA_ASSERT(phase1==+1 or phase1==-1);
    SHASTA_ASSERT(phase==+1 or phase==-1);

    if(phase == +1) {
        return phase0 == phase1;
    } else {
        return phase0 != phase1;
    }
}



void CompressedPathGraph1B::PhasingGraph::writeGraphviz(const string& fileName) const
{
    const PhasingGraph& phasingGraph = *this;

    ofstream dot(fileName);
    dot << "graph PhasingGraph {\n";

    BGL_FORALL_EDGES(e, phasingGraph, PhasingGraph) {
        const vertex_descriptor v0 = source(e, phasingGraph);
        const vertex_descriptor v1 = target(e, phasingGraph);
        dot <<
            phasingGraph[v0].positionInBubbleChain << "--" <<
            phasingGraph[v1].positionInBubbleChain;
        if(phasingGraph[e].isSpanningTreeEdge) {
            dot << " [color=green]";
        } else  if(not isConsistent(e)) {
            dot << " [color=red]";
        }
        dot << ";\n";
    }

    dot << "}\n";
}



void CompressedPathGraph1B::TangleMatrix::analyze(
    uint64_t lowThreshold,
    uint64_t highThreshold,
    int64_t& phase,
    uint64_t& minConcordant,
    uint64_t& maxDiscordant,
    uint64_t& total) const
{
    const TangleMatrix& m = *this;

    // Classify matrix elements:
    // 0 = low (<=lowThreshold)
    // 1 = ambiguous (>lowThreshold, <highThreshold)
    // 2 = high (>=highThreshold)
    array< array<uint64_t, 2>, 2> c;
    total = 0;
    for(uint64_t i=0; i<2; i++) {
        for(uint64_t j=0; j<2; j++) {
            const uint64_t matrixElement = m[i][j];
            total += matrixElement;
            uint64_t& classification = c[i][j];
            if(matrixElement <= lowThreshold) {
                classification = 0;
            } else if(matrixElement >= highThreshold) {
                classification = 2;
            } else {
                classification = 1;
            }
        }
    }

    // Check if this tangle matrix is unambiguously in phase.
    if(c[0][0]==2 and c[1][1]==2 and c[0][1]==0 and c[1][0]==0) {
        phase = +1;
        minConcordant = min(m[0][0], m[1][1]);
        maxDiscordant = max(m[0][1], m[1][0]);
    }

    // Check if this tangle matrix is unambiguously out of phase.
    else if(c[0][1]==2 and c[1][0]==2 and c[0][0]==0 and c[1][1]==0) {
        phase = -1;
        minConcordant = min(m[0][1], m[1][0]);
        maxDiscordant = max(m[0][0], m[0][0]);
    }

    // Otherwise, it is ambiguous.
    else {
        phase = 0;
        minConcordant = 0;
        maxDiscordant = 0;
    }
}



// Collapse consecutive haploid bubbles of a BubbleChain.
void BubbleChain::compress()
{
    BubbleChain& bubbleChain = *this;
    BubbleChain newBubbleChain;

    // Find sets of consecutive haploid bubbles.
    for(uint64_t i=0; i<size(); i++) {
        const Bubble& bubble = bubbleChain[i];

        if(bubble.isHaploid()) {

            // This bubble is haploid.
            // If the last bubble of the new bubble is haploid, append it to that.
            // Otherwise apppend it to the last bubble.
            if(not newBubbleChain.empty() and newBubbleChain.back().isHaploid()) {
                const Chain& chain = bubble.front();
                Chain& newChain = newBubbleChain.back().front();
                copy(chain.begin()+1, chain.end(), back_inserter(newChain));
            } else {
                newBubbleChain.push_back(bubble);
            }
        } else {

            // This bubble is not haploid. Just append it to the last bubble.
            newBubbleChain.push_back(bubble);
        }

    }

    // Replace it with the new one.
    bubbleChain = newBubbleChain;
}



void CompressedPathGraph1B::assembleChains(
    uint64_t threadCount0,
    uint64_t threadCount1)
{
    CompressedPathGraph1B& cGraph = *this;

    ofstream csv("ChainsAssemblyDetails.csv");
    cout << timestamp << "Assembling sequence." << endl;

    // ****** THIS SHOULD BE MADE MULTITHREADED USING threadCount0 threads.
    BGL_FORALL_EDGES(ce, cGraph, CompressedPathGraph1B) {
        BubbleChain& bubbleChain = cGraph[ce];
        for(uint64_t positionInBubbleChain=0; positionInBubbleChain<bubbleChain.size();
            positionInBubbleChain++)  {
            Bubble& bubble = bubbleChain[positionInBubbleChain];
            for(uint64_t indexInBubble=0; indexInBubble<bubble.size(); indexInBubble++) {
                Chain& chain = bubble[indexInBubble];
                const string chainName = chainStringId(ce, positionInBubbleChain, indexInBubble);
                assembleChain(chain, threadCount1, chainName, csv);
            }
        }
    }
    cout << timestamp << "Done assembling sequence." << endl;
}



// Assemble sequence for a Chain.
void CompressedPathGraph1B::assembleChain(
    Chain& chain,
    uint64_t threadCount1,
    const string& chainName,
    ostream& csv) const
{

    vector<MarkerGraphEdgePairInfo> infos(chain.size() - 1);
    for(uint64_t i=0; i<infos.size(); i++) {
        const MarkerGraphEdgeId edgeId0 = chain[i];
        const MarkerGraphEdgeId edgeId1 = chain[i+1];
        SHASTA_ASSERT(assembler.analyzeMarkerGraphEdgePair(
            edgeId0, edgeId1, infos[i]));
    }

    AssemblyPath assemblyPath(assembler, chain, infos, threadCount1);
    assemblyPath.getSequence(chain.sequence);
    assemblyPath.writeCsv(csv, chainName);
}



// Make a copy of an edge, truncating it at its end by removing the last MarkerGraphEdgeId.
// Return the target vertex of the newly created edge.
// The last bubble of the bubble chain of the given edge must be haploid.
// If the bubble chain consists of just a single haploid bubble with a chain of length 2,
// no new edge is created, and this simply returns the source vertex of the given edge.
CompressedPathGraph1B::vertex_descriptor
    CompressedPathGraph1B::cloneAndTruncateAtEnd(edge_descriptor ce)
{
    CompressedPathGraph1B& cGraph = *this;
    const CompressedPathGraph1BEdge& edge = cGraph[ce];
    const vertex_descriptor cv0 = source(ce, cGraph);
    const BubbleChain& bubbleChain = cGraph[ce];

    // Sanity checks.
    SHASTA_ASSERT(not bubbleChain.empty());
    SHASTA_ASSERT(bubbleChain.lastBubble().isHaploid());



    // Case where the bubble chain consists of a single bubble, which must be haploid,
    // that is, consist of a single chain.
    if(bubbleChain.size() == 1) {
        const Bubble& bubble = bubbleChain.lastBubble();
        SHASTA_ASSERT(bubble.isHaploid());
        const Chain& chain = bubble.front();
        SHASTA_ASSERT(chain.size() > 1);

        // If the Chain has length 2, we can't truncate it.
        // So we don't create a new edge, and instead just return cv0.
        // Detangling code will connect there, as prescribed by the tangle matrix.
        if(chain.size() == 2) {
            return cv0;
        }

        // Create the new edge, without adding it to the graph for now.
        CompressedPathGraph1BEdge newEdge = edge;
        newEdge.id = nextEdgeId++;
        BubbleChain& newBubbleChain = newEdge;
        SHASTA_ASSERT(newBubbleChain.size() == 1);
        Bubble& newBubble = newBubbleChain.lastBubble();
        SHASTA_ASSERT(newBubble.isHaploid());
        Chain& newChain = newBubble.front();
        SHASTA_ASSERT(chain.size() > 2);
        newChain.pop_back();    // Remove the last MarkerGraphEdgeId.

        // Add it to the graph.
        // It will be dangling at its end.
        // Detangling code will later connect it s prescribed by the tangle matrix.
        const vertex_descriptor cv2 = createVertex(newBubbleChain.lastMarkerGraphEdgeId());
        add_edge(cv0, cv2, newEdge, cGraph);
        return cv2;
    }



    // Case where the bubble chain consists of more than one bubble.
    else {
        const Bubble& lastBubble = bubbleChain.lastBubble();
        SHASTA_ASSERT(lastBubble.isHaploid());
        const Chain& lastChain = lastBubble.front();
        SHASTA_ASSERT(lastChain.size() > 1);

        // Create the new edge, without adding it to the graph for now.
        CompressedPathGraph1BEdge newEdge = edge;
        newEdge.id = nextEdgeId++;
        BubbleChain& newBubbleChain = newEdge;
        SHASTA_ASSERT(newBubbleChain.size() > 1);
        Bubble& newLastBubble = newBubbleChain.lastBubble();
        SHASTA_ASSERT(newLastBubble.isHaploid());
        Chain& newLastChain = newLastBubble.front();

        // If the last chain has length 2, just remove the last bubble from newBubbleChain.
        // Otherwise, remove the last MarkerGraphEdgeId from the lastChain.
        if(newLastChain.size() == 2) {
            newBubbleChain.pop_back();
        } else {
            newLastChain.pop_back();
        }

        // Add it to the graph.
        // It will be dangling at its end.
        // Detangling code will later connect it s prescribed by the tangle matrix.
        const vertex_descriptor cv2 = createVertex(newBubbleChain.lastMarkerGraphEdgeId());
        add_edge(cv0, cv2, newEdge, cGraph);
        return cv2;
    }

}





// Make a copy of an edge, truncating it at its beginning by removing the first MarkerGraphEdgeId.
// Return the source vertex of the newly created edge.
// The first bubble of the bubble chain of the given edge must be haploid.
// If the bubble chain consists of just a single haploid bubble with a chain of length 2,
// no new edge is created, and this simply returns the target vertex of the given edge.
CompressedPathGraph1B::vertex_descriptor
    CompressedPathGraph1B::cloneAndTruncateAtBeginning(edge_descriptor ce)
{
    CompressedPathGraph1B& cGraph = *this;
    const CompressedPathGraph1BEdge& edge = cGraph[ce];
    const vertex_descriptor cv1 = target(ce, cGraph);
    const BubbleChain& bubbleChain = cGraph[ce];

    // Sanity checks.
    SHASTA_ASSERT(not bubbleChain.empty());
    SHASTA_ASSERT(bubbleChain.firstBubble().isHaploid());



    // Case where the bubble chain consists of a single bubble, which must be haploid,
    // that is, consist of a single chain.
    if(bubbleChain.size() == 1) {
        const Bubble& bubble = bubbleChain.firstBubble();
        SHASTA_ASSERT(bubble.isHaploid());
        const Chain& chain = bubble.front();
        SHASTA_ASSERT(chain.size() > 1);

        // If the Chain has length 2, we can't truncate it.
        // So we don't create a new edge, and instead just return cv1.
        // Detangling code will connect there, as prescribed by the tangle matrix.
        if(chain.size() == 2) {
            return cv1;
        }

        // Create the new edge, without adding it to the graph for now.
        CompressedPathGraph1BEdge newEdge = edge;
        newEdge.id = nextEdgeId++;
        BubbleChain& newBubbleChain = newEdge;
        SHASTA_ASSERT(newBubbleChain.size() == 1);
        Bubble& newBubble = newBubbleChain.firstBubble();
        SHASTA_ASSERT(newBubble.isHaploid());
        Chain& newChain = newBubble.front();
        SHASTA_ASSERT(chain.size() > 2);
        newChain.erase(newChain.begin());    // Remove the first MarkerGraphEdgeId.

        // Add it to the graph.
        // It will be dangling at its beginning.
        // Detangling code will later connect it s prescribed by the tangle matrix.
        const vertex_descriptor cv2 = createVertex(newBubbleChain.firstMarkerGraphEdgeId());
        add_edge(cv2, cv1, newEdge, cGraph);
        return cv2;
    }



    // Case where the bubble chain consists of more than one bubble.
    else {
        const Bubble& firstBubble = bubbleChain.firstBubble();
        SHASTA_ASSERT(firstBubble.isHaploid());
        const Chain& firstChain = firstBubble.front();
        SHASTA_ASSERT(firstChain.size() > 1);

        // Create the new edge, without adding it to the graph for now.
        CompressedPathGraph1BEdge newEdge = edge;
        newEdge.id = nextEdgeId++;
        BubbleChain& newBubbleChain = newEdge;
        SHASTA_ASSERT(newBubbleChain.size() > 1);
        Bubble& newFirstBubble = newBubbleChain.firstBubble();
        SHASTA_ASSERT(newFirstBubble.isHaploid());
        Chain& newFirstChain = newFirstBubble.front();

        // If the last chain has length 2, just remove the first bubble from newBubbleChain.
        // Otherwise, remove the first MarkerGraphEdgeId from the lastChain.
        if(newFirstChain.size() == 2) {
            newBubbleChain.erase(newBubbleChain.begin());
        } else {
            newFirstChain.erase(newFirstChain.begin());
        }

        // Add it to the graph.
        // It will be dangling at its end.
        // Detangling code will later connect it s prescribed by the tangle matrix.
        const vertex_descriptor cv2 = createVertex(newBubbleChain.firstMarkerGraphEdgeId());
        add_edge(cv2, cv1, newEdge, cGraph);
        return cv2;
    }

}


// Create a new edge connecting the cv0 and cv1.
// The new edge will consist of a simple BubbleChain with a single
// haploid Bubble with a Chain of length 2.
void CompressedPathGraph1B::connect(vertex_descriptor cv0, vertex_descriptor cv1)
{
    CompressedPathGraph1B& cGraph = *this;

    edge_descriptor ceNew;
    tie(ceNew, ignore) = add_edge(cv0, cv1, cGraph);
    CompressedPathGraph1BEdge& newEdge = cGraph[ceNew];
    newEdge.id = nextEdgeId++;
    BubbleChain& newBubbleChain = newEdge;

    // The new BubbleChain consists of a single Bubble.
    newBubbleChain.resize(1);
    Bubble& bubble = newBubbleChain.front();

    // The new Bubble is haploid, that is, consists of a single Chain.
    bubble.resize(1);

    // The new Bubble consists of just the two MarkerGraphEdgeIds
    // corresponding to cv0 and cv1.
    Chain& chain = bubble.front();
    chain.push_back(cGraph[cv0].edgeId);
    chain.push_back(cGraph[cv1].edgeId);

}



void CompressedPathGraph1B::save(const string& fileName) const
{
    ofstream file(fileName);
    boost::archive::binary_oarchive archive(file);
    archive << *this;
}



void CompressedPathGraph1B::load(const string& fileName)
{
    ifstream file(fileName);
    boost::archive::binary_iarchive archive(file);
    archive >> *this;
}



// Optimize chains before assembly, to remove assembly steps with
// less that minCommon reads.
void CompressedPathGraph1B::optimizeChains(
    bool debug,
    uint64_t minCommon,
    uint64_t k)
{
    CompressedPathGraph1B& cGraph = *this;

    BGL_FORALL_EDGES(ce, cGraph, CompressedPathGraph1B) {
        BubbleChain& bubbleChain = cGraph[ce];

        for(uint64_t positionInBubbleChain=0; positionInBubbleChain<bubbleChain.size(); positionInBubbleChain++) {
            Bubble& bubble = bubbleChain[positionInBubbleChain];
            const uint64_t ploidy = bubble.size();

            for(uint64_t indexInBubble=0; indexInBubble<ploidy; indexInBubble++) {
                Chain& chain = bubble[indexInBubble];
                SHASTA_ASSERT(chain.size() >= 2);

                if(debug) {
                    cout << "Optimizing chain " << chainStringId(ce, positionInBubbleChain, indexInBubble) << endl;
                }
                optimizeChain(debug, chain, minCommon, k);
            }
        }
    }

}



// Optimize a chain before assembly, to remove assembly steps with
// less that minCommon reads.
void CompressedPathGraph1B::optimizeChain(
    bool debug,
    Chain& chain,
    uint64_t minCommon,
    uint64_t k)
{
    if(debug) {
        cout << "Optimizing a chain of length " << chain.size() << endl;
    }
    SHASTA_ASSERT(chain.size() >= 2);


    // A directed graph describing the initial and final chains.
    // Each vertex stores a MarkerGraphEdgeId.
    // Each edge stores the number of common oriented reads.
    class ChainGraphVertex {
    public:
        MarkerGraphEdgeId edgeId;
        uint64_t immediateDominator = invalid<uint64_t>;
    };
    class ChainGraphEdge {
    public:
        uint64_t commonCount;
        bool keep = false;
    };
    using ChainGraphBaseClass = boost::adjacency_list<
        boost::listS,
        boost::vecS,
        boost::bidirectionalS,
        ChainGraphVertex,
        ChainGraphEdge>;
    class ChainGraph : public ChainGraphBaseClass {
    public:
    };
    ChainGraph chainGraph;

    class PathInspector {
    public:
        PathInspector(ChainGraph& chainGraph, bool debug) : chainGraph(chainGraph), debug(debug) {}
        ChainGraph& chainGraph;
        bool debug;
        using Path = vector<ChainGraph::edge_descriptor>;
        Path bestPath;
        uint64_t bestPathMinCommonCount = 0;
        void operator()(const Path& path)
        {
            // Compute the minimum number of common oriented reads over edges of this path.
            uint64_t minCommonCount = invalid<uint64_t>;
            for(const ChainGraph::edge_descriptor e: path) {
                minCommonCount = min(minCommonCount, chainGraph[e].commonCount);
            }

            if(debug) {
                cout << "Path with minCommonCount " << minCommonCount << ":";
                for(const ChainGraph::edge_descriptor e: path) {
                    cout << " " << source(e, chainGraph);
                }
                cout << " " << target(path.back(), chainGraph) << "\n";
            }

            // A Path is better if it has a higher minCommonCount or
            // it has the same minCommonCount and is longer.
            //
            if( (minCommonCount >  bestPathMinCommonCount) or
                (minCommonCount == bestPathMinCommonCount and path.size() > bestPath.size())) {
                bestPath = path;
                bestPathMinCommonCount = minCommonCount;
            }
        }

    };

    // Construct the initial ChainGraph.

    // Add the vertices.
    // We are using vecS as the second template argument for ChainGraph,
    // so positions in the chain are also vertex descriptors in the ChainGraph.
    for(const MarkerGraphEdgeId edgeId: chain) {
        add_vertex({edgeId}, chainGraph);
    }

    // Add the edges that correspond to the initial Chain.
    for(uint64_t i1=1; i1<chain.size(); i1++) {
        const uint64_t i0 = i1 - 1;
        const MarkerGraphEdgeId edgeId0 = chainGraph[i0].edgeId;
        const MarkerGraphEdgeId edgeId1 = chainGraph[i1].edgeId;
        MarkerGraphEdgePairInfo info;
        SHASTA_ASSERT(assembler.analyzeMarkerGraphEdgePair(edgeId0, edgeId1, info));
        add_edge(i0, i1, {info.common}, chainGraph);
    }



    // Add edges that skip around any edges with less than minCommon common oriented reads.
    uint64_t totalAddedEdgesCount = 0;
    uint64_t totalRemovedEdgesCount = 0;
    for(uint64_t i1=1; i1<chain.size(); i1++) {
        const uint64_t i0 = i1 - 1;
        ChainGraph::edge_descriptor e;
        bool edgeWasFound = false;
        tie(e, edgeWasFound) = edge(i0, i1, chainGraph);
        SHASTA_ASSERT(edgeWasFound);

        // If this edge has enough common reads, don't do anything.
        if(chainGraph[e].commonCount >= minCommon) {
            continue;
        }

        if(debug) {
            cout << i0 << "->" << i1 << " " << chainGraph[i0].edgeId << "->" << chainGraph[i1].edgeId <<
                " has " << chainGraph[e].commonCount << " common oriented reads, adding edges to skip it." << endl;
        }

        // Loop over pairs of predecessors of v0 and successors of v1.
        uint64_t addedEdgesCount = 0;
        for(uint64_t j0=i0; j0>=i0-k; j0--) {
            for(uint64_t j1=i1; j1<=i1+k; j1++) {
                if(j1 >= chain.size()) {
                    break;
                }
                if(j0==i0 and j1 == i1) {
                    // We already have the edge between v0 and v1.
                    continue;
                }
                MarkerGraphEdgePairInfo info;
                SHASTA_ASSERT(assembler.analyzeMarkerGraphEdgePair(chainGraph[j0].edgeId, chainGraph[j1].edgeId, info));

                // If the number of common reads is better than for e, add this edge.
                if(info.common > chainGraph[e].commonCount) {
                    add_edge(j0, j1, {info.common}, chainGraph);
                    ++addedEdgesCount;
                    cout << " Added " << j0 << "->" << j1 << " " << chainGraph[j0].edgeId << "->" << chainGraph[j1].edgeId <<
                        " with " << info.common << " common oriented reads." << endl;
                }
            }

            if(j0 == 0) {
                break;
            }
        }
        totalAddedEdgesCount += addedEdgesCount;

        // If we added any edges skipping e, we can remove e.
        if(addedEdgesCount > 0) {
            cout << "Removed " << i0 << "->" << i1 << " " << chainGraph[i0].edgeId << "->" << chainGraph[i1].edgeId <<
                " with " << chainGraph[e].commonCount << " common oriented reads." << endl;
            boost::remove_edge(e, chainGraph);
            ++totalRemovedEdgesCount;
        } else {
            if(debug) {
                cout << "Did not find any suitable replacement edges." << endl;
            }
        }
    }


    // If we did not add or remove any edges, leave this Chain alone.
    if(totalAddedEdgesCount == 0) {
        SHASTA_ASSERT(totalRemovedEdgesCount == 0);
        if(debug) {
            cout << "No edges were added or removed, so this Chain will be left unchanged." << endl;
        }
        return;
    }

    if(debug) {
        cout << "This chain will be optimized." << endl;
    }



    // To find the optimized chain, we want to do path enumeration on the ChainGraph,
    // looking for paths that only use edges with large numbers of common oriented reads.
    // Specifically, we use as the new chain the path that maximizes the minimum
    // number of common oriented reads encountered on edges along the path.
    // For efficiency of the path enumeration, we compute a dominator tree
    // for the ChainGraph, with entrance at the beginning of the chain.
    // The unique path on that tree from the entrance to the exit
    // divides the graph in segments, and we can do path enumeration on one segment at a time.
    shasta::lengauer_tarjan_dominator_tree(chainGraph, 0,
        boost::get(&ChainGraphVertex::immediateDominator, chainGraph));

    // The unique path on the dominator tree from the entrance to the exit.
    vector<ChainGraph::vertex_descriptor> dominatorTreePath;
    ChainGraph::vertex_descriptor v = chain.size() - 1;
    while(true) {
        dominatorTreePath.push_back(v);
        if(v == 0) {
            break;
        }
        v = chainGraph[v].immediateDominator;
        if(v == invalid<uint64_t>) {
            cout << "Assertion failure at " << v << endl;
        }
        SHASTA_ASSERT(v != invalid<uint64_t>);
    }
    if(debug) {
        cout << "Dominator tree path length " << dominatorTreePath.size() << endl;
    }
    reverse(dominatorTreePath.begin(), dominatorTreePath.end());

    if(false) {
        cout << "Dominator tree path:" << endl;
        for(uint64_t i=0; i<dominatorTreePath.size(); i++) {
            const uint64_t v = dominatorTreePath[i];
            cout << i << "," << v << "," << chainGraph[v].edgeId << "\n";
        }
    }



    // The dominator tree path divides the graph in segments,
    // and we can do path enumeration on one segment at a time.
    // For each segment we find the best path and mark the edges on that
    // best path as to be kept in the final chain.
    for(uint64_t i1=1; i1<dominatorTreePath.size(); i1++) {
        const uint64_t i0 = i1 - 1;
        const ChainGraph::vertex_descriptor v0 = dominatorTreePath[i0];
        const ChainGraph::vertex_descriptor v1 = dominatorTreePath[i1];

        // Fast handling of the most common case.
        if(v1 == v0+1 and out_degree(v0, chainGraph)==1 and in_degree(v1, chainGraph)==1) {
            ChainGraph::edge_descriptor e;
            bool edgeWasFound = true;
            tie(e, edgeWasFound) = edge(v0, v1, chainGraph);
            if(edgeWasFound) {
                chainGraph[e].keep = true;
                continue;
            }
        }

        // If getting here, we have to do path enumeration.
        if(debug) {
            cout << "Starting path enumeration between " << v0 << " " << v1 << endl;
        }

        // Enumerate paths starting at v0 and ending at v1.
        PathInspector pathInspector(chainGraph, debug);
        enumeratePathsBetween(chainGraph, v0, v1, pathInspector);

        if(debug) {
            if(debug) {
                cout << "The best path has minCommonCount " << pathInspector.bestPathMinCommonCount << ":";
                for(const ChainGraph::edge_descriptor e: pathInspector.bestPath) {
                    cout << " " << source(e, chainGraph);
                }
                cout << " " << target(pathInspector.bestPath.back(), chainGraph) << "\n";
            }
        }

        // Mark as to be kept all edges on the best path.
        for(const ChainGraph::edge_descriptor e: pathInspector.bestPath) {
            chainGraph[e].keep = true;
        }
    }


    // Remove all edges not marked to be kept.
    vector<ChainGraph::edge_descriptor> edgesToBeRemoved;
    BGL_FORALL_EDGES(e, chainGraph, ChainGraph) {
        if(not chainGraph[e].keep) {
            edgesToBeRemoved.push_back(e);
        }
    }
    for(const ChainGraph::edge_descriptor e: edgesToBeRemoved) {
        boost::remove_edge(e, chainGraph);
    }

    // The remaining edges should form a path in the ChainGraph
    // which defines the optimized Chain.
    SHASTA_ASSERT(in_degree(0, chainGraph) == 0);
    SHASTA_ASSERT(out_degree(0, chainGraph) == 1);
    SHASTA_ASSERT(in_degree(chain.size()-1, chainGraph) == 1);
    SHASTA_ASSERT(out_degree(chain.size()-1, chainGraph) == 0);
    for(uint64_t i=1; i<chain.size()-1; i++) {
        const uint64_t inDegree = in_degree(i, chainGraph);
        const uint64_t outDegree = out_degree(i, chainGraph);
        SHASTA_ASSERT(
            (inDegree==1 and outDegree==1) or   // In the new chain.
            (inDegree==0 and outDegree==0)      // Now isolated.
            );
    }

    // Find the path from the entrance to the exit in the update ChainGraph.
    vector<uint64_t> newPath;
    v = 0;
    while(true) {
        newPath.push_back(v);
        if(v == chain.size()-1) {
            break;
        }

        // Move forward.
        SHASTA_ASSERT(out_degree(v, chainGraph) == 1);
        ChainGraph::out_edge_iterator it;
        tie(it, ignore) = out_edges(v, chainGraph);
        const ChainGraph::edge_descriptor e = *it;
        v = target(e, chainGraph);
    }

    // Sanity check that the path is moving forward.
    for(uint64_t i=1; i<newPath.size(); i++) {
        SHASTA_ASSERT(newPath[i] > newPath[i-1]);
    }

    // Construct the new Chain.
    chain.clear();
    chain.sequence.clear();
    for(const uint64_t v: newPath) {
        chain.push_back(chainGraph[v].edgeId);
    }

}
