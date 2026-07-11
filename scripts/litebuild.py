#!/usr/bin/env python3
import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path


OPEN_SBI_REV = "74434f255873d74e56cc50aa762d1caf24c099f8"
U_BOOT_REV = "127a42c7257a6ffbbd1575ed1cbaa8f5408a44b3"
LINUX_REV = "a607c8f744340ad2c2486d46e96b66df47caffba"

REPOS = {
    "opensbi": {
        "url": "https://github.com/riscv/opensbi.git",
        "mirror": "github.com.riscv.opensbi.git",
        "rev": OPEN_SBI_REV,
    },
    "u-boot": {
        "url": "https://source.denx.de/u-boot/u-boot.git",
        "mirror": "source.denx.de.u-boot.u-boot.git",
        "rev": U_BOOT_REV,
    },
    "linux": {
        "url": "https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git",
        "mirror": "git.kernel.org.pub.scm.linux.kernel.git.stable.linux.git",
        "rev": LINUX_REV,
    },
}


class Paths:
    def __init__(self):
        self.repo = Path(__file__).resolve().parents[1]
        self.ws = self.repo.parent
        self.fusdk = self.ws / "sifiveinc-2026.01"
        self.cache = Path(os.environ.get(
            "UNMATCHED_LITE_GIT_CACHE",
            self.fusdk / "build/downloads/git2",
        ))
        self.src = Path(os.environ.get("UNMATCHED_LITE_SRC", self.repo / "src"))
        self.out = Path(os.environ.get("UNMATCHED_LITE_OUT", self.repo / "out"))
        self.deploy = Path(os.environ.get("UNMATCHED_LITE_DEPLOY", self.repo / "deploy"))
        self.meta_sifive = self.fusdk / "meta-sifive"
        self.freedom_sdk = self.fusdk / "freedom-u-sdk"
        self.opensbi_src = self.src / "opensbi"
        self.uboot_src = self.src / "u-boot"
        self.linux_src = self.src / "linux"
        self.opensbi_out = self.out / "opensbi"
        self.uboot_out = self.out / "u-boot"
        self.linux_out = self.out / "linux"


class Toolchain:
    def __init__(self, args):
        self.cross_compile = args.cross_compile
        self.toolchain_bindirs = [Path(p) for p in args.toolchain_bindir]
        self.native_bindirs = [Path(p) for p in args.native_bindir]


def say(msg):
    print(msg, flush=True)


def run(cmd, cwd=None, env=None):
    say("$ " + " ".join(str(c) for c in cmd))
    subprocess.run([str(c) for c in cmd], cwd=cwd, env=env, check=True)


def touch(path):
    if path:
        p = Path(path)
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text("ok\n", encoding="utf-8")


def jobs():
    return os.environ.get("NINJAJOBS") or os.environ.get("JOBS") or str(os.cpu_count() or 4)


def prepend_path(env, path):
    if path.is_dir():
        env["PATH"] = str(path) + os.pathsep + env.get("PATH", "")


def build_env(paths, toolchain):
    env = os.environ.copy()

    for path in toolchain.toolchain_bindirs:
        prepend_path(env, path)
    for path in toolchain.native_bindirs:
        prepend_path(env, path)

    # Fallback convenience paths for running without a Meson cross file.
    for rel in (
        "build/tmp/sysroots-components/x86_64/gcc-cross-riscv64/usr/bin/riscv64-freedomusdk-linux",
        "build/tmp/sysroots-components/x86_64/binutils-cross-riscv64/usr/bin/riscv64-freedomusdk-linux",
        "build/tmp/sysroots-components/x86_64/bison-native/usr/bin",
        "build/tmp/sysroots-components/x86_64/flex-native/usr/bin",
        "build/tmp/sysroots-components/x86_64/m4-native/usr/bin",
        "build/tmp/sysroots-components/x86_64/dtc-native/usr/bin",
        "build/tmp/sysroots-components/x86_64/openssl-native/usr/bin",
    ):
        prepend_path(env, paths.fusdk / rel)

    if toolchain.cross_compile:
        env["CROSS_COMPILE"] = toolchain.cross_compile
    elif "CROSS_COMPILE" not in env:
        for prefix in (
            "riscv64-linux-gnu-",
            "riscv64-unknown-linux-gnu-",
            "riscv64-freedomusdk-linux-",
            "riscv64-unknown-elf-",
        ):
            if shutil.which(prefix + "gcc", path=env.get("PATH")):
                env["CROSS_COMPILE"] = prefix
                break

    env.setdefault("ARCH", "riscv")
    return env


