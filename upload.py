import os
import time
import urllib.request
import subprocess

# === CONFIG ===
URL = "http://192.168.4.1"
RETRY_DELAY = 3
CHROME_PATH = r"C:\Program Files\Google\Chrome\Application\chrome.exe"
SERIAL_PORT = "COM12"
BAUDRATE = 115200
TRIGGER = "UPLOAD"   # Bericht van de ESP dat upload moet starten

def upload_with_hooks():
    """Run platformio upload + jouw pre/post acties."""
    # Pre-upload LittleFS
    if os.path.isdir("data") and os.listdir("data"):
        print("📂 Uploading LittleFS files before firmware...")
        subprocess.run(["pio", "run", "--target", "uploadfs"])

    # Firmware upload
    print("🚀 Start firmware upload...")
    result = subprocess.run(["pio", "run", "--target", "upload"])
    if result.returncode != 0:
        print("❌ Upload mislukt")
        return

    # Post-upload: wachten op webpagina
    print(f"⌛ Wachten tot {URL} bereikbaar is...")
    while True:
        try:
            response = urllib.request.urlopen(URL, timeout=2)
            if response.status == 200:
                print("✅ Webpagina is bereikbaar! Openen in Chrome...")
                chrome_process = subprocess.Popen([CHROME_PATH, "--new-window", URL])
                time.sleep(5)
                chrome_process.terminate()
                print("🛑 Chrome gesloten na 5 seconden.")
                break
        except Exception:
            print("⏳ Nog niet bereikbaar...")
            time.sleep(RETRY_DELAY)

def wait_for_trigger():
    """Lees één regel van COM12 en sluit de poort meteen."""
    try:
        with serial.Serial(SERIAL_PORT, BAUDRATE, timeout=1) as ser:
            line = ser.readline().decode(errors="ignore").strip()
            return line
    except Exception:
        return None

def serial_loop():
    """Blijf luisteren naar COM12 en trigger upload zodra er iets uitkomt."""
    while True:
        line = wait_for_trigger()
        if line:
            print(f"🔎 Ontvangen: {line}")
            print("✅ Trigger ontvangen → starten upload")
            upload_with_hooks()
            print("🔄 Klaar, wacht op nieuwe trigger...")
        time.sleep(0.2)



if __name__ == "__main__":
    serial_loop()
