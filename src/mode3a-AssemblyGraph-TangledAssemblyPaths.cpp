// Shasta.
#include "mode3a-AssemblyGraph.hpp"
#include "deduplicate.hpp"
#include "enumeratePaths.hpp"
#include "invalid.hpp"
using namespace shasta;
using namespace mode3a;

// Boost libraries.
#include <boost/graph/iteration_macros.hpp>
#include <boost/graph/vf2_sub_graph_iso.hpp>
#include "dominatorTree.hpp"

// Standard library.
#include "array.hpp"
#include "fstream.hpp"



void AssemblyGraph::computeTangledAssemblyPaths(uint64_t threadCount)
{
    cout << "computeTangledAssemblyPaths begins." << endl;

    // Initialize a TangledAssemblyPath for each of the
    // longest path computed by analyzePartialPaths.
    tangledAssemblyPaths.clear();
    tangledAssemblyPaths.resize(analyzePartialPathsData.longestPaths.size());

    // Compute the TangledAssemblyPaths in parallel.
    setupLoadBalancing(tangledAssemblyPaths.size(), 1);
    runThreads(&AssemblyGraph::computeTangledAssemblyPathsThreadFunction, threadCount);
}



void AssemblyGraph::computeTangledAssemblyPathsThreadFunction(uint64_t threadId)
{
    ofstream debugOut("ComputeTangledAssemblyPaths-Thread" + to_string(threadId) + ".txt");

    uint64_t begin, end;
    while(getNextBatch(begin, end)) {
        for(uint64_t i=begin; i!=end; ++i) {
            computeTangledAssemblyPath(
                analyzePartialPathsData.longestPaths[i],
                tangledAssemblyPaths[i],
                debugOut);
        }
    }

}



void AssemblyGraph::computeTangledAssemblyPath(
    const vector<vertex_descriptor>& primaryVertices,
    TangledAssemblyPath& tangledAssemblyPath,
    ostream& debugOut
    )
{
    // Store the primary vertices.
    tangledAssemblyPath.primaryVertices = primaryVertices;

    // Compute the secondary vertices for each pair of primary vertices.
    tangledAssemblyPath.secondaryVerticesInfos.clear();
    tangledAssemblyPath.secondaryVerticesInfos.resize(tangledAssemblyPath.primaryVertices.size() - 1);

    for(uint64_t i=0; i<tangledAssemblyPath.secondaryVerticesInfos.size(); i++) {
        computeSecondaryVertices(
            tangledAssemblyPath.primaryVertices[i],
            tangledAssemblyPath.primaryVertices[i+1],
            tangledAssemblyPath.secondaryVerticesInfos[i],
            debugOut);
    }
    if(debugOut) {
        debugOut << "Tangled assembly path " <<
            vertexStringId(primaryVertices.front()) << "..." <<
            vertexStringId(primaryVertices.back()) << "\n";
        for(uint64_t i=0; /* Check later */ ; i++) {

            debugOut << vertexStringId(primaryVertices[i]) << " ";
            if(i == primaryVertices.size() - 1) {
                break;
            }

            for(const vertex_descriptor v: tangledAssemblyPath.secondaryVerticesInfos[i].secondaryVertices) {
                debugOut << vertexStringId(v) << " ";
            }
        }
        debugOut << "\n";
    }

}



