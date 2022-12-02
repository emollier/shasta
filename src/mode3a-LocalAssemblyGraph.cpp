// Shasta.
#include "mode3a-LocalAssemblyGraph.hpp"
#include "computeLayout.hpp"
#include "html.hpp"
#include "HttpServer.hpp"
#include "mode3a-AssemblyGraphSnapshot.hpp"
#include "MurmurHash2.hpp"
#include "writeGraph.hpp"
using namespace shasta;
using namespace mode3a;

// Boost libraries.
#include <boost/geometry/algorithms/make.hpp>
#include <boost/geometry/algorithms/length.hpp>
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/iteration_macros.hpp>
#include <boost/graph/fruchterman_reingold.hpp>
#include <boost/graph/random_layout.hpp>
#include <boost/graph/topology.hpp>

// Standard library.
#include <map>
#include <queue>
#include "tuple.hpp"



// Create the LocalAssemblyGraph using a BFS
// that starts at the specified vertex and moves away
// (in both directions) up to the specified distance.
LocalAssemblyGraph::LocalAssemblyGraph(
    const AssemblyGraphSnapshot& assemblyGraphSnapshot,
    uint64_t startVertexId,
    uint64_t maxDistance,
    uint64_t minLinkCoverage) :
    assemblyGraphSnapshot(assemblyGraphSnapshot),
    maxDistance(maxDistance)
{
    LocalAssemblyGraph& localAssemblyGraph= *this;

    // The BFS queue.
    std::queue<uint64_t> q;

    // Map vertices of the AssemblyGraphSnapshot to vertices of the LocalAssemblyGraph.
    std::map<uint64_t, vertex_descriptor> vertexMap;

    // Initialize the BFS.
    if(maxDistance > 0) {
        q.push(startVertexId);
    }
    const vertex_descriptor vStart = addVertex(startVertexId, 0);
    vertexMap.insert(make_pair(startVertexId, vStart));



    // BFS.
    while(not q.empty()) {

        // Dequeue a segment.
        const uint64_t vertexId0 = q.front();
        q.pop();
        const vertex_descriptor v0 = vertexMap[vertexId0];
        const uint64_t distance0 = localAssemblyGraph[v0].distance;
        const uint64_t distance1 = distance0 + 1;

        // Loop over children.
        for(const uint64_t linkId: assemblyGraphSnapshot.edgesBySource[vertexId0]) {
            if(assemblyGraphSnapshot.getEdgeCoverage(linkId) < minLinkCoverage) {
                continue;
            }
            const AssemblyGraphSnapshot::Edge& link = assemblyGraphSnapshot.edgeVector[linkId];
            const uint64_t vertexId1 = link.vertexId1;
            if(vertexMap.find(vertexId1) != vertexMap.end()) {
                // We already encountered this vertex.
                continue;
            }
            const vertex_descriptor v1 = addVertex(vertexId1, distance1);
            vertexMap.insert(make_pair(vertexId1, v1));
            if(distance1 < maxDistance) {
                q.push(vertexId1);
            }
        }

        // Loop over parents.
        for(const uint64_t linkId: assemblyGraphSnapshot.edgesByTarget[vertexId0]) {
            if(assemblyGraphSnapshot.getEdgeCoverage(linkId) < minLinkCoverage) {
                continue;
            }
            const AssemblyGraphSnapshot::Edge& link = assemblyGraphSnapshot.edgeVector[linkId];
            const uint64_t vertexId1 = link.vertexId0;
            if(vertexMap.find(vertexId1) != vertexMap.end()) {
                // We already encountered this vertex.
                continue;
            }
            const vertex_descriptor v1 = addVertex(vertexId1, distance1);
            vertexMap.insert(make_pair(vertexId1, v1));
            if(distance1 < maxDistance) {
                q.push(vertexId1);
            }
        }
    }



    // Add the edges.
    for(const auto& p: vertexMap) {
        const uint64_t vertexId0 = p.first;
        const vertex_descriptor v0 = p.second;

        for(const uint64_t edgeId: assemblyGraphSnapshot.edgesBySource[vertexId0]) {
            if(assemblyGraphSnapshot.getEdgeCoverage(edgeId) < minLinkCoverage) {
                continue;
            }
            const AssemblyGraphSnapshot::Edge& edge = assemblyGraphSnapshot.edgeVector[edgeId];
            const uint64_t vertexId1 = edge.vertexId1;
            const auto it1 = vertexMap.find(vertexId1);
            if(it1 == vertexMap.end()) {
                continue;
            }
            const vertex_descriptor v1 = it1->second;
            boost::add_edge(v0, v1, LocalAssemblyGraphEdge(edgeId), localAssemblyGraph);
        }
    }

}


