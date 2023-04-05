#ifndef SHASTA_ALIGN4_HPP
#define SHASTA_ALIGN4_HPP

/*******************************************************************************

Marker alignment of two sequences markerSequence0 and markerSequence1,
each defined as a sequence of marker KmerId's.

We call x or y the index (or position or ordinal)
of a marker in markerSequence0 or markerSequence1 respectively, so:
    - markerSequence0[x] is the marker at position x of markerSequence0
    - markerSequence1[y] is the marker at position y of markerSequence1

The number of markers in markerSequence0 is nx
and the number of markers in markerSequence1 is ny.
For any two positions x and y the following hold:
0 <= x <= nx-1
0 <= y <= ny-1

The coordinates in the alignment matrix (x, y)
are represented with x along the horizontal axis and increasing toward the right,
and y along the vertical axis and increasing toward
the bottom. The alignment matrix element at position (x,y)
exists if markerSequence0[x]=markerSequence1[y] when
working with sequences of markers and if
featureSequence0[x]=featureSequence1[y] when working with sequences
of features.

We also use coordinates X and Y defined as:
X = x + y
Y = y + (nx - 1 - x)

It can be verified that:
0 <= X <= nx + ny - 2
0 <= Y <= nx + ny - 2
So the total number of distinct values of X and Y is nx + ny - 1.

X is a coordinate along the diagonal of the alignment matrix,
and Y is orthogonal to it and identifies the diagonal.
In (X,Y) coordinates the alignment matrix is a subset of
the square of size nx + ny -1. The alignment matrix is
rotated by 45 degrees relative to this square.

The inverse transformation is
x = (X - Y + nx - 1) / 2
y = (X + Y - nx + 1) / 2

We use a sparse representation of the alignment matrix
in which non-zero alignment matrix entries are stored
organized by cell in a rectangular arrangement if cells
of size (deltaX, deltaY) in (X,Y) space.

*******************************************************************************/

#include "MemoryMappedAllocator.hpp"
#include "shastaTypes.hpp"
#include "span.hpp"

#include "array.hpp"
#include <limits>
#include <unordered_map>
#include "utility.hpp"
#include "vector.hpp"

namespace shasta {
    class Alignment;
    class AlignmentInfo;
    class CompressedMarker;
    class PngImage;

    namespace Align4 {
        class Aligner;
        class MatrixEntry;
        class Options;

        // This is used to store (x,y), (X,Y), or (iX, iY).
        using Coordinates = pair<uint32_t, uint32_t>;

        // When converting an arbitrary (X,Y) to (x,y)
        // we can end up with negative values.
        using SignedCoordinates = pair<uint32_t, uint32_t>;

        // Compute the alignment.
        // The KmerIds are the KmerIds for the two reads, in position order.
        // The sorted markers are pairs(KmerId, ordinal) sorted by KmerId.
        void align(
            const array< span<const KmerId>, 2>& kmerIds,
            const array<span< const pair<KmerId, uint32_t> >, 2> sortedMarkers,
            const Align4::Options&,
            MemoryMapped::ByteAllocator&,
            Alignment&,
            AlignmentInfo&,
            bool debug);
    }

    namespace MemoryMapped {
        class ByteAllocator;
    }

}



class shasta::Align4::Options {
public:
    uint64_t deltaX;
    uint64_t deltaY;
    uint64_t minEntryCountPerCell;
    uint64_t maxDistanceFromBoundary;
    uint64_t minAlignedMarkerCount;
    double minAlignedFraction;
    uint64_t maxSkip;
    uint64_t maxDrift;
    uint64_t maxTrim;
    uint64_t maxBand;
    int64_t matchScore;
    int64_t mismatchScore;
    int64_t gapScore;
};



class shasta::Align4::MatrixEntry {
public:
    Coordinates xy;
    MatrixEntry() {}
    MatrixEntry(const Coordinates& xy) : xy(xy) {}
};



class shasta::Align4::Aligner {
public:

    // The constructor does all the work.
    // The kmerIds are in position orders.
    // The sorted markers are pairs(KmerId, ordinal) sorted by KmerId.
    Aligner(
        const array< span<const KmerId>, 2>& kmerIds,
        const array<span< const pair<KmerId, uint32_t> >, 2> sortedMarkers,
        const Options&,
        MemoryMapped::ByteAllocator&,
        Alignment&,
        AlignmentInfo&,
        bool debug);

private:

    // Number of markers (not features) in the two sequences being aligned.
    uint32_t nx;
    uint32_t ny;

    // Cell sizes in the X and Y direction.
    uint32_t deltaX;
    uint32_t deltaY;

    // Alignment scores.
    int32_t matchScore = 6;
    int32_t mismatchScore = -1;
    int32_t gapScore = -1;


    // Vector of markers for each sequence.
    // array<vector<KmerId>, 2> markers;

