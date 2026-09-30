#ifndef OPENDAC_RATE_CONTROL_H
#define OPENDAC_RATE_CONTROL_H
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    RATE_IDLE, RATE_MEASURING, RATE_ADJUST_PENDING, RATE_VERIFYING,
    RATE_DONE, RATE_FAILED
} rate_phase_t;

typedef struct {
    uint16_t pll_n;
    uint16_t divisor;
    bool valid;
} rate_override_t;

enum {
    CALIBRATION_REQUIRED_THRESHOLD_PPM = 1000,
    CALIBRATION_SUCCESS_THRESHOLD_PPM = 2500,
    CALIBRATION_FAILURE_THRESHOLD_PPM = 10000
};

void rate_begin(uint32_t hz, uint16_t pll_n, uint16_t divisor);
void rate_sof(uint32_t consumed_halfwords);
bool rate_take_adjustment(rate_override_t *out);
rate_override_t rate_override_for(uint32_t hz);
rate_phase_t rate_phase(void);
uint32_t rate_measured_hz(void);
int32_t rate_error_ppm(void);
bool rate_calibration_valid(uint32_t hz);
bool rate_gross_failure(void);
uint32_t rate_feedback_q14(uint32_t hz, uint32_t queued_halfwords);

#endif
