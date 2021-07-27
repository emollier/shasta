#ifndef SHASTA_ASSEMBLY_GRAPH2_HPP
#define SHASTA_ASSEMBLY_GRAPH2_HPP

// Assembly graph for assembly mode2.



// Shasta.
#include "Marker.hpp"
#include "MarkerGraph.hpp"

// Boost libraries.
#include <boost/graph/adjacency_list.hpp>

// Standard library.
#include <map>
#include "string.hpp"
#include "vector.hpp"



namespace shasta {
    class AssemblyGraph2;
    class AssemblyGraph2Vertex;
    class AssemblyGraph2Edge;
    class MarkerGraph;

    using AssemblyGraph2BaseClass =
        boost::adjacency_list<boost::listS, boost::listS, boost::bidirectionalS,
        AssemblyGraph2Vertex, AssemblyGraph2Edge>;

    using MarkerGraphPath = vector<MarkerGraph::EdgeId>;

    namespace MemoryMapped {
        template<class T, class Int> class VectorOfVectors;
    }
}



class shasta::AssemblyGraph2Vertex {
public:
    MarkerGraph::VertexId markerGraphVertexId;

    AssemblyGraph2Vertex(MarkerGraph::VertexId markerGraphVertexId) :
        markerGraphVertexId(markerGraphVertexId) {}
};



class shasta::AssemblyGraph2Edge {
public:

    // Id used for gfa output.
    uint64_t id;



    // Each assembly graph edge corresponds to
    // a set of paths in the marker graph.
    // This way it can describe a bubble in the marker graph.

    // Class to describe a single branch.
    class Branch {
    public:
        MarkerGraphPath path;
        Branch(const MarkerGraphPath& path) : path(path) {}

        // Assembled sequence.
        // This excludes the first and last k/2 RLE bases.
        vector<Base> rawSequence;

        // Sequence to be written to gfa.
        vector<Base> gfaSequence;
    };
    vector<Branch> branches;

    // The reverse complement of this edge.
    // It contains the reverse complements of the bubbles of this edge,
    // in the same order.
    AssemblyGraph2BaseClass::edge_descriptor reverseComplement;

    // This constructor creates an edge without any paths.
    AssemblyGraph2Edge(uint64_t id) : id(id) {}

    // This constructor creates an edge with a single path.
    AssemblyGraph2Edge(uint64_t id, const MarkerGraphPath& path) :
        id(id), branches(1, Branch(path)) {}

    uint64_t ploidy() const {
        return branches.size();
    }

    bool isBubble() const
    {
        return ploidy() > 1;
    }

    // Construct a string to id each of the markerGraphPaths.
    string pathId(uint64_t branchId) const
    {
        string s = to_string(id);
        if(isBubble()) {
            s.append("." + to_string(branchId));
        }
        return s;
    }

    // Return the number of raw bases of sequence identical between
    // all branches at the beginning/end.
    uint64_t countCommonPrefixBases() const;
    uint64_t countCommonSuffixBases() const;

    // The number of raw sequence bases transfered
    // in each direction for gfa output.
    uint64_t backwardTransferCount = 0;
    uint64_t forwardTransferCount = 0;

    // Figure out if this is a bubble is caused by copy number
    // differences in repeats of period up to maxPeriod.
    // If this is the case, returns the shortest period for which this is true.
    // Otherwise, returns 0.
    uint64_t isCopyNumberDifference(uint64_t maxPeriod) const;
};



class shasta::AssemblyGraph2 : public AssemblyGraph2BaseClass {
public:

    // The constructor creates an edge for each linear path
    // in the marker graph. Therefore, immediately after construction,
    // each edge has a single MarkerGraphPath (no bubbles).
    AssemblyGraph2(
        uint64_t k, // Marker length
        const MemoryMapped::VectorOfVectors<CompressedMarker, uint64_t>& markers,
        const MarkerGraph&);

    void writeCsv(const string& baseName) const;
    void writeVerticesCsv(const string& baseName) const;
    void writeEdgesCsv(const string& baseName) const;
    void writeEdgeDetailsCsv(const string& baseName) const;

    // This writes a gfa and a csv file with the given base name.
    // If transferCommonBubbleSequence is true,
    // common sequence at the begin/end of all branches of a
    // bubble is donated to the preceding/following edge, when possible.
    // This is not const because it updates the number of bases transferred
    // for each bubble edge.
    void writeGfa(
        const string& baseName,
        bool writeSequence,
        bool transferCommonBubbleSequence);

    // Hide a AssemblyGraph2BaseClass::Base.
    using Base = shasta::Base;

private:

    // Some Assembler data that we need.
    uint64_t k;
    const MemoryMapped::VectorOfVectors<CompressedMarker, uint64_t>& markers;
    const MarkerGraph& markerGraph;

    // Map that gives us the vertex descriptor corresponding to
    // each marker graph vertex.
    std::map<MarkerGraph::VertexId, vertex_descriptor> vertexMap;

    // Initial creation of vertices and edges.
    void create();

    // Get the vertex descriptor for the vertex corresponding to
    // a given MarkerGraph::VertexId, creating the vertex if necessary.
    vertex_descriptor getVertex(MarkerGraph::VertexId);

    uint64_t nextEdgeId = 0;

    // Create a new edge corresponding to the given path.
    // Also create the vertices if necessary.
    edge_descriptor addEdge(const MarkerGraphPath&);

    // Assemble sequence for every marker graph path of every edge.
    void assemble();

    // Assemble sequence for every marker graph path of a given edge.
    void assemble(edge_descriptor);

    // Store GFA sequence in each edge.
    void storeGfaSequence(bool transferCommonBubbleSequence);

    // Finds edges that form bubbles, then combine
    // each of them into a single edge with multiple paths.
    void gatherBubbles();
    edge_descriptor createBubble(
        vertex_descriptor v0,
        vertex_descriptor v1,
        const vector<edge_descriptor>&);

    // Find bubbles caused by copy number changes in repeats
    // with period up to maxPeriod.
    void findCopyNumberBubbles(uint64_t maxPeriod);

    // For each edge, compute the number of raw sequence bases
    // transfered in each direction for gfa output.
    void countTransferredBases();

    // Return true if an edge has id less than its reverse complement.
    bool idIsLessThanReverseComplement(edge_descriptor) const;

    // Return true if an edge has id greater than its reverse complement.
    bool idIsGreaterThanReverseComplement(edge_descriptor) const;

private:
    void checkReverseComplementEdges() const;

};



#endif