// Given a pair of primary vertices in a tangled assembly path,
// compute the intervening secondary vertices.
// The code is similar to AssemblyGraph::computePartialPath2,
// but instead of using entire oriented read journeys,
// it only uses portions between v0 and v1.
void AssemblyGraph::computeSecondaryVertices(
    vertex_descriptor v0,
    vertex_descriptor v1,
    TangledAssemblyPath::SecondaryVertexInfo& secondaryVerticesInfo,
    ostream& debugOut)
{
    // EXPOSE WHEN CODE STABILIZES.
    const uint64_t minLinkCoverage = 3;

    const AssemblyGraph& assemblyGraph = *this;
    const AssemblyGraphVertex& vertex0 = assemblyGraph[v0];
    const AssemblyGraphVertex& vertex1 = assemblyGraph[v1];

#if 0
    const bool debug =
        vertex0.segmentId == 23361 and
        vertex1.segmentId == 7997;
#else
    const bool debug = true;
#endif

    if(debug and debugOut) {
        debugOut << "computeSecondaryVertices begins for " <<
            vertexStringId(v0) << " " << vertexStringId(v1) << endl;
    }

    // The vertices we encounter when following the oriented read journeys.
    vector<vertex_descriptor> verticesEncountered;

    // The transitions we encounter when following the oriented read journeys.
    vector< pair<vertex_descriptor, vertex_descriptor> > transitionsEncountered;

    // Joint loop over journey entries in v0 and v1.
    // The entries are sorted by OrientedReadId, but an
    // OrientedReadId can appear more than once.
    auto it0 = vertex0.journeyEntries.begin();
    auto it1 = vertex1.journeyEntries.begin();
    const auto end0 = vertex0.journeyEntries.end();
    const auto end1 = vertex1.journeyEntries.end();
    while(it0!=end0 and it1!=end1) {
        if(it0->orientedReadId < it1->orientedReadId) {
            ++it0;
            continue;
        }
        if(it0->orientedReadId > it1->orientedReadId) {
            ++it1;
            continue;
        }
        const OrientedReadId orientedReadId = it0->orientedReadId;
        SHASTA_ASSERT(orientedReadId == it1->orientedReadId);

        // Find the streaks in v0 and v1 for this oriented read.
        // In most cases these streak have length one as each
        // oriented read appears once in the journey entries
        // of each vertex.
        const auto streakBegin0 = it0;
        auto streakEnd0 = streakBegin0;
        while(streakEnd0 != end0 and streakEnd0->orientedReadId == orientedReadId) {
            ++streakEnd0;
        }
        const auto streakBegin1 = it1;
        auto streakEnd1 = streakBegin1;
        while(streakEnd1 != end1 and streakEnd1->orientedReadId == orientedReadId) {
            ++streakEnd1;
        }



        // Loop over entries in [streakBegin0, streakEnd0).
        for(auto jt0=streakBegin0; jt0!=streakEnd0; ++jt0) {
            const uint64_t position0 = jt0->position;

            // Find the best matching journey entry in [streakBegin1, streakEnd1).
            uint64_t bestPosition1 = invalid<uint64_t>;
            for(auto jt1=streakBegin1; jt1!=streakEnd1; ++jt1) {
                const uint64_t position1 = jt1->position;
                if(position1 < position0) {
                    continue;
                }
                bestPosition1 = min(bestPosition1, position1);
            }
            if(bestPosition1 == invalid<uint64_t>) {
                continue;
            }
            const uint64_t position1 = bestPosition1;

            // Consider all journey entries in [position0, position1]
            // for this oriented read.

            // Store the vertices encountered in this journey portion
            const auto journey = journeys[orientedReadId.getValue()];
            for(uint64_t position=position0; position<=position1; position++) {
                const vertex_descriptor v = journey[position];
                if(v != null_vertex()) {
                    verticesEncountered.push_back(v);
                }
            }

            // Also store the transitions.
            for(uint64_t position=position0+1; position<=position1; position++) {
                const vertex_descriptor v0 = journey[position-1];
                const vertex_descriptor v1 = journey[position];
                if(v0 != null_vertex() and v1 != null_vertex()) {
                    transitionsEncountered.push_back(make_pair(v0, v1));
                }
            }
        }

        // Position the iterators at the end of the streaks
        it0 = streakEnd0;
        it1 = streakEnd1;
    }



    // Count how many times we encountered each vertex.
    vector<uint64_t> vertexFrequency;
    deduplicateAndCount(verticesEncountered, vertexFrequency);

    // Locate v0 and v1 in verticesEncountered.
    const auto q0 = std::equal_range(verticesEncountered.begin(), verticesEncountered.end(), v0);
    SHASTA_ASSERT(q0.first != verticesEncountered.end());
    SHASTA_ASSERT(q0.second - q0.first == 1);
    const uint64_t iv0 = q0.first - verticesEncountered.begin();
    const auto q1 = std::equal_range(verticesEncountered.begin(), verticesEncountered.end(), v1);
    SHASTA_ASSERT(q1.first != verticesEncountered.end());
    SHASTA_ASSERT(q1.second - q1.first == 1);
    const uint64_t iv1 = q1.first - verticesEncountered.begin();

    // Count how many times we encountered each transition.
    // Keep only the ones that appear at least minLinkCoverage times.
    vector<uint64_t> transitionFrequency;
    deduplicateAndCountWithThreshold(
        transitionsEncountered, transitionFrequency, minLinkCoverage);

    // The transitions we kept define a graph.
    // The vertex stores vertexFrequency.
    // The edge stores transitionFrequency.
    using Graph = SecondaryVerticesGraph;
    Graph graph(
        assemblyGraph,
        verticesEncountered,
        vertexFrequency,
        transitionsEncountered,
        transitionFrequency,
        iv0, iv1);

    // Write the graph.
    if(debug and debugOut) {
        const string graphName = "ComputeSecondaryVerticesGraph" +
            vertexStringId(v0) + "_" + vertexStringId(v1);
        graph.write(debugOut, graphName);
    }

    // Compute the path v0...v1 on the dominator tree.
    graph.computeDominatorTreePath();
    if(false) {
        debugOut << "Dominator tree path:";
        for(const Graph::vertex_descriptor v: graph.dominatorTreePath) {
            debugOut << " " << graph.vertexStringId(v);
        }
        debugOut << "\n";
    }
    graph.computeBestPath(debugOut);


    // Use the best path stored in the graph to fill in the secondary
    // vertices between this pair of primary vertices.
    secondaryVerticesInfo.secondaryVertices.clear();
    for(uint64_t i=1; i<graph.bestPath.size(); i++) {
        const Graph::edge_descriptor e = graph.bestPath[i];
        const Graph::vertex_descriptor v = source(e, graph);
        const vertex_descriptor u = verticesEncountered[v];
        secondaryVerticesInfo.secondaryVertices.push_back(u);
    }

#if 0
    // Remove edges that "skip" a vertex. These are commonly generated by errors.
    if(debug and debugOut) {
        graph.handleDottedEdges1(debugOut);
    }

    // Figure out if the graph is linear.
    const bool isLinear = graph.isLinear(iv0, iv1);
    if(isLinear) {
        debugOut << "Graph is linear\n";
    } else {
        debugOut << "Graph is not linear\n";
    }

    return isLinear;
#endif
}



