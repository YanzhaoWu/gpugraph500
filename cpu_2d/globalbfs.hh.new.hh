#ifndef GLOBALBFS_HH
#define GLOBALBFS_HH

#include "distmatrix2d.hh"
#include "comp_opt.h"
#include "bitlevelfunctions.h"
#include <vector>
#include <cstdio>
#include <assert.h>
#include "vreduce.hpp"
#include <ctgmath>
#include <string.h>
#include <functional>
#include <stdlib.h>
#include <algorithm>
#ifdef _SCOREP_USER_INSTRUMENTATION
#include "scorep/SCOREP_User.h"
#endif
#include "config.h"
#include "constants.hh"

#if _OPENMP
#include <parallel/algorithm>
#endif

#ifdef INSTRUMENTED
#include <unistd.h>
#include <chrono>
using namespace std::chrono;
#endif

#ifdef _COMPRESSION
#include "compression/compression.hh"
#include "compression/types_compression.h"
#endif


using std::function;
using std::min;
using std::bind;
using std::vector;
using std::string;
using std::is_sorted;
using namespace std::placeholders;

/*
 * This classs implements a distributed level synchronus BFS on global scale.
 */
template <typename Derived,
          typename FQ_T,  // Queue Type
          typename MType, // Bitmap mask
          typename STORE> // Storage of Matrix
class GlobalBFS
{
private:
    MPI_Comm row_comm, col_comm;
    //int rank;
    int err, err1, err2;
    // sending node column slice, startvtx, size
    vector <typename STORE::fold_prop> fold_fq_props;

    void allReduceBitCompressed(typename STORE::vertexType *&owen, typename STORE::vertexType *tmp,
                                MType *owenmap, MType *tmpmap);
    void allReduceBitCompressed_dup(typename STORE::vertexType *owen, typename STORE::vertexType *tmp,
                                MType *owenmap, MType *tmpmap, typename STORE::vertexType **outPredecessorQ);

protected:
    const STORE &store;
    typename STORE::vertexType *predecessor;
    MPI_Datatype fq_tp_type; //Frontier Queue Type (Usually Integger 64-bit. Converting this to unsigned would save time in compression calls. see allBitCompressedBitmap() banner)
#ifdef _COMPRESSION
    MPI_Datatype fq_tp_typeC; //Compressed FQ (usually Unisigned 32-bit)
#endif
    MPI_Datatype bm_type;    // Bitmap Type
    // FQ_T*  __restrict__ fq_64; - conflicts with void* ref
    FQ_T *fq_64; // Frontier Queue (aka. Current Queue, CQ, etc). Beware: allocated using cudaMemAlloc() (uses pinned mem)
    std::size_t fq_64_length;
    MType *owenmask;
    MType *tmpmask;
    std::size_t mask_size;

    // Functions that have to be implemented by the children
    // void reduce_fq_out(FQ_T* startaddr, long insize)=0;  //Global Reducer of the local outgoing frontier queues. Have to be implemented by the children.
    // void getOutgoingFQ(FQ_T* &startaddr, vertexTypee& outsize)=0;
    // void setModOutgoingFQ(FQ_T* startaddr, long insize)=0; //startaddr: 0, self modification
    // void getOutgoingFQ(vertexTypee globalstart, vertexTypee size, FQ_T* &startaddr, vertexTypee& outsize)=0;
    // void setIncommingFQ(vertexTypee globalstart, vertexTypee size, FQ_T* startaddr, vertexTypee& insize_max)=0;
    // bool istheresomethingnew()=0;           //to detect if finished
    // void setStartVertex(const vertexTypee start)=0;
    // void runLocalBFS()=0; // For accelerators with own memory
    void getBackPredecessor(); // expected to be used after the application finished
    void getBackOutqueue();
    void setBackInqueue();
    void generatOwenMask();

    // bfsMemCpy() - Uses the device memory calls to copy the MPI buffer. This buffer is created on the device (CPU, CUDA, OPENGL, etc)
    // using a malloc will cause a crash if the app is not in CPU mode. Implemented on the clidren class where also, the
    // buffer is created.
    void bfsMemCpy(FQ_T *&dst, FQ_T *src, size_t size);

public:
    /**
     * Constructor & destructor declaration
     */
    GlobalBFS(STORE &_store);
    ~GlobalBFS();
    typename STORE::vertexType *getPredecessor();
#ifdef _COMPRESSION
    typedef Compression<FQ_T, compressionType> CompressionClassT;
#endif
#ifdef INSTRUMENTED
#ifdef _COMPRESSION
    void runBFS(typename STORE::vertexType startVertex, double &lexp, double &lqueue, double &rowcom, double &colcom,
                double &predlistred, const CompressionClassT &bitmapSchema, const CompressionClassT &predecessorListbitmapSchema);
#else
    void runBFS(typename STORE::vertexType startVertex, double &lexp, double &lqueue, double &rowcom, double &colcom,
                double &predlistred);
#endif
#else
#ifdef _COMPRESSION
    void runBFS(typename STORE::vertexType startVertex, const CompressionClassT &bitmapSchema, const CompressionClassT &predecessorListbitmapSchema);
#else
    void runBFS(typename STORE::vertexType startVertex);
#endif
#endif

};

/**
 *
 *
 *
 *
 * GlobalBFS
 */
template<typename Derived, typename FQ_T, typename MType, typename STORE>
GlobalBFS<Derived, FQ_T, MType, STORE>::GlobalBFS(STORE &_store) : store(_store)
{
    int64_t mtypesize = sizeof(MType) << 3; // * 2^3
    int64_t local_column = store.getLocalColumnID(), local_row = store.getLocalRowID();

    //MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    // Split communicator into row and column communicator
    // Split by row, rank by column
    MPI_Comm_split(MPI_COMM_WORLD, local_row, local_column, &row_comm);
    // Split by column, rank by row
    MPI_Comm_split(MPI_COMM_WORLD, local_column, local_row, &col_comm);
    fold_fq_props = store.getFoldProperties();
    mask_size = (store.getLocColLength() / mtypesize) + ((store.getLocColLength() % mtypesize > 0) ? 1ULL : 0ULL);
    owenmask = new MType[mask_size];
    tmpmask = new MType[mask_size];
}

/**
 *
 *
 *
 *
 * ~GlobalBFS
 */
template<typename Derived, typename FQ_T, typename MType, typename STORE>
GlobalBFS<Derived, FQ_T, MType, STORE>::~GlobalBFS()
{
    delete[] owenmask;
    delete[] tmpmask;
}

template<typename Derived, typename FQ_T, typename MType, typename STORE>
void GlobalBFS<Derived, FQ_T, MType, STORE>::getBackPredecessor() { }

template<typename Derived, typename FQ_T, typename MType, typename STORE>
void GlobalBFS<Derived, FQ_T, MType, STORE>::getBackOutqueue() { }

template<typename Derived, typename FQ_T, typename MType, typename STORE>
void GlobalBFS<Derived, FQ_T, MType, STORE>::setBackInqueue() { }

/**
 *
 *
 *
 *
 * generatOwenMask(). Generates a map of the vertex with predecessor
 */
template<typename Derived, typename FQ_T, typename MType, typename STORE>
void GlobalBFS<Derived, FQ_T, MType, STORE>::generatOwenMask()
{
    const std::size_t mtypesize = sizeof(MType) << 3;
    const uint64_t store_col_length = store.getLocColLength();


#ifdef _OPENMP
        #pragma omp for schedule (guided, OMP_CHUNK)
#endif
        for (std::size_t i = 0ULL; i < mask_size; ++i)
        {
            MType tmp = 0U;
            const uint64_t iindex = i * mtypesize;
            const std::size_t mask_word_end = std::min(mtypesize, store_col_length - iindex);
            for (std::size_t j = 0LL; j < mask_word_end; ++j)
            {
                const std::size_t jindex = iindex + j;
                if (predecessor[jindex] != -1)
                {
                    tmp |= 1U << j;
                }
            }
            owenmask[i] = tmp;
        }
}

