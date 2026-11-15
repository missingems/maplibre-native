#include <mln/map/transform_state.hpp>
#include <mln/renderer/buckets/symbol_bucket.hpp>
#include <mln/style/variable_anchor_offset_collection.hpp>
#include <mln/test/util.hpp>
#include <mln/text/cross_tile_symbol_index.hpp>

#include <array>
#include <cstdio>
#include <memory>
#include <random>
#include <string>
#include <unordered_set>
#include <vector>

using namespace mln;

SymbolInstance makeSymbolInstance(float x, float y, std::u16string key) {
    GeometryCoordinates line;
    ImageMap imageMap;
    const ShapedTextOrientations shaping{};
    style::SymbolLayoutProperties::Evaluated layout_;
    IndexedSubfeature subfeature(0, {}, {}, 0);
    Anchor anchor(x, y, 0, 0);
    std::array<float, 2> textOffset{{0.0f, 0.0f}};
    std::array<float, 2> iconOffset{{0.0f, 0.0f}};
    std::array<float, 2> variableTextOffset{{0.0f, 0.0f}};
    std::vector<AnchorOffsetPair> anchorOffsets = {{style::SymbolAnchorType::Left, variableTextOffset}};
    VariableAnchorOffsetCollection variableAnchorOffsetCollection(std::move(anchorOffsets));
    style::SymbolPlacementType placementType = style::SymbolPlacementType::Point;

    auto sharedData = std::make_shared<SymbolInstanceSharedData>(std::move(line),
                                                                 shaping,
                                                                 std::nullopt,
                                                                 std::nullopt,
                                                                 layout_,
                                                                 placementType,
                                                                 textOffset,
                                                                 imageMap,
                                                                 0.0f,
                                                                 SymbolContent::IconSDF,
                                                                 false,
                                                                 false);
    return SymbolInstance(anchor,
                          std::move(sharedData),
                          shaping,
                          std::nullopt,
                          std::nullopt,
                          0,
                          0,
                          placementType,
                          textOffset,
                          0,
                          0,
                          iconOffset,
                          subfeature,
                          0,
                          0,
                          key,
                          0.0f,
                          0.0f,
                          0.0f,
                          variableAnchorOffsetCollection,
                          false);
}

