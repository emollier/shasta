#ifndef SHASTA_ENUMERATE_PATHS_HPP
#define SHASTA_ENUMERATE_PATHS_HPP

#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/iteration_macros.hpp>

#include "algorithm.hpp"
#include <stack>
#include "tuple.hpp"
#include "utility.hpp"
#include "vector.hpp"

namespace shasta {

    template<class G> void enumerateSelfAvoidingPaths(const G&,
        typename G::vertex_descriptor vA, typename G::vertex_descriptor vB,
        vector<vector<typename G::edge_descriptor> > &paths);

    template<class G, class PathInspector> void enumeratePaths(
        const G&,
        typename G::vertex_descriptor v,
        uint64_t pathLength,
        PathInspector&);
    template<class G, class PathInspector> void enumeratePathsRecursive(
        const G&,
        typename G::vertex_descriptor v,
        uint64_t pathLength,
        PathInspector&,
        vector<typename G::edge_descriptor>& path);

    void testEnumeratePaths();
}



// Enumerate self-avoiding paths starting at v0 and ending at v1.
// Self-avoiding means that an edge cannot be used twice.
template<class G> void shasta::enumerateSelfAvoidingPaths(const G &g,
    typename G::vertex_descriptor vA, typename G::vertex_descriptor vB,
    vector<vector<typename G::edge_descriptor> > &paths)
{
    using vertex_descriptor = typename G::vertex_descriptor;
    using edge_descriptor = typename G::edge_descriptor;
    using out_edge_iterator = typename G::out_edge_iterator;
    using Path = vector<typename G::edge_descriptor>;
    using std::stack;

    paths.clear();
    stack<Path> partialPaths;

    // For some reason I was not able to get BGL_FORALL_OUTEDGES_T 
    // to work, so using explicit iterators instead.
    out_edge_iterator it, end;

    std::tie(it, end) = boost::out_edges(vA, g);
    for (; it != end; ++it) {
        const edge_descriptor e = *it;
        const vertex_descriptor v1 = boost::target(e, g);
        const Path path(1, e);
        if (v1 == vB) {
            paths.push_back(path);
        } else {
            partialPaths.push(path);
        }
    }

    // Recursively add edges to the partial paths.
    while (not partialPaths.empty()) {
        const Path path = partialPaths.top();
        partialPaths.pop();

        const vertex_descriptor v0 = boost::target(path.back(), g);
        std::tie(it, end) = boost::out_edges(v0, g);
        for (; it != end; ++it) {
            const edge_descriptor e = *it;

            // If e is already on the path, skip it.
            if (find(path.begin(), path.end(), e) != path.end()) {
                continue;
            }

            const vertex_descriptor v1 = target(e, g);
            Path newPath = path;
            newPath.push_back(e);
            if (v1 == vB) {
                paths.push_back(newPath);
            } else {
                partialPaths.push(newPath);
            }
        }
    }
}



// In a directed graph of type G,
// enumerate all paths starting at v and with length (number of edges)
// up to pathLength.
// For each path found, apply the given function object by calling
// functionObject(path), where path is a vector<G::edge_descriptor>
template<class G, class PathInspector> void shasta::enumeratePaths(
    const G& g,
    typename G::vertex_descriptor v,
    uint64_t maxPathLength,
    PathInspector& pathInspector)
{
    vector<typename G::edge_descriptor> path;
    enumeratePathsRecursive(g, v, maxPathLength, pathInspector, path);
}
template<class G, class PathInspector> void shasta::enumeratePathsRecursive(
    const G& g,
    typename G::vertex_descriptor v,
    uint64_t maxPathLength,
    PathInspector& pathInspector,
    vector<typename G::edge_descriptor>& path)
{
    if(maxPathLength == 0) {
        return;
    }
    BGL_FORALL_OUTEDGES_T(v, e, g, G) {
        path.push_back(e);
        pathInspector(path);
        enumeratePathsRecursive(g, target(e, g), maxPathLength - 1, pathInspector, path);
        path.pop_back();
    }
}

#endif

