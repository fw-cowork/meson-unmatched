#!/usr/bin/env python3
import argparse
import gzip
import hashlib
import os
import shlex
import shutil
import stat
import subprocess
import tarfile
import tempfile
import urllib.request
from pathlib import Path

from fat import write_fat16_image


OPEN_SBI_REV = "74434f255873d74e56cc50aa762d1caf24c099f8"
U_BOOT_REV = "127a42c7257a6ffbbd1575ed1cbaa8f5408a44b3"
LINUX_REV = "a607c8f744340ad2c2486d46e96b66df47caffba"
BUSYBOX_VERSION = "1.37.0"
BUSYBOX_SHA256 = "3311dff32e746499f4df0d5df04d7eb396382d7e108bb9250e7b519b837043a4"
BUSYBOX_URL = f"https://busybox.net/downloads/busybox-{BUSYBOX_VERSION}.tar.bz2"

SECTOR_SIZE = 512
SPL_START = 34
SPL_END = 2081
UBOOT_START = 2082
UBOOT_END = 10273
BOOT_START = 16384
BOOT_END = 282623
ROOT_START = 286720
SPL_TYPE = "5B193300-FC78-40CD-8002-E86C45580B47"
UBOOT_TYPE = "2E54B353-1271-4842-806F-E436D6AF6985"
QEMU_BOOT_START = 2048
QEMU_BOOT_SECTORS = 262144
QEMU_CPUS = "8"
QEMU_MEMORY = "2G"

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
    def __init__(self, profile):
        self.repo = Path(__file__).resolve().parents[1]
        self.profile = profile
        cache = os.environ.get("UNMATCHED_LITE_GIT_CACHE")
        self.cache = Path(cache) if cache else None
        self.downloads = Path(os.environ.get("UNMATCHED_LITE_DOWNLOAD_DIR", self.repo / "downloads"))
        self.busybox_tarball = Path(os.environ.get(
            "UNMATCHED_LITE_BUSYBOX_TARBALL",
            self.downloads / f"busybox-{BUSYBOX_VERSION}.tar.bz2",
        ))
        self.src = Path(os.environ.get("UNMATCHED_LITE_SRC", self.repo / "src"))
        default_out = self.repo / "out"
        default_deploy = self.repo / "deploy"
        if profile == "qemu":
            default_out /= "qemu"
            default_deploy /= "qemu"
        self.out = Path(os.environ.get("UNMATCHED_LITE_OUT", default_out))
        self.deploy = Path(os.environ.get("UNMATCHED_LITE_DEPLOY", default_deploy))
        self.opensbi_src = self.src / "opensbi"
        self.uboot_src = self.src / "u-boot"
        self.linux_src = self.src / "linux"
        self.busybox_src = self.src / "busybox"
        self.opensbi_out = self.out / "opensbi"
        self.uboot_out = self.out / "u-boot"
        self.qemu_uboot_out = self.out / "u-boot-qemu"
        self.linux_out = self.out / "linux"
        self.busybox_out = self.out / "busybox"
        self.rootfs_tree = self.out / "rootfs"
        self.rootfs_image = self.deploy / "rootfs.ext4"
        self.rootfs_cpio = self.deploy / "rootfs.cpio.gz"
        self.sd_image = self.deploy / "unmatched-lite.img"
        self.qemu_image = self.deploy / "qemu-lite.img"
        self.qemu_fit = self.deploy / "fit.itb"
        self.unmatched_fit = self.deploy / "unmatched-fit.itb"
        self.unmatched_firmware_fit = self.deploy / "unmatched-firmware.itb"
        self.qemu_boot_script = self.deploy / "boot.scr"
        self.qemu_uboot_bin = self.deploy / "u-boot.bin"
        self.qemu_uboot = self.deploy / "u-boot-qemu.bin"
        self.linux_defconfig = self.repo / "configs/linux/unmatched_defconfig"
        self.rootfs_overlay = self.repo / "rootfs"
        self.uboot_patch = self.repo / "patches/u-boot/2026.01/0005-riscv-dts-Add-few-PMU-events.patch"
        self.uboot_patch2 = self.repo / "patches/u-boot/2026.01/0006-pcie-fu740-debug-trace.patch"
        self.uboot_patch3 = self.repo / "patches/u-boot/2026.01/0007-unmatched-tftp-and-opensbi-prints.patch"
        self.uboot_patch4 = self.repo / "patches/u-boot/2026.01/0008-unmatched-enable-spl-debug-logs.patch"
        self.linux_patch = self.repo / "patches/linux/6.18/0001-riscv-dts-sifive-unmatched-keep-leds-settings.patch"
        self.linux_patch2 = self.repo / "patches/linux/6.18/0002-pcie-fu740-debug-trace.patch"


class Toolchain:
    def __init__(self, args):
        self.cross_compile = args.cross_compile
        self.sysroot = args.sysroot
        self.toolchain_bindirs = [Path(p) for p in args.toolchain_bindir]
        self.native_bindirs = [Path(p) for p in args.native_bindir]
        self.native_sysroot_dirs = [Path(p) for p in args.native_sysroot]


def say(msg):
    print(msg, flush=True)


def run(cmd, cwd=None, env=None, quiet=False):
    say("$ " + " ".join(str(c) for c in cmd))
    subprocess.run(
        [str(c) for c in cmd],
        cwd=cwd,
        env=env,
        check=True,
        stdout=subprocess.DEVNULL if quiet else None,
    )


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


