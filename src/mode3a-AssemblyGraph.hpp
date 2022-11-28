#ifndef SHASTA_MODE3A_ASSEMBLY_GRAPH_HPP
#define SHASTA_MODE3A_ASSEMBLY_GRAPH_HPP

// See comments in mode3a.hpp.

// Shasta.
#include "ReadId.hpp"

// Boost libraries.
#include <boost/graph/adjacency_list.hpp>

// Standard library.
#include <list>
#include "tuple.hpp"
#include "vector.hpp"

namespace shasta {
    namespace mode3a {

        class AssemblyGraph;
        class AssemblyGraphVertex;
        class AssemblyGraphEdge;
        using AssemblyGraphBaseClass = boost::adjacency_list<
            boost::listS, boost::listS, boost::bidirectionalS,
            AssemblyGraphVertex, AssemblyGraphEdge>;

        class PathEntry;
        class Transition;
        class TransitionInfo;

        class PackedMarkerGraph;
    }
}



// An entry in the path of an oriented read.
class shasta::mode3a::PathEntry {
public:
    OrientedReadId orientedReadId;

    // The position in the path for this oriented read.
    uint64_t position;
};



// Each vertex represents a segment replica.
class shasta::mode3a::AssemblyGraphVertex {
public:

    // The PackedMarkerGraph segment that this vertex corresponds to.
    uint64_t segmentId;

    // Replica index among all vertices with the same segmentId.
    uint64_t segmentReplicaIndex;

    AssemblyGraphVertex(uint64_t segmentId, uint64_t segmentReplicaIndex=0) :
        segmentId(segmentId), segmentReplicaIndex(segmentReplicaIndex) {}

    string stringId() const
    {
        string s = to_string(segmentId);
        if(segmentReplicaIndex != 0) {
            s += "." + to_string(segmentReplicaIndex);
        }
        return s;
    }

    // The path entries that go through this vertex.
    vector<PathEntry> pathEntries;
};



// A transition of an oriented read from a segment to the next.
class shasta::mode3a::Transition {
public:
    uint64_t position0;
    uint64_t position1;
    OrientedReadId orientedReadId;

    bool operator<(const Transition& that) const
    {
        return
            tie(orientedReadId, position0, position1) <
            tie(that.orientedReadId, that.position0, that.position1);
    }
};



class shasta::mode3a::TransitionInfo {
public:
    AssemblyGraphBaseClass::vertex_descriptor v0;
    AssemblyGraphBaseClass::vertex_descriptor v1;
    Transition transition;

    bool operator<(const TransitionInfo& that) const
    {
        return
            tie(v0, v1, transition) <
            tie(that.v0, that.v1, that.transition);
    }
};



// Each edge represents a link.
class shasta::mode3a::AssemblyGraphEdge {
public:

    vector<Transition> transitions;

    uint64_t coverage() const
    {
        return transitions.size();
    }
};



class shasta::mode3a::AssemblyGraph : public AssemblyGraphBaseClass {
public:
    AssemblyGraph(const PackedMarkerGraph&);
    void write(const string& name) const;

    // The sequence of segments visited by each oriented read
    // is a path, if we consider all edges (links) without regards to coverage.
    // Initially, we construct it from the corresponding journey
    // in the PackedMarkerGraph.
    // Indexed by OrientedReadId::getValue().
    vector< vector<vertex_descriptor> > paths;

    // Each segment in the AssemblyGraph corresponds to a segment in
    // this PackedMarkerGraph.
    const PackedMarkerGraph& packedMarkerGraph;
private:

    // The number of vertices created so far for each segmentId
    // (some of these may have been deleted).
    // This also equals the next id for a vertex with a given segmentId.
    vector<uint64_t> vertexCountBySegment;

    void createSegmentsAndPaths();
    void createLinks();

    void writeGfa(const string& name, uint64_t minLinkCoverage) const;
    void writeLinkCoverageHistogram(const string& name) const;
    void writePaths(const string& name) const;
};

#endif