TEST(CrossTileSymbolLayerIndex, addBucket) {
    uint32_t maxCrossTileID = 0;
    uint32_t maxBucketInstanceId = 0;
    CrossTileSymbolLayerIndex index(maxCrossTileID);

    Immutable<style::SymbolLayoutProperties::PossiblyEvaluated> layout =
        makeMutable<style::SymbolLayoutProperties::PossiblyEvaluated>();
    bool iconsNeedLinear = false;
    bool sortFeaturesByY = false;
    std::string bucketLeaderID = "test";

    OverscaledTileID mainID(6, 0, 6, 8, 8);
    std::vector<SymbolInstance> mainInstances;
    std::vector<SortKeyRange> mainRanges;
    mainInstances.push_back(makeSymbolInstance(1000, 1000, u"Detroit"));
    mainInstances.push_back(makeSymbolInstance(2000, 2000, u"Toronto"));
    SymbolBucket mainBucket{layout,
                            {},
                            16.0f,
                            1.0f,
                            0,
                            iconsNeedLinear,
                            sortFeaturesByY,
                            bucketLeaderID,
                            std::move(mainInstances),
                            std::move(mainRanges),
                            1.0f,
                            false,
                            {},
                            false /*iconsInText*/};
    mainBucket.bucketInstanceId = ++maxBucketInstanceId;
    index.addBucket(mainID, mat4{}, mainBucket);

    // Assigned new IDs
    ASSERT_EQ(mainBucket.symbolInstances.at(0).getCrossTileID(), 1u);
    ASSERT_EQ(mainBucket.symbolInstances.at(1).getCrossTileID(), 2u);

    OverscaledTileID childID(7, 0, 7, 16, 16);
    std::vector<SymbolInstance> childInstances;
    std::vector<SortKeyRange> childRanges;
    childInstances.push_back(makeSymbolInstance(2000, 2000, u"Detroit"));
    childInstances.push_back(makeSymbolInstance(2000, 2000, u"Windsor"));
    childInstances.push_back(makeSymbolInstance(3000, 3000, u"Toronto"));
    childInstances.push_back(makeSymbolInstance(4001, 4001, u"Toronto"));
    SymbolBucket childBucket{layout,
                             {},
                             16.0f,
                             1.0f,
                             0,
                             iconsNeedLinear,
                             sortFeaturesByY,
                             bucketLeaderID,
                             std::move(childInstances),
                             std::move(childRanges),
                             1.0f,
                             false,
                             {},
                             false /*iconsInText*/};
    childBucket.bucketInstanceId = ++maxBucketInstanceId;
    index.addBucket(childID, mat4{}, childBucket);

    // matched parent tile
    ASSERT_EQ(childBucket.symbolInstances.at(0).getCrossTileID(), 1u);
    // does not match because of different key
    ASSERT_EQ(childBucket.symbolInstances.at(1).getCrossTileID(), 3u);
    // does not match because of different location
    ASSERT_EQ(childBucket.symbolInstances.at(2).getCrossTileID(), 4u);
    // matches with a slightly different location
    ASSERT_EQ(childBucket.symbolInstances.at(3).getCrossTileID(), 2u);

    OverscaledTileID parentID(5, 0, 5, 4, 4);
    std::vector<SymbolInstance> parentInstances;
    std::vector<SortKeyRange> parentRanges;
    parentInstances.push_back(makeSymbolInstance(500, 500, u"Detroit"));
    SymbolBucket parentBucket{layout,
                              {},
                              16.0f,
                              1.0f,
                              0,
                              iconsNeedLinear,
                              sortFeaturesByY,
                              bucketLeaderID,
                              std::move(parentInstances),
                              std::move(parentRanges),
                              1.0f,
                              false,
                              {},
                              false /*iconsInText*/};
    parentBucket.bucketInstanceId = ++maxBucketInstanceId;
    index.addBucket(parentID, mat4{}, parentBucket);

    // matched child tile
    ASSERT_EQ(parentBucket.symbolInstances.at(0).getCrossTileID(), 1u);

    std::unordered_set<uint32_t> currentIDs;
    currentIDs.insert(mainBucket.bucketInstanceId);
    index.removeStaleBuckets(currentIDs);

    // grandchild
    OverscaledTileID grandchildID(8, 0, 8, 32, 32);
    std::vector<SymbolInstance> grandchildInstances;
    std::vector<SortKeyRange> grandchildRanges;
    grandchildInstances.push_back(makeSymbolInstance(4000, 4000, u"Detroit"));
    grandchildInstances.push_back(makeSymbolInstance(4000, 4000, u"Windsor"));
    SymbolBucket grandchildBucket{layout,
                                  {},
                                  16.0f,
                                  1.0f,
                                  0,
                                  iconsNeedLinear,
                                  sortFeaturesByY,
                                  bucketLeaderID,
                                  std::move(grandchildInstances),
                                  std::move(grandchildRanges),
                                  1.0f,
                                  false,
                                  {},
                                  false /*iconsInText*/};
    grandchildBucket.bucketInstanceId = ++maxBucketInstanceId;
    index.addBucket(grandchildID, mat4{}, grandchildBucket);

    // Matches the symbol in `mainBucket`
    ASSERT_EQ(grandchildBucket.symbolInstances.at(0).getCrossTileID(), 1u);
    // Does not match the previous value for Windsor because that tile was removed
    ASSERT_EQ(grandchildBucket.symbolInstances.at(1).getCrossTileID(), 5u);
}

TEST(CrossTileSymbolLayerIndex, resetIDs) {
    uint32_t maxCrossTileID = 0;
    uint32_t maxBucketInstanceId = 0;
    CrossTileSymbolLayerIndex index(maxCrossTileID);

    Immutable<style::SymbolLayoutProperties::PossiblyEvaluated> layout =
        makeMutable<style::SymbolLayoutProperties::PossiblyEvaluated>();
    bool iconsNeedLinear = false;
    bool sortFeaturesByY = false;
    std::string bucketLeaderID = "test";

    OverscaledTileID mainID(6, 0, 6, 8, 8);
    std::vector<SymbolInstance> mainInstances;
    std::vector<SortKeyRange> mainRanges;
    mainInstances.push_back(makeSymbolInstance(1000, 1000, u"Detroit"));
    SymbolBucket mainBucket{layout,
                            {},
                            16.0f,
                            1.0f,
                            0,
                            iconsNeedLinear,
                            sortFeaturesByY,
                            bucketLeaderID,
                            std::move(mainInstances),
                            std::move(mainRanges),
                            1.0f,
                            false,
                            {},
                            false /*iconsInText*/};
    mainBucket.bucketInstanceId = ++maxBucketInstanceId;

    OverscaledTileID childID(7, 0, 7, 16, 16);
    std::vector<SymbolInstance> childInstances;
    std::vector<SortKeyRange> childRanges;
    childInstances.push_back(makeSymbolInstance(2000, 2000, u"Detroit"));
    SymbolBucket childBucket{layout,
                             {},
                             16.0f,
                             1.0f,
                             0,
                             iconsNeedLinear,
                             sortFeaturesByY,
                             bucketLeaderID,
                             std::move(childInstances),
                             std::move(childRanges),
                             1.0f,
                             false,
                             {},
                             false /*iconsInText*/};
    childBucket.bucketInstanceId = ++maxBucketInstanceId;

    // assigns a new id
    index.addBucket(mainID, mat4{}, mainBucket);
    ASSERT_EQ(mainBucket.symbolInstances.at(0).getCrossTileID(), 1u);

    // removes the tile
    std::unordered_set<uint32_t> currentIDs;
    index.removeStaleBuckets(currentIDs);

    // assigns a new id
    index.addBucket(childID, mat4{}, childBucket);
    ASSERT_EQ(childBucket.symbolInstances.at(0).getCrossTileID(), 2u);

    // overwrites the old id to match the already-added tile
    index.addBucket(mainID, mat4{}, mainBucket);
    ASSERT_EQ(mainBucket.symbolInstances.at(0).getCrossTileID(), 2u);
}

