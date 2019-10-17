// Copyright 2019 The MediaPipe Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <memory>

#include "myMediapipe/calculators/util/angles_to_detection_calculator.pb.h"
#include "mediapipe/framework/calculator_framework.h"
#include "mediapipe/framework/formats/detection.pb.h"
#include "tensorflow/lite/interpreter.h"
#include "mediapipe/framework/port/ret_check.h"

namespace mediapipe {

typedef std::vector<Detection> Detections; 

namespace {

constexpr char kDetectionTag[] = "DETECTIONS";
constexpr char kTfLiteFloat32[] = "TENSORS"; 

}  // namespace

// Converts Angles to Detection proto. 
//
// If option queue_size is set , it will put each new 
// detection in a FIFO of size queue_size  
// and will return the class corresponding to the 
// highest occcurence in the FIFO, usefull to stabilize detections
// eliminating spurious missclasifications 
//
// Input:
//  TENSOR: A Vector of TfLiteTensor of type kTfLiteFloat32 with the confidence score
//          for each static gesture.
//
// Output:
//   DETECTION: A Detection proto.
//
// Example config:
// node {
//   calculator: "AnglesToDetectionCalculator"
//   input_stream: ""TENSORS:tensors"
//   output_stream: "DETECTIONS:detections"
// }

class AnglesToDetectionCalculator : public CalculatorBase {
 public:
  static ::mediapipe::Status GetContract(CalculatorContract* cc);
  ::mediapipe::Status Open(CalculatorContext* cc) override;

  ::mediapipe::Status Process(CalculatorContext* cc) override;

 private:
  typedef struct {
    float score_; 
    int label_;
    } inValues_t ;
  inValues_t inValues;
  inValues_t *inferenceQueue;
  ::mediapipe::AnglesToDetectionCalculatorOptions options_;
  void mostFrequent(inValues_t* curr_Inference);
  
};
REGISTER_CALCULATOR(AnglesToDetectionCalculator);

::mediapipe::Status AnglesToDetectionCalculator::GetContract(
    CalculatorContract* cc) {
  RET_CHECK(cc->Inputs().HasTag(kTfLiteFloat32));
  //RET_CHECK(cc->Outputs().HasTag(kDetectionTag));
  // TODO: Also support converting Landmark to Detection.
  cc->Inputs()
      .Tag(kTfLiteFloat32) 
      .Set<std::vector<TfLiteTensor>>();
  //cc->Outputs().Tag(kDetectionTag).Set<Detection>();
  cc->Outputs().Index(0).Set<Detections>();

  return ::mediapipe::OkStatus();
}

::mediapipe::Status AnglesToDetectionCalculator::Open(
    CalculatorContext* cc) {
  cc->SetOffset(TimestampDiff(0));

  options_ = cc->Options<::mediapipe::AnglesToDetectionCalculatorOptions>();
  
  //this is the fifo to reject missdetections
  if(options_.queue_size())
   
    inferenceQueue = (inValues_t *) calloc(options_.queue_size(), sizeof(*inferenceQueue));
    
    //if(inferenceQueue==NULL) std::cout << "ERROROROORRRRR";
    // std::cout << "\nHere2: " <<  sizeof(inferenceQueue)
    //                         << "\t" <<  sizeof(&inferenceQueue) ;///sizeof(inferenceQueue[0]);
  return ::mediapipe::OkStatus();
}

::mediapipe::Status AnglesToDetectionCalculator::Process(
    CalculatorContext* cc) {
  RET_CHECK(!cc->Inputs().Tag(kTfLiteFloat32).IsEmpty());

  const auto& input_tensors =
      cc->Inputs().Tag(kTfLiteFloat32).Get<std::vector<TfLiteTensor>>();
  // TODO: Add option to specify which tensor to take from.
  const TfLiteTensor* raw_tensor = &input_tensors[0];
  const float* raw_floats = raw_tensor->data.f;

  
  //std::cout  << raw_tensor->name;
  inValues_t currentInference; 
  

  for(int i=0;i<raw_tensor->dims->data[1]; i++){
    if(raw_floats[i]>currentInference.score_){
        currentInference.score_=raw_floats[i];
        currentInference.label_=i;
    }  
    //std::cout  << "\t:" << std::to_string(raw_floats[i]);
  }
  //   std::cout  << "\n";
  if(options_.queue_size()) mostFrequent(&currentInference);
  
  
  auto* output_detections = new Detections();
  output_detections->reserve(1);
  auto detection = absl::make_unique<Detection>();
  detection->add_score(currentInference.score_);
  detection->add_label_id(currentInference.label_);
  
  LocationData location_data;
  location_data.set_format(LocationData::BOUNDING_BOX);
  location_data.mutable_bounding_box()->set_xmin(450);
  location_data.mutable_bounding_box()->set_ymin(450);
  location_data.mutable_bounding_box()->set_width(200);
  location_data.mutable_bounding_box()->set_height(20);
  *(detection->mutable_location_data()) = location_data;

  
  
  output_detections->push_back(*detection);

  cc->Outputs()
      .Index(0)
      .Add(output_detections, cc->InputTimestamp());

  return ::mediapipe::OkStatus();
}

void AnglesToDetectionCalculator::mostFrequent(inValues_t* curr_ference){
  struct attr
  {
    decltype(curr_ference->score_) sumScores;
    int32 counts;
  } inferenceAttr={};
  
  struct highestOccurrence_t{
    int32 label;
    int32 count;
  } highestOccurrence = {};

  const auto c_ference = curr_ference;

  std::map<int,attr> trackInferences; //label, score, count
  std::map<int,attr>::iterator it;

  for(int i=options_.queue_size() - 1;i>=0;i--){
    
       if(i>0) inferenceQueue[i]=inferenceQueue[i - 1];
       else inferenceQueue[i]=*c_ference;

      
    it = trackInferences.find(inferenceQueue[i].label_);
    if(it == trackInferences.end()) {
      inferenceAttr.counts=1;
      inferenceAttr.sumScores=inferenceQueue[i].score_;
      trackInferences.emplace(inferenceQueue[i].label_,inferenceAttr);
      std::cout << "\n2:" << std::to_string(i)
                << "\t" << std::to_string(inferenceQueue[i].label_)             
                << "\t"   << std::to_string(trackInferences[inferenceQueue[i].label_].counts)
                << "\t"  << std::to_string(trackInferences[inferenceQueue[i].label_].sumScores);
    }
    else{
      trackInferences[inferenceQueue[i].label_].counts++;
      trackInferences[inferenceQueue[i].label_].sumScores+=inferenceQueue[i].score_;
    }
    
    if(trackInferences[inferenceQueue[i].label_].counts > highestOccurrence.count){ 
      highestOccurrence.label = inferenceQueue[i].label_;
      highestOccurrence.count = trackInferences[inferenceQueue[i].label_].counts;
    }

     std::cout << "\n3:" << std::to_string(i)
             << "\t" << std::to_string(inferenceQueue[i].label_)
             //<< "\t" << std::to_string(c_ference.label_)
             << "\t" << std::to_string(trackInferences[inferenceQueue[i].label_].counts)
             << "\t" << std::to_string(highestOccurrence.label);
  
   }
  
  
  curr_ference->label_=highestOccurrence.label;
  curr_ference->score_= trackInferences[highestOccurrence.label].sumScores / 
                        highestOccurrence.count;
  std::cout 
            << "\t" << std::to_string(curr_ference->label_)
            << "\t" << std::to_string(curr_ference->score_);
  trackInferences.clear();  
}

}  // namespace mediapipe
