#include "project_validation.h"
#include "project_format.h"
#include <SDL3/SDL.h>
#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>

#define PROJECT_VALIDATION_MAX_SAMPLES 128
#define PROJECT_VALIDATION_ID_MAX 128
#define VAPORPLANE_PROJECT_PREVIEW_FILENAME "preview.wav"

typedef struct {
    char sample_id[PROJECT_VALIDATION_ID_MAX];
    char path[CLIP_MAX_PATH];
} ValidationSampleRef;

static void path_join_local(char *out, size_t out_size, const char *base, const char *leaf) {
    if (!out || out_size == 0) return;
    if (!base || !base[0]) {
        SDL_strlcpy(out, leaf ? leaf : "", out_size);
        return;
    }
    size_t len = SDL_strlen(base);
    const char *separator = (len > 0 && (base[len - 1] == '/' || base[len - 1] == '\\')) ? "" : "/";
    SDL_snprintf(out, out_size, "%s%s%s", base, separator, leaf ? leaf : "");
}

static bool path_is_directory_local(const char *path) {
    SDL_PathInfo info;
    return path && path[0] && SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_DIRECTORY;
}

static bool path_exists_local(const char *path) {
    SDL_PathInfo info;
    return path && path[0] && SDL_GetPathInfo(path, &info);
}

static bool has_suffix_ci(const char *text, const char *suffix) {
    if (!text || !suffix) return false;
    size_t text_len = SDL_strlen(text);
    size_t suffix_len = SDL_strlen(suffix);
    return text_len >= suffix_len &&
           SDL_strcasecmp(text + text_len - suffix_len, suffix) == 0;
}

static const char *path_basename_local(const char *path) {
    if (!path || !path[0]) return "";
    const char *base = path;
    for (const char *p = path; *p; ++p) {
        if (*p == '/' || *p == '\\') base = p + 1;
    }
    return base;
}

static void bundle_name_from_path(const char *path, char *out, size_t out_size) {
    if (!out || out_size == 0) return;
    SDL_strlcpy(out, path_basename_local(path), out_size);
    size_t len = SDL_strlen(out);
    size_t suffix_len = SDL_strlen(VAPORPLANE_PROJECT_BUNDLE_SUFFIX);
    if (len > suffix_len &&
        SDL_strcasecmp(out + len - suffix_len, VAPORPLANE_PROJECT_BUNDLE_SUFFIX) == 0) {
        out[len - suffix_len] = '\0';
    }
    if (!out[0]) SDL_strlcpy(out, "vaporplane_project", out_size);
}

static void result_init(ProjectValidationResult *out, const char *bundle_path) {
    SDL_memset(out, 0, sizeof(*out));
    out->status = PROJECT_VALIDATION_UNKNOWN;
    SDL_strlcpy(out->reason, "unknown", sizeof(out->reason));
    if (bundle_path) SDL_strlcpy(out->bundle_path, bundle_path, sizeof(out->bundle_path));
    bundle_name_from_path(bundle_path, out->project_name, sizeof(out->project_name));
}

static void result_set(ProjectValidationResult *out,
                       ProjectValidationStatus status,
                       const char *reason,
                       const char *detail) {
    if (!out) return;
    out->status = status;
    SDL_strlcpy(out->reason, reason ? reason : "", sizeof(out->reason));
    SDL_strlcpy(out->detail, detail ? detail : out->reason, sizeof(out->detail));
}

static void result_warn(ProjectValidationResult *out, const char *reason, const char *detail) {
    if (!out || out->status == PROJECT_VALIDATION_INVALID) return;
    if (out->status != PROJECT_VALIDATION_WARNING) {
        result_set(out, PROJECT_VALIDATION_WARNING, reason, detail);
    }
}

const char *project_validation_status_label(ProjectValidationStatus status) {
    switch (status) {
        case PROJECT_VALIDATION_VALID: return "VALID";
        case PROJECT_VALIDATION_WARNING: return "WARNING";
        case PROJECT_VALIDATION_INVALID: return "INVALID";
        case PROJECT_VALIDATION_UNKNOWN:
        default: return "UNKNOWN";
    }
}