TEST(CrossTileSymbolLayerIndex, noDuplicatesWithinZoomLevel) {
    uint32_t maxCrossTileID = 0;
    uint32_t maxBucketInstanceId = 0;
    CrossTileSymbolLayerIndex index(maxCrossTileID);

    Immutable<style::SymbolLayoutProperties::PossiblyEvaluated> layout =
        makeMutable<style::SymbolLayoutProperties::PossiblyEvaluated>();
    bool iconsNeedLinear = false;
    bool sortFeaturesByY = false;
    std::string bucketLeaderID = "test";

    OverscaledTileID mainID(6, 0, 6, 8, 8);
    std::vector<SymbolInstance> mainInstances;
    std::vector<SortKeyRange> mainRanges;
    mainInstances.push_back(makeSymbolInstance(1000, 1000, u"")); // A
    mainInstances.push_back(makeSymbolInstance(1000, 1000, u"")); // B
    SymbolBucket mainBucket{layout,
                            {},
                            16.0f,
                            1.0f,
                            0,
                            iconsNeedLinear,
                            sortFeaturesByY,
                            bucketLeaderID,
                            std::move(mainInstances),
                            std::move(mainRanges),
                            1.0f,
                            false,
                            {},
                            false /*iconsInText*/};
    mainBucket.bucketInstanceId = ++maxBucketInstanceId;

    OverscaledTileID childID(7, 0, 7, 16, 16);
    std::vector<SymbolInstance> childInstances;
    std::vector<SortKeyRange> childRanges;
    childInstances.push_back(makeSymbolInstance(2000, 2000, u"")); // A'
    childInstances.push_back(makeSymbolInstance(2000, 2000, u"")); // B'
    childInstances.push_back(makeSymbolInstance(2000, 2000, u"")); // C'
    SymbolBucket childBucket{layout,
                             {},
                             16.0f,
                             1.0f,
                             0,
                             iconsNeedLinear,
                             sortFeaturesByY,
                             bucketLeaderID,
                             std::move(childInstances),
                             std::move(childRanges),
                             1.0f,
                             false,
                             {},
                             false /*iconsInText*/};
    childBucket.bucketInstanceId = ++maxBucketInstanceId;

    // assigns new ids
    index.addBucket(mainID, mat4{}, mainBucket);
    ASSERT_EQ(mainBucket.symbolInstances.at(0).getCrossTileID(), 1u);
    ASSERT_EQ(mainBucket.symbolInstances.at(1).getCrossTileID(), 2u);

    // copies parent ids without duplicate ids in this tile
    index.addBucket(childID, mat4{}, childBucket);
    ASSERT_EQ(childBucket.symbolInstances.at(0).getCrossTileID(),
              1u); // A' copies from A
    ASSERT_EQ(childBucket.symbolInstances.at(1).getCrossTileID(),
              2u); // B' copies from B
    ASSERT_EQ(childBucket.symbolInstances.at(2).getCrossTileID(),
              3u); // C' gets new ID
}