/**
 *
 *
 *
 *
 * getPredecessor()
 *
 */
template<typename Derived, typename FQ_T, typename MType, typename STORE>
typename STORE::vertexType *GlobalBFS<Derived, FQ_T, MType, STORE>::getPredecessor()
{
    return predecessor;
}

/**
 *
 *
 *
 *
 *
 * allReduceBitCompressed()
 * Bitmap compression on predecessor reduction
 *
 */

template<typename Derived, typename FQ_T, typename MType, typename STORE>
void GlobalBFS<Derived, FQ_T, MType, STORE>::allReduceBitCompressed(typename STORE::vertexType *&predecessorQ,
        typename STORE::vertexType *frontierQ, MType *predecessorQmap,
        MType *frontierQmap)
{
    static int colCommunicatorSize, colCommunicatorRank, rowCommunicatorSize, rowCommunicatorRank;
    MPI_Comm_size(col_comm, &colCommunicatorSize);
    MPI_Comm_rank(col_comm, &colCommunicatorRank);

    MPI_Comm_size(row_comm, &rowCommunicatorSize);
    MPI_Comm_rank(row_comm, &rowCommunicatorRank);
    /**
     *
     *
     * calculates communicator distribution for p2p transmissions (even-to-odd ranks and viceversa)
     *
     * e.g.
     * scale 3 ---> p=9nodes (scale^2), half_p=4,3 (hprow,hpcol); ilog2_hpcol=2; residuum=1 (hpcol (3) - 2^ilog2_hpcol (2) = 1)
     * scale 4 ---> p=16nodes (scale^2), half_p=8,8 (hprow,hpcol); ilog2_hpcol=3; residuum=0 (hpcol (8) - 2^ilog2_hpcol (8) = 0)
     * scale 5 ---> p=25nodes (scale^2), half_p=13,12 (hprow,hpcol); ilog2_hpcol=3 residuum=4 (hpcol (12) - 2^ilog2_hpcol (8) = 4)
     *
     * the matrix is symmetric (scale x scale). however, may not be even: (scale x scale) % 2 (e.g scale 5 or scale 9)
     * row and columns rank partitions are configurable at the constructor (MPI_comm_split)
     *
     * bitwise mult && div: x>>y == x/(2^y); x<<y == x*(2^y)
     *
     * If the general VertexType is changed from int64_t to uint64_t (better for compression. would remove 2 full buffer convertions per compression call)
     * the int32_t type in this func. should also be changed to unsigned 64-bit. (uint64_t)
     *
     * the previous change is not possible yet since the NULL (starting) vertex uses a '-1' label
     *
     * the OpenMP buffer initializations, fix each buffer to a fixed core/thread (for the case of OMP only). this avoids the same data
     * to be treated by a different thread (OMP case only) and thus, a possible && expensive L1 cache miss.
     */

    MPI_Status status;
    const int32_t psize = static_cast<int32_t>(mask_size);
    const int32_t mtypesize = sizeof(MType) << 3; // * 8
    //step 1
    const int32_t intLdSize = ilogbf(static_cast<float>(colCommunicatorSize)); //integer log_2 of size
    const int32_t power2intLdSize = 1 << intLdSize; // 2^n
    const int32_t residuum = colCommunicatorSize - power2intLdSize;
    const int32_t twoTimesResiduum = residuum << 1;

    /**
     *
     * created at compiletime. in most of the cases faster than inline funcs
     */

    const function <int32_t(int32_t)> newRank = [&residuum](int32_t oldr)
    {
        return (oldr < (residuum << 1)) ? (oldr >> 1) : oldr - residuum;
    };
    const function <int32_t(int32_t)> oldRank = [&residuum](uint32_t newr)
    {
        return (newr < residuum) ? (newr << 1) : newr + residuum;
    };
    const int32_t vrank = newRank(colCommunicatorRank);

    /**
     *
     *
     * manage residuums.
     */

    if (colCommunicatorRank < twoTimesResiduum)
    {
        if ((colCommunicatorRank & 1) == 0)   // even
        {
            MPI_Sendrecv(predecessorQmap, psize, bm_type, colCommunicatorRank + 1, 0, frontierQmap, psize, bm_type, colCommunicatorRank + 1, 0,
                         col_comm, &status);

            for (int32_t i = 0; i < psize; ++i)
            {
                frontierQmap[i] &= ~predecessorQmap[i];
                predecessorQmap[i] |= frontierQmap[i];
            }
            MPI_Recv(frontierQ, store.getLocColLength(), fq_tp_type, colCommunicatorRank + 1, 1, col_comm, &status);
            // set recived elements where the bit maps indicate it
            int32_t p = 0;
            for (int32_t i = 0; i < psize; ++i)
            {
                MType frontierQm = frontierQmap[i];
                const int32_t size = i * mtypesize;
                while (frontierQm != 0U)
                {
                    int32_t last = ffsl(frontierQm) - 1;
                    predecessorQ[size + last] = frontierQ[p];
                    ++p;
                    frontierQm ^= (1U << last);
                }
            }
        }
        else     // odd
        {
            MPI_Sendrecv(predecessorQmap, psize, bm_type, colCommunicatorRank - 1, 0, frontierQmap, psize, bm_type, colCommunicatorRank - 1, 0,
                         col_comm, &status);

            for (int32_t i = 0; i < psize; ++i)
            {
                frontierQmap[i] = ~frontierQmap[i] & predecessorQmap[i];
            }
            int32_t p = 0;
            for (int32_t i = 0; i < psize; ++i)
            {
                MType frontierQm = frontierQmap[i];
                const int32_t size = i * mtypesize;
                while (frontierQm != 0U)
                {
                    const int32_t last = ffsl(frontierQm) - 1;
                    frontierQ[p] = predecessorQ[size + last];
                    ++p;
                    frontierQm ^= (1 << last);
                }
            }
            MPI_Send(frontierQ, p, fq_tp_type, colCommunicatorRank - 1, 1, col_comm);
        }
    }

    /**
     *
     *
     * general communication case
     */

    if ((((colCommunicatorRank & 1) == 0) &&
        (colCommunicatorRank < twoTimesResiduum)) || (colCommunicatorRank >= twoTimesResiduum))
    {
        int32_t ssize, offset;
        ssize = psize;
        offset = 0;
        // intLdSize: 4 iteractions (scale 22, 16 gpus)
        for (int32_t it = 0; it < intLdSize; ++it)
        {
            const int32_t lowers = ssize >> 1; //lower slice size
            const int32_t uppers = ssize - lowers; //upper slice size
            const int32_t size = lowers * mtypesize;
            const int32_t orankEven = oldRank((vrank + (1 << it)) & (power2intLdSize - 1));
            const int32_t orankOdd = oldRank((power2intLdSize + vrank - (1 << it)) & (power2intLdSize - 1));
            const int32_t twoTimesIterator = it << 1;
            const int32_t iterator2 = twoTimesIterator + 2;
            const int32_t iterator3 = twoTimesIterator + 3;

            if (((vrank >> it) & 1) == 0)  // even
            {
                //Transmission of the the bitmap
                MPI_Sendrecv(predecessorQmap + offset, ssize, bm_type, orankEven, iterator2,
                             frontierQmap + offset, ssize, bm_type, orankEven, iterator2,
                             col_comm, &status);

                // lowers: ~65k iteractions per MPI node (scale 22, 16 gpus)
#ifdef _OPENMP
                #pragma omp parallel for
#endif
                for (int32_t i = 0; i < lowers; ++i)
                {
                    const int32_t iOffset = i + offset;
                    frontierQmap[iOffset] &= ~predecessorQmap[iOffset];
                    predecessorQmap[iOffset] |= frontierQmap[iOffset];
                }

                // lowers: ~65k iteractions per MPI node (scale 22, 16 gpus)
#ifdef _OPENMP
                #pragma omp parallel for
#endif
                for (int32_t i = lowers; i < ssize; ++i)
                {
                    const int32_t iOffset = i + offset;
                    frontierQmap[iOffset] = (~frontierQmap[iOffset]) & predecessorQmap[iOffset];
                }

                //Generation of foreign updates
                // uppers: ~65k iteractions per MPI node (scale 22, 16 gpus)
                int32_t p = 0;
                for (int32_t i = 0; i < uppers; ++i)
                {
                    const int32_t iOffset = i + offset;
                    const int32_t iOffsetLowers = iOffset + lowers;
                    const int32_t index = iOffsetLowers * mtypesize;
                    MType frontierQm = frontierQmap[iOffsetLowers];
                    while (frontierQm != 0U)
                    {
                        int32_t last = ffsl(frontierQm) - 1;
                        frontierQ[size + p] = predecessorQ[index + last];
                        ++p;
                        frontierQm ^= (1U << last);
                    }
                }
                //Transmission of updates
                MPI_Sendrecv(frontierQ + size, p, fq_tp_type,
                             orankEven, iterator3,
                             frontierQ, size, fq_tp_type,
                             orankEven, iterator3,
                             col_comm, &status);

                //Updates for own data
                p = 0;
                // lowers: ~65k iteractions per MPI node (scale 22, 16 gpus)
                for (int32_t i = 0; i < lowers; ++i)
                {
                    const int32_t iOffset = i + offset;
                    const int32_t index = iOffset * mtypesize;
                    MType frontierQm = frontierQmap[iOffset];
                    while (frontierQm != 0U)
                    {
                        int32_t last = ffsl(frontierQm) - 1;
                        predecessorQ[index + last] = frontierQ[p];
                        ++p;
                        frontierQm ^= (1U << last);
                    }
                }
                ssize = lowers;
//std::cout << "c,r: " << colCommunicatorRank << ","<< rowCommunicatorRank<< " - lower predecessor created here.\n";

            }
            else     // odd
            {
                //Transmission of the the bitmap
                MPI_Sendrecv(predecessorQmap + offset, ssize, bm_type,
                             orankOdd, iterator2,
                             frontierQmap + offset, ssize, bm_type,
                             orankOdd, iterator2,
                             col_comm, &status);

                // lowers: ~65k iteractions per MPI node (scale 22, 16 gpus)
#ifdef _OPENMP
                #pragma omp parallel for
#endif
                for (int32_t i = 0; i < lowers; ++i)
                {
                    const int32_t iOffset = i + offset;
                    frontierQmap[iOffset] = (~frontierQmap[iOffset]) & predecessorQmap[iOffset];
                }

                // lowers: ~65k iteractions per MPI node (scale 22, 16 gpus)
#ifdef _OPENMP
                #pragma omp parallel for
#endif
                for (int32_t i = lowers; i < ssize; ++i)
                {
                    const int32_t iOffset = i + offset;
                    frontierQmap[iOffset] &= ~predecessorQmap[iOffset];
                    predecessorQmap[iOffset] |= frontierQmap[iOffset];
                }

                // Generation of foreign updates
                // inner p: ~50k iteractions per MPI node (scale 22, 16 gpus)
                int32_t p = 0;
                for (int32_t i = 0; i < lowers; ++i)
                {
                    const int32_t iOffset = i + offset;
                    const int32_t iOffsetMtype = iOffset * mtypesize;
                    MType frontierQm = frontierQmap[iOffset];
                    while (frontierQm != 0U)
                    {
                        const int32_t last = ffsl(frontierQm) - 1;
                        frontierQ[p] = predecessorQ[iOffsetMtype + last];
                        ++p;
                        frontierQm ^= (1U << last);
                    }
                }

                //Transmission of updates
                MPI_Sendrecv(frontierQ, p, fq_tp_type,
                             orankOdd, iterator3,
                             frontierQ + size, uppers * mtypesize, fq_tp_type,
                             orankOdd, iterator3,
                             col_comm, &status);

                //Updates for own data
                // inner p: ~50k iteractions per MPI node (scale 22, 16 gpus)
                p = 0;
                for (int32_t i = 0; i < uppers; ++i)
                {
                    const int32_t iOffset = offset + lowers + i;
                    const int32_t lindex = iOffset * mtypesize;
                    MType frontierQm = frontierQmap[iOffset];
                    while (frontierQm != 0U)
                    {
                        const int32_t last = ffsl(frontierQm) - 1;
                        predecessorQ[lindex + last] = frontierQ[p + size];
                        ++p;
                        frontierQm ^= (1U << last);
                    }
                }
                offset += lowers;
                ssize = uppers;
//std::cout << "c,r: " << colCommunicatorRank << ","<< rowCommunicatorRank<< " - lower predecessor created here.\n";

            }
        }
    }

}


