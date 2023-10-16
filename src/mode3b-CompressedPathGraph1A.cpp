// Shasta.
#include "mode3b-PathGraph1.hpp"
#include "Assembler.hpp"
#include "deduplicate.hpp"
#include "findLinearChains.hpp"
using namespace shasta;
using namespace mode3b;

// Boost libraries.
#include <boost/graph/filtered_graph.hpp>
#include <boost/graph/iteration_macros.hpp>

// Standard library.
#include "fstream.hpp"
#include <queue>
#include "tuple.hpp"

// Explicit instantiation.
#include "MultithreadedObject.tpp"
template class MultithreadedObject<CompressedPathGraph1A>;



CompressedPathGraph1A::CompressedPathGraph1A(
    const PathGraph1& graph,
    uint64_t componentId,
    const Assembler& assembler) :
    MultithreadedObject<CompressedPathGraph1A>(*this),
    graph(graph),
    componentId(componentId),
    assembler(assembler)
{
    // EXPOSE WHEN CODE STABILIZES.
    const uint64_t detangleThresholdLow = 2;
    const uint64_t detangleThresholdHigh = 6;

    create();
    writeGfaAndGraphviz("Initial");

    detangle(detangleThresholdLow, detangleThresholdHigh);
    writeGfaAndGraphviz("Final");
}



// Initial creation from the PathGraph1.
void CompressedPathGraph1A::create()
{
    CompressedPathGraph1A& cGraph = *this;

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
    vector< vector<PathGraph1::edge_descriptor> > chains;
    findLinearChains(filteredGraph, 0, chains);

    // Each chain generates an edge.
    // Vertices are added as needed.
    for(const vector<PathGraph1::edge_descriptor>& chain: chains) {
        const PathGraph1::vertex_descriptor v0 = source(chain.front(), graph);
        const PathGraph1::vertex_descriptor v1 = target(chain.back(), graph);
        const vertex_descriptor cv0 = getCompressedVertex(v0);
        const vertex_descriptor cv1 = getCompressedVertex(v1);

        CompressedPathGraph1AEdge edge;
        edge.id = nextEdgeId++;
        for(const PathGraph1::edge_descriptor e: chain) {
            const PathGraph1::vertex_descriptor v = source(e, graph);
            edge.chain.push_back(graph[v].edgeId);
        }
        const PathGraph1::edge_descriptor eLast = chain.back();
        const PathGraph1::vertex_descriptor vLast = target(eLast, graph);
        edge.chain.push_back(graph[vLast].edgeId);

        add_edge(cv0, cv1, edge, cGraph);
    };

}



// Get the vertex_descriptor corresponding to a PathGraph1::vertex_descriptor,
// adding a vertex if necessary.
CompressedPathGraph1A::vertex_descriptor CompressedPathGraph1A::getCompressedVertex(PathGraph1::vertex_descriptor v)
{
    CompressedPathGraph1A& cGraph = *this;

    auto it = vertexMap.find(v);
    if(it == vertexMap.end()) {
        const vertex_descriptor cv = add_vertex({v}, cGraph);
        vertexMap.insert({v, cv});
        return cv;
    } else {
        return it->second;
    }
}



MarkerGraphEdgeId CompressedPathGraph1A::firstMarkerGraphEdgeId(edge_descriptor ce) const
{
    const CompressedPathGraph1A& cGraph = *this;
    const auto& chain = cGraph[ce].chain;

    SHASTA_ASSERT(chain.size() >= 2);
    return chain.front();
}



MarkerGraphEdgeId CompressedPathGraph1A::lastMarkerGraphEdgeId(edge_descriptor ce) const
{
    const CompressedPathGraph1A& cGraph = *this;
    const auto& chain = cGraph[ce].chain;

    SHASTA_ASSERT(chain.size() >= 2);
    return chain.back();
}



MarkerGraphEdgeId CompressedPathGraph1A::secondMarkerGraphEdgeId(edge_descriptor ce) const
{
    const CompressedPathGraph1A& cGraph = *this;
    const auto& chain = cGraph[ce].chain;

    SHASTA_ASSERT(chain.size() >= 2);
    return chain[1];
}



MarkerGraphEdgeId CompressedPathGraph1A::secondToLastMarkerGraphEdgeId(edge_descriptor ce) const
{
    const CompressedPathGraph1A& cGraph = *this;
    const auto& chain = cGraph[ce].chain;

    SHASTA_ASSERT(chain.size() >= 2);
    return chain[chain.size() - 2];
}



