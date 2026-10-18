//! Standard IEEE 802.3 CRC-32 checksum calculation.
//! Mirrors ESP32 ROM table algorithm.

pub struct Crc32;

impl Crc32 {
    const POLY: u32 = 0xEDB88320;

    /// Compute CRC-32 across a byte slice
    pub fn compute(data: &[u8]) -> u32 {
        let mut crc: u32 = 0xFFFFFFFF;
        for &byte in data {
            crc ^= byte as u32;
            for _ in 0..8 {
                if (crc & 1) != 0 {
                    crc = (crc >> 1) ^ Self::POLY;
                } else {
                    crc >>= 1;
                }
            }
        }
        crc ^ 0xFFFFFFFF
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_crc32_check() {
        // Standard check: "123456789" -> 0xCBF43926
        let input = b"123456789";
        let crc = Crc32::compute(input);
        assert_eq!(crc, 0xCBF43926);
    }
}