def native_flex_shim(paths, env):
    flex = shutil.which("flex", path=env.get("PATH"))
    m4 = shutil.which("m4", path=env.get("PATH"))
    if not flex or not m4:
        return

    flex_real = Path(flex).with_name("flex.real")
    if not flex_real.is_file():
        return

    # Yocto's flex wrapper assumes m4 is installed in the same component.
    shim_dir = paths.out / "native-tool-shim"
    shim_dir.mkdir(parents=True, exist_ok=True)
    shim = shim_dir / "flex"
    shim.write_text(
        "#!/bin/sh\n"
        f"export M4={shlex.quote(m4)}\n"
        f"exec {shlex.quote(str(flex_real))} \"$@\"\n",
        encoding="ascii",
    )
    shim.chmod(0o755)
    prepend_path(env, shim_dir)


def build_env(paths, toolchain):
    env = os.environ.copy()
    host_cflags = []
    host_ldflags = []

    for path in toolchain.toolchain_bindirs:
        prepend_path(env, path)
    for path in toolchain.native_bindirs:
        prepend_path(env, path)
    for sysroot in toolchain.native_sysroot_dirs:
        root = sysroot / "usr"
        include = root / "include"
        lib = root / "lib"
        pkgconfig = lib / "pkgconfig"
        if include.is_dir():
            host_cflags.append(f"-I{include}")
        if lib.is_dir():
            host_ldflags.append(f"-L{lib}")
            env["LD_LIBRARY_PATH"] = str(lib) + os.pathsep + env.get("LD_LIBRARY_PATH", "")
        if pkgconfig.is_dir():
            env["PKG_CONFIG_PATH"] = str(pkgconfig) + os.pathsep + env.get("PKG_CONFIG_PATH", "")

    if host_cflags:
        env["HOSTCFLAGS"] = (env.get("HOSTCFLAGS", "") + " " + " ".join(host_cflags)).strip()
    if host_ldflags:
        env["HOSTLDFLAGS"] = (env.get("HOSTLDFLAGS", "") + " " + " ".join(host_ldflags)).strip()

    if toolchain.cross_compile:
        env["CROSS_COMPILE"] = toolchain.cross_compile
    if toolchain.sysroot:
        env["UNMATCHED_LITE_SYSROOT"] = toolchain.sysroot
    env.setdefault("ARCH", "riscv")
    native_flex_shim(paths, env)
    return env


def require_tools(names, env):
    missing = [name for name in names if not shutil.which(name, path=env.get("PATH"))]
    if missing:
        raise SystemExit("Missing host tools: " + ", ".join(missing))


