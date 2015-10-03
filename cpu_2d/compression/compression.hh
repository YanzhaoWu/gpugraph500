#ifndef BFS_MULTINODE_COMPRESSION_H
#define BFS_MULTINODE_COMPRESSION_H



template <typename T>
class Compression
{
protected:
public:
    virtual void benchmarkCompression(const T *fq, const int size) const = 0;
    virtual void compress(T *fq_64, const size_t &size, T **compressed_fq_64, size_t &compressedsize) const = 0;
    virtual void decompress(T *compressed_fq_64, const int size,
                            /*Out*/ T **uncompressed_fq_64, /*In Out*/size_t &uncompressedsize) const = 0;
    virtual void verifyCompression(const T *fq, const T *uncompressed_fq_64,
                                   const size_t uncompressedsize) const = 0;
    virtual inline bool isCompressed(const size_t originalsize, const size_t compressedsize) const = 0;
    virtual ~Compression()
    {
    }
    virtual std::string name() const = 0;
};

#endif // BFS_MULTINODE_COMPRESSION_H