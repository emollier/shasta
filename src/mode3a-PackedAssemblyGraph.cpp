// Shasta.
#include "mode3a-PackedAssemblyGraph.hpp"
#include "mode3a-AssemblyGraph.hpp"
#include "mode3a-PackedMarkerGraph.hpp"
#include "deduplicate.hpp"
#include "findLinearChains.hpp"
#include "orderPairs.hpp"
#include "removeReciprocalEdges.hpp"
#include "timestamp.hpp"
using namespace shasta;
using namespace mode3a;

// Boost libraries.
#include <boost/graph/filtered_graph.hpp>
#include <boost/graph/iteration_macros.hpp>

// Standard library.
#include "fstream.hpp"
#include <map>
#include "utility.hpp"



PackedAssemblyGraph::PackedAssemblyGraph(
    AssemblyGraph& assemblyGraph,
    uint64_t minSegmentCoverage,
    uint64_t minLinkCoverage1,
    uint64_t minLinkCoverage2,
    uint64_t minMarkerCount,
    double minJaccard,
    uint64_t mForJaccard,
    uint64_t threadCount) :
    assemblyGraph(assemblyGraph)
{
    PackedAssemblyGraph& packedAssemblyGraph = *this;
    createVertices(minSegmentCoverage, minLinkCoverage1, minMarkerCount);
    computeJourneys();
    writeJourneys();
    createEdgesUsingJourneys(minLinkCoverage2, minJaccard, mForJaccard, threadCount);
    writeVertices();
    writeGraphviz();
    writeGfa();

    cout << "The PackedAssemblyGraph has " << num_vertices(packedAssemblyGraph) <<
        " vertices and " << num_edges(packedAssemblyGraph) << " edges." << endl;
}



void PackedAssemblyGraph::createVertices(
    uint64_t minSegmentCoverage,
    uint64_t minLinkCoverage1,
    uint64_t minMarkerCount)
{
    PackedAssemblyGraph& packedAssemblyGraph = *this;

    // Create a subgraph using only vertices with
    // coverage at least minSegmentCoverage and
    // edges with coverage at least minLinkCoverage.
    BGL_FORALL_VERTICES(v, assemblyGraph, AssemblyGraph) {
        AssemblyGraphVertex& vertex = assemblyGraph[v];
        vertex.isActive = vertex.journeyEntries.size() >= minSegmentCoverage;
    }
    BGL_FORALL_EDGES(e, assemblyGraph, AssemblyGraph) {
        assemblyGraph[e].isActive = assemblyGraph.edgeCoverage(e) >= minLinkCoverage1;
    }
    boost::filtered_graph<AssemblyGraph, AssemblyGraphEdgePredicate, AssemblyGraphVertexPredicate>
        filteredAssemblyGraph(assemblyGraph,
            AssemblyGraphEdgePredicate(assemblyGraph),
            AssemblyGraphVertexPredicate(assemblyGraph));



    // Iteratively find linear chains of vertices in this subgraph of the assembly graph,
    // then filter out vertices in short linear chains.
    vector< vector<AssemblyGraph::vertex_descriptor> > chains;
    while(true) {
        findLinearVertexChains(filteredAssemblyGraph, chains);
        cout << "Found " << chains.size() << " linear chains." << endl;

        uint64_t shortChainCount = 0;
        for(const vector<AssemblyGraph::vertex_descriptor>& chain: chains) {

            // See if this chain is sufficiently long.
            uint64_t totalMarkerCount = 0;
            for(const AssemblyGraph::vertex_descriptor v: chain) {
                const uint64_t segmentId = assemblyGraph[v].segmentId;
                const uint64_t markerCount = assemblyGraph.packedMarkerGraph.segments[segmentId].size();
                totalMarkerCount += markerCount;
            }
            if(totalMarkerCount >= minMarkerCount) {
                continue;
            }

            // If getting here, this chain is short. Filter out
            // all of its vertices.
            ++shortChainCount;
            for(const AssemblyGraph::vertex_descriptor v: chain) {
                assemblyGraph[v].isActive = false;
            }
        }

        if(shortChainCount == 0) {
            break;
        }
    }



    // Set back all the isActive flags.
    BGL_FORALL_VERTICES(v, assemblyGraph, AssemblyGraph) {
        AssemblyGraphVertex& vertex = assemblyGraph[v];
        vertex.isActive = true;
    }
    BGL_FORALL_EDGES(e, assemblyGraph, AssemblyGraph) {
        assemblyGraph[e].isActive = true;
    }

    // Each linear chain generates a vertex of the PackedAssemblyGraph,
    uint64_t nextVertexId = 0;
    for(const vector<AssemblyGraph::vertex_descriptor>& chain: chains) {

        // Generate a PackedAssemblyGraph vertex corresponding to this linear chain
        // of Assembly graph vertices.
        const vertex_descriptor v = add_vertex(packedAssemblyGraph);
        PackedAssemblyGraphVertex& vertex = packedAssemblyGraph[v];
        vertex.id = nextVertexId++;
        vertex.assemblyGraphVertices = chain;

        // Store this information in the assembly graph.
        for(uint64_t position=0; position<chain.size(); position++) {
            const AssemblyGraph::vertex_descriptor av = chain[position];
            AssemblyGraphVertex& aVertex = assemblyGraph[av];
            aVertex.packedAssemblyGraphVertexId = vertex.id;
            aVertex.positionInPackedAssemblyGraph = position;
        }
    }
}



