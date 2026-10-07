import os
import shutil
import zipfile

roms_root = os.path.abspath("roms")

# Target folders
games_dir = os.path.join(roms_root, "games")
video_games_dir = os.path.join(roms_root, "video_games")
videos_dir = os.path.join(roms_root, "videos")
hardware_dir = os.path.join(roms_root, "hardware")
docs_media_dir = os.path.join(roms_root, "docs_and_media")

for d in [games_dir, video_games_dir, videos_dir, hardware_dir, docs_media_dir]:
    os.makedirs(d, exist_ok=True)

# 1. Move docs and media
for item in ["boxart", "manuals 300dpi", "!carts.jpg", "!DigiBlast Games.xlsx"]:
    p = os.path.join(roms_root, item)
    if os.path.exists(p):
        dest = os.path.join(docs_media_dir, item)
        if not os.path.exists(dest):
            shutil.move(p, dest)
            print(f"[Media] Moved {item} -> docs_and_media/")

# 2. Check "not by me" folder and move files to root for categorization
not_by_me = os.path.join(roms_root, "not by me")
if os.path.exists(not_by_me):
    for f in os.listdir(not_by_me):
        src = os.path.join(not_by_me, f)
        dest = os.path.join(roms_root, f)
        if not os.path.exists(dest):
            shutil.move(src, dest)
    shutil.rmtree(not_by_me, ignore_errors=True)
    print("[Organize] Processed 'not by me' folder.")

# 3. Categorize zip files
for f in os.listdir(roms_root):
    if not f.endswith(".zip"):
        continue
    src = os.path.join(roms_root, f)
    
    if "[V+G]" in f:
        dest_folder = video_games_dir
        cat = "Video+Games"
    elif "[G]" in f:
        dest_folder = games_dir
        cat = "Games"
    elif "[V]" in f:
        dest_folder = videos_dir
        cat = "Videos"
    elif "MP3" in f:
        dest_folder = hardware_dir
        cat = "Hardware"
    else:
        dest_folder = games_dir
        cat = "Games"
        
    dest_path = os.path.join(dest_folder, f)
    if not os.path.exists(dest_path):
        shutil.move(src, dest_path)
        print(f"[{cat}] Moved {f}")

# 4. Extract .bin files from games and video_games
print("\nExtracting cartridge .bin files for emulator...")
for folder in [games_dir, video_games_dir]:
    for f in os.listdir(folder):
        if f.endswith(".zip"):
            zip_path = os.path.join(folder, f)
            with zipfile.ZipFile(zip_path, "r") as z:
                for member in z.namelist():
                    if member.endswith(".bin"):
                        target_file = os.path.join(folder, member)
                        if not os.path.exists(target_file):
                            z.extract(member, folder)
                            print(f"  Extracted: {member} ({os.path.getsize(target_file)} bytes)")

print("\n--- Organization Complete! ---")
