Import("env")
import serial
import time

def release_pins(source, target, env):
    port = env.get("UPLOAD_PORT")
    if not port:
        print("No serial port found.")
        return

    try:
        print("Releasing DTR/RTS on port:", port)
        with serial.Serial(port) as ser:
            ser.dtr = False   # Laat GPIO0 los
            ser.rts = False   # Laat RESET los
            time.sleep(0.1)
    except Exception as e:
        print("Warning: Couldn't release serial lines:", e)

env.AddPostAction("upload", release_pins)