AssemblyGraph::SecondaryVerticesGraph::SecondaryVerticesGraph(
    const AssemblyGraph& assemblyGraph,
    const vector<AssemblyGraph::vertex_descriptor>& verticesEncountered,
    const vector<uint64_t>& vertexFrequency,
    const vector< pair<AssemblyGraph::vertex_descriptor, AssemblyGraph::vertex_descriptor> >&
        transitionsEncountered,
    const vector<uint64_t>& transitionFrequency,
    vertex_descriptor iv0,
    vertex_descriptor iv1) :
    SecondaryVerticesGraphBaseClass(verticesEncountered.size()),
    assemblyGraph(assemblyGraph),
    verticesEncountered(verticesEncountered),
    vertexFrequency(vertexFrequency),
    iv0(iv0),
    iv1(iv1)
{
    SHASTA_ASSERT(vertexFrequency.size() == verticesEncountered.size());
    SHASTA_ASSERT(transitionFrequency.size() == transitionsEncountered.size());

    using Graph = SecondaryVerticesGraph;
    Graph& graph = *this;

    BGL_FORALL_VERTICES(v, graph, Graph) {
        graph[v] = vertexFrequency[v];
    }

    for(uint64_t i=0; i<transitionsEncountered.size(); i++) {
        const auto& p = transitionsEncountered[i];
        array<AssemblyGraph::vertex_descriptor, 2> v = {p.first, p.second};
        array<uint64_t, 2> iv;
        for(uint64_t k=0; k<2; k++) {
            const auto q = std::equal_range(verticesEncountered.begin(), verticesEncountered.end(), v[k]);
            SHASTA_ASSERT(q.first != verticesEncountered.end());
            SHASTA_ASSERT(q.second - q.first == 1);
            iv[k] = q.first - verticesEncountered.begin();
        }
        edge_descriptor e;
        tie(e, ignore) = add_edge(iv[0], iv[1], graph);
        graph[e] = transitionFrequency[i];
    }
}