def require_tools(names, env):
    missing = [name for name in names if not shutil.which(name, path=env.get("PATH"))]
    if missing:
        raise SystemExit("Missing host tools: " + ", ".join(missing))


def require_cross(env):
    prefix = env.get("CROSS_COMPILE")
    if not prefix:
        raise SystemExit(
            "No RISC-V cross compiler found. Set CROSS_COMPILE, for example "
            "CROSS_COMPILE=riscv64-linux-gnu-"
        )
    if not shutil.which(prefix + "gcc", path=env.get("PATH")):
        raise SystemExit(f"Cross compiler not found: {prefix}gcc")


def binutils_shim(paths, env):
    prefix = env.get("CROSS_COMPILE", "")
    if not prefix:
        return None

    # Yocto's relocated cross gcc can look for unprefixed as/ld. Build a
    # private -B directory with those names pointing at the prefixed binutils.
    shim = paths.out / "toolchain-shim" / Path(prefix.rstrip("-")).name
    shim.mkdir(parents=True, exist_ok=True)
    made = False
    for tool in ("as", "ld", "ld.bfd", "ar", "nm", "objcopy", "objdump", "ranlib", "readelf", "size", "strings", "strip"):
        real = shutil.which(prefix + tool, path=env.get("PATH"))
        if real:
            link = shim / tool
            if link.exists() or link.is_symlink():
                link.unlink()
            link.symlink_to(real)
            made = True
    return shim if made else None


def cross_cc(paths, env):
    prefix = env.get("CROSS_COMPILE", "")
    cc = prefix + "gcc"
    shim = binutils_shim(paths, env)
    if shim:
        cc += f" -B{shim}/"
    return cc


def source_from_cache(paths, key):
    mirror = paths.cache / REPOS[key]["mirror"]
    if mirror.exists():
        return mirror
    return REPOS[key]["url"]


def git_checkout(paths, key, dest):
    spec = REPOS[key]
    dest.parent.mkdir(parents=True, exist_ok=True)
    if not (dest / ".git").exists():
        run(["git", "clone", source_from_cache(paths, key), dest])
    run(["git", "reset", "--hard"], cwd=dest)
    run(["git", "clean", "-fdx"], cwd=dest)
    try:
        run(["git", "checkout", "--detach", spec["rev"]], cwd=dest)
    except subprocess.CalledProcessError:
        run(["git", "fetch", "--tags", "origin"], cwd=dest)
        run(["git", "checkout", "--detach", spec["rev"]], cwd=dest)


def apply_patch_once(src, patch):
    if not patch.exists():
        raise SystemExit(f"Missing patch: {patch}")
    reverse = subprocess.run(
        ["git", "apply", "--reverse", "--check", str(patch)],
        cwd=src,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    if reverse.returncode == 0:
        say(f"Patch already applied: {patch.name}")
        return
    run(["git", "apply", str(patch)], cwd=src)


def fetch(paths, only=None):
    selected = only or ("opensbi", "u-boot", "linux")
    for key in selected:
        if key == "opensbi":
            git_checkout(paths, key, paths.opensbi_src)
        elif key == "u-boot":
            git_checkout(paths, key, paths.uboot_src)
            apply_patch_once(
                paths.uboot_src,
                paths.meta_sifive / "recipes-bsp/u-boot/u-boot-sifive-2026.01/riscv64/0005-riscv-dts-Add-few-PMU-events.patch",
            )
        elif key == "linux":
            git_checkout(paths, key, paths.linux_src)
            apply_patch_once(
                paths.linux_src,
                paths.freedom_sdk / "recipes-kernel/linux/linux-sifive/freedom-u-sdk/0001-riscv-dts-sifive-unmatched-keep-leds-settings.patch",
            )
        else:
            raise SystemExit(f"Unknown source key: {key}")


def copy_artifact(src, dst):
    if not src.exists():
        raise SystemExit(f"Expected artifact was not built: {src}")
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)
    say(f"DEPLOY {dst}")


def build_opensbi(paths, env):
    fetch(paths, ("opensbi",))
    require_tools(["make"], env)
    require_cross(env)
    run([
        "make",
        "-C", paths.opensbi_src,
        f"O={paths.opensbi_out}",
        "-j", jobs(),
        "REPRODUCIBLE=y",
        f"CROSS_COMPILE={env['CROSS_COMPILE']}",
        f"CC={cross_cc(paths, env)}",
        "PLATFORM=generic",
        "FW_TEXT_START=0x80000000",
    ], env=env)

    fwdir = paths.opensbi_out / "platform/generic/firmware"
    for name in ("fw_dynamic.bin", "fw_dynamic.elf", "fw_jump.bin", "fw_jump.elf", "fw_payload.bin", "fw_payload.elf"):
        copy_artifact(fwdir / name, paths.deploy / name)


