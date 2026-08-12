/* =============================================================================
 * sd_driver.h — SPI SD Card Driver & File System Interface
 * Project: Digital Harmonium Workstation Firmware
 *
 * This driver provides the file I/O API used by:
 *   - The VHR recorder (write event stream, finalize header)
 *   - The VHR player loader (read .vhr files from /recordings/)
 *   - The VHL loader (read .vhl instrument files from /instruments/)
 *
 * SD Card Directory Layout:
 *   /instruments/          VHL instrument packages (*.vhl)
 *   /recordings/           VHR performance recordings (*.vhr)
 *
 * Recording Write Strategy:
 *   1. vhl_sd_file_open_write() — opens /recordings/<name>.vhr for writing
 *   2. Write placeholder header (96 zero bytes) with vhl_sd_file_write()
 *   3. Append event stream blocks as they are drained from the ring buffer
 *   4. vhl_sd_file_seek(handle, 0) — seek back to file start
 *   5. Write finalized VHRFileHeader to overwrite the placeholder
 *   6. vhl_sd_file_close()
 *
 * Hardware: SPI bus, CS on VHL_PIN_SPI_CS_SD (GPIO5).
 * Library: Arduino SD.h (or SdFat — implementation-dependent).
 * ============================================================================= */

#ifndef SD_DRIVER_H
#define SD_DRIVER_H

#include "vhl_types.h"
#include "vhl_err.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * Opaque File Handle
 * The implementation (sd_driver.c) defines what vhl_sd_file_handle_t actually is.
 * Callers treat it as an opaque pointer and must not dereference it.
 * ============================================================================= */

typedef void* vhl_sd_file_handle_t;

/* =============================================================================
 * SD Card Initialization
 * ============================================================================= */

/**
 * Initialize the SPI SD card driver and mount the filesystem.
 * Must be called once during system startup before any file operations.
 * Creates /instruments/ and /recordings/ directories if they do not exist.
 *
 * @return VHL_OK on success.
 *         VHL_ERR_FAIL if the SD card is not present or cannot be initialized.
 */
vhl_err_t vhl_sd_driver_init(void);

/**
 * Check whether the SD card is currently mounted and ready.
 * @return true if mounted and ready, false otherwise.
 */
bool vhl_sd_is_ready(void);

/* =============================================================================
 * Write Operations (used by VHR recorder)
 * ============================================================================= */

/**
 * Open a file for writing (creates file if it does not exist; overwrites if it does).
 * The path must be an absolute SD card path, e.g. "/recordings/perf01.vhr".
 *
 * @param path       Null-terminated absolute file path.
 * @param outHandle  Set to the opened file handle on success. Must not be NULL.
 * @return VHL_OK or VHL_ERR_FAIL.
 */
vhl_err_t vhl_sd_file_open_write(const char* path, vhl_sd_file_handle_t* outHandle);

/**
 * Write data to an open file.
 * Advances the file write cursor by length bytes.
 *
 * @param handle   File handle from vhl_sd_file_open_write().
 * @param data     Pointer to data to write.
 * @param length   Number of bytes to write.
 * @return VHL_OK or VHL_ERR_FAIL (partial write counts as failure).
 */
vhl_err_t vhl_sd_file_write(vhl_sd_file_handle_t handle,
                             const void* data,
                             size_t length);

/**
 * Seek to an absolute byte offset within an open file.
 * Used to rewind to offset 0 to overwrite the VHR file header after finalization.
 *
 * @param handle  File handle.
 * @param offset  Byte offset from the beginning of the file.
 * @return VHL_OK or VHL_ERR_FAIL.
 */
vhl_err_t vhl_sd_file_seek(vhl_sd_file_handle_t handle, uint32_t offset);

/**
 * Flush any buffered write data to the SD card.
 * Call periodically during long recordings to reduce data loss risk on power failure.
 *
 * @param handle  File handle.
 * @return VHL_OK or VHL_ERR_FAIL.
 */
vhl_err_t vhl_sd_file_flush(vhl_sd_file_handle_t handle);

/**
 * Close an open file. Flushes and releases the file handle.
 * The handle is invalid after this call.
 *
 * @param handle  File handle to close.
 * @return VHL_OK or VHL_ERR_FAIL.
 */
vhl_err_t vhl_sd_file_close(vhl_sd_file_handle_t handle);

/* =============================================================================
 * Read Operations (used by VHR player loader and VHL instrument loader)
 * ============================================================================= */

/**
 * Open a file for reading.
 *
 * @param path       Null-terminated absolute file path.
 * @param outHandle  Set to the opened file handle on success.
 * @return VHL_OK, VHL_ERR_NOT_FOUND if file does not exist, or VHL_ERR_FAIL.
 */
vhl_err_t vhl_sd_file_open_read(const char* path, vhl_sd_file_handle_t* outHandle);

/**
 * Read data from an open file.
 * Advances the file read cursor by the number of bytes actually read.
 *
 * @param handle    File handle from vhl_sd_file_open_read().
 * @param outBuffer Destination buffer. Must be at least length bytes.
 * @param length    Maximum number of bytes to read.
 * @param outRead   Set to the actual number of bytes read (may be < length at EOF).
 * @return VHL_OK, or VHL_ERR_FAIL on I/O error.
 */
vhl_err_t vhl_sd_file_read(vhl_sd_file_handle_t handle,
                            void* outBuffer,
                            size_t length,
                            size_t* outRead);

/**
 * Query the size of a file on the SD card without opening it for reading.
 * Returns VHL_ERR_NOT_FOUND if the file does not exist.
 *
 * @param path     Null-terminated absolute file path.
 * @param outSize  Set to the file size in bytes on success.
 * @return VHL_OK, VHL_ERR_NOT_FOUND, or VHL_ERR_FAIL.
 */
vhl_err_t vhl_sd_file_size(const char* path, uint32_t* outSize);

/**
 * Check whether a file exists on the SD card.
 * @param path  Null-terminated absolute file path.
 * @return true if the file exists and is readable, false otherwise.
 */
bool vhl_sd_file_exists(const char* path);

/* =============================================================================
 * Directory Operations (used by VHR/VHL browser)
 * ============================================================================= */

/**
 * List files in a directory.
 * Fills outNames with null-terminated filenames (not full paths).
 * Each entry in outNames is a char array of VHL_SD_FILENAME_MAX_LEN bytes.
 *
 * @param dirPath      Null-terminated directory path (e.g. "/recordings").
 * @param outNames     Caller-provided 2D array: outNames[maxFiles][VHL_SD_FILENAME_MAX_LEN].
 * @param maxFiles     Maximum number of entries to fill.
 * @param outCount     Set to the actual number of entries returned.
 * @return VHL_OK, VHL_ERR_NOT_FOUND if directory does not exist, or VHL_ERR_FAIL.
 */
#define VHL_SD_FILENAME_MAX_LEN (32U)

vhl_err_t vhl_sd_dir_list(const char*  dirPath,
                           char         outNames[][VHL_SD_FILENAME_MAX_LEN],
                           uint8_t      maxFiles,
                           uint8_t*     outCount);

#ifdef __cplusplus
}
#endif

#endif /* SD_DRIVER_H */
