#include "pwm_audio.h"
#include "hardware.h"

// BCM2835 Clock Manager password - required for clock modifications
// This is a hardware security feature to prevent accidental changes
#define PWM_CLOCK_PASSWORD 0x5A000000

void pwm_audio_init(void) {
    // Stop PWM clock
    MMIO_WRITE(CM_PWMCTL, PWM_CLOCK_PASSWORD | 0x01);
    delay_us(110);
    
    // Wait for clock to stop
    while (MMIO_READ(CM_PWMCTL) & 0x80) {
        delay_us(1);
    }
    
    // Set clock divider for audio frequencies
    // 19.2 MHz / 2 = 9.6 MHz
    MMIO_WRITE(CM_PWMDIV, PWM_CLOCK_PASSWORD | (2 << 12));
    
    // Source = oscillator (1), enable
    MMIO_WRITE(CM_PWMCTL, PWM_CLOCK_PASSWORD | 0x11);
    
    // Wait for clock to start
    delay_us(110);
    
    // Configure GPIO 40/45 for PWM (ALT0)
    // For simplicity, we'll use GPIO 40
    uint32_t ra = MMIO_READ(GPIO_BASE + 0x10);  // GPFSEL4
    ra &= ~(7 << 0);  // Clear GPIO 40
    ra |= (4 << 0);   // Set ALT0
    MMIO_WRITE(GPIO_BASE + 0x10, ra);
    
    // Disable PWM
    MMIO_WRITE(PWM_CTL, 0);
    
    // Clear FIFO
    delay_us(10);
    
    // Configure PWM in PWM mode (not serializer mode)
    MMIO_WRITE(PWM_CTL, 0);
}

void pwm_play_beep(uint32_t frequency_hz, uint32_t duration_ms) {
    if (frequency_hz == 0 || duration_ms == 0) {
        return;
    }
    
    // Calculate range for frequency
    // PWM clock is 9.6 MHz
    uint32_t range = 9600000 / frequency_hz;
    
    // Set range
    MMIO_WRITE(PWM_RNG1, range);
    
    // Set data to 50% duty cycle for square wave
    MMIO_WRITE(PWM_DAT1, range / 2);
    
    // Enable PWM
    MMIO_WRITE(PWM_CTL, 0x81);  // Enable + PWM mode
    
    // Play for duration
    delay_ms(duration_ms);
    
    // Disable PWM
    MMIO_WRITE(PWM_CTL, 0);
}

void pwm_boot_beep(void) {
    // Fallout-style boot beep sequence
    // High-low-mid pattern
    pwm_play_beep(800, 150);   // High beep
    delay_ms(50);
    pwm_play_beep(400, 150);   // Low beep
    delay_ms(50);
    pwm_play_beep(600, 200);   // Mid beep
}
