#include "onnx/Tensor.h"

// ---
/*
OnnxOutputTensor::OnnxOutputTensor(const std::vector<Ort::Value>& runResult) : runResult(runResult) {

}

size_t OnnxOutputTensor::getValueByteSize() const {
    if(this->runResult.size() == 0) return 0;
    return getOnnxValueTypeByteSize(this->runResult.at(0).GetTensorTypeAndShapeInfo().GetElementType());
}

uflw::Shape OnnxOutputTensor::getShape() const {
    uflw::Shape shape;

    if(this->runResult.size() == 0) return shape;

    std::vector<int64_t> originalShape = this->runResult.at(0).GetTensorTypeAndShapeInfo().GetShape();

    for(size_t i = 0; i < sizeof(shape.valueCount); i++) {
        if(i < originalShape.size()) {
            shape.valueCount[i] = originalShape[i];
        }
    }
    shape.dimensionCount = originalShape.size();

    return shape;
}

size_t OnnxOutputTensor::getValueCount() const {
 
    if(this->runResult.size() == 0) return 0;

    uflw::Shape shape = this->getShape();
    
    size_t valueCount = 1;
    for(size_t i = 0; i < shape.dimensionCount; i++) {
        valueCount *= shape.valueCount[i];
    }

    return valueCount;
}

size_t OnnxOutputTensor::getByteSize() const {
    if(this->runResult.size() == 0) return 0;

    return this->getValueByteSize() * this->getValueCount();
}

const void* OnnxOutputTensor::getRawData() const {
    if(this->runResult.size() == 0) return nullptr;
    return this->runResult.at(0).GetTensorRawData();
}

bool OnnxOutputTensor::dump(const std::string& fileName) const {

    const void* raw = getRawData();
    size_t amount = getByteSize();

    if(!raw || !amount) return false;

    FILE* f = fopen(fileName.c_str(), "wb");

    if(!f) return false;

    if(amount < fwrite(raw, 1, amount, f)) {
        fclose(f);
        return false;
    }
    
    fclose(f);
    return true;
}
*/
/*uflw::YoloLikeParser::ModelOutput OnnxTools::getOutputFromYoloModel(const Ort::Session* session, int index) {

    uflw::YoloLikeParser::ModelOutput out;

    Ort::AllocatorWithDefaultOptions alloc;
    Ort::ModelMetadata meta = session->GetModelMetadata();

    // ---

    std::vector<Ort::AllocatedStringPtr> keys = meta.GetCustomMetadataMapKeysAllocated(alloc);

    for (const auto& k : keys) {
        
        Ort::AllocatedStringPtr v = meta.LookupCustomMetadataMapAllocated(k.get(), alloc);
        if(v) {
            if(!strcmp(k.get(), "stride")) {
                int stride;
                if(safeParseInt(v.get(), &stride)) {
                    out.detectionStepStride = stride;
                }
            }
        }


        //printf("Meta[%s] = %s\n", k.get(), v ? v.get() : "(null)");
    
    }  

    // ---

    return out;

}*/


