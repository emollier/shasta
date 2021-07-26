#include "AssemblyGraph2.hpp"
#include "AssembledSegment.hpp"
#include "assembleMarkerGraphPath.hpp"
#include "copyNumber.hpp"
#include "deduplicate.hpp"
#include "orderPairs.hpp"
using namespace shasta;

#include <boost/graph/iteration_macros.hpp>

#include "fstream.hpp"
#include <numeric>


// The constructor creates an edge for each linear path
// in the marker graph. Therefore, immediately after construction,
// each edge has a single MarkerGraphPath (no bubbles).
AssemblyGraph2::AssemblyGraph2(
    uint64_t k, // Marker length
    const MemoryMapped::VectorOfVectors<CompressedMarker, uint64_t>& markers,
    const MarkerGraph& markerGraph) :
    k(k),
    markers(markers),
    markerGraph(markerGraph)
{
    // Because of the way we write the GFA file (without overlaps),
    // k is required to be even.
    SHASTA_ASSERT((k % 2) == 0);

    // Maximum period to remove bubbles caused by copy number differences.
    const uint64_t maxPeriod = 4;

    // Initial creation of vertices and edges.
    // At this stage, every edge has exactly one branch (no bubbles).
    create();
    cout << "The initial AssemblyGraph2 has " << num_vertices(*this) << " vertices and " <<
        num_edges(*this) << " edges." << endl;
    writeGfa("AssemblyGraph2-0", false, true);

    // Assemble sequence for every marker graph path of every edge.
    assemble();

    // Gather bubble edges.
    gatherBubbles();
    cout << "After gathering bubbles, the AssemblyGraph2 has " << num_vertices(*this) << " vertices and " <<
        num_edges(*this) << " edges." << endl;
    writeGfa("AssemblyGraph2-1-NoSequence", false, true);
    writeGfa("AssemblyGraph2-1", true, true);

    // Find bubbles caused by copy number changes in repeats
    // with period up to maxPeriod.
    findCopyNumberBubbles(maxPeriod);

}