void CompressedPathGraph1A::writeGfaAndGraphviz(const string& fileNamePrefix) const
{
    writeGfa(fileNamePrefix);
    writeGraphviz(fileNamePrefix);
}



void CompressedPathGraph1A::writeGfa(const string& fileNamePrefix) const
{
    const CompressedPathGraph1A& cGraph = *this;

    ofstream gfa("CompressedPathGraph1A-" + to_string(componentId) + "-" + fileNamePrefix + ".gfa");

    // Write the header line.
    gfa << "H\tVN:Z:1.0\n";

    // Write a segment for each edge.
    BGL_FORALL_EDGES(ce, cGraph, CompressedPathGraph1A) {

        // Record type.
        gfa << "S\t";

        // Name.
        gfa << edgeStringId(ce) << "\t";

        // Sequence.
        gfa << "*\t";

        // Sequence length in bases.
        gfa << "LN:i:" << totalBaseOffset(ce) << "\n";
    }


    // For each vertex, write links between each pair of incoming/outgoing edges.
    BGL_FORALL_VERTICES(cv, cGraph, CompressedPathGraph1A) {
        BGL_FORALL_INEDGES(cv, ceIn, cGraph, CompressedPathGraph1A) {
            BGL_FORALL_OUTEDGES(cv, ceOut, cGraph, CompressedPathGraph1A) {
                gfa <<
                    "L\t" <<
                    edgeStringId(ceIn) << "\t+\t" <<
                    edgeStringId(ceOut) << "\t+\t*\n";
            }
        }
    }

}



void CompressedPathGraph1A::writeGraphviz(const string& fileNamePrefix) const
{
    const CompressedPathGraph1A& cGraph = *this;

    cout << fileNamePrefix << ": " << num_vertices(cGraph) <<
        " vertices, " << num_edges(cGraph) << " edges." << endl;

    ofstream dot("CompressedPathGraph1A-" + to_string(componentId) + "-" + fileNamePrefix + ".dot");
    dot << "digraph CompressedPathGraph1A_" << componentId << " {\n";


    // Vertices.
    BGL_FORALL_VERTICES(cv, cGraph, CompressedPathGraph1A) {
        const PathGraph1::vertex_descriptor v = cGraph[cv].v;
        dot << graph[v].edgeId << ";\n";
    }



    // Edges.
    BGL_FORALL_EDGES(ce, cGraph, CompressedPathGraph1A) {
        const vertex_descriptor cv0 = source(ce, cGraph);
        const vertex_descriptor cv1 = target(ce, cGraph);

        const PathGraph1::vertex_descriptor v0 = cGraph[cv0].v;
        const PathGraph1::vertex_descriptor v1 = cGraph[cv1].v;

        const auto& chain = cGraph[ce].chain;
        SHASTA_ASSERT(chain.size() >= 2);
        dot << graph[v0].edgeId << "->" << graph[v1].edgeId;

        // Label.
        dot << " [label=\"" << edgeStringId(ce) <<
            "\\n" << totalBaseOffset(ce);
        if(chain.size() == 2) {
            // Nothing else
        } else if(chain.size() == 3) {
            dot << "\\n" <<
            secondMarkerGraphEdgeId(ce);
        } else if(chain.size() == 4) {
            dot << "\\n" <<
                secondMarkerGraphEdgeId(ce) << "\\n" <<
                secondToLastMarkerGraphEdgeId(ce);
        } else {
            dot << "\\n" <<
                secondMarkerGraphEdgeId(ce) << "\\n...\\n" <<
                secondToLastMarkerGraphEdgeId(ce);
        }
        dot << "\"];\n";
    }

    dot << "}\n";
}



string CompressedPathGraph1A::edgeStringId(edge_descriptor ce) const
{
    const CompressedPathGraph1A& cGraph = *this;
    return to_string(componentId) + "-" + to_string(cGraph[ce].id);
}



void CompressedPathGraph1A::detangle(
    uint64_t detangleThresholdLow,
    uint64_t detangleThresholdHigh
    )
{
    while(
        detangleEdges(
            detangleThresholdLow,
            detangleThresholdHigh));

    // For now detangle bubble chains just once at the very end.
    // Eventually we will iterate.
    detangleBubbleChains(
        detangleThresholdLow,
        detangleThresholdHigh);
}



