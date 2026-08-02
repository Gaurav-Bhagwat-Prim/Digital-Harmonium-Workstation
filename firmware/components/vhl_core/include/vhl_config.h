/* =============================================================================
 * vhl_config.h — Global System Configuration & Hardware Map
 * Project: Digital Harmonium Workstation Firmware
 * Stage 1 to 5 — System Core Architecture
 * ============================================================================= */

#ifndef VHL_CONFIG_H
#define VHL_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* -----------------------------------------------------------------------------
 * Audio Hardware Configuration
 * ----------------------------------------------------------------------------- */

#define VHL_SAMPLE_RATE          (44100U)
#define VHL_DMA_BUFFER_FRAMES    (512U)
#define VHL_DMA_BUFFER_COUNT     (2U)
#define VHL_MAX_POLYPHONY        (16U)

/* -----------------------------------------------------------------------------
 * Input Scanning & Debouncing Parameters
 * ----------------------------------------------------------------------------- */

#define VHL_INPUT_DEBOUNCE_SAMPLES     (4U)    /* 4-bit shift register integrator */
#define VHL_INPUT_SCAN_INTERVAL_MS     (2.5f)  /* 2.5 ms scan period (10 ms debounce window) */
#define VHL_KEYBOARD_MATRIX_ROWS       (5U)    /* 5 Matrix Rows */
#define VHL_KEYBOARD_MATRIX_COLS       (8U)    /* 8 Matrix Columns (Up to 40 keys) */

/* -----------------------------------------------------------------------------
 * Analog Slider Filtering Parameters
 * ----------------------------------------------------------------------------- */

#define VHL_SLIDER_EMA_ALPHA           (0.20f) /* EMA smoothing coefficient (0.0 to 1.0) */
#define VHL_SLIDER_HYSTERESIS_LSB      (2U)    /* 2-LSB deadband noise guard */

/* -----------------------------------------------------------------------------
 * FreeRTOS Task Priorities (0 to 7 Scale, 7 = Highest)
 * ----------------------------------------------------------------------------- */

#define VHL_TASK_PRIO_AUDIO      (7U)  /* Core 1: Dedicated Real-Time DSP Audio */
#define VHL_TASK_PRIO_INPUT      (4U)  /* Core 0: Keyboard Scan & Debouncing    */
#define VHL_TASK_PRIO_UI         (3U)  /* Core 0: LCD Display Render Loop       */
#define VHL_TASK_PRIO_LOADER     (2U)  /* Core 0: VHL Flash Package Installer   */
#define VHL_TASK_PRIO_STORAGE    (2U)  /* Core 0: Event Recorder SD Writer      */
#define VHL_TASK_PRIO_DIAG       (1U)  /* Core 0: System Health & Diagnostics   */

/* -----------------------------------------------------------------------------
 * Task Stack Size Reservations (in Bytes)
 * ----------------------------------------------------------------------------- */

#define VHL_STACK_SIZE_AUDIO    (8192U)
#define VHL_STACK_SIZE_INPUT    (4096U)
#define VHL_STACK_SIZE_UI       (4096U)
#define VHL_STACK_SIZE_LOADER   (8192U)
#define VHL_STACK_SIZE_STORAGE  (4096U)
#define VHL_STACK_SIZE_DIAG     (2048U)

/* -----------------------------------------------------------------------------
 * Static Queue Capacities
 * ----------------------------------------------------------------------------- */

#define VHL_QUEUE_CAP_KEY_EVENTS (64U)
#define VHL_QUEUE_CAP_UI_CMDS    (32U)
#define VHL_QUEUE_CAP_STORAGE    (16U)

/* -----------------------------------------------------------------------------
 * Target Hardware Pin Maps (ESP32 DevKit V1 Prototype)
 * ----------------------------------------------------------------------------- */

#define VHL_PIN_I2S_BCK         (26)
#define VHL_PIN_I2S_WS          (25)
#define VHL_PIN_I2S_DATA        (22)

#define VHL_PIN_SPI_MISO        (19)
#define VHL_PIN_SPI_MOSI        (23)
#define VHL_PIN_SPI_CLK         (18)
#define VHL_PIN_SPI_CS_SD       (5)

#define VHL_PIN_LCD_SDA         (21)
#define VHL_PIN_LCD_SCL         (22)

#ifdef __cplusplus
}
#endif

#endif /* VHL_CONFIG_H */