// Initial creation of vertices and edges.
void AssemblyGraph2::create()
{
    const bool debug = false;
    ofstream debugOut;
    if(debug) {
        debugOut.open("AssemblyGraph2-Constructor.txt");
    }

    const MarkerGraph::EdgeId edgeCount = markerGraph.edges.size();
    vector<bool> wasFound(edgeCount, false);

    MarkerGraphPath nextEdges;
    MarkerGraphPath previousEdges;
    MarkerGraphPath path;
    MarkerGraphPath reverseComplementedPath;



    // Main loop over all edges of the marker graph.
    // At each iteration we find a new linear path of edges.
    for(MarkerGraph::EdgeId startEdgeId=0; startEdgeId<edgeCount; startEdgeId++) {
        if(debug) {
            const MarkerGraph::Edge& startEdge = markerGraph.edges[startEdgeId];
            debugOut << "Starting a new path at edge " << startEdgeId << " " <<
                startEdge.source << "->" << startEdge.target << "\n";
        }

        // If we already found this edge, skip it.
        // It is part of a path we already found.
        if(wasFound[startEdgeId]) {
            continue;
        }

        // Follow the path forward.
        nextEdges.clear();
        MarkerGraph::EdgeId edgeId = startEdgeId;
        bool isCircular = false;
        while(true) {
            const MarkerGraph::Edge edge = markerGraph.edges[edgeId];
            const MarkerGraph::VertexId v1 = edge.target;
            const auto outEdges = markerGraph.edgesBySource[v1];
            if(outEdges.size() != 1) {
                break;
            }
            const auto inEdges = markerGraph.edgesByTarget[v1];
            if(inEdges.size() != 1) {
                break;
            }
            edgeId = outEdges[0];
            if(edgeId == startEdgeId) {
                isCircular = true;
                if(debug) {
                    cout << "Is circular." << endl;
                }
                break;
            }
            nextEdges.push_back(edgeId);
            SHASTA_ASSERT(not wasFound[edgeId]);
            if(debug) {
                debugOut << "Forward " << edgeId << " " <<
                    edge.source << "->" << edge.target << "\n";

            }
        }

        // Follow the path backward.
        if(!isCircular) {
            previousEdges.clear();
            edgeId = startEdgeId;
            while(true) {
                const MarkerGraph::Edge edge = markerGraph.edges[edgeId];
                const MarkerGraph::VertexId v0 = edge.source;
                const auto outEdges = markerGraph.edgesBySource[v0];
                if(outEdges.size() != 1) {
                    break;
                }
                const auto inEdges = markerGraph.edgesByTarget[v0];
                if(inEdges.size() != 1) {
                    break;
                }
                edgeId = inEdges[0];
                previousEdges.push_back(edgeId);
                SHASTA_ASSERT(not wasFound[edgeId]);
                if(debug) {
                    debugOut << "Backward " << edgeId << " " <<
                        edge.source << "->" << edge.target << "\n";
                }
            }
        }

        // Gather the path.
        path.clear();
        copy(previousEdges.rbegin(), previousEdges.rend(), back_inserter(path));
        path.push_back(startEdgeId);
        copy(nextEdges.begin(), nextEdges.end(), back_inserter(path));

        // Mark all the edges in the path as found.
        for(const MarkerGraph::EdgeId edgeId: path) {
            wasFound[edgeId] = true;
        }

        // Store this path as a new edge of the assembly graph.
        addEdge(path);
        if(debug) {
            for(const MarkerGraph::EdgeId edgeId: path) {
                const MarkerGraph::Edge& edge = markerGraph.edges[edgeId];
                debugOut << "Path " << edgeId << " " <<
                    edge.source << "->" << edge.target << "\n";
            }
        }

        // Also construct the reverse complemented path.
        reverseComplementedPath.clear();
        for(const MarkerGraph::EdgeId edgeId: path) {
            const MarkerGraph::EdgeId edgeIdRc = markerGraph.reverseComplementEdge[edgeId];
            reverseComplementedPath.push_back(edgeIdRc);
        }
        std::reverse(reverseComplementedPath.begin(), reverseComplementedPath.end());



        // Figure out if the reverse complemented chain is the same
        // as the original chain. This can happen in exceptional cases.
        bool isSelfComplementary = false;
        if(!isCircular) {
            isSelfComplementary = (path == reverseComplementedPath);
        } else {

            // For a circular path the test is more complex.
            // We check if the reverse complement of the first edge
            // is in the path.
            isSelfComplementary =
                find(path.begin(), path.end(), reverseComplementedPath.front()) != path.end();
        }


        // Store the reverse complemented path, if different from the original one.
        if(not isSelfComplementary) {
            addEdge(reverseComplementedPath);
            for(const MarkerGraph::EdgeId edgeIdRc: reverseComplementedPath) {
                SHASTA_ASSERT(not wasFound[edgeIdRc]);
                wasFound[edgeIdRc] = true;
            }

            if(debug) {
                for(const MarkerGraph::EdgeId edgeId: path) {
                    const MarkerGraph::Edge& edge = markerGraph.edges[edgeId];
                    debugOut << "Reverse complemented path " << edgeId << " " <<
                        edge.source << "->" << edge.target << "\n";
                }
            }
        }

    }



    // Check that all edges of the marker graph were found.
    SHASTA_ASSERT(find(wasFound.begin(), wasFound.end(), false) == wasFound.end());

}



// Get the vertex descriptor for the vertex corresponding to
// a given MarkerGraph::VertexId, creating the vertex if necessary.
AssemblyGraph2::vertex_descriptor AssemblyGraph2::getVertex(MarkerGraph::VertexId vertexId)
{
    const auto it = vertexMap.find(vertexId);
    if(it == vertexMap.end()) {
        const vertex_descriptor v = add_vertex(AssemblyGraph2Vertex(vertexId), *this);
        vertexMap.insert(make_pair(vertexId, v));
        return v;
    } else {
        return it->second;
    }
}



// Create a new edges corresponding to the given path.
// Also create the vertices if necessary.
void AssemblyGraph2::addEdge(const MarkerGraphPath& path)
{
    // Get the first and last edge of the path.
    const MarkerGraph::EdgeId edgeId0 = path.front();
    const MarkerGraph::EdgeId edgeId1 = path.back();
    const MarkerGraph::Edge edge0 = markerGraph.edges[edgeId0];
    const MarkerGraph::Edge edge1 = markerGraph.edges[edgeId1];

    // Get the first and last vertex of the path.
    // This creates the vertices if necessary.
    const MarkerGraph::VertexId vertexId0 = edge0.source;
    const MarkerGraph::VertexId vertexId1 = edge1.target;
    const vertex_descriptor v0 = getVertex(vertexId0);
    const vertex_descriptor v1 = getVertex(vertexId1);

    // Create the edge.
    add_edge(v0, v1, AssemblyGraph2Edge(nextEdgeId++, path), *this);
}



