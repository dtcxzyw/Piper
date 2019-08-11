#pragma once
#include "../../CUDA.hpp"
#include "Fresnel.hpp"

class LambertianReflection final {
private:
    Spectrum mR;
public:
    DEVICE LambertianReflection(const Spectrum &r) :mR(r) {}
    DEVICE Spectrum f(const Vec3 &, const Vec3 &) const;
    DEVICE Vec3 sampleF(const Vec3 &wo, const Vec2 &u) const;
    DEVICE float pdf(const Vec3 &wo, const Vec3 &wi) const;
};

class MicrofacetReflection final {
private:
    Spectrum mR;
    Vec2 mAlpha;
    Fresnel mFresnel;
    static DEVICE Vec2 toAlpha(const Vec2 &roughness);
public:
    DEVICE MicrofacetReflection(const Spectrum &r, const Vec2 &roughness,
        const Fresnel &fresnel) :mR(r), mAlpha(toAlpha(roughness)),
        mFresnel(fresnel) {}
    DEVICE Spectrum f(const Vec3 &wo, const Vec3 &wi) const;
    DEVICE Vec3 sampleF(const Vec3 &wo, const Vec2 &u) const;
    DEVICE float pdf(const Vec3 &wo, const Vec3 &wi) const;
};

class BxDFWarpper final {
private:
    enum class Type {
        BxDFLambertianReflection,
        BxDFMicrofacetReflection
    } mType;
    union Content {
        LambertianReflection lr;
        MicrofacetReflection mr;
        DEVICE Content(const LambertianReflection &lr) : lr(lr) {}
        DEVICE Content(const MicrofacetReflection &mr) : mr(mr) {}
    } mContent;
public:
    DEVICE BxDFWarpper(const LambertianReflection &lr)
        :mType(Type::BxDFLambertianReflection), mContent(lr) {}
    DEVICE BxDFWarpper(const MicrofacetReflection &mr)
        : mType(Type::BxDFMicrofacetReflection), mContent(mr) {}
    DEVICE Spectrum f(const Vec3 &wo, const Vec3 &wi) const {
        switch (mType) {
            case Type::BxDFLambertianReflection:return mContent.lr.f(wo, wi);
            case Type::BxDFMicrofacetReflection:return mContent.mr.f(wo, wi);
        }
    }
    DEVICE Vec3 sampleF(const Vec3 &wo, const Vec2 &u) const {
        switch (mType) {
            case Type::BxDFLambertianReflection:return mContent.lr.sampleF(wo, u);
            case Type::BxDFMicrofacetReflection:return mContent.mr.sampleF(wo, u);
        }
    }
    DEVICE float pdf(const Vec3 &wo, const Vec3 &wi) const {
        switch (mType) {
            case Type::BxDFLambertianReflection:return mContent.lr.pdf(wo, wi);
            case Type::BxDFMicrofacetReflection:return mContent.mr.pdf(wo, wi);
        }
    }
};
