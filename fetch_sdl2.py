import urllib.request
import zipfile
import os

url = "https://github.com/libsdl-org/SDL/releases/download/release-2.30.6/SDL2-devel-2.30.6-VC.zip"
zip_path = "SDL2.zip"
extract_path = "third_party/SDL2"

if not os.path.exists(extract_path):
    print("Downloading SDL2...")
    urllib.request.urlretrieve(url, zip_path)
    print("Extracting SDL2...")
    with zipfile.ZipFile(zip_path, 'r') as zip_ref:
        zip_ref.extractall("third_party")
    os.rename("third_party/SDL2-2.30.6", extract_path)
    os.remove(zip_path)
    print("Done!")
else:
    print("SDL2 already exists.")
