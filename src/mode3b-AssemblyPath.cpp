#include "mode3b-AssemblyPath.hpp"
#include "mode3b-PathFiller.hpp"
#include "mode3b-PathFinder.hpp"
#include "Assembler.hpp"
#include "MarkerInterval.hpp"
#include "SHASTA_ASSERT.hpp"
using namespace shasta;
using namespace mode3b;

#include <iostream.hpp>


AssemblyPath::AssemblyPath(
    const Assembler& assembler,
    MarkerGraphEdgeId startEdgeId,
    uint64_t direction  // 0 = forward, 1 = backward.
    ) :
    assembler(assembler)
{
    vector< pair<MarkerGraphEdgeId, MarkerGraphEdgePairInfo> > otherPrimaryEdges;
    PathFinder pathFinder(assembler, startEdgeId, direction, otherPrimaryEdges);

    // Create the primaryEdges and the steps.
    primaryEdges.push_back(startEdgeId);
    for(const auto& p: otherPrimaryEdges) {
        primaryEdges.push_back(p.first);
        steps.push_back(Step(p.second));
    }
    SHASTA_ASSERT(primaryEdges.size() == steps.size() + 1);

    // Fill in the secondary edges for each Step.
    for(uint64_t i=0; i<steps.size(); i++) {
        const MarkerGraphEdgeId edgeIdA = primaryEdges[i];
        const MarkerGraphEdgeId edgeIdB = primaryEdges[i+1];
        cout << "Filling in secondary edges between primary edges " <<
            edgeIdA << " " << edgeIdB << endl;
        Step& step = steps[i];
        ostream html(0);  // Causes output to be skipped.
        PathFiller pathFiller(assembler, edgeIdA, edgeIdB, html);
        step.secondaryEdges = pathFiller.secondaryEdges;
    }
}



AssemblyPath::Step::Step(const MarkerGraphEdgePairInfo& info) :
    info(info)
{}



void AssemblyPath::getSequence(vector<Base>& sequence) const
{
    sequence.clear();
    for(uint64_t i=0; /* Check later */ ; i++) {

        // Append the primary edge sequence.
        const MarkerGraphEdgeId edgeId = primaryEdges[i];
        const auto edgeSequence = assembler.markerGraph.edgeSequence[edgeId];
        copy(edgeSequence.begin(), edgeSequence.end(), back_inserter(sequence));

        // If this is the last primary edge, we are done.
        if(i == primaryEdges.size() - 1) {
            break;
        }

        // Append the secondary edge sequences.
        const Step& step = steps[i];
        for(const MarkerGraphEdgeId edgeId: step.secondaryEdges) {
            const auto edgeSequence = assembler.markerGraph.edgeSequence[edgeId];
            copy(edgeSequence.begin(), edgeSequence.end(), back_inserter(sequence));
        }
    }
}


void AssemblyPath::writeFasta(ostream& fasta) const
{
    vector<Base> sequence;
    getSequence(sequence);

    fasta << ">Path " << sequence.size() << "\n";
    copy(sequence.begin(), sequence.end(), ostream_iterator<Base>(fasta));
    fasta << "\n";
}