AssemblyGraph::SecondaryVerticesGraph::SecondaryVerticesGraph(
    const AssemblyGraph& assemblyGraph,
    const vector<AssemblyGraph::vertex_descriptor>& verticesEncountered,
    const vector<uint64_t>& vertexFrequency,
    uint64_t n) :
    SecondaryVerticesGraphBaseClass(n),
    assemblyGraph(assemblyGraph),
    verticesEncountered(verticesEncountered),
    vertexFrequency(vertexFrequency)
{
}



void AssemblyGraph::SecondaryVerticesGraph::write(
    ostream& graphOut,
    const string& graphName) const
{
    using Graph = SecondaryVerticesGraph;
    const Graph& graph = *this;

    graphOut << "digraph " << graphName << " {\n";

    // Write the vertices.
    BGL_FORALL_VERTICES(v, graph, Graph) {
        graphOut << "\"" << vertexStringId(v) << "\" [label=\"" <<
            vertexStringId(v) << "\\n" << graph[v] <<
            "\"];\n";
    }

    // Write the edges.
    BGL_FORALL_EDGES(e, graph, Graph) {
        const vertex_descriptor v0 = source(e, graph);
        const vertex_descriptor v1 = target(e, graph);
        const AssemblyGraph::vertex_descriptor u0 = verticesEncountered[v0];
        const AssemblyGraph::vertex_descriptor u1 = verticesEncountered[v1];
        graphOut <<
            "\"" << vertexStringId(v0) << "\"->\"" <<
            vertexStringId(v1) << "\" [label=\"" << graph[e] <<
                "\"";
        if(not assemblyGraph.segmentsAreAdjacent(u0, u1)) {
            graphOut << " style=dashed color=red";
        }
        graphOut << "];\n";
    }
    graphOut << "}\n";

}



bool AssemblyGraph::SecondaryVerticesGraph::isLinear(
    vertex_descriptor v0,
    vertex_descriptor v1
) const
{
    using Graph = SecondaryVerticesGraph;
    const Graph& graph = *this;

    BGL_FORALL_VERTICES(v, graph, Graph) {
        const uint64_t inDegree = in_degree(v, graph);
        const uint64_t outDegree = out_degree(v, graph);

        // Check v0.
        if(v == v0) {
            if(inDegree != 0) {
                return false;
            }
            if(outDegree != 1) {
                return false;
            }

        // Check v1.
        } else if(v == v1) {
            if(outDegree != 0) {
                return false;
            }
            if(inDegree != 1) {
                return false;
            }

        // Check the other vertices.
        } else {

            // Allow isolated vertex.
            if(inDegree==0 and outDegree==0) {
                // This vertex is isolated.
                // Don't check anything else.
            } else {
                if(outDegree != 1) {
                    return false;
                }
                if(inDegree != 1) {
                    return false;
                }
            }
        }
    }

    return true;

}



