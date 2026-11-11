/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "probe.h"
#include "hw.h"
#include "presets.h"
#include "estimate.h"
#include "encode.h"
#include "strings.h"
#include "util.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("FAIL: %s\n", msg); } \
} while (0)

static void check_close(double a, double b, double eps, const char *msg)
{
    double d = a - b;
    if (d < 0) d = -d;
    CHECK(d <= eps, msg);
}

static ProbeError parse(const char *json, SourceInfo *out)
{
    return probe_parse_json(json, out);
}

static void test_h264_sdr(void)
{
    static const char *json =
        "{"
        "\"streams\":["
        "{\"codec_type\":\"video\",\"codec_name\":\"h264\",\"codec_long_name\":\"H.264 / AVC\","
        "\"profile\":\"High\",\"width\":1920,\"height\":1080,\"pix_fmt\":\"yuv420p\","
        "\"avg_frame_rate\":\"30/1\",\"r_frame_rate\":\"30/1\",\"bit_rate\":\"8000000\","
        "\"color_primaries\":\"bt709\",\"color_transfer\":\"bt709\",\"color_space\":\"bt709\","
        "\"color_range\":\"tv\",\"field_order\":\"progressive\",\"sample_aspect_ratio\":\"1:1\","
        "\"display_aspect_ratio\":\"16:9\",\"nb_frames\":\"1800\"},"
        "{\"codec_type\":\"audio\",\"codec_name\":\"aac\",\"codec_long_name\":\"AAC\","
        "\"channels\":2,\"sample_rate\":\"48000\",\"bit_rate\":\"128000\","
        "\"tags\":{\"language\":\"eng\"}}"
        "],"
        "\"format\":{\"format_name\":\"mov,mp4,m4a,3gp,3g2,mj2\","
        "\"format_long_name\":\"QuickTime / MOV\",\"duration\":\"60.0\","
        "\"size\":\"60000000\",\"bit_rate\":\"8000000\"}"
        "}";
    SourceInfo s;
    CHECK(parse(json, &s) == PROBE_OK, "H.264 fixture parses");
    CHECK(s.coded_width == 1920 && s.coded_height == 1080, "H.264 resolution");
    CHECK(s.display_width == 1920 && s.display_height == 1080, "H.264 display size");
    check_close(s.duration, 60.0, 0.01, "H.264 duration");
    check_close(s.fps, 30.0, 0.001, "H.264 fps");
    CHECK(!s.is_vfr, "H.264 is CFR");
    CHECK(!s.is_hdr, "H.264 is SDR");
    CHECK(s.bit_depth == 8, "H.264 is 8-bit");
    CHECK(s.video_track_count == 1, "H.264 video track count");
    CHECK(s.audio_track_count == 1, "H.264 audio track count");
    CHECK(s.subtitle_track_count == 0, "H.264 subtitle count");
    CHECK(wcscmp(s.audio_codec, L"aac") == 0, "H.264 audio codec");
    CHECK(wcscmp(s.audio_language, L"eng") == 0, "H.264 audio language");
    CHECK(s.audio_channels == 2 && s.audio_sample_rate == 48000, "H.264 audio format");
    CHECK(s.file_size == 60000000, "H.264 file size");
    CHECK(s.video_bitrate == 8000000, "H.264 video bitrate");
    CHECK(s.bits_per_pixel > 0.12 && s.bits_per_pixel < 0.14, "H.264 bits per pixel");
    CHECK(wcsstr(s.container, L"mp4") != NULL, "H.264 container");
}

static void test_hevc_hdr_rotated(void)
{
    static const char *json =
        "{"
        "\"streams\":["
        "{\"codec_type\":\"video\",\"codec_name\":\"hevc\",\"codec_long_name\":\"HEVC\","
        "\"profile\":\"Main 10\",\"width\":3840,\"height\":2160,\"pix_fmt\":\"yuv420p10le\","
        "\"avg_frame_rate\":\"30000/1001\",\"r_frame_rate\":\"30000/1001\","
        "\"bit_rate\":\"20000000\",\"color_primaries\":\"bt2020\","
        "\"color_transfer\":\"smpte2084\",\"color_space\":\"bt2020nc\",\"color_range\":\"tv\","
        "\"field_order\":\"progressive\",\"sample_aspect_ratio\":\"1:1\","
        "\"side_data_list\":[{\"side_data_type\":\"Display Matrix\",\"rotation\":90}]},"
        "{\"codec_type\":\"audio\",\"codec_name\":\"eac3\",\"channels\":6,"
        "\"sample_rate\":\"48000\",\"bit_rate\":\"640000\"}"
        "],"
        "\"format\":{\"format_name\":\"matroska,webm\",\"duration\":\"120.0\","
        "\"size\":\"400000000\",\"bit_rate\":\"26666666\"}"
        "}";
    SourceInfo s;
    CHECK(parse(json, &s) == PROBE_OK, "HEVC HDR fixture parses");
    CHECK(s.is_hdr, "HEVC HDR flag");
    CHECK(!s.is_hlg, "HEVC is HDR10, not HLG");
    CHECK(s.bit_depth == 10, "HEVC is 10-bit");
    CHECK(s.rotation == 90, "HEVC rotation");
    CHECK(s.display_width == 2160 && s.display_height == 3840, "HEVC display size after rotation");
    CHECK(s.audio_track_count == 1 && s.audio_channels == 6, "HEVC 5.1 audio");
    CHECK(wcscmp(s.audio_codec, L"eac3") == 0, "HEVC E-AC-3 audio");
    CHECK(wcscmp(s.color_transfer, L"smpte2084") == 0, "HEVC color transfer");
    CHECK(wcsstr(s.container, L"matroska") != NULL, "HEVC container");
}