LocalAssemblyGraphVertex::LocalAssemblyGraphVertex(
    uint64_t vertexId,
    uint64_t distance) :
    vertexId(vertexId),
    distance(distance)
{
}



LocalAssemblyGraphVertex::LocalAssemblyGraphVertex() :
    vertexId(0),
    distance(0)
{
}



LocalAssemblyGraph::vertex_descriptor LocalAssemblyGraph::addVertex(
    uint64_t vertexId,
    uint64_t distance)
{
    return add_vertex(LocalAssemblyGraphVertex(vertexId, distance), *this);
}



uint64_t LocalAssemblyGraph::getVertexAssembledSequenceLength(vertex_descriptor v) const
{
    const LocalAssemblyGraph& localAssemblyGraph = *this;
    const LocalAssemblyGraphVertex& localAssemblyGraphVertex = localAssemblyGraph[v];
    const uint64_t assemblyGraphSnapshotVertexId = localAssemblyGraphVertex.vertexId;

    return assemblyGraphSnapshot.getVertexAssembledSequenceLength(assemblyGraphSnapshotVertexId);
}



void LocalAssemblyGraph::writeHtml(ostream& html, const SvgOptions& options) const
{
    // Write the svg object.
    html << "<div style='display: inline-block; vertical-align:top'>";
    writeSvg(html, options);
    html << "</div>";
    addSvgDragAndZoom(html);

    // Side panel.
    html << "<div style='display: inline-block'>";



    // Highlight a segment.
    html << R"stringDelimiter(
        <script>
        function highlightSegment()
        {
            // Get the segment id from the input field.
            var segmentId = document.getElementById("highlightSegmentId").value;
            var segmentCopyIndex = document.getElementById("highlightSegmentCopyIndex").value;
            var element = document.getElementById("Segment-" + segmentId + "-" + segmentCopyIndex);

            // Make it dashed and wider.
            var thickness = element.getAttribute("stroke-width");
            element.style.strokeDasharray = 0.2 * thickness;
            element.setAttribute("stroke-width", 2. * thickness);
        }
        </script>
        )stringDelimiter";



    // Zoom to a segment.
    html << R"stringDelimiter(
        <script>
        function zoomToSegment()
        {
            // Get the segment id from the input field.
            var segmentId = document.getElementById("zoomSegmentId").value;
            var segmentReplicaIndex = document.getElementById("zoomSegmentReplicaIndex").value;
            var element = document.getElementById("Segment-" + segmentId + "-" + segmentReplicaIndex);

            // Find the bounding box and its center.
            var box = element.getBBox();
            var xCenter = box.x + 0.5 * box.width;
            var yCenter = box.y + 0.5 * box.height;

            // Change the viewbox of the svg to be a bit larger than a square
            // containing the bounding box.
            var enlargeFactor = 5.;
            var size = enlargeFactor * Math.max(box.width, box.height);
            width = size;
            height = size;
            x = xCenter - 0.5 * size;
            y = yCenter - 0.5 * size;
            var svg = document.querySelector('svg');
            svg.setAttribute('viewBox', `${x} ${y} ${size} ${size}`);
            ratio = size / svg.getBoundingClientRect().width;

        }
        </script>

        <table>
        <tr><td><th>Segment id<th>Segment<br>replica index
        <tr><td>Highlight segment
        <td><input id=highlightSegmentId type=text onchange="highlightSegment()"
            size=10 style='text-align:center'>
        <td><input id=highlightSegmentCopyIndex type=text onchange="highlightSegment()"
            size=10 style='text-align:center' value='0'>
        <tr><td>Zoom to segment
        <td><input id=zoomSegmentId type=text onchange="zoomToSegment()"
            size=10 style='text-align:center'>
        <td><input id=zoomSegmentReplicaIndex type=text onchange="zoomToSegment()"
            size=10 style='text-align:center' value='0'>
        </table>
        )stringDelimiter";



    // Tables that will be automatically updated when the mouse is on a segment.
    html << R"zzz(  
<p>
Hover on a segment to populate the table below.
<p>
<table style='font-size:9'>
<tr><th class='left'>Segment id<td id='segmentIdCell' class=centered style='width:8em'>
<tr><th class='left'>Segment replica index<td id='segmentReplicaIndexCell' class=centered style='width:8em'>
</table>

