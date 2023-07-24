// Shasta.
#include "mode3b-PathGraph.hpp"
#include "Assembler.hpp"
#include "deduplicate.hpp"
#include "orderPairs.hpp"
#include "transitiveReduction.hpp"
using namespace shasta;
using namespace mode3b;

// Boost libraries.
#include "boost/graph/iteration_macros.hpp"
#include <boost/pending/disjoint_sets.hpp>

// Standard library.
#include "fstream.hpp"


PathGraph::PathGraph(const Assembler& assembler) :
    assembler(assembler)
{
    // EXPOSE WHEN CODE STABILIZES.
    minPrimaryCoverage = 8;
    maxPrimaryCoverage = 25;
    minCoverage = 6;
    minComponentSize = 6;
    const uint64_t minChainLength = 3;

    findVertices();
    cout << "The path graph has " << verticesVector.size() << " vertices. "
        "Each vertex corresponds to a primary edge of the marker graph. " <<
        "The marker graph has a total " <<
        assembler.markerGraph.edges.size() << " edges." << endl;

    computeOrientedReadJourneys();
    findEdges();
    cout << "The path graph has " << edgesVector.size() << " edges." << endl;

    createComponents();
    findChains(minChainLength);


    // Graphviz output.
    // writeGraphviz();    // Entire graph.
    for(uint64_t componentRank=0; componentRank<componentIndex.size(); componentRank++) {
        const uint64_t componentId = componentIndex[componentRank].first;
        const Graph& component = components[componentId];
        ofstream out("PathGraphComponent" + to_string(componentRank) + ".dot");
        component.writeGraphviz(componentRank, out);
    }
}



// Find out if a marker graph edge is a primary edge.
bool PathGraph::isPrimary(MarkerGraphEdgeId edgeId) const
{
    // Check coverage.
    const MarkerGraph& markerGraph = assembler.markerGraph;
    const uint64_t coverage = markerGraph.edgeCoverage(edgeId);
    if(coverage < minPrimaryCoverage) {
        return false;
    }
    if(coverage > maxPrimaryCoverage) {
        return false;
    }

    // Check for duplicate oriented reads on the edge.
    if(markerGraph.edgeHasDuplicateOrientedReadIds(edgeId)) {
        return false;
    }

    // Check for duplicate oriented reads on its vertices.
    const MarkerGraph::Edge& edge = markerGraph.edges[edgeId];
    if(
        const auto& markers = assembler.markers;
        markerGraph.vertexHasDuplicateOrientedReadIds(edge.source, markers) or
        markerGraph.vertexHasDuplicateOrientedReadIds(edge.target, markers)) {
        return false;
    }

    // Check that is also is a branch edge.
    if(not isBranchEdge(edgeId)) {
        return false;
    }

    // If all above checks passed, this is a primary edge.
    return true;
}



void PathGraph::findVertices()
{
    const MarkerGraph& markerGraph = assembler.markerGraph;

    verticesVector.clear();

    for(MarkerGraphEdgeId edgeId=0; edgeId<markerGraph.edges.size(); edgeId++) {
        if(isPrimary(edgeId)) {
            verticesVector.push_back(edgeId);
        }
    }
}



// The "journey" of each oriented read is the sequence of vertices it encounters.
// It stores pairs (ordinal0, vertexId) for each oriented read, sorted by ordinal0.
// The vertexId is the index in verticesVector.
// Indexed by OrientedReadId::getValue.
// Journeys are used to generate edges by "following the reads".
void PathGraph::computeOrientedReadJourneys()
{
    orientedReadJourneys.clear();
    orientedReadJourneys.resize(assembler.markers.size());

    for(uint64_t vertexId=0; vertexId<verticesVector.size(); vertexId++) {
        const MarkerGraphEdgeId edgeId = verticesVector[vertexId];

        // Loop over MarkerIntervals of this primary marker graph edge.
        const auto markerIntervals = assembler.markerGraph.edgeMarkerIntervals[edgeId];
        for(const MarkerInterval& markerInterval: markerIntervals) {
            const OrientedReadId orientedReadId = markerInterval.orientedReadId;
            const uint32_t ordinal0 = markerInterval.ordinals[0];
            orientedReadJourneys[orientedReadId.getValue()].push_back(make_pair(ordinal0, vertexId));
        }
    }

    // Now sort them, for each oriented read.
    for(auto& orientedReadJourney: orientedReadJourneys) {
        sort(orientedReadJourney.begin(), orientedReadJourney.end(),
            OrderPairsByFirstOnly<uint32_t, uint64_t>());
    }
}



