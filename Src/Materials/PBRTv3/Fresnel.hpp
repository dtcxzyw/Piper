#pragma once
#include "../../Shared/KernelShared.hpp"

class FresnelDielectric final {
private:
    float mEtaI, mEtaT;

public:
    INLINEDEVICE FresnelDielectric(float etaI, float etaT)
        : mEtaI(etaI), mEtaT(etaT) {}
    INLINEDEVICE Spectrum eval(float cosThetaI) const {
        float etaI = mEtaI, etaT = mEtaT;
        if(cosThetaI < 0.0f) {
            cosThetaI = -cosThetaI;
            swap(etaI, etaT);
        }
        float sinThetaT = etaI / etaT * cos2Sin(cosThetaI);

        // Total internal reflection
        if(sinThetaT >= 1.0f)
            return Spectrum{ 1.0f };

        float cosThetaT = sin2Cos(sinThetaT);

        float ii = etaI * cosThetaI, it = etaI * cosThetaT;
        float ti = etaT * cosThetaI, tt = etaT * cosThetaT;

        float a = (ti - it) / (ti + it);
        float b = (ii - tt) / (ii + tt);

        return Spectrum{ 0.5f * (a * a + b * b) };
    }
};

class FresnelConductor final {
private:
    Spectrum mEtaI, mEtaT, mK;

public:
    INLINEDEVICE FresnelConductor(const Spectrum& etaI, const Spectrum& etaT,
                                  const Spectrum& k)
        : mEtaI(etaI), mEtaT(etaT), mK(k) {}
    // https://seblagarde.wordpress.com/2013/04/29/memo-on-fresnel-equations/
    INLINEDEVICE Spectrum eval(float cosThetaI) const {
        cosThetaI = fabsf(cosThetaI);
        Spectrum eta = mEtaT / mEtaI;
        Spectrum etak = mK / mEtaI;

        float cosThetaI2 = cosThetaI * cosThetaI;
        float sinThetaI2 = 1.0f - cosThetaI2;
        Spectrum eta2 = eta * eta;
        Spectrum etak2 = etak * etak;

        Spectrum t0 = eta2 - etak2 - sinThetaI2;
        Spectrum a2plusb2 = sqrtf(t0 * t0 + 4.0f * eta2 * etak2);
        Spectrum t1 = a2plusb2 + cosThetaI2;
        Spectrum a = sqrtf(0.5f * (a2plusb2 + t0));
        Spectrum t2 = 2.0f * cosThetaI * a;
        Spectrum Rs = (t1 - t2) / (t1 + t2);

        Spectrum t3 = cosThetaI2 * a2plusb2 + sinThetaI2 * sinThetaI2;
        Spectrum t4 = t2 * sinThetaI2;
        Spectrum Rp = Rs * (t3 - t4) / (t3 + t4);

        return 0.5f * (Rp + Rs);
    }
};

class Fresnel final {
private:
    union Content {
        FresnelConductor con;
        FresnelDielectric die;
        INLINEDEVICE Content(const FresnelConductor& con) : con(con) {}
        INLINEDEVICE Content(const FresnelDielectric& die) : die(die) {}
    } mContent;
    bool mConductor;

public:
    INLINEDEVICE Fresnel(const FresnelConductor& con)
        : mConductor(true), mContent(con) {}
    INLINEDEVICE Fresnel(const FresnelDielectric& die)
        : mConductor(false), mContent(die) {}
    INLINEDEVICE Spectrum eval(float cosThetaI) const {
        return mConductor ? mContent.con.eval(cosThetaI) :
                            mContent.die.eval(cosThetaI);
    }
};
