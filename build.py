import gzip
import os
import shutil
import subprocess
import sys


def run_cmd(cmd, cwd):
    result = subprocess.run(cmd, cwd=cwd, shell=True)
    if result.returncode != 0:
        raise RuntimeError(f"Command failed ({result.returncode}): {cmd}")


def build_web_assets(project_dir):
    data_www = os.path.join(project_dir, "data", "www")
    worker_dir = os.path.join(project_dir, "worker")
    ui_dir = os.path.join(project_dir, "ui")

    if os.path.exists(data_www):
        shutil.rmtree(data_www)

    print("Transpiling TS worker code")
    run_cmd("npm run build", cwd=worker_dir)

    worker_out = os.path.join(data_www, "worker")
    os.makedirs(worker_out, exist_ok=True)
    shutil.copy2(os.path.join(worker_dir, "dist_packed", "main.js"), os.path.join(worker_out, "worker.js"))
    shutil.copy2(os.path.join(worker_dir, "dist_packed", "oneline.js"), os.path.join(worker_out, "oneline.js"))
    shutil.copy2(os.path.join(worker_dir, "dist_packed", "oneline.wasm"), os.path.join(worker_out, "oneline.wasm"))

    print("Building React UI")
    run_cmd("npm run build", cwd=ui_dir)

    print("Gzip compressing static assets")
    for dirpath, _, filenames in os.walk(data_www):
        for filename in filenames:
            if filename.endswith(".gz"):
                continue
            src = os.path.join(dirpath, filename)
            dst = src + ".gz"
            with open(src, "rb") as f_in, gzip.open(dst, "wb", compresslevel=9) as f_out:
                shutil.copyfileobj(f_in, f_out)
            os.remove(src)

    # Mirror compressed assets into firmware/mural_esp8266/data/www so Arduino IDE
    # LittleFS uploader plugins can upload the exact same bundle.
    arduino_data_www = os.path.join(project_dir, "firmware", "mural_esp8266", "data", "www")
    if os.path.exists(arduino_data_www):
        shutil.rmtree(arduino_data_www)
    shutil.copytree(data_www, arduino_data_www)


if __name__ == "__main__":
    build_web_assets(os.path.dirname(os.path.abspath(__file__)))
else:
    from SCons.Script import COMMAND_LINE_TARGETS

    Import("env")
    skip_targets = {"nobuild", "clean", "compiledb", "idedata"}
    if os.environ.get("MURAL_SKIP_WEB_BUILD") == "1" or any(
        t in COMMAND_LINE_TARGETS for t in skip_targets
    ):
        print("Skipping web asset build")
    else:
        build_web_assets(env.subst("$PROJECT_DIR"))