bool project_validation_is_safe_relative_path(const char *path) {
    if (!path || !path[0]) return false;
    if (path[0] == '/' || path[0] == '\\') return false;
    for (const char *p = path; *p && *p != '/' && *p != '\\'; ++p) {
        if (*p == ':') return false;
    }

    const char *segment = path;
    while (*segment) {
        const char *end = segment;
        while (*end && *end != '/' && *end != '\\') ++end;
        size_t len = (size_t)(end - segment);
        if (len == 0) return false;
        if (len == 1 && segment[0] == '.') return false;
        if (len == 2 && segment[0] == '.' && segment[1] == '.') return false;
        segment = *end ? end + 1 : end;
    }
    return true;
}

static bool path_starts_with_samples_dir(const char *path) {
    size_t len = SDL_strlen(VAPORPLANE_PROJECT_SAMPLES_DIRNAME);
    return path &&
           SDL_strncmp(path, VAPORPLANE_PROJECT_SAMPLES_DIRNAME, len) == 0 &&
           (path[len] == '/' || path[len] == '\\') &&
           path[len + 1] != '\0';
}

static const char *range_strstr_local(const char *start, const char *end, const char *needle) {
    if (!start || !end || !needle || start > end) return NULL;
    size_t needle_len = SDL_strlen(needle);
    if (needle_len == 0) return start;
    for (const char *p = start; p + needle_len <= end; ++p) {
        if (SDL_memcmp(p, needle, needle_len) == 0) return p;
    }
    return NULL;
}

static const char *json_key_value(const char *start, const char *end, const char *key) {
    char needle[128];
    SDL_snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *p = range_strstr_local(start, end, needle);
    if (!p) return NULL;
    p += SDL_strlen(needle);
    while (p < end && isspace((unsigned char)*p)) ++p;
    if (p >= end || *p != ':') return NULL;
    ++p;
    while (p < end && isspace((unsigned char)*p)) ++p;
    return p < end ? p : NULL;
}

static bool json_get_int_range(const char *start, const char *end, const char *key, int *out) {
    const char *p = json_key_value(start, end, key);
    if (!p) return false;
    char *parse_end = NULL;
    long value = strtol(p, &parse_end, 10);
    if (parse_end == p || parse_end > end) return false;
    if (out) *out = (int)value;
    return true;
}

static bool json_get_string_range(const char *start, const char *end, const char *key, char *out, size_t out_size) {
    const char *p = json_key_value(start, end, key);
    if (!p || p >= end || *p != '"' || !out || out_size == 0) return false;
    ++p;
    size_t write = 0;
    while (p < end && *p != '"') {
        char c = *p++;
        if (c == '\\' && p < end) {
            char esc = *p++;
            switch (esc) {
                case '"': c = '"'; break;
                case '\\': c = '\\'; break;
                case '/': c = '/'; break;
                case 'b': c = '\b'; break;
                case 'f': c = '\f'; break;
                case 'n': c = '\n'; break;
                case 'r': c = '\r'; break;
                case 't': c = '\t'; break;
                default: c = esc; break;
            }
        }
        if (write + 1 < out_size) out[write++] = c;
    }
    if (p >= end || *p != '"') return false;
    out[write] = '\0';
    return true;
}

static bool json_find_compound_range(const char *start,
                                     const char *end,
                                     const char *key,
                                     char open,
                                     char close,
                                     const char **out_start,
                                     const char **out_end) {
    const char *p = json_key_value(start, end, key);
    if (!p || p >= end || *p != open) return false;
    const char *compound_start = p;
    int depth = 0;
    bool in_string = false;
    bool escaped = false;
    for (; p < end; ++p) {
        char c = *p;
        if (in_string) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') in_string = false;
            continue;
        }
        if (c == '"') in_string = true;
        else if (c == open) depth++;
        else if (c == close) {
            depth--;
            if (depth == 0) {
                if (out_start) *out_start = compound_start;
                if (out_end) *out_end = p + 1;
                return true;
            }
        }
    }
    return false;
}

