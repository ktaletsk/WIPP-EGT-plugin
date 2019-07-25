// NIST-developed software is provided by NIST as a public service. 
// You may use, copy and distribute copies of the  software in any  medium, 
// provided that you keep intact this entire notice. You may improve, 
// modify and create derivative works of the software or any portion of the 
// software, and you may copy and distribute such modifications or works. 
// Modified works should carry a notice stating that you changed the software 
// and should note the date and nature of any such change. Please explicitly 
// acknowledge the National Institute of Standards and Technology as the 
// source of the software.
// NIST-developed software is expressly provided "AS IS." NIST MAKES NO WARRANTY
// OF ANY KIND, EXPRESS, IMPLIED, IN FACT  OR ARISING BY OPERATION OF LAW, 
// INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTY OF MERCHANTABILITY, 
// FITNESS FOR A PARTICULAR PURPOSE, NON-INFRINGEMENT AND DATA ACCURACY. NIST 
// NEITHER REPRESENTS NOR WARRANTS THAT THE OPERATION  OF THE SOFTWARE WILL 
// BE UNINTERRUPTED OR ERROR-FREE, OR THAT ANY DEFECTS WILL BE CORRECTED. NIST 
// DOES NOT WARRANT  OR MAKE ANY REPRESENTATIONS REGARDING THE USE OF THE 
// SOFTWARE OR THE RESULTS THEREOF, INCLUDING BUT NOT LIMITED TO THE 
// CORRECTNESS, ACCURACY, RELIABILITY, OR USEFULNESS OF THE SOFTWARE.
// You are solely responsible for determining the appropriateness of using 
// and distributing the software and you assume  all risks associated with 
// its use, including but not limited to the risks and costs of program 
// errors, compliance  with applicable laws, damage to or loss of data, 
// programs or equipment, and the unavailability or interruption of operation. 
// This software is not intended to be used in any situation where a failure 
// could cause risk of injury or damage to property. The software developed 
// by NIST employees is not subject to copyright protection within 
// the United States.

/// @file BlobMerger.h
/// @author Alexandre Bardakoff - Timothy Blattner
/// @date  4/6/17
/// @brief Blob merger task


#ifndef FEATURECOLLECTION_BLOBMERGER_H
#define FEATURECOLLECTION_BLOBMERGER_H

#include <tiffio.h>
#include <htgs/api/ITask.hpp>
#include <cstdint>
#include <utility>
#include <FastImage/FeatureCollection/tools/UnionFind.h>
#include <egt/FeatureCollection/Data/ListBlobs.h>


namespace egt {
/// \namespace egt EGT namespace

/**
  * @class BlobMerger BlobMerger.h <FastImage/FeatureCollection/Tasks/BlobMerger.h>
  *
  * @brief Merges multiple egt::ViewAnalyse to build the Feature Collection.
  *
  *  Merge the different analyse with a disjoint-set data structure to represent
  *  each blobs and merge them easily:
  *  https://en.wikipedia.org/wiki/Disjoint-set_data_structure

  **/
class BlobMerger : public htgs::ITask<ViewAnalyse, ListBlobs> {
 public:
  /// \brief BlobMerger constructor
  /// \param imageHeight Image Height
  /// \param imageWidth ImageWidth
  /// \param nbTiles Number of tiles in the image
  BlobMerger(uint32_t imageHeight, uint32_t imageWidth, uint32_t nbTiles)
      : ITask(1), _nbTiles(nbTiles) {
    _blobs = new ListBlobs();
  }

  /// \brief Get the view analyse, merge them into one analyse and merge all 
  /// blob to build the feature collection
  /// \param data View analyse
  void executeTask(std::shared_ptr<ViewAnalyse> data) override {
    // Merge the analyse
    //TODO CANT WE JUST COPY BACK?. MERGE SEEMS TO HAVE A NOTION OF ORDERING? http://www.cplusplus.com/reference/list/list/merge/
    for (auto blobListCoordToMerge : data->getToMerge()) {
      this->_toMerge[blobListCoordToMerge.first]
          .merge(blobListCoordToMerge.second);
    }
    auto viewBlob = data->getBlobs();
    this->_blobs->_blobs.merge(viewBlob);

    _count++;

    // If all the analyse has been collected, merge the blobs
    if (_count == _nbTiles) {

      auto startMerge = std::chrono::high_resolution_clock::now();


        VLOG(1) << "merging " << this->_blobs->_blobs.size() << " blobs...";

//        for(auto blob: this->_blobs->_blobs){
//            VLOG(5) << *blob;
//        }
      _count = 0;
      merge();

      VLOG(1) << "after last merge, we have : " << _blobs->_blobs.size() << " blobs left";

      auto endMerge = std::chrono::high_resolution_clock::now();
      VLOG(1) << "    Merge blobs: " << std::chrono::duration_cast<std::chrono::milliseconds>(endMerge - startMerge).count() << " mS";

      this->addResult(_blobs);
    }
  }