<script>
function onMouseEnterSegment(
    segmentId,
    segmentReplicaIndex
)
{
    document.getElementById('segmentIdCell').innerHTML = segmentId;
    document.getElementById('segmentReplicaIndexCell').innerHTML = segmentReplicaIndex;
}
function onMouseExitSegment()
{
    document.getElementById('segmentIdCell').innerHTML = '';
    document.getElementById('segmentReplicaIndexCell').innerHTML = '';
}
</script>
    )zzz";



    // Change segment thickness
    html << R"stringDelimiter(
    <p><table>
    <tr><th class=left>Segment thickness<td>
    <button type='button' onClick='segmentThickness(0.1)' style='width:3em'>---</button>
    <button type='button' onClick='segmentThickness(0.5)' style='width:3em'>--</button>
    <button type='button' onClick='segmentThickness(0.8)' style='width:3em'>-</button>
    <button type='button' onClick='segmentThickness(1.25)' style='width:3em'>+</button>
    <button type='button' onClick='segmentThickness(2.)' style='width:3em'>++</button>
    <button type='button' onClick='segmentThickness(10.)' style='width:3em'>+++</button>
        <script>
        function segmentThickness(factor)
        {
            const group = document.getElementById('LocalAssemblyGraph-segments');
            descendants = group.querySelectorAll("path");
            for (let i=0; i<descendants.length; i++) {
                path = descendants[i];
                path.setAttribute('stroke-width', factor * path.getAttribute('stroke-width'));
            }
        }
        </script>
        )stringDelimiter";



    // Change link thickness
    html << R"stringDelimiter(
    <tr><th class=left>Link thickness<td>
    <button type='button' onClick='linkThickness(0.1)' style='width:3em'>---</button>
    <button type='button' onClick='linkThickness(0.5)' style='width:3em'>--</button>
    <button type='button' onClick='linkThickness(0.8)' style='width:3em'>-</button>
    <button type='button' onClick='linkThickness(1.25)' style='width:3em'>+</button>
    <button type='button' onClick='linkThickness(2.)' style='width:3em'>++</button>
    <button type='button' onClick='linkThickness(10.)' style='width:3em'>+++</button>
        <script>
        function linkThickness(factor)
        {
            const group1 = document.getElementById('LocalAssemblyGraph-links');
            for (let i=0; i<group1.children.length; i++) {
                group2 = group1.children[i];
                if(group2.tagName == 'g') {
                    for (let j=0; j<group2.children.length; j++) {
                        path = group2.children[j];
                        if(path.tagName == 'path') {
                            path.setAttribute('stroke-width', factor * path.getAttribute('stroke-width'));
                        }
                    }
                }
            }
        }
        </script>
        )stringDelimiter";



    // Zoom buttons.
    html << R"stringDelimiter(
    <tr title='Or use the mouse wheel.'><th class=left>Zoom<td>
    <button type='button' onClick='zoomSvg(0.1)' style='width:3em'>---</button>
    <button type='button' onClick='zoomSvg(0.5)' style='width:3em'>--</button>
    <button type='button' onClick='zoomSvg(0.8)' style='width:3em'>-</button>
    <button type='button' onClick='zoomSvg(1.25)' style='width:3em'>+</button>
    <button type='button' onClick='zoomSvg(2.)' style='width:3em'>++</button>
    <button type='button' onClick='zoomSvg(10.)' style='width:3em'>+++</button>
     </table>
        )stringDelimiter";

    // End of side panel.
    html << "</div>";

}



