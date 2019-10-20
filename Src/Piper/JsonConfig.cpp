#include "../Shared/ConfigAPI.hpp"
#pragma warning(push, 0)
//#define NDEBUG
#include <nlohmann/json.hpp>
#pragma warning(pop)
#include <fstream>

using Json = nlohmann::json;

class JsonConfig final : public Config {
private:
    std::shared_ptr<Json> mData;
    const Json* mRef;
    std::string mPath;

public:
    explicit JsonConfig(Bus::ModuleInstance& instance) : Config(instance) {}
    JsonConfig(Bus::ModuleInstance& instance, std::shared_ptr<Json> data,
               const Json* ref, const std::string& path)
        : Config(instance), mData(data), mRef(ref), mPath(path) {}
    std::string dump() const override {
        return mRef->dump();
    }
    std::string path() const override {
        return mPath;
    }
    bool load(const fs::path& path) override {
        try {
            mData = std::make_shared<Json>();
            mRef = mData.get();
            std::ifstream in(path);
            in >> (*mData);
            mPath = "Root";
            return true;
        } catch(...) {
        }
        return false;
    }
    std::shared_ptr<Config> attribute(Name attr) const override {
        return std::make_shared<JsonConfig>(mInstance, mData,
                                            &((*mRef)[attr.data()]),
                                            mPath + '/' + attr.data());
    }
    std::vector<std::shared_ptr<Config>> expand() const override {
        std::vector<std::shared_ptr<Config>> res;
        unsigned idx = 0;
        for(auto&& ref : *mRef)
            res.emplace_back(std::make_shared<JsonConfig>(
                mInstance, mData, &ref,
                mPath + "/[" + std::to_string(idx++) + ']'));
        return res;
    }
    size_t size() const override {
        return mRef->size();
    }
    DataType getType() const override {
        using Type = Json::value_t;
        switch(mRef->type()) {
            case Type::array:
                return DataType::Array;
            case Type::boolean:
                return DataType::Bool;
            case Type::number_float:
                return DataType::Float;
            case Type::number_unsigned:
                return DataType::Unsigned;
            case Type::string:
                return DataType::String;
            case Type::object:
                return DataType::Object;
            default:
                throw std::runtime_error("Unknown Type");
        }
    }
    unsigned asUint() const override {
        return static_cast<unsigned>(mRef->get<uint64_t>());
    }
    float asFloat() const override {
        return static_cast<float>(mRef->get<double>());
    }
    std::string asString() const override {
        return mRef->get<std::string>();
    }
    bool asBool() const override {
        return mRef->get<bool>();
    }
    bool hasAttr(Name attr) const override {
        return mRef->contains(attr);
    }
};

std::shared_ptr<Bus::ModuleFunctionBase>
makeJsonConfig(Bus::ModuleInstance& instance) {
    return std::make_shared<JsonConfig>(instance);
}
