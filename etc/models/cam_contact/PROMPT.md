# Camera Contact model integration

I have this new camera contact model I want to integrate into my pipeline.
The model tells us if a person is looking at a camera or not.

## Input
FLOAT[1,3,224,224]

## Output
Output is FLOAT[1,2]
The model outputs logits for two classes (contact / no contact). To get the final prediction for a batch:

## Task
Change my #file:opchain.json  and #file:model.json configuration to fit this new model. These files were copied from my gazedetection project without change.
Change amposd in a way that it should draw a red circle to the persons face if the person isn't looking into the camera and a green circle if they are looking at the camera.
Similarly to #file:YoloParser.cpp  and #file:GazeDetectionParser.cpp I need similar post processing elements except the CameraContact post processor should make sense of the new models output tensor.
Check my #codebase and make the needed modifications to make this model work.

### Build and documentation
The project builds with  ./scripts/build-elements.sh debug command.
The main build entry point is #file:meson.build 

### Documentation
Architectural and tutorial documentations are under #file:docs folder