void LocalAssemblyGraph::writeSvg(
    const string& fileName,
    const SvgOptions& options) const
{
    ofstream svg(fileName);
    writeSvg(svg, options);
}
void LocalAssemblyGraph::writeSvg(
    ostream& svg,
    const SvgOptions& options
    ) const
{
    const LocalAssemblyGraph& localAssemblyGraph = *this;

    using boost::geometry::add_point;
    using boost::geometry::expand;
    using boost::geometry::make_inverse;
    using boost::geometry::multiply_value;
    using boost::geometry::subtract_point;
    using Box = boost::geometry::model::box<Point>;

    // Compute the view box.
    Box box = make_inverse<Box>();
    BGL_FORALL_VERTICES(v, localAssemblyGraph, LocalAssemblyGraph) {
        const LocalAssemblyGraphVertex& vertex = localAssemblyGraph[v];
        SHASTA_ASSERT(vertex.position.size() >= 2);
        const Point& p1 = vertex.position.front();
        const Point& p2 = vertex.position.back();

        expand(box, p1);
        expand(box, p2);
    }
    Point minCorner = box.min_corner();
    Point maxCorner = box.max_corner();

    // Add a bit of extra space.
    Point delta = maxCorner;
    subtract_point(delta, minCorner);
    multiply_value(delta, 0.05);
    subtract_point(minCorner, delta);
    add_point(maxCorner, delta);



    // Figure out the required size of the viewbox.
    Point diagonal = maxCorner;
    subtract_point(diagonal, minCorner);

    // Begin the svg.
    const string svgId = "LocalAssemblyGraph";
    svg << "\n<svg id='" << svgId <<
        "' width='" <<  options.sizePixels <<
        "' height='" << options.sizePixels <<
        "' viewbox='" << minCorner.x() << " " << minCorner.y() << " " <<
        diagonal.x() << " " <<
        diagonal.y() << "'"
        " style='border-style:solid;border-color:Black;'"
        ">\n";



    // Write the links first, so they don't overwrite the segments.
    svg << "<g id='" << svgId << "-links'>\n";
    BGL_FORALL_EDGES(e, localAssemblyGraph, LocalAssemblyGraph) {
        const uint64_t edgeId =  localAssemblyGraph[e].edgeId; // In the AssemblyGraphSnapshot

        // Access the LocalAssemblyGraph vertices corresponding to
        // the two segments of this Link and extract some information
        // from them.
        const vertex_descriptor v1 = source(e, localAssemblyGraph);
        const vertex_descriptor v2 = target(e, localAssemblyGraph);
        const LocalAssemblyGraphVertex& localAssemblyGraphVertex1 = localAssemblyGraph[v1];
        const LocalAssemblyGraphVertex& localAssemblyGraphVertex2 = localAssemblyGraph[v2];
        const AssemblyGraphSnapshot::Vertex& snapshotVertex1 =
            assemblyGraphSnapshot.vertexVector[localAssemblyGraphVertex1.vertexId];
        const AssemblyGraphSnapshot::Vertex& snapshotVertex2 =
            assemblyGraphSnapshot.vertexVector[localAssemblyGraphVertex2.vertexId];

        // Get the positions of the ends of this link.
        SHASTA_ASSERT(localAssemblyGraphVertex1.position.size() >= 2);
        SHASTA_ASSERT(localAssemblyGraphVertex2.position.size() >= 2);
        const Point& p1 = localAssemblyGraphVertex1.position.back();
        const Point& p2 = localAssemblyGraphVertex2.position.front();
        const double length = boost::geometry::distance(p1, p2);

        // Get the tangents and compute the control points.
        const double controlPointDistance = 0.25 * length;
        const Point& t1 = localAssemblyGraphVertex1.t2;
        const Point& t2 = localAssemblyGraphVertex2.t1;
        Point q1 = t1;
        multiply_value(q1, controlPointDistance);
        add_point(q1, p1);
        Point q2 = t2;
        multiply_value(q2, controlPointDistance);
        add_point(q2, p2);

        const double linkThickness =
            options.minimumLinkThickness +
            options.additionalLinkThicknessPerRead * double(assemblyGraphSnapshot.getEdgeCoverage(edgeId) - 1);

        const string dash =
            assemblyGraphSnapshot.segmentsAreAdjacent(edgeId) ? "" :
            " stroke-dasharray='0 " + to_string(1.5 * linkThickness) + "'";


        svg <<
            "<g>"
            // "<a href='exploreMode3AssemblyGraphLink?linkId=" << linkId << "'>"
            "<title>" <<
            snapshotVertex1.stringId() <<
            "->" << snapshotVertex2.stringId() <<
            " coverage " << assemblyGraphSnapshot.getEdgeCoverage(edgeId) <<
            "</title>"
            "<path d="
            "'M " << p1.x() << " " << p1.y() <<
            " C " << q1.x() << " " << q1.y() << ", "
                  << q2.x() << " " << q2.y() << ","
                  << p2.x() << " " << p2.y() << "'"
            " stroke='black'" <<
            dash <<
            " stroke-width='" << linkThickness << "'"
            " stroke-linecap='round'"
            " fill='transparent'"
            // " vector-effect='non-scaling-stroke'"
            // " onclick='if(event.ctrlKey) {location.href=\"exploreMode3AssemblyGraphLink?linkId=" << linkId << "\";}'"
            "/>"
            // "</a>"
            "</g>\n";

    }
    svg << "</g>\n";



    // Write the segments.
    svg << "<g id='" << svgId << "-segments'>\n";
    BGL_FORALL_VERTICES(v, localAssemblyGraph, LocalAssemblyGraph) {
        const LocalAssemblyGraphVertex& localAssemblyGraphVertex = localAssemblyGraph[v];
        const AssemblyGraphSnapshot::Vertex& snapshotVertex =
            assemblyGraphSnapshot.vertexVector[localAssemblyGraphVertex.vertexId];
        const uint64_t distance = localAssemblyGraph[v].distance;

        // Get the positions of the ends of this segment.
        SHASTA_ASSERT(localAssemblyGraphVertex.position.size() >= 2);
        const Point& p1 = localAssemblyGraphVertex.position.front();
        const Point& p2 = localAssemblyGraphVertex.position.back();
        const double length = boost::geometry::distance(p1, p2);

        // Get the tangents and compute the control points.
        const double controlPointDistance = 0.25 * length;
        const Point& t1 = localAssemblyGraphVertex.t1;
        const Point& t2 = localAssemblyGraphVertex.t2;
        Point q1 = t1;
        multiply_value(q1, -controlPointDistance);
        add_point(q1, p1);
        Point q2 = t2;
        multiply_value(q2, -controlPointDistance);
        add_point(q2, p2);

        // Decide the color for this segment.
        string color;
        if(distance == maxDistance) {
            color = "LightGray";
        } else {
            color = randomSegmentColor(snapshotVertex.segmentId);
        }

       // Create a marker to show the arrow for this segment.
        const string arrowMarkerName = "arrow" + to_string(localAssemblyGraphVertex.vertexId);
        svg <<
            "<defs>\n"
            "<marker id='" << arrowMarkerName <<
            "' viewBox='0 0 0.6 1'\n"
            "refX='0.1' refY='0.5'\n"
            "markerUnits='strokeWidth'\n"
            "markerWidth='0.6' markerHeight='1'\n"
            "orient='auto'>\n"
            "<path id='marker" << localAssemblyGraphVertex.vertexId << "' d='M 0 0 L 0.1 0 L 0.6 0.5 L 0.1 1 L 0 1 z' "
            "fill='" << color << "' "
            "/>\n"
            "</marker>\n"
            "</defs>\n";

        // Create a group to contain this segment.
        svg << "<g><title>" << snapshotVertex.stringId() << "</title>";

        // Add this segment to the svg.
        const auto oldPrecision = svg.precision(1);
        const auto oldFlags = svg.setf(std::ios_base::fixed, std::ios_base::floatfield);
        svg << "<path id='Segment-" << snapshotVertex.segmentId <<
            "-" << snapshotVertex.segmentReplicaIndex << "'" <<
            " onmouseenter='onMouseEnterSegment(" <<
            snapshotVertex.segmentId << "," <<
            snapshotVertex.segmentReplicaIndex << ")'" <<
            " onmouseleave='onMouseExitSegment()'" <<
            " d='M " <<
            p1.x() << " " << p1.y() << " L " <<
            p2.x() << " " << p2.y() << "'" <<
            " stroke='" << color << "'"
            " stroke-width='" <<
            options.minimumSegmentThickness /* + averageEdgeCoverage * options.additionalSegmentThicknessPerUnitCoverage*/ << "'"
            " fill='none'"
            " marker-end='url(#" <<
            arrowMarkerName <<
            ")'"
            // " onclick='if(event.ctrlKey) {"
            // "location.href=\"exploreMode3AssemblyGraphSegment?segmentId=" << segmentId <<
            // "&showSequence=on\";}'"
            "/>"
            "\n";
        svg.precision(oldPrecision);
        svg.flags(oldFlags);

        // End the group containing this segment.
        svg << "<g>";
    }

    // End the group containing segments.
    svg << "</g>\n";



    // End the svg.
    svg << "</svg>\n";
}