TEST(CrossTileSymbolLayerIndex, bucketReplacement) {
    uint32_t maxCrossTileID = 0;
    uint32_t maxBucketInstanceId = 0;
    CrossTileSymbolLayerIndex index(maxCrossTileID);

    Immutable<style::SymbolLayoutProperties::PossiblyEvaluated> layout =
        makeMutable<style::SymbolLayoutProperties::PossiblyEvaluated>();
    bool iconsNeedLinear = false;
    bool sortFeaturesByY = false;
    std::string bucketLeaderID = "test";

    OverscaledTileID tileID(6, 0, 6, 8, 8);
    std::vector<SymbolInstance> firstInstances;
    std::vector<SortKeyRange> firstRanges;
    firstInstances.push_back(makeSymbolInstance(1000, 1000, u"")); // A
    firstInstances.push_back(makeSymbolInstance(1000, 1000, u"")); // B
    SymbolBucket firstBucket{layout,
                             {},
                             16.0f,
                             1.0f,
                             0,
                             iconsNeedLinear,
                             sortFeaturesByY,
                             bucketLeaderID,
                             std::move(firstInstances),
                             std::move(firstRanges),
                             1.0f,
                             false,
                             {},
                             false /*iconsInText*/};
    firstBucket.bucketInstanceId = ++maxBucketInstanceId;

    std::vector<SymbolInstance> secondInstances;
    std::vector<SortKeyRange> secondRanges;
    secondInstances.push_back(makeSymbolInstance(1000, 1000, u"")); // A'
    secondInstances.push_back(makeSymbolInstance(1000, 1000, u"")); // B'
    secondInstances.push_back(makeSymbolInstance(1000, 1000, u"")); // C'
    SymbolBucket secondBucket{layout,
                              {},
                              16.0f,
                              1.0f,
                              0,
                              iconsNeedLinear,
                              sortFeaturesByY,
                              bucketLeaderID,
                              std::move(secondInstances),
                              std::move(secondRanges),
                              1.0f,
                              false,
                              {},
                              false /*iconsInText*/};
    secondBucket.bucketInstanceId = ++maxBucketInstanceId;

    // assigns new ids
    index.addBucket(tileID, mat4{}, firstBucket);
    ASSERT_EQ(firstBucket.symbolInstances.at(0).getCrossTileID(), 1u);
    ASSERT_EQ(firstBucket.symbolInstances.at(1).getCrossTileID(), 2u);

    // copies parent ids without duplicate ids in this tile
    index.addBucket(tileID, mat4{}, secondBucket);
    ASSERT_EQ(secondBucket.symbolInstances.at(0).getCrossTileID(),
              1u); // A' copies from A
    ASSERT_EQ(secondBucket.symbolInstances.at(1).getCrossTileID(),
              2u); // B' copies from B
    ASSERT_EQ(secondBucket.symbolInstances.at(2).getCrossTileID(),
              3u); // C' gets new ID
}

namespace {

void populatePosMatrix(mat4& posMatrix, const OverscaledTileID& tileId, double lat, double lon, double zoom) {
    TransformState transformState;
    transformState.setSize({512, 512});
    transformState.setLatLngZoom(LatLng(lat, lon), zoom);
    transformState.matrixFor(posMatrix, tileId.toUnwrapped());
    matrix::multiply(posMatrix, transformState.getProjectionMatrix(), posMatrix);
}

} // namespace

TEST(CrossTileSymbolLayerIndex, offscreenSymbols) {
    uint32_t maxCrossTileID = 0;
    CrossTileSymbolLayerIndex index(maxCrossTileID);

    Immutable<style::SymbolLayoutProperties::PossiblyEvaluated> layout =
        makeMutable<style::SymbolLayoutProperties::PossiblyEvaluated>();
    bool iconsNeedLinear = false;
    bool sortFeaturesByY = false;
    std::string bucketLeaderID = "test";

    OverscaledTileID tileId(7, 0, 6, 18, 24);
    std::vector<SymbolInstance> mainInstances;
    mainInstances.push_back(makeSymbolInstance(1000, 1000, u"Washington"));
    mainInstances.push_back(makeSymbolInstance(2000, 2000, u"Richmond"));
    std::vector<SortKeyRange> mainRanges;
    SymbolBucket symbolBucket{layout,
                              {},
                              16.0f,
                              1.0f,
                              0,
                              iconsNeedLinear,
                              sortFeaturesByY,
                              bucketLeaderID,
                              std::move(mainInstances),
                              std::move(mainRanges),
                              1.0f,
                              false,
                              {},
                              false /*iconsInText*/};
    mat4 posMatrix;
    populatePosMatrix(posMatrix, tileId, 60.0, 25.0, 7.0);
    index.addBucket(tileId, posMatrix, symbolBucket);

    EXPECT_EQ(symbolBucket.symbolInstances.at(0).getCrossTileID(), SymbolInstance::invalidCrossTileID);
    EXPECT_EQ(symbolBucket.symbolInstances.at(1).getCrossTileID(), SymbolInstance::invalidCrossTileID);

    populatePosMatrix(posMatrix, tileId, 39.0, -76.0, 7.0);
    index.addBucket(tileId, posMatrix, symbolBucket);

    EXPECT_EQ(symbolBucket.symbolInstances.at(0).getCrossTileID(), 1u);
    EXPECT_EQ(symbolBucket.symbolInstances.at(1).getCrossTileID(), 2u);
}