  /// \brief Get the name of the task
  /// \return Task name
  std::string getName() override { return "Merge & File creation"; }

  /// \brief Task copy, should be a singleton, to send bask itself
  /// \return itself
  BlobMerger *copy() override { return this; }

 private:
  /// \brief Retrieve a blob from a coordinate, nullptr is to blob is
  /// corresponding
  /// \param row Row
  /// \param col Column
  /// \return True if a blob has been found, else False
  Blob *getBlobFromCoord(const int32_t &row, const int32_t &col) const {
    // Iterate over the blobs
    for (auto blob : _blobs->_blobs) {
      // Test if the pixel is in the blob
      if(blob->getFeature()->isInBitMask(row,col)) {
        return blob;
      }
//
//      if (blob->isPixelinFeature(row, col)) {
//        return blob;
//      }
    }
    return nullptr;
  }

  /// \brief Merge all the blobs from all the view analyser
  void merge() {
    fc::UnionFind<Blob>
        uf{};

    std::map<Blob *, std::set<Blob *>>
        parentSons{};

    // Apply the UF algorithm to every linked blob
    for (auto blobCoords : _toMerge) {
      for (auto coord : blobCoords.second) {
          if(auto other = getBlobFromCoord(coord.first, coord.second)) {
            uf.unionElements(blobCoords.first, other);
          }
      }
    }

    // Clear to merge data stucture
    _toMerge.clear();

    //TODO WE DO FOR EVERY BLOB, WE SHOULD NOT
    // Associate every blob to it parent
    for (auto blob : _blobs->_blobs) {
      parentSons[uf.find(blob)].insert(blob);
    }

    // For every Parent - son blob, merge every parent to it son
    for (auto pS : parentSons) {
      auto parent = pS.first;
      auto sons = pS.second;
      VLOG(5) << "nb of sons: " << sons.size();
      Blob
          *toMerge = *sons.begin(),
          *merged = nullptr;

      auto bb = calculateBoundingBox(sons);
      double size = ceil((bb->getHeight() * bb->getWidth()) / 32.);
      uint32_t* bitMask = new uint32_t[(uint32_t) size]();

      parent->addToBitMask(bitMask, bb);

      auto *f = new Feature(parent->getTag(), *bb, bitMask);
      VLOG(0) << (*f);

      for (auto son = std::next(sons.begin()); son != sons.end(); ++son) {


        (*son)->addToBitMask(bitMask, bb);
     //   delete (*son);
        _blobs->_blobs.remove(*son);
      }


      delete parent->getFeature();
      auto *feature = new Feature(parent->getTag(), *bb, bitMask);

      VLOG(3) << "Blob merged: ";
      VLOG(3) << (*feature);

      parent->setFeature(feature);
    }
  }

  BoundingBox* calculateBoundingBox(std::set<Blob *> sons) {

    uint32_t  upperLeftRow = std::numeric_limits<int32_t>::max(),
              upperLeftCol = std::numeric_limits<int32_t>::max(),
              bottomRightRow = 0,
              bottomRightCol = 0;

    for (auto son = sons.begin(); son != sons.end(); ++son) {
      auto bb = (*son)->getFeature()->getBoundingBox();
      upperLeftRow = (bb.getUpperLeftRow() < upperLeftRow) ? bb.getUpperLeftRow() : upperLeftRow;
      upperLeftCol = (bb.getUpperLeftCol() < upperLeftCol) ? bb.getUpperLeftCol() : upperLeftCol;
      bottomRightRow = (bb.getBottomRightRow() > bottomRightRow) ? bb.getBottomRightRow() : bottomRightRow;
      bottomRightCol = bb.getBottomRightCol() > bottomRightCol ? bb.getBottomRightCol() : bottomRightCol;
    }

    return new BoundingBox(upperLeftRow, upperLeftCol, bottomRightRow, bottomRightCol);
  }

  uint32_t
      _nbTiles = 0;                 ///< Images number of tiles

  std::map<Blob *, std::list<Coordinate >>
      _toMerge{};                   ///< Merge structure

  ListBlobs *
      _blobs;                       ///< Blobs list

  uint32_t
      _count = 0;                   ///< Number of views analysed
};
}

#endif //FEATURECOLLECTION_BLOBMERGER_H
