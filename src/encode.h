/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef ENCODE_H
#define ENCODE_H

#include <wchar.h>
#include "probe.h"

typedef enum {
    ENC_CONT_MKV = 0,
    ENC_CONT_MP4 = 1
} EncodeContainer;

typedef enum {
    ENC_VCODEC_HEVC = 0,
    ENC_VCODEC_H264 = 1
} EncodeVideoCodec;

typedef enum {
    ENC_ACODEC_COPY = 0,
    ENC_ACODEC_OPUS = 1,
    ENC_ACODEC_AAC = 2,
    ENC_ACODEC_MP3 = 3,
    ENC_ACODEC_FLAC = 4,
    ENC_ACODEC_AC3 = 5,
    ENC_ACODEC_EAC3 = 6,
    ENC_ACODEC_VORBIS = 7,
    ENC_ACODEC_COUNT = 8
} EncodeAudioCodec;

typedef enum {
    ENC_ABITRATE_VOICE = 0,
    ENC_ABITRATE_MUSIC = 1,
    ENC_ABITRATE_HIGH = 2,
    ENC_ABITRATE_CUSTOM = 3
} EncodeBitratePreset;

typedef struct {
    int container;       /* EncodeContainer */
    int video_codec;     /* EncodeVideoCodec */
    int audio_codec;     /* EncodeAudioCodec */
    int bitrate;         /* EncodeBitratePreset */
    int custom_kbps;
    int downmix_stereo;
} EncodeOptions;

typedef struct {
    double fraction;     /* 0..1 */
    double out_seconds;
    double speed;
    int verifying;
} EncodeProgress;

typedef void (*EncodeProgressCallback)(const EncodeProgress *progress, void *user);

typedef enum {
    ENCODE_OK = 0,
    ENCODE_CANCELLED,
    ENCODE_ERROR,
    ENCODE_VERIFY_FAILED
} EncodeResult;

typedef enum {
    ENCODE_ERR_NONE = 0,
    ENCODE_ERR_TOOL,
    ENCODE_ERR_CODEC,
    ENCODE_ERR_AUDIO,
    ENCODE_ERR_SUBS,
    ENCODE_ERR_PATH,
    ENCODE_ERR_FOLDER,
    ENCODE_ERR_START,
    ENCODE_ERR_FFMPEG,
    ENCODE_ERR_VERIFY,
    ENCODE_ERR_RENAME,
    ENCODE_ERR_OUT_OF_MEMORY
} EncodeError;

#define ENC_BITRATE_MIN_KBPS 16
#define ENC_BITRATE_MAX_KBPS 512
#define ENC_BITRATE_VOICE_KBPS 24
#define ENC_BITRATE_MUSIC_KBPS 36
#define ENC_BITRATE_HIGH_KBPS 64

void encode_options_defaults(EncodeOptions *opt, const SourceInfo *src);
int  encode_audio_available(int container, int codec);
int  encode_audio_copy_available(const SourceInfo *src, int container);
int  encode_default_audio(int container);
int  encode_audio_has_bitrate(int codec);
int  encode_audio_min_kbps(int codec);
int  encode_audio_force_stereo(int codec);
void encode_fix_bitrate(EncodeOptions *opt);
int  encode_bitrate_kbps(const EncodeOptions *opt);
int  encode_audio_downmix_track(const SourceInfo *src, const EncodeOptions *opt,
                                int track_index);
int  encode_any_downmix(const SourceInfo *src, const EncodeOptions *opt);
int  encode_audio_track_copies(const SourceInfo *src, const EncodeOptions *opt,
                               int track_index);
int  encode_has_multichannel(const SourceInfo *src);
int  encode_subtitle_is_text(const wchar_t *codec);
int  encode_text_subtitle_count(const SourceInfo *src);
int  encode_bitmap_subtitle_count(const SourceInfo *src);
int  encode_preview_output_path(const SourceInfo *src, int container,
                                wchar_t *out, size_t cap);
int  encode_build_command(const SourceInfo *src, int encoder, int level,
                          const EncodeOptions *opt, const wchar_t *out_path,
                          wchar_t *cmd, size_t cap, EncodeError *error);
EncodeResult encode_run(const SourceInfo *src, int encoder, int level,
                        const EncodeOptions *opt,
                        EncodeProgressCallback cb, void *user,
                        volatile int *cancel,
                        wchar_t *out_path, size_t out_cap,
                        long long *out_size, EncodeError *error,
                        wchar_t *detail, size_t detail_cap);
void encode_open_folder(const wchar_t *file_path);

#endif /* ENCODE_H */