void PackedAssemblyGraph::computeJourneys()
{
    PackedAssemblyGraph& packedAssemblyGraph = *this;

    journeys.resize(assemblyGraph.journeys.size());
    for(uint64_t i=0; i<journeys.size(); i++) {
        journeys[i].resize(assemblyGraph.journeys[i].size(), null_vertex());
    }

    BGL_FORALL_VERTICES(pv, packedAssemblyGraph, PackedAssemblyGraph) {
        const auto& assemblyGraphVertices = packedAssemblyGraph[pv].assemblyGraphVertices;
        for(const auto av: assemblyGraphVertices) {
            const auto& assemblyGraphVertex = assemblyGraph[av];
            for(const JourneyEntry& journeyEntry: assemblyGraphVertex.journeyEntries) {
                const OrientedReadId orientedReadId = journeyEntry.orientedReadId;
                const uint64_t position = journeyEntry.position;
                journeys[orientedReadId.getValue()][position] = pv;
            }
        }
    }

    // Remove from the journeys:
    // - Entries equal to null_vertex.
    // - Duplicate entries.
    for(auto& journey: journeys) {
        vector<vertex_descriptor> newJourney;
        for(const vertex_descriptor v: journey) {
            if(v == null_vertex()) {
                continue;
            }
            if(newJourney.empty() or v != newJourney.back()) {
                newJourney.push_back(v);
            }
        }
        journey.swap(newJourney);
    }

    // Store journey entries in the vertices.
    for(ReadId i=0; i<journeys.size(); i++) {
        const auto& journey = journeys[i];
        const OrientedReadId orientedReadId = OrientedReadId::fromValue(i);
        for(uint64_t position=0; position<journey.size(); position++) {
            const vertex_descriptor v = journey[position];
            packedAssemblyGraph[v].journeyEntries.push_back({orientedReadId, position});
        }
    }
}



void PackedAssemblyGraph::writeJourneys() const
{
    const PackedAssemblyGraph& packedAssemblyGraph = *this;
    ofstream csv("PackedAssemblyGraphJourneys.csv");

    for(uint64_t i=0; i<journeys.size(); i++) {
        const OrientedReadId orientedReadId = OrientedReadId::fromValue(ReadId(i));
        const auto journey = journeys[orientedReadId.getValue()];

        csv << orientedReadId << ",";
        for(const vertex_descriptor v: journey) {
            csv << packedAssemblyGraph.vertexStringId(v) << ",";
        }
        csv << "\n";
    }

}


void PackedAssemblyGraph::createEdgesUsingJaccard(double minJaccard, uint64_t threadCount)
{
    PackedAssemblyGraph& packedAssemblyGraph = *this;

    // Compute candidate edges by following the journeys.
    vector< pair<vertex_descriptor, vertex_descriptor> > candidateEdges;
    for(const vector<vertex_descriptor>& journey: journeys) {
        for(uint64_t i=1; i<journey.size(); i++) {
            const vertex_descriptor v1 = journey[i];
            for(uint64_t j=0; j<i; j++) {
                const vertex_descriptor v0 = journey[j];
                if(v0 != v1) {
                    candidateEdges.push_back(make_pair(v0, v1));
                }
            }
        }
    }
    deduplicate(candidateEdges);

    // To compute Jaccard similarities, we need OrientedReadIds
    // to be available in AssemblyGraph vertices.
    assemblyGraph.computeVertexOrientedReadIds(threadCount);



    // Create the edges.
    vector<OrientedReadId> commonOrientedReadIds;
    for(const auto& p: candidateEdges) {

        // Access the PackedAssemblyGraph vertices for this candidate edge.
        const vertex_descriptor pv0 = p.first;
        const vertex_descriptor pv1 = p.second;
        const PackedAssemblyGraphVertex& vertex0 = packedAssemblyGraph[pv0];
        const PackedAssemblyGraphVertex& vertex1 = packedAssemblyGraph[pv1];

        // Access the relevant AssemblyGraph vertices.
        const auto av0 = vertex0.assemblyGraphVertices.back();
        const auto av1 = vertex1.assemblyGraphVertices.front();

        // If the Jaccard similarity is sufficiently high, create an edge.
        const double jaccard = assemblyGraph.computeJaccard(av0, av1, commonOrientedReadIds);
        if(jaccard >= minJaccard) {
            add_edge(pv0, pv1, PackedAssemblyGraphEdge({jaccard, invalid<uint64_t>}), packedAssemblyGraph);
        }
    }


    // Cleanup.
    assemblyGraph.clearVertexOrientedReadIds();
}



