#ifndef SHASTA_MODE3_JACCARD_GRAPH_HPP
#define SHASTA_MODE3_JACCARD_GRAPH_HPP

/*******************************************************************************

The mode3::JaccardGraph is a directed graph in which each vertex represents
a segment in the mode3::AssemblyGraph.

A directed edge S0->S1 is created if S0 and S1 have:
- A sufficient number of common reads.
- High Jaccard similarity.
- Low unexplained fractions.
(The above quantities defined as computed by
mode3::AssemblyGraph::analyzeSegmentPair).
For the edge to be created, we also require one of the following:
1. S1 is the first primary segment encountered starting from S0,
   and performing a forward path search using the algorithm defined by
   mode3::AssemblyGraph::createAssemblyPath3.
2. S0 is the first primary segment encountered starting from S1,
   and performing a backward path search using the algorithm defined by
   mode3::AssemblyGraph::createAssemblyPath3.

*******************************************************************************/

// Shasta.
#include "mode3-SegmentPairInformation.hpp"

// Boost libraries.
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/iteration_macros.hpp>

// Standard library.
#include "cstdint.hpp"
#include "iosfwd.hpp"
#include <map>
#include "string.hpp"
#include "tuple.hpp"
#include "utility.hpp"
#include "vector.hpp"



namespace shasta {
    namespace mode3 {
        class JaccardGraph;
        class JaccardGraphEdge;
        class JaccardGraphEdgeInfo;
        class JaccardGraphVertex;

        using JaccardGraphBaseClass = boost::adjacency_list<
            boost::listS, boost::listS, boost::bidirectionalS,
            JaccardGraphVertex, JaccardGraphEdge>;

        class ExpandedJaccardGraph;
        class ExpandedJaccardGraphVertex;
        using ExpandedJaccardGraphBaseClass = boost::adjacency_list<
            boost::setS, boost::listS, boost::bidirectionalS,
            ExpandedJaccardGraphVertex>;

    }

    namespace MemoryMapped {
        template<class T> class Vector;
    }
}



class shasta::mode3::JaccardGraphVertex {
public:

    // The assembly graph segment corresponding to this vertex.
    uint64_t segmentId;
};



class shasta::mode3::JaccardGraphEdge {
public:

    // The SegmentPairInformation computed by
    // mode3::AssemblyGraph::analyzeSegmentPair
    // when called for (segmentId0, segmentId1), in this order.
    SegmentPairInformation segmentPairInformation;

    // The segments encountered on the way.
    vector<uint64_t> segmentIds;

    // Flags for the directions in which this edge was found
    // (0=forward, 1=backward).
    array<bool, 2> wasFoundInDirection = {false, false};

    // A strong edge is one that was found in both directions.
    bool isStrong() const
    {
        return wasFoundInDirection[0] and wasFoundInDirection[1];
    }

    JaccardGraphEdge(
        const SegmentPairInformation& segmentPairInformation,
        uint64_t direction,
        const vector<uint64_t>& segmentIds) :
        segmentPairInformation(segmentPairInformation),
        segmentIds(segmentIds)
        {
            wasFoundInDirection[direction] = true;
        }
};



// This is only used during parallel creation of the edges.
class shasta::mode3::JaccardGraphEdgeInfo {
public:
    uint64_t segmentId0;
    uint64_t segmentId1;

    // The direction in which we found this (0=forward, 1=backward).
    uint64_t direction;

    // SegmentPairInformation between segmentId0 and segmentId1.
    SegmentPairInformation segmentPairInformation;

    // The segments encountered on the way.
    vector<uint64_t> segmentIds;
};



class shasta::mode3::JaccardGraph : public JaccardGraphBaseClass {
public:

    // Create a JaccardGraph with the given number of vertices
    // (one for each segment) and no edges.
    JaccardGraph(uint64_t segmentCount);

    // Map segment ids to vertices.
    // If  vertex is removed, the corresponding entry will be null_vertex().
    vector<vertex_descriptor> vertexTable;

    // Remove a vertex, making sure to update the vertexTable.
    void removeVertex(vertex_descriptor v);

    // The edges found by each thread.
    // Only used during edge creation.
    vector< vector<JaccardGraphEdgeInfo> > threadEdges;

    // Use the threadEdges to add edges to the graph.
    void storeEdges();

    // A strong vertex is one that is incident to at least one strong edge.
    bool isStrongVertex(vertex_descriptor) const;

    // Remove all weak vertices.
    void removeWeakVertices();

    // Remove all edges to/from weak vertices.
    void clearWeakVertices();

    // Write the JaccardGraph in graphviz format.
    void writeGraphviz(
        const string& fileName,
        bool includeIsolatedVertices,
        bool writeLabels) const;
    void writeGraphviz(
        ostream&,
        bool includeIsolatedVertices,
        bool writeLabels) const;

    // Write edges in csv format.
    void writeEdgesCsv(const string& fileName) const;
    void writeEdgesCsv(ostream&) const;

    // Compute all connected components of size at least minComponentSize.
    // They are stored in order of decreasing size.
    // The vectors contain segmentIds. Use the vertexMap
    // to convert to file decriptors.
    void computeConnectedComponents(uint64_t minComponentSize);
    vector< vector<uint64_t> > components;

    // Each stored connected component generates a cluster.
    void findClusters(
        MemoryMapped::Vector<uint64_t>& clusterIds);

    // Compute assembly paths.
    void computeAssemblyPaths();
    void computeAssemblyPaths(uint64_t componentId);
    vector< vector<uint64_t> > assemblyPaths;

};



class shasta::mode3::ExpandedJaccardGraphVertex {
public:

    // The assembly graph segment corresponding to this vertex.
    uint64_t segmentId;

    // The total number of JaccardGraph vertices that were merged
    // into this vertex.
    uint64_t totalCount;

    // The number of primary JaccardGraph vertices that were merged
    // into this vertex.
    uint64_t primaryCount;

    // Construction
    ExpandedJaccardGraphVertex() {}
    ExpandedJaccardGraphVertex(
        uint64_t segmentId,
        bool isPrimary) :
        segmentId(segmentId),
        totalCount(1),
        primaryCount(isPrimary ? 1 : 0)
        {}

    double primaryFraction() const
    {
        return double(primaryCount) / double(totalCount);
    }

};



// The ExpandedJaccardGraph is constructed starting with vertices
// of the JaccardGraph, and expanding each of the edges into a linear
// chain of vertices. The graph is then cleaned up by merging equivalent branches.
class shasta::mode3::ExpandedJaccardGraph : public ExpandedJaccardGraphBaseClass {
public:
    ExpandedJaccardGraph(const JaccardGraph&);

    // Write in graphviz format.
    void writeGraphviz(const string& fileName) const;
    void writeGraphviz(ostream&) const;

    // Recursively merge pairs of vertices that have a common parent or child
    // and that refer to the same segmentId.
    void merge();
private:

    // Each Branch represents a pair (vertex_descriptor, direction)
    // where direction can be:
    //   - 0 (forward). In this case the vertex has out_degree>1.
    // or
    //   - 1 (backward). In this case the vertex has in_degree>1.
    using Branch = pair<vertex_descriptor, uint64_t>;

    // Merge v1 and v2 while updating the set of branches.
    void merge(vertex_descriptor v1, vertex_descriptor v2, std::set<Branch>&, bool debug);
};



#endif