static bool json_find_array_range(const char *start, const char *end, const char *key, const char **out_start, const char **out_end) {
    return json_find_compound_range(start, end, key, '[', ']', out_start, out_end);
}

static bool json_next_object(const char **cursor, const char *end, const char **out_start, const char **out_end) {
    const char *p = *cursor;
    while (p < end && *p != '{') ++p;
    if (p >= end) {
        *cursor = end;
        return false;
    }
    const char *object_start = p;
    int depth = 0;
    bool in_string = false;
    bool escaped = false;
    for (; p < end; ++p) {
        char c = *p;
        if (in_string) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') in_string = false;
            continue;
        }
        if (c == '"') in_string = true;
        else if (c == '{') depth++;
        else if (c == '}') {
            depth--;
            if (depth == 0) {
                *out_start = object_start;
                *out_end = p + 1;
                *cursor = p + 1;
                return true;
            }
        }
    }
    *cursor = end;
    return false;
}

static bool load_text_file_local(const char *path, char **out_text, size_t *out_size) {
    size_t size = 0;
    void *bytes = SDL_LoadFile(path, &size);
    if (!bytes) return false;
    char *text = (char *)SDL_malloc(size + 1);
    if (!text) {
        SDL_free(bytes);
        return false;
    }
    SDL_memcpy(text, bytes, size);
    text[size] = '\0';
    SDL_free(bytes);
    if (out_text) *out_text = text;
    else SDL_free(text);
    if (out_size) *out_size = size;
    return true;
}

static bool read_be16_mem(const Uint8 *data, size_t size, size_t *pos, Uint16 *out) {
    if (*pos + 2 > size) return false;
    *out = (Uint16)(((Uint16)data[*pos] << 8) | data[*pos + 1]);
    *pos += 2;
    return true;
}

static bool read_be32_mem(const Uint8 *data, size_t size, size_t *pos, Uint32 *out) {
    if (*pos + 4 > size) return false;
    *out = ((Uint32)data[*pos] << 24) | ((Uint32)data[*pos + 1] << 16) |
           ((Uint32)data[*pos + 2] << 8) | (Uint32)data[*pos + 3];
    *pos += 4;
    return true;
}

static Uint16 read_le16_at(const Uint8 *data, size_t pos) {
    return (Uint16)(((Uint16)data[pos]) | ((Uint16)data[pos + 1] << 8));
}

static Uint32 read_le32_at(const Uint8 *data, size_t pos) {
    return ((Uint32)data[pos]) | ((Uint32)data[pos + 1] << 8) |
           ((Uint32)data[pos + 2] << 16) | ((Uint32)data[pos + 3] << 24);
}

typedef struct {
    bool plausible;
    bool canonical;
} WavHeaderCheck;

static WavHeaderCheck check_wav_header(const char *path) {
    WavHeaderCheck check = {0};
    size_t size = 0;
    Uint8 *data = (Uint8 *)SDL_LoadFile(path, &size);
    if (!data) return check;
    if (size < 44 || SDL_memcmp(data, "RIFF", 4) != 0 || SDL_memcmp(data + 8, "WAVE", 4) != 0) {
        SDL_free(data);
        return check;
    }

    bool have_fmt = false;
    bool have_data = false;
    Uint16 format = 0;
    Uint16 channels = 0;
    Uint32 sample_rate = 0;
    Uint16 bits = 0;
    size_t pos = 12;
    while (pos + 8 <= size) {
        Uint32 chunk_size = read_le32_at(data, pos + 4);
        size_t chunk_data = pos + 8;
        if (chunk_data + chunk_size > size) break;
        if (SDL_memcmp(data + pos, "fmt ", 4) == 0 && chunk_size >= 16) {
            format = read_le16_at(data, chunk_data);
            channels = read_le16_at(data, chunk_data + 2);
            sample_rate = read_le32_at(data, chunk_data + 4);
            bits = read_le16_at(data, chunk_data + 14);
            have_fmt = true;
        } else if (SDL_memcmp(data + pos, "data", 4) == 0) {
            have_data = true;
        }
        pos = chunk_data + chunk_size + (chunk_size & 1u);
    }
    SDL_free(data);

    check.plausible = have_fmt && have_data && channels > 0 && sample_rate > 0 && bits > 0;
    check.canonical = check.plausible &&
                      format == 3 &&
                      channels == VAPORPLANE_PROJECT_CHANNELS &&
                      sample_rate == VAPORPLANE_PROJECT_SAMPLE_RATE &&
                      bits == VAPORPLANE_PROJECT_WAV_BITS_PER_SAMPLE;
    return check;
}

