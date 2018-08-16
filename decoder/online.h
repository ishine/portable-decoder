// decoder/online.h

// wujian@2018

#ifndef ONLINE_H
#define ONLINE_H

#include "decoder/common.h"
#include "decoder/config.h"
#include "decoder/signal.h"

enum VadStatus { kSilence, kActive };

enum FeatureType { 
    kSpectrogram,
    kFbank,
    kMfcc,
    kUnkown
};

FeatureType StringToFeatureType(const std::string &type);

struct EnergyVadOpts {
    Float32 threshold_in_db;
    Int32 transition_context;
    Int32 frame_length, frame_shift;

    EnergyVadOpts(Float32 thres, Int32 context = 10, Int32 frame_length = 400, 
                  Int32 frame_shift = 160): threshold_in_db(thres), transition_context(context),
                                            frame_length(frame_length), 
                                            frame_shift(frame_shift) { }

    EnergyVadOpts(const EnergyVadOpts &opts): threshold_in_db(opts.threshold_in_db),
                                            transition_context(opts.transition_context),
                                            frame_length(opts.frame_length),
                                            frame_shift(opts.frame_shift) { }
    void Check() { 
        ASSERT(transition_context >= 1);
        ASSERT(frame_length > frame_shift);
        ASSERT(frame_length > 0 && frame_shift > 0);
    }

};

class EnergyVadWrapper {
public:
    EnergyVadWrapper(const EnergyVadOpts vad_opts): vad_opts_(vad_opts) { 
        // remove_dc=false
        FrameOpts frame_opts(vad_opts.frame_length, vad_opts.frame_shift, 8000, 0.0, kNone, false);
        vad_opts_.Check(); frame_opts.Check();
        splitter_ = new FrameSplitter(frame_opts);
        frame_cache_ = new Float32[vad_opts.frame_length];
        // Initialize state
        inner_steps_ = 0; inner_status_ = kSilence;
    }
    
    ~EnergyVadWrapper() {
        if (splitter_)
            delete splitter_;
        if (frame_cache_)
            delete[] frame_cache_;
    }

    Int32 Context() { return vad_opts_.transition_context; }

    Bool Run(Float32 *signal, Int32 num_samps, std::vector<Bool> *vad_status);

private:
    // Run one step
    Bool Run(Float32 energy);

    FrameSplitter *splitter_;
    EnergyVadOpts vad_opts_;

    VadStatus inner_status_;
    Int32 inner_steps_;
    Float32 *frame_cache_;
};

// Simple wrapper
class FeatureExtractor {
public:
    FeatureExtractor(const std::string& conf, const std::string &type):
                    computer_(NULL) { 
        type_ = StringToFeatureType(type);
        ConfigureParser parser(conf);
        // initialize
        SpectrogramOpts spectrogram_opts;
        FbankOpts fbank_opts;
        MfccOpts mfcc_opts;
        switch (type_) {
            case kSpectrogram:
                LOG_INFO << "Create FeatureExtractor(Spectrogram)";
                spectrogram_opts.ParseConfigure(&parser);
                computer_ = new SpectrogramComputer(spectrogram_opts);
                break;
            case kFbank:
                LOG_INFO << "Create FeatureExtractor(Fbank)";
                fbank_opts.ParseConfigure(&parser);
                computer_ = new FbankComputer(fbank_opts);
                break;
            case kMfcc:
                LOG_INFO << "Create FeatureExtractor(Mfcc)";
                mfcc_opts.ParseConfigure(&parser);
                computer_ = new MfccComputer(mfcc_opts);
                break;
            case kUnkown:
                LOG_FAIL << "Unknown feature type: " << type;
                break;
        }
    }

    Int32 Compute(Float32 *signal, Int32 num_samps, Float32 *addr, Int32 stride) {
        Int32 num_frames = -1;
        switch (type_) {
            case kSpectrogram:
            case kFbank:
            case kMfcc:
                num_frames = ComputeFeature(computer_, signal, num_samps, addr, stride);
                break;
            case kUnkown:
                LOG_FAIL << "Init FeatureExtractor with unknown feature type, stop compute";
                break;
        }
        return num_frames;
    }


    Int32 FeatureDim() { return computer_->FeatureDim(); }

    Int32 NumFrames(Int32 num_samps) { return computer_->NumFrames(num_samps); }

    ~FeatureExtractor() {
        if (computer_ != NULL)
            delete computer_;
    }

private:
    FeatureType type_;
    Computer *computer_;
};

struct ContextOpts {
    Int32 left_context, right_context;
    std::string cmvn;

    ContextOpts(Int32 left = 0, Int32 right = 0): left_context(left), right_context(right), cmvn("") { }
    
    ContextOpts(const ContextOpts &opts): left_context(opts.left_context), right_context(opts.right_context),
                                        cmvn(opts.cmvn) { }
};

// Using vad
class OnlineExtractor {
public:
    OnlineExtractor(const std::string &conf, const std::string &type, 
                    EnergyVadOpts vad_opts): extractor_(conf, type), vad_(vad_opts) {
        speech_buffer_.reserve(buffer_size);
        status_buffer_.reserve(buffer_size);
    }

    Int32 NumFramesReady();

    Int32 Dim() { return extractor_.FeatureDim(); }

    void Receive(Float32 *wav, Int32 num_samps);

    void Compute(Float32 *feats, Int32 stride);
    
private:
    const Int32 buffer_size = 160 * 500;

    FeatureExtractor extractor_;
    EnergyVadWrapper vad_;
    
    std::vector<Float32> speech_buffer_;
    std::vector<Bool> status_buffer_;
};

#endif