def require_cross(env):
    prefix = env.get("CROSS_COMPILE")
    if not prefix:
        raise SystemExit(
            "No RISC-V cross compiler configured. Run ./toolchain.sh setup or "
            "provide an explicit Meson cross file."
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
    sysroot = env.get("UNMATCHED_LITE_SYSROOT")
    if sysroot:
        cc += f" --sysroot={sysroot}"
    return cc


def source_from_cache(paths, key):
    if paths.cache:
        mirror = paths.cache / REPOS[key]["mirror"]
        if mirror.exists():
            return mirror
    return REPOS[key]["url"]


def git_checkout(paths, key, dest, dev=False):
    spec = REPOS[key]
    dest.parent.mkdir(parents=True, exist_ok=True)
    has_checkout = (dest / ".git").exists()
    if dev and has_checkout:
        revision = subprocess.run(
            ["git", "rev-parse", "HEAD"],
            cwd=dest,
            check=True,
            capture_output=True,
            text=True,
        ).stdout.strip()
        say(f"DEV preserve {key} source at {revision[:12]}: {dest}")
        if revision != spec["rev"]:
            say(f"DEV warning {key} source differs from pinned {spec['rev'][:12]}")
        return
    if not has_checkout:
        run(["git", "clone", source_from_cache(paths, key), dest])
    run(["git", "reset", "--hard"], cwd=dest)
    run(["git", "clean", "-fdx"], cwd=dest)
    try:
        run(["git", "checkout", "--detach", spec["rev"]], cwd=dest)
    except subprocess.CalledProcessError:
        run(["git", "fetch", "--tags", "origin"], cwd=dest)
        run(["git", "checkout", "--detach", spec["rev"]], cwd=dest)


def _sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _extract_tarball(tarball, destination):
    root = destination.resolve()
    with tarfile.open(tarball, "r:bz2") as archive:
        members = archive.getmembers()
        for member in members:
            target = (root / member.name).resolve()
            if target != root and root not in target.parents:
                raise SystemExit(f"Unsafe path in BusyBox archive: {member.name}")
        archive.extractall(root)


def fetch_busybox(paths):
    marker = paths.busybox_src / ".unmatched-busybox-version"
    if paths.busybox_src.joinpath("Makefile").exists() and marker.exists():
        if marker.read_text(encoding="ascii").strip() == BUSYBOX_VERSION:
            return

    tarball = paths.busybox_tarball
    if not tarball.exists():
        tarball.parent.mkdir(parents=True, exist_ok=True)
        temporary = tarball.with_suffix(tarball.suffix + ".tmp")
        say(f"DOWNLOAD {BUSYBOX_URL}")
        try:
            urllib.request.urlretrieve(BUSYBOX_URL, temporary)
            temporary.replace(tarball)
        except Exception as exc:
            temporary.unlink(missing_ok=True)
            raise SystemExit(f"Unable to download BusyBox: {exc}") from exc

    actual = _sha256(tarball)
    if actual != BUSYBOX_SHA256:
        raise SystemExit(
            f"BusyBox checksum mismatch: {tarball}\n"
            f"expected {BUSYBOX_SHA256}, got {actual}"
        )

    paths.src.mkdir(parents=True, exist_ok=True)
    temporary_dir = Path(tempfile.mkdtemp(prefix=".busybox-", dir=paths.src))
    try:
        _extract_tarball(tarball, temporary_dir)
        extracted = temporary_dir / f"busybox-{BUSYBOX_VERSION}"
        if not extracted.joinpath("Makefile").exists():
            raise SystemExit(f"BusyBox archive has no expected source directory: {extracted}")
        if paths.busybox_src.exists():
            shutil.rmtree(paths.busybox_src)
        shutil.move(str(extracted), paths.busybox_src)
        (paths.busybox_src / ".unmatched-busybox-version").write_text(
            f"{BUSYBOX_VERSION}\n", encoding="ascii"
        )
    finally:
        shutil.rmtree(temporary_dir, ignore_errors=True)


def _set_config_option(path, name, value):
    enabled = f"{name}={value}"
    disabled = f"# {name} is not set"
    replacement = disabled if value == "n" else enabled
    lines = path.read_text(encoding="utf-8").splitlines()
    for index, line in enumerate(lines):
        if line.startswith(f"{name}=") or line == disabled:
            lines[index] = replacement
            path.write_text("\n".join(lines) + "\n", encoding="utf-8")
            return
    lines.append(replacement)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def _parse_size(value):
    value = value.strip().upper()
    units = {"K": 1024, "M": 1024**2, "G": 1024**3}
    for suffix, multiplier in units.items():
        if value.endswith(suffix):
            return int(value[:-1]) * multiplier
    return int(value)


def _busybox_make_base(paths, env):
    return [
        "make",
        "-C", paths.busybox_src,
        f"O={paths.busybox_out}",
        "ARCH=riscv",
        f"CROSS_COMPILE={env['CROSS_COMPILE']}",
        f"CC={cross_cc(paths, env)}",
    ]


def build_busybox(paths, env):
    fetch_busybox(paths)
    require_tools(["make"], env)
    require_cross(env)
    paths.busybox_out.mkdir(parents=True, exist_ok=True)
    make_base = _busybox_make_base(paths, env)
    config = paths.busybox_out / ".config"
    if not config.exists():
        run(make_base + ["defconfig"], env=env)
    for name, value in (
        ("CONFIG_STATIC", "y"),
        ("CONFIG_FEATURE_PREFER_APPLETS", "y"),
        ("CONFIG_FEATURE_SH_STANDALONE", "y"),
        ("CONFIG_SHA1_HWACCEL", "n"),
        ("CONFIG_SHA256_HWACCEL", "n"),
        ("CONFIG_TC", "n"),
    ):
        _set_config_option(config, name, value)
    run(make_base + ["-j", jobs(), "busybox"], env=env)
    copy_artifact(paths.busybox_out / "busybox", paths.deploy / "busybox")

    if paths.rootfs_tree.exists():
        shutil.rmtree(paths.rootfs_tree)
    paths.rootfs_tree.mkdir(parents=True, exist_ok=True)
    run(make_base + [f"CONFIG_PREFIX={paths.rootfs_tree}", "install"], env=env)
    shutil.copytree(paths.rootfs_overlay, paths.rootfs_tree, dirs_exist_ok=True)
    for directory in ("dev", "dev/pts", "dev/shm", "proc", "sys", "run", "tmp"):
        (paths.rootfs_tree / directory).mkdir(parents=True, exist_ok=True)
    init_script = paths.rootfs_tree / "etc/init.d/rcS"
    init_script.chmod(init_script.stat().st_mode | 0o111)
    if paths.profile == "qemu":
        init = paths.rootfs_tree / "init"
        if init.exists() or init.is_symlink():
            init.unlink()
        init.symlink_to("sbin/init")


def build_rootfs(paths, env):
    build_busybox(paths, env)
    paths.deploy.mkdir(parents=True, exist_ok=True)
    if paths.profile == "qemu":
        write_cpio_newc_gz(paths.rootfs_tree, paths.rootfs_cpio)
        say(f"DEPLOY {paths.rootfs_cpio}")
        return

    if paths.rootfs_image.exists():
        paths.rootfs_image.unlink()
    size = _parse_size(os.environ.get("UNMATCHED_LITE_ROOTFS_SIZE", "64M"))
    if size < 1024 or size % 1024:
        raise SystemExit("UNMATCHED_LITE_ROOTFS_SIZE must be at least 1K and K-aligned")
    run([
        "mke2fs",
        "-q",
        "-t", "ext4",
        "-F",
        "-L", "UNMATCHED",
        "-d", paths.rootfs_tree,
        paths.rootfs_image,
        f"{size // 1024}K",
    ], env=env)
    _set_ext4_root_ownership(paths.rootfs_tree, paths.rootfs_image, env)
    say(f"DEPLOY {paths.rootfs_image}")


def _set_ext4_root_ownership(rootfs_tree, rootfs_image, env):
    commands = ["set_inode_field / uid 0", "set_inode_field / gid 0"]
    for parent, directories, files in os.walk(rootfs_tree, followlinks=False):
        parent_path = Path(parent)
        for name in directories + files:
            relative = (parent_path / name).relative_to(rootfs_tree)
            path = "/" + relative.as_posix()
            commands.append(f"set_inode_field {path} uid 0")
            commands.append(f"set_inode_field {path} gid 0")

    with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", delete=False) as command_file:
        command_file.write("\n".join(commands) + "\n")
        command_path = Path(command_file.name)
    try:
        run(["debugfs", "-w", "-f", command_path, rootfs_image], env=env, quiet=True)
    finally:
        command_path.unlink(missing_ok=True)


def _write_cpio_entry(archive, name, mode, data, inode):
    encoded_name = name.encode("ascii") + b"\0"
    header = (
        "070701"
        f"{inode:08x}{mode:08x}{0:08x}{0:08x}{1:08x}{0:08x}"
        f"{len(data):08x}{0:08x}{0:08x}{0:08x}{0:08x}{len(encoded_name):08x}{0:08x}"
    ).encode("ascii")
    archive.write(header)
    archive.write(encoded_name)
    archive.write(b"\0" * (-(len(header) + len(encoded_name)) % 4))
    archive.write(data)
    archive.write(b"\0" * ((-len(data)) % 4))


def write_cpio_newc_gz(tree, output):
    output.parent.mkdir(parents=True, exist_ok=True)
    entries = [tree]
    entries.extend(sorted(tree.rglob("*"), key=lambda path: path.relative_to(tree).as_posix()))
    with output.open("wb") as raw:
        with gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=0) as archive:
            for inode, source in enumerate(entries, start=1):
                name = "." if source == tree else source.relative_to(tree).as_posix()
                source_stat = source.lstat()
                if stat.S_ISREG(source_stat.st_mode):
                    data = source.read_bytes()
                elif stat.S_ISLNK(source_stat.st_mode):
                    data = os.readlink(source).encode("utf-8")
                elif stat.S_ISDIR(source_stat.st_mode):
                    data = b""
                else:
                    raise SystemExit(f"Unsupported rootfs entry for CPIO: {source}")
                _write_cpio_entry(archive, name, source_stat.st_mode, data, inode)
            _write_cpio_entry(archive, "TRAILER!!!", 0, b"", len(entries) + 1)


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