void PathGraph::findEdges()
{

    edgesVector.clear();
    for(uint64_t i=0; i<orientedReadJourneys.size(); i++) {
        const auto& orientedReadJourney = orientedReadJourneys[i];

        for(uint64_t j=1; j<orientedReadJourney.size(); j++) {
            edgesVector.push_back({orientedReadJourney[j-1].second, orientedReadJourney[j].second});
        }
    }

    // Find coverage for each edge and remove the ones with low coverage;
    edgeCoverage.clear();
    deduplicateAndCountWithThreshold(edgesVector, edgeCoverage, minCoverage);
    edgesVector.shrink_to_fit();

    // Write a coverage histogram.
    {
        vector<uint64_t> histogram;
        for(const uint64_t c: edgeCoverage) {
            if(c >= histogram.size()) {
                histogram.resize(c+1, 0);
            }
            ++histogram[c];
        }

        ofstream csv("PathGraphCoverageHistogram.csv");
        csv << "Coverage,Frequency\n";
        for(uint64_t c=0; c<histogram.size(); c++) {
            const uint64_t frequency = histogram[c];
            if(frequency) {
                csv << c << ",";
                csv << frequency << "\n";
            }
        }
    }

}



// Write the entire PathGraph in graphviz format.
void PathGraph::writeGraphviz() const
{
    ofstream out("PathGraph.dot");
    out << "digraph PathGraph {\n";

    for(uint64_t i=0; i<edgesVector.size(); i++) {
        const pair<uint64_t, uint64_t>& edge = edgesVector[i];
        const uint64_t vertexId0 = edge.first;
        const uint64_t vertexId1 = edge.second;
        const MarkerGraphEdgeId edgeId0 = verticesVector[vertexId0];
        const MarkerGraphEdgeId edgeId1 = verticesVector[vertexId1];
        out << edgeId0 << "->";
        out << edgeId1;
        out << "[tooltip=\"" << edgeId0 << "->" << edgeId1 << " " << edgeCoverage[i] << "\"];\n";
    }

    out << "}\n";
}



// Write a component of the PathGraph in graphviz format.
void PathGraph::Graph::writeGraphviz(uint64_t componentId, ostream& out) const
{
    const Graph& graph = *this;
    out << "digraph PathGraphComponent" << componentId << " {\n";

    BGL_FORALL_VERTICES(v, graph, Graph) {
        out << graph[v].edgeId << ";\n";
    }

    BGL_FORALL_EDGES(e, graph, Graph) {
        const vertex_descriptor v0 = source(e, graph);
        const vertex_descriptor v1 = target(e, graph);
        out << graph[v0].edgeId << "->";
        out << graph[v1].edgeId << ";\n";
    }

    out << "}\n";
}