/**
 *
 *
 *
 *
 *
 * allReduceBitCompressed_dup()
 * Bitmap compression on predecessor reduction
 *
 */

template<typename Derived, typename FQ_T, typename MType, typename STORE>
void GlobalBFS<Derived, FQ_T, MType, STORE>::allReduceBitCompressed_dup(typename STORE::vertexType *predecessorQ,
        typename STORE::vertexType *frontierQ, MType *predecessorQmap,
        MType *frontierQmap, typename STORE::vertexType **outPredecessorQ)
{
    static int colCommunicatorSize, colCommunicatorRank, rowCommunicatorSize, rowCommunicatorRank;
    MPI_Comm_size(col_comm, &colCommunicatorSize);
    MPI_Comm_rank(col_comm, &colCommunicatorRank);

    MPI_Comm_size(row_comm, &rowCommunicatorSize);
    MPI_Comm_rank(row_comm, &rowCommunicatorRank);
    /**
     *
     *
     * calculates communicator distribution for p2p transmissions (even-to-odd ranks and viceversa)
     *
     * e.g.
     * scale 3 ---> p=9nodes (scale^2), half_p=4,3 (hprow,hpcol); ilog2_hpcol=2; residuum=1 (hpcol (3) - 2^ilog2_hpcol (2) = 1)
     * scale 4 ---> p=16nodes (scale^2), half_p=8,8 (hprow,hpcol); ilog2_hpcol=3; residuum=0 (hpcol (8) - 2^ilog2_hpcol (8) = 0)
     * scale 5 ---> p=25nodes (scale^2), half_p=13,12 (hprow,hpcol); ilog2_hpcol=3 residuum=4 (hpcol (12) - 2^ilog2_hpcol (8) = 4)
     *
     * the matrix is symmetric (scale x scale). however, may not be even: (scale x scale) % 2 (e.g scale 5 or scale 9)
     * row and columns rank partitions are configurable at the constructor (MPI_comm_split)
     *
     * bitwise mult && div: x>>y == x/(2^y); x<<y == x*(2^y)
     *
     * If the general VertexType is changed from int64_t to uint64_t (better for compression. would remove 2 full buffer convertions per compression call)
     * the int32_t type in this func. should also be changed to unsigned 64-bit. (uint64_t)
     *
     * the previous change is not possible yet since the NULL (starting) vertex uses a '-1' label
     *
     * the OpenMP buffer initializations, fix each buffer to a fixed core/thread (for the case of OMP only). this avoids the same data
     * to be treated by a different thread (OMP case only) and thus, a possible && expensive L1 cache miss.
     */

    MPI_Status status;
    const int32_t psize = static_cast<int32_t>(mask_size);
    const int32_t mtypesize = sizeof(MType) << 3; // * 8
    //step 1
    const int32_t intLdSize = ilogbf(static_cast<float>(colCommunicatorSize)); //integer log_2 of size
    const int32_t power2intLdSize = 1 << intLdSize; // 2^n
    const int32_t residuum = colCommunicatorSize - power2intLdSize;
    const int32_t twoTimesResiduum = residuum << 1;

    /**
     *
     * created at compiletime. in most of the cases faster than inline funcs
     */
    const function <int32_t(int32_t)> newRank = [&residuum](int32_t oldr)
    {
        return (oldr < (residuum << 1)) ? (oldr >> 1) : oldr - residuum;
    };
    const function <int32_t(int32_t)> oldRank = [&residuum](uint32_t newr)
    {
        return (newr < residuum) ? (newr << 1) : newr + residuum;
    };
    const int32_t vrank = newRank(colCommunicatorRank);

    /**
     *
     *
     * manage residuums.
     */
/*
    if (colCommunicatorRank < twoTimesResiduum)
    {
        if ((colCommunicatorRank & 1) == 0)   // even
        {
            MPI_Sendrecv(predecessorQmap, psize, bm_type, colCommunicatorRank + 1, 0, frontierQmap, psize, bm_type, colCommunicatorRank + 1, 0,
                         col_comm, &status);

            for (int32_t i = 0; i < psize; ++i)
            {
                frontierQmap[i] &= ~predecessorQmap[i];
                predecessorQmap[i] |= frontierQmap[i];
            }
            MPI_Recv(frontierQ, store.getLocColLength(), fq_tp_type, colCommunicatorRank + 1, 1, col_comm, &status);
            // set recived elements where the bit maps indicate it
            int32_t p = 0;
            for (int32_t i = 0; i < psize; ++i)
            {
                MType frontierQm = frontierQmap[i];
                const int32_t size = i * mtypesize;
                while (frontierQm != 0U)
                {
                    int32_t last = ffsl(frontierQm) - 1;
                    predecessorQ[size + last] = frontierQ[p];
                    ++p;
                    frontierQm ^= (1U << last);
                }
            }
        }
        else     // odd
        {
            MPI_Sendrecv(predecessorQmap, psize, bm_type, colCommunicatorRank - 1, 0, frontierQmap, psize, bm_type, colCommunicatorRank - 1, 0,
                         col_comm, &status);

            for (int32_t i = 0; i < psize; ++i)
            {
                frontierQmap[i] = ~frontierQmap[i] & predecessorQmap[i];
            }
            int32_t p = 0;
            for (int32_t i = 0; i < psize; ++i)
            {
                MType frontierQm = frontierQmap[i];
                const int32_t size = i * mtypesize;
                while (frontierQm != 0U)
                {
                    const int32_t last = ffsl(frontierQm) - 1;
                    frontierQ[p] = predecessorQ[size + last];
                    ++p;
                    frontierQm ^= (1 << last);
                }
            }
            MPI_Send(frontierQ, p, fq_tp_type, colCommunicatorRank - 1, 1, col_comm);
        }
    }
*/
    /**
     *
     *
     * general communication case
     */
//    if ((((colCommunicatorRank & 1) == 0) &&
//        (colCommunicatorRank < twoTimesResiduum)) || (colCommunicatorRank >= twoTimesResiduum))
//    {

//    if ((colCommunicatorRank & 1) == 0) // even
//    {
        int32_t ssize, offset;
        ssize = psize;
        const int bsize = psize * colCommunicatorSize;
        offset = 0;

  
        const int64_t boffset = psize * colCommunicatorRank;  
std::cout<<"size: "<<bsize<<" offset: "<<boffset<<" c,r: "<< colCommunicatorRank<<", "<< rowCommunicatorRank<< "\n ";

	const int64_t fullPQSize = psize * mtypesize * colCommunicatorSize;
        const int64_t fullPQOffset = psize * mtypesize * colCommunicatorRank; 
        MType *fullPredecessorQmap = new MType[bsize];
	std::memcpy(fullPredecessorQmap + boffset, predecessorQmap, psize); 
        MType *fullFrontierQmap = new MType[bsize];
        std::memcpy(fullFrontierQmap + boffset, frontierQmap, psize);


        *outPredecessorQ = new FQ_T[fullPQSize];
        std::memcpy(*outPredecessorQ + fullPQOffset, predecessorQ, psize * mtypesize );

//        // intLdSize: 4 iteractions (scale 22, 16 gpus)
//        for (int32_t it = 0; it < intLdSize; ++it)
//        {
            const int32_t lowers = ssize >> 1; //lower slice size
            const int32_t uppers = ssize - lowers; //upper slice size
            const int32_t size = lowers * mtypesize;
            //const int32_t orankEven = oldRank((vrank + (1 << it)) & (power2intLdSize - 1));
            //const int32_t orankOdd = oldRank((power2intLdSize + vrank - (1 << it)) & (power2intLdSize - 1));
            //const int32_t twoTimesIterator = it << 1;
            //const int32_t iterator2 = twoTimesIterator + 2;
            //const int32_t iterator3 = twoTimesIterator + 3;


                //Transmission of the the bitmap
/*
                MPI_Sendrecv(predecessorQmap + offset, ssize, bm_type, orankEven, iterator2,
                             frontierQmap + offset, ssize, bm_type, orankEven, iterator2,
                             col_comm, &status);
*/

		MPI_Allgather(MPI_IN_PLACE, 0, MPI_DATATYPE_NULL, 
			fullPredecessorQmap, psize, bm_type, col_comm);	

                MPI_Allgather(MPI_IN_PLACE, 0, MPI_DATATYPE_NULL,
                        fullFrontierQmap, psize, bm_type, col_comm);


                // lowers: ~65k iteractions per MPI node (scale 22, 16 gpus)
#ifdef _OPENMP
                #pragma omp parallel for
#endif
                for (int64_t i = 0LL; i < bsize; ++i)
                {
                    frontierQmap[i] &= ~predecessorQmap[i];
                    predecessorQmap[i] |= frontierQmap[i];
                }
/*
                // lowers: ~65k iteractions per MPI node (scale 22, 16 gpus)
#ifdef _OPENMP
                #pragma omp parallel for
#endif
                for (int32_t i = lowers; i < ssize; ++i)
                {
                    frontierQmap[i] &= predecessorQmap[i];
                }
*/
/*
                //Generation of foreign updates
                // uppers: ~65k iteractions per MPI node (scale 22, 16 gpus)
                int32_t p = 0;
                for (int32_t i = 0; i < uppers; ++i)
                {
                    const int32_t iOffset = i + offset;
                    const int32_t iOffsetLowers = iOffset + lowers;
                    const int32_t index = iOffsetLowers * mtypesize;
                    MType frontierQm = frontierQmap[iOffsetLowers];
                    while (frontierQm != 0U)
                    {
                        int32_t last = ffsl(frontierQm) - 1;
                        frontierQ[size + p] = predecessorQ[index + last];
                        ++p;
                        frontierQm ^= (1U << last);
                    }
                }

                //Transmission of updates

                MPI_Sendrecv(frontierQ + size, p, fq_tp_type,
                             orankEven, iterator3,
                             frontierQ, size, fq_tp_type,
                             orankEven, iterator3,
                             col_comm, &status);
*/

//if (rowCommunicatorRank == 1 &&  rowCommunicatorRank == colCommunicatorRank) {
                //Updates for own data
                int64_t p = 0LL;
                // lowers: ~65k iteractions per MPI node (scale 22, 16 gpus)
                for (int64_t i = 0LL; i < bsize; ++i)
                {
                    //const int32_t iOffset = i + offset;
                    const int64_t index = i * mtypesize;
                    MType frontierQm = frontierQmap[i];

                    while (frontierQm != 0U)
                    {
                        const int32_t last = ffsl(frontierQm) - 1L;
                        (*outPredecessorQ)[index + last] = frontierQ[p];
                        ++p;
                        frontierQm ^= (1U << last);
                    }
                }
//}

//                ssize = lowers;
std::cout << "c,r: " << colCommunicatorRank << ","<< rowCommunicatorRank<< " - lower predecessor created here.\n";
delete[] fullFrontierQmap;
delete[] fullPredecessorQmap;
//delete[] outPredecessorQ;

/*
            if (((vrank >> it) & 1) == 0)  // even
            {
                //Transmission of the the bitmap
                MPI_Sendrecv(predecessorQmap + offset, ssize, bm_type, orankEven, iterator2,
                             frontierQmap + offset, ssize, bm_type, orankEven, iterator2,
                             col_comm, &status);

                // lowers: ~65k iteractions per MPI node (scale 22, 16 gpus)
#ifdef _OPENMP
                #pragma omp parallel for
#endif
                for (int32_t i = 0; i < lowers; ++i)
                {
                    const int32_t iOffset = i + offset;
                    frontierQmap[iOffset] &= ~predecessorQmap[iOffset];
                    predecessorQmap[iOffset] |= frontierQmap[iOffset];
                }

                // lowers: ~65k iteractions per MPI node (scale 22, 16 gpus)
#ifdef _OPENMP
                #pragma omp parallel for
#endif
                for (int32_t i = lowers; i < ssize; ++i)
                {
                    const int32_t iOffset = i + offset;
                    frontierQmap[iOffset] = (~frontierQmap[iOffset]) & predecessorQmap[iOffset];
                }

                //Generation of foreign updates
                // uppers: ~65k iteractions per MPI node (scale 22, 16 gpus)
                int32_t p = 0;
                for (int32_t i = 0; i < uppers; ++i)
                {
                    const int32_t iOffset = i + offset;
                    const int32_t iOffsetLowers = iOffset + lowers;
                    const int32_t index = iOffsetLowers * mtypesize;
                    MType frontierQm = frontierQmap[iOffsetLowers];
                    while (frontierQm != 0U)
                    {
                        int32_t last = ffsl(frontierQm) - 1;
                        frontierQ[size + p] = predecessorQ[index + last];
                        ++p;
                        frontierQm ^= (1U << last);
                    }
                }
                //Transmission of updates

                MPI_Sendrecv(frontierQ + size, p, fq_tp_type,
                             orankEven, iterator3,
                             frontierQ, size, fq_tp_type,
                             orankEven, iterator3,
                             col_comm, &status);

                //Updates for own data
                p = 0;
                // lowers: ~65k iteractions per MPI node (scale 22, 16 gpus)
                for (int32_t i = 0; i < lowers; ++i)
                {
                    const int32_t iOffset = i + offset;
                    const int32_t index = iOffset * mtypesize;
                    MType frontierQm = frontierQmap[iOffset];
                    while (frontierQm != 0U)
                    {
                        int32_t last = ffsl(frontierQm) - 1;
                        predecessorQ[index + last] = frontierQ[p];
                        ++p;
                        frontierQm ^= (1U << last);
                    }
                }
                ssize = lowers;
std::cout << "c,r: " << colCommunicatorRank << ","<< rowCommunicatorRank<< " - lower predecessor created here.\n";

            }
            else     // odd
            {
                //Transmission of the the bitmap
                MPI_Sendrecv(predecessorQmap + offset, ssize, bm_type,
                             orankOdd, iterator2,
                             frontierQmap + offset, ssize, bm_type,
                             orankOdd, iterator2,
                             col_comm, &status);

                // lowers: ~65k iteractions per MPI node (scale 22, 16 gpus)
#ifdef _OPENMP
                #pragma omp parallel for
#endif
                for (int32_t i = 0; i < lowers; ++i)
                {
                    const int32_t iOffset = i + offset;
                    frontierQmap[iOffset] = (~frontierQmap[iOffset]) & predecessorQmap[iOffset];
                }

                // lowers: ~65k iteractions per MPI node (scale 22, 16 gpus)
#ifdef _OPENMP
                #pragma omp parallel for
#endif
                for (int32_t i = lowers; i < ssize; ++i)
                {
                    const int32_t iOffset = i + offset;
                    frontierQmap[iOffset] &= ~predecessorQmap[iOffset];
                    predecessorQmap[iOffset] |= frontierQmap[iOffset];
                }

                // Generation of foreign updates
                // inner p: ~50k iteractions per MPI node (scale 22, 16 gpus)
                int32_t p = 0;
                for (int32_t i = 0; i < lowers; ++i)
                {
                    const int32_t iOffset = i + offset;
                    const int32_t iOffsetMtype = iOffset * mtypesize;
                    MType frontierQm = frontierQmap[iOffset];
                    while (frontierQm != 0U)
                    {
                        const int32_t last = ffsl(frontierQm) - 1;
                        frontierQ[p] = predecessorQ[iOffsetMtype + last];
                        ++p;
                        frontierQm ^= (1U << last);
                    }
                }

                //Transmission of updates
                MPI_Sendrecv(frontierQ, p, fq_tp_type,
                             orankOdd, iterator3,
                             frontierQ + size, uppers * mtypesize, fq_tp_type,
                             orankOdd, iterator3,
                             col_comm, &status);

                //Updates for own data
                // inner p: ~50k iteractions per MPI node (scale 22, 16 gpus)
                p = 0;
                for (int32_t i = 0; i < uppers; ++i)
                {
                    const int32_t iOffset = offset + lowers + i;
                    const int32_t lindex = iOffset * mtypesize;
                    MType frontierQm = frontierQmap[iOffset];
                    while (frontierQm != 0U)
                    {
                        const int32_t last = ffsl(frontierQm) - 1;
                        predecessorQ[lindex + last] = frontierQ[p + size];
                        ++p;
                        frontierQm ^= (1U << last);
                    }
                }
                offset += lowers;
                ssize = uppers;
std::cout << "c,r: " << colCommunicatorRank << ","<< rowCommunicatorRank<< " - lower predecessor created here.\n";

            }
        }
    }
*/
}



