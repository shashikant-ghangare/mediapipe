/* Copyright 2022 The MediaPipe Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include "mediapipe/calculators/core/clip_vector_size_calculator.pb.h"
#include "mediapipe/calculators/core/gate_calculator.pb.h"
#include "mediapipe/calculators/util/collection_has_min_size_calculator.pb.h"
#include "mediapipe/framework/api2/builder.h"
#include "mediapipe/framework/api2/port.h"
#include "mediapipe/framework/formats/classification.pb.h"
#include "mediapipe/framework/formats/detection.pb.h"
#include "mediapipe/framework/formats/image.h"
#include "mediapipe/framework/formats/landmark.pb.h"
#include "mediapipe/framework/formats/rect.pb.h"
#include "mediapipe/framework/formats/tensor.h"
#include "mediapipe/tasks/cc/common.h"
#include "mediapipe/tasks/cc/components/utils/gate.h"
#include "mediapipe/tasks/cc/core/model_task_graph.h"
#include "mediapipe/tasks/cc/core/utils.h"
#include "mediapipe/tasks/cc/vision/hand_detector/proto/hand_detector_graph_options.pb.h"
#include "mediapipe/tasks/cc/vision/hand_landmarker/calculators/hand_association_calculator.pb.h"
#include "mediapipe/tasks/cc/vision/hand_landmarker/proto/hand_landmarker_graph_options.pb.h"
#include "mediapipe/tasks/cc/vision/hand_landmarker/proto/hand_landmarker_subgraph_options.pb.h"

namespace mediapipe {
namespace tasks {
namespace vision {
namespace hand_landmarker {

namespace {

using ::mediapipe::api2::Input;
using ::mediapipe::api2::Output;
using ::mediapipe::api2::builder::Graph;
using ::mediapipe::api2::builder::Source;
using ::mediapipe::tasks::components::utils::DisallowIf;
using ::mediapipe::tasks::vision::hand_detector::proto::
    HandDetectorGraphOptions;
using ::mediapipe::tasks::vision::hand_landmarker::proto::
    HandLandmarkerGraphOptions;
using ::mediapipe::tasks::vision::hand_landmarker::proto::
    HandLandmarkerSubgraphOptions;

constexpr char kImageTag[] = "IMAGE";
constexpr char kLandmarksTag[] = "LANDMARKS";
constexpr char kWorldLandmarksTag[] = "WORLD_LANDMARKS";
constexpr char kHandRectNextFrameTag[] = "HAND_RECT_NEXT_FRAME";
constexpr char kHandednessTag[] = "HANDEDNESS";
constexpr char kPalmDetectionsTag[] = "PALM_DETECTIONS";
constexpr char kPalmRectsTag[] = "PALM_RECTS";
constexpr char kPreviousLoopbackCalculatorName[] = "PreviousLoopbackCalculator";

struct HandLandmarkerOutputs {
  Source<std::vector<NormalizedLandmarkList>> landmark_lists;
  Source<std::vector<LandmarkList>> world_landmark_lists;
  Source<std::vector<NormalizedRect>> hand_rects_next_frame;
  Source<std::vector<ClassificationList>> handednesses;
  Source<std::vector<NormalizedRect>> palm_rects;
  Source<std::vector<Detection>> palm_detections;
  Source<Image> image;
};

}  // namespace

// A "mediapipe.tasks.vision.HandLandmarkerGraph" performs hand
// landmarks detection. The HandLandmarkerGraph consists of two subgraphs:
// HandDetectorGraph and HandLandmarkerSubgraph. HandLandmarkerSubgraph detects
// landmarks from bounding boxes produced by HandDetectorGraph.
// HandLandmarkerGraph tracks the landmarks over time, and skips the
// HandDetectorGraph. If the tracking is lost or the detectd hands are
// less than configured max number hands, HandDetectorGraph would be triggered
// to detect hands.
//
// Accepts CPU input images and outputs Landmarks on CPU.
//
// Inputs:
//   IMAGE - Image
//     Image to perform hand landmarks detection on.
//
// Outputs:
//   LANDMARKS: - std::vector<NormalizedLandmarkList>
//     Vector of detected hand landmarks.
//   WORLD_LANDMARKS - std::vector<LandmarkList>
//     Vector of detected hand landmarks in world coordinates.
//   HAND_RECT_NEXT_FRAME - std::vector<NormalizedRect>
//     Vector of the predicted rects enclosing the same hand RoI for landmark
//     detection on the next frame.
//   HANDEDNESS - std::vector<ClassificationList>
//     Vector of classification of handedness.
//   PALM_RECTS - std::vector<NormalizedRect>
//     Detected palm bounding boxes in normalized coordinates.
//   PALM_DETECTIONS - std::vector<Detection>
//     Detected palms with maximum `num_hands` specified in options.
//   IMAGE - Image
//     The input image that the hand landmarker runs on and has the pixel data
//     stored on the target storage (CPU vs GPU).
//
// Example:
// node {
//   calculator: "mediapipe.tasks.vision.HandLandmarkerGraph"
//   input_stream: "IMAGE:image_in"
//   output_stream: "LANDMARKS:hand_landmarks"
//   output_stream: "WORLD_LANDMARKS:world_hand_landmarks"
//   output_stream: "HAND_RECT_NEXT_FRAME:hand_rect_next_frame"
//   output_stream: "HANDEDNESS:handedness"
//   output_stream: "PALM_RECTS:palm_rects"
//   output_stream: "PALM_DETECTIONS:palm_detections"
//   output_stream: "IMAGE:image_out"
//   options {
//     [mediapipe.tasks.hand_landmarker.proto.HandLandmarkerGraphOptions.ext] {
//       base_options {
//          model_asset {
//            file_name: "hand_landmarker.task"
//          }
//       }
//       hand_detector_graph_options {
//         base_options {
//            model_asset {
//              file_name: "palm_detection.tflite"
//            }
//         }
//         min_detection_confidence: 0.5
//         num_hands: 2
//       }
//       hand_landmarker_subgraph_options {
//         base_options {
//              model_asset {
//                file_name: "hand_landmark_lite.tflite"
//              }
//           }
//           min_detection_confidence: 0.5
//       }
//     }
//   }
// }
class HandLandmarkerGraph : public core::ModelTaskGraph {
 public:
  absl::StatusOr<CalculatorGraphConfig> GetConfig(
      SubgraphContext* sc) override {
    Graph graph;
    ASSIGN_OR_RETURN(
        auto hand_landmarker_outputs,
        BuildHandLandmarkerGraph(sc->Options<HandLandmarkerGraphOptions>(),
                                 graph[Input<Image>(kImageTag)], graph));
    hand_landmarker_outputs.landmark_lists >>
        graph[Output<std::vector<NormalizedLandmarkList>>(kLandmarksTag)];
    hand_landmarker_outputs.world_landmark_lists >>
        graph[Output<std::vector<LandmarkList>>(kWorldLandmarksTag)];
    hand_landmarker_outputs.hand_rects_next_frame >>
        graph[Output<std::vector<NormalizedRect>>(kHandRectNextFrameTag)];
    hand_landmarker_outputs.handednesses >>
        graph[Output<std::vector<ClassificationList>>(kHandednessTag)];
    hand_landmarker_outputs.palm_rects >>
        graph[Output<std::vector<NormalizedRect>>(kPalmRectsTag)];
    hand_landmarker_outputs.palm_detections >>
        graph[Output<std::vector<Detection>>(kPalmDetectionsTag)];
    hand_landmarker_outputs.image >> graph[Output<Image>(kImageTag)];

    // TODO remove when support is fixed.
    // As mediapipe GraphBuilder currently doesn't support configuring
    // InputStreamInfo, modifying the CalculatorGraphConfig proto directly.
    CalculatorGraphConfig config = graph.GetConfig();
    for (int i = 0; i < config.node_size(); ++i) {
      if (config.node(i).calculator() == kPreviousLoopbackCalculatorName) {
        auto* info = config.mutable_node(i)->add_input_stream_info();
        info->set_tag_index("LOOP");
        info->set_back_edge(true);
        break;
      }
    }
    return config;
  }

 private:
  // Adds a mediapipe hand landmark detection graph into the provided
  // builder::Graph instance.
  //
  // tasks_options: the mediapipe tasks module HandLandmarkerGraphOptions.
  // image_in: (mediapipe::Image) stream to run hand landmark detection on.
  // graph: the mediapipe graph instance to be updated.
  absl::StatusOr<HandLandmarkerOutputs> BuildHandLandmarkerGraph(
      const HandLandmarkerGraphOptions& tasks_options, Source<Image> image_in,
      Graph& graph) {
    const int max_num_hands =
        tasks_options.hand_detector_graph_options().num_hands();

    auto& previous_loopback = graph.AddNode(kPreviousLoopbackCalculatorName);
    image_in >> previous_loopback.In("MAIN");
    auto prev_hand_rects_from_landmarks =
        previous_loopback[Output<std::vector<NormalizedRect>>("PREV_LOOP")];

    auto& min_size_node =
        graph.AddNode("NormalizedRectVectorHasMinSizeCalculator");
    prev_hand_rects_from_landmarks >> min_size_node.In("ITERABLE");
    min_size_node.GetOptions<CollectionHasMinSizeCalculatorOptions>()
        .set_min_size(max_num_hands);
    auto has_enough_hands = min_size_node.Out("").Cast<bool>();

    auto image_for_hand_detector =
        DisallowIf(image_in, has_enough_hands, graph);

    auto& hand_detector =
        graph.AddNode("mediapipe.tasks.vision.hand_detector.HandDetectorGraph");
    hand_detector.GetOptions<HandDetectorGraphOptions>().CopyFrom(
        tasks_options.hand_detector_graph_options());
    image_for_hand_detector >> hand_detector.In("IMAGE");
    auto hand_rects_from_hand_detector = hand_detector.Out("HAND_RECTS");

    auto& hand_association = graph.AddNode("HandAssociationCalculator");
    hand_association.GetOptions<HandAssociationCalculatorOptions>()
        .set_min_similarity_threshold(tasks_options.min_tracking_confidence());
    prev_hand_rects_from_landmarks >>
        hand_association[Input<std::vector<NormalizedRect>>::Multiple("")][0];
    hand_rects_from_hand_detector >>
        hand_association[Input<std::vector<NormalizedRect>>::Multiple("")][1];
    auto hand_rects = hand_association.Out("");

    auto& clip_hand_rects =
        graph.AddNode("ClipNormalizedRectVectorSizeCalculator");
    clip_hand_rects.GetOptions<ClipVectorSizeCalculatorOptions>()
        .set_max_vec_size(max_num_hands);
    hand_rects >> clip_hand_rects.In("");
    auto clipped_hand_rects = clip_hand_rects.Out("");

    auto& hand_landmarker_subgraph = graph.AddNode(
        "mediapipe.tasks.vision.hand_landmarker.HandLandmarkerSubgraph");
    hand_landmarker_subgraph.GetOptions<HandLandmarkerSubgraphOptions>()
        .CopyFrom(tasks_options.hand_landmarker_subgraph_options());
    image_in >> hand_landmarker_subgraph.In("IMAGE");
    clipped_hand_rects >> hand_landmarker_subgraph.In("HAND_RECT");

    auto hand_rects_for_next_frame =
        hand_landmarker_subgraph[Output<std::vector<NormalizedRect>>(
            kHandRectNextFrameTag)];
    // Back edge.
    hand_rects_for_next_frame >> previous_loopback.In("LOOP");

    // TODO: Replace PassThroughCalculator with a calculator that
    // converts the pixel data to be stored on the target storage (CPU vs GPU).
    auto& pass_through = graph.AddNode("PassThroughCalculator");
    image_in >> pass_through.In("");

    return {{
        /* landmark_lists= */ hand_landmarker_subgraph
            [Output<std::vector<NormalizedLandmarkList>>(kLandmarksTag)],
        /*  world_landmark_lists= */
        hand_landmarker_subgraph[Output<std::vector<LandmarkList>>(
            kWorldLandmarksTag)],
        /* hand_rects_next_frame= */ hand_rects_for_next_frame,
        hand_landmarker_subgraph[Output<std::vector<ClassificationList>>(
            kHandednessTag)],
        /* palm_rects= */
        hand_detector[Output<std::vector<NormalizedRect>>(kPalmRectsTag)],
        /* palm_detections */
        hand_detector[Output<std::vector<Detection>>(kPalmDetectionsTag)],
        /* image */
        pass_through[Output<Image>("")],
    }};
  }
};

REGISTER_MEDIAPIPE_GRAPH(
    ::mediapipe::tasks::vision::hand_landmarker::HandLandmarkerGraph);

}  // namespace hand_landmarker
}  // namespace vision
}  // namespace tasks
}  // namespace mediapipe
