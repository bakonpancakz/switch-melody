// Ogg Opus: common/ogg.c's packets into libopus. (Not opusfile: it holds the
// whole tags packet, cover and all, in memory.) Pre-skip and the last page's
// end trimming as RFC 7845 says; a seek starts 80 ms early for the decoder to
// settle and counts samples again from the next page's granule.
#include <opus/opus.h>

#include <cstdlib>
#include <cstring>
#include <new>

#include "decoder.h"
#include "ogg.h"

namespace {

constexpr int MAX_FRAMES = 5760;  // 120 ms, the longest packet
constexpr size_t MAX_PACKET = 16 << 10;
constexpr int64_t PREROLL = 3840;

class OpusDec : public Decoder {
public:
    ~OpusDec() override {
        if (dec_) opus_decoder_destroy(dec_);
        free(pkt_);
        free(pcm_);
    }

    bool init(Stream* s, const Tags& t) {
        if (t.mapping != 0 || t.channels < 1 || t.channels > 2) return false;  // (surround isn't handled)
        rate = 48000;
        ch_ = (int)t.channels;
        preskip_ = t.preskip;
        data_start_ = t.data_start;
        int err = 0;
        dec_ = opus_decoder_create(48000, ch_, &err);
        pkt_ = (uint8_t*)malloc(MAX_PACKET);
        pcm_ = (float*)malloc(sizeof(float) * 2 * MAX_FRAMES);
        if (!dec_ || err != OPUS_OK || !pkt_ || !pcm_) return false;
        opus_decoder_ctl(dec_, OPUS_SET_GAIN(t.gain));
        ogg_init(&r_, s);
        r_.serial = t.serial;
        r_.have_serial = 1;
        return stream_seek(s, data_start_) == 0;
    }

    size_t read(float* out, size_t max) override {
        size_t n = 0;
        while (n < max) {
            if (at_ == have_ && !decodePacket()) break;
            size_t k = have_ - at_;
            if (k > max - n) k = max - n;
            memcpy(out + 2 * n, pcm_ + 2 * at_, sizeof(float) * 2 * k);
            n += k;
            at_ += k;
        }
        return n;
    }

    bool seek(uint64_t frame) override {
        int64_t target = (int64_t)frame + preskip_;
        int64_t from = target - PREROLL;
        int64_t g = ogg_seek_granule(&r_, data_start_, from < 0 ? 0 : from);
        opus_decoder_ctl(dec_, OPUS_RESET_STATE);
        at_ = have_ = 0;
        gpos_ = g;
        synced_ = stream_tell(r_.s) == data_start_;  // (from the start: the count is known)
        if (synced_) gpos_ = 0;
        skip_to_ = target;
        return true;
    }

private:
    // Decode the next packet into pcm_ (what's left after trimming): false at the end
    bool decodePacket() {
        for (;;) {
            at_ = have_ = 0;
            long len = ogg_packet(&r_, pkt_, MAX_PACKET);
            if (len == -1) return false;
            if (len < 0) continue;  // too long for Opus: not a real packet
            int n = opus_decode_float(dec_, pkt_, (opus_int32)len, pcm_, MAX_FRAMES, 0);
            if (n < 0) continue;
            int64_t start = gpos_, end = gpos_ + n;
            if (r_.end_of_page && r_.end_granule >= 0) {
                end = r_.end_granule;  // the page says where this packet ends
                start = end - n;
                if (!synced_) {
                    synced_ = true;
                    gpos_ = end;
                    continue;  // (what came before the count was known)
                }
            }
            gpos_ = end;
            if (!synced_) continue;
            int64_t keep_from = start;
            if (keep_from < skip_to_) keep_from = skip_to_;
            if (keep_from < preskip_) keep_from = preskip_;
            int64_t keep_to = start + n;
            if (r_.end_of_stream && r_.end_granule >= 0 && keep_to > r_.end_granule) keep_to = r_.end_granule;
            if (keep_to <= keep_from) continue;
            if (ch_ == 1)  // mono: each sample twice (from the end back, so nothing's overwritten before it's read)
                for (int i = n - 1; i >= 0; i--) pcm_[2 * i] = pcm_[2 * i + 1] = pcm_[i];
            // (what's kept is a stretch of pcm_, not a copy of it: one buffer, not two)
            at_ = (size_t)(keep_from - start);
            have_ = (size_t)(keep_to - start);
            return true;
        }
    }

    OggReader r_ = {};
    OpusDecoder* dec_ = nullptr;
    uint8_t* pkt_ = nullptr;
    float* pcm_ = nullptr;  // the last packet, stereo; [at_, have_) is still to be given out
    size_t at_ = 0, have_ = 0;
    int ch_ = 2;
    int64_t preskip_ = 0, data_start_ = 0;
    int64_t gpos_ = 0;      // the granule of the next sample decoded
    bool synced_ = true;
    int64_t skip_to_ = 0;   // granule to start giving samples from (after a seek)
};

}  // namespace

Decoder* flacOpen(Stream* s, const Tags& t);

static Decoder* opusOpen(Stream* s, const Tags& t) {
    OpusDec* d = new (std::nothrow) OpusDec();
    if (d && !d->init(s, t)) {
        delete d;
        d = nullptr;
    }
    return d;
}

Decoder* decoderOpen(Stream* s, const Tags& t) {
    if (t.format == TagsFormat_Flac) return flacOpen(s, t);
    if (t.format == TagsFormat_Opus) return opusOpen(s, t);
    return nullptr;
}