def fetch(paths, only=None, dev=False):
    selected = only or ("opensbi", "u-boot", "linux")
    for key in selected:
        if key == "opensbi":
            git_checkout(paths, key, paths.opensbi_src, dev=dev)
        elif key == "u-boot":
            git_checkout(paths, key, paths.uboot_src, dev=dev)
            if paths.profile == "unmatched":
                apply_patch_once(paths.uboot_src, paths.uboot_patch)
                if paths.uboot_patch2.exists():
                    apply_patch_once(paths.uboot_src, paths.uboot_patch2)
                if paths.uboot_patch3.exists():
                    apply_patch_once(paths.uboot_src, paths.uboot_patch3)
                apply_patch_once(paths.uboot_src, paths.uboot_patch4)
        elif key == "linux":
            git_checkout(paths, key, paths.linux_src, dev=dev)
            if paths.profile == "unmatched":
                apply_patch_once(paths.linux_src, paths.linux_patch)
            if paths.linux_patch2.exists():
                apply_patch_once(paths.linux_src, paths.linux_patch2)
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


def _uboot_make_base(paths, env, output):
    return [
        "make",
        "-C", paths.uboot_src,
        f"O={output}",
        "ARCH=riscv",
        f"CROSS_COMPILE={env['CROSS_COMPILE']}",
        f"CC={cross_cc(paths, env)}",
    ]


def build_uboot(paths, env, dev=False):
    fetch(paths, ("u-boot",), dev=dev)
    require_tools(["make", "bc", "bison", "flex"], env)
    require_cross(env)

    make_base = _uboot_make_base(paths, env, paths.uboot_out)
    if paths.profile == "qemu":
        run(make_base + ["qemu-riscv64_smode_defconfig"], env=env)
        _set_config_option(paths.uboot_out / ".config", "CONFIG_BOOTDELAY", "5")
        run(make_base + ["olddefconfig"], env=env)
        run(make_base + ["-j", jobs()], env=env)
        copy_artifact(paths.uboot_out / "u-boot.bin", paths.qemu_uboot_bin)
        return

    if not (paths.deploy / "fw_dynamic.bin").exists():
        build_opensbi(paths, env)

    config_dot = paths.uboot_out / ".config"
    if dev and config_dot.exists():
        say(f"DEV preserve existing U-Boot .config: {config_dot}")
        run(make_base + ["olddefconfig"], env=env)
    else:
        run(make_base + ["sifive_unmatched_defconfig"], env=env)
        _set_config_option(config_dot, "CONFIG_BOOTDELAY", "5")
        run(make_base + ["olddefconfig"], env=env)

    run(make_base + ["-j", jobs(), f"OPENSBI={paths.deploy / 'fw_dynamic.bin'}"], env=env)

    copy_artifact(paths.uboot_out / "spl/u-boot-spl.bin", paths.deploy / "u-boot-spl.bin")
    copy_artifact(paths.uboot_out / "u-boot.itb", paths.deploy / "u-boot.itb")
    build_unmatched_firmware_fit(paths, env)


def build_qemu_uboot(paths, env):
    fetch(paths, ("u-boot",))
    require_tools(["make", "bc", "bison", "flex"], env)
    require_cross(env)

    make_base = _uboot_make_base(paths, env, paths.qemu_uboot_out)
    run(make_base + ["qemu-riscv64_smode_defconfig"], env=env)
    _set_config_option(paths.qemu_uboot_out / ".config", "CONFIG_BOOTDELAY", "-1")
    run(make_base + ["olddefconfig"], env=env)
    run(make_base + ["-j", jobs()], env=env)
    copy_artifact(paths.qemu_uboot_out / "u-boot.bin", paths.qemu_uboot)