void LocalAssemblyGraph::computeLayout(
    const SvgOptions& options,
    double timeout)
{
    LocalAssemblyGraph& localAssemblyGraph = *this;

    // Create an auxiliary graph with some vertices for each segment.
    using G = boost::adjacency_list<boost::vecS, boost::vecS, boost::undirectedS>;
    G g;
    std::map<vertex_descriptor, vector<G::vertex_descriptor> > vertexMap;
    std::map<G::edge_descriptor, double> edgeLengthMap;
    BGL_FORALL_VERTICES(v, localAssemblyGraph, LocalAssemblyGraph) {

        const uint64_t assembledSequenceLength = localAssemblyGraph.getVertexAssembledSequenceLength(v);
        const double auxiliaryEdgeLength =
            (options.minimumSegmentLength +
            double(assembledSequenceLength) * options.additionalSegmentLengthPerBase) /
            double(options.auxiliaryVertexCountPerSegment - 1);

        // Add the auxiliary vertices.
        vector<G::vertex_descriptor>& auxiliaryVertices = vertexMap[v];
        for(uint64_t i=0; i<options.auxiliaryVertexCountPerSegment; i++) {
            auxiliaryVertices.push_back(boost::add_vertex(g));
        }

        // Add the edges between these auxiliary vertices.
        for(uint64_t i=1; i<options.auxiliaryVertexCountPerSegment; i++) {
            G::edge_descriptor e;
            tie(e, ignore) = boost::add_edge(auxiliaryVertices[i-1], auxiliaryVertices[i], g);
            edgeLengthMap.insert(make_pair(e, auxiliaryEdgeLength));
        }
    }



    // Add auxiliary graph edges between vertices corresponding to different
    // LocalAssemblyGraph vertices.
    BGL_FORALL_EDGES(e, localAssemblyGraph, LocalAssemblyGraph) {
        const vertex_descriptor v1 = source(e, localAssemblyGraph);
        const vertex_descriptor v2 = target(e, localAssemblyGraph);
        const bool segmentsAreAdjacent = assemblyGraphSnapshot.segmentsAreAdjacent(localAssemblyGraph[e].edgeId);

        double length = options.linkLength;
        if((not segmentsAreAdjacent) and (out_degree(v1, localAssemblyGraph)>1) and (in_degree(v2, localAssemblyGraph)>1)) {
            length *= 3.;
        }

        G::edge_descriptor eAuxiliary;
        tie(eAuxiliary, ignore) = add_edge(
            vertexMap[v1].back(),
            vertexMap[v2].front(),
            g);
        edgeLengthMap.insert(make_pair(eAuxiliary, length));
    }



    // Compute the layout of the auxiliary graph.
    std::map<G::vertex_descriptor, array<double, 2> > positionMap;
    ComputeLayoutReturnCode returnCode = ComputeLayoutReturnCode::Success;
    if(options.layoutMethod == "neato") {
        returnCode = shasta::computeLayoutGraphviz(g, "neato", timeout, positionMap, "", &edgeLengthMap);
    } else if(options.layoutMethod == "custom") {
        returnCode = shasta::computeLayoutCustom(g, edgeLengthMap, positionMap, timeout);
    } else {
        throw runtime_error("Invalid layout method specified: " + options.layoutMethod);
    }
    if(returnCode == ComputeLayoutReturnCode::Timeout) {
        throw runtime_error("Graph layout took too long. "
            "Increase the timeout or decrease the maximum distance.");
    }
    if(returnCode != ComputeLayoutReturnCode::Success) {
        throw runtime_error("Graph layout failed.");
    }



    // Store the layout in the vertices of the localAssemblyGraph.
    BGL_FORALL_VERTICES(v, localAssemblyGraph, LocalAssemblyGraph) {
        LocalAssemblyGraphVertex& vertex = localAssemblyGraph[v];
        vertex.position.clear();

        // Locate the auxiliary vertices corresponding to this segment.
        auto it = vertexMap.find(v);
        SHASTA_ASSERT(it != vertexMap.end());
        const vector<G::vertex_descriptor>& auxiliaryVertices = it->second;

        // Loop over the auxiliary vertices.
        for(const G::vertex_descriptor u: auxiliaryVertices) {
            auto jt = positionMap.find(u);
            SHASTA_ASSERT(jt != positionMap.end());
            const array<double, 2>& p = jt->second;
            vertex.position.push_back(Point(p[0], p[1]));
        }
    }
}



