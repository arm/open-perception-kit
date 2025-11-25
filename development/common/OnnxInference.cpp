#include "OnnxInference.h"
#include "OnnxTools.h"
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

        this->managedModel = OnnxTools::inspectModel(*this->session);
        if(false == this->managedModel.parseError.empty()) {
            return OnnxResult::UniflowModelInspectError;
        }

        /*
        Ort::AllocatorWithDefaultOptions allocator;
        const size_t ni = this->session->GetInputCount();
        const size_t no = this->session->GetOutputCount();

        this->inputNames.clear();
        this->outputNames.clear();
        for (size_t i=0;i<ni;++i) {
            auto s = this->session->GetInputNameAllocated(i, allocator);
            printf("**** Input:[%s]\n", s.get());
            this->inputNames.push_back(strdup(s.get()));
        }
        for(size_t i = 0; i < no; ++i) {
            auto s = this->session->GetOutputNameAllocated(i, allocator);
            printf("**** Output:[%s]\n", s.get());
            this->outputNames.push_back(strdup(s.get()));
        }
            */
        
        this->setupReady = true;
    } 
    catch (const std::exception& e) {
    
        return OnnxResult::CreateEnvironmentError;
    }

    return OnnxResult::Ok;

}