// Remove edges that "skip" a vertex. These are commonly generated by errors.
template <typename CorrespondenceMap1To2, typename CorrespondenceMap2To1>
bool AssemblyGraph::SecondaryVerticesGraph::HandleDottedEdges1Callback::operator()(
    const CorrespondenceMap1To2& vertexMap,
    const CorrespondenceMap2To1&)
{

    if(debugOut) {
        BGL_FORALL_VERTICES_T(v, smallGraph, SecondaryVerticesGraph) {
            const uint64_t iv = get(vertexMap, v);
            const AssemblyGraph::vertex_descriptor u = verticesEncountered[iv];
            debugOut << '(' << v << ", " <<
                assemblyGraph.vertexStringId(u) << ") ";
        }
        debugOut << "\n";

        debugOut <<
            "v0  = " <<
            assemblyGraph.vertexStringId(verticesEncountered[get(vertexMap, 0)]) <<
            ", v1  = " <<
            assemblyGraph.vertexStringId(verticesEncountered[get(vertexMap, 1)]) <<
            ", v2  = " <<
            assemblyGraph.vertexStringId(verticesEncountered[get(vertexMap, 2)]) << "\n";
    }



    // The small graph consists of edges 0->1, 1->2, and 0->2,
    // that is, edge 0->2 "skips" vertex 1.
    // Find the corresponding vertices and edges in the SecondaryVerticesGraph.
    // such that the following edges exist:
    const vertex_descriptor v0 = get(vertexMap, 0);
    const vertex_descriptor v1 = get(vertexMap, 1);
    const vertex_descriptor v2 = get(vertexMap, 2);
    edge_descriptor e01;
    edge_descriptor e12;
    edge_descriptor e02;
    tie(e01, ignore) = boost::edge(v0, v1, secondaryVerticesGraph);
    tie(e12, ignore) = boost::edge(v1, v2, secondaryVerticesGraph);
    tie(e02, ignore) = boost::edge(v0, v2, secondaryVerticesGraph);

    // Find coverage in the SecondaryVerticesGraph for these vertices and edges.
    const uint64_t c0 = vertexFrequency[v0];
    const uint64_t c1 = vertexFrequency[v1];
    const uint64_t c2 = vertexFrequency[v2];
    const uint64_t c01 = secondaryVerticesGraph[e01];
    const uint64_t c12 = secondaryVerticesGraph[e12];
    const uint64_t c02 = secondaryVerticesGraph[e02];

    if(debugOut) {
        debugOut << "c0 " << c0 << "\n";
        debugOut << "c1 " << c1 << "\n";
        debugOut << "c2 " << c2 << "\n";
        debugOut << "c01 " << c01 << "\n";
        debugOut << "c12 " << c12 << "\n";
        debugOut << "c02 " << c02 << "\n";
    }

    if(
        c02<c0 and c02<c1 and c02<c2 and
        c02<c01 and c02<c12) {
        edgesToBeRemoved.push_back(e02);
    }

    // Return true to continue the processing for other triangles in the
    // SecondaryVerticesGraph.
    return true;
}



// Remove edges that "skip" a vertex. These are commonly generated by errors.
void AssemblyGraph::SecondaryVerticesGraph::handleDottedEdges1(ostream& debugOut)
{
    using Graph = SecondaryVerticesGraph;
    Graph& graph = *this;

    // The small graph that we will look for.
    // Edge 0->2 "skips" vertex 1.
    Graph graphSmall(assemblyGraph, verticesEncountered, vertexFrequency, 3);
    add_edge(0, 1, graphSmall);
    add_edge(1, 2, graphSmall);
    add_edge(0, 2, graphSmall);

    vector<edge_descriptor> edgesToBeRemoved;
    HandleDottedEdges1Callback callback(
        assemblyGraph,
        verticesEncountered, vertexFrequency,
        graphSmall, graph, edgesToBeRemoved, debugOut);
    boost::vf2_subgraph_iso(graphSmall, graph, callback);

    // Remove the edges.
    deduplicate(callback.edgesToBeRemoved);
    for(const edge_descriptor e: callback.edgesToBeRemoved) {
        if(debugOut) {
            const vertex_descriptor v0 = source(e, graph);
            const vertex_descriptor v1 = target(e, graph);
            debugOut << "Removing edge " <<
                assemblyGraph.vertexStringId(verticesEncountered[v0]) << "->" <<
                assemblyGraph.vertexStringId(verticesEncountered[v1]) << "\n";
        }
        boost::remove_edge(e, graph);
    }
}



void AssemblyGraph::SecondaryVerticesGraph::computeDominatorTreePath()
{
    SecondaryVerticesGraph& graph = *this;

    // Compute the dominator tree with v0 as the entrance.
    std::map<vertex_descriptor, vertex_descriptor> predecessorMap;
    shasta::lengauer_tarjan_dominator_tree(
        graph,
        iv0,
        boost::make_assoc_property_map(predecessorMap));

    // Construct the dominator tree path starting at v1 and moving back.
    SHASTA_ASSERT(iv0 != iv1);
    dominatorTreePath.clear();
    dominatorTreePath.push_back(iv1);
    vertex_descriptor iv = iv1;
    while(true) {
        auto it = predecessorMap.find(iv);
        if(it == predecessorMap.end()) {
            SHASTA_ASSERT(0);   // v1 is not reachable from v0.
        }
        iv = it->second;
        dominatorTreePath.push_back(iv);
        if(iv == iv0) {
            break;
        }
    }
    std::reverse(dominatorTreePath.begin(), dominatorTreePath.end());

}