uint64_t CompressedPathGraph1A::detangleEdges(
    uint64_t detangleThresholdLow,
    uint64_t detangleThresholdHigh
    )
{
    const CompressedPathGraph1A& cGraph = *this;

    // The detagling process will create new edges and remove some.
    // Create a set of the edges that exist now.
    // We will erase edges from the set as they get removed,
    // but never add to it.
    std::set<edge_descriptor> initialEdges;
    BGL_FORALL_EDGES(ce, cGraph, CompressedPathGraph1A) {
        initialEdges.insert(ce);
    }
    SHASTA_ASSERT(initialEdges.size() == num_edges(cGraph));

    vector<edge_descriptor> removedEdges;
    uint64_t detangledCount = 0;
    for(auto it=initialEdges.begin(); it!=initialEdges.end(); /* Increment later */) {
        const edge_descriptor ce = *it;
        const bool wasDetangled = detangleEdge(
            ce,
            detangleThresholdLow,
            detangleThresholdHigh,
            removedEdges);
        if(wasDetangled) {
            ++detangledCount;

            // Before removing any edges from the initialEdges set,
            // increment the iterator to point to an edge that will not be removed.
            sort(removedEdges.begin(), removedEdges.end());
            while(it != initialEdges.end()) {
                if(not binary_search(removedEdges.begin(), removedEdges.end(), *it)) {
                    break;
                }
                ++it;
            }

            for(const edge_descriptor ceRemoved: removedEdges) {
                initialEdges.erase(ceRemoved);
            }
        } else {
            ++it;
        }
    }
    cout << detangledCount << " edges were detangled." << endl;
    return detangledCount;
}



bool CompressedPathGraph1A::detangleEdge(
    edge_descriptor ce,
    uint64_t detangleThresholdLow,
    uint64_t detangleThresholdHigh,
    vector<edge_descriptor>& removedEdges)
{
    CompressedPathGraph1A& cGraph = *this;
    removedEdges.clear();

    // The source vertex must have out-degree 1
    // (this is the only edge out of it).
    const vertex_descriptor cv0 = source(ce, cGraph);
    if(out_degree(cv0, cGraph) > 1) {
        return false;
    }

    // The target vertex must have out-degree 1
    // (this is the only edge in from it).
    const vertex_descriptor cv1 = target(ce, cGraph);
    if(in_degree(cv1, cGraph) > 1) {
        return false;
    }

    // Compute the TangleMatrix.
    TangleMatrix tangleMatrix;
    computeTangleMatrix(cv0, cv1, tangleMatrix);

    // Only detangle if in-degree and out-degree are both 2.
    if(tangleMatrix.inDegree() != 2) {
        return false;
    }
    if(tangleMatrix.outDegree() != 2) {
        return false;
    }

#if 0
    cout << "Tangle matrix for " << edgeStringId(ce) << endl;
    for(uint64_t i=0; i<tangleMatrix.inEdges.size(); i++) {
        const edge_descriptor ce0 = tangleMatrix.inEdges[i];

        for(uint64_t j=0; j<tangleMatrix.outEdges.size(); j++) {
            const edge_descriptor ce1 = tangleMatrix.outEdges[j];

            cout << edgeStringId(ce0) << " ";
            cout << edgeStringId(ce1) << " ";
            cout << tangleMatrix.m[i][j] << endl;
        }
    }
#endif


    // We can only detangle if each column and row of the tangle matrix contains
    // exactly one element >= detangleThresholdHigh
    // and all other elements are <= detangleThresholdLow.
    for(uint64_t i=0; i<tangleMatrix.inEdges.size(); i++) {
        uint64_t highCount = 0;
        for(uint64_t j=0; j<tangleMatrix.outEdges.size(); j++) {
            const uint64_t m = tangleMatrix.m[i][j];
            if(m >= detangleThresholdHigh) {
                ++highCount;
            } else if(m > detangleThresholdLow) {
                return false;
            }
        }
        if(highCount != 1) {
            return false;
        }
    }

    for(uint64_t j=0; j<tangleMatrix.outEdges.size(); j++) {
        uint64_t highCount = 0;
        for(uint64_t i=0; i<tangleMatrix.inEdges.size(); i++) {
            const uint64_t m = tangleMatrix.m[i][j];
            if(m >= detangleThresholdHigh) {
                ++highCount;
            } else if(m > detangleThresholdLow) {
                return false;
            }
        }
        if(highCount != 1) {
            return false;
        }
    }
    // cout << "Can detangle." << endl;

    // Create pairs of edges that are going to be merged.
    // This code assumes degree = 2.
    vector< pair<edge_descriptor, edge_descriptor> > mergePairs;
    if(tangleMatrix.m[0][0] >= detangleThresholdHigh) {
        mergePairs.push_back({tangleMatrix.inEdges[0], tangleMatrix.outEdges[0]});
        mergePairs.push_back({tangleMatrix.inEdges[1], tangleMatrix.outEdges[1]});
    } else {
        mergePairs.push_back({tangleMatrix.inEdges[0], tangleMatrix.outEdges[1]});
        mergePairs.push_back({tangleMatrix.inEdges[1], tangleMatrix.outEdges[0]});
    }



    // Create the merged edges.
    for(const auto& p: mergePairs) {
        const edge_descriptor ce0 = p.first;
        const edge_descriptor ce1 = p.second;
        // cout << "Merging edges " << edgeStringId(ce0) << " " << edgeStringId(ce1) << endl;

        const auto& chain0 = cGraph[ce0].chain;
        const auto& chain1 = cGraph[ce1].chain;

        CompressedPathGraph1AEdge newEdge;
        newEdge.id = nextEdgeId++;
        copy(chain0.begin(), chain0.end() - 1, back_inserter(newEdge.chain));
        copy(chain1.begin() + 1, chain1.end(), back_inserter(newEdge.chain));

        add_edge(source(ce0, cGraph), target(ce1, cGraph), newEdge, cGraph);
    }

    // Remove the old edges.
    boost::remove_edge(ce, cGraph);
    removedEdges.push_back(ce);
    for(const edge_descriptor ce: tangleMatrix.inEdges) {
        boost::remove_edge(ce, cGraph);
        removedEdges.push_back(ce);
    }
    for(const edge_descriptor ce: tangleMatrix.outEdges) {
        boost::remove_edge(ce, cGraph);
        removedEdges.push_back(ce);
    }

    // Remove the vertices.
    clear_vertex(cv0, cGraph);
    clear_vertex(cv1, cGraph);
    remove_vertex(cv0, cGraph);
    remove_vertex(cv1, cGraph);

    return true;
}



