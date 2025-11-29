#include "OnnxInference.h"
#include "OnnxTools.h"
#include "uniflow/detection_types.h"
#include <memory>

OnnxInference::OnnxInference() {
}

OnnxInference::~OnnxInference() {

}

OnnxResult OnnxInference::setup(const std::string& file) {

    this->modelPath = file;

    try {
        
        this->sessionOptions = new Ort::SessionOptions();
        this->sessionOptions->SetIntraOpNumThreads(1);

        this->environment = new Ort::Env(ORT_LOGGING_LEVEL_WARNING, "ampinfer");
        this->memoryInfo = new Ort::MemoryInfo(Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeCPU));
  
        this->session = new Ort::Session(*this->environment, this->modelPath.c_str(), *this->sessionOptions);

        this->uflwModel = OnnxTools::inspectModel(*this->session, this->modelPath);
        if(false == this->uflwModel.parseError.empty()) {
            printf("INSPECT MODEL FAILED: %s\n", this->uflwModel.parseError.c_str());
            return OnnxResult::UniflowModelInspectError;
        }
        
        std::string modelLog = OnnxTools::toString(this->uflwModel);
        printf("---> New  model  parsed <---\n");
        printf("%s", modelLog.c_str());
        printf("--- --- --- ---- --- --- ---\n");

        this->setupReady = true;
    } 
    catch (const std::exception& e) {
    
        return OnnxResult::CreateEnvironmentError;
    }

    return OnnxResult::Ok;

}

uflw::DetectionResult OnnxInference::execute(const uflw::TensorReader* tensor0, const uflw::TensorReader* tensor1, const uflw::TensorReader* tensor2, const uflw::TensorReader* tensor3) {

    uflw::DetectionResult result;

    return result;

}
