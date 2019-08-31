#pragma once
#include "../../Shared/KernelShared.hpp"

class FresnelDielectric final {
private:
    float mEtaI, mEtaT;

public:
    DEVICE FresnelDielectric(float etaI, float etaT)
        : mEtaI(etaI), mEtaT(etaT) {}
    DEVICE Spectrum eval(float cosThetaI) const;
};

class FresnelConductor final {
private:
    Spectrum mEtaI, mEtaT, mK;

public:
    DEVICE FresnelConductor(const Spectrum& etaI, const Spectrum& etaT,
                            const Spectrum& k)
        : mEtaI(etaI), mEtaT(etaT), mK(k) {}
    DEVICE Spectrum eval(float cosThetaI) const;
};

class Fresnel final {
private:
    union Content {
        FresnelConductor con;
        FresnelDielectric die;
        DEVICE Content(const FresnelConductor& con) : con(con) {}
        DEVICE Content(const FresnelDielectric& die) : die(die) {}
    } mContent;
    bool mConductor;

public:
    DEVICE Fresnel(const FresnelConductor& con)
        : mConductor(true), mContent(con) {}
    DEVICE Fresnel(const FresnelDielectric& die)
        : mConductor(false), mContent(die) {}
    DEVICE Spectrum eval(float cosThetaI) const {
        return mConductor ? mContent.con.eval(cosThetaI) :
                            mContent.die.eval(cosThetaI);
    }
};
