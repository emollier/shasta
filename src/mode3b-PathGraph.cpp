// Shasta.
#include "mode3b-PathGraph.hpp"
#include "Assembler.hpp"
#include "deduplicate.hpp"
#include "findLinearChains.hpp"
#include "orderPairs.hpp"
#include "transitiveReduction.hpp"
using namespace shasta;
using namespace mode3b;

// Boost libraries.
#include "boost/graph/filtered_graph.hpp"
#include "boost/graph/iteration_macros.hpp"
#include <boost/pending/disjoint_sets.hpp>

// Standard library.
#include "fstream.hpp"
#include <iomanip>
#include <queue>



GlobalPathGraph::GlobalPathGraph(const Assembler& assembler) :
    assembler(assembler)
{
    // EXPOSE WHEN CODE STABILIZES.
    minPrimaryCoverage = 8;
    maxPrimaryCoverage = 25;
    minCoverage = 2;
    minComponentSize = 100;
    const double minCorrectedJaccardForChain1 = 0.5;    // For chain creation
    const double minCorrectedJaccardForChain2 = 0.3;    // For chain graph creation
    const uint64_t minTotalBaseOffsetForChain = 200;

    createVertices();
    cout << "The path graph has " << verticesVector.size() << " vertices. "
        "Each vertex corresponds to a primary edge of the marker graph. " <<
        "The marker graph has a total " <<
        assembler.markerGraph.edges.size() << " edges." << endl;

    computeOrientedReadJourneys();
    createEdges();
    cout << "The path graph has " << edgesVector.size() << " edges." << endl;

    createComponents();



    // For each connected component of the GlobalPathGraph (a PathGraph)
    // find chains and generate a ChainGraph.
    {
        ofstream csv("Chains.csv");
        csv << "Component rank,ChainId,Position,EdgeId0,EdgeId1,Corrected Jaccard,Coverage,Base offset\n";
        for(uint64_t componentRank=0; componentRank<componentIndex.size(); componentRank++) {
            const uint64_t componentId = componentIndex[componentRank].first;
            PathGraph& component = components[componentId];
            component.findChains(minCorrectedJaccardForChain1, minTotalBaseOffsetForChain);

            // Write out the chains to csv.
            for(uint64_t chainId=0; chainId < component.chains.size(); chainId++) {
                const vector<PathGraph::edge_descriptor>& chain = component.chains[chainId];
                for(uint64_t position=0; position < chain.size(); position++) {
                    const PathGraph::edge_descriptor e = chain[position];
                    const PathGraphEdge& edge = component[e];
                    const PathGraph::vertex_descriptor v0 = source(e, component);
                    const PathGraph::vertex_descriptor v1 = target(e, component);
                    const MarkerGraphEdgeId edgeId0 = component[v0].edgeId;
                    const MarkerGraphEdgeId edgeId1 = component[v1].edgeId;
                    csv << componentRank << ",";
                    csv << chainId << ",";
                    csv << position << ",";
                    csv << edgeId0 << ",";
                    csv << edgeId1 << ",";
                    csv << edge.info.correctedJaccard() << ",";
                    csv << edge.info.common << ",";
                    csv << edge.info.offsetInBases << "\n";
                }
            }

            // Create a graph describing the chains in this connected component
            // and their connectivity.
            ChainGraph chainGraph(component, assembler, minCorrectedJaccardForChain2);
            ofstream out("ChainGraphComponent" + to_string(componentRank) + ".dot");
            chainGraph.writeGraphviz(out);
        }
    }



    // Graphviz output.
    for(uint64_t componentRank=0; componentRank<componentIndex.size(); componentRank++) {
        const uint64_t componentId = componentIndex[componentRank].first;
        const PathGraph& component = components[componentId];
        ofstream out("PathGraphComponent" + to_string(componentRank) + ".dot");
        component.writeGraphviz(componentRank, out, minCoverage);
    }
}



// Find out if a marker graph edge is a primary edge.
bool GlobalPathGraph::isPrimary(MarkerGraphEdgeId edgeId) const
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

#if 0
    // Check that is also is a branch edge.
    if(not isBranchEdge(edgeId)) {
        return false;
    }
#endif

    // If all above checks passed, this is a primary edge.
    return true;
}



void GlobalPathGraph::createVertices()
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
void GlobalPathGraph::computeOrientedReadJourneys()
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

    // Write the journeys to csv.
    ofstream csv("GlobalPathGraphJourneys.csv");
    for(ReadId readId=0; readId<assembler.markers.size()/2; readId++) {
        for(Strand strand=0; strand<2; strand++) {
            const OrientedReadId orientedReadId(readId, strand);
            const auto journey = orientedReadJourneys[orientedReadId.getValue()];
            csv << orientedReadId;
            for(const auto& p: journey) {
                const uint64_t vertexId = p.second;
                const MarkerGraphEdgeId edgeId = verticesVector[vertexId];
                csv << "," << edgeId;
            }
            csv << "\n";
        }
    }
}