static bool check_midi_structure(const char *path) {
    size_t size = 0;
    Uint8 *data = (Uint8 *)SDL_LoadFile(path, &size);
    if (!data) return false;
    bool ok = false;
    size_t pos = 0;
    Uint32 header_length = 0;
    Uint16 format = 0, track_count = 0, division = 0;
    if (size < 14 || SDL_memcmp(data, "MThd", 4) != 0) goto done;
    pos = 4;
    if (!read_be32_mem(data, size, &pos, &header_length) || header_length < 6 || pos + header_length > size) goto done;
    if (!read_be16_mem(data, size, &pos, &format) ||
        !read_be16_mem(data, size, &pos, &track_count) ||
        !read_be16_mem(data, size, &pos, &division)) {
        goto done;
    }
    if (format > 1 || track_count == 0 || (division & 0x8000) != 0) goto done;
    pos = 8 + header_length;
    for (Uint16 track = 0; track < track_count; ++track) {
        Uint32 track_length = 0;
        if (pos + 8 > size || SDL_memcmp(data + pos, "MTrk", 4) != 0) goto done;
        pos += 4;
        if (!read_be32_mem(data, size, &pos, &track_length) || pos + track_length > size) goto done;
        pos += track_length;
    }
    ok = true;
done:
    SDL_free(data);
    return ok;
}

static int sample_index_by_id(const ValidationSampleRef *samples, int count, const char *sample_id) {
    for (int i = 0; i < count; ++i) {
        if (SDL_strcmp(samples[i].sample_id, sample_id) == 0) return i;
    }
    return -1;
}