void AssemblyGraph2::writeCsv(const string& baseName) const
{
    writeVerticesCsv(baseName + "-Vertices.csv");
    writeEdgesCsv(baseName + "-Edges.csv");
    writeEdgeDetailsCsv(baseName + "-EdgeDetails.csv");
}



void AssemblyGraph2::writeVerticesCsv(const string& fileName) const
{
    const AssemblyGraph2& graph = *this;

    ofstream csv(fileName);
    csv << "VertexId0\n";
    BGL_FORALL_VERTICES(v, graph, AssemblyGraph2) {
        csv << graph[v].markerGraphVertexId << "\n";
    }
}



void AssemblyGraph2::writeEdgesCsv(const string& fileName) const
{
    const AssemblyGraph2& graph = *this;

    ofstream csv(fileName);
    csv << "VertexId0,VertexId1\n";
    BGL_FORALL_EDGES(e, graph, AssemblyGraph2) {
        const vertex_descriptor v0 = source(e, graph);
        const vertex_descriptor v1 = target(e, graph);
        csv << graph[v0].markerGraphVertexId << ",";
        csv << graph[v1].markerGraphVertexId << "\n";
    }

}



void AssemblyGraph2::writeEdgeDetailsCsv(const string& fileName) const
{
    using G = AssemblyGraph2;
    const G& g = *this;

    ofstream csv(fileName);
    csv << "FirstVertexId,LastVertexId,Branch,Position,EdgeId,VertexId0,VertexId1\n";

    // Loop over edges.
    BGL_FORALL_EDGES(e, g, G) {
        const AssemblyGraph2Edge& edge = g[e];
        const vertex_descriptor v0 = source(e, g);
        const vertex_descriptor v1 = target(e, g);
        const MarkerGraph::VertexId vertexId0 = g[v0].markerGraphVertexId;
        const MarkerGraph::VertexId vertexId1 = g[v1].markerGraphVertexId;

        // Loop over branches of this edge.
        for(uint64_t branchId=0; branchId<edge.ploidy(); branchId++) {
            const AssemblyGraph2Edge::Branch& branch = edge.branches[branchId];
            const MarkerGraphPath& path = branch.path;
            for(uint64_t j=0; j<path.size(); j++) {
                const MarkerGraph::EdgeId edgeId = path[j];
                const MarkerGraph::Edge& edge = markerGraph.edges[edgeId];
                csv << vertexId0 << ",";
                csv << vertexId1 << ",";
                csv << branchId << ",";
                csv << j << ",";
                csv << edgeId << ",";
                csv << edge.source << ",";
                csv << edge.target << "\n";
            }
        }
    }

}





// Assemble sequence for every marker graph path of every edge.
void AssemblyGraph2::assemble()
{
    using G = AssemblyGraph2;
    G& g = *this;

    cout << timestamp << "Assembling sequence." << endl;

    BGL_FORALL_EDGES(e, g, G) {
        assemble(e);
    }

    cout << timestamp << "Done assembling sequence." << endl;
}


// Assemble sequence for every marker graph path of a given edge.
void AssemblyGraph2::assemble(edge_descriptor e)
{
    using G = AssemblyGraph2;
    G& g = *this;


    AssemblyGraph2Edge& edge = g[e];
    for(AssemblyGraph2Edge::Branch& branch: edge.branches) {
        const MarkerGraphPath& path = branch.path;

        AssembledSegment assembledSegment;
        MarkerGraph::EdgeId const * const begin = &path[0];
        MarkerGraph::EdgeId const * const end = begin + path.size();
        const span<const MarkerGraph::EdgeId> pathSpan(begin, end);
        assembleMarkerGraphPath(k, markers, markerGraph, pathSpan, false, assembledSegment);



        // Store the sequence, excluding the first and last k/2 RLE bases.

        // Compute the number of raw bases to skip at the beginning.
        const uint64_t beginSkip = std::accumulate(
            assembledSegment.repeatCounts.begin(),
            assembledSegment.repeatCounts.begin() + k/2, 0);

        // Compute the number of raw bases to skip at the end.
        const uint64_t endSkip = std::accumulate(
            assembledSegment.repeatCounts.end() - k/2,
            assembledSegment.repeatCounts.end(), 0);

        // Copy the raw bases, excluding those corresponding to the first and last
        // k/2 RLE bases.
        branch.rawSequence.resize(assembledSegment.rawSequence.size() - beginSkip - endSkip);
        copy(
            assembledSegment.rawSequence.begin() + beginSkip,
            assembledSegment.rawSequence.end() - endSkip,
            branch.rawSequence.begin());
    }
}