// Find linear chains of vertices with in-degree<=1, out-degree<=1.
void PathGraph::Graph::findLinearChains(
    uint64_t minChainLength,
    vector< vector<MarkerGraphEdgeId> >& chains)
{
    Graph& graph =*this;
    chains.clear();

    // Mark all vertices as unprocessed.
    BGL_FORALL_VERTICES(v, graph, Graph) {
        graph[v].wasProcessed = false;
    }



    // Main loop to find chains.
    vector<vertex_descriptor> forwardChain;
    vector<vertex_descriptor> backwardChain;
    BGL_FORALL_VERTICES(vStart, graph, Graph) {

        // See if we can start from here.
        if(graph[vStart].wasProcessed) {
            continue;
        }
        graph[vStart].wasProcessed = true;

        const uint64_t inDegree = in_degree(vStart, graph);
        if(inDegree > 1) {
            continue;
        }

        const uint64_t outDegree = out_degree(vStart, graph);
        if(outDegree > 1) {
            continue;
        }



        // Move forward.
        // This creates a forwardChain that includes vStart.
        forwardChain.clear();
        vertex_descriptor v = vStart;
        while(true) {
            if(in_degree(v, graph) > 1) {
                break;
            }
            const uint64_t outDegree = out_degree(v, graph);
            if(outDegree > 1) {
                break;
            }

            // Add it to the forward chain.
            forwardChain.push_back(v);

            if(outDegree == 0) {
                break;
            }

            // Move forward.
            out_edge_iterator it;
            tie(it, ignore) = out_edges(v, graph);
            const edge_descriptor e = *it;
            v = target(e, graph);
            graph[v].wasProcessed = true;
        }


        // Move backward.
        // This creates a backwardChain that includes vStart.
        backwardChain.clear();
        v = vStart;
        while(true) {
            if(out_degree(v, graph) > 1) {
                break;
            }
            const uint64_t inDegree = in_degree(v, graph);
            if(inDegree > 1) {
                break;
            }

            // Add it to the backward chain.
            backwardChain.push_back(v);

            if(inDegree == 0) {
                break;
            }

            // Move backward.
            in_edge_iterator it;
            tie(it, ignore) = in_edges(v, graph);
            const edge_descriptor e = *it;
            v = source(e, graph);
            graph[v].wasProcessed = true;
        }

        // Reverse the backward chain, then remove vStart.
        reverse(backwardChain.begin(), backwardChain.end());
        backwardChain.resize(backwardChain.size() - 1);

        // Check the length.
        if(forwardChain.size() + backwardChain.size() < minChainLength) {
            continue;
        }

        // Store this chain.
        chains.resize(chains.size() + 1);
        vector<MarkerGraphEdgeId>& chain = chains.back();
        for(const vertex_descriptor v: backwardChain) {
            chain.push_back(graph[v].edgeId);
        }
        for(const vertex_descriptor v: forwardChain) {
            chain.push_back(graph[v].edgeId);
        }
    }
}



void PathGraph::createComponents()
{
    // Compute connected components.
    const uint64_t n = verticesVector.size();
    vector<uint64_t> rank(n);
    vector<uint64_t> parent(n);
    boost::disjoint_sets<uint64_t*, uint64_t*> disjointSets(&rank[0], &parent[0]);
    for(uint64_t i=0; i<n; i++) {
        disjointSets.make_set(i);
    }
    for(const auto& p: edgesVector) {
        disjointSets.union_set(p.first, p.second);
    }

    // Generate vertices of each connected component.
    components.clear();
    components.resize(n);
    for(uint64_t i=0; i<n; i++) {
        const uint64_t componentId = disjointSets.find_set(i);
        components[componentId].addVertex(verticesVector[i]);
    }

    // Create edges of each connected component.
    for(const auto& p: edgesVector) {
        const uint64_t vertexId0 = p.first;
        const uint64_t vertexId1 = p.second;
        const uint64_t componentId = disjointSets.find_set(vertexId0);
        SHASTA_ASSERT(componentId == disjointSets.find_set(vertexId1));
        const MarkerGraphEdgeId edgeId0 = verticesVector[vertexId0];
        const MarkerGraphEdgeId edgeId1 = verticesVector[vertexId1];
        components[componentId].addEdge(edgeId0, edgeId1);
    }

    // Transitive reduction of each connected component.
    for(uint64_t componentId=0; componentId<n; componentId++) {
        Graph& component = components[componentId];
        if(num_vertices(component) > 2) {
            try {
                transitiveReduction(component);
            } catch(const exception& e) {
                cout << "Transitive reduction failed for component " <<
                    componentId << endl;
            }
        }
    }

    // Create the componentIndex.
    componentIndex.clear();
    for(uint64_t componentId=0; componentId<n; componentId++) {
        const uint64_t componentSize = num_vertices(components[componentId]);
        if(componentSize >= minComponentSize) {
            componentIndex.push_back({componentId, componentSize});
        }
    }
    sort(componentIndex.begin(), componentIndex.end(),
        OrderPairsBySecondOnlyGreater<uint64_t, uint64_t>());
    cout << "Found " << componentIndex.size() << " connected components of the path graph "
        "with at least " << minComponentSize << " vertices." << endl;
}



