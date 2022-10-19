#include "mode3-Detangler.hpp"
#include "mode3.hpp"
using namespace shasta;
using namespace mode3;



Detangler::Detangler(const AssemblyGraph& assemblyGraph)
{
    createJourneys(assemblyGraph);
    createInitialClusters();
    cout << "The initial Detangler has " << clusters.size() << " clusters." << endl;
}




// To create the journeys, simply extract the segmentIds from the assemblyGraphJourneys.
void Detangler::createJourneys(const AssemblyGraph& assemblyGraph)
{
    const uint64_t journeyCount = assemblyGraph.assemblyGraphJourneys.size();

    journeys.clear();
    journeys.resize(journeyCount);
    for(uint64_t i=0; i<journeyCount; i++) {
        const span<const AssemblyGraphJourneyEntry> assemblyGraphJourney = assemblyGraph.assemblyGraphJourneys[i];
        Journey& journey = journeys[i];

        for(const AssemblyGraphJourneyEntry& assemblyGraphJourneyEntry: assemblyGraphJourney) {
            journey.push_back(Step(assemblyGraphJourneyEntry.segmentId));
        }
    }
}



// Initially, we create a Cluster for each segmentId.
void Detangler::createInitialClusters()
{

    // Loop over all oriented reads.
    const ReadId readCount = ReadId(journeys.size() / 2);
    for(ReadId readId=0; readId<readCount; readId++) {
        for(Strand strand=0; strand<2; strand++) {
            const OrientedReadId orientedReadId(readId, strand);

            // Get the Journey for this oriented read.
            Journey& journey = journeys[orientedReadId.getValue()];

            // Loop over Step(s) in this Journey.
            StepInfo stepInfo;
            stepInfo.orientedReadId = orientedReadId;
            for(uint64_t position=0; position<journey.size(); position++) {
                stepInfo.position = position;
                Step& step = journey[position];
                const uint64_t segmentId = step.segmentId;

                // Locate the Cluster corresponding to this segment,
                // creating it if necessary.
                ClusterContainer::iterator it = clusters.find(segmentId);
                if(it == clusters.end()) {
                    tie(it, ignore) = clusters.insert(make_pair(segmentId, std::list<Cluster>()));
                    it->second.push_back(Cluster(segmentId));
                }
                std::list<Cluster>& segmentClusters = it->second;

                // Sanity check: this segmentId must correspond to exactly one Cluster.
                SHASTA_ASSERT(segmentClusters.size() == 1);
                Cluster& cluster = segmentClusters.front();

                // Add this Step to the Cluster.
                cluster.steps.push_back(stepInfo);
                step.cluster = &cluster;
            }
        }
    }
}



// Find the next cluster reached by each oriented read in a given cluster.
void Detangler::findNextClusters(
    const Cluster* cluster,
    vector< pair<const Cluster*, vector<OrientedReadId> > >& nextClusters
    ) const
{
    // For each read, go forward one step.
    // This can be done faster.
    std::map<const Cluster*, vector<OrientedReadId> > nextClustersMap;
    for(const StepInfo& stepInfo: cluster->steps) {
        const OrientedReadId orientedReadId = stepInfo.orientedReadId;
        const uint64_t position = stepInfo.position;
        const Journey& journey = journeys[orientedReadId.getValue()];
        const uint64_t nextPosition = position + 1;
        if(nextPosition < journey.size()) {
            const Step& nextStep = journey[nextPosition];
            nextClustersMap[nextStep.cluster].push_back(orientedReadId);
        }
    };

    nextClusters.clear();
    copy(nextClustersMap.begin(), nextClustersMap.end(), back_inserter(nextClusters));

}



// Find the previous cluster reached by each oriented read in a given cluster.
void Detangler::findPreviousClusters(
    const Cluster* cluster,
    vector< pair<const Cluster*, vector<OrientedReadId> > >& previousClusters
    ) const
{
    // For each read, go backward one step.
    // This can be done faster.
    std::map<const Cluster*, vector<OrientedReadId> > previousClustersMap;
    for(const StepInfo& stepInfo: cluster->steps) {
        const OrientedReadId orientedReadId = stepInfo.orientedReadId;
        const uint64_t position = stepInfo.position;
        const Journey& journey = journeys[orientedReadId.getValue()];
        if(position > 0) {
            const uint64_t previousPosition = position - 1;
            const Step& previousStep = journey[previousPosition];
            previousClustersMap[previousStep.cluster].push_back(orientedReadId);
        }
    };

    previousClusters.clear();
    copy(previousClustersMap.begin(), previousClustersMap.end(), back_inserter(previousClusters));

}