def build_linux(paths, env, dev=False):
    fetch(paths, ("linux",), dev=dev)
    require_tools(["make", "bc", "bison", "flex", "openssl"], env)
    require_cross(env)

    paths.linux_out.mkdir(parents=True, exist_ok=True)
    make_base = [
        "make",
        "-C", paths.linux_src,
        f"O={paths.linux_out}",
        "ARCH=riscv",
        f"CROSS_COMPILE={env['CROSS_COMPILE']}",
        f"CC={cross_cc(paths, env)}",
    ]
    if paths.profile == "qemu":
        if os.environ.get("UNMATCHED_LITE_KEEP_CONFIG") != "1":
            run(make_base + ["defconfig"], env=env)
            dot_config = paths.linux_out / ".config"
            with dot_config.open("a") as f:
                f.write("CONFIG_PCIE_ENUM_DEBUG=y\n")
            run(make_base + ["olddefconfig"], env=env)
        else:
            run(make_base + ["olddefconfig"], env=env)
        run(make_base + ["-j", jobs(), "Image.gz"], env=env)
        copy_artifact(paths.linux_out / "arch/riscv/boot/Image.gz", paths.deploy / "Image.gz")
        return

    defconfig = paths.linux_defconfig
    if not defconfig.exists():
        raise SystemExit(f"Missing Linux defconfig: {defconfig}")
    if not paths.linux_out.joinpath(".config").exists() or (
        not dev and os.environ.get("UNMATCHED_LITE_KEEP_CONFIG") != "1"
    ):
        shutil.copy2(defconfig, paths.linux_out / ".config")
    run(make_base + ["olddefconfig"], env=env)
    run(make_base + ["-j", jobs(), "Image.gz", "dtbs"], env=env)

    copy_artifact(paths.linux_out / "arch/riscv/boot/Image.gz", paths.deploy / "Image.gz")
    copy_artifact(
        paths.linux_out / "arch/riscv/boot/dts/sifive/hifive-unmatched-a00.dtb",
        paths.deploy / "hifive-unmatched-a00.dtb",
    )
    if paths.uboot_out.joinpath("tools/mkimage").is_file():
        build_unmatched_fit(paths, env)
    else:
        say("SKIP unmatched FIT: build U-Boot first, then run the fit target")


def _write_region(image, source, offset, size):
    if source.stat().st_size > size:
        raise SystemExit(f"{source} is larger than its image partition")
    with image.open("r+b") as destination, source.open("rb") as data:
        destination.seek(offset)
        shutil.copyfileobj(data, destination)


def _extlinux_conf():
    return """TIMEOUT 5
DEFAULT unmatched

LABEL unmatched
  KERNEL /Image.gz
  FDT /hifive-unmatched-a00.dtb
  APPEND root=/dev/mmcblk0p4 rw rootwait console=ttySIF0,115200 earlycon=sbi
"""


def _unmatched_fit_its(paths):
    return f"""/dts-v1/;

/ {{
  description = "HiFive Unmatched Linux boot image";
  #address-cells = <2>;

  images {{
    kernel {{
      description = "Linux kernel";
      data = /incbin/("{paths.deploy / 'Image.gz'}");
      type = "kernel";
      arch = "riscv";
      os = "linux";
      compression = "gzip";
      load = <0x0 0x80200000>;
      entry = <0x0 0x80200000>;
      hash-1 {{
        algo = "sha256";
      }};
    }};

    fdt {{
      description = "HiFive Unmatched device tree";
      data = /incbin/("{paths.deploy / 'hifive-unmatched-a00.dtb'}");
      type = "flat_dt";
      arch = "riscv";
      compression = "none";
      hash-1 {{
        algo = "sha256";
      }};
    }};
  }};

  configurations {{
    default = "unmatched";
    unmatched {{
      description = "HiFive Unmatched";
      kernel = "kernel";
      fdt = "fdt";
    }};
  }};
}};
"""


def build_unmatched_fit(paths, env):
    if paths.profile != "unmatched":
        raise SystemExit("fit is only available for the unmatched profile")

    mkimage = paths.uboot_out / "tools/mkimage"
    if not mkimage.is_file():
        raise SystemExit("U-Boot mkimage is missing; build U-Boot before the FIT")

    for artifact in (paths.deploy / "Image.gz", paths.deploy / "hifive-unmatched-a00.dtb"):
        if not artifact.is_file():
            raise SystemExit(f"Expected FIT input was not built: {artifact}")

    its = paths.out / "unmatched-fit.its"
    its.write_text(_unmatched_fit_its(paths), encoding="ascii")
    run([mkimage, "-f", its, paths.unmatched_fit], env=env)
    say(f"DEPLOY {paths.unmatched_fit}")