void PackedAssemblyGraph::createEdgesUsingJourneys(
    uint64_t minLinkCoverage2,
    double minJaccard,
    uint64_t mForJaccard,
    uint64_t threadCount)
{
    PackedAssemblyGraph& packedAssemblyGraph = *this;

    vector< pair<vertex_descriptor, vertex_descriptor> > transitions;
    for(const auto& journey: journeys) {
        for(uint64_t i=1; i<journey.size(); i++) {
            transitions.push_back({journey[i-1], journey[i]});
        }
    }

    vector<uint64_t> frequency;
    deduplicateAndCount(transitions, frequency);

    for(uint64_t i=0; i<transitions.size(); i++) {
        if(frequency[i] >= minLinkCoverage2) {
            const auto& transition = transitions[i];
            edge_descriptor e;
            tie(e, ignore) = add_edge(transition.first, transition.second,
                {invalid<double>, frequency[i]}, packedAssemblyGraph);
        }
    }

    // To compute Jaccard similarities, we need OrientedReadIds
    // to be available in AssemblyGraph vertices.
    assemblyGraph.computeVertexOrientedReadIds(threadCount);

    // Compute Jaccard similarity for all edges.
    vector<OrientedReadId> commonOrientedReadIds;
    BGL_FORALL_EDGES(e, packedAssemblyGraph, PackedAssemblyGraph) {
        PackedAssemblyGraphEdge& edge = packedAssemblyGraph[e];
        const vertex_descriptor pv0 = source(e, packedAssemblyGraph);
        const vertex_descriptor pv1 = target(e, packedAssemblyGraph);
        const PackedAssemblyGraphVertex& pVertex0 = packedAssemblyGraph[pv0];
        const PackedAssemblyGraphVertex& pVertex1 = packedAssemblyGraph[pv1];
        const vector<AssemblyGraph::vertex_descriptor> v0 = pVertex0.assemblyGraphVertices;
        const vector<AssemblyGraph::vertex_descriptor> v1 = pVertex1.assemblyGraphVertices;

        // Compute the best Jaccard similarity between
        // the last mForJaccard assembly graph vertices of pVertex0 and
        // the first mForJaccard assembly graph vertices of pVertex1.
        double maxJaccard = 0.;
        for(uint64_t i=0; i<min(mForJaccard, v0.size()); i++) {
            const AssemblyGraph::vertex_descriptor av0 = v0[v0.size() -1 - i];
            for(uint64_t j=0; j<min(mForJaccard, v1.size()); j++) {
                const AssemblyGraph::vertex_descriptor av1 = v1[j];
                const double jaccard = assemblyGraph.computeJaccard(av0, av1, commonOrientedReadIds);
                maxJaccard = max(maxJaccard, jaccard);
            }

        }
        edge.jaccard = maxJaccard;
    }

    // Cleanup.
    assemblyGraph.clearVertexOrientedReadIds();

    // Remove edges with low Jaccard similarity.
    vector<edge_descriptor> edgesToBeRemoved;
    BGL_FORALL_EDGES(e, packedAssemblyGraph, PackedAssemblyGraph) {
        if(packedAssemblyGraph[e].jaccard < minJaccard) {
            edgesToBeRemoved.push_back(e);
        }
    }
    for(const edge_descriptor e: edgesToBeRemoved) {
        boost::remove_edge(e, packedAssemblyGraph);
    }

    // removeReciprocalEdges(packedAssemblyGraph);
}



string PackedAssemblyGraph::vertexStringId(vertex_descriptor pv) const
{
    const PackedAssemblyGraph& packedAssemblyGraph = *this;
    const PackedAssemblyGraphVertex& packedAssemblyGraphVertex = packedAssemblyGraph[pv];
    return "P" + to_string(packedAssemblyGraphVertex.id);

#if 0
    const AssemblyGraph::vertex_descriptor v0 = packedAssemblyGraphVertex.assemblyGraphVertices.front();
    const AssemblyGraph::vertex_descriptor v1 = packedAssemblyGraphVertex.assemblyGraphVertices.back();

    return assemblyGraph.vertexStringId(v0) + "_" + assemblyGraph.vertexStringId(v1);
#endif
}



