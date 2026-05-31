Import("env")
import struct

def stm32_crc(data):
    crc = 0xFFFFFFFF
    # Pad binary with 0xFF so it's a multiple of 4 bytes
    if len(data) % 4 != 0:
        data += b'\xFF' * (4 - (len(data) % 4))
    
    for i in range(0, len(data), 4):
        # Read 32-bit word (little endian)
        word = data[i] | (data[i+1] << 8) | (data[i+2] << 16) | (data[i+3] << 24)
        crc ^= word
        for _ in range(32):
            if crc & 0x80000000:
                crc = (crc << 1) ^ 0x04C11DB7
            else:
                crc = (crc << 1)
            crc &= 0xFFFFFFFF
    return crc

def inject_crc_to_bin(source, target, env):
    firmware_path = target[0].get_abspath()
    
    with open(firmware_path, "rb") as f:
        data = bytearray(f.read())
        
    # Search for our magic number 0xAA55AA55 (little endian: 55 AA 55 AA)
    magic_bytes = b'\x55\xAA\x55\xAA'
    idx = data.find(magic_bytes)
    
    if idx == -1:
        print("\n[ERROR] AppHeader magic number not found in binary!")
        return

    print(f"\n[SUCCESS] Found AppHeader at offset 0x{idx:X}")
    
    length = len(data)
    
    # 1. Make sure CRC field is cleared to 0xFFFFFFFF for calculation
    data[idx+4 : idx+8] = b'\xFF\xFF\xFF\xFF'
    
    # 2. Update the Length field in the binary
    length_bytes = struct.pack("<I", length)
    data[idx+8 : idx+12] = length_bytes
    
    # 3. Calculate STM32 hardware CRC over the whole binary
    calculated_crc = stm32_crc(data)
    print(f"[INFO] Calculated CRC32: 0x{calculated_crc:08X} for length {length} bytes")
    
    # 4. Inject the calculated CRC back into the header
    crc_bytes = struct.pack("<I", calculated_crc)
    data[idx+4 : idx+8] = crc_bytes
    
    # Write back the modified binary
    with open(firmware_path, "wb") as f:
        f.write(data)
        
    print("[SUCCESS] Checksum and Length successfully injected into firmware.bin!\n")

# Attach the script to run automatically after the .bin is created
env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", inject_crc_to_bin)