    // The alignment matrix, in a sparse representation organized by
    // cells in (X,Y) space.
    // For each iY, we store pairs(iX, xy) sorted by iX.
    // Even though this requires sorting, it is more efficient
    // than using a hash table, due to the better memory access pattern.
    using AlignmentMatrixEntry = pair<uint32_t, Coordinates>; // (iX, xy)
    using AlignmentMatrixAllocator = MemoryMapped::Allocator<AlignmentMatrixEntry>;
    using AlignmentMatrixEntryVector = vector<AlignmentMatrixEntry, AlignmentMatrixAllocator>; // For one iY
    using AlignmentMatrix = vector<AlignmentMatrixEntryVector>; // Indexed by iY.
    AlignmentMatrix alignmentMatrix;
    void createAlignmentMatrix(const array<span< const pair<KmerId, uint32_t> >, 2> sortedMarkers);
    void writeAlignmentMatrixCsv(const string& fileName) const;
    void writeAlignmentMatrixPng(
        const string& fileName,
        uint64_t maxDistanceFromBoundary) const;
    void writeCheckerboard(
        PngImage&,
        uint64_t maxDistanceFromBoundary) const;



    // Cells in (X,Y) space.
    // Stored similarly to alignmentMatrix above: for each iY,
    // we store pairs (iX, Cell) sorted by iX.
    class Cell {
    public:
        uint8_t isNearLeftOrTop         : 1;
        uint8_t isNearRightOrBottom     : 1;
        uint8_t isForwardAccessible     : 1;
        uint8_t isBackwardAccessible    : 1;
        Cell()
        {
            *reinterpret_cast<uint8_t*>(this) = 0;
        }
        bool isActive() const
        {
            return (isForwardAccessible == 1) and (isBackwardAccessible == 1);
        }
    };
    static_assert(sizeof(Cell)==1, "Unexpected size of Align5::Aligner::Cell.");
    vector< vector< pair<uint32_t, Cell> > > cells;
    void createCells(
        uint64_t minEntryCountPerCell,
        uint64_t maxDistanceFromBoundary);
    void writeCellsCsv(const string& fileName) const;
    void writeCellsPng(const string& fileName) const;

    // Return the distance of a cell from the left boundary
    // of the alignment matrix, or 0 if the cell
    // is partially or entirely to the left of that boundary.
    uint32_t cellDistanceFromLeft(const Coordinates& iXY) const;

    // Return the distance of a cell from the right boundary
    // of the alignment matrix, or 0 if the cell
    // is partially or entirely to the right of that boundary.
    uint32_t cellDistanceFromRight(const Coordinates& iXY) const;

    // Return the distance of a cell from the top boundary
    // of the alignment matrix, or 0 if the cell
    // is partially or entirely above that boundary.
    uint32_t cellDistanceFromTop(const Coordinates& iXY) const;

    // Return the distance of a cell from the bottom boundary
    // of the alignment matrix, or 0 if the cell
    // is partially or entirely below that boundary.
    uint32_t cellDistanceFromBottom(const Coordinates& iXY) const;

    // Find a cell with given (iX,iY).
    Cell* findCell(const Coordinates& iXY);
    const Cell* findCell(const Coordinates& iXY) const;



    // Coordinate transformations.

    // Return (X,Y) given (x,y).
    Coordinates getXY(Coordinates xy) const;

    // Return (iX,iY) given (X,Y).
    Coordinates getCellIndexesFromXY(Coordinates XY) const
    {
        return Coordinates(
            XY.first  / deltaX,
            XY.second / deltaY
            );
    }

    // Return (iX,iY) given (x,y).
    Coordinates getCellIndexesFromxy(Coordinates xy) const
    {
        const Coordinates XY = getXY(xy);
        return getCellIndexesFromXY(XY);
    }


    // Convert an arbitrary (X,Y) to (x,y).
    // If the point is outside the alignment matrix,
    // we can end up with negative values.
    SignedCoordinates getxy(Coordinates XY) const;

    // Searches in cell space.
    void forwardSearch();
    void backwardSearch();



    // Group active cells in connected component.
    // Each connected component also generates a diagonal range
    // that could be used as band to compute an alignment.
    vector< vector<Coordinates> > activeCellsConnectedComponents;
    void findActiveCellsConnectedComponents();

    // Compute a banded alignment for each connected component of
    // active cells. Return the ones that match requirements on
    // minAlignedMarkerCount, minAlignedFraction, maxSkip, maxDrift, maxTrim.
    void computeBandedAlignments(
        const array<span<const KmerId>, 2>& kmerIds,
        uint64_t minAlignedMarkerCount,
        double minAlignedFraction,
        uint64_t maxSkip,
        uint64_t maxDrift,
        uint64_t maxTrim,
        uint64_t maxBand,
        vector< pair<Alignment, AlignmentInfo> >&,
        bool debug) const;

    // Compute a banded alignment with a given band.
    bool computeBandedAlignment(
        const array<span<const KmerId>, 2>& kmerIds,
        int32_t bandMin,
        int32_t bandMax,
        Alignment&,
        AlignmentInfo&,
        bool debug) const;

    MemoryMapped::ByteAllocator& byteAllocator;


};



#endif
