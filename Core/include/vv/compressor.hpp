// Port of the Web Audio DynamicsCompressorNode (Chromium/WebKit DynamicsCompressorKernel) - the web app's master limiter.
// Float32 arithmetic on purpose, same operation order as the browser. Stereo-linked (detector = max over channels).
#pragma once
#include <cmath>
#include <algorithm>
#include <vector>
#include <cstring>

namespace vv {
class Compressor {
public:
    struct Params { float thresholdDb = -24, kneeDb = 30, ratio = 12, attack = 0.003f, release = 0.25f, postGainDb = 0, effectBlend = 1; float preDelay = 0.006f; };
    void prepare(double sr) {
        sr_ = (float)sr; maxPre_ = 1; while (maxPre_ < 1024) maxPre_ <<= 1; mask_ = maxPre_ - 1;
        for (auto& b : pre_) b.assign(maxPre_, 0.f);
        meteringReleaseK_ = 1.f - std::exp(-1.f / (sr_ * 0.325f));
        reset();
    }
    void reset() {
        for (auto& b : pre_) std::fill(b.begin(), b.end(), 0.f);
        detectorAverage_ = 0; compressorGain_ = 1; maxAttackDiffDb_ = -1; meteringGain_ = 1; lastPreFrames_ = -1; readIdx_ = writeIdx_ = 0;
        dbThreshold_ = dbKnee_ = ratio_ = -12345.f;
    }
    float meteringGainDb() const { return meteringGain_; }
    // Processes n frames in place-capable fashion. in/out may alias. Works in blocks of 32 like the browser.
    void process(const float* inL, const float* inR, float* outL, float* outR, int n, const Params& p) {
        const float dryMix = 1.f - p.effectBlend, wetMix = p.effectBlend;
        const float k = updateStaticCurve(p.thresholdDb, p.kneeDb, p.ratio);
        float fullRangeGain = saturate(1.f, k);
        float makeup = std::pow(1.f / fullRangeGain, 0.6f);
        const float masterLinearGain = dbToLin(p.postGainDb) * makeup;
        const float attackTime = std::max(0.001f, p.attack);
        const float attackFrames = attackTime * sr_;
        const float releaseFrames0 = sr_ * p.release;
        const float satReleaseFrames = 0.0025f * sr_;
        const float y1 = releaseFrames0 * 0.09f, y2 = releaseFrames0 * 0.16f, y3 = releaseFrames0 * 0.42f, y4 = releaseFrames0 * 0.98f;
        const float kA = 0.9999999999999998f * y1 + 1.8432219684323923e-16f * y2 - 1.9373394351676423e-16f * y3 + 8.824516011816245e-18f * y4;
        const float kB = -1.5788320352845888f * y1 + 2.3305837032074286f * y2 - 0.9141194204840429f * y3 + 0.1623677525612032f * y4;
        const float kC = 0.5334142869106424f * y1 - 1.272736789213631f * y2 + 0.9258856042207512f * y3 - 0.18656310191776226f * y4;
        const float kD = 0.08783463138207234f * y1 - 0.1694162967925622f * y2 + 0.08588057951595272f * y3 - 0.00429891410546283f * y4;
        const float kE = -0.042416883008123074f * y1 + 0.1115693827987602f * y2 - 0.09764676325265872f * y3 + 0.028494263462021576f * y4;
        setPreDelay(p.preDelay);
        const float kPi = 3.14159265358979323846f;
        int frame = 0;
        const int nDiv = n / 32;
        for (int d = 0; d < nDiv; d++) {
            if (std::isnan(detectorAverage_) || std::isinf(detectorAverage_)) detectorAverage_ = 1;
            float desiredGain = detectorAverage_;
            float scaledDesiredGain = std::asin(desiredGain) / (0.5f * kPi);
            float envelopeRate;
            bool isReleasing = scaledDesiredGain > compressorGain_;
            float compressionDiffDb = linToDb(compressorGain_ / scaledDesiredGain);
            if (isReleasing) {
                maxAttackDiffDb_ = -1;
                if (std::isnan(compressionDiffDb) || std::isinf(compressionDiffDb)) compressionDiffDb = -1;
                float x = std::max(-12.f, std::min(0.f, compressionDiffDb));
                x = 0.25f * (x + 12.f);
                float x2 = x * x, x3 = x2 * x, x4 = x2 * x2;
                float releaseFrames = kA + kB * x + kC * x2 + kD * x3 + kE * x4;
                float dbPerFrame = 5.f / releaseFrames;
                envelopeRate = dbToLin(dbPerFrame);
            } else {
                if (std::isnan(compressionDiffDb) || std::isinf(compressionDiffDb)) compressionDiffDb = 1;
                if (maxAttackDiffDb_ == -1 || maxAttackDiffDb_ < compressionDiffDb) maxAttackDiffDb_ = compressionDiffDb;
                float effAttenDiffDb = std::max(0.5f, maxAttackDiffDb_);
                float x = 0.25f / effAttenDiffDb;
                envelopeRate = 1.f - std::pow(x, 1.f / attackFrames);
            }
            int rd = readIdx_, wr = writeIdx_;
            float detectorAverage = detectorAverage_, compressorGain = compressorGain_;
            for (int i = 0; i < 32; i++, frame++) {
                float l = inL[frame], r = inR ? inR[frame] : l;
                float compressorInput = 0;
                pre_[0][wr] = l; pre_[1][wr] = r;
                compressorInput = std::max(std::fabs(l), std::fabs(r));
                float absInput = std::fabs(compressorInput);
                float shapedInput = saturate(absInput, k);
                float attenuation = absInput <= 0.0001f ? 1.f : shapedInput / absInput;
                float attenuationDb = -linToDb(attenuation);
                attenuationDb = std::max(2.f, attenuationDb);
                float dbPerFrame = attenuationDb / satReleaseFrames;
                float satReleaseRate = dbToLin(dbPerFrame) - 1.f;
                bool isRelease = attenuation > detectorAverage;
                float rate = isRelease ? satReleaseRate : 1.f;
                detectorAverage += (attenuation - detectorAverage) * rate;
                detectorAverage = std::min(1.f, detectorAverage);
                if (std::isnan(detectorAverage) || std::isinf(detectorAverage)) detectorAverage = 1;
                if (envelopeRate < 1.f) compressorGain += (scaledDesiredGain - compressorGain) * envelopeRate;
                else { compressorGain *= envelopeRate; compressorGain = std::min(1.f, compressorGain); }
                float postWarp = std::sin(0.5f * kPi * compressorGain);
                float totalGain = dryMix + wetMix * masterLinearGain * postWarp;
                float dbReal = linToDb(postWarp);
                if (dbReal < meteringGain_) meteringGain_ = dbReal; else meteringGain_ += (dbReal - meteringGain_) * meteringReleaseK_;
                outL[frame] = pre_[0][rd] * totalGain;
                if (outR) outR[frame] = pre_[1][rd] * totalGain;
                rd = (rd + 1) & mask_; wr = (wr + 1) & mask_;
            }
            readIdx_ = rd; writeIdx_ = wr; detectorAverage_ = detectorAverage; compressorGain_ = compressorGain;
        }
    }
private:
    static float dbToLin(float db) { return std::pow(10.f, 0.05f * db); }
    static float linToDb(float x) { return 20.f * std::log10(x); }
    void setPreDelay(float t) {
        int frames = (int)(t * sr_); if (frames > maxPre_ - 1) frames = maxPre_ - 1;
        if (lastPreFrames_ != frames) { lastPreFrames_ = frames; for (auto& b : pre_) std::fill(b.begin(), b.end(), 0.f); readIdx_ = 0; writeIdx_ = frames; }
    }
    float kneeCurve(float x, float k) const { if (x < linearThreshold_) return x; return linearThreshold_ + (1.f - std::exp(-k * (x - linearThreshold_))) / k; }
    float saturate(float x, float k) const {
        float y;
        if (x < linearThreshold_) y = x;
        else if (x < kneeThreshold_) y = kneeCurve(x, k);
        else { float xDb = linToDb(x); float yDb = ykneeThresholdDb_ + slope_ * (xDb - kneeThresholdDb_); y = dbToLin(yDb); }
        return y;
    }
    float slopeAt(float x, float k) const {
        if (x < linearThreshold_) return 1.f;
        float x2 = x * 1.001f; float xDb = linToDb(x), x2Db = linToDb(x2);
        float yDb = linToDb(kneeCurve(x, k)), y2Db = linToDb(kneeCurve(x2, k));
        return (y2Db - yDb) / (x2Db - xDb);
    }
    float kAtSlope(float desired) const {
        float xDb = dbThreshold_ + dbKnee_; float x = dbToLin(xDb);
        float minK = 0.1f, maxK = 10000.f, k = 5.f;
        for (int i = 0; i < 15; i++) { float s = slopeAt(x, k); if (s < desired) maxK = k; else minK = k; k = std::sqrt(minK * maxK); }
        return k;
    }
    float updateStaticCurve(float thr, float knee, float ratio) {
        if (thr != dbThreshold_ || knee != dbKnee_ || ratio != ratio_) {
            dbThreshold_ = thr; dbKnee_ = knee; linearThreshold_ = dbToLin(thr);
            ratio_ = ratio; slope_ = 1.f / ratio_;
            k_ = kAtSlope(1.f / ratio_);
            kneeThresholdDb_ = thr + knee; kneeThreshold_ = dbToLin(kneeThresholdDb_);
            ykneeThresholdDb_ = linToDb(kneeCurve(kneeThreshold_, k_));
        }
        return k_;
    }
    float sr_ = 48000, meteringReleaseK_ = 0;
    int maxPre_ = 1024, mask_ = 1023, lastPreFrames_ = -1, readIdx_ = 0, writeIdx_ = 0;
    std::vector<float> pre_[2];
    float detectorAverage_ = 0, compressorGain_ = 1, maxAttackDiffDb_ = -1, meteringGain_ = 1;
    float dbThreshold_ = 0, dbKnee_ = 0, ratio_ = 0, slope_ = 1, linearThreshold_ = 1, kneeThresholdDb_ = 0, kneeThreshold_ = 1, ykneeThresholdDb_ = 0, k_ = 5;
};
}
