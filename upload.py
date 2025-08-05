Import("env")
import os
import time
import urllib.request
import subprocess

# === CONFIG ===
URL = "http://192.168.4.1"
RETRY_DELAY = 3  # seconden tussen pogingen
CHROME_PATH = r"C:\Program Files\Google\Chrome\Application\chrome.exe"  # Pas aan als nodig

# === Pre-upload: LittleFS ===
def before_upload(source, target, env):
    if os.path.isdir("data") and os.listdir("data"):
        print("📂 Uploading LittleFS files before firmware...")
        env.Execute("pio run --target uploadfs")
    else:
        print("⚠️  'data/' folder is missing or empty. Skipping LittleFS upload.")

env.AddPreAction("upload", before_upload)

# === Post-upload: open Chrome tijdelijk ===
def after_upload(source, target, env):
    print(f"⌛ Wachten tot {URL} bereikbaar is...")

    while True:
        try:
            response = urllib.request.urlopen(URL, timeout=2)
            if response.status == 200:
                print("✅ Webpagina is bereikbaar! Openen in Chrome...")

                # Start Chrome met nieuwe venster
                chrome_process = subprocess.Popen([
                    CHROME_PATH,
                    "--new-window",
                    URL
                ])
                
                # 5 seconden wachten, dan afsluiten
                time.sleep(5)
                chrome_process.terminate()
                print("🛑 Chrome gesloten na 5 seconden.")
                break
        except Exception as e:
            print(f"⏳ Nog niet bereikbaar... ({e})")
        time.sleep(RETRY_DELAY)

env.AddPostAction("upload", after_upload)