namespace {

std::unique_ptr<SymbolBucket> makeBucket(std::vector<SymbolInstance> instances, uint32_t bucketInstanceId) {
    Immutable<style::SymbolLayoutProperties::PossiblyEvaluated> layout =
        makeMutable<style::SymbolLayoutProperties::PossiblyEvaluated>();
    bool iconsNeedLinear = false;
    bool sortFeaturesByY = false;
    std::string bucketLeaderID = "test";
    std::vector<SortKeyRange> ranges;
    auto bucket = std::unique_ptr<SymbolBucket>(new SymbolBucket{layout,
                                                                 {},
                                                                 16.0f,
                                                                 1.0f,
                                                                 0,
                                                                 iconsNeedLinear,
                                                                 sortFeaturesByY,
                                                                 bucketLeaderID,
                                                                 std::move(instances),
                                                                 std::move(ranges),
                                                                 1.0f,
                                                                 false,
                                                                 {},
                                                                 false /*iconsInText*/});
    bucket->bucketInstanceId = bucketInstanceId;
    return bucket;
}

} // namespace

TEST(CrossTileSymbolLayerIndex, overscaledRegisterKeepsIDsAndReportsAdded) {
    uint32_t maxCrossTileID = 0;
    CrossTileSymbolLayerIndex index(maxCrossTileID);

    const OverscaledTileID tileId(7, 0, 6, 18, 24);
    std::vector<SymbolInstance> instances;
    instances.push_back(makeSymbolInstance(1000, 1000, u"Washington"));
    instances.push_back(makeSymbolInstance(2000, 2000, u"Richmond"));
    // Far to the east of the tile, so it is outside the view of the first matrix
    instances.push_back(makeSymbolInstance(20000, 1000, u"Norfolk"));
    auto bucket = makeBucket(std::move(instances), 1);
    auto& symbols = bucket->symbolInstances;

    mat4 inView;
    populatePosMatrix(inView, tileId, 39.0, -76.0, 7.0);
    mat4 eastView;
    populatePosMatrix(eastView, tileId, 40.5, -65.0, 7.0);
    mat4 offView;
    populatePosMatrix(offView, tileId, 60.0, 25.0, 7.0);

    // 1. First indexing assigns IDs to the two symbols in view
    EXPECT_TRUE(index.addBucket(tileId, inView, *bucket));
    EXPECT_EQ(symbols.at(0).getCrossTileID(), 1u);
    EXPECT_EQ(symbols.at(1).getCrossTileID(), 2u);
    EXPECT_EQ(symbols.at(2).getCrossTileID(), SymbolInstance::invalidCrossTileID);
    const uint64_t firstGeneration = index.getGeneration();
    EXPECT_NE(firstGeneration, 0u);

    // 2. The second run is the settling one: same IDs, same generation
    EXPECT_TRUE(index.addBucket(tileId, inView, *bucket));
    EXPECT_EQ(symbols.at(0).getCrossTileID(), 1u);
    EXPECT_EQ(symbols.at(1).getCrossTileID(), 2u);
    EXPECT_EQ(symbols.at(2).getCrossTileID(), SymbolInstance::invalidCrossTileID);
    EXPECT_EQ(index.getGeneration(), firstGeneration);

    // 3. The third call is a skip: same result, same generation, no new ID taken
    EXPECT_TRUE(index.addBucket(tileId, inView, *bucket));
    EXPECT_EQ(symbols.at(0).getCrossTileID(), 1u);
    EXPECT_EQ(symbols.at(1).getCrossTileID(), 2u);
    EXPECT_EQ(symbols.at(2).getCrossTileID(), SymbolInstance::invalidCrossTileID);
    EXPECT_EQ(index.getGeneration(), firstGeneration);
    EXPECT_EQ(maxCrossTileID, 2u);

    // 4. A matrix under which the third symbol is in view (and the first two are not)
    EXPECT_TRUE(index.addBucket(tileId, eastView, *bucket));
    EXPECT_EQ(symbols.at(0).getCrossTileID(), SymbolInstance::invalidCrossTileID);
    EXPECT_EQ(symbols.at(1).getCrossTileID(), SymbolInstance::invalidCrossTileID);
    EXPECT_EQ(symbols.at(2).getCrossTileID(), 3u);
    EXPECT_NE(index.getGeneration(), firstGeneration);

    // 5. Off view: every ID is invalid, as in the offscreenSymbols test
    EXPECT_TRUE(index.addBucket(tileId, offView, *bucket));
    for (const auto& symbol : symbols) {
        EXPECT_EQ(symbol.getCrossTileID(), SymbolInstance::invalidCrossTileID);
    }
}

namespace {

// The cross-tile index as it was before the re-index skip, kept as the reference for the differential test.
class LegacyLayerIndex {
public:
    LegacyLayerIndex(uint32_t& maxCrossTileID_)
        : maxCrossTileID(maxCrossTileID_) {}