static void test_vfr_interlaced_anamorphic(void)
{
    static const char *json =
        "{"
        "\"streams\":["
        "{\"codec_type\":\"video\",\"codec_name\":\"h264\",\"width\":1280,\"height\":720,"
        "\"pix_fmt\":\"yuv420p\",\"avg_frame_rate\":\"24000/1001\",\"r_frame_rate\":\"30/1\","
        "\"bit_rate\":\"3000000\",\"field_order\":\"tt\",\"sample_aspect_ratio\":\"4:3\","
        "\"display_aspect_ratio\":\"16:9\"}"
        "],"
        "\"format\":{\"format_name\":\"matroska,webm\",\"duration\":\"30.0\","
        "\"size\":\"12000000\",\"bit_rate\":\"3200000\"}"
        "}";
    SourceInfo s;
    CHECK(parse(json, &s) == PROBE_OK, "VFR fixture parses");
    CHECK(s.is_vfr, "VFR detected");
    CHECK(s.is_interlaced, "Interlaced detected");
    CHECK(s.is_anamorphic, "Anamorphic detected");
    CHECK(s.audio_track_count == 0, "No audio detected");
    check_close(s.fps, 23.976, 0.01, "VFR average fps");
    check_close(s.r_fps, 30.0, 0.001, "VFR real fps");
}

static void test_dolby_vision(void)
{
    static const char *json =
        "{"
        "\"streams\":["
        "{\"codec_type\":\"video\",\"codec_name\":\"hevc\",\"width\":3840,\"height\":2160,"
        "\"pix_fmt\":\"yuv420p10le\",\"avg_frame_rate\":\"24/1\",\"r_frame_rate\":\"24/1\","
        "\"color_primaries\":\"bt2020\",\"color_transfer\":\"smpte2084\","
        "\"side_data_list\":[{\"side_data_type\":\"DOVI configuration record\","
        "\"dv_profile\":8,\"dv_level\":6}]}"
        "],"
        "\"format\":{\"format_name\":\"mov,mp4\",\"duration\":\"60.0\",\"size\":\"100000000\"}"
        "}";
    SourceInfo s;
    CHECK(parse(json, &s) == PROBE_OK, "Dolby Vision fixture parses");
    CHECK(s.is_dolby_vision, "Dolby Vision detected");
    CHECK(s.is_hdr, "Dolby Vision is HDR");
    CHECK(s.bit_depth == 10, "Dolby Vision 10-bit");
}

static void test_wide_gamut_sdr(void)
{
    static const char *json =
        "{"
        "\"streams\":["
        "{\"codec_type\":\"video\",\"codec_name\":\"h264\",\"width\":1920,\"height\":1080,"
        "\"pix_fmt\":\"yuv420p\",\"avg_frame_rate\":\"30/1\",\"r_frame_rate\":\"30/1\","
        "\"color_primaries\":\"bt2020\",\"color_transfer\":\"bt709\","
        "\"color_space\":\"bt2020nc\"}"
        "],"
        "\"format\":{\"format_name\":\"mp4\",\"duration\":\"10.0\",\"size\":\"10000000\"}"
        "}";
    SourceInfo s;
    CHECK(parse(json, &s) == PROBE_OK, "Wide-gamut SDR fixture parses");
    CHECK(!s.is_hdr, "BT.2020 with BT.709 transfer is not HDR");
}

static void test_legacy_rotation_tag(void)
{
    static const char *json =
        "{"
        "\"streams\":["
        "{\"codec_type\":\"video\",\"codec_name\":\"h264\",\"width\":1920,\"height\":1080,"
        "\"pix_fmt\":\"yuv420p\",\"avg_frame_rate\":\"30/1\",\"r_frame_rate\":\"30/1\","
        "\"tags\":{\"rotate\":\"270\"}}"
        "],"
        "\"format\":{\"format_name\":\"mov,mp4\",\"duration\":\"10.0\",\"size\":\"10000000\"}"
        "}";
    SourceInfo s;
    CHECK(parse(json, &s) == PROBE_OK, "Legacy rotation fixture parses");
    CHECK(s.rotation == 270, "Legacy rotation value");
    CHECK(s.display_width == 1080 && s.display_height == 1920, "Legacy rotation display size");
}

static void test_opus_webm(void)
{
    static const char *json =
        "{"
        "\"streams\":["
        "{\"codec_type\":\"video\",\"codec_name\":\"vp9\",\"width\":1920,\"height\":1080,"
        "\"pix_fmt\":\"yuv420p\",\"avg_frame_rate\":\"30/1\",\"r_frame_rate\":\"30/1\","
        "\"bit_rate\":\"2000000\"},"
        "{\"codec_type\":\"audio\",\"codec_name\":\"opus\",\"channels\":2,"
        "\"sample_rate\":\"48000\",\"bit_rate\":\"96000\"}"
        "],"
        "\"format\":{\"format_name\":\"matroska,webm\",\"duration\":\"10.0\",\"size\":\"2500000\"}"
        "}";
    SourceInfo s;
    CHECK(parse(json, &s) == PROBE_OK, "Opus/WebM fixture parses");
    CHECK(wcscmp(s.audio_codec, L"opus") == 0, "Opus audio");
    CHECK(s.video_track_count == 1 && s.audio_track_count == 1, "Opus track counts");
    CHECK(wcsstr(s.container, L"webm") != NULL, "WebM container");
}

static void test_no_video_and_bad_json(void)
{
    static const char *audio_only =
        "{\"streams\":[{\"codec_type\":\"audio\",\"codec_name\":\"aac\"}],"
        "\"format\":{\"format_name\":\"mp4\",\"duration\":\"10.0\"}}";
    static const char *bad = "{\"streams\":[";
    SourceInfo s;
    CHECK(parse(audio_only, &s) == PROBE_ERR_NO_VIDEO, "Audio-only file reports no video");
    CHECK(parse(bad, &s) == PROBE_ERR_BAD_JSON, "Broken JSON is rejected");
}