def _unmatched_firmware_update_cmd():
    return """setenv firmware_addr_r 0x84000000 &&
setenv spl_update_addr_r 0x82000000 &&
setenv uboot_update_addr_r 0x82400000 &&
setenv firmware_verify_addr_r 0x83000000 &&
setenv firmware_mmcdev 0 &&
setenv firmware_spl_part 1 &&
setenv firmware_uboot_part 2 &&
setenv verify yes &&
imxtract ${firmware_addr_r} spl ${spl_update_addr_r} &&
setenv spl_update_size ${filesize} &&
setexpr spl_update_blocks ${filesize} + 0x1ff &&
setexpr spl_update_blocks ${spl_update_blocks} / 0x200 &&
imxtract ${firmware_addr_r} uboot ${uboot_update_addr_r} &&
setenv uboot_update_size ${filesize} &&
setexpr uboot_update_blocks ${filesize} + 0x1ff &&
setexpr uboot_update_blocks ${uboot_update_blocks} / 0x200 &&
mmc dev ${firmware_mmcdev} &&
part type mmc ${firmware_mmcdev}:${firmware_spl_part} spl_partition_type &&
part type mmc ${firmware_mmcdev}:${firmware_uboot_part} uboot_partition_type &&
itest.s ${spl_partition_type} == 5b193300-fc78-40cd-8002-e86c45580b47 &&
itest.s ${uboot_partition_type} == 2e54b353-1271-4842-806f-e436d6af6985 &&
part start mmc ${firmware_mmcdev} ${firmware_spl_part} spl_update_start &&
part size mmc ${firmware_mmcdev} ${firmware_spl_part} spl_partition_blocks &&
part start mmc ${firmware_mmcdev} ${firmware_uboot_part} uboot_update_start &&
part size mmc ${firmware_mmcdev} ${firmware_uboot_part} uboot_partition_blocks &&
itest ${spl_update_blocks} -le ${spl_partition_blocks} &&
itest ${uboot_update_blocks} -le ${uboot_partition_blocks} &&
echo Writing U-Boot and OpenSBI to MMC ${firmware_mmcdev}:${firmware_uboot_part}... &&
mmc write ${uboot_update_addr_r} ${uboot_update_start} ${uboot_update_blocks} &&
mmc read ${firmware_verify_addr_r} ${uboot_update_start} ${uboot_update_blocks} &&
cmp.b ${uboot_update_addr_r} ${firmware_verify_addr_r} ${uboot_update_size} &&
echo Writing SPL to MMC ${firmware_mmcdev}:${firmware_spl_part}... &&
mmc write ${spl_update_addr_r} ${spl_update_start} ${spl_update_blocks} &&
mmc read ${firmware_verify_addr_r} ${spl_update_start} ${spl_update_blocks} &&
cmp.b ${spl_update_addr_r} ${firmware_verify_addr_r} ${spl_update_size} &&
echo Firmware update verified - reset the board
"""


def _unmatched_firmware_fit_its(spl, uboot, update_script):
    return f"""/dts-v1/;

/ {{
  description = "HiFive Unmatched SPL, OpenSBI, and U-Boot update";
  #address-cells = <2>;

  images {{
    spl {{
      description = "U-Boot SPL";
      data = /incbin/("{spl}");
      type = "firmware";
      arch = "riscv";
      os = "u-boot";
      compression = "none";
      hash-1 {{
        algo = "sha256";
      }};
    }};

    uboot {{
      description = "OpenSBI and U-Boot FIT";
      data = /incbin/("{uboot}");
      type = "firmware";
      arch = "riscv";
      os = "u-boot";
      compression = "none";
      hash-1 {{
        algo = "sha256";
      }};
    }};

    update {{
      description = "Hash-checked MMC firmware update script";
      data = /incbin/("{update_script}");
      type = "script";
      arch = "riscv";
      compression = "none";
      hash-1 {{
        algo = "sha256";
      }};
    }};
  }};

  configurations {{
    default = "unmatched";
    unmatched {{
      description = "HiFive Unmatched firmware update";
      firmware = "uboot";
      loadables = "spl";
      script = "update";
    }};
  }};
}};
"""


def build_unmatched_firmware_fit(paths, env):
    if paths.profile != "unmatched":
        raise SystemExit("firmware-fit is only available for the unmatched profile")

    mkimage = paths.uboot_out / "tools/mkimage"
    if not mkimage.is_file():
        raise SystemExit("U-Boot mkimage is missing; build U-Boot before the firmware FIT")

    artifacts = (
        (paths.deploy / "u-boot-spl.bin", (SPL_END - SPL_START + 1) * SECTOR_SIZE),
        (paths.deploy / "u-boot.itb", (UBOOT_END - UBOOT_START + 1) * SECTOR_SIZE),
    )
    for artifact, partition_size in artifacts:
        if not artifact.is_file():
            raise SystemExit(f"Expected firmware FIT input was not built: {artifact}")
        if artifact.stat().st_size > partition_size:
            raise SystemExit(f"{artifact} is larger than its SD image partition")

    padded_artifacts = []
    for artifact, _ in artifacts:
        padded = paths.out / f"{artifact.name}.update"
        data = artifact.read_bytes()
        padded.write_bytes(data + bytes(-len(data) % SECTOR_SIZE))
        padded_artifacts.append(padded)

    update_script = paths.out / "unmatched-firmware-update.cmd"
    update_script.write_text(_unmatched_firmware_update_cmd(), encoding="ascii")
    its = paths.out / "unmatched-firmware.its"
    its.write_text(
        _unmatched_firmware_fit_its(*padded_artifacts, update_script),
        encoding="ascii",
    )
    run([mkimage, "-f", its, paths.unmatched_firmware_fit], env=env)
    say(f"DEPLOY {paths.unmatched_firmware_fit}")


def _qemu_boot_cmd():
    return """setenv fit_addr 0x84000000
fatload virtio 0:1 ${fit_addr} fit.itb
setenv bootargs console=ttyS0 earlycon=sbi loglevel=8
bootm ${fit_addr}#qemu
"""