// Finds edges that form bubbles, then combine
// each of them into a single edge with multiple paths.
void AssemblyGraph2::gatherBubbles()
{
    using G = AssemblyGraph2;
    G& g = *this;

    // Vector to contain pairs (v1, e01) for a single v0.
    // Here, e01 is an edge v0->v1.
    vector< pair<vertex_descriptor, edge_descriptor> > next;

    // Ploidy histogram.
    vector<uint64_t> ploidyHistogram;


    // Look for sets of parallel edges v0->v1.
    BGL_FORALL_VERTICES(v0, g, G) {

        next.clear();
        BGL_FORALL_OUTEDGES(v0, e01, g, G) {
            const vertex_descriptor v1 = target(e01, g);
            next.push_back(make_pair(v1, e01));
        }

        // Sort them by v1.
        sort(next.begin(), next.end(), OrderPairsByFirstOnly<vertex_descriptor, edge_descriptor>());


        // Find streaks with the same v1.
        for(auto it=next.begin(); it!=next.end(); /*Incremented in the loop*/) {
            const vertex_descriptor v1 = it->first;

            auto jt = it;
            for(; jt!=next.end(); ++jt) {
                if(jt->first != v1) {
                    break;
                }
            }

            // Here, it and jt define a streak with the same v1.
            const uint64_t ploidy = jt - it;
            if(ploidy >= ploidyHistogram.size()) {
                ploidyHistogram.resize(ploidy + 1);
            }
            ++ploidyHistogram[ploidy];


            // Combine the edges in the streak into a new edge.
            if(ploidy > 1) {
                edge_descriptor eNew;
                bool edgeWasAdded = false;
                tie(eNew, edgeWasAdded) = add_edge(v0, v1, AssemblyGraph2Edge(nextEdgeId++), g);
                SHASTA_ASSERT(edgeWasAdded);
                AssemblyGraph2Edge& edgeNew = g[eNew];

                for(auto kt=it; kt!=jt; kt++) {
                    const edge_descriptor eOld = kt->second;
                    const AssemblyGraph2Edge& edgeOld = g[eOld];
                    copy(edgeOld.branches.begin(), edgeOld.branches.end(),
                       back_inserter(edgeNew.branches));
                    boost::remove_edge(eOld, g);
                }
            }

            // Prepare to process the next streak.
            it = jt;
        }
    }

    cout << "Ploidy histogram (counting both strands):" << endl;
    for(uint64_t ploidy=1; ploidy<ploidyHistogram.size(); ploidy++) {
        cout << "Ploidy " << ploidy << ": " << ploidyHistogram[ploidy] << " edges." << endl;
    }

}



// Find bubbles caused by copy number changes in repeats
// with period up to maxPeriod.
void AssemblyGraph2::findCopyNumberBubbles(uint64_t maxPeriod)
{
    using G = AssemblyGraph2;
    G& g = *this;

    BGL_FORALL_EDGES(e, g, G) {
        const AssemblyGraph2Edge& edge = g[e];
        const uint64_t period = edge.isCopyNumberDifference(maxPeriod);
        if(period == 0) {
            continue;
        }
        cout << "Bubble " << edge.id << " of ploidy " << edge.ploidy() <<
            " is a copy number bubble with period " << period << endl;
    }
}



