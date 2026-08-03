import urllib.request
import json
import zipfile
import os

def download_latest():
    url = "https://api.github.com/repos/ran-j/PS2Recomp/releases/latest"
    req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
    print(f"Fetching latest release from {url}...")
    try:
        with urllib.request.urlopen(req) as response:
            data = json.loads(response.read().decode())
            asset = next((a for a in data.get('assets', []) if 'win' in a['name'].lower() or 'windows' in a['name'].lower()), None)
            
            if not asset:
                print("No Windows release found.")
                return
                
            download_url = asset['browser_download_url']
            print(f"Downloading {download_url}...")
            urllib.request.urlretrieve(download_url, "ps2recomp.zip")
            
            print("Extracting...")
            os.makedirs("tools/ps2recomp_bin", exist_ok=True)
            with zipfile.ZipFile("ps2recomp.zip", 'r') as zip_ref:
                zip_ref.extractall("tools/ps2recomp_bin")
                
            os.remove("ps2recomp.zip")
            print("Done! Extracted to tools/ps2recomp_bin")
            
    except Exception as e:
        print(f"Error: {e}")

if __name__ == "__main__":
    download_latest()