void PackedAssemblyGraph::writeGraphviz() const
{
    const PackedAssemblyGraph& packedAssemblyGraph = *this;

    ofstream dot("Mode3a-PackedAssemblyGraph.dot");
    dot << "digraph PackedAssemblyGraph {\n";

    BGL_FORALL_VERTICES(pv, packedAssemblyGraph, PackedAssemblyGraph) {
        const PackedAssemblyGraphVertex& pVertex = packedAssemblyGraph[pv];
        const AssemblyGraph::vertex_descriptor av0 = pVertex.assemblyGraphVertices.front();
        const AssemblyGraph::vertex_descriptor av1 = pVertex.assemblyGraphVertices.back();
        dot << "P" << pVertex.id;
        dot << " [label=\"P" <<
            pVertex.id << "\\n" <<
            assemblyGraph.vertexStringId(av0) << "\\n" <<
            assemblyGraph.vertexStringId(av1) << "\\n" <<
            pVertex.journeyEntries.size() << "/" <<
            10 * vertexMarkerCount(pv) <<
            "\"]";
        dot << ";\n";
    }

    dot << std::setprecision(3);
    BGL_FORALL_EDGES(e, packedAssemblyGraph, PackedAssemblyGraph) {
        const vertex_descriptor pv0 = source(e, packedAssemblyGraph);
        const vertex_descriptor pv1 = target(e, packedAssemblyGraph);

        // Color it red if jaccard=0, green if jaccard=1.
        const double jaccard = packedAssemblyGraph[e].jaccard;
        const double hue = jaccard / 3.;

        dot <<
            "P" << packedAssemblyGraph[pv0].id << "->" <<
            "P" << packedAssemblyGraph[pv1].id <<
            " [color=\"" << hue << ",1,1\"]"
            " [label=\"" << jaccard << "/" << packedAssemblyGraph[e].coverage << "\"]"
            ";\n";
    }

    dot << "}\n";
}



void PackedAssemblyGraph::writeGfa() const
{
    const PackedAssemblyGraph& packedAssemblyGraph = *this;

    ofstream gfa("Mode3a-PackedAssemblyGraph.gfa");
    gfa << "H\tVN:Z:1.0\n";

    BGL_FORALL_VERTICES(pv, packedAssemblyGraph, PackedAssemblyGraph) {
        const PackedAssemblyGraphVertex& pVertex = packedAssemblyGraph[pv];
        gfa <<"S\tP" << pVertex.id << "\t*\t";
        gfa << "LN:i:" << 10 * vertexMarkerCount(pv) << "\n";
    }

    BGL_FORALL_EDGES(e, packedAssemblyGraph, PackedAssemblyGraph) {
        const vertex_descriptor pv0 = source(e, packedAssemblyGraph);
        const vertex_descriptor pv1 = target(e, packedAssemblyGraph);
        gfa << "L\t" <<
            "P" << packedAssemblyGraph[pv0].id << "\t+\t" <<
            "P" << packedAssemblyGraph[pv1].id << "\t+\t*\n";
    }
}


// This computes the total number of markers, counting only the segments
// of this vertces. Links are not counted.
uint64_t PackedAssemblyGraph::vertexMarkerCount(vertex_descriptor pv) const
{
    const PackedAssemblyGraph& packedAssemblyGraph = *this;
    const vector<AssemblyGraph::vertex_descriptor>& assemblyGraphVertices =
        packedAssemblyGraph[pv].assemblyGraphVertices;

    uint64_t totalMarkerCount = 0;
    for(const AssemblyGraph::vertex_descriptor av: assemblyGraphVertices) {
        const AssemblyGraphVertex aVertex = assemblyGraph[av];
        const uint64_t segmentId = aVertex.segmentId;
        const uint64_t markerCount = assemblyGraph.packedMarkerGraph.segments[segmentId].size();
        totalMarkerCount += markerCount;
    }
    return totalMarkerCount;
}



void PackedAssemblyGraph::writeVertices() const
{
    const PackedAssemblyGraph& packedAssemblyGraph = *this;

    ofstream csv("Mode3a-PackedAssemblyGraphVertices.csv");
    BGL_FORALL_VERTICES(pv, packedAssemblyGraph, PackedAssemblyGraph) {
        const PackedAssemblyGraphVertex& pVertex = packedAssemblyGraph[pv];

        for(uint64_t position=0; position<pVertex.assemblyGraphVertices.size(); position++) {
            const AssemblyGraph::vertex_descriptor av = pVertex.assemblyGraphVertices[position];
            csv <<
                "P" << pVertex.id << "," <<
                position << "," <<
                assemblyGraph.vertexStringId(av) << "\n";
        }

    }


}
