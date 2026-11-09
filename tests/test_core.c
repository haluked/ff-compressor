/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "probe.h"
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
    test_strings_and_utils();

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    if (g_fail == 0) {
        printf("ALL CORE TESTS PASSED\n");
        return 0;
    }
    return 1;
}