void PathGraph::Graph::addVertex(MarkerGraphEdgeId edgeId)
{
    SHASTA_ASSERT(not vertexMap.contains(edgeId));
    vertexMap.insert({edgeId, add_vertex(Vertex{edgeId}, *this)});
}



void PathGraph::Graph::addEdge(
    MarkerGraphEdgeId edgeId0,
    MarkerGraphEdgeId edgeId1)
{
    auto it0 = vertexMap.find(edgeId0);
    auto it1 = vertexMap.find(edgeId1);
    SHASTA_ASSERT(it0 != vertexMap.end());
    SHASTA_ASSERT(it1 != vertexMap.end());
    const vertex_descriptor v0 = it0->second;
    const vertex_descriptor v1 = it1->second;

    add_edge(v0, v1, *this);
}



// Find out if a marker graph edge is a branch edge.
// A marker graph edge is a branch edge if:
// - Its source vertex has more than one outgoing edge with coverage at least minPrimaryCoverage.
// OR
// - Its target vertex has more than one incoming edge with coverage at least minPrimaryCoverage.
bool PathGraph::isBranchEdge(MarkerGraphEdgeId edgeId) const
{
    // Access this marker graph edge and its vertices.
    const MarkerGraph::Edge& edge = assembler.markerGraph.edges[edgeId];
    const MarkerGraphVertexId vertexId0 = edge.source;
    const MarkerGraphVertexId vertexId1 = edge.target;

    // Check outgoing edges of vertexId0.
    const auto outgoingEdges0 = assembler.markerGraph.edgesBySource[vertexId0];
    uint64_t count0 = 0;
    for(const MarkerGraphEdgeId edgeId0: outgoingEdges0) {
        if(assembler.markerGraph.edgeCoverage(edgeId0) >= minPrimaryCoverage) {
            ++count0;
        }
    }
    if(count0 > 1) {
        return true;
    }

    // Check incoming edges of vertexId1.
    const auto incomingEdges1 = assembler.markerGraph.edgesByTarget[vertexId1];
    uint64_t count1 = 0;
    for(const MarkerGraphEdgeId edgeId1: incomingEdges1) {
        if(assembler.markerGraph.edgeCoverage(edgeId1) >= minPrimaryCoverage) {
            ++count1;
        }
    }
    if(count1 > 1) {
        return true;
    }

    return false;
}



void PathGraph::findChains(uint64_t minChainLength)
{
    chains.clear();
    vector< vector<MarkerGraphEdgeId> > componenthChains;

    for(uint64_t componentRank=0; componentRank<componentIndex.size(); componentRank++) {
        const uint64_t componentId = componentIndex[componentRank].first;
        Graph& component = components[componentId];
        component.findLinearChains(minChainLength, componenthChains);

        copy(componenthChains.begin(), componenthChains.end(), back_inserter(chains));
    }

    // Count the vertices in all chains.
    uint64_t chainVertexCount = 0;
    for(const auto& chain: chains) {
        chainVertexCount += chain.size();
    }

    cout << "Found " << chains.size() << " linear chains containing a total " <<
        chainVertexCount << " vertices." << endl;

    // Write the chains.
    ofstream csv("Chains.csv");
    for(uint64_t chainId=0; chainId<chains.size(); chainId++) {
        const auto& chain = chains[chainId];
        for(uint64_t position=0; position<chain.size(); position++) {
            csv << chainId << ",";
            csv << position << ",";
            csv << chain[position] << "\n";
        }
    }
}