// This writes a gfa and a csv file with the given base name.
// If transferCommonBubbleSequence is true,
// common sequence at the begin/end of all branches of a
// bubble is donated to the preceding/following edge, when possible.
void AssemblyGraph2::writeGfa(
    const string& baseName,
    bool writeSequence,
    bool transferCommonBubbleSequence)
{
    using G = AssemblyGraph2;
    G& g = *this;


    // For each edge, compute the number of raw sequence bases
    // transfered in each direction for gfa output.
    if(transferCommonBubbleSequence) {
        countTransferredBases();
    }


    // Open the gfa and write the header.
    ofstream gfa(baseName + ".gfa");
    gfa << "H\tVN:Z:1.0\n";

    // Open the csv and write the header.
    ofstream csv(baseName + ".csv");
    csv << ",Color,First,Last\n";



    // Each edge of the AssemblyGraph2 generates a gfa Segment
    // for each of its marker graph paths.
    BGL_FORALL_EDGES(e, g, G) {
        const AssemblyGraph2Edge& edge = g[e];
        const vertex_descriptor v0 = source(e, g);
        const vertex_descriptor v1 = target(e, g);

        for(uint64_t branchId=0; branchId<edge.ploidy(); branchId++) {
            const AssemblyGraph2Edge::Branch& branch = edge.branches[branchId];
            const MarkerGraphPath& path = branch.path;

            // Write a Segment to the GFA file.
            gfa << "S\t" << edge.pathId(branchId) << "\t";
            if(writeSequence) {
                const vector<Base>& rawSequence = branch.rawSequence;
                uint64_t begin = 0;
                uint64_t end = rawSequence.size();
                uint64_t sequenceLength = 0;

                // If transferCommonBubbleSequence is true,  restrict the range
                // excluding and transferred bases.
                if(transferCommonBubbleSequence) {
                    begin = edge.backwardTransferCount;
                    end -= edge.forwardTransferCount;
                }


                // Write the sequence transferred forward by the preceding bubble, if appropriate.
                if(transferCommonBubbleSequence and not edge.isBubble()) {
                    if(in_degree(v0, g)==1 and out_degree(v0, g)==1) {
                        in_edge_iterator it;
                        tie(it, ignore) = in_edges(v0, g);
                        const AssemblyGraph2Edge& previousEdge = g[*it];
                        if(previousEdge.isBubble()) {
                            const vector<Base>& s = previousEdge.branches.front().rawSequence;
                            copy(s.end() - previousEdge.forwardTransferCount, s.end(),
                                ostream_iterator<Base>(gfa));
                            sequenceLength += previousEdge.forwardTransferCount;
                        }
                    }
                }

                copy(rawSequence.begin() + begin, rawSequence.begin() + end,
                    ostream_iterator<Base>(gfa));
                sequenceLength += (end - begin);

                // Write the sequence transferred backward by the following bubble, if appropriate.
                if(transferCommonBubbleSequence and not edge.isBubble()) {
                    if(in_degree(v1, g)==1 and out_degree(v1, g)==1) {
                        out_edge_iterator it;
                        tie(it, ignore) = out_edges(v1, g);
                        const AssemblyGraph2Edge& nextEdge = g[*it];
                        if(nextEdge.isBubble()) {
                            const vector<Base>& s = nextEdge.branches.front().rawSequence;
                            copy(s.begin(), s.begin() + nextEdge.backwardTransferCount,
                                ostream_iterator<Base>(gfa));
                            sequenceLength += nextEdge.backwardTransferCount;
                        }
                    }
                }

                gfa << "\tLN:i:" << sequenceLength << "\n";
            } else {
                gfa << "*\tLN:i:" << path.size() << "\n";
            }

            // Also write a line to the csv file.
            const string color = edge.isBubble() ? "Green" : "Grey";
            csv << edge.pathId(branchId) << "," << color << "," <<
                path.front() << "," << path.back() << "\n";
        }
    }



    // Generate link record.
    // For each vertex, we generate a Link for each pair of
    // incoming/outgoing marker graph paths.
    BGL_FORALL_VERTICES(v, g, G) {

        // Loop over marker graph paths of incoming edges.
        BGL_FORALL_INEDGES(v, e0, g, G) {
            const AssemblyGraph2Edge& edge0 = g[e0];
            for(uint64_t i0=0; i0<edge0.ploidy(); i0++) {

                // Loop over marker graph paths of outgoing edges.
                BGL_FORALL_OUTEDGES(v, e1, g, G) {
                    const AssemblyGraph2Edge& edge1 = g[e1];
                    for(uint64_t i1=0; i1<edge1.ploidy(); i1++) {

                        // To make Bandage happy, we write a Cigar string
                        // consisting of 0M rather than an empty Cigar string.
                        gfa << "L\t" <<
                            edge0.pathId(i0) << "\t+\t" <<
                            edge1.pathId(i1) << "\t+\t0M\n";

                    }
                }
            }
        }
    }
}