uint64_t CompressedPathGraph1A::detangleBubbleChains(
    uint64_t detangleThresholdLow,
    uint64_t detangleThresholdHigh
    )
{
    // Find the bubble chains.
    vector<BubbleChain> bubbleChains;
    findBubbleChains(bubbleChains);

    return 0;
}



void CompressedPathGraph1A::findBubbleChains(vector<BubbleChain>& bubbleChains) const
{
    const CompressedPathGraph1A& cGraph = *this;

    // Find the bubbles.
    vector<Bubble> bubbles;
    findBubbles(bubbles);

    // Index the bubbles by their source vertex.
    std::map<vertex_descriptor, Bubble*> bubbleMap;
    for(Bubble& bubble: bubbles) {
        SHASTA_ASSERT(not bubbleMap.contains(bubble.source));
        bubbleMap.insert({bubble.source, &bubble});
    }

    // Find the bubble following/preceding each bubble.
    for(Bubble& bubble: bubbles) {

        // See if there is bubble immediately following.
        auto it = bubbleMap.find(bubble.target);
        if(it != bubbleMap.end()) {
            SHASTA_ASSERT(bubble.nextBubble == 0);
            bubble.nextBubble = it->second;
            SHASTA_ASSERT(bubble.nextBubble->previousBubble == 0);
            bubble.nextBubble->previousBubble = &bubble;
            continue;
        }

        // See if there is a bubble following, with an edge in between.
        if(out_degree(bubble.target, cGraph) != 1) {
            continue;
        }
        edge_descriptor nextEdge;
        BGL_FORALL_OUTEDGES(bubble.target, ce, cGraph, CompressedPathGraph1A) {
            nextEdge = ce;
        }
        const vertex_descriptor cv1 = target(nextEdge, cGraph);
        if(in_degree(cv1, cGraph) != 1) {
            continue;
        }
        it = bubbleMap.find(cv1);
        if(it != bubbleMap.end()) {
            SHASTA_ASSERT(bubble.nextBubble == 0);
            bubble.nextBubble = it->second;
            SHASTA_ASSERT(bubble.nextBubble->previousBubble == 0);
            bubble.nextBubble->previousBubble = &bubble;
        }
    }

    // Sanity check.
    uint64_t noNextCount = 0;
    uint64_t noPreviousCount = 0;
    for(const Bubble& bubble: bubbles) {
        if(bubble.nextBubble == 0) {
            noNextCount++;
        }
        if(bubble.previousBubble == 0) {
            noPreviousCount++;
        }
    }
    SHASTA_ASSERT(noNextCount == noPreviousCount);


    // Now we can construct the BubbleChains.
    bubbleChains.clear();
    for(const Bubble& bubble: bubbles) {

        if(bubble.previousBubble) {
            // A BubbleChain does not begin at this bubble.
            continue;
        }

        bubbleChains.resize(bubbleChains.size() + 1);
        BubbleChain& bubbleChain = bubbleChains.back();
        const Bubble* b = &bubble;
        while(b) {
            bubbleChain.bubbles.push_back(b);
            b = b->nextBubble;
        }
    }
    cout << "Found " << bubbleChains.size() << " bubble chains." << endl;

    // Find the InterBubbles.
    for(BubbleChain& bubbleChain: bubbleChains) {
        for(uint64_t i=1; i<bubbleChain.bubbles.size(); i++) {
            const Bubble* bubble0 = bubbleChain.bubbles[i-1];
            const Bubble* bubble1 = bubbleChain.bubbles[i];

            if(bubble0->target == bubble1->source) {
                // There is no edge in between.
                bubbleChain.interBubbles.push_back({edge_descriptor(), false});
            } else {
                // Find the edge in between.
                edge_descriptor ce;
                bool edgeWasFound;
                tie(ce, edgeWasFound) = boost::edge(bubble0->target, bubble1->source, cGraph);
                SHASTA_ASSERT(edgeWasFound);
                SHASTA_ASSERT(source(ce, cGraph) == bubble0->target);
                SHASTA_ASSERT(target(ce, cGraph) == bubble1->source);
                SHASTA_ASSERT(out_degree(bubble0->target, cGraph) == 1);
                SHASTA_ASSERT(in_degree(bubble1->source, cGraph) == 1);
                bubbleChain.interBubbles.push_back({ce, true});
            }
        }
    }

    if(true) {
        for(const BubbleChain& bubbleChain: bubbleChains) {
            cout << "Bubble chain with " << bubbleChain.bubbles.size() << " bubbles:" << flush;
            for(uint64_t i=0; /* Check later */; i++) {
                const Bubble* bubble = bubbleChain.bubbles[i];
                SHASTA_ASSERT(bubble);
                cout << " ";
                writeBubble(*bubble, cout);
                cout << flush;

                if(i == bubbleChain.bubbles.size() - 1) {
                    break;
                }

                const InterBubble& interbubble = bubbleChain.interBubbles[i];
                if(interbubble.edgeExists) {
                    cout << " " << edgeStringId(interbubble.edge);
                }
            }
            cout << endl;
        }
    }

}




void CompressedPathGraph1A::findBubbles(vector<Bubble>& bubbles) const
{
    const CompressedPathGraph1A& cGraph = *this;
    bubbles.clear();

    vector<vertex_descriptor> targets;
    BGL_FORALL_VERTICES(cv0, cGraph, CompressedPathGraph1A) {

        // For cv0 to be the source of a bubble, all its out-edges must have the same target.
        targets.clear();
        BGL_FORALL_OUTEDGES(cv0, ce, cGraph, CompressedPathGraph1A) {
            targets.push_back(target(ce, cGraph));
        }
        if(targets.size() < 2) {
            continue;
        }
        deduplicate(targets);
        if(targets.size() != 1) {
            // No bubble with cv0 as the source.
            continue;
        }
        const vertex_descriptor cv1 = targets.front();

        // We must also check that all in-edges of cv1 have cv0 as their source.
        bool isBubble = true;
        BGL_FORALL_INEDGES(cv1, ce, cGraph, CompressedPathGraph1A) {
            if(source(ce, cGraph) != cv0) {
                isBubble = false;
                break;
            }
        }
        if(not isBubble) {
            continue;
        }

        // Create a bubble.
        Bubble bubble;
        bubble.source = cv0;
        bubble.target = cv1;
        BGL_FORALL_OUTEDGES(cv0, ce, cGraph, CompressedPathGraph1A) {
            bubble.edges.push_back(ce);
        }
        bubbles.push_back(bubble);

    }

    cout << "Found " << bubbles.size() << " bubbles." << endl;

    // Bubble size histogram.
    {
        vector<uint64_t> histogram;
        for(const Bubble& bubble: bubbles) {
            const uint64_t degree = bubble.degree();
            if(degree >= histogram.size()) {
                histogram.resize(degree+1, 0);
            }
            ++histogram[degree];
        }
        cout << "Bubble degree histogram" << endl;
        for(uint64_t degree=0; degree<histogram.size(); degree++) {
            const uint64_t frequency = histogram[degree];
            if(frequency) {
                cout << degree << " " << frequency << endl;
            }
        }
    }

    // Bubble output.
    if(false) {
        for(const Bubble& bubble: bubbles) {
            const PathGraph1::vertex_descriptor v0 = cGraph[bubble.source].v;
            const PathGraph1::vertex_descriptor v1 = cGraph[bubble.target].v;

            cout << "Bubble between vertices " <<
                graph[v0].edgeId << " " <<
                graph[v1].edgeId << ":";
            for(const edge_descriptor ce: bubble.edges) {
                cout << " " << edgeStringId(ce);
            }
            cout << endl;
        }

    }

}