/**
 *
 * runBFS
 *
 *
 *
 *
 * BFS search:
 * 0) Node 0 sends start vertex to all nodes
 * 1) Nodes test, if they are responsible for this vertex and push it,
 *    if they are in there fq
 * 2) Local expansion
 * 3) Test if anything is done
 * 4) global expansion: Column Communication
 * 5) global fold: Row Communication
 */

template<typename Derived, typename FQ_T, typename MType, typename STORE>
#ifdef INSTRUMENTED
#ifdef _COMPRESSION
void GlobalBFS<Derived, FQ_T, MType, STORE>::runBFS(typename STORE::vertexType startVertex, double &lexp,
        double &lqueue,
        double &rowcom, double &colcom, double &predlistred, const CompressionClassT &schema, const CompressionClassT &bitmapSchema)
#else
void GlobalBFS<Derived, FQ_T, MType, STORE>::runBFS(typename STORE::vertexType startVertex, double &lexp,
        double &lqueue,
        double &rowcom, double &colcom, double &predlistred)
#endif
#else
#ifdef _COMPRESSION
void GlobalBFS<Derived, FQ_T, MType, STORE>::runBFS(typename STORE::vertexType startVertex, const CompressionClassT &schema, const CompressionClassT &bitmapSchema)
#else
void GlobalBFS<Derived, FQ_T, MType, STORE>::runBFS(typename STORE::vertexType startVertex)
#endif
#endif
{

    /**
     *
     *
     * Variable initialization. Seta a random start vertex. Broadcast it.
     *
     *
     *
     */

#ifdef _SCOREP_USER_INSTRUMENTATION
    SCOREP_USER_REGION_DEFINE(vertexBroadcast_handle)
    SCOREP_USER_REGION_DEFINE(localExpansion_handle)
    SCOREP_USER_REGION_DEFINE(columnCommunication_handle)
    SCOREP_USER_REGION_DEFINE(rowCommunication_handle)
    SCOREP_USER_REGION_DEFINE(allReduceBC_handle)
#endif

    static int colCommunicatorSize, colCommunicatorRank, rowCommunicatorSize, rowCommunicatorRank, rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(col_comm, &colCommunicatorSize);
    MPI_Comm_rank(col_comm, &colCommunicatorRank);

    MPI_Comm_size(row_comm, &rowCommunicatorSize);
    MPI_Comm_rank(row_comm, &rowCommunicatorRank);

    const int32_t intLdSize = ilogbf(colCommunicatorSize); //integer log_2 of size
    const uint32_t mtypesize = sizeof(MType) << 3; // * 8
    const int32_t power2intLdSize = 1 << intLdSize; // 2^n
    const int32_t residuum = colCommunicatorSize - power2intLdSize;
    bool finishedBFS = false;

#ifdef INSTRUMENTED
    double tstart, tend;
    lexp = 0;
    lqueue = 0;
    double comtstart, comtend;
    rowcom = 0;
    colcom = 0;
#endif

    const function <int32_t(int32_t)> newRank = [&residuum](int32_t oldr)
    {
        return (oldr < (residuum << 1)) ? (oldr >> 1) : oldr - residuum;
    };
    const function <int32_t(int32_t)> oldRank = [&residuum](uint32_t newr)
    {
        return (newr < residuum) ? (newr << 1) : newr + residuum;
    };

    const function <void(FQ_T, long, FQ_T *, int)> reduce =
        bind(static_cast<void (Derived::*)(FQ_T, long, FQ_T *, int)>(&Derived::reduce_fq_out),
             static_cast<Derived *>(this), _1, _2, _3, _4);
    const function <void(FQ_T, long, FQ_T *&, int &)> get =
        bind(static_cast<void (Derived::*)(FQ_T, long, FQ_T *&, int &)>(&Derived::getOutgoingFQ),
             static_cast<Derived *>(this), _1, _2, _3, _4);

#ifdef _COMPRESSION
    size_t uncompressedsize, compressedsize;
#endif

    /**
     *
     * empty. available for allBitmapCompressed() bitmap. Run-Length Encoding codec family recommended here
     */
    bitmapSchema.init();






#ifdef _SCOREP_USER_INSTRUMENTATION
    SCOREP_USER_REGION_BEGIN(vertexBroadcast_handle, "BFSRUN_region_vertexBroadcast", SCOREP_USER_REGION_TYPE_COMMON)
#endif

// 0) Node 0 sends start vertex to all nodes
    MPI_Bcast(&startVertex, 1, MPI_LONG, 0, MPI_COMM_WORLD);


#ifdef _SCOREP_USER_INSTRUMENTATION
    SCOREP_USER_REGION_END(vertexBroadcast_handle)
#endif

// 1) Nodes test, if they are responsible for this vertex and push it, if they are in there fq
#ifdef INSTRUMENTED
    tstart = MPI_Wtime();
#endif

    static_cast<Derived *>(this)->setStartVertex(startVertex);


#ifdef INSTRUMENTED
    lqueue += MPI_Wtime() - tstart;
#endif

// 2) Local expansion

    int depthBFS = 0;

        /**
         *
         *
         *
         * Start main BFS iteration.
         *
         *
         *
         */

    while (!finishedBFS)
    {



#ifdef INSTRUMENTED
        tstart = MPI_Wtime();
#endif

#ifdef _SCOREP_USER_INSTRUMENTATION
        SCOREP_USER_REGION_BEGIN(localExpansion_handle, "BFSRUN_region_localExpansion", SCOREP_USER_REGION_TYPE_COMMON)
#endif

        static_cast<Derived *>(this)->runLocalBFS();

#ifdef _SCOREP_USER_INSTRUMENTATION
        SCOREP_USER_REGION_END(localExpansion_handle)
#endif

#ifdef INSTRUMENTED
        lexp += MPI_Wtime() - tstart;
#endif

// 3) Test if anything is done
        int anynewnodes, anynewnodes_global;

#ifdef INSTRUMENTED
        tstart = MPI_Wtime();
#endif

        /**
         *
         * avoid first iteration's check
         * adds a fail probaility of p=1/(2^scale) for a true random start Vertex selection
         */
        if (depthBFS > 0)
        {

            anynewnodes = static_cast<Derived *>(this)->istheresomethingnew();

#ifdef INSTRUMENTED
            lqueue += MPI_Wtime() - tstart;
#endif

            MPI_Allreduce(&anynewnodes, &anynewnodes_global, 1, MPI_INT, MPI_LOR, MPI_COMM_WORLD);

            if (!anynewnodes_global)
            {

                finishedBFS = true;

                /**
                 *
                 *
                 *
                 * End of BFS iteration. Pass predecessors to main() for Verification()
                 *
                 *
                 */

                /*
                int err, err1, err2;

                int32_t * restrict sizes = NULL;
                int32_t * restrict disps = NULL;
                err1 = posix_memalign((void **)&sizes, ALIGNMENT, colCommunicatorSize * sizeof(int32_t));
                err2 = posix_memalign((void **)&disps, ALIGNMENT, colCommunicatorSize * sizeof(int32_t));
                if (err1 || err2) {
                    throw "Memory error.";
                }
                int32_t * restrict compressed_sizes = NULL;
                int32_t * restrict compressed_disps = NULL;
                err1 = posix_memalign((void **)&compressed_sizes, ALIGNMENT, colCommunicatorSize * sizeof(int32_t));
                err2 = posix_memalign((void **)&compressed_disps, ALIGNMENT, colCommunicatorSize * sizeof(int32_t));
                if (err1 || err2) {
                    throw "Memory error.";
                }
                int32_t psize;
                int32_t maskLengthRes;
                int32_t lastTargetNode;
                uint32_t lastReversedSliceIDs;




#ifdef INSTRUMENTED
                tstart = MPI_Wtime();
#endif
                static_cast<Derived *>(this)->getBackPredecessor();
#ifdef INSTRUMENTED
                lqueue += MPI_Wtime() - tstart;
#endif

#ifdef _SCOREP_USER_INSTRUMENTATION
                SCOREP_USER_REGION_BEGIN(allReduceBC_handle, "BFSRUN_region_allReduceBC", SCOREP_USER_REGION_TYPE_COMMON)
#endif



                static_cast<Derived *>(this)->generatOwenMask();


                const size_t normalsize = store.getLocColLength();
                size_t compressedsize, decompressedsize;
                compressionType *compressedFQ = NULL, *compressedPredeccessors = NULL;
                FQ_T *decompressedFQ = NULL, *decompressedPredeccesors = NULL;





                schema.compress(fq_64, normalsize, &compressedFQ, compressedsize);

                MPI_Allgather(&compressedsize, 1, MPI_INT, compressed_sizes, 1, MPI_INT, col_comm);

                for (int32_t i = 1L; i< colCommunicatorSize; ++i)
                {
                    sizes[i] = normalsize;
                }

#ifdef _COMPRESSIONVERIFY
                decompressedsize = normalsize;
                int compressedsize_int = static_cast<int32_t>(compressedsize);
                schema.decompress(compressedFQ, compressedsize_int, &decompressedFQ, decompressedsize);
                assert(normalsize == decompressedsize);
                assert(memcmp(fq_64, decompressedFQ, normalsize * sizeof(FQ_T)) == 0);
#endif

                lastReversedSliceIDs = 0UL;
                lastTargetNode = oldRank(lastReversedSliceIDs);

                disps[lastTargetNode] = 0;
                compressed_disps[lastTargetNode] = 0;

                for (int32_t slice = 1L; slice < power2intLdSize; ++slice)
                {
                    const uint32_t reversedSliceIDs = reverse(slice, intLdSize);
                    const int32_t targetNode = oldRank(reversedSliceIDs);
                    compressed_disps[targetNode] = compressed_disps[lastTargetNode] + compressed_sizes[lastTargetNode];
                    disps[targetNode] = disps[lastTargetNode] + sizes[lastTargetNode];
                    lastTargetNode = targetNode;
                }

                for (int32_t node = 0L; node < residuum; ++node)
                {
                    const int32_t index = 2 * node + 1;
                    disps[index] = 0;
                    compressed_disps[index] = 0;
                }

                //size_t csize = compressed_disps[lastTargetNode] + compressed_sizes[lastTargetNode];
                //size_t rsize = disps[lastTargetNode] + sizes[lastTargetNode];

                err = posix_memalign((void **)&compressedPredeccessors, ALIGNMENT, csize * sizeof(compressionType));
                if (err) {
                    throw "Memory error.";
                }
                err = posix_memalign((void **)&decompressedPredeccesors, ALIGNMENT, rsize * sizeof(FQ_T));
                if (err) {
                    throw "Memory error.";
                }

                MPI_Allgatherv(compressedFQ, compressed_sizes[communicatorRank],
                        fq_tp_typeC, compressedPredeccessors, compressed_sizes,
                        compressed_disps, fq_tp_typeC, col_comm);

                free(compressedFQ);

                for (int32_t i = 0; i < colCommunicatorSize; ++i)
                {
                    compressedsize = compressed_sizes[i];
                    decompressedsize = sizes[i];
                    if (compressedsize != 0)
                    {
                        schema.decompress(&compressedPredeccessors[compressed_disps[i]], compressedsize, &decompressedFQ, decompressedsize);
                        memcpy(&decompressedPredeccesors[disps[i]], decompressedFQ, decompressedsize * sizeof(FQ_T));
                        free(decompressedFQ);
                    }
                }

                //free(compressedPredeccessors);
                //free(decompressedPredeccesors);

                allReduceBitCompressed(predecessor,
                                       fq_64,
                                       owenmask, tmpmask);



                psize = static_cast<int32_t>(mask_size);
                maskLengthRes = psize % (1 << intLdSize);
                lastReversedSliceIDs = 0U;
                lastTargetNode = oldRank(lastReversedSliceIDs);
                sizes[lastTargetNode] = (psize >> intLdSize) * mtypesize;
                disps[lastTargetNode] = 0;

                for (int32_t slice = 1; slice < power2intLdSize; ++slice)
                {
                    const uint32_t reversedSliceIDs = reverse(slice, intLdSize);
                    const int32_t targetNode = oldRank(reversedSliceIDs);
                    sizes[targetNode] = ((psize >> intLdSize) + (((power2intLdSize - reversedSliceIDs - 1) < maskLengthRes) ? 1 : 0)) *
                                        mtypesize;
                    disps[targetNode] = disps[lastTargetNode] + sizes[lastTargetNode];
                    lastTargetNode = targetNode;
                }
                sizes[lastTargetNode] = std::min(sizes[lastTargetNode],
                                                 static_cast<int32_t>(store.getLocColLength() - disps[lastTargetNode]));

                for (int32_t node = 0; node < residuum; ++node)
                {
                    const int32_t index = (node * 2) + 1;
                    sizes[index] = 0;
                    disps[index] = 0;
                }

                MPI_Allgatherv(MPI_IN_PLACE, 0, MPI_DATATYPE_NULL,
                                predecessor, sizes, disps, fq_tp_type, col_comm);




#ifdef _SCOREP_USER_INSTRUMENTATION
                SCOREP_USER_REGION_END(allReduceBC_handle)
#endif

#ifdef INSTRUMENTED
                predlistred = MPI_Wtime() - tstart;
#endif

                free(sizes);
                free(disps);


                finishedBFS = true;
                return;
*/
            }
        }

// 4) global expansion
#ifdef INSTRUMENTED
        comtstart = MPI_Wtime();
        tstart = comtstart;
#endif

#ifdef _SCOREP_USER_INSTRUMENTATION
        SCOREP_USER_REGION_BEGIN(columnCommunication_handle, "BFSRUN_region_columnCommunication",
                                 SCOREP_USER_REGION_TYPE_COMMON)
#endif

        static_cast<Derived *>(this)->getBackOutqueue();

#ifdef INSTRUMENTED
        lqueue +=  MPI_Wtime() - tstart;
#endif

        int _outsize;

            /**
             *
             *
             *
             *
             *
             * 2D-partitioning - Column communication. (vreduce())
             *
             */

        vreduce(reduce, get,
#ifdef _COMPRESSION
                schema, fq_tp_typeC,
#endif
                fq_64, _outsize, store.getLocColLength(), fq_tp_type, col_comm, row_comm
#ifdef INSTRUMENTED
                , lqueue
#endif
               );


            /**
             *
             *
             *
             *
             *
             *
             * 2D-partitioning - Rows communication.
             *
             */


        static_cast<Derived *>(this)->setModOutgoingFQ(fq_64, _outsize);

#ifdef _SCOREP_USER_INSTRUMENTATION
        SCOREP_USER_REGION_END(columnCommunication_handle)
#endif

#ifdef INSTRUMENTED
        comtend = MPI_Wtime();
        colcom += comtend - comtstart;
#endif

// 5) global fold
#ifdef INSTRUMENTED
        comtstart = comtend;
#endif

#ifdef _SCOREP_USER_INSTRUMENTATION
        SCOREP_USER_REGION_BEGIN(rowCommunication_handle, "BFSRUN_region_rowCommunication",
                                 SCOREP_USER_REGION_TYPE_LOOP)
#endif

        int root_rank;
        for (typename vector<typename STORE::fold_prop>::iterator it = fold_fq_props.begin();
             it != fold_fq_props.end(); ++it)
        {
            root_rank = it->sendColSl;
            if (root_rank == store.getLocalColumnID())
            {

                int originalsize;
                FQ_T *startaddr;
#ifdef _COMPRESSION
                compressionType *compressed_fq;
                FQ_T *uncompressed_fq;
#endif

#ifdef INSTRUMENTED
                tstart = MPI_Wtime();
#endif
                static_cast<Derived *>(this)->getOutgoingFQ(it->startvtx, it->size, startaddr, originalsize);

#ifdef INSTRUMENTED
                lqueue += MPI_Wtime() - tstart;
#endif

#ifdef _COMPRESSION

#if defined(_COMPRESSIONDEBUG)
                schema.debugCompression(startaddr, originalsize);
#endif

                uncompressedsize = static_cast<size_t>(originalsize);
                schema.compress(startaddr, uncompressedsize, &compressed_fq, compressedsize);

#if defined(_COMPRESSIONVERIFY)
                schema.decompress(compressed_fq, compressedsize,  &uncompressed_fq, uncompressedsize);
#endif

#endif

#ifdef _COMPRESSION

                int * restrict vectorizedsize = NULL;
                err = posix_memalign((void **)&vectorizedsize, ALIGNMENT, 2 * sizeof(int));
                if (err) {
                    throw "Memory error.";
                }
                vectorizedsize[0] = originalsize;
                vectorizedsize[1] = compressedsize;
                MPI_Bcast(vectorizedsize, 1, MPI_2INT, root_rank, row_comm);

#if defined(_COMPRESSIONVERIFY)
                bool isCompressed = schema.isCompressed(originalsize, compressedsize);
                if (isCompressed)
                {
                        MPI_Bcast(startaddr, originalsize, fq_tp_type, root_rank, row_comm);
                }
#endif

                MPI_Bcast(compressed_fq, compressedsize, fq_tp_typeC, root_rank, row_comm);

#else
                MPI_Bcast(&originalsize, 1, MPI_INT, root_rank, row_comm);
                MPI_Bcast(startaddr, originalsize, fq_tp_type, root_rank, row_comm);
#endif

#ifdef _COMPRESSION

                if (colCommunicatorRank != root_rank)
                {
                    uncompressedsize = static_cast<size_t>(originalsize);
                    schema.decompress(compressed_fq, compressedsize,  &uncompressed_fq,  uncompressedsize);
                }

#if defined(_COMPRESSIONVERIFY)
                if (colCommunicatorRank != root_rank)
                {
                    assert(memcmp(startaddr, uncompressed_fq, originalsize * sizeof(FQ_T)) == 0);
                    assert(is_sorted(uncompressed_fq, uncompressed_fq + uncompressedsize));
                    schema.verifyCompression(startaddr, uncompressed_fq, originalsize);
                }
                else
                {
                    assert(is_sorted(startaddr, startaddr + originalsize));
                }
#endif


#endif

#ifdef INSTRUMENTED
                tstart = MPI_Wtime();
#endif


#ifdef _COMPRESSION
                if (colCommunicatorRank != root_rank)
                {
                    static_cast<Derived *>(this)->setIncommingFQ(it->startvtx, it->size, uncompressed_fq, originalsize);
                }
                else
                {
                    static_cast<Derived *>(this)->setIncommingFQ(it->startvtx, it->size, startaddr, originalsize);
                }
#else
                static_cast<Derived *>(this)->setIncommingFQ(it->startvtx, it->size, startaddr, originalsize);
#endif

#ifdef _COMPRESSION
                if (colCommunicatorRank != root_rank)
                {
                    free(uncompressed_fq);
                }
                free(compressed_fq);
                free(vectorizedsize);
#endif

#ifdef INSTRUMENTED
                lqueue += MPI_Wtime() - tstart;
#endif

            }
            else
            {

#ifdef _COMPRESSION
                compressionType *compressed_fq = NULL;
                FQ_T *uncompressed_fq = NULL;
                int originalsize, compressedsize;
                int * restrict vectorizedsize = NULL;
                err = posix_memalign((void **)&vectorizedsize, ALIGNMENT, 2 * sizeof(int));
                if (err) {
                    throw "Memory error.";
                }

                MPI_Bcast(vectorizedsize, 1, MPI_2INT, root_rank, row_comm);
                originalsize = vectorizedsize[0];
                compressedsize = vectorizedsize[1];
                err = posix_memalign((void **)&compressed_fq, ALIGNMENT, compressedsize * sizeof(compressionType));
                if (err) {
                    throw "Memory error.";
                }

#if defined(_COMPRESSIONVERIFY)
        bool isCompressed = schema.isCompressed(originalsize, compressedsize);
                FQ_T *startaddr = NULL;
                if (isCompressed)
                {
                    err = posix_memalign((void **)&startaddr, ALIGNMENT, originalsize * sizeof(FQ_T));
                    if (err) {
                        throw "Memory error.";
                    }

                    MPI_Bcast(startaddr, originalsize, fq_tp_type, root_rank, row_comm);
                }
#endif
                // Please Note: fq_64 buffer has been alloceted in the children class.
                // In case of CUDA, it has been allocated using cudaMalloc. In that case,
                // it would be pinned memory and therefore can not be modified using
                // normal C/C++ memory functions. Further info on the
                // (this)->bfsMemCpy() call below.
                // MPI_Bcast(fq_64, compressedsize, fq_tp_typeC, root_rank, row_comm);
                // compressed_fq is type: fq_tp_typeC

                MPI_Bcast(compressed_fq, compressedsize, fq_tp_typeC, root_rank, row_comm);

                uncompressedsize = static_cast<size_t>(originalsize);
                schema.decompress(compressed_fq, compressedsize, /*Out*/ &uncompressed_fq, /*In Out*/ uncompressedsize);

                static_cast<Derived *>(this)->bfsMemCpy(fq_64, uncompressed_fq, originalsize);

#if defined(_COMPRESSION) && defined(_COMPRESSIONVERIFY)
                assert(is_sorted(fq_64, fq_64 + originalsize));
#endif


#else
                int originalsize;
                MPI_Bcast(&originalsize, 1, MPI_INT, root_rank, row_comm);
                MPI_Bcast(fq_64, originalsize, fq_tp_type, root_rank, row_comm);
#endif




#ifdef INSTRUMENTED
                tstart = MPI_Wtime();
#endif


                static_cast<Derived *>(this)->setIncommingFQ(it->startvtx, it->size, fq_64, originalsize);

#ifdef INSTRUMENTED
                lqueue += MPI_Wtime() - tstart;
#endif

#ifdef _COMPRESSION
                free(vectorizedsize);
                free(uncompressed_fq);
                free(compressed_fq);

#if defined(_COMPRESSIONVERIFY)
                if (isCompressed)
                {
                    free(startaddr);
                }
#endif

#if defined(_COMPRESSIONVERIFY)
                free(startaddr);
#endif

#endif
            }
        }


        if (finishedBFS) {


            static_cast<Derived *>(this)->getBackPredecessor();


            /**
             *
             *
             *
             * End of BFS iteration. Pass predecessors to main() for Verification()
             *
             *
             */

/*
            int err, err1, err2;

            int32_t * restrict sizes = NULL;
            int32_t * restrict disps = NULL;
            err1 = posix_memalign((void **)&sizes, ALIGNMENT, colCommunicatorSize * sizeof(int32_t));
            err2 = posix_memalign((void **)&disps, ALIGNMENT, colCommunicatorSize * sizeof(int32_t));
            if (err1 || err2) {
                throw "Memory error.";
            }
            int32_t psize;
            int32_t maskLengthRes;
            int32_t lastTargetNode;
            uint32_t lastReversedSliceIDs;
*/

	    FQ_T *outPredecessorQ;
            FQ_T *destroyPointer;	
            //allReduceBitCompressed(predecessor, fq_64, owenmask, tmpmask);
            allReduceBitCompressed_dup(predecessor, fq_64, owenmask, tmpmask, &outPredecessorQ);

	    destroyPointer = predecessor;
	    // do delete
	    predecessor = outPredecessorQ;		
/*
            psize = static_cast<int32_t>(mask_size);
            maskLengthRes = psize % (1 << intLdSize);
            lastReversedSliceIDs = 0U;
            lastTargetNode = oldRank(lastReversedSliceIDs);
            sizes[lastTargetNode] = (psize >> intLdSize) * mtypesize;
            disps[lastTargetNode] = 0;

            for (int32_t slice = 1; slice < power2intLdSize; ++slice)
            {
                const uint32_t reversedSliceIDs = reverse(slice, intLdSize);
                const int32_t targetNode = oldRank(reversedSliceIDs);
                sizes[targetNode] = ((psize >> intLdSize) + (((power2intLdSize - reversedSliceIDs - 1) < maskLengthRes) ? 1 : 0)) *
                                    mtypesize;
                disps[targetNode] = disps[lastTargetNode] + sizes[lastTargetNode];
                lastTargetNode = targetNode;
            }
            sizes[lastTargetNode] = std::min(sizes[lastTargetNode],
                                             static_cast<int32_t>(store.getLocColLength() - disps[lastTargetNode]));

            for (int32_t node = 0; node < residuum; ++node)
            {
                const int32_t index = (node * 2) + 1;
                sizes[index] = 0;
                disps[index] = 0;
            }

            MPI_Allgatherv(MPI_IN_PLACE, 0, MPI_DATATYPE_NULL,
                            predecessor, sizes, disps, fq_tp_type, col_comm);

*/

            return;
        }



#ifdef INSTRUMENTED
        tstart = MPI_Wtime();
#endif

        static_cast<Derived *>(this)->setBackInqueue();

#ifdef INSTRUMENTED
        tend = MPI_Wtime();
        lqueue += tend - tstart;
#endif

#ifdef INSTRUMENTED
        rowcom += tend - comtstart;
#endif

#ifdef _SCOREP_USER_INSTRUMENTATION
        SCOREP_USER_REGION_END(rowCommunication_handle)
#endif
        /**
         *
         *
         *
         * Deeph of graph "search-tree" increases.
         *
         */

        ++depthBFS;
    }
}
#endif // GLOBALBFS_HH
