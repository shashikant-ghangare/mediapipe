# Template matching assignment

## Problem Statement
Implement a Template matching algorithm using https://google.github.io/mediapipe/ for 3 objects and share a video demonstrating detection of the objects using template matching and also calculate and display the orientation of the detected object in the video. 

## Approach
1. Use Mediapipe KNIFT(https://google.github.io/mediapipe/solutions/knift.html) for template matching.
2. Extract the bounding box/enclosing rectangle coordinates from the pipeline and calculated angle using 2 methods:
    - minAreaRect()
    - PCA 
## Installation Steps for Ubuntu 18.04 Linux

1. Install Bazel using Bazelisk
```
$ wget https://github.com/bazelbuild/bazelisk/releases/download/v1.15.0/bazelisk-linux-amd64
$ sudo mv bazelisk-linux-amd64 /usr/local/bin/bazel
$ sudo chmod +x /usr/local/bin/bazel
```

2. Clone the forked mediapipe repository and checkout the assignment branch
```
$ git clone https://github.com/shashikant-ghangare/mediapipe.git
$ cd mediapipe
$ git checkout asssignment
```

3. Install OpenCV 3.2 and FFmpeg:
```
$ sudo apt-get install -y \
    libopencv-core-dev \
    libopencv-highgui-dev \
    libopencv-calib3d-dev \
    libopencv-features2d-dev \
    libopencv-imgproc-dev \
    libopencv-video-dev
```
4. Build and run the template matching application:

- Get orientation using minAreaRect():
```
$ bazel build -c opt --define MEDIAPIPE_DISABLE_GPU=1 mediapipe/examples/desktop/template_matching_orientation:template_matching_orientation_tflite

$ bazel-bin/mediapipe/examples/desktop/template_matching_orientation/template_matching_orientation_tflite --calculator_graph_config_file=mediapipe/graphs/template_matching_orientation/template_matching_desktop.pbtxt --input_video_path=<absolute_path_to_input_video> --output_video_path=<absolute_path_to_input_video>
```
- Get orientation using PCA:
```
$ bazel build -c opt --define MEDIAPIPE_DISABLE_GPU=1 mediapipe/examples/desktop/template_matching_orientation_pca:template_matching_orienta
tion_pca_tflite

$ bazel-bin/mediapipe/examples/desktop/template_matching_orientation_pca/template_matching_orientation_pca_tflite --calculator_graph_config_file=mediapipe/graphs/template_matching_orientation/template_matching_desktop.pbtxt --input_video_path=<absolute_path_to_input_video> --output_video_path=<absolute_path_to_input_video>
```
Note: Skip the **--input_video_path** to use webcam as input and **--output_video_path** to display the output on screen.

### Using your own template images

5. Templates are present in the **assignment_templates**. Generate the index file for all the templates, run:
```
$ bazel build -c opt --define MEDIAPIPE_DISABLE_GPU=1 \
mediapipe/examples/desktop/template_matching:template_matching_tflite

$ bazel-bin/mediapipe/examples/desktop/template_matching/template_matching_tflite \
--calculator_graph_config_file=mediapipe/graphs/template_matching/index_building.pbtxt \
--input_side_packets="file_directory=assignment_templates,file_suffix=jpg,output_index_filename=mediapipe/models/knift_index_assignment.pb"
```
4. Update **mediapipe/models/knift_labelmap_assigment.txt** with your own template names.


