"""Download the published hbmenu archive and run the same bridge used by Swift."""
import argparse
import hashlib
import io
import pathlib
import shutil
import subprocess
import urllib.request
import zipfile

URL = "https://github.com/switchbrew/nx-hbmenu/releases/download/v3.6.1/nx-hbmenu_v3.6.1.zip"
DIGEST = "4790c58fdd75c4bcc644fa8135fa11c380cb3ebaf685e400e336031972410ee7"
RALLY = pathlib.Path(__file__).resolve().parent.parent / "apps" / "ios" / "Resources" / "rally.nro"
RACER_URL = "https://github.com/HopeAero/nfm-switch-port/releases/download/v1.2.3/nfm_switch.nro"
RACER_DIGEST = "1fac25fd4aa950b825766e4859312f74bf2944846a07f93d0c86f29eb5b7501a"

def fetch_racer(directory):
    with urllib.request.urlopen(RACER_URL, timeout=90) as response:
        data = response.read(14 << 20)
    if hashlib.sha256(data).hexdigest() != RACER_DIGEST:
        raise ValueError("Unexpected racing homebrew fingerprint")
    (directory / "nfm_switch.nro").write_bytes(data)

def fetch(directory):
    directory.mkdir(parents=True, exist_ok=True)
    with urllib.request.urlopen(URL, timeout=60) as response:
        archive = response.read(16 << 20)
    if hashlib.sha256(archive).hexdigest() != DIGEST:
        raise ValueError("Unexpected archive fingerprint")
    with zipfile.ZipFile(io.BytesIO(archive)) as package:
        name, = [item for item in package.namelist() if pathlib.PurePosixPath(item).name == "hbmenu.nro"]
        nro = directory / "hbmenu.nro"
        nro.write_bytes(package.read(name))
    return nro

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("probe", type=pathlib.Path, nargs="?")
    parser.add_argument("directory", type=pathlib.Path, nargs="?")
    parser.add_argument("--fetch-only", metavar="DIRECTORY", type=pathlib.Path)
    args = parser.parse_args()
    if args.fetch_only is not None:
        fetch(args.fetch_only)
        fetch_racer(args.fetch_only)
        return
    if not args.probe or not args.directory:
        parser.error("Specify the probe executable and an output directory")
    nro = fetch(args.directory)
    frame = args.directory / "hbmenu.ppm"
    subprocess.run([args.probe.resolve(), nro, args.directory / "title", "frame", frame], check=True)
    with frame.open("rb") as image:
        assert image.readline() == b"P6\n" and image.readline() == b"1280 720\n"
        assert image.readline() == b"255\n"
        pixels = image.read()
    if len(pixels) != 1280 * 720 * 3 or len(set(zip(pixels[::3], pixels[1::3], pixels[2::3]))) < 16:
        raise ValueError("Frame missing or uniform")
    # hbmenu lists Rally from its own SD, starts it with A and becomes the menu again after +.
    launcher = args.directory / "launcher"
    shutil.rmtree(launcher, ignore_errors=True)
    (launcher / "sdmc" / "switch").mkdir(parents=True)
    shutil.copyfile(RALLY, launcher / "sdmc" / "switch" / "rally.nro")
    subprocess.run([args.probe.resolve(), nro, launcher, "launcher"], check=True)

if __name__ == "__main__":
    main()