static void test_multi_track_probe(void)
{
    static const char *json =
        "{"
        "\"streams\":["
        "{\"index\":0,\"codec_type\":\"video\",\"codec_name\":\"mjpeg\","
        "\"width\":600,\"height\":600,\"disposition\":{\"attached_pic\":1}},"
        "{\"index\":1,\"codec_type\":\"video\",\"codec_name\":\"h264\","
        "\"width\":1920,\"height\":1080,\"pix_fmt\":\"yuv420p\","
        "\"avg_frame_rate\":\"25/1\",\"r_frame_rate\":\"25/1\","
        "\"bit_rate\":\"29877000\",\"disposition\":{\"default\":1}},"
        "{\"index\":2,\"codec_type\":\"video\",\"codec_name\":\"h264\","
        "\"width\":1280,\"height\":720,\"pix_fmt\":\"yuv420p\","
        "\"avg_frame_rate\":\"25/1\",\"r_frame_rate\":\"25/1\"},"
        "{\"index\":3,\"codec_type\":\"audio\",\"codec_name\":\"dts\","
        "\"channels\":6,\"channel_layout\":\"5.1(side)\",\"sample_rate\":\"48000\","
        "\"bit_rate\":\"3840000\",\"disposition\":{\"default\":1},"
        "\"tags\":{\"language\":\"pol\",\"title\":\"Original\"}},"
        "{\"index\":4,\"codec_type\":\"audio\",\"codec_name\":\"flac\","
        "\"channels\":2,\"channel_layout\":\"stereo\",\"sample_rate\":\"48000\","
        "\"bit_rate\":\"820000\",\"tags\":{\"language\":\"rus\"}}"
        "],"
        "\"format\":{\"format_name\":\"matroska,webm\",\"duration\":\"10512.0\","
        "\"size\":\"51027000000\"}"
        "}";
    SourceInfo s;

    CHECK(parse(json, &s) == PROBE_OK, "multi-track fixture parses");
    CHECK(s.video_track_count == 3, "all video streams are counted");
    CHECK(s.video_real_count == 2, "cover art is excluded from real video count");
    CHECK(s.video_tracks_stored == 3, "video track details are stored");
    CHECK(s.video_primary >= 0 && s.video_primary < s.video_tracks_stored,
          "primary video index is valid");
    CHECK(s.video_tracks[s.video_primary].index == 1,
          "primary video uses its absolute stream index");
    CHECK(s.video_primary == 1, "cover art is not selected as primary video");
    CHECK(s.display_width == 1920 && s.display_height == 1080,
          "primary video metadata is used");
    CHECK(s.audio_track_count == 2 && s.audio_tracks_stored == 2,
          "audio tracks are stored");
    CHECK(s.audio_tracks[0].has_index && s.audio_tracks[0].index == 3,
          "audio absolute stream index is stored");
    CHECK(s.audio_tracks[0].default_track, "default audio disposition is stored");
    CHECK(wcscmp(s.audio_tracks[0].layout, L"5.1(side)") == 0,
          "audio channel layout is stored");
    CHECK(wcscmp(s.audio_tracks[0].title, L"Original") == 0,
          "audio title is stored");
    CHECK(wcscmp(s.audio_tracks[0].language, L"pol") == 0,
          "audio language is stored");
    CHECK(s.audio_tracks[1].index == 4 && wcscmp(s.audio_tracks[1].codec, L"flac") == 0,
          "second audio track metadata is stored");
}

static void test_hw_profiles(void)
{
    HwInfo h;

    hw_info_init(&h);
    h.logical_cores = 24;
    h.speed8_median[HW_ENC_X265] = 1.10;
    CHECK(hw_choose_profile_raw(&h) == HW_PROFILE_BALANCED, "x265 1.1x -> Balanced");

    h.speed8_median[HW_ENC_X265] = 0.80;
    CHECK(hw_choose_profile_raw(&h) == HW_PROFILE_LIGHT, "x265 0.8x -> Light");

    hw_info_init(&h);
    h.logical_cores = 24;
    h.enc_ok8[HW_ENC_AMF] = 1;
    h.speed8_median[HW_ENC_AMF] = 30.0;
    h.speed8_median[HW_ENC_X265] = 1.5;
    CHECK(hw_choose_profile_raw(&h) == HW_PROFILE_THOROUGH, "fast GPU -> Thorough");

    hw_info_init(&h);
    h.logical_cores = 24;
    h.speed8_median[HW_ENC_X265] = 3.2;
    CHECK(hw_choose_profile_raw(&h) == HW_PROFILE_THOROUGH, "fast x265 -> Thorough");

    hw_info_init(&h);
    h.logical_cores = 24;
    h.enc_ok8[HW_ENC_AMF] = 1;
    h.speed8_median[HW_ENC_AMF] = 10.0;
    h.speed8_median[HW_ENC_X265] = 1.5;
    CHECK(hw_choose_profile_raw(&h) == HW_PROFILE_BALANCED, "mid GPU -> Balanced");

    /* Hysteresis: 20% margin. */
    hw_info_init(&h);
    h.logical_cores = 24;
    h.enc_ok8[HW_ENC_AMF] = 1;
    h.speed8_median[HW_ENC_AMF] = 3.2;
    h.speed8_median[HW_ENC_X265] = 1.1;
    CHECK(hw_choose_profile_raw(&h) == HW_PROFILE_BALANCED, "hysteresis raw Balanced");
    CHECK(hw_choose_profile_hysteresis(&h, HW_PROFILE_LIGHT) == HW_PROFILE_LIGHT,
          "Light keeps below 20% margin");

    h.speed8_median[HW_ENC_AMF] = 3.8;
    CHECK(hw_choose_profile_hysteresis(&h, HW_PROFILE_LIGHT) == HW_PROFILE_BALANCED,
          "Light changes above 20% margin");

    hw_info_init(&h);
    h.logical_cores = 8;
    h.enc_ok8[HW_ENC_AMF] = 1;
    h.speed8_median[HW_ENC_AMF] = 26.0;
    h.speed8_median[HW_ENC_X265] = 1.5;
    CHECK(hw_choose_profile_hysteresis(&h, HW_PROFILE_BALANCED) == HW_PROFILE_BALANCED,
          "Balanced keeps near Thorough boundary");

    h.speed8_median[HW_ENC_AMF] = 31.0;
    CHECK(hw_choose_profile_hysteresis(&h, HW_PROFILE_BALANCED) == HW_PROFILE_THOROUGH,
          "Balanced changes clearly above Thorough boundary");

    hw_info_init(&h);
    h.logical_cores = 24;
    h.enc_ok8[HW_ENC_AMF] = 1;
    h.speed8_median[HW_ENC_AMF] = 2.0;
    h.speed8_median[HW_ENC_X265] = 0.7;
    CHECK(hw_choose_profile_hysteresis(&h, HW_PROFILE_BALANCED) == HW_PROFILE_LIGHT,
          "Balanced changes clearly below Light boundary");

    hw_info_init(&h);
    h.logical_cores = 24;
    h.enc_ok8[HW_ENC_AMF] = 1;
    h.speed8_median[HW_ENC_AMF] = 2.9;
    h.speed8_median[HW_ENC_X265] = 0.9;
    CHECK(hw_choose_profile_hysteresis(&h, HW_PROFILE_BALANCED) == HW_PROFILE_BALANCED,
          "Balanced keeps near Light boundary");

    hw_info_init(&h);
    h.logical_cores = 24;
    h.enc_ok8[HW_ENC_AMF] = 1;
    h.speed8_median[HW_ENC_AMF] = 20.0;
    h.speed8_median[HW_ENC_X265] = 2.0;
    CHECK(hw_choose_profile_hysteresis(&h, HW_PROFILE_THOROUGH) == HW_PROFILE_BALANCED,
          "Thorough changes clearly below boundary");

    h.speed8_median[HW_ENC_AMF] = 24.0;
    CHECK(hw_choose_profile_hysteresis(&h, HW_PROFILE_THOROUGH) == HW_PROFILE_THOROUGH,
          "Thorough keeps near boundary");

    CHECK(wcscmp(hw_profile_name(HW_PROFILE_BALANCED), L"BALANCED") == 0, "profile name");
    CHECK(wcscmp(hw_encoder_name(HW_ENC_AMF), L"AMD AMF") == 0, "encoder name");
    CHECK(hw_fallback_encoder(HW_ENC_NVENC) == HW_ENC_X265, "NVENC failure falls back to x265");
    CHECK(hw_fallback_encoder(HW_ENC_QSV) == HW_ENC_X265, "QSV failure falls back to x265");
    CHECK(hw_fallback_encoder(HW_ENC_AMF) == HW_ENC_X265, "AMF failure falls back to x265");
    CHECK(hw_fallback_encoder(HW_ENC_X265) == -1, "x265 failure has no fallback");
}

