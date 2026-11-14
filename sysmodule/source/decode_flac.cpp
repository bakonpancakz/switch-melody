// FLAC with libFLAC's stream decoder, reading through the buffered SD file
#include <FLAC/stream_decoder.h>

#include <cstdlib>
#include <cstring>
#include <new>

#include "decoder.h"

namespace {

class FlacDecoder : public Decoder {
public:
    ~FlacDecoder() override {
        if (dec_) FLAC__stream_decoder_delete(dec_);
        free(pcm_);
    }

    bool init(Stream* s, const Tags& t) {
        s_ = s;
        rate = t.rate;
        if (stream_seek(s, 0) != 0 || !(dec_ = FLAC__stream_decoder_new())) return false;
        FLAC__stream_decoder_set_md5_checking(dec_, false);
        if (FLAC__stream_decoder_init_stream(dec_, onRead, onSeek, onTell, onLength, onEof, onWrite, nullptr, onError,
                                             this) != FLAC__STREAM_DECODER_INIT_STATUS_OK)
            return false;
        return FLAC__stream_decoder_process_until_end_of_metadata(dec_);
    }

    size_t read(float* out, size_t max) override {
        size_t n = 0;
        while (n < max) {
            if (at_ == have_) {
                at_ = have_ = 0;
                if (!FLAC__stream_decoder_process_single(dec_)) break;
                FLAC__StreamDecoderState st = FLAC__stream_decoder_get_state(dec_);
                if (have_ == 0 && (st == FLAC__STREAM_DECODER_END_OF_STREAM || st == FLAC__STREAM_DECODER_ABORTED)) break;
                continue;
            }
            size_t k = have_ - at_;
            if (k > max - n) k = max - n;
            memcpy(out + 2 * n, pcm_ + 2 * at_, sizeof(float) * 2 * k);
            n += k;
            at_ += k;
        }
        return n;
    }

    bool seek(uint64_t frame) override {
        at_ = have_ = 0;
        if (FLAC__stream_decoder_seek_absolute(dec_, frame)) return true;
        if (FLAC__stream_decoder_get_state(dec_) == FLAC__STREAM_DECODER_SEEK_ERROR) FLAC__stream_decoder_flush(dec_);
        at_ = have_ = 0;
        return false;
    }

private:
    static FLAC__StreamDecoderReadStatus onRead(const FLAC__StreamDecoder*, FLAC__byte buf[], size_t* bytes, void* c) {
        FlacDecoder* d = (FlacDecoder*)c;
        if (*bytes == 0) return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
        *bytes = stream_read(d->s_, buf, *bytes);
        return *bytes ? FLAC__STREAM_DECODER_READ_STATUS_CONTINUE : FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
    }
    static FLAC__StreamDecoderSeekStatus onSeek(const FLAC__StreamDecoder*, FLAC__uint64 off, void* c) {
        return stream_seek(((FlacDecoder*)c)->s_, (int64_t)off) == 0 ? FLAC__STREAM_DECODER_SEEK_STATUS_OK
                                                                     : FLAC__STREAM_DECODER_SEEK_STATUS_ERROR;
    }
    static FLAC__StreamDecoderTellStatus onTell(const FLAC__StreamDecoder*, FLAC__uint64* off, void* c) {
        *off = (FLAC__uint64)stream_tell(((FlacDecoder*)c)->s_);
        return FLAC__STREAM_DECODER_TELL_STATUS_OK;
    }
    static FLAC__StreamDecoderLengthStatus onLength(const FLAC__StreamDecoder*, FLAC__uint64* len, void* c) {
        *len = (FLAC__uint64)((FlacDecoder*)c)->s_->size;
        return FLAC__STREAM_DECODER_LENGTH_STATUS_OK;
    }
    static FLAC__bool onEof(const FLAC__StreamDecoder*, void* c) {
        Stream* s = ((FlacDecoder*)c)->s_;
        return stream_tell(s) >= s->size;
    }
    static FLAC__StreamDecoderWriteStatus onWrite(const FLAC__StreamDecoder*, const FLAC__Frame* frame,
                                                  const FLAC__int32* const buffer[], void* c) {
        FlacDecoder* d = (FlacDecoder*)c;
        unsigned n = frame->header.blocksize, ch = frame->header.channels;
        if (n > d->cap_) {
            float* p = (float*)realloc(d->pcm_, sizeof(float) * 2 * n);
            if (!p) return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
            d->pcm_ = p;
            d->cap_ = n;
        }
        const float scale = 1.0f / (float)(1u << (frame->header.bits_per_sample - 1));
        const FLAC__int32* L = buffer[0];
        const FLAC__int32* R = ch > 1 ? buffer[1] : buffer[0];  // (more than two: the front pair)
        for (unsigned i = 0; i < n; i++) {
            d->pcm_[2 * i] = L[i] * scale;
            d->pcm_[2 * i + 1] = R[i] * scale;
        }
        d->at_ = 0;
        d->have_ = n;
        return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
    }
    static void onError(const FLAC__StreamDecoder*, FLAC__StreamDecoderErrorStatus, void*) {}

    Stream* s_ = nullptr;
    FLAC__StreamDecoder* dec_ = nullptr;
    float* pcm_ = nullptr;  // the last frame decoded
    size_t cap_ = 0, at_ = 0, have_ = 0;
};

}  // namespace

Decoder* flacOpen(Stream* s, const Tags& t) {
    FlacDecoder* d = new (std::nothrow) FlacDecoder();
    if (d && !d->init(s, t)) {
        delete d;
        d = nullptr;
    }
    return d;
}