// For each edge, compute the number of raw sequence bases
// transfered in each direction for gfa output.
void AssemblyGraph2::countTransferredBases()
{
    using G = AssemblyGraph2;
    G& g = *this;

    BGL_FORALL_EDGES(e, g, G) {
        AssemblyGraph2Edge& edge = g[e];
        edge.backwardTransferCount = 0;
        edge.forwardTransferCount = 0;

        // To transfer any bases, the edge
        // must be a bubble preceded and followed by a single non-bubble.
        // If these conditions are not satisfied, leave the numbers
        // of transfered bases at 0.

        // The edge must be a bubble.
        if(not edge.isBubble()) {
            continue;
        }

        // v0 must have in-degree and out-degree 1.
        const vertex_descriptor v0 = source(e, g);
        if(in_degree(v0, g) != 1) {
            continue;
        }
        if(out_degree(v0, g) != 1) {
            continue;
        }

        // v1 must have in-degree and out-degree 1.
        const vertex_descriptor v1 = target(e, g);
        if(in_degree(v1, g) != 1) {
            continue;
        }
        if(out_degree(v1, g) != 1) {
            continue;
        }

        // The previous edge must not be a bubble.
        in_edge_iterator itPrevious;
        tie(itPrevious, ignore) = in_edges(v0, g);
        const AssemblyGraph2Edge& previousEdge = g[*itPrevious];
        if(previousEdge.isBubble()) {
            continue;
        }

        // The next edge must not be a bubble.
        out_edge_iterator itNext;
        tie(itNext, ignore) = out_edges(v1, g);
        const AssemblyGraph2Edge& nextEdge = g[*itNext];
        if(nextEdge.isBubble()) {
            continue;
        }

        // All conditions are satisfied.
        // Set the number of transfered bases equal to the number
        // of common identical prefix/suffix bases for all the
        // branches of this bubble.
        edge.backwardTransferCount = edge.countCommonPrefixBases();
        edge.forwardTransferCount = edge.countCommonSuffixBases();

        // Make sure we don't transfer more than the length of the
        // shortest branch of this edge.
        uint64_t shortestBranchLength = std::numeric_limits<uint64_t>::max();
        for(const AssemblyGraph2Edge::Branch& branch:edge.branches) {
            shortestBranchLength = min(shortestBranchLength, uint64_t(branch.rawSequence.size()));
        }
        while(true) {
            if(edge.backwardTransferCount + edge.forwardTransferCount <= shortestBranchLength) {
                break;
            }
            --edge.backwardTransferCount;
            if(edge.backwardTransferCount + edge.forwardTransferCount <= shortestBranchLength) {
                break;
            }
            --edge.forwardTransferCount;
        }
    }
}



// Return the number of raw bases of sequence identical between
// all branches at the beginning.
uint64_t AssemblyGraph2Edge::countCommonPrefixBases() const
{
    SHASTA_ASSERT(isBubble());

    const vector<Base>& firstRawSequence = branches.front().rawSequence;

    for(uint64_t position=0; position<firstRawSequence.size(); position++) {
        const Base base = firstRawSequence[position];

        for(uint64_t branchId=1; branchId<branches.size(); branchId++) {
            const vector<Base>& rawSequence = branches[branchId].rawSequence;
            if(position == rawSequence.size()) {
                return position;
            }
            if(rawSequence[position] != base) {
                return position;
            }
        }
    }

    return firstRawSequence.size();
}



// Return the number of raw bases of sequence identical between
// all branches at the end.
uint64_t AssemblyGraph2Edge::countCommonSuffixBases() const
{
    SHASTA_ASSERT(isBubble());

    const vector<Base>& firstRawSequence = branches.front().rawSequence;

    for(uint64_t position=0; position<firstRawSequence.size(); position++) {
        const Base base = firstRawSequence[firstRawSequence.size() - 1 - position];

        for(uint64_t branchId=1; branchId<branches.size(); branchId++) {
            const vector<Base>& rawSequence = branches[branchId].rawSequence;
            if(position == rawSequence.size()) {
                return position;
            }
            if(rawSequence[rawSequence.size() - 1 - position] != base) {
                return position;
            }
        }
    }

    return firstRawSequence.size();
}




// Figure out if this is a bubble is caused by copy number
// differences in repeats of period up to maxPeriod.
// If this is the case, returns the shortest period for which this is true.
// Otherwise, returns 0.
uint64_t AssemblyGraph2Edge::isCopyNumberDifference(uint64_t maxPeriod) const
{
    if(not isBubble()) {
        return 0;
    }

    // Check all pairs of branches.
    vector<uint64_t> periods;
    for(uint64_t i=0; i<branches.size()-1; i++) {
        const vector<Base>& iSequence = branches[i].rawSequence;
        for(uint64_t j=i+1; j<branches.size(); j++) {
            const vector<Base>& jSequence = branches[j].rawSequence;
            const uint64_t period = shasta::isCopyNumberDifference(iSequence, jSequence, maxPeriod);
            if(period == 0) {
                return false;
            }
            periods.push_back(period);
        }
    }
    deduplicate(periods);


    if(periods.size() == 1) {
        return periods.front();
    } else {
        return 0;
    }
}