// Compute the "best" path between iv0 and iv1.
void AssemblyGraph::SecondaryVerticesGraph::computeBestPath(ostream& debugOut)
{
    SecondaryVerticesGraph& graph = *this;
    bestPath.clear();

    // Loop over legs of the dominator tree path.
    for(uint64_t i=1; i<dominatorTreePath.size(); i++) {
        const vertex_descriptor u0 = dominatorTreePath[i-1];
        const vertex_descriptor u1 = dominatorTreePath[i];

        // We need to find the "best" path between u0 and u1.


        // Enumerate paths u0->...->u1 on the graph.
        // I was not able to get path enumeration to work on a filtered
        // graph.
        vector< vector<edge_descriptor> > allPaths;
        enumerateSelfAvoidingPaths(graph, u0, u1, allPaths);
        if(false) {
            debugOut << "Looking for paths " <<
                vertexStringId(u0) << "->...->" << vertexStringId(u1) << "\n";
            debugOut << "Found " << allPaths.size() << " paths.\n";
        }

        // Find the paths that only use solid edges.
        vector< vector<edge_descriptor> > solidPaths;
        for(const auto& path: allPaths) {
            bool hasDottedEdges = false;
            for(const edge_descriptor e: path) {
                if(not segmentsAreAdjacent(e)) {
                    hasDottedEdges = true;
                    break;
                }
            }
            if(not hasDottedEdges) {
                solidPaths.push_back(path);
            }
        }
        if(false) {
            debugOut << solidPaths.size() << " paths use only solid edges.\n";
        }

        // If paths that only uses solid edges are present, choose among those.
        const vector< vector<edge_descriptor> >& pathsToChooseFrom =
            solidPaths.empty() ? allPaths : solidPaths;

        // We hope this never happens but this is not guaranteed,
        // so we will have to behave better eventually.
        SHASTA_ASSERT(not pathsToChooseFrom.empty());

        // If there is only one path to choose from, pick that one and we are done.
        if(pathsToChooseFrom.size() == 1) {
            copy(pathsToChooseFrom.front().begin(), pathsToChooseFrom.front().end(),
                back_inserter(bestPath));
            continue;
        }



        // There is more than one path to choose from for this leg of the dominator tree.
        // Find the "best" one. Pick the one with the highest minimum edge coverage.
        const vector<edge_descriptor>* bestLegPath = 0;
        uint64_t highestMinimumEdgeCoverage = 0;
        for(const vector<edge_descriptor>& path: pathsToChooseFrom) {

            // Find minimum edge coverage for this one.
            uint64_t minimumEdgeCoverage = std::numeric_limits<uint64_t>::max();
            for(const edge_descriptor e: path) {
                minimumEdgeCoverage = min(minimumEdgeCoverage, graph[e]);
            }
            if(minimumEdgeCoverage > highestMinimumEdgeCoverage) {
                highestMinimumEdgeCoverage = minimumEdgeCoverage;
                bestLegPath = &path;
            }

        }
        SHASTA_ASSERT(bestLegPath);
        copy(bestLegPath->begin(), bestLegPath->end(),
            back_inserter(bestPath));
    }

    // Write out the best path.
    if(debugOut) {
        debugOut << "Best path:";
        for(uint64_t i=0; i<bestPath.size(); i++) {
            const edge_descriptor e = bestPath[i];
            const vertex_descriptor v0 = source(e, graph);
            const vertex_descriptor v1 = target(e, graph);

            if(i == 0) {
                debugOut << " " << vertexStringId(v0);
            }
            debugOut << " " << vertexStringId(v1);
        }
        debugOut << "\n";
    }
}