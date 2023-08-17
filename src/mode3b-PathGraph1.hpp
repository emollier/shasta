#ifndef SHASTA_MODE3B_PATH_GRAPH1_HPP
#define SHASTA_MODE3B_PATH_GRAPH1_HPP

/*******************************************************************************

In the mode3b::GlobalPathGraph1, each vertex corresponds to a primary edge of
of the marker graph, which is believed to correspond to a single copy
of sequence. It is characterized as follows:
- minPrimaryCoverage <= coverage <= maxPrimaryCoverage
- No duplicate oriented reads on the marker graph edge or its vertices.

A directed edge v0->v1 is generated if:
- A sufficient number of oriented reads visit the marker graph edge
  corresponding to v1 shortly after the marker graph edge corresponding to v0.
- The corrected Jaccard similarity between v0 and v1 is sufficiently high.

*******************************************************************************/

// Shasta.
#include "MarkerGraphEdgePairInfo.hpp"
#include "ReadId.hpp"
#include "shastaTypes.hpp"

// Boost libraries.
#include <boost/graph/adjacency_list.hpp>

// Standard library.
#include "iosfwd.hpp"
#include "memory.hpp"
#include "string.hpp"
#include "utility.hpp"
#include "vector.hpp"

namespace shasta {
    class Assembler;
    namespace mode3b {

        // The global path graph.
        // Each vertex corresponds to a primary marker graph edge.
        class GlobalPathGraph1;
        class GlobalPathGraph1Vertex;
        class GlobalPathGraph1Edge;

        // A subset of the GlobalPathGraph1, for example
        // a single connected component, represented as a Boost graph.
        class PathGraph1Vertex;
        class PathGraph1Edge;
        class PathGraph1;
        using PathGraph1GraphBaseClass = boost::adjacency_list<
            boost::listS,
            boost::vecS,
            boost::bidirectionalS,
            PathGraph1Vertex,
            PathGraph1Edge>;
    }
}



class shasta::mode3b::PathGraph1Vertex {
public:

    // The corresponding GlobalPathGraph1 vertexId.
    uint64_t vertexId;

    // The corresponding marker graph edgeId.
    MarkerGraphEdgeId edgeId;
};



class shasta::mode3b::PathGraph1Edge {
public:
    MarkerGraphEdgePairInfo info;
};



// A subset of the GlobalPathGraph1, for example
// a single connected component, represented as a Boost graph.
class shasta::mode3b::PathGraph1 : public PathGraph1GraphBaseClass {
public:

    std::map<MarkerGraphEdgeId, vertex_descriptor> vertexMap;
    void addVertex(
        uint64_t vertexId,
        MarkerGraphEdgeId);

    void addEdge(
        MarkerGraphEdgeId,
        MarkerGraphEdgeId,
        const MarkerGraphEdgePairInfo&);

    void writeGraphviz(
        const vector<GlobalPathGraph1Vertex>& globalVertices,
        const string& graphName,
        double redJ,
        double greenJ,
        ostream&) const;

    // For each vertex, only keep the best k outgoing and k incoming edges.
    // "Best" as defined by correctedJaccard of the edges.
    void knn(uint64_t k);

    // Create the connected components of this PathGraph1,
    // without changing the PathGraph1 itself.
    vector< shared_ptr<PathGraph1> > createConnectedComponents(uint64_t minComponentSize) const;
};



class shasta::mode3b::GlobalPathGraph1Vertex {
public:
    MarkerGraphEdgeId edgeId;
    uint64_t chainId = invalid<uint64_t>;
    uint64_t positionInChain = invalid<uint64_t>;
    bool isFirstInChain = false;
    bool isLastInChain = false;

    GlobalPathGraph1Vertex(MarkerGraphEdgeId edgeId) : edgeId(edgeId) {}

    // Information on the oriented reads that visit this vertex.
    class JourneyInfoItem {
    public:
        OrientedReadId orientedReadId;
        uint64_t positionInJourney;
    };
    vector<JourneyInfoItem> journeyInfoItems;

    // Compare by edgeId only.
    bool operator<(const GlobalPathGraph1Vertex& that) const
    {
        return edgeId < that.edgeId;
    }
};




class shasta::mode3b::GlobalPathGraph1Edge {
public:
    uint64_t vertexId0;
    uint64_t vertexId1;
    MarkerGraphEdgePairInfo info;
};



class shasta::mode3b::GlobalPathGraph1 {
public:
    GlobalPathGraph1(const Assembler&);
private:
    const Assembler& assembler;

    // Find out if a marker graph edge is a primary edge.
    bool isPrimary(
        MarkerGraphEdgeId,
        uint64_t minPrimaryCoverage,
        uint64_t maxPrimaryCoverage) const;

    // Find out if a marker graph edge is a branch edge.
    // A marker graph edge is a branch edge if:
    // - Its source vertex has more than one outgoing edge with coverage at least minEdgeCoverage.
    // OR
    // - Its target vertex has more than one incoming edge with coverage at least minEdgeCoverage.
    bool isBranchEdge(
        MarkerGraphEdgeId,
        uint64_t minEdgeCoverage) const;