static bool validate_manifest(ProjectValidationResult *out,
                              const char *bundle_path,
                              const char *json,
                              ProjectValidationMode mode,
                              ValidationSampleRef *samples,
                              int *sample_count_out,
                              char *timeline_ref,
                              size_t timeline_ref_size,
                              char *surfaces_ref,
                              size_t surfaces_ref_size) {
    const char *end = json + SDL_strlen(json);
    char format[64];
    if (!json_get_string_range(json, end, "format", format, sizeof(format)) ||
        SDL_strcmp(format, VAPORPLANE_PROJECT_FORMAT_NAME) != 0) {
        result_set(out, PROJECT_VALIDATION_INVALID, "unsupported project format", "project.json format is missing or unsupported");
        return false;
    }
    int version = 0;
    if (!json_get_int_range(json, end, "version", &version) ||
        version < 1 || version > VAPORPLANE_PROJECT_FORMAT_VERSION) {
        result_set(out, PROJECT_VALIDATION_INVALID, "unsupported project version", "project.json version is missing or unsupported");
        return false;
    }
    json_get_string_range(json, end, "name", out->project_name, sizeof(out->project_name));
    if (!out->project_name[0]) bundle_name_from_path(bundle_path, out->project_name, sizeof(out->project_name));

    if (!json_get_string_range(json, end, "timeline", timeline_ref, timeline_ref_size)) {
        result_set(out, PROJECT_VALIDATION_INVALID, "missing timeline reference", "project.json has no timeline field");
        return false;
    }
    if (!json_get_string_range(json, end, "surfaces", surfaces_ref, surfaces_ref_size)) {
        result_set(out, PROJECT_VALIDATION_INVALID, "missing surfaces reference", "project.json has no surfaces field");
        return false;
    }
    if (!project_validation_is_safe_relative_path(timeline_ref) ||
        !project_validation_is_safe_relative_path(surfaces_ref)) {
        result_set(out, PROJECT_VALIDATION_INVALID, "unsafe relative path", "timeline or surfaces path escapes the bundle");
        return false;
    }
    if (SDL_strcmp(timeline_ref, VAPORPLANE_PROJECT_TIMELINE_FILENAME) != 0 ||
        SDL_strcmp(surfaces_ref, VAPORPLANE_PROJECT_SURFACES_FILENAME) != 0) {
        result_set(out, PROJECT_VALIDATION_INVALID, "unsupported project reference", "loader currently expects timeline.mid and surfaces.json");
        return false;
    }

    const char *array_start = NULL;
    const char *array_end = NULL;
    if (!json_find_array_range(json, end, "samples", &array_start, &array_end)) {
        result_set(out, PROJECT_VALIDATION_INVALID, "missing samples list", "project.json has no samples array");
        return false;
    }
    int sample_count = 0;
    const char *cursor = array_start + 1;
    const char *object_start = NULL;
    const char *object_end = NULL;
    while (json_next_object(&cursor, array_end - 1, &object_start, &object_end)) {
        if (sample_count >= PROJECT_VALIDATION_MAX_SAMPLES) {
            result_set(out, PROJECT_VALIDATION_INVALID, "too many samples", "project manifest exceeds validator sample capacity");
            return false;
        }
        ValidationSampleRef *sample = &samples[sample_count];
        if (!json_get_string_range(object_start, object_end, "sample_id", sample->sample_id, sizeof(sample->sample_id)) ||
            !json_get_string_range(object_start, object_end, "path", sample->path, sizeof(sample->path))) {
            result_set(out, PROJECT_VALIDATION_INVALID, "broken sample mapping", "sample entry is missing sample_id or path");
            return false;
        }
        if (sample_index_by_id(samples, sample_count, sample->sample_id) >= 0) {
            result_set(out, PROJECT_VALIDATION_INVALID, "duplicate sample_id", "project.json repeats a sample_id");
            return false;
        }
        if (!project_validation_is_safe_relative_path(sample->path) ||
            !path_starts_with_samples_dir(sample->path)) {
            result_set(out, PROJECT_VALIDATION_INVALID, "unsafe relative path", "sample path escapes the samples folder");
            return false;
        }
        sample_count++;
    }
    if (sample_count <= 0) {
        result_set(out, PROJECT_VALIDATION_INVALID, "missing sample file", "project contains no sample mappings");
        return false;
    }

    bool binding_seen[16][128];
    SDL_memset(binding_seen, 0, sizeof(binding_seen));
    if (!json_find_array_range(json, end, "roster_clips", &array_start, &array_end)) {
        result_set(out, PROJECT_VALIDATION_INVALID, "missing roster clips", "project.json has no roster_clips array");
        return false;
    }
    int roster_count = 0;
    cursor = array_start + 1;
    while (json_next_object(&cursor, array_end - 1, &object_start, &object_end)) {
        char roster_clip_id[PROJECT_VALIDATION_ID_MAX];
        char sample_id[PROJECT_VALIDATION_ID_MAX];
        if (!json_get_string_range(object_start, object_end, "roster_clip_id", roster_clip_id, sizeof(roster_clip_id)) ||
            !json_get_string_range(object_start, object_end, "sample_id", sample_id, sizeof(sample_id))) {
            result_set(out, PROJECT_VALIDATION_INVALID, "broken roster mapping", "roster clip is missing roster_clip_id or sample_id");
            return false;
        }
        if (sample_index_by_id(samples, sample_count, sample_id) < 0) {
            result_set(out, PROJECT_VALIDATION_INVALID, "broken roster mapping", "roster clip references an unknown sample_id");
            return false;
        }
        const char *binding_start = NULL;
        const char *binding_end = NULL;
        if (json_find_compound_range(object_start, object_end, "midi_binding", '{', '}', &binding_start, &binding_end)) {
            int channel = 1;
            int note = 60 + roster_count;
            json_get_int_range(binding_start, binding_end, "channel", &channel);
            json_get_int_range(binding_start, binding_end, "note", &note);
            if (channel < 1 || channel > 16 || note < 0 || note > 127) {
                result_set(out, PROJECT_VALIDATION_INVALID, "invalid MIDI binding", "roster clip MIDI binding is out of range");
                return false;
            }
            if (binding_seen[channel - 1][note]) {
                result_set(out, PROJECT_VALIDATION_INVALID, "duplicate MIDI binding", "two roster clips use the same MIDI channel/note");
                return false;
            }
            binding_seen[channel - 1][note] = true;
        } else if (mode == PROJECT_VALIDATION_FULL) {
            result_warn(out, "missing MIDI binding", "roster clip has no midi_binding and will use loader fallback");
        }
        roster_count++;
    }
    if (roster_count <= 0) {
        result_set(out, PROJECT_VALIDATION_INVALID, "missing roster clips", "project contains no roster clips");
        return false;
    }

    if (sample_count_out) *sample_count_out = sample_count;
    return true;
}