static void test_estimates(void)
{
    SourceInfo src;
    HwInfo hw;
    int levels[4], count, clips;
    double clip_sec;
    wchar_t args[1024];
    wchar_t cmd[4096];
    int fell_back = 0;

    memset(&src, 0, sizeof(src));
    src.duration = 30.0;
    src.bit_depth = 8;
    src.audio_track_count = 1;
    src.audio_bitrate = 128000;

    hw_info_init(&hw);
    hw.profile = HW_PROFILE_BALANCED;
    hw.recommended = HW_ENC_AMF;
    hw.best8 = HW_ENC_AMF;
    hw.enc_ok8[HW_ENC_AMF] = 1;
    hw.speed8_median[HW_ENC_AMF] = 10.0;

    count = estimate_plan(&src, HW_PROFILE_BALANCED, levels, &count, &clips, &clip_sec);
    CHECK(count == 4 && clips == 3, "Balanced plan is 4 levels x 3 clips");
    CHECK(clip_sec > 3.9 && clip_sec < 4.1, "Balanced clip is 4 s");

    count = estimate_plan(&src, HW_PROFILE_LIGHT, levels, &count, &clips, &clip_sec);
    CHECK(count == 3 && clips == 2, "Light plan is 3 levels x 2 clips");
    CHECK(clip_sec > 2.9 && clip_sec < 3.1, "Light clip is 3 s");

    count = estimate_plan(&src, HW_PROFILE_THOROUGH, levels, &count, &clips, &clip_sec);
    CHECK(count == 4 && clips == 4, "Thorough plan is 4 levels x 4 clips");

    src.duration = 3600.0;
    count = estimate_plan(&src, HW_PROFILE_BALANCED, levels, &count, &clips, &clip_sec);
    CHECK(clips == 4 && clip_sec > 4.9 && clip_sec < 5.1,
          "long films use 4 x 5 s samples");
    src.duration = 7200.0;
    count = estimate_plan(&src, HW_PROFILE_BALANCED, levels, &count, &clips, &clip_sec);
    CHECK(clips == 4 && clip_sec > 5.9 && clip_sec < 6.1,
          "very long films use 4 x 6 s samples");
    src.duration = 30.0;

    CHECK(estimate_choose_encoder(&src, &hw) == HW_ENC_AMF, "estimate chooses AMF");
    src.bit_depth = 10;
    hw.best10 = HW_ENC_QSV;
    hw.enc_ok10[HW_ENC_QSV] = 1;
    CHECK(estimate_choose_encoder(&src, &hw) == HW_ENC_QSV,
          "10-bit SDR chooses a verified 10-bit encoder");
    hw.best10 = -1;
    hw.enc_ok10[HW_ENC_QSV] = 0;
    CHECK(estimate_choose_encoder(&src, &hw) == HW_ENC_X265,
          "10-bit SDR falls back to CPU when only 8-bit GPUs passed");
    src.bit_depth = 8;
    hw.best10 = -1;
    CHECK(estimate_next_encoder(HW_ENC_NVENC, 1, &fell_back) == HW_ENC_X265,
          "estimate falls back from NVENC");
    CHECK(fell_back == 1, "estimate records fallback");
    fell_back = 0;
    CHECK(estimate_next_encoder(HW_ENC_X265, 1, &fell_back) == -1,
          "estimate has no fallback from x265");

    CHECK(presets_video_args(&src, HW_ENC_AMF, QUALITY_BALANCED, args, 1024) == 1,
          "AMF args build");
    CHECK(wcsstr(args, L"hevc_amf") != NULL, "AMF args use hevc_amf");
    CHECK(wcsstr(args, L"-qp_i 30") != NULL, "AMF Balanced uses QP 30");
    src.bit_depth = 10;
    CHECK(presets_video_args(&src, HW_ENC_AMF, QUALITY_BALANCED, args, 1024) == 1,
          "AMF 10-bit args build");
    CHECK(wcsstr(args, L"p010le") != NULL, "AMF 10-bit uses p010le");
    src.is_hdr = 1;
    src.bit_depth = 10;
    wcscpy(src.color_primaries, L"bt2020");
    wcscpy(src.color_transfer, L"smpte2084");
    wcscpy(src.color_space, L"bt2020nc");
    wcscpy(src.color_range, L"tv");
    CHECK(presets_video_args(&src, HW_ENC_X265, QUALITY_BALANCED, args, 1024) == 1,
          "x265 HDR args build");
    CHECK(wcsstr(args, L"hdr10=1") != NULL, "x265 HDR adds hdr10");

    src.duration = 30.0;
    wcscpy(src.path, L"tests\\media\\h264_1080p_20s.mp4");
    CHECK(presets_build_sample_cmd(&src, HW_ENC_AMF, QUALITY_BALANCED, 3.0, 4.0,
                                   L"tests\\build\\sample.mkv", cmd, 4096) == 1,
          "sample command builds");
    CHECK(wcsstr(cmd, L"-ss 3.000") != NULL, "sample command seeks");
}