    bool addBucket(const OverscaledTileID& tileID, const mat4& tileMatrix, SymbolBucket& bucket) {
        auto& thisZoomIndexes = indexes[tileID.overscaledZ];
        auto previousIndex = thisZoomIndexes.find(tileID);
        if (previousIndex != thisZoomIndexes.end()) {
            if (previousIndex->second.bucketInstanceId == bucket.bucketInstanceId &&
                !bucket.hasUninitializedSymbols) {
                return false;
            } else {
                removeBucketCrossTileIDs(tileID.overscaledZ, previousIndex->second);
            }
        }

        bucket.hasUninitializedSymbols = false;

        if (tileID.overscaleFactor() > 1u) {
            for (auto& symbolInstance : bucket.symbolInstances) {
                if (symbolInstance.check(SYM_GUARD_LOC) &&
                    isInViewport(tileMatrix, symbolInstance.getAnchor().point)) {
                    symbolInstance.setCrossTileID(0u);
                } else {
                    symbolInstance.setCrossTileID(SymbolInstance::invalidCrossTileID);
                    bucket.hasUninitializedSymbols = true;
                }
            }
        } else {
            for (auto& symbolInstance : bucket.symbolInstances) {
                symbolInstance.setCrossTileID(0u);
            }
        }

        auto& thisZoomUsedCrossTileIDs = usedCrossTileIDs[tileID.overscaledZ];

        for (auto& it : indexes) {
            auto zoom = it.first;
            const auto& zoomIndexes = it.second;
            if (zoom > tileID.overscaledZ) {
                for (auto& childIndex : zoomIndexes) {
                    if (childIndex.second.coord.isChildOf(tileID)) {
                        childIndex.second.findMatches(bucket, tileID, thisZoomUsedCrossTileIDs);
                    }
                }
            } else {
                auto parentTileID = tileID.scaledTo(zoom);
                auto parentIndex = zoomIndexes.find(parentTileID);
                if (parentIndex != zoomIndexes.end()) {
                    parentIndex->second.findMatches(bucket, tileID, thisZoomUsedCrossTileIDs);
                }
            }
        }

        for (auto& symbolInstance : bucket.symbolInstances) {
            if (symbolInstance.check(SYM_GUARD_LOC) && !symbolInstance.getCrossTileID()) {
                symbolInstance.setCrossTileID(++maxCrossTileID);
                thisZoomUsedCrossTileIDs.insert(symbolInstance.getCrossTileID());
            }
        }

        thisZoomIndexes.erase(tileID);
        thisZoomIndexes.emplace(
            std::piecewise_construct,
            std::forward_as_tuple(tileID),
            std::forward_as_tuple(tileID, bucket.symbolInstances, bucket.bucketInstanceId, bucket.bucketLeaderID));
        return true;
    }

    bool removeStaleBuckets(const std::unordered_set<uint32_t>& currentIDs) {
        bool tilesChanged = false;
        for (auto& zoomIndexes : indexes) {
            for (auto it = zoomIndexes.second.begin(); it != zoomIndexes.second.end();) {
                if (!currentIDs.contains(it->second.bucketInstanceId)) {
                    removeBucketCrossTileIDs(zoomIndexes.first, it->second);
                    it = zoomIndexes.second.erase(it);
                    tilesChanged = true;
                } else {
                    ++it;
                }
            }
        }
        return tilesChanged;
    }

    void handleWrapJump(float newLng) {
        const auto wrapDelta = static_cast<int>(std::round((newLng - lng) / 360.0f));
        if (wrapDelta != 0) {
            std::map<uint8_t, std::map<OverscaledTileID, TileLayerIndex>> newIndexes;
            for (auto& zoomIndex : indexes) {
                std::map<OverscaledTileID, TileLayerIndex> newZoomIndex;
                for (auto& index : zoomIndex.second) {
                    index.second.coord = index.second.coord.unwrapTo(index.second.coord.wrap + wrapDelta);
                    newZoomIndex.emplace(index.second.coord, std::move(index.second));
                }
                newIndexes.emplace(zoomIndex.first, std::move(newZoomIndex));
            }

            indexes = std::move(newIndexes);
        }

        lng = newLng;
    }

private:
    static bool isInViewport(const mat4& posMatrix, const Point<float>& point) {
        vec4 p = {{point.x, point.y, 0, 1}};
        matrix::transformMat4(p, p, posMatrix);

        constexpr double buffer = 1.0;
        constexpr double edge = 1.0 + buffer;
        const double x = p[0] / p[3];
        const double y = p[1] / p[3];
        return (x > -edge && y > -edge && x < edge && y < edge);
    }

    void removeBucketCrossTileIDs(uint8_t zoom, const TileLayerIndex& removedBucket) {
        for (const auto& key : removedBucket.indexedSymbolInstances) {
            for (auto indexedSymbolInstance : key.second) {
                usedCrossTileIDs[zoom].erase(indexedSymbolInstance.crossTileID);
            }
        }
    }

