#pragma once

#include <mln/tile/tile_id.hpp>
#include <mln/util/bitmask_operations.hpp>
#include <mln/util/constants.hpp>
#include <mln/util/geometry.hpp>
#include <mln/util/mat4.hpp>

#include <cstdint>
#include <map>
#include <set>
#include <vector>
#include <string>
#include <memory>
#include <thread>
#include <unordered_set>

namespace mln {

class SymbolInstance;
class RenderLayer;
class SymbolBucket;

class IndexedSymbolInstance {
public:
    IndexedSymbolInstance(uint32_t crossTileID_, Point<int64_t> coord_)
        : crossTileID(crossTileID_),
          coord(coord_) {}

    uint32_t crossTileID;
    Point<int64_t> coord;
};

class TileLayerIndex {
public:
    TileLayerIndex(OverscaledTileID coord,
                   std::vector<SymbolInstance>&,
                   uint32_t bucketInstanceId,
                   std::string bucketLeaderId);

    Point<int64_t> getScaledCoordinates(const SymbolInstance&, const OverscaledTileID&) const;
    void findMatches(SymbolBucket&, const OverscaledTileID&, std::set<uint32_t>&) const;

    OverscaledTileID coord;
    uint32_t bucketInstanceId;
    std::string bucketLeaderId;
    std::map<std::u16string, std::vector<IndexedSymbolInstance>> indexedSymbolInstances;
    // Change stamp of the layer index right after this bucket was last indexed.
    uint64_t indexedGeneration = 0;
    // Whether that indexing left every cross-tile ID of the bucket as it was.
    bool settled = false;
};

class CrossTileSymbolLayerIndex {
public:
    CrossTileSymbolLayerIndex(uint32_t& maxCrossTileID);
    bool addBucket(const OverscaledTileID&, const mat4& tileMatrix, SymbolBucket&);
    bool removeStaleBuckets(const std::unordered_set<uint32_t>& currentIDs);
    void handleWrapJump(float newLng);

    /// Changes whenever an index of this layer changes: a bucket indexed with different
    /// cross-tile IDs, a bucket removed, or a wrap jump. Values set by markChanged are unique across all indexes.
    uint64_t getGeneration() const { return generation; }

private:
    void removeBucketCrossTileIDs(uint8_t zoom, const TileLayerIndex& removedBucket);
    void markChanged();

    std::map<uint8_t, std::map<OverscaledTileID, TileLayerIndex>> indexes;
    std::map<uint8_t, std::set<uint32_t>> usedCrossTileIDs;
    float lng = 0;
    uint32_t& maxCrossTileID;
    uint64_t generation = 0;
};

class CrossTileSymbolIndex {
public:
    CrossTileSymbolIndex();

    enum class AddLayerResult : uint8_t {
        NoChanges = 0,
        BucketsAdded = 1 << 0,
        BucketsRemoved = 1 << 1
    };

    AddLayerResult addLayer(const RenderLayer& layer, float lng);
    void pruneUnusedLayers(const std::set<std::string>&);

    void reset();

    /// The change stamp of the given layer's index, or 0 if the layer has none.
    uint64_t layerGeneration(const std::string& layerID) const;

    const std::thread::id renderThreadID = std::this_thread::get_id();

private:
    std::map<std::string, CrossTileSymbolLayerIndex> layerIndexes;
    uint32_t maxCrossTileID = 0;
};

} // namespace mln