static void test_encode_options(void)
{
    SourceInfo src;
    EncodeOptions opt;
    wchar_t args[1024];
    wchar_t cmd[16384];
    wchar_t out[UTIL_PATH_MAX];
    EncodeError err = ENCODE_ERR_NONE;
    HwInfo hw;

    memset(&src, 0, sizeof(src));
    src.duration = 60.0;
    src.coded_width = 1920;
    src.coded_height = 1080;
    src.bit_depth = 8;
    src.audio_track_count = 3;
    src.audio_tracks_stored = 3;
    wcscpy(src.audio_tracks[0].codec, L"opus");
    src.audio_tracks[0].channels = 2;
    src.audio_tracks[0].bitrate = 48000;
    wcscpy(src.audio_tracks[1].codec, L"aac");
    src.audio_tracks[1].channels = 6;
    src.audio_tracks[1].bitrate = 128000;
    wcscpy(src.audio_tracks[2].codec, L"mp3");
    src.audio_tracks[2].channels = 2;
    src.audio_tracks[2].bitrate = 24000;
    src.subtitle_track_count = 2;
    src.subtitle_tracks_stored = 2;
    wcscpy(src.subtitle_tracks[0].codec, L"subrip");
    wcscpy(src.subtitle_tracks[1].codec, L"hdmv_pgs_subtitle");
    wcscpy(src.path, L"C:\\videos\\clip.mp4");
    wcscpy(src.file_name, L"clip.mp4");

    CHECK(encode_audio_available(ENC_CONT_MKV, ENC_ACODEC_OPUS), "MKV allows Opus");
    CHECK(encode_audio_available(ENC_CONT_MKV, ENC_ACODEC_FLAC), "MKV allows FLAC");
    CHECK(encode_audio_available(ENC_CONT_MKV, ENC_ACODEC_VORBIS), "MKV allows Vorbis");
    CHECK(encode_audio_available(ENC_CONT_MP4, ENC_ACODEC_AAC), "MP4 allows AAC");
    CHECK(!encode_audio_available(ENC_CONT_MP4, ENC_ACODEC_OPUS), "MP4 hides Opus");
    CHECK(!encode_audio_available(ENC_CONT_MP4, ENC_ACODEC_FLAC), "MP4 hides FLAC");
    CHECK(encode_audio_copy_available(&src, ENC_CONT_MKV), "MKV can copy any audio");
    CHECK(!encode_audio_copy_available(&src, ENC_CONT_MP4), "MP4 rejects copy of Opus source");
    {
        SourceInfo aac_src = src;
        aac_src.audio_track_count = 1;
        aac_src.audio_tracks_stored = 1;
        wcscpy(aac_src.audio_tracks[0].codec, L"aac");
        CHECK(encode_audio_copy_available(&aac_src, ENC_CONT_MP4),
              "MP4 allows copy of AAC source");
    }
    CHECK(encode_default_audio(ENC_CONT_MKV) == ENC_ACODEC_OPUS, "MKV defaults to Opus");
    CHECK(encode_default_audio(ENC_CONT_MP4) == ENC_ACODEC_AAC, "MP4 defaults to AAC");

    encode_options_defaults(&opt, &src);
    CHECK(opt.container == ENC_CONT_MKV, "default container is MKV");
    CHECK(opt.video_codec == ENC_VCODEC_HEVC, "default video is H.265");
    CHECK(opt.audio_codec == ENC_ACODEC_OPUS, "default audio is Opus");
    CHECK(opt.bitrate == ENC_ABITRATE_MUSIC, "default bitrate is Music");
    CHECK(!encode_audio_has_bitrate(ENC_ACODEC_COPY), "copy has no bitrate");
    CHECK(!encode_audio_has_bitrate(ENC_ACODEC_FLAC), "FLAC has no bitrate");
    CHECK(encode_audio_min_kbps(ENC_ACODEC_AC3) == 64, "AC-3 minimum is 64k");
    CHECK(encode_audio_min_kbps(ENC_ACODEC_VORBIS) == 32, "Vorbis minimum is 32k");
    CHECK(encode_audio_force_stereo(ENC_ACODEC_MP3), "MP3 forces stereo");
    opt.audio_codec = ENC_ACODEC_AC3;
    opt.bitrate = ENC_ABITRATE_VOICE;
    encode_fix_bitrate(&opt);
    CHECK(opt.bitrate == ENC_ABITRATE_HIGH, "AC-3 clamps Voice to High");
    opt.bitrate = ENC_ABITRATE_CUSTOM;
    opt.custom_kbps = 48;
    encode_fix_bitrate(&opt);
    CHECK(opt.bitrate == ENC_ABITRATE_HIGH, "AC-3 clamps low custom to High");
    opt.bitrate = ENC_ABITRATE_CUSTOM;
    opt.custom_kbps = 96;
    encode_fix_bitrate(&opt);
    CHECK(opt.bitrate == ENC_ABITRATE_CUSTOM, "AC-3 keeps valid custom bitrate");
    CHECK(encode_subtitle_is_text(L"subrip"), "SRT is text");
    CHECK(encode_subtitle_is_text(L"mov_text"), "mov_text is text");
    CHECK(!encode_subtitle_is_text(L"hdmv_pgs_subtitle"), "PGS is bitmap");
    CHECK(encode_text_subtitle_count(&src) == 1, "one text subtitle");
    CHECK(encode_bitmap_subtitle_count(&src) == 1, "one bitmap subtitle");

    opt.audio_codec = ENC_ACODEC_OPUS;
    opt.bitrate = ENC_ABITRATE_MUSIC;
    CHECK(encode_bitrate_kbps(&opt) == 36, "Music is 36k");
    CHECK(!encode_audio_track_copies(&src, &opt, 0), "48k Opus source is re-encoded at 36k target");
    CHECK(!encode_audio_track_copies(&src, &opt, 1), "128k AAC source is re-encoded");
    CHECK(encode_audio_track_copies(&src, &opt, 2) == 0,
          "24k MP3 source is re-encoded when Opus was requested");
    wcscpy(src.audio_tracks[2].codec, L"opus");
    CHECK(encode_audio_track_copies(&src, &opt, 2),
          "same-codec smaller track is copied");
    wcscpy(src.audio_tracks[2].codec, L"mp3");
    CHECK(encode_has_multichannel(&src), "source has multichannel audio");
    CHECK(encode_audio_downmix_track(&src, &opt, 1), "5.1 track is downmixed");
    CHECK(encode_any_downmix(&src, &opt), "downmix is proposed for an encoded 5.1 track");
    src.audio_tracks[1].bitrate = 32000;
    CHECK(!encode_audio_track_copies(&src, &opt, 1),
          "5.1 track is re-encoded when a downmix was requested");
    CHECK(encode_any_downmix(&src, &opt),
          "requested downmix still applies to the 5.1 track");
    src.audio_tracks[1].bitrate = 128000;
    opt.downmix_stereo = 0;
    CHECK(!encode_audio_downmix_track(&src, &opt, 1), "downmix can be disabled");
    opt.audio_codec = ENC_ACODEC_MP3;
    CHECK(encode_audio_downmix_track(&src, &opt, 1), "MP3 always downmixes");

    CHECK(encode_audio_total_bitrate(&src) == 200000, "audio bitrates are summed");

    wcscpy(src.audio_tracks[1].layout, L"5.1(side)");
    src.audio_tracks[1].has_index = 1;
    src.audio_tracks[1].index = 7;
    opt.container = ENC_CONT_MKV;
    opt.video_codec = ENC_VCODEC_HEVC;
    opt.audio_codec = ENC_ACODEC_OPUS;
    opt.bitrate = ENC_ABITRATE_HIGH;
    opt.custom_kbps = ENC_BITRATE_HIGH_KBPS;
    opt.downmix_stereo = 0;
    opt.audio_mask = 1u << 1;
    CHECK(encode_selected_audio_count(&src, &opt) == 1, "one track is selected");
    CHECK(!encode_audio_selected(&src, &opt, 0) &&
          encode_audio_selected(&src, &opt, 1), "selection skips other tracks");
    err = ENCODE_ERR_NONE;
    CHECK(encode_build_command(&src, HW_ENC_AMF, QUALITY_BALANCED, &opt,
                               L"C:\\videos\\Compressed\\clip (compressed).mkv",
                               cmd, 16384, &err) == 1, "selected-track command builds");
    CHECK(wcsstr(cmd, L"-map 0:7") != NULL, "selected track maps by absolute index");
    CHECK(wcsstr(cmd, L"-map 0:a:0") == NULL, "unselected tracks are not mapped");
    CHECK(wcsstr(cmd, L"-c:a:0 libopus") != NULL, "selected track becomes output audio 0");
    CHECK(wcsstr(cmd, L"-af:a:0 \"aformat=channel_layouts=5.1\"") != NULL,
          "5.1(side) is remapped to an Opus-safe layout");
    CHECK(wcsstr(cmd, L"-ar:a:0 48000") != NULL, "Opus output is forced to 48 kHz");
    CHECK(wcsstr(cmd, L"-ac:a:0 2") == NULL, "no downmix is forced when it is disabled");

    opt.downmix_stereo = 1;
    CHECK(encode_build_command(&src, HW_ENC_AMF, QUALITY_BALANCED, &opt,
                               L"C:\\videos\\Compressed\\clip (compressed).mkv",
                               cmd, 16384, &err) == 1, "downmixed command builds");
    CHECK(wcsstr(cmd, L"-af:a:0 \"aformat=channel_layouts=stereo\"") != NULL,
          "unsupported layout is explicitly remapped when downmixing");
    CHECK(encode_estimated_audio_bitrate(&src, &opt) == 64000,
          "downmixed estimate uses the stereo target");
    opt.downmix_stereo = 0;
    CHECK(encode_estimated_audio_bitrate(&src, &opt) == 192000,
          "surround estimate scales the target for six channels");

    src.video_tracks_stored = 2;
    src.video_primary = 1;
    src.video_tracks[1].has_index = 1;
    src.video_tracks[1].index = 4;
    src.video_tracks[1].width = 1280;
    src.video_tracks[1].height = 720;
    src.video_tracks[1].display_width = 1280;
    src.video_tracks[1].display_height = 720;
    opt.video_track = 1;
    CHECK(encode_selected_video_track(&src, &opt) == 1, "secondary video track is selected");
    CHECK(encode_build_command(&src, HW_ENC_AMF, QUALITY_BALANCED, &opt,
                               L"C:\\videos\\Compressed\\clip (compressed).mkv",
                               cmd, 16384, &err) == 1, "secondary-video command builds");
    CHECK(wcsstr(cmd, L"-map 0:4") != NULL, "secondary video maps by absolute index");
    src.video_tracks_stored = 0;
    src.video_primary = -1;
    src.audio_tracks[1].has_index = 0;
    src.audio_tracks[1].index = 0;
    src.audio_tracks[1].layout[0] = L'\0';
    opt.audio_mask = 0;
    opt.video_track = 0;

    CHECK(encode_preview_output_path(&src, ENC_CONT_MKV, out, UTIL_PATH_MAX) == 1,
          "preview output path builds");
    CHECK(wcsstr(out, L"Compressed") != NULL, "preview path uses Compressed folder");
    CHECK(wcsstr(out, L"clip (compressed).mkv") != NULL, "preview path keeps base name");

    hw_info_init(&hw);
    hw.profile = HW_PROFILE_BALANCED;
    hw.recommended = HW_ENC_AMF;
    hw.best8 = HW_ENC_AMF;
    hw.enc_ok8[HW_ENC_AMF] = 1;
    hw.speed8_median[HW_ENC_AMF] = 10.0;

    opt.container = ENC_CONT_MKV;
    opt.video_codec = ENC_VCODEC_HEVC;
    opt.audio_codec = ENC_ACODEC_OPUS;
    opt.bitrate = ENC_ABITRATE_MUSIC;
    opt.downmix_stereo = 1;
    wcscpy(src.audio_tracks[2].codec, L"opus");
    CHECK(encode_build_command(&src, HW_ENC_AMF, QUALITY_BALANCED, &opt,
                               L"C:\\videos\\Compressed\\clip (compressed).mkv",
                               cmd, 16384, &err) == 1, "MKV command builds");
    CHECK(wcsstr(cmd, L"-map 0:v:0") != NULL, "command maps video explicitly");
    CHECK(wcsstr(cmd, L"-map 0:a:0") != NULL, "command maps first audio track");
    CHECK(wcsstr(cmd, L"-map 0:a:1") != NULL, "command maps second audio track");
    CHECK(wcsstr(cmd, L"-map 0:a:2") != NULL, "command maps third audio track");
    CHECK(wcsstr(cmd, L"-map 0:a?") == NULL, "command maps audio individually");
    CHECK(wcsstr(cmd, L"-map 0:s:0") != NULL, "command maps first subtitle");
    CHECK(wcsstr(cmd, L"-map 0:s:1") != NULL, "command maps second subtitle");
    CHECK(wcsstr(cmd, L"-map 0:s?") == NULL, "command maps subtitles individually");
    CHECK(wcsstr(cmd, L"-c:s copy") != NULL, "MKV copies subtitles");
    wcscpy(src.subtitle_tracks[0].codec, L"mov_text");
    CHECK(encode_build_command(&src, HW_ENC_AMF, QUALITY_BALANCED, &opt,
                               L"C:\\videos\\Compressed\\clip (compressed).mkv",
                               cmd, 16384, &err) == 1, "mov_text command builds");
    CHECK(wcsstr(cmd, L"-c:s:0 srt") != NULL,
          "mov_text is converted to SRT for MKV");
    wcscpy(src.subtitle_tracks[0].codec, L"subrip");
    CHECK(wcsstr(cmd, L"-c:a libopus") != NULL, "Opus encoder is used");
    CHECK(wcsstr(cmd, L"-c:a:2 copy") != NULL, "small audio track overrides to copy");
    CHECK(wcsstr(cmd, L"-ac:a:1 2") != NULL, "5.1 track receives stereo downmix");
    CHECK(wcsstr(cmd, L"-f matroska") != NULL, "MKV muxer is used");
    CHECK(wcsstr(cmd, L"-progress pipe:1") != NULL, "command uses progress pipe");

    opt.subtitle_mask = 1u; /* keep only the text subtitle */
    opt.subtitles_none = 0;
    CHECK(encode_selected_subtitle_count(&src, &opt, ENC_CONT_MKV) == 1,
          "one subtitle is selected");
    CHECK(encode_build_command(&src, HW_ENC_AMF, QUALITY_BALANCED, &opt,
                               L"C:\\videos\\Compressed\\clip (compressed).mkv",
                               cmd, 16384, &err) == 1, "subtitle subset command builds");
    CHECK(wcsstr(cmd, L"-map 0:s:0") != NULL, "selected subtitle is mapped");
    CHECK(wcsstr(cmd, L"-map 0:s:1") == NULL, "unselected subtitle is dropped");
    opt.subtitles_none = 1;
    CHECK(encode_build_command(&src, HW_ENC_AMF, QUALITY_BALANCED, &opt,
                               L"C:\\videos\\Compressed\\clip (compressed).mkv",
                               cmd, 16384, &err) == 1, "no-subtitle command builds");
    CHECK(wcsstr(cmd, L"-map 0:s") == NULL, "drop-all removes subtitle maps");
    opt.subtitles_none = 0;
    opt.subtitle_mask = 0;

    opt.audio_none = 1;
    CHECK(encode_selected_audio_count(&src, &opt) == 0, "all audio can be dropped");
    CHECK(encode_build_command(&src, HW_ENC_AMF, QUALITY_BALANCED, &opt,
                               L"C:\\videos\\Compressed\\clip (compressed).mkv",
                               cmd, 16384, &err) == 1, "no-audio command builds");
    CHECK(wcsstr(cmd, L"-an") != NULL, "no-audio command uses -an");
    CHECK(wcsstr(cmd, L"-map 0:a") == NULL, "no-audio command has no audio maps");
    opt.audio_none = 0;

    CHECK(presets_quality_value(HW_ENC_X265, QUALITY_HIGH, PRESETS_VIDEO_HEVC) == 21,
          "x265 HIGH quality is CRF 21");
    CHECK(presets_quality_value(HW_ENC_QSV, QUALITY_HIGH, PRESETS_VIDEO_HEVC) == 25,
          "QSV HIGH quality is 25");
    opt.encoder_override = HW_ENC_X265;
    opt.crf_override = 19;
    CHECK(encode_build_command(&src, HW_ENC_AMF, QUALITY_BALANCED, &opt,
                               L"C:\\videos\\Compressed\\clip (compressed).mkv",
                               cmd, 16384, &err) == 1, "custom CRF command builds");
    CHECK(wcsstr(cmd, L"libx265") != NULL && wcsstr(cmd, L"-crf 19") != NULL,
          "custom encoder/CRF override is used");
    opt.encoder_override = -1;
    opt.crf_override = 0;

    opt.container = ENC_CONT_MP4;
    opt.audio_codec = ENC_ACODEC_AAC;
    opt.bitrate = ENC_ABITRATE_HIGH;
    CHECK(encode_build_command(&src, HW_ENC_AMF, QUALITY_BALANCED, &opt,
                               L"C:\\videos\\Compressed\\clip (compressed).mp4",
                               cmd, 16384, &err) == 1, "MP4 command builds");
    CHECK(wcsstr(cmd, L"-map 0:s:0") != NULL, "MP4 maps the text subtitle only");
    CHECK(wcsstr(cmd, L"-c:s mov_text") != NULL, "MP4 converts text subtitles");
    CHECK(wcsstr(cmd, L"hdmv_pgs") == NULL, "MP4 command never maps PGS by codec");
    CHECK(wcsstr(cmd, L"-tag:v hvc1") != NULL, "MP4 H.265 uses hvc1 tag");
    CHECK(wcsstr(cmd, L"-f mp4") != NULL, "MP4 muxer is used");

    opt.audio_codec = ENC_ACODEC_OPUS;
    err = ENCODE_ERR_NONE;
    CHECK(encode_build_command(&src, HW_ENC_AMF, QUALITY_BALANCED, &opt,
                               L"C:\\videos\\Compressed\\clip.mp4",
                               cmd, 16384, &err) == 0, "MP4 rejects Opus");
    CHECK(err == ENCODE_ERR_AUDIO, "MP4 rejection reports audio error");

    opt.audio_codec = ENC_ACODEC_COPY;
    err = ENCODE_ERR_NONE;
    CHECK(encode_build_command(&src, HW_ENC_AMF, QUALITY_BALANCED, &opt,
                               L"C:\\videos\\Compressed\\clip.mp4",
                               cmd, 16384, &err) == 0, "MP4 rejects copy of Opus source");
    CHECK(err == ENCODE_ERR_AUDIO, "MP4 copy rejection reports audio error");

    src.is_hdr = 1;
    src.bit_depth = 10;
    wcscpy(src.color_primaries, L"bt2020");
    wcscpy(src.color_transfer, L"smpte2084");
    wcscpy(src.color_space, L"bt2020nc");
    opt.container = ENC_CONT_MP4;
    opt.video_codec = ENC_VCODEC_H264;
    opt.audio_codec = ENC_ACODEC_AAC;
    opt.bitrate = ENC_ABITRATE_HIGH;
    CHECK(encode_build_command(&src, HW_ENC_AMF, QUALITY_BALANCED, &opt,
                               L"C:\\videos\\Compressed\\clip.mp4",
                               cmd, 16384, &err) == 1, "HDR H.264 command builds");
    CHECK(wcsstr(cmd, L"-c:v libx264") != NULL, "H.264 uses x264");
    CHECK(wcsstr(cmd, L"zscale") != NULL, "HDR H.264 tone-maps");
    CHECK(wcsstr(cmd, L"-color_primaries bt709") != NULL, "HDR H.264 tags BT.709");
    CHECK(presets_video_args_ex(&src, HW_ENC_AMF, QUALITY_BALANCED,
                                PRESETS_VIDEO_H264, args, 1024) == 1,
          "H.264 preset args build");
    CHECK(wcsstr(args, L"libx264") != NULL, "preset uses libx264");
    CHECK(wcsstr(args, L"crf 24") != NULL, "Balanced H.264 uses CRF 24");
}

