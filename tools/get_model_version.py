import json
import sys
import os


def get_version(path):
    info_path = os.path.join(path, "model_info.json")
    if not os.path.exists(info_path):
        return "Unknown"

    try:
        with open(info_path, 'r') as f:
            data = json.load(f)
            return f"{data.get('version', 'Unknown')} ({data.get('date', '')})"
    except:
        return "Error"


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: get_model_version.py <model_dir>")
        sys.exit(1)

    print(get_version(sys.argv[1]))