void LocalAssemblyGraph::computeSegmentTangents()
{
    LocalAssemblyGraph& localAssemblyGraph = *this;
    BGL_FORALL_VERTICES(v, localAssemblyGraph, LocalAssemblyGraph) {
        computeSegmentTangents(v);
    }
}




void LocalAssemblyGraph::computeSegmentTangents(vertex_descriptor v0)
{
    LocalAssemblyGraph& localAssemblyGraph = *this;
    LocalAssemblyGraphVertex& vertex0 = localAssemblyGraph[v0];
    SHASTA_ASSERT(vertex0.position.size() >= 2);
    const Point& vertex0Start = vertex0.position.front();
    const Point& vertex0End = vertex0.position.back();

    Point t = vertex0End;
    boost::geometry::subtract_point(t, vertex0Start);
    const double length = sqrt(t.x() * t.x() + t.y() * t.y());
    boost::geometry::multiply_value(t, 1. / length);
    vertex0.t2 = t;
    boost::geometry::multiply_value(t, -1.);
    vertex0.t1 = t;


#if 0
    // This is used if we display segments as Bezier cubics.


    // To compute t1, average the unit vectors of the backward links.
    array<double, 2> direction = {0., 0.};
    uint64_t n = 0;
    BGL_FORALL_INEDGES(v0, e, localAssemblyGraph, LocalAssemblyGraph) {
        const vertex_descriptor v1 = source(e, localAssemblyGraph);
        LocalAssemblyGraphVertex& vertex1 = localAssemblyGraph[v1];
        SHASTA_ASSERT(vertex1.position.size() >= 2);
        const Point& vertex1Start = vertex1.position.front();

        const double dx = vertex1Start.x() - vertex0End.x();
        const double dy = vertex1Start.y() - vertex0End.y();
        const double d = sqrt(dx * dx + dy * dy);
        if(d == 0.) {
            continue;
        }

        // Accumulate the unit vector.
        ++n;
        direction[0] += dx / d;
        direction[1] += dy / d;
    }
    // Compute the average,normalized direction.
    double dLength = sqrt(direction[0] * direction[0] + direction[1] * direction[1]);
    if(dLength == 0.) {
        direction[0] = vertex0Start.x() - vertex0End.x();
        direction[1] = vertex0Start.y() - vertex0End.y();
        dLength = sqrt(direction[0] * direction[0] + direction[1] * direction[1]);
    }
    direction[0] /= dLength;
    direction[1] /= dLength;

    vertex0.t1.x(direction[0]);
    vertex0.t1.y(direction[1]);



    // To compute the second control point, q2,
    // average the unit vectors of the forward links.
    direction = {0., 0.};
    n = 0;
    BGL_FORALL_OUTEDGES(v0, e, localAssemblyGraph, LocalAssemblyGraph) {
        const vertex_descriptor v1 = target(e, localAssemblyGraph);
        LocalAssemblyGraphVertex& vertex1 = localAssemblyGraph[v1];
        SHASTA_ASSERT(vertex1.position.size() >= 2);
        const Point& vertex1Start = vertex1.position.front();

        const double dx = vertex1Start.x() - vertex0End.x();
        const double dy = vertex1Start.y() - vertex0End.y();
        const double d = sqrt(dx * dx + dy * dy);
        if(d == 0.) {
            continue;
        }

        // Accumulate the unit vector.
        ++n;
        direction[0] += dx / d;
        direction[1] += dy / d;
    }
    // Compute the average,normalized direction.
    dLength = sqrt(direction[0] * direction[0] + direction[1] * direction[1]);
    if(dLength == 0.) {
        direction[0] = vertex0End.x() - vertex0Start.x();
        direction[1] = vertex0End.y() - vertex0Start.y();
        dLength = sqrt(direction[0] * direction[0] + direction[1] * direction[1]);
    }
    direction[0] /= dLength;
    direction[1] /= dLength;

    vertex0.t2.x(direction[0]);
    vertex0.t2.y(direction[1]);
#endif
}


