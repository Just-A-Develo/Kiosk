Import("env")

def before_upload(source, target, env):
    print("📂 Uploading LittleFS files...")
    env.Execute("pio run --target uploadfs")

env.AddPreAction("upload", before_upload)
