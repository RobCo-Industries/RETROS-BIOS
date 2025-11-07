#ifndef PWM_AUDIO_H
#define PWM_AUDIO_H

#include <stdint.h>

// Initialize PWM for audio output
void pwm_audio_init(void);

// Play a beep at specified frequency and duration
void pwm_play_beep(uint32_t frequency_hz, uint32_t duration_ms);

// Boot beep sequence
void pwm_boot_beep(void);

#endif // PWM_AUDIO_H