static void test_strings_and_utils(void)
{
    wchar_t buf[128];
    wchar_t dir[128];
    int i;

    for (i = 0; i < STR_COUNT; i++) {
        const wchar_t *s = text((StringId)i);
        if (!s || !*s) {
            printf("FAIL: empty string table entry %d\n", i);
            g_fail++;
        } else {
            g_pass++;
        }
    }

    CHECK(wcscmp(text(STR_APP_NAME), L"FF COMPRESSOR") == 0, "app name string");

    util_format_bytes(1500000, buf, 128);
    CHECK(wcsstr(buf, L"MB") != NULL, "format MB");
    util_format_duration(3661.0, buf, 128);
    CHECK(wcscmp(buf, L"1:01:01") == 0, "format duration hours");
    util_format_duration(61.0, buf, 128);
    CHECK(wcscmp(buf, L"1:01") == 0, "format duration minutes");
    util_format_bitrate(8500000, buf, 128);
    CHECK(wcsstr(buf, L"Mbps") != NULL, "format Mbps");
    util_format_fps(29.97, buf, 128);
    CHECK(wcsstr(buf, L"fps") != NULL, "format fps");

    CHECK(wcscmp(util_basename(L"C:\\videos\\clip.mp4"), L"clip.mp4") == 0, "basename");
    util_dirname(L"C:\\videos\\clip.mp4", dir, 128);
    CHECK(wcscmp(dir, L"C:\\videos") == 0, "dirname");
    util_join_path(buf, 128, L"C:\\videos", L"clip.mp4");
    CHECK(wcscmp(buf, L"C:\\videos\\clip.mp4") == 0, "join path");
    CHECK(util_quote_arg(buf, 128, L"C:\\videos\\clip.mp4") == 1, "quote path");
    CHECK(wcscmp(buf, L"\"C:\\videos\\clip.mp4\"") == 0, "quoted path value");
    CHECK(util_quote_arg(buf, 128, L"bad\"path") == 0, "quote rejects embedded quote");

    wcscpy(buf, L"  hello  ");
    util_trim(buf);
    CHECK(wcscmp(buf, L"hello") == 0, "trim");
}

int main(void)
{
    printf("==========================================\n");
    printf("RUNNING FF-COMPRESSOR CORE TESTS\n");
    printf("==========================================\n");

    test_h264_sdr();
    test_hevc_hdr_rotated();
    test_vfr_interlaced_anamorphic();
    test_dolby_vision();
    test_wide_gamut_sdr();
    test_legacy_rotation_tag();
    test_opus_webm();
    test_no_video_and_bad_json();
    test_multi_track_probe();
    test_hw_profiles();
    test_estimates();
    test_encode_options();
    test_strings_and_utils();

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    if (g_fail == 0) {
        printf("ALL CORE TESTS PASSED\n");
        return 0;
    }
    return 1;
}