// Return the svg color for a segment.
// All copies of a segment are alway displayed in the same color.
string LocalAssemblyGraph::randomSegmentColor(uint64_t segmentId)
{
    const uint32_t hue = MurmurHash2(&segmentId, sizeof(segmentId), 231) % 360;
    return "hsl(" + to_string(hue) + ",50%,50%)";
}



#if 0
// Find out if the paths of two segments are consecutive.
bool LocalAssemblyGraph::haveConsecutivePaths(
    vertex_descriptor v0,
    vertex_descriptor v1
) const
{
    const LocalAssemblyGraph& localAssemblyGraph = *this;

    const LocalAssemblyGraphVertex& vertex0 = localAssemblyGraph[v0];
    const LocalAssemblyGraphVertex& vertex1 = localAssemblyGraph[v1];

    const uint64_t segmentId0 = vertex0.segmentId;
    const uint64_t segmentId1 = vertex1.segmentId;

    const auto path0 = assemblyGraph.markerGraphPaths[segmentId0];
    const auto path1 = assemblyGraph.markerGraphPaths[segmentId1];

    const MarkerGraphEdgeId edgeId0 = path0.back();
    const MarkerGraphEdgeId edgeId1 = path1.front();

    const MarkerGraph::Edge& edge0 = markerGraph.edges[edgeId0];
    const MarkerGraph::Edge& edge1 = markerGraph.edges[edgeId1];

    return edge0.target == edge1.source;
}



// Return the average link separation for the Link
// described by an edge.
int32_t LocalAssemblyGraph::linkSeparation(edge_descriptor e) const
{
    const LocalAssemblyGraph& localAssemblyGraph = *this;
    const uint64_t linkId = localAssemblyGraph[e].linkId;
    return assemblyGraph.links[linkId].separation;
}
#endif



// Construct the svg options from an html request.
LocalAssemblyGraph::SvgOptions::SvgOptions(const vector<string>& request)
{
    // The initial layout method if set to "custom" if
    // command "customLayout" is available, "neato" otherwise.
    static bool firstTime = true;
    static string layoutDefaultMethod = "neato";
    if(firstTime) {
        firstTime = false;
        const string command = "which customLayout";
        const int returnCode = system(command.c_str());
        if(returnCode == 0) {
            layoutDefaultMethod = "custom";
        }
    }
    layoutMethod = layoutDefaultMethod;

    HttpServer::getParameterValue(request, "sizePixels", sizePixels);
    HttpServer::getParameterValue(request, "layoutMethod", layoutMethod);

    // Segment length and thickness.
    HttpServer::getParameterValue(request, "minimumSegmentLength", minimumSegmentLength);
    HttpServer::getParameterValue(request, "additionalSegmentLengthPerBase", additionalSegmentLengthPerBase);
    HttpServer::getParameterValue(request, "minimumSegmentThickness", minimumSegmentThickness);
    HttpServer::getParameterValue(request, "additionalSegmentThicknessPerUnitCoverage", additionalSegmentThicknessPerUnitCoverage);
    HttpServer::getParameterValue(request, "auxiliaryVertexCountPerSegment", auxiliaryVertexCountPerSegment);

    // Link length and thickness.
    HttpServer::getParameterValue(request, "linkLength", linkLength);
    HttpServer::getParameterValue(request, "minimumLinkThickness", minimumLinkThickness);
    HttpServer::getParameterValue(request, "additionalLinkThicknessPerRead", additionalLinkThicknessPerRead);
}



