#include "odm_progress.h"

typedef struct {
    uint64_t high;
    uint64_t low;
} odm_progress_u128;

static odm_progress_u128 odm_progress_mul_u64(uint64_t left, uint64_t right) {
    const uint64_t mask = UINT64_C(0xffffffff);
    uint64_t left_low = left & mask;
    uint64_t left_high = left >> 32;
    uint64_t right_low = right & mask;
    uint64_t right_high = right >> 32;
    uint64_t t = left_low * right_low;
    uint64_t word0 = t & mask;
    uint64_t carry = t >> 32;
    uint64_t word1;
    uint64_t word2;
    odm_progress_u128 result;
    t = left_high * right_low + carry;
    word1 = t & mask;
    word2 = t >> 32;
    t = left_low * right_high + word1;
    carry = t >> 32;
    result.low = (t << 32) + word0;
    result.high = left_high * right_high + word2 + carry;
    return result;
}

static int odm_progress_u128_le(odm_progress_u128 left,
                                odm_progress_u128 right) {
    return left.high < right.high ||
           (left.high == right.high && left.low <= right.low);
}

const char *odm_job_state_name(odm_job_state state) {
    switch (state) {
        case ODM_JOB_IDLE:      return "idle";
        case ODM_JOB_STARTING:  return "starting";
        case ODM_JOB_RUNNING:   return "running";
        case ODM_JOB_SUCCEEDED: return "succeeded";
        case ODM_JOB_FAILED:    return "failed";
        case ODM_JOB_CANCELLED: return "cancelled";
        case ODM_JOB_DESTROYED: return "destroyed";
        default:                return "unknown";
    }
}

const char *odm_job_phase_name(odm_job_phase phase) {
    switch (phase) {
        case ODM_PHASE_IDLE:      return "idle";
        case ODM_PHASE_PREPARE:   return "prepare";
        case ODM_PHASE_VALIDATE:  return "validate";
        case ODM_PHASE_EXECUTE:   return "execute";
        case ODM_PHASE_FINALIZE:  return "finalize";
        case ODM_PHASE_COMPLETE:  return "complete";
        case ODM_PHASE_CANCELLED: return "cancelled";
        case ODM_PHASE_FAILED:    return "failed";
        default:                  return "unknown";
    }
}

odm_status odm_progress_fraction(uint64_t done, uint64_t total,
                                 uint32_t *out_millionths) {
    uint32_t low = 0u;
    uint32_t high = ODM_PROGRESS_ONE;
    odm_progress_u128 target;
    if (!out_millionths || done > total) return ODM_STATUS_INVALID_ARGUMENT;
    if (total == 0u) {
        *out_millionths = 0u;
        return ODM_STATUS_OK;
    }
    if (done == total) {
        *out_millionths = ODM_PROGRESS_ONE;
        return ODM_STATUS_OK;
    }
    target = odm_progress_mul_u64(done, (uint64_t)ODM_PROGRESS_ONE);
    while (low < high) {
        uint32_t middle = low + (high - low + 1u) / 2u;
        odm_progress_u128 candidate = odm_progress_mul_u64((uint64_t)middle,
                                                           total);
        if (odm_progress_u128_le(candidate, target)) low = middle;
        else high = middle - 1u;
    }
    *out_millionths = low;
    return ODM_STATUS_OK;
}