static bool validate_surfaces(ProjectValidationResult *out, const char *path) {
    char *json = NULL;
    if (!load_text_file_local(path, &json, NULL)) {
        result_set(out, PROJECT_VALIDATION_INVALID, "missing surfaces.json", "could not read surfaces sidecar");
        return false;
    }
    const char *end = json + SDL_strlen(json);
    char format[64];
    int version = 0;
    bool ok = true;
    if (!json_get_string_range(json, end, "format", format, sizeof(format)) ||
        SDL_strcmp(format, VAPORPLANE_SURFACES_FORMAT_NAME) != 0) {
        result_set(out, PROJECT_VALIDATION_INVALID, "unsupported surfaces format", "surfaces.json format is missing or unsupported");
        ok = false;
    } else if (!json_get_int_range(json, end, "version", &version) ||
               version < 1 || version > VAPORPLANE_SURFACES_FORMAT_VERSION) {
        result_set(out, PROJECT_VALIDATION_INVALID, "unsupported surfaces version", "surfaces.json version is missing or unsupported");
        ok = false;
    }
    SDL_free(json);
    return ok;
}

bool project_validate_bundle(const char *bundle_path,
                             ProjectValidationMode mode,
                             ProjectValidationResult *out) {
    if (!out) return false;
    result_init(out, bundle_path);
    if (!bundle_path || !bundle_path[0]) {
        result_set(out, PROJECT_VALIDATION_INVALID, "missing project path", "bundle path is empty");
        return true;
    }
    if (!path_is_directory_local(bundle_path)) {
        result_set(out, PROJECT_VALIDATION_INVALID, "not a project folder", "bundle path is not a directory");
        return true;
    }
    if (!has_suffix_ci(bundle_path, VAPORPLANE_PROJECT_BUNDLE_SUFFIX)) {
        result_set(out, PROJECT_VALIDATION_INVALID, "missing .vapor suffix", "project folder does not end in .vapor");
        return true;
    }

    char project_path[CLIP_MAX_PATH];
    path_join_local(project_path, sizeof(project_path), bundle_path, VAPORPLANE_PROJECT_MANIFEST_FILENAME);
    char *project_json = NULL;
    if (!load_text_file_local(project_path, &project_json, NULL)) {
        result_set(out, PROJECT_VALIDATION_INVALID, "missing project.json", "could not read project manifest");
        return true;
    }

    ValidationSampleRef samples[PROJECT_VALIDATION_MAX_SAMPLES];
    SDL_memset(samples, 0, sizeof(samples));
    int sample_count = 0;
    char timeline_ref[CLIP_MAX_PATH];
    char surfaces_ref[CLIP_MAX_PATH];
    SDL_memset(timeline_ref, 0, sizeof(timeline_ref));
    SDL_memset(surfaces_ref, 0, sizeof(surfaces_ref));
    bool manifest_ok = validate_manifest(out,
                                         bundle_path,
                                         project_json,
                                         mode,
                                         samples,
                                         &sample_count,
                                         timeline_ref,
                                         sizeof(timeline_ref),
                                         surfaces_ref,
                                         sizeof(surfaces_ref));
    SDL_free(project_json);
    if (!manifest_ok) return true;

    char timeline_path[CLIP_MAX_PATH];
    char surfaces_path[CLIP_MAX_PATH];
    char samples_dir[CLIP_MAX_PATH];
    path_join_local(timeline_path, sizeof(timeline_path), bundle_path, timeline_ref);
    path_join_local(surfaces_path, sizeof(surfaces_path), bundle_path, surfaces_ref);
    path_join_local(samples_dir, sizeof(samples_dir), bundle_path, VAPORPLANE_PROJECT_SAMPLES_DIRNAME);
    if (!path_exists_local(timeline_path)) {
        result_set(out, PROJECT_VALIDATION_INVALID, "missing timeline.mid", "referenced MIDI timeline does not exist");
        return true;
    }
    if (!path_exists_local(surfaces_path)) {
        result_set(out, PROJECT_VALIDATION_INVALID, "missing surfaces.json", "referenced surfaces sidecar does not exist");
        return true;
    }
    if (!path_is_directory_local(samples_dir)) {
        result_set(out, PROJECT_VALIDATION_INVALID, "missing samples folder", "samples directory does not exist");
        return true;
    }

    for (int i = 0; i < sample_count; ++i) {
        char sample_path[CLIP_MAX_PATH];
        path_join_local(sample_path, sizeof(sample_path), bundle_path, samples[i].path);
        if (!path_exists_local(sample_path)) {
            result_set(out, PROJECT_VALIDATION_INVALID, "sample file missing", "referenced bundled sample does not exist");
            return true;
        }
        if (mode == PROJECT_VALIDATION_FULL) {
            WavHeaderCheck wav = check_wav_header(sample_path);
            if (!wav.plausible) {
                result_set(out, PROJECT_VALIDATION_INVALID, "invalid WAV header", "referenced bundled sample is not a plausible WAV");
                return true;
            }
            if (!wav.canonical) {
                result_warn(out, "noncanonical WAV format", "bundled sample is not canonical project WAV format");
            }
        }
    }

    if (mode == PROJECT_VALIDATION_FULL) {
        if (!validate_surfaces(out, surfaces_path)) return true;
        if (!check_midi_structure(timeline_path)) {
            result_set(out, PROJECT_VALIDATION_INVALID, "invalid MIDI header", "timeline.mid is not a structurally valid Standard MIDI File");
            return true;
        }

        char preview_path[CLIP_MAX_PATH];
        path_join_local(preview_path, sizeof(preview_path), bundle_path, VAPORPLANE_PROJECT_PREVIEW_FILENAME);
        if (!path_exists_local(preview_path)) {
            result_warn(out, "preview.wav missing", "project has no browser preview artifact");
        } else {
            WavHeaderCheck preview = check_wav_header(preview_path);
            out->preview_available = preview.plausible;
            if (!preview.plausible) {
                result_warn(out, "preview.wav invalid", "preview.wav is not a plausible WAV file");
            } else if (!preview.canonical) {
                result_warn(out, "preview.wav noncanonical", "preview.wav is not canonical project WAV format");
            }
        }
    }

    if (out->status == PROJECT_VALIDATION_UNKNOWN) {
        result_set(out, PROJECT_VALIDATION_VALID, "valid", "project bundle is valid");
    }
    return true;
}
