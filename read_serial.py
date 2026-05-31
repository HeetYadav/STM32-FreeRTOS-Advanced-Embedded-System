import serial
import time
import sys

try:
    print("Opening COM13...")
    ser = serial.Serial('COM13', 115200, timeout=1)
    
    # Send a newline to get prompt
    ser.write(b'\r\n')
    
    print("Reading from COM13 for 5 seconds...")
    start_time = time.time()
    output = []
    
    while time.time() - start_time < 5:
        if ser.in_waiting:
            line = ser.readline().decode('utf-8', errors='replace')
            output.append(line.strip())
            
    ser.close()
    
    print("\n--- SERIAL OUTPUT ---")
    for line in output:
        print(line)
    print("---------------------\n")
    
except Exception as e:
    print(f"Error: {e}")
    sys.exit(1)
