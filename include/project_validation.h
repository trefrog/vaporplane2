#pragma once

#include <stdbool.h>
#include "clip.h"

#define PROJECT_VALIDATION_REASON_MAX 96
#define PROJECT_VALIDATION_DETAIL_MAX 192
#define PROJECT_VALIDATION_NAME_MAX 128

typedef enum {
    PROJECT_VALIDATION_VALID,
    PROJECT_VALIDATION_WARNING,
    PROJECT_VALIDATION_INVALID,
    PROJECT_VALIDATION_UNKNOWN
} ProjectValidationStatus;

typedef enum {
    PROJECT_VALIDATION_QUICK,
    PROJECT_VALIDATION_FULL
} ProjectValidationMode;

typedef struct {
    ProjectValidationStatus status;
    char reason[PROJECT_VALIDATION_REASON_MAX];
    char detail[PROJECT_VALIDATION_DETAIL_MAX];
    char project_name[PROJECT_VALIDATION_NAME_MAX];
    char bundle_path[CLIP_MAX_PATH];
    bool preview_available;
} ProjectValidationResult;

bool project_validate_bundle(const char *bundle_path,
                             ProjectValidationMode mode,
                             ProjectValidationResult *out);
const char *project_validation_status_label(ProjectValidationStatus status);
bool project_validation_is_safe_relative_path(const char *path);