    // Each vertex corresponds to a primary marker graph edge.
    // Store them here.
    // The index in this table is the vertexId.
    // The table is sorted by MarkerGraphEdgeId.
    vector<GlobalPathGraph1Vertex> vertices;
    void createVertices(
        uint64_t minPrimaryCoverage,
        uint64_t maxPrimaryCoverage);

    // Return the vertexId corresponding to a given MarkerGraphEdgeId, or
    // invalid<MarkerGraphEdgeId> if no such a vertex exists.
    uint64_t getVertexId(MarkerGraphEdgeId) const;

    // The "journey" of each oriented read is the sequence of vertices it encounters.
    // It stores pairs (ordinal0, vertexId) for each oriented read, sorted by ordinal0.
    // The vertexId is the index in verticesVector.
    // Indexed by OrientedReadId::getValue.
    // Journeys are used to generate edges by "following the reads".
    vector < vector< pair<uint32_t, uint64_t> > > orientedReadJourneys;
    void computeOrientedReadJourneys();

    vector<GlobalPathGraph1Edge> edges;
    void createEdges0(
        uint64_t maxDistanceInJourney,
        uint64_t minEdgeCoverage,
        double minCorrectedJaccard);
    void createEdges1(
        uint64_t minEdgeCoverage,
        double minCorrectedJaccard);

    // Find children edges of vertexId0.
    // The first element of each pair of the children vector
    // is the vertexId of the child vertex.
    void findChildren(
        uint64_t vertexId0,
        uint64_t minEdgeCoverage,
        double minCorrectedJaccard,
        vector< pair<uint64_t, MarkerGraphEdgePairInfo> >& children);
    void findParents(
        uint64_t vertexId0,
        uint64_t minEdgeCoverage,
        double minCorrectedJaccard,
        vector< pair<uint64_t, MarkerGraphEdgePairInfo> >& parents);

    // The connected components of the GlobalPathGraph1.
    // Stored sorted by decreasing size, as measured by number of vertices.
    vector< shared_ptr<PathGraph1> > components;

    // Create connected components.
    // This only considers edges with corrected Jaccard at least equal to
    // minCorrectedJaccard, and only stores connected components with at
    // least minComponentSize vertices.
    void createComponents(
        double minCorrectedJaccard,
        uint64_t minComponentSize);

    // K-nn of each connected component:
    // for each vertex, keep only the k best outgoing and incoming edges,
    // as measured by correctedJaccard of each edge.
    // This can break contiguity of the connected component.
    void knn(uint64_t k);

    // Transitive reduction of each connected component.
    void transitiveReduction();



    // A chain is a sequence of vertices (not necessarily a path)
    // that can be used to generate an AssemblyPath.
    class Chain {
    public:

        // The vertexIds (indices in the vertices vector) of
        // the vertices of this chain.
        vector<uint64_t> vertexIds;

        // The MarkerGraphEdgePairInfos in between the vertices.
        // infos.size() is always vertexids.size() - 1;
        vector<MarkerGraphEdgePairInfo> infos;

        uint64_t totalOffset() const;
    };



    // For each connected component, use the longest path
    // to create a Chain. Only keep the ones that are sufficiently long.
    // This also stores chain information in the vertices.
    void createChainsFromComponents(
        uint64_t minEstimatedLength,
        vector<Chain>&);

    // Seed chains.
    vector<Chain> seedChains;
    void writeSeedChains() const;
    void writeSeedChainsDetails() const;
    void writeSeedChainsStatistics() const;

    // Connect seed chains by following reads.
    void connectSeedChains0();

    // Connect seed chains by walking the graph.
    class ChainConnector {
    public:
        uint64_t chainId0;
        uint64_t chainId1;
        vector<uint64_t> vertexIds;
        vector<MarkerGraphEdgePairInfo> infos;
        void reverse();
    };
    void connectSeedChains1(
        uint64_t minEdgeCoverage,
        double minCorrectedJaccard,
        vector<ChainConnector>&
        );
    void connectSeedChain1(
        uint64_t chainId,
        uint64_t direction, // 0 = forward, 1 = backward
        uint64_t minEdgeCoverage,
        double minCorrectedJaccard,
        vector<ChainConnector>&,
        ostream& out
        );

    // Use the ChainConnectors to stitch together the seed chains.
    // The replaces the connected components with the connected
    // components of the stitched graph.
    void stitchSeedChains(
        const vector<ChainConnector>&,
        uint64_t minComponentSize);

    // This generates an AssemblyPath for each of the Chains passed in,
    // then assembles the AssemblyPath and writes assembled sequence to fasta.
    void assembleChains(
        const vector<Chain>&,
        ostream& fasta,
        const string& csvPrefix) const;

    // Write the entire PathGraph in graphviz format.
    void writeGraphviz() const;

    // Write each connected component in graphviz format.
    void writeComponentsGraphviz(
        const string& baseName,
        double redJ,
        double greenJ) const;
};



#endif