void GlobalPathGraph::createEdges()
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



// Write the entire GlobalPathGraph in graphviz format.
void GlobalPathGraph::writeGraphviz() const
{
    ofstream out("GlobalPathGraph.dot");
    out << "digraph GlobalPathGraph {\n";

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



// Write a component of the GlobalPathGraph in graphviz format.
void PathGraph::writeGraphviz(
    uint64_t componentId,
    ostream& out,
    uint64_t minCoverage) const
{
    const PathGraph& graph = *this;
    out << "digraph PathGraphComponent" << componentId << " {\n";

    BGL_FORALL_VERTICES(v, graph, PathGraph) {
        out << graph[v].edgeId << ";\n";
    }



    BGL_FORALL_EDGES(e, graph, PathGraph) {
        const PathGraphEdge& edge = graph[e];
        const vertex_descriptor v0 = source(e, graph);
        const vertex_descriptor v1 = target(e, graph);

        // Color based on corrected Jaccard.
        const double correctedJaccard = edge.info.correctedJaccard();
        const double hue = correctedJaccard / 3.;   // 0=red, 0.5=yellow, 1=green

        out <<
            graph[v0].edgeId << "->" <<
            graph[v1].edgeId <<
            " [tooltip=\"" <<
            graph[v0].edgeId << "->" <<
            graph[v1].edgeId << " " <<
            " coverage " << edge.coverage << " J' " <<
            std::fixed << std::setprecision(2) << edge.info.correctedJaccard() << " offset " <<
            edge.info.offsetInBases;
        if(edge.adjacent) {
            out << " adjacent";
        }
        if(edge.chainId != invalid<uint64_t>) {
            out << " chain " << edge.chainId << " position " << edge.positionInChain;
        }
        out << "\" color=\"" << hue << ",1,1\"];\n";
    }

    out << "}\n";
}



void GlobalPathGraph::createComponents()
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
    for(uint64_t i=0; i<edgesVector.size(); i++) {
        const auto& p = edgesVector[i];
        const uint64_t vertexId0 = p.first;
        const uint64_t vertexId1 = p.second;
        const uint64_t componentId = disjointSets.find_set(vertexId0);
        SHASTA_ASSERT(componentId == disjointSets.find_set(vertexId1));
        const MarkerGraphEdgeId edgeId0 = verticesVector[vertexId0];
        const MarkerGraphEdgeId edgeId1 = verticesVector[vertexId1];
        MarkerGraphEdgePairInfo info;
        SHASTA_ASSERT(assembler.analyzeMarkerGraphEdgePair(edgeId0, edgeId1, info));
        const bool adjacent =
            assembler.markerGraph.edges[edgeId0].target ==
            assembler.markerGraph.edges[edgeId1].source;
        components[componentId].addEdge(edgeId0, edgeId1, edgeCoverage[i], info, adjacent);
    }

    // Transitive reduction of each connected component.
    for(uint64_t componentId=0; componentId<n; componentId++) {
        PathGraph& component = components[componentId];
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



void PathGraph::addVertex(MarkerGraphEdgeId edgeId)
{
    SHASTA_ASSERT(not vertexMap.contains(edgeId));
    vertexMap.insert({edgeId, add_vertex({edgeId}, *this)});
}



void PathGraph::addEdge(
    MarkerGraphEdgeId edgeId0,
    MarkerGraphEdgeId edgeId1,
    uint64_t coverage,
    const MarkerGraphEdgePairInfo& info,
    bool adjacent)
{
    auto it0 = vertexMap.find(edgeId0);
    auto it1 = vertexMap.find(edgeId1);
    SHASTA_ASSERT(it0 != vertexMap.end());
    SHASTA_ASSERT(it1 != vertexMap.end());
    const vertex_descriptor v0 = it0->second;
    const vertex_descriptor v1 = it1->second;

    add_edge(v0, v1, {coverage, info, adjacent}, *this);
}



// Find out if a marker graph edge is a branch edge.
// A marker graph edge is a branch edge if:
// - Its source vertex has more than one outgoing edge with coverage at least minPrimaryCoverage.
// OR
// - Its target vertex has more than one incoming edge with coverage at least minPrimaryCoverage.
bool GlobalPathGraph::isBranchEdge(MarkerGraphEdgeId edgeId) const
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




void PathGraph::findChains(
    double minCorrectedJaccard,
    uint64_t minTotalBaseOffset)
{
    PathGraph& graph = *this;

    // A predicate to select edges with correctedJaccard at least equal to
    // minCorrectedJaccard. This is used to create the filtered graph below.
    class EdgePredicate {
    public:
        EdgePredicate(
            const PathGraph& graph,
            double minCorrectedJaccard) :
            graph(&graph),
            minCorrectedJaccard(minCorrectedJaccard) {}
        EdgePredicate(const EdgePredicate& that) :
            graph(that.graph),
            minCorrectedJaccard(that.minCorrectedJaccard) {}
        EdgePredicate() :
            graph(0),
            minCorrectedJaccard(0) {}
        const PathGraph* graph;
        double minCorrectedJaccard;
        bool operator()(const edge_descriptor e) const
        {
            return (*graph)[e].info.correctedJaccard() >= minCorrectedJaccard;
        }
    };
    const EdgePredicate edgePredicate(graph, minCorrectedJaccard);

    // Create a filtered graph that uses the above predicate to select edges.
    using FilteredGraph = boost::filtered_graph<PathGraph, EdgePredicate>;
    const FilteredGraph filteredGraph(graph, edgePredicate);

    // Find linear chains of edges in the filtered graph.
    vector< vector<edge_descriptor> > allChains;
    findLinearChains(filteredGraph, 0, allChains);

    // Only keep the ones that have a sufficient total base offset.
    chains.clear();
    for(const vector<edge_descriptor>& chain: allChains) {
        uint64_t totalBaseOffset = 0;
        for(const edge_descriptor e: chain) {
            totalBaseOffset += graph[e].info.offsetInBases;
        }
        if(totalBaseOffset >= minTotalBaseOffset) {
            chains.push_back(chain);
        }

    }
    cout << "Found " << chains.size() << " chains in a connected component with " <<
        num_vertices(graph) << " vertices and " << num_edges(graph) << " edges." << endl;

    // Store chain information in the edges.
    for(uint64_t chainId=0; chainId<chains.size(); chainId++) {
        const vector<edge_descriptor>& chain = chains[chainId];
        for(uint64_t position=0; position<chain.size(); position++) {
            const edge_descriptor e = chain[position];
            PathGraphEdge& edge = graph[e];
            edge.chainId = chainId;
            edge.positionInChain = position;
        }
    }

}



ChainGraph::ChainGraph(
    const PathGraph& pathGraph,
    const Assembler& assembler,
    double minCorrectedJaccardForChain) :
    ChainGraphBaseClass(pathGraph.chains.size())
{
    ChainGraph& chainGraph = *this;

    // Two maps that, given a vertex of the PathGraph,
    // gives the chains that begins/ends there, if any.
    std::map<PathGraph::vertex_descriptor, vector<uint64_t> > chainBeginMap;
    std::map<PathGraph::vertex_descriptor, vector<uint64_t> > chainEndMap;
    for(uint64_t chainId=0; chainId<pathGraph.chains.size(); chainId++) {
        const vector<PathGraph::edge_descriptor>& chain = pathGraph.chains[chainId];
        SHASTA_ASSERT(not chain.empty());

        const PathGraph::vertex_descriptor v0 = source(chain.front(), pathGraph);
        chainBeginMap[v0].push_back(chainId);

        const PathGraph::vertex_descriptor v1 = target(chain.back(), pathGraph);
        chainEndMap[v1].push_back(chainId);
    }



    // Loop over chains.
    for(uint64_t chainId0=0; chainId0<pathGraph.chains.size(); chainId0++) {
        const vector<PathGraph::edge_descriptor>& chain0 = pathGraph.chains[chainId0];
        SHASTA_ASSERT(not chain0.empty());

        // Do a forward BFS starting at the end of this chain.
        // The BFS gets blocked at vertices where other chains begin.
        {
            const PathGraph::vertex_descriptor vStart = target(chain0.back(), pathGraph);
            std::queue<PathGraph::vertex_descriptor> q;
            q.push(vStart);
            std::set<PathGraph::vertex_descriptor> verticesEncountered;
            verticesEncountered.insert(vStart);
            while(not q.empty()) {

                // Dequeue a vertex.
                const PathGraph::vertex_descriptor v0 = q.front();
                q.pop();
                // cout << "Dequeued " << pathGraph[v0].edgeId << endl;

                // If any chains begin here, generate an edge between chain0 and those chains.
                // and don't continue the BFS from here.
                // Otherwise, continue the BFS.
                auto it = chainBeginMap.find(v0);
                if(it == chainBeginMap.end()) {
                    BGL_FORALL_OUTEDGES(v0, e, pathGraph, PathGraph) {
                        const PathGraph::vertex_descriptor v1 = target(e, pathGraph);
                        if(not verticesEncountered.contains(v1)) {
                            verticesEncountered.insert(v1);
                            q.push(v1);
                            // cout << "Enqueued " << pathGraph[v1].edgeId << endl;
                        }
                    }
                } else {
                    const vector<uint64_t>& chainIds1 = it->second;
                    for(const uint64_t chainId1: chainIds1) {

                        bool edgeExists;
                        tie(ignore, edgeExists) = edge(chainId0, chainId1, chainGraph);
                        if(edgeExists) {
                            continue;
                        }

                        const vector<PathGraph::edge_descriptor>& chain1 = pathGraph.chains[chainId1];
                        const MarkerGraphEdgeId edgeId0 = pathGraph[target(chain0.back(), pathGraph)].edgeId;
                        const MarkerGraphEdgeId edgeId1 = pathGraph[source(chain1.front(), pathGraph)].edgeId;
                        MarkerGraphEdgePairInfo info;
                        SHASTA_ASSERT(assembler.analyzeMarkerGraphEdgePair(edgeId0, edgeId1, info));
                        if(info.correctedJaccard() >= minCorrectedJaccardForChain) {
                            add_edge(chainId0, chainId1, {info}, chainGraph);
                        }
                    }
                }
            }
        }


        // Do a backward BFS starting at the beginning of this chain.
        // The BFS gets blocked at vertices where other chains end.
        {
            const PathGraph::vertex_descriptor vStart = source(chain0.front(), pathGraph);
            std::queue<PathGraph::vertex_descriptor> q;
            q.push(vStart);
            std::set<PathGraph::vertex_descriptor> verticesEncountered;
            verticesEncountered.insert(vStart);
            while(not q.empty()) {

                // Dequeue a vertex.
                const PathGraph::vertex_descriptor v0 = q.front();
                q.pop();
                // cout << "Dequeued " << pathGraph[v0].edgeId << endl;

                // If any chains ends here, generate an edge between chain0 and those chains.
                // and don't continue the BFS from here.
                // Otherwise, continue the BFS.
                auto it = chainEndMap.find(v0);
                if(it == chainEndMap.end()) {
                    BGL_FORALL_INEDGES(v0, e, pathGraph, PathGraph) {
                        const PathGraph::vertex_descriptor v1 = source(e, pathGraph);
                        if(not verticesEncountered.contains(v1)) {
                            verticesEncountered.insert(v1);
                            q.push(v1);
                            // cout << "Enqueued " << pathGraph[v1].edgeId << endl;
                        }
                    }
                } else {
                    const vector<uint64_t>& chainIds1 = it->second;
                    for(const uint64_t chainId1: chainIds1) {

                        bool edgeExists;
                        tie(ignore, edgeExists) = edge(chainId1, chainId0, chainGraph);
                        if(edgeExists) {
                            continue;
                        }

                        const vector<PathGraph::edge_descriptor>& chain1 = pathGraph.chains[chainId1];
                        const MarkerGraphEdgeId edgeId0 = pathGraph[target(chain1.back(), pathGraph)].edgeId;
                        const MarkerGraphEdgeId edgeId1 = pathGraph[source(chain0.front(), pathGraph)].edgeId;
                        MarkerGraphEdgePairInfo info;
                        SHASTA_ASSERT(assembler.analyzeMarkerGraphEdgePair(edgeId0, edgeId1, info));
                        if(info.correctedJaccard() >= minCorrectedJaccardForChain) {
                            add_edge(chainId1, chainId0, {info}, chainGraph);
                        }
                    }
                }
            }
        }
    }

    transitiveReduction(chainGraph);
}




void ChainGraph::writeGraphviz(ostream& out) const
{
    const ChainGraph& chainGraph = *this;

    out << std::fixed << std::setprecision(2);
    out << "digraph ChainGraph {\n";

    BGL_FORALL_VERTICES(v, chainGraph, ChainGraph) {
        out << v << ";\n";
    }

    BGL_FORALL_EDGES(e, chainGraph, ChainGraph) {
        const vertex_descriptor v0 = source(e, chainGraph);
        const vertex_descriptor v1 = target(e, chainGraph);

        // Color based on corrected Jaccard.
        const double correctedJaccard = chainGraph[e].info.correctedJaccard();
        const double hue = correctedJaccard / 3.;   // 0=red, 0.5=yellow, 1=green

        out << v0 << "->" << v1;

        out << "[";
        if(correctedJaccard > 0.) {
            out << "label=\"" <<  chainGraph[e].info.correctedJaccard() << "\"";
        } else {
            out << "style=dashed";
        }
        out << " color=\"" << hue << ",1,1\"";
        out << "];\n";
    }
    out << "}\n";
}
