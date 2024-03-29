#pragma once
#include "PluginShared.hpp"

enum class DataType { Object, Float, Unsigned, String, Bool, Array };

class Config : public Bus::ModuleFunctionBase {
protected:
    explicit Config(Bus::ModuleInstance& instance)
        : ModuleFunctionBase(instance) {}

public:
    static Name getInterface() {
        return "Piper.Utilities.Config";
    }
    virtual std::string dump() const = 0;
    virtual std::string path() const = 0;
    virtual bool load(const fs::path& path) = 0;
    virtual std::shared_ptr<Config> attribute(Name attr) const = 0;
    virtual std::vector<std::shared_ptr<Config>> expand() const = 0;
    virtual size_t size() const = 0;
    virtual DataType getType() const = 0;
    virtual unsigned asUint() const = 0;
    virtual float asFloat() const = 0;
    virtual std::string asString() const = 0;
    virtual bool asBool() const = 0;
    virtual bool hasAttr(Name attr) const = 0;
    virtual Vec4 asVec4() const {
        BUS_TRACE_BEGIN("Piper.Utilities.Config") {
            auto child = expand();
            if(child.size() != 4)
                BUS_TRACE_THROW(std::logic_error("Need Vec4"));
            return Vec4(child[0]->asFloat(), child[1]->asFloat(),
                        child[2]->asFloat(), child[3]->asFloat());
        }
        BUS_TRACE_END();
    }
    virtual Vec3 asVec3() const {
        BUS_TRACE_BEGIN("Piper.Utilities.Config") {
            auto child = expand();
            if(child.size() != 3)
                BUS_TRACE_THROW(std::logic_error("Need Vec3"));
            return Vec3(child[0]->asFloat(), child[1]->asFloat(),
                        child[2]->asFloat());
        }
        BUS_TRACE_END();
    }
    virtual Vec2 asVec2() const {
        BUS_TRACE_BEGIN("Piper.Utilities.Config") {
            auto child = expand();
            if(child.size() != 2)
                BUS_TRACE_THROW(std::logic_error("Need Vec2"));
            return Vec2(child[0]->asFloat(), child[1]->asFloat());
        }
        BUS_TRACE_END();
    }
    virtual Uint2 asUint2() const {
        BUS_TRACE_BEGIN("Piper.Utilities.Config") {
            auto child = expand();
            if(child.size() != 2)
                BUS_TRACE_THROW(std::logic_error("Need Uint2"));
            return Uint2(child[0]->asUint(), child[1]->asUint());
        }
        BUS_TRACE_END();
    }
#define GENGET(T, F)                          \
    T get##F(Name attr, const T& def) const { \
        if(hasAttr(attr))                     \
            return attribute(attr)->as##F();  \
        return def;                           \
    }

    GENGET(unsigned, Uint)
    GENGET(float, Float)
    GENGET(std::string, String)
    GENGET(Vec2, Vec2)
    GENGET(Vec3, Vec3)
    GENGET(Vec4, Vec4)
    GENGET(Uint2, Uint2)
    GENGET(bool, Bool)

#undef GENGET
    virtual SRT getTransform(Name attr) const {
        BUS_TRACE_BEGIN("Piper.Utilities.Config") {
            SRT res;
            res.trans = Vec3{ 0.0f }, res.scale = Vec3{ 1.0f },
            res.rotate = Quat{};
            if(hasAttr(attr)) {
                auto transform = attribute(attr);
                res.trans = transform->getVec3("Trans", res.trans);
                if(transform->hasAttr("Scale")) {
                    auto scale = transform->attribute("Scale");
                    if(scale->getType() == DataType::Float)
                        res.scale = Vec3{ scale->asFloat() };
                    else
                        res.scale = scale->asVec3();
                }
                if(transform->hasAttr("Rotate")) {
                    auto rotate = transform->attribute("Rotate");
                    if(rotate->size() == 3) {
                        Vec3 euler = rotate->asVec3();
                        Quat q = glm::angleAxis(euler.x,
                                                glm::vec3{ 1.0f, 0.0f, 0.0f }) *
                            glm::angleAxis(euler.y,
                                           glm::vec3{ 0.0f, 1.0f, 0.0f }) *
                            glm::angleAxis(euler.z,
                                           glm::vec3{ 0.0f, 0.0f, 1.0f });
                        res.rotate = q;
                        // Check
                        glm::vec3 ang = glm::eulerAngles(q);
                        ASSERT(ang.x == euler.x, "Bad quat cast");
                        ASSERT(ang.y == euler.y, "Bad quat cast");
                        ASSERT(ang.z == euler.z, "Bad quat cast");
                    } else {
                        Vec4 v = rotate->asVec4();
                        res.rotate.x = v.x, res.rotate.y = v.y,
                        res.rotate.z = v.z, res.rotate.w = v.w;
                    }
                }
            }
            return res;
        }
        BUS_TRACE_END();
    }
};
