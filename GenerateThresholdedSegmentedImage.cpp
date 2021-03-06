//
// Created by gerardin on 9/26/19.
//

#include <string>
#include <egt/loaders/PyramidTiledTiffLoader.h>
#include <FastImage/api/FastImage.h>
#include <egt/FeatureCollection/Tasks/BlobMerger.h>
#include <egt/FeatureCollection/Tasks/FeatureCollection.h>
#include <egt/FeatureCollection/Tasks/ViewAnalyseFilter.h>
#include <egt/api/DataTypes.h>
#include <egt/tasks/ThresholdFinder.h>
#include <egt/tasks/SobelFilterOpenCV.h>
#include <egt/tasks/FCSobelFilterOpenCV.h>
#include <htgs/log/TaskGraphSignalHandler.hpp>
#include <egt/tasks/CustomSobelFilter3by3.h>
#include <egt/tasks/FCCustomSobelFilter3by3.h>
#include <egt/memory/TileAllocator.h>
#include <egt/FeatureCollection/Tasks/ViewFilter.h>
#include <egt/tasks/TiffTileWriter.h>
#include <egt/api/EGTOptions.h>
#include <random>
#include <egt/tasks/EGTSobelFilter.h>
#include <egt/tasks/NoTransform.h>
#include <egt/FeatureCollection/Tasks/EGTGradientViewAnalyzer.h>
#include <experimental/filesystem>
#include <egt/utils/PixelIntensityBoundsFinder.h>
#include <egt/utils/pyramid/Pyramid.h>
#include <egt/utils/pyramid/RecursiveBlockTraversal.h>
#include <egt/rules/MergeRule.h>
#include <egt/FeatureCollection/Tasks/MergeBlob.h>
#include <egt/rules/MergeCompletedRule.h>
#include <egt/FeatureCollection/Tasks/FeatureBuilder.h>
#include <egt/tasks/ConvTask.h>
#include <egt/tasks/OpenCVConvTask.h>

using namespace egt;

typedef uint16_t T;


template <class T>
class Thresholder : public  htgs::ITask<htgs::MemoryData<fi::View<T>>,  htgs::MemoryData<fi::View<T>>> {

public:

    explicit Thresholder(size_t numThreads, T threshold) : ITask<htgs::MemoryData<fi::View<T>>,  htgs::MemoryData<fi::View<T>>>(numThreads), threshold(threshold) {}

    void executeTask(std::shared_ptr<MemoryData<fi::View<T>>> data) override {

        auto maxVal = std::numeric_limits<T>::max();
        auto view = data->get();

        for(auto i = 0 ; i < view->getTileHeight(); i++) {
            for(auto j = 0 ; j < view->getTileWidth(); j++) {
                auto val = view->getPixel(i,j);
                view->setPixel(i,j, (val > threshold) ? maxVal : 0);
            }
        }
        this->addResult(data);

    }

    ITask<MemoryData<fi::View<T>>, MemoryData<fi::View<T>>> *copy() override {
        return new Thresholder(this->getNumThreads(), threshold);
    }

    T threshold;

};

/// \brief Create a gaussian kernel used by openCV
/// \param radiusKernel Kernel radius
/// \param sigma Gaussian sigma
/// \return Gaussian kernel in a vector
std::vector<T> gaussian(uint32 radiusKernel, double sigma) {
    std::vector<T> kernel;
    for (int32_t i = -radiusKernel; i <= (int32_t) radiusKernel; ++i) {
        for (int32_t j = -radiusKernel; j <= (int32_t) radiusKernel; ++j) {
            kernel.push_back((T) ((exp(-((i * i + j * j) / (2 * sigma * sigma)))) / (2 * M_PI * sigma * sigma)));
        }
    }
    return kernel;
}



int main(int argc, const char **argv) {


    VLOG(3) << "ok";

    auto inputPath = "/home/gerardin/Documents/images/egt-test-images/egt_test/inputs/phase_image_002_tiled256_pyramid.tif";

//    auto inputPath = "/home/gerardin/Documents/images/egt-test-images/dataset02/tiled/dataset02_tiled16.tif";
//    auto outputPath = "/home/gerardin/Documents/projects/egt++/outputs/tiled_stitched_c01t020p1_pyramid_1024.ome.tif";
//    auto inputPath = "/home/gerardin/Documents/images/egt-test-images/dataset01/images/test01-tiled.tif";
//    auto inputPath = "/home/gerardin/Documents/images/dataset2/images/tiled_stitched_c01t020p1_pyramid_1024.ome.tif";
//    auto inputPath = "/home/gerardin/Documents/images/dataset2/images/tiled_stitched_c01t020p1_pyramid_1024.ome.tif";

    auto outputPath = "/home/gerardin/Documents/projects/egt++/outputs/thresholdedGradientImage_phase_image_002_tiled256_pyramid.tif";

    uint32_t concurrentTiles = 1;
    size_t nbLoaderThreads = 1;

//    auto imageDepth = ImageDepth::_8U;
    auto imageDepth = ImageDepth::_16U;

    auto threshold = 108;
//    auto threshold = 87;

    auto graph = new htgs::TaskGraphConf<htgs::MemoryData<fi::View<T>>, htgs::VoidData>();
    auto runtime = new htgs::TaskGraphRuntime(graph);

    uint32_t level = 0;
    uint32_t radius = 2;

    auto loader = new PyramidTiledTiffLoader<T>(inputPath, nbLoaderThreads);
    auto *fi = new fi::FastImage<T>(loader, radius);
    fi->getFastImageOptions()->setNumberOfViewParallel(concurrentTiles);
    auto fastImage = fi->configureAndMoveToTaskGraphTask("Fast Image");
    auto imageHeight = fi->getImageHeight(level);
    auto imageWidth = fi->getImageWidth(level);
    auto tileSize = fi->getTileHeight(level);
    uint32_t nbTiles = fi->getNumberTilesHeight(level) *
                       fi->getNumberTilesWidth(level);

    auto pyramid = pb::Pyramid(fi->getImageWidth(), fi->getImageHeight(), fi->getTileWidth());


    auto sobelFilter = new FCCustomSobelFilter3by3<T>(concurrentTiles, imageDepth, 1, 1);

    auto thresholder = new Thresholder<T>(concurrentTiles, threshold);

    auto tiffTileWriter = new TiffTileWriter<T>(1, imageHeight, imageWidth, tileSize, imageDepth, outputPath);

    graph->addEdge(fastImage, sobelFilter);

    //PERFORM GRADIENT AND THRESHOLDING
        graph->addEdge(sobelFilter, thresholder);
        graph->addEdge(thresholder, tiffTileWriter);

    runtime->executeRuntime();
    fi->requestAllTiles(true, level);


    auto traversal = new pb::RecursiveBlockTraversal(pyramid);
    for (auto step : traversal->getTraversal()) {
        auto row = step.first;
        auto col = step.second;
        fi->requestTile(row, col, false, 0);
        VLOG(4) << "Requesting tile (" << row << "," << col << ")";
    }

    runtime->waitForRuntime();
    delete traversal;

   // delete fi;
}