    std::map<uint8_t, std::map<OverscaledTileID, TileLayerIndex>> indexes;
    std::map<uint8_t, std::set<uint32_t>> usedCrossTileIDs;
    float lng = 0;
    uint32_t& maxCrossTileID;
};

template <class Index>
struct IndexWorld {
    uint32_t maxCrossTileID = 0;
    Index index{maxCrossTileID};
    std::array<std::unique_ptr<SymbolBucket>, 5> buckets;
};

struct SymbolSpec {
    float x;
    float y;
    std::u16string key;
};

struct View {
    double lat;
    double lon;
    double zoom;
};

struct LastAdd {
    uint32_t bucketID = 0;
    std::size_t view = 0;
    uint64_t generationBefore = 0;
    uint64_t generationAfter = 0;
};

} // namespace

TEST(CrossTileSymbolLayerIndex, reindexSkipMatchesLegacy) {
    constexpr std::size_t tileCount = 5;
    // One z6 parent, three overscaled tiles of the same canonical tile (two overscaled zooms and a sibling),
    // and one real z7 child.
    const std::array<OverscaledTileID, tileCount> baseTiles = {OverscaledTileID(6, 0, 6, 18, 24),
                                                               OverscaledTileID(7, 0, 6, 18, 24),
                                                               OverscaledTileID(8, 0, 6, 18, 24),
                                                               OverscaledTileID(7, 0, 6, 19, 24),
                                                               OverscaledTileID(7, 0, 7, 36, 48)};
    constexpr std::array<View, 6> views = {View{39.0, -76.0, 7.0},
                                       View{39.4, -75.2, 7.0},
                                       View{38.0, -77.0, 7.5},
                                       View{60.0, 25.0, 7.0},
                                       View{40.5, -74.0, 8.0},
                                       View{39.0, -76.0, 9.0}};

    std::array<std::array<mat4, views.size()>, tileCount> matrices;
    for (std::size_t t = 0; t < tileCount; ++t) {
        for (std::size_t v = 0; v < views.size(); ++v) {
            populatePosMatrix(matrices[t][v], baseTiles[t], views[v].lat, views[v].lon, views[v].zoom);
        }
    }

    std::mt19937 rng(20241114);
    auto pick = [&rng](std::size_t bound) { return static_cast<std::size_t>(rng() % bound); };

    // A shared pool of anchors, so symbols of different tiles and levels often match and often sit within
    // the matching tolerance of each other.
    std::array<std::array<float, 2>, 12> pool{};
    for (auto& point : pool) {
        point = {static_cast<float>(pick(8192)), static_cast<float>(pick(8192))};
    }
    const std::array<std::u16string, 3> keys = {u"Alpha", u"Beta", u"Gamma"};

    auto makeSpecs = [&]() {
        std::vector<SymbolSpec> specs;
        const std::size_t count = 6 + pick(5);
        for (std::size_t i = 0; i < count; ++i) {
            SymbolSpec spec;
            if (pick(6) == 0) {
                spec.x = static_cast<float>(pick(8192));
                spec.y = static_cast<float>(pick(8192));
            } else {
                const auto& point = pool[pick(pool.size())];
                const bool jitter = pick(2) == 0;
                spec.x = point[0] + (jitter ? static_cast<float>(pick(61)) - 30.0f : 0.0f);
                spec.y = point[1] + (jitter ? static_cast<float>(pick(61)) - 30.0f : 0.0f);
            }
            spec.key = keys[pick(keys.size())];
            specs.push_back(std::move(spec));
        }
        return specs;
    };

    auto makeInstances = [](const std::vector<SymbolSpec>& specs) {
        std::vector<SymbolInstance> instances;
        for (const auto& spec : specs) {
            instances.push_back(makeSymbolInstance(spec.x, spec.y, spec.key));
        }
        return instances;
    };

    IndexWorld<LegacyLayerIndex> legacy;
    IndexWorld<CrossTileSymbolLayerIndex> current;
    std::array<uint32_t, tileCount> bucketIDs{}; // 0: no bucket for this tile
    std::array<LastAdd, tileCount> lastAdds;
    uint32_t lastBucketID = 0;
    int16_t wrap = 0;
    float lng = 0.0f;
    std::size_t currentView = 0;
    std::size_t reAdds = 0;
    std::size_t likelySkips = 0;

    auto freshBucket = [&](std::size_t t) {
        const auto specs = makeSpecs();
        bucketIDs[t] = ++lastBucketID;
        legacy.buckets[t] = makeBucket(makeInstances(specs), bucketIDs[t]);
        current.buckets[t] = makeBucket(makeInstances(specs), bucketIDs[t]);
    };

    std::string failure;
    auto addBucket = [&](std::size_t t, std::size_t view) {
        if (bucketIDs[t] == 0) {
            freshBucket(t);
        }
        const OverscaledTileID tileID = baseTiles[t].unwrapTo(wrap);
        const bool legacyResult = legacy.index.addBucket(tileID, matrices[t][view], *legacy.buckets[t]);
        const uint64_t generationBefore = current.index.getGeneration();
        const bool currentResult = current.index.addBucket(tileID, matrices[t][view], *current.buckets[t]);
        const uint64_t generationAfter = current.index.getGeneration();
        if (legacyResult != currentResult && failure.empty()) {
            failure = "addBucket returned different values";
        }
        if (currentResult) {
            ++reAdds;
            const LastAdd& last = lastAdds[t];
            // The previous indexing of this very bucket left the generation alone (so it was settled), nothing
            // in the layer changed since, and the matrix is the same: the new index must skip this call.
            if (last.bucketID == bucketIDs[t] && last.view == view && last.generationBefore == last.generationAfter &&
                generationBefore == last.generationAfter) {
                ++likelySkips;
            }
        }
        lastAdds[t] = {bucketIDs[t], view, generationBefore, generationAfter};
    };

    auto compareWorlds = [&]() {
        if (!failure.empty()) {
            return;
        }
        if (legacy.maxCrossTileID != current.maxCrossTileID) {
            failure = "maxCrossTileID differs";
            return;
        }
        for (std::size_t t = 0; t < tileCount; ++t) {
            if (bucketIDs[t] == 0) {
                continue;
            }
            const auto& expected = legacy.buckets[t]->symbolInstances;
            const auto& actual = current.buckets[t]->symbolInstances;
            if (expected.size() != actual.size()) {
                failure = "symbol count differs in tile " + std::to_string(t);
                return;
            }
            for (std::size_t i = 0; i < expected.size(); ++i) {
                if (expected[i].getCrossTileID() != actual[i].getCrossTileID()) {
                    failure = "cross-tile ID differs in tile " + std::to_string(t) + " symbol " + std::to_string(i);
                    return;
                }
            }
        }
    };

    for (std::size_t t = 0; t < tileCount; ++t) {
        addBucket(t, currentView);
    }
    compareWorlds();
    ASSERT_TRUE(failure.empty()) << failure;

    constexpr std::size_t operations = 20000;
    for (std::size_t n = 0; n < operations; ++n) {
        const std::size_t choice = pick(100);
        const std::size_t t = pick(tileCount);
        if (choice < 70) {
            // Same matrix again, as on every frame while the camera barely moves
            addBucket(t, currentView);
        } else if (choice < 80) {
            currentView = pick(views.size());
        } else if (choice < 88) {
            addBucket(t, pick(views.size()));
        } else if (choice < 93) {
            freshBucket(t);
            addBucket(t, currentView);
        } else if (choice < 97) {
            std::unordered_set<uint32_t> currentIDs;
            for (std::size_t i = 0; i < tileCount; ++i) {
                if (bucketIDs[i] != 0 && pick(5) != 0) {
                    currentIDs.insert(bucketIDs[i]);
                }
            }
            const bool legacyResult = legacy.index.removeStaleBuckets(currentIDs);
            const bool currentResult = current.index.removeStaleBuckets(currentIDs);
            if (legacyResult != currentResult && failure.empty()) {
                failure = "removeStaleBuckets returned different values";
            }
            for (std::size_t i = 0; i < tileCount; ++i) {
                if (bucketIDs[i] != 0 && !currentIDs.contains(bucketIDs[i])) {
                    bucketIDs[i] = 0;
                    legacy.buckets[i].reset();
                    current.buckets[i].reset();
                }
            }
        } else if (choice < 98) {
            const int16_t delta = pick(2) == 0 ? 1 : -1;
            wrap = static_cast<int16_t>(wrap + delta);
            lng += 360.0f * static_cast<float>(delta);
            legacy.index.handleWrapJump(lng);
            current.index.handleWrapJump(lng);
        } else {
            // A bucket with several sort-key ranges is added several times in one frame
            addBucket(t, currentView);
            addBucket(t, currentView);
        }
        compareWorlds();
        ASSERT_TRUE(failure.empty()) << "operation " << n << ": " << failure;
    }

    std::printf("[   INFO   ] reindexSkipMatchesLegacy: %zu of %zu indexing calls were skips (%.1f%%)\n",
                likelySkips,
                reAdds,
                reAdds == 0 ? 0.0 : 100.0 * static_cast<double>(likelySkips) / static_cast<double>(reAdds));
    EXPECT_GT(likelySkips, 0u);
}