void CompressedPathGraph1A::computeTangleMatrix(
    vertex_descriptor cv0,
    vertex_descriptor cv1,
    TangleMatrix& tangleMatrix
    ) const
{
    const CompressedPathGraph1A& cGraph = *this;

    tangleMatrix.cv0 = cv0;
    tangleMatrix.cv1 = cv1;

    tangleMatrix.inEdges.clear();
    BGL_FORALL_INEDGES(cv0, ce, cGraph, CompressedPathGraph1A) {
        tangleMatrix.inEdges.push_back(ce);
    }

    tangleMatrix.outEdges.clear();
    BGL_FORALL_OUTEDGES(cv1, ce, cGraph, CompressedPathGraph1A) {
        tangleMatrix.outEdges.push_back(ce);
    }



    // Fill in the tangle matrix entries.
    tangleMatrix.m.resize(tangleMatrix.inEdges.size(), vector<uint64_t>(tangleMatrix.outEdges.size()));
    MarkerGraphEdgePairInfo info;
    for(uint64_t i=0; i<tangleMatrix.inEdges.size(); i++) {
        const edge_descriptor ce0 = tangleMatrix.inEdges[i];
        const MarkerGraphEdgeId markerGraphEdgeId0 = secondToLastMarkerGraphEdgeId(ce0);

        for(uint64_t j=0; j<tangleMatrix.outEdges.size(); j++) {
            const edge_descriptor ce1 = tangleMatrix.outEdges[j];
            const MarkerGraphEdgeId markerGraphEdgeId1 = secondToLastMarkerGraphEdgeId(ce1);

            SHASTA_ASSERT(assembler.analyzeMarkerGraphEdgePair(markerGraphEdgeId0, markerGraphEdgeId1, info));
            tangleMatrix.m[i][j] = info.common;
        }
    }
}



uint64_t CompressedPathGraph1A::TangleMatrix::inDegree() const
{
    return inEdges.size();
}
uint64_t CompressedPathGraph1A::TangleMatrix::outDegree() const
{
    return outEdges.size();
}



uint64_t CompressedPathGraph1A::totalBaseOffset(edge_descriptor ce) const
{
    const CompressedPathGraph1A& cGraph = *this;
    const CompressedPathGraph1AEdge& edge = cGraph[ce];
    const vector<MarkerGraphEdgeId>& chain = edge.chain;

    uint64_t totalOffset = 0;
    MarkerGraphEdgePairInfo info;
    for(uint64_t i=0; i<chain.size(); i++) {
        const MarkerGraphEdgeId edgeId0 = chain[i-1];
        const MarkerGraphEdgeId edgeId1 = chain[i];
        SHASTA_ASSERT(assembler.analyzeMarkerGraphEdgePair(edgeId0, edgeId1, info));

        if(info.common > 0 and info.offsetInBases > 0) {
            totalOffset += info.offsetInBases;
        }
    }
    return totalOffset;
}



void CompressedPathGraph1A::writeBubble(const Bubble& bubble, ostream& s) const
{
    s << "{";
    for(uint64_t i=0; i<bubble.edges.size(); i++) {
        if(i != 0) {
            s << ",";
        }
        s << edgeStringId(bubble.edges[i]);
    }
    s << "}";
}