// Add rows to the html request form.
void LocalAssemblyGraph::SvgOptions::addFormRows(ostream& html)
{
    html <<
        "<tr>"
        "<td>Graphics size in pixels"
        "<td class=centered><input type=text name=sizePixels size=8 style='text-align:center'"
        " value='" << sizePixels <<
        "'>"

        "<tr>"
        "<td>Layout method"
        "<td class=left>"
        "<input type=radio name=layoutMethod value=neato"
        << (layoutMethod=="neato" ? " checked=checked" : "") <<
        ">Graphviz neato (slow for large graphs)<br>"
        "<input type=radio name=layoutMethod value=custom"
        << (layoutMethod=="custom" ? " checked=checked" : "") <<
        ">Custom (user-provided command <code>customLayout</code>)<br>"

        "<tr>"
        "<td>Segments"
        "<td class=centered>"
        "<table>"
        "<tr><td class=left>"
        "Minimum display length "
        "<td><input type=text name=minimumSegmentLength size=8 style='text-align:center'"
        " value='" << minimumSegmentLength << "'>"
        "<tr><td class=left>"
        "Additional display length per base"
        "<td><input type=text name=additionalSegmentLengthPerBase size=8 style='text-align:center'"
        " value='" << additionalSegmentLengthPerBase << "'>"
        "<tr>"
        "<td class=left>Minimum thickness"
        "<td class=centered><input type=text name=minimumSegmentThickness size=8 style='text-align:center'"
        " value='" << minimumSegmentThickness <<
        "'>"
        "<tr>"
        "<td class=left>Additional thickness per unit coverage"
        "<td class=centered><input type=text name=additionalSegmentThicknessPerUnitCoverage size=8 style='text-align:center'"
        " value='" << additionalSegmentThicknessPerUnitCoverage <<
        "'>"
        "<tr>"
        "<td class=left>Auxiliary vertices per segment"
        "<td class=centered><input type=number min=2 max=6 name=auxiliaryVertexCountPerSegment size=8 style='text-align:center'"
        " value='" << auxiliaryVertexCountPerSegment <<
        "'>"
        "</table>"



        "<tr>"
        "<td>Links"
        "<td class=centered>"
        "<table>"
        "<tr><td class=left>"
        "Display length "
        "<td><input type=text name=linkLength size=8 style='text-align:center'"
        " value='" << linkLength << "'>"
        "<tr>"
        "<td class=left>Minimum thickness"
        "<td class=centered><input type=text name=minimumLinkThickness size=8 style='text-align:center'"
        " value='" << minimumLinkThickness <<
        "'>"
        "<tr>"
        "<td class=left>Additional thickness per read"
        "<td class=centered><input type=text name=additionalLinkThicknessPerRead size=8 style='text-align:center'"
        " value='" << additionalLinkThicknessPerRead <<
        "'>"
        "</table>"

        "</table>";

}



#if 0
// Write the local assembly graph in gfa format.
void LocalAssemblyGraph::writeGfa(const string& fileName) const
{
    ofstream gfa(fileName);
    writeGfa(gfa);
}
void LocalAssemblyGraph::writeGfa(ostream& gfa) const
{
    const LocalAssemblyGraph& localAssemblyGraph = *this;

    // Write the header.
    gfa << "H\tVN:Z:1.0\n";

    // Write the segments.
    BGL_FORALL_VERTICES(v, localAssemblyGraph, LocalAssemblyGraph) {
        const uint64_t segmentId = localAssemblyGraph[v].segmentId;
        const auto path = assemblyGraph.markerGraphPaths[segmentId];
        gfa <<
            "S\t" << segmentId << "\t" <<
            "*\tLN:i:" << path.size() << "\n";
    }


    // Write the links.
    BGL_FORALL_EDGES(e, localAssemblyGraph, LocalAssemblyGraph) {
        const uint64_t linkId = localAssemblyGraph[e].linkId;
        const mode3::AssemblyGraph::Link& link = assemblyGraph.links[linkId];
        gfa << "L\t" <<
            link.segmentId0 << "\t+\t" <<
            link.segmentId1 << "\t+\t0M\n";
    }

}

#endif