def _qemu_fit_its(paths, dtb):
    return f"""/dts-v1/;

/ {{
  description = "QEMU RISC-V boot image";
  #address-cells = <2>;

  images {{
    kernel {{
      description = "Linux kernel";
      data = /incbin/("{paths.deploy / 'Image.gz'}");
      type = "kernel";
      arch = "riscv";
      os = "linux";
      compression = "gzip";
      load = <0x0 0x80200000>;
      entry = <0x0 0x80200000>;
      hash-1 {{
        algo = "sha256";
      }};
    }};

    fdt {{
      description = "QEMU virt device tree";
      data = /incbin/("{dtb}");
      type = "flat_dt";
      arch = "riscv";
      compression = "none";
      hash-1 {{
        algo = "sha256";
      }};
    }};

    ramdisk {{
      description = "BusyBox rootfs";
      data = /incbin/("{paths.rootfs_cpio}");
      type = "ramdisk";
      arch = "riscv";
      os = "linux";
      compression = "none";
      hash-1 {{
        algo = "sha256";
      }};
    }};
  }};

  configurations {{
    default = "qemu";
    qemu {{
      description = "QEMU virt";
      kernel = "kernel";
      fdt = "fdt";
      ramdisk = "ramdisk";
    }};
  }};
}};
"""


def build_qemu_fit(paths, env):
    qemu = os.environ.get("QEMU_SYSTEM_RISCV64", "qemu-system-riscv64")
    qemu_path = shutil.which(qemu, path=env.get("PATH"))
    if not qemu_path:
        raise SystemExit(f"QEMU system emulator not found: {qemu}")

    mkimage = paths.uboot_out / "tools/mkimage"
    if not mkimage.is_file():
        raise SystemExit(f"Expected U-Boot mkimage was not built: {mkimage}")

    dtb = paths.out / "qemu-virt.dtb"
    if dtb.exists():
        dtb.unlink()
    run([
        qemu_path,
        "-machine", f"virt,dumpdtb={dtb}",
        "-smp", QEMU_CPUS,
        "-m", QEMU_MEMORY,
    ], env=env)

    its = paths.out / "fit.its"
    its.write_text(_qemu_fit_its(paths, dtb), encoding="ascii")
    run([mkimage, "-f", its, paths.qemu_fit], env=env)
    say(f"DEPLOY {paths.qemu_fit}")

    boot_cmd = paths.out / "boot.cmd"
    boot_cmd.write_text(_qemu_boot_cmd(), encoding="ascii")
    run([
        mkimage,
        "-A", "riscv",
        "-T", "script",
        "-C", "none",
        "-n", "QEMU FIT boot",
        "-d", boot_cmd,
        paths.qemu_boot_script,
    ], env=env)
    say(f"DEPLOY {paths.qemu_boot_script}")


def build_sd_image(paths, env):
    if paths.profile != "unmatched":
        raise SystemExit("sd-image is only available for the unmatched profile")
    build_opensbi(paths, env)
    build_uboot(paths, env)
    build_linux(paths, env)
    build_rootfs(paths, env)

    boot_size = (BOOT_END - BOOT_START + 1) * SECTOR_SIZE
    boot_image = paths.out / "boot.vfat"
    extlinux = paths.out / "extlinux.conf"
    extlinux.write_text(_extlinux_conf(), encoding="ascii")
    write_fat16_image(
        boot_image,
        size_bytes=boot_size,
        hidden_sectors=BOOT_START,
        label="BOOT",
        files={
            "Image.gz": paths.deploy / "Image.gz",
            "hifive-unmatched-a00.dtb": paths.deploy / "hifive-unmatched-a00.dtb",
            "extlinux.conf": extlinux,
        },
    )

    root_sectors = (paths.rootfs_image.stat().st_size + SECTOR_SIZE - 1) // SECTOR_SIZE
    root_end = ROOT_START + root_sectors - 1
    total_sectors = root_end + 34
    paths.deploy.mkdir(parents=True, exist_ok=True)
    if paths.sd_image.exists():
        paths.sd_image.unlink()
    with paths.sd_image.open("wb") as image:
        image.truncate(total_sectors * SECTOR_SIZE)

    run(["sgdisk", "--zap-all", str(paths.sd_image)], env=env)
    run([
        "sgdisk",
        "--set-alignment=1",
        f"--new=1:{SPL_START}:{SPL_END}",
        f"--typecode=1:{SPL_TYPE}",
        "--change-name=1:SPL",
        f"--new=2:{UBOOT_START}:{UBOOT_END}",
        f"--typecode=2:{UBOOT_TYPE}",
        "--change-name=2:U-Boot",
        f"--new=3:{BOOT_START}:{BOOT_END}",
        "--typecode=3:EF00",
        "--change-name=3:boot",
        f"--new=4:{ROOT_START}:{root_end}",
        "--typecode=4:8300",
        "--change-name=4:rootfs",
        str(paths.sd_image),
    ], env=env)

    _write_region(
        paths.sd_image,
        paths.deploy / "u-boot-spl.bin",
        SPL_START * SECTOR_SIZE,
        (SPL_END - SPL_START + 1) * SECTOR_SIZE,
    )
    _write_region(
        paths.sd_image,
        paths.deploy / "u-boot.itb",
        UBOOT_START * SECTOR_SIZE,
        (UBOOT_END - UBOOT_START + 1) * SECTOR_SIZE,
    )
    _write_region(paths.sd_image, boot_image, BOOT_START * SECTOR_SIZE, boot_size)
    _write_region(
        paths.sd_image,
        paths.rootfs_image,
        ROOT_START * SECTOR_SIZE,
        root_sectors * SECTOR_SIZE,
    )
    run(["sgdisk", "--verify", str(paths.sd_image)], env=env)
    say(f"DEPLOY {paths.sd_image}")