def build_uboot(paths, env):
    fetch(paths, ("u-boot",))
    if not (paths.deploy / "fw_dynamic.bin").exists():
        build_opensbi(paths, env)
    require_tools(["make", "bc", "bison", "flex"], env)
    require_cross(env)

    make_base = [
        "make",
        "-C", paths.uboot_src,
        f"O={paths.uboot_out}",
        "ARCH=riscv",
        f"CROSS_COMPILE={env['CROSS_COMPILE']}",
        f"CC={cross_cc(paths, env)}",
    ]
    run(make_base + ["sifive_unmatched_defconfig"], env=env)
    run(make_base + ["-j", jobs(), f"OPENSBI={paths.deploy / 'fw_dynamic.bin'}"], env=env)

    copy_artifact(paths.uboot_out / "spl/u-boot-spl.bin", paths.deploy / "u-boot-spl.bin")
    copy_artifact(paths.uboot_out / "u-boot.itb", paths.deploy / "u-boot.itb")


def build_linux(paths, env):
    fetch(paths, ("linux",))
    require_tools(["make", "bc", "bison", "flex", "openssl"], env)
    require_cross(env)

    paths.linux_out.mkdir(parents=True, exist_ok=True)
    defconfig = paths.meta_sifive / "recipes-kernel/linux/linux-sifive-6.18.3+git/unmatched/defconfig"
    if not defconfig.exists():
        raise SystemExit(f"Missing Linux defconfig: {defconfig}")
    if os.environ.get("UNMATCHED_LITE_KEEP_CONFIG") != "1":
        shutil.copy2(defconfig, paths.linux_out / ".config")

    make_base = [
        "make",
        "-C", paths.linux_src,
        f"O={paths.linux_out}",
        "ARCH=riscv",
        f"CROSS_COMPILE={env['CROSS_COMPILE']}",
        f"CC={cross_cc(paths, env)}",
    ]
    run(make_base + ["olddefconfig"], env=env)
    run(make_base + ["-j", jobs(), "Image.gz", "dtbs"], env=env)

    copy_artifact(paths.linux_out / "arch/riscv/boot/Image.gz", paths.deploy / "Image.gz")
    copy_artifact(
        paths.linux_out / "arch/riscv/boot/dts/sifive/hifive-unmatched-a00.dtb",
        paths.deploy / "hifive-unmatched-a00.dtb",
    )


def print_info(paths, env):
    print(f"repo:          {paths.repo}")
    print(f"source dir:    {paths.src}")
    print(f"build dir:     {paths.out}")
    print(f"deploy dir:    {paths.deploy}")
    print(f"git cache:     {paths.cache}")
    print(f"CROSS_COMPILE: {env.get('CROSS_COMPILE', '<not found>')}")
    print()
    print("Pinned versions:")
    print(f"  OpenSBI {OPEN_SBI_REV}")
    print(f"  U-Boot  {U_BOOT_REV}")
    print(f"  Linux   {LINUX_REV}")


def check(paths, env):
    print_info(paths, env)
    require_tools(["git", "make", "bc", "bison", "flex", "openssl"], env)
    require_cross(env)
    print()
    print("Tool check passed.")


def clean(paths):
    for path in (paths.out, paths.deploy):
        if path.exists():
            shutil.rmtree(path)
            say(f"removed {path}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=[
        "info", "check", "fetch", "opensbi", "u-boot", "linux", "bootchain", "clean", "stamp",
    ])
    parser.add_argument("--cross-compile", default="")
    parser.add_argument("--toolchain-bindir", action="append", default=[])
    parser.add_argument("--native-bindir", action="append", default=[])
    parser.add_argument("--stamp")
    args = parser.parse_args()

    paths = Paths()
    toolchain = Toolchain(args)
    env = build_env(paths, toolchain)

    if args.command == "info":
        print_info(paths, env)
    elif args.command == "check":
        check(paths, env)
    elif args.command == "fetch":
        fetch(paths)
    elif args.command == "opensbi":
        build_opensbi(paths, env)
    elif args.command == "u-boot":
        build_uboot(paths, env)
    elif args.command == "linux":
        build_linux(paths, env)
    elif args.command == "bootchain":
        build_opensbi(paths, env)
        build_uboot(paths, env)
        build_linux(paths, env)
    elif args.command == "clean":
        clean(paths)
    elif args.command == "stamp":
        pass

    touch(args.stamp)


if __name__ == "__main__":
    main()
