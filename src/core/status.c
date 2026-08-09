#include "odm_status.h"

const char *odm_status_name(odm_status status) {
    switch (status) {
        case ODM_STATUS_OK:               return "ok";
        case ODM_STATUS_INVALID_ARGUMENT: return "invalid_argument";
        case ODM_STATUS_UNSUPPORTED:      return "unsupported";
        case ODM_STATUS_OVERFLOW:         return "overflow";
        case ODM_STATUS_OUT_OF_MEMORY:    return "out_of_memory";
        case ODM_STATUS_BUDGET_EXCEEDED:  return "budget_exceeded";
        case ODM_STATUS_CANCELLED:        return "cancelled";
        case ODM_STATUS_BUSY:             return "busy";
        case ODM_STATUS_INVALID_STATE:    return "invalid_state";
        case ODM_STATUS_BUFFER_TOO_SMALL: return "buffer_too_small";
        case ODM_STATUS_INVARIANT_BROKEN: return "invariant_broken";
        case ODM_STATUS_INTERNAL_ERROR:   return "internal_error";
        case ODM_STATUS_INVALID_DATA:     return "invalid_data";
        case ODM_STATUS_INTEGRITY_ERROR:  return "integrity_error";
        case ODM_STATUS_VERSION_MISMATCH: return "version_mismatch";
        default:                          return "unknown_status";
    }
}

int odm_status_is_ok(odm_status status) {
    return status == ODM_STATUS_OK;
}
