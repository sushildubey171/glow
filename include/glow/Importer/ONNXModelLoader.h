/**
 * Copyright (c) Glow Contributors. See CONTRIBUTORS file.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef GLOW_IMPORTER_ONNXMODELLOADER_H
#define GLOW_IMPORTER_ONNXMODELLOADER_H

#include "glow/Graph/Graph.h"
#include "glow/Importer/CommonOperatorLoader.h"

#include "onnx/onnx_pb.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"

#include "google/protobuf/io/coded_stream.h"
#include "google/protobuf/io/zero_copy_stream_impl.h"
#include <fstream>
#include <string>
#include <unordered_set>

namespace ONNX_NAMESPACE {
class AttributeProto;
class NodeProto;
class GraphProto;
class ModelProto;
class TensorProto;
} // namespace ONNX_NAMESPACE

namespace glow {

/// Loads tensor \p T from the input \p in. \p useGlowCustomOps changes the
/// format for doc_string format for adding meta information.
Error loadTensor(const ONNX_NAMESPACE::TensorProto &in, Tensor *T,
                 bool useGlowCustomOps = false);

/// Parses as input file name \p fileName which is an ONNX file
/// and \returns a parsed GraphProto.
ONNX_NAMESPACE::GraphProto parseOnnxFile(const std::string &fileName);

/// Takes an ONNX file in \p fileName reads it and loads the tensors
/// in \p bindings. If the tensors loaded from the underlying file
/// Are smaller than what the placeholder for that tensor expects it gets
/// Padded with 0 if \p partialTensorPayloads is nullptr other wise
/// \p partialTensorPayloads holds the data for full tensors.
/// If \p usingGlowCustomOps then the custom Glow ONNX format will be
/// expected/used to load from the ONNX file.
void fillPlaceholders(const std::string &fileName,
                      PlaceholderBindings *bindings,
                      std::vector<Tensor> *partialTensorPayloads = nullptr,
                      bool usingGlowCustomOps = false);

/// Override that takes \p parsedFile as a parsed file instead of file name.
void fillPlaceholders(const ONNX_NAMESPACE::GraphProto &parsedFile,
                      PlaceholderBindings *bindings,
                      std::vector<Tensor> *partialTensorPayloads = nullptr,
                      bool usingGlowCustomOps = false);

/// Define undefined symbols to \p str loaded from an ONNX proto. See
/// onnxDefineSymbolOpt in ONNXModelLoader.cpp.
void setOnnxDefineSymbol(const std::vector<std::string> &lst);

/// Loads ONNX models.
class ONNXModelLoader
    : public CommonOperatorLoader<ONNX_NAMESPACE::NodeProto,
                                  ONNX_NAMESPACE::AttributeProto> {
  /// \returns True if the operator has broadcasting activated.
  Expected<bool> getBroadcast(ArgumentDictionaryTy &dict) override;

  /// \returns True if the operator with the name \p typeName has support for
  /// multidirectional broadcasting.
  bool hasMultidirectionalBroadcast(const llvm::StringRef typeName) override;

  /// Converts a ONNX TensorProto DataType enum to the Glow element type.
  /// Supports only non quantized and signed types.
  Expected<ElemKind>
  convertTensorProtoDataType(ONNX_NAMESPACE::TensorProto_DataType t);

  /// Load the operator \p op into the network. This creates one or more nodes
  /// in the network. \returns Error if operator \p op cannot be loaded.
  Error loadOperator(const ONNX_NAMESPACE::NodeProto &op);

  /// \returns a TypeRef found in \p dict which is loaded and uniqued into the
  /// Module. The TypeRef is represented in the ONNX proto by concatenating the
  /// relevant members of a type, (ElemKind, Shape, and Scale and Offset if
  /// ElemKind is quantized) with \p resNo.
  Expected<TypeRef> loadTypeFromAttributes(unsigned resNo,
                                           ArgumentDictionaryTy &dict);

  /// If this is a custom Glow op that was exported via NodeGen automatic export
  /// logic, try to load the op. \returns Expected<true> if the op is
  /// successfully loaded. \returns Expected<false> if op type is not supported.
  /// \returns an Error if an error occurred while trying to load, or otherwise
  /// the single Node that was created.
  Expected<Node *> tryLoadGlowCustomOp(llvm::StringRef typeName,
                                       const ONNX_NAMESPACE::NodeProto &op,
                                       ArgumentDictionaryTy &dict);

  /// \returns True if the operator\ op is successfully folded.
  Expected<bool> foldOperator(const ONNX_NAMESPACE::NodeProto &op);

  /// ONNX model ir_version;
  size_t irVersion_;

  /// ONNX model op_version;
  size_t opsetVersion_;

  /// Whether we're loading an ONNX file exported using Glow custom ops.
  bool useGlowCustomOps_{false};

  /// A set of inputs which will be static placeholders.
  std::unordered_set<std::string> staticInputs_;

  /// Load Constant ONNX operator.
  Error loadConstant(const ONNX_NAMESPACE::NodeProto &op,
                     ArgumentDictionaryTy &dict);

  /// Load Slice ONNX operator.
  Error loadSlice(const ONNX_NAMESPACE::NodeProto &op,
                  ArgumentDictionaryTy &dict);

  /// Load Conv ONNX operator.
  Error loadConv(const ONNX_NAMESPACE::NodeProto &op,
                 ArgumentDictionaryTy &dict);

  /// Load ChannelwiseQuantizedConvolution Glow operator.
  Error loadChannelwiseQuantizedConvolution(const ONNX_NAMESPACE::NodeProto &op,
                                            ArgumentDictionaryTy &dict);

  /// Load Glow conv operator with quantized inputs. Since this isn't a normal
  /// part of the ops supported by onnx, the assumption is that this op was
  /// produced by Glow's on ONNXModelWriter and thus has NHWC layout for inputs.
  Error loadTensorwiseQuantizedConvolution(const ONNX_NAMESPACE::NodeProto &op,
                                           ArgumentDictionaryTy &dict);
  /// Load ConvTranspose ONNX operator.
  Error loadConvTranspose(const ONNX_NAMESPACE::NodeProto &op,
                          ArgumentDictionaryTy &dict);

  /// Load Conv1D operator.
  /// As per conv operation definition at
  /// https://github.com/onnx/onnx/blob/master/docs/Operators.md#Conv ,
  /// input is in format (NxCxD1x...xDn). If the input tensor dimension size is
  /// 3 , we have kernel size of only 1 dimension and we call such a conv
  /// operation as conv1d.
  /// Conv1d is implemented using Conv2d as follows:
  ///   a) Expand the input and kernel dimension to 4 using expand operator
  ///   b) Do the necessary tensor format conversion as required for Conv2d
  ///   c) Then use Conv2d for execution
  ///   d) To reduce the output tensor dimension from 4 to 3, Squeeze is used
  Error loadConv1D(const ONNX_NAMESPACE::NodeProto &op,
                   ArgumentDictionaryTy &dict);

  /// Load MaxPool or AveragePool ONNX operator. \p typeName is the name of the
  /// ONNX operator being loaded, either MaxPool or AveragePool.
  Error loadPool(const ONNX_NAMESPACE::NodeProto &op,
                 ArgumentDictionaryTy &dict, llvm::StringRef typeName);

  /// Load Glow pooling operator with quantized inputs. Since this isn't a
  /// normal part of the ops supported by onnx, the assumption is that this op
  /// was produced by Glow's on ONNXModelWriter and thus has NHWC layout for
  /// inputs.
  Error loadTensorwiseQuantizedPool(const ONNX_NAMESPACE::NodeProto &op,
                                    ArgumentDictionaryTy &dict,
                                    llvm::StringRef typeName);

  /// Load GlobalAveragePool ONNX operator.
  Error loadGlobalAveragePool(const ONNX_NAMESPACE::NodeProto &op,
                              ArgumentDictionaryTy &dict);

  /// Load Squeeze ONNX operator.
  Error loadSqueeze(const ONNX_NAMESPACE::NodeProto &op,
                    ArgumentDictionaryTy &dict);

  /// Load Unsqueeze ONNX operator.
  Error loadUnsqueeze(const ONNX_NAMESPACE::NodeProto &op,
                      ArgumentDictionaryTy &dict);

  /// Load ArgMax ONNX operator.
  Error loadArgMax(const ONNX_NAMESPACE::NodeProto &op,
                   ArgumentDictionaryTy &dict);

  /// Load Upsample ONNX operator.
  Error loadUpsample(const ONNX_NAMESPACE::NodeProto &op,
                     ArgumentDictionaryTy &dict);

  /// Load BatchNormalization ONNX operator.
  Error loadBatchNormalization(const ONNX_NAMESPACE::NodeProto &op,
                               ArgumentDictionaryTy &dict);

  /// Load Concat ONNX operator.
  Error loadConcat(const ONNX_NAMESPACE::NodeProto &op,
                   ArgumentDictionaryTy &dict);

  /// Load FCTransposed ONNX operator.
  Error loadFCTransposed(const ONNX_NAMESPACE::NodeProto &op,
                         ArgumentDictionaryTy &dict);

  /// Load Gemm ONNX operator.
  Error loadGemm(const ONNX_NAMESPACE::NodeProto &op,
                 ArgumentDictionaryTy &dict);

  /// Load MatMul ONNX operator.
  Error loadMatMul(const ONNX_NAMESPACE::NodeProto &op,
                   ArgumentDictionaryTy &dict);

  /// Load Pad ONNX operator.
  Error loadPad(const ONNX_NAMESPACE::NodeProto &op,
                ArgumentDictionaryTy &dict);

  /// Load Cast ONNX operator.
  Error loadCast(const ONNX_NAMESPACE::NodeProto &op,
                 ArgumentDictionaryTy &dict);

  /// Load LeakyRelu ONNX operator.
  Error loadLeakyRelu(const ONNX_NAMESPACE::NodeProto &op,
                      ArgumentDictionaryTy &dict);

  /// Load SpaceToDepth ONNX operator.
  Error loadSpaceToDepth(const ONNX_NAMESPACE::NodeProto &op,
                         ArgumentDictionaryTy &dict);

  /// Load ReduceL2 ONNX operator
  Error loadReduceL2(const ONNX_NAMESPACE::NodeProto &op,
                     const ArgumentDictionaryTy &dict);

  /// Load ConstantOfShape ONNX operator.
  Error loadConstantOfShape(const ONNX_NAMESPACE::NodeProto &op,
                            ArgumentDictionaryTy &dict, bool isSplat);

  /// Load Tile ONNX operator.
  Error loadTile(const ONNX_NAMESPACE::NodeProto &op,
                 ArgumentDictionaryTy &dict);

  /// Load Expand ONNX operator.
  Error loadExpand(const ONNX_NAMESPACE::NodeProto &op,
                   const ArgumentDictionaryTy &dict);

  /// Load Where ONNX operator.
  Error loadWhere(const ONNX_NAMESPACE::NodeProto &op,
                  ArgumentDictionaryTy &dict);

  /// Load RNN ONNX operator.
  Error loadRNN(const ONNX_NAMESPACE::NodeProto &op,
                ArgumentDictionaryTy &dict);

  /// Load GRU ONNX operator.
  Error loadGRU(const ONNX_NAMESPACE::NodeProto &op,
                ArgumentDictionaryTy &dict);

  /// Load LSTM ONNX operator.
  Error loadLSTM(const ONNX_NAMESPACE::NodeProto &op,
                 ArgumentDictionaryTy &dict);

  /// Load Glow specific operators, not defined in ONNX format
  /// Load Glow CmpEQ operator.
  Error loadCmpEQ(const ONNX_NAMESPACE::NodeProto &op,
                  ArgumentDictionaryTy &dict);

  /// Load Glow CmpLTE operator.
  Error loadCmpLTE(const ONNX_NAMESPACE::NodeProto &op,
                   ArgumentDictionaryTy &dict);

  /// Load Glow Select operator.
  Error loadSelect(const ONNX_NAMESPACE::NodeProto &op,
                   ArgumentDictionaryTy &dict);

  /// Load Glow Quantize operator.
  Error loadQuantize(const ONNX_NAMESPACE::NodeProto &op,
                     ArgumentDictionaryTy &dict);

  /// Load Glow ConvertTo operator.
  Error loadConvertTo(const ONNX_NAMESPACE::NodeProto &op,
                      ArgumentDictionaryTy &dict);

  /// Load Glow Dequantize operator.
  Error loadDequantize(const ONNX_NAMESPACE::NodeProto &op,
                       ArgumentDictionaryTy &dict);

  /// Load Glow Regression operator.
  Error loadRegression(const ONNX_NAMESPACE::NodeProto &op,
                       ArgumentDictionaryTy &dict);

  /// Load Glow BatchedAdd operator.
  Error loadBatchedAdd(const ONNX_NAMESPACE::NodeProto &op,
                       ArgumentDictionaryTy &dict);

  /// Load Glow CumSum operator.
  Error loadCumSum(const ONNX_NAMESPACE::NodeProto &op,
                   ArgumentDictionaryTy &dict);

  /// Load Glow ScatterAssign operator.
  Error loadScatterAssign(const ONNX_NAMESPACE::NodeProto &op,
                          ArgumentDictionaryTy &dict);

  /// Load Glow IntLookupTable operator.
  Error loadIntLookupTable(const ONNX_NAMESPACE::NodeProto &op,
                           ArgumentDictionaryTy &dict);

  /// Load Glow LengthsRangeFill operator.
  Error loadLengthsRangeFill(const ONNX_NAMESPACE::NodeProto &op,
                             ArgumentDictionaryTy &dict);

  /// Load Glow RescaleQuantized operator.
  Error loadRescaleQuantized(const ONNX_NAMESPACE::NodeProto &op,
                             ArgumentDictionaryTy &dict);

  /// Load Glow RowwiseQuantizedSparseLengthsWeightedSum operator.
  Error loadRowwiseQuantizedSparseLengthsWeightedSum(
      const ONNX_NAMESPACE::NodeProto &op, ArgumentDictionaryTy &dict);

  /// Load Glow FusedRowwiseQuantizedSparseLengthsWeightedSum operator.
  Error loadFusedRowwiseQuantizedSparseLengthsWeightedSum(
      const ONNX_NAMESPACE::NodeProto &op, ArgumentDictionaryTy &dict);

  /// Load Glow FusedRowwiseQuantizedSparseLengthsSum operator.
  Error
  loadFusedRowwiseQuantizedSparseLengthsSum(const ONNX_NAMESPACE::NodeProto &op,
                                            ArgumentDictionaryTy &dict);

  /// Load Glow RowwiseQuantizedFullyConnected operator.
  Error loadRowwiseQuantizedFullyConnected(const ONNX_NAMESPACE::NodeProto &op,
                                           ArgumentDictionaryTy &dict);

  /// Load Glow FullyConnected operator.
  Error loadFullyConnected(const ONNX_NAMESPACE::NodeProto &op,
                           ArgumentDictionaryTy &dict);

  /// Load ONNX Identity operator.
  Error loadIdentity(const ONNX_NAMESPACE::NodeProto &op,
                     ArgumentDictionaryTy &dict);

  /// Load Glow Splat operator.
  Error loadSplat(const ONNX_NAMESPACE::NodeProto &op,
                  ArgumentDictionaryTy &dict);

  /// Load NonMaxSuppression ONNX and TF NMSv4 operator.
  /// The \p isV4 indicates whether this is ONNX or custom NMSv4 operator.
  Error loadNonMaxSuppression(const ONNX_NAMESPACE::NodeProto &op,
                              ArgumentDictionaryTy &dict, bool isV4);

  /// Load Glow InsertTensor operator.
  Error loadInsertTensor(const ONNX_NAMESPACE::NodeProto &op,
                         ArgumentDictionaryTy &dict);

  /// Load AdaptiveAvgPool Glow operator.
  /// NOTE: since this operator is not a standard onnx op, assume this is from
  /// OnnxModelWriter and is therefore in NHWC format.
  Error loadAdaptiveAvgPool(const ONNX_NAMESPACE::NodeProto &op,
                            ArgumentDictionaryTy &dict);

  /// Load Flip Glow operator.
  Error loadFlip(const ONNX_NAMESPACE::NodeProto &op,
                 ArgumentDictionaryTy &dict);

  /// Load AudioSpectrogram Glow operator.
  Error loadAudioSpectrogram(const ONNX_NAMESPACE::NodeProto &op,
                             ArgumentDictionaryTy &dict);

  /// Load MFCC Glow operator.
  Error loadMFCC(const ONNX_NAMESPACE::NodeProto &op,
                 ArgumentDictionaryTy &dict);

protected:
  /// Load the network operators from the GraphProto.
  /// \returns Error if network cannot be loaded.
  Error loadNetwork(ONNX_NAMESPACE::GraphProto &net);

  /// Set the output nodes of the network \p net. Initializes the map from the
  /// names of the outputs to the save nodes that save each output.
  /// \returns Error if network cannot be loaded.
  Error setOutputNodes(ONNX_NAMESPACE::GraphProto &net);

  /// Set ir verion and op version.
  Error setVersion(ONNX_NAMESPACE::ModelProto MP);

  /// \returns Expected<ModelProto> if a ModelProto can be loaded from the
  /// stream \p iStream.
  static Expected<ONNX_NAMESPACE::ModelProto>
  loadProto(google::protobuf::io::ZeroCopyInputStream &iStream);

  /// Load the network initializers from the GraphProto.
  Error loadInitializers(ONNX_NAMESPACE::GraphProto &net);

  /// Load the inputs from the GraphProto. If \p loadInputsAsPlaceholdersForOnnx
  /// is true then this will load each graph input as a placeholder otherwise it
  /// will create an empty tensor for each input.
  Error loadInputs(ONNX_NAMESPACE::GraphProto &net,
                   bool loadInputsAsPlaceholdersForOnnx);

  /// \returns Expected<ModelProto> if a ModelProto can be constructed from the
  /// contents of the file \p filename and Error otherwise.
  /// Loads ModelProto from the file containing serialized protobuf.
  /// If \p zipMode then zip format will be expected/loaded.
  static Expected<ONNX_NAMESPACE::ModelProto>
  loadProto(const std::string &filename, bool zipMode);

  /// \returns Expected<ModelProto> if a ModelProto can be constructed from the
  /// in-memory serialized protobuf.
  /// Loads ModelProto from the in-memory serialized protobuf \p
  /// onnxModel with the model size \p onnxModelSize.
  static Expected<ONNX_NAMESPACE::ModelProto> loadProto(const void *onnxModel,
                                                        size_t onnxModelSize);

  /// Checks that the inputs tensors are compatible with the inputs declared in
  /// the ONNX model. The input types in \p types match the list of names
  /// \p tensorNames.
  Error checkInputs(ONNX_NAMESPACE::GraphProto &net,
                    llvm::ArrayRef<const char *> tensorNames,
                    llvm::ArrayRef<TypeRef> types);

  /// Go through the ValueInfoProto of the inputs of the \p net and collect
  /// static placeholders if it's marked in the ValueInfoProto.
  Error collectStaticInputs(ONNX_NAMESPACE::GraphProto &net);

  /// Creates a ONNX model loader to build \p F.
  /// Loads the ONNIXFI \p model from memory of \p modelSize size,
  /// and \p weightsCount, and \p onnxTensorDescriptorV1 correspondent
  /// descriptors. Converts inputs into placeholder if requested \p
  /// loadInputsAsPlaceholdersForOnnx. Reports success/failure through optional
  /// parameter \p errPtr. This constructor always overrides the default
  /// constant folding in loader flag with \p constFoldInLoader.
  ONNXModelLoader(const void *model, uint32_t modelSize, uint32_t weightsCount,
                  const onnxTensorDescriptorV1 *weightDescriptors, Function &F,
                  bool loadInputsAsPlaceholdersForOnnx, Error *errPtr = nullptr,
                  bool constFoldInLoader = true);

  friend class ONNXIFIModelLoader;

  /// \returns success if the folding of operator \p op in the loader
  /// \p loader is successful. The folding utility uses temporary
  /// loader \p tmpLoader, and associated temporary function \p F.
  template <class LoaderType, class OpType>
  friend Error constantFoldInLoader(Function *F, LoaderType &tmpLoader,
                                    LoaderType *loader, const OpType &op);

  /// Creates tensor \p T from the input \p in. Note, there is no data
  /// associated with the Tensor. This method makes sure that the tensor is
  /// created with the proper shape and element type.
  Error setTensorType(const ONNX_NAMESPACE::ValueInfoProto &in, Tensor *T);

  /// Load a model \p modelDef given \p tensorNames, \p types, \p B, and
  /// \p loadInputsAsPlaceholdersForOnnx.
  Error loadModel(ONNX_NAMESPACE::ModelProto &modelDef,
                  llvm::ArrayRef<const char *> tensorNames,
                  llvm::ArrayRef<TypeRef> types, const Backend *B,
                  bool loadInputsAsPlaceholdersForOnnx);

  /// Setup partitions by creating Functions and loading metadata into \p PPC
  /// from the metadata props found in \p modelDef given \p rootName and
  /// \p numPartitions.
  Error setupPartitions(ONNX_NAMESPACE::ModelProto &modelDef,
                        runtime::PrePartitionedConfig &PPC,
                        llvm::StringRef rootName, int numPartitions);

public:
  /// \returns ONNX model ir_version;
  size_t getIrVersion() const { return irVersion_; };

  /// \returns ONNX model op_version;
  size_t getOpSetVersion() const { return opsetVersion_; };

  /// \returns if the loader is loading a proto using custom Glow ops.
  bool usingGlowCustomOps() const { return useGlowCustomOps_; };

  /// Creates a ONNX model loader to build \p F.
  /// If \p errPtr is not null then if an error occurs it will get assigned
  /// there otherwise if an error occurs it will abort.
  ONNXModelLoader(Function &F, Error *errPtr = nullptr);

  /// Loads the ONNX model that's represented by a model description file,
  /// serialized in \p modelDescFilename and populates the network into \p F.
  /// The types in \p types match the list of names \p tensorNames and used as
  /// inputs to the network.
  /// If \p names and \p types are empty loader fills inputs automatically.
  /// If \p errPtr is not null then if an error occurs it will get assigned
  /// there otherwise if an error occurs it will abort.
  /// If \p disableConstFoldInLoader then constant folding will be disabled
  /// during loading. \p B will be used during function verification after
  /// loading.
  ONNXModelLoader(const std::string &modelDescFilename,
                  llvm::ArrayRef<const char *> tensorNames,
                  llvm::ArrayRef<TypeRef> types, Function &F,
                  Error *errPtr = nullptr, bool zipMode = false,
                  BackendSpecificNodeInfo *perNodeOpts = nullptr,
                  bool disableConstFoldInLoader = false,
                  const Backend *B = nullptr);

  /// Loads the ONNX model that's represented by a model description file,
  /// serialized in \p modelDescFilename and populates the network into \p mod.
  /// Supports loading a DAG which was serialized, loading in DAGNode meta info
  /// into \p PPC which can be later used to recreated the DAG. \p funName is
  /// used to setup the DAG root node's name, or if the input model is not
  /// partitioned then is used as the name of the single Function loaded.
  /// The types in \p types match the list of names \p tensorNames and used as
  /// inputs to the network.
  /// If \p names and \p types are empty loader fills inputs automatically.
  /// If \p errPtr is not null then if an error occurs it will get assigned
  /// there otherwise if an error occurs it will abort.
  /// If \p disableConstFoldInLoader then constant folding will be disabled
  /// during loading. \p B will be used during function verification after
  /// loading.
  ONNXModelLoader(const std::string &modelDescFilename,
                  llvm::ArrayRef<const char *> tensorNames,
                  llvm::ArrayRef<TypeRef> types, Module &mod,
                  llvm::StringRef funName,
                  runtime::PrePartitionedConfig *PPC = nullptr,
                  Error *errPtr = nullptr, bool zipMode = false,
                  BackendSpecificNodeInfo *perNodeOpts = nullptr,
                  bool disableConstFoldInLoader = false,
                  const Backend *B = nullptr);

private:
  /// Per-node options that may be specified in a proto.
  BackendSpecificNodeInfo *perNodeOpts_{nullptr};
};

} // namespace glow

#endif // GLOW_IMPORTER_ONNXMODELLOADER_H