def build_qemu_image(paths, env):
    if paths.profile != "qemu":
        raise SystemExit("qemu-image requires --profile qemu")
    build_opensbi(paths, env)
    build_uboot(paths, env)
    build_linux(paths, env)
    build_rootfs(paths, env)
    build_qemu_fit(paths, env)

    boot_size = QEMU_BOOT_SECTORS * SECTOR_SIZE
    boot_image = paths.out / "boot.vfat"
    write_fat16_image(
        boot_image,
        size_bytes=boot_size,
        hidden_sectors=QEMU_BOOT_START,
        label="BOOT",
        files={
            "fit.itb": paths.qemu_fit,
            "boot.scr": paths.qemu_boot_script,
        },
    )

    boot_end = QEMU_BOOT_START + QEMU_BOOT_SECTORS - 1
    total_sectors = boot_end + 34
    paths.deploy.mkdir(parents=True, exist_ok=True)
    if paths.qemu_image.exists():
        paths.qemu_image.unlink()
    with paths.qemu_image.open("wb") as image:
        image.truncate(total_sectors * SECTOR_SIZE)

    run(["sgdisk", "--zap-all", str(paths.qemu_image)], env=env)
    run([
        "sgdisk",
        "--set-alignment=1",
        f"--new=1:{QEMU_BOOT_START}:{boot_end}",
        "--typecode=1:EF00",
        "--change-name=1:boot",
        str(paths.qemu_image),
    ], env=env)
    _write_region(paths.qemu_image, boot_image, QEMU_BOOT_START * SECTOR_SIZE, boot_size)
    run(["sgdisk", "--verify", str(paths.qemu_image)], env=env)
    say(f"DEPLOY {paths.qemu_image}")


def print_info(paths, env):
    print(f"profile:       {paths.profile}")
    print(f"repo:          {paths.repo}")
    print(f"source dir:    {paths.src}")
    print(f"build dir:     {paths.out}")
    print(f"deploy dir:    {paths.deploy}")
    print(f"git cache:     {paths.cache or '<network or existing checkout>'}")
    print(f"BusyBox:       {BUSYBOX_VERSION}")
    rootfs = paths.rootfs_cpio if paths.profile == "qemu" else paths.rootfs_image
    print(f"rootfs image:  {rootfs}")
    print(f"CROSS_COMPILE: {env.get('CROSS_COMPILE', '<not found>')}")
    print()
    print("Pinned versions:")
    print(f"  OpenSBI {OPEN_SBI_REV}")
    print(f"  U-Boot  {U_BOOT_REV}")
    print(f"  Linux   {LINUX_REV}")


def check(paths, env):
    print_info(paths, env)
    require_tools(["git", "make", "bc", "bison", "flex", "mke2fs", "debugfs", "sgdisk", "openssl"], env)
    require_cross(env)
    inputs = [paths.rootfs_overlay]
    if paths.profile == "unmatched":
        inputs += [paths.uboot_patch, paths.uboot_patch4, paths.linux_patch, paths.linux_defconfig]
        if paths.uboot_patch2.exists():
            inputs.append(paths.uboot_patch2)
        if paths.uboot_patch3.exists():
            inputs.append(paths.uboot_patch3)
        if paths.linux_patch2.exists():
            inputs.append(paths.linux_patch2)
    for path in inputs:
        if not path.exists():
            raise SystemExit(f"Missing repository input: {path}")
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
        "info", "check", "fetch", "opensbi", "u-boot", "qemu-u-boot", "linux", "busybox", "rootfs",
        "fit", "firmware-fit", "bootchain", "sd-image", "qemu-image", "dev-linux", "dev-uboot", "clean", "stamp",
    ])
    parser.add_argument("--profile", choices=["unmatched", "qemu"], default="unmatched")
    parser.add_argument("--cross-compile", default="")
    parser.add_argument("--sysroot", default="")
    parser.add_argument("--toolchain-bindir", action="append", default=[])
    parser.add_argument("--native-bindir", action="append", default=[])
    parser.add_argument("--native-sysroot", action="append", default=[])
    parser.add_argument("--stamp")
    args = parser.parse_args()

    paths = Paths(args.profile)
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
    elif args.command == "qemu-u-boot":
        build_qemu_uboot(paths, env)
    elif args.command == "linux":
        build_linux(paths, env)
    elif args.command == "dev-linux":
        build_linux(paths, env, dev=True)
    elif args.command == "dev-uboot":
        build_uboot(paths, env, dev=True)
    elif args.command == "busybox":
        build_busybox(paths, env)
    elif args.command == "rootfs":
        build_rootfs(paths, env)
    elif args.command == "fit":
        build_unmatched_fit(paths, env)
    elif args.command == "firmware-fit":
        build_unmatched_firmware_fit(paths, env)
    elif args.command == "bootchain":
        build_opensbi(paths, env)
        build_uboot(paths, env)
        build_linux(paths, env)
    elif args.command == "sd-image":
        build_sd_image(paths, env)
    elif args.command == "qemu-image":
        build_qemu_image(paths, env)
    elif args.command == "clean":
        clean(paths)
    elif args.command == "stamp":
        pass

    touch(args.stamp)


if __name__ == "__main__":
    main()
