#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 libaoahid contributors
"""Focused fail-closed tests for release metadata inspectors."""

from __future__ import annotations

import argparse
import contextlib
import gzip
import json
import io
from io import BytesIO
from pathlib import Path
import os
import re
import shutil
import stat
import struct
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest import mock


REPOSITORY = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPOSITORY / "tools" / "release"))
sys.path.insert(0, str(REPOSITORY / "tools" / "symbol-lint"))
sys.path.insert(0, str(REPOSITORY / "tools"))

import check_doc_references  # noqa: E402
import check_license_headers  # noqa: E402
import glibc_versions  # noqa: E402
import collect_release  # noqa: E402
import generate_spdx  # noqa: E402
import package_release  # noqa: E402
import package_source  # noqa: E402
import pkg_config_metadata  # noqa: E402
import sync_version  # noqa: E402
import validate_release  # noqa: E402
import version_files  # noqa: E402
import symbol_lint  # noqa: E402


class SymbolLintTests(unittest.TestCase):
    def test_macos_parser_keeps_only_true_dynamic_exports(self) -> None:
        fixture = """\
0000000000001000 (__TEXT,__text) external _aoahid_version
0000000000001010 (__TEXT,__text) weak external _aoahid_weak
0000000000001020 (__TEXT,__text) private external ___dso_handle
0000000000001030 (__TEXT,__text) weak private external _private_weak
0000000000001040 (__TEXT,__text) weak external automatically hidden _hidden
0000000000001050 (__TEXT,__text) non-external _local
0000000000001060 (__TEXT,__text) external [Thumb] _unexpected
"""
        self.assertEqual(
            symbol_lint.parse_macos_nm_exports(fixture),
            {"aoahid_version", "aoahid_weak", "_unexpected"},
        )


class GlibcVersionTests(unittest.TestCase):
    def fake_readelf(self, directory: Path, output: str) -> Path:
        script = directory / "fake-readelf"
        script.write_text(
            "#!/usr/bin/env python3\n"
            f"print({output!r})\n",
            encoding="utf-8",
        )
        script.chmod(script.stat().st_mode | stat.S_IXUSR)
        return script

    def test_numeric_requirements_are_canonicalized(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            library = root / "libfixture.so"
            library.touch()
            readelf = self.fake_readelf(
                root,
                "Name: GLIBC_2.17 Flags: none\\n"
                "Name: GLIBC_2.2.5 Flags: none\\n"
                "Name: GLIBC_2.17 Flags: none",
            )
            self.assertEqual(
                glibc_versions.inspect(str(readelf), library),
                ["GLIBC_2.2.5", "GLIBC_2.17"],
            )

    def test_nonnumeric_requirement_is_rejected(self) -> None:
        for unsupported in ("GLIBC_PRIVATE", "GLIBC_ABI_DT_RELR"):
            with self.subTest(unsupported=unsupported), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                library = root / "libfixture.so"
                library.touch()
                readelf = self.fake_readelf(
                    root,
                    f"Name: GLIBC_2.17 Flags: none\\nName: {unsupported} Flags: none",
                )
                with self.assertRaisesRegex(
                    glibc_versions.GlibcVersionError,
                    "unsupported nonnumeric GLIBC requirements",
                ):
                    glibc_versions.inspect(str(readelf), library)


class AbiNamespacePolicyTests(unittest.TestCase):
    def test_major_zero_minor_bumps_keep_the_0_0_namespace(self) -> None:
        for version in ("0.1.0", "0.2.0", "0.99.7"):
            with self.subTest(version=version):
                validate_release.validate_abi_namespace(version, "0", "0.0")

    def test_project_minor_is_not_an_abi_namespace(self) -> None:
        with self.assertRaisesRegex(
            validate_release.ValidationError,
            r"stable namespace 0\.0",
        ):
            validate_release.validate_abi_namespace("0.1.0", "0", "0.1")

    def test_soversion_must_match_project_major(self) -> None:
        with self.assertRaisesRegex(
            validate_release.ValidationError,
            "SOVERSION 0 disagrees with project major 1",
        ):
            validate_release.validate_abi_namespace("1.0.0", "0", "0.0")

    def test_new_abi_major_requires_an_explicit_namespace_policy(self) -> None:
        # "99" stands in for a next major with no policy entry yet.
        with self.assertRaisesRegex(
            validate_release.ValidationError,
            "no ELF ABI namespace policy is defined",
        ):
            validate_release.validate_abi_namespace("99.0.0", "99", "99.0")


class VersionSyncTests(unittest.TestCase):
    """sync_version.py / version_files.py: the single-command version bump.

    These copy only the files version_files.SITES names, into a scratch
    directory, so the round trip can rewrite them without touching the real
    tree the rest of this test run still depends on.
    """

    def _copy_tracked_files(self, root: Path) -> None:
        for relative in sorted({site.path for site in version_files.SITES}):
            source = REPOSITORY / relative
            destination = root / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(source.read_bytes())

    def test_write_all_rewrites_every_site_to_the_same_new_version(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self._copy_tracked_files(root)
            before = version_files.assembled_versions(root)
            self.assertEqual(len(set(before.values())), 1, before)

            changed = version_files.write_all(root, "9.9.9")

            self.assertEqual(set(changed), {site.path for site in version_files.SITES})
            after = version_files.assembled_versions(root)
            self.assertTrue(all(value == "9.9.9" for value in after.values()), after)

    def test_write_all_is_a_no_op_when_the_version_is_unchanged(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self._copy_tracked_files(root)
            current = next(iter(version_files.assembled_versions(root).values()))

            changed = version_files.write_all(root, current)

            self.assertEqual(changed, [])

    def test_write_all_rejects_a_malformed_version(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self._copy_tracked_files(root)
            with self.assertRaises(version_files.VersionFileError):
                version_files.write_all(root, "not-a-version")

    def test_assembled_versions_reports_disagreement_within_one_file(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self._copy_tracked_files(root)
            current = next(iter(version_files.assembled_versions(root).values()))
            mismatched = "9.9.8" if current != "9.9.8" else "9.9.9"
            rust_manifest = root / "bindings/rust/aoahid/Cargo.toml"
            text = rust_manifest.read_text(encoding="utf-8")
            rust_manifest.write_text(
                text.replace(f'version = "{current}"', f'version = "{mismatched}"', 1),
                encoding="utf-8",
            )
            with self.assertRaisesRegex(version_files.VersionFileError, "disagree"):
                version_files.assembled_versions(root)

    def test_sync_version_cli_writes_files_and_reports_success(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self._copy_tracked_files(root)
            with mock.patch("sys.argv", ["sync_version.py", "1.2.3", "--root", str(root), "--no-generate"]):
                exit_code = sync_version.main()
            self.assertEqual(exit_code, 0)
            after = version_files.assembled_versions(root)
            self.assertTrue(all(value == "1.2.3" for value in after.values()), after)

    def test_sync_version_cli_rejects_a_malformed_version(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self._copy_tracked_files(root)
            with mock.patch("sys.argv", ["sync_version.py", "not-a-version", "--root", str(root)]):
                exit_code = sync_version.main()
            self.assertEqual(exit_code, 1)


class SourcePackageTests(unittest.TestCase):
    def git(self, repository: Path, *arguments: str, env: dict[str, str] | None = None) -> str:
        result = subprocess.run(
            ["git", "-C", str(repository), *arguments],
            check=True,
            capture_output=True,
            text=True,
            env=env,
        )
        return result.stdout.strip()

    def fixture_repository(self, root: Path) -> tuple[str, int]:
        files = {
            ".gitignore": (REPOSITORY / ".gitignore").read_text(encoding="utf-8"),
            "CMakeLists.txt": "project(libaoahid VERSION 0.1.0)\n",
            "README.md": "# libaoahid\n",
            "LICENSE": "fixture license\n",
            "include/aoahid.h": "#pragma once\n",
            "src/api/c_api_context.cpp": "int fixture;\n",
            ".github/workflows/release.yml": "name: Release\n",
            "tools/release/package_source.py": "# fixture\n",
        }
        for relative, contents in files.items():
            path = root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(contents, encoding="utf-8")
        generated = {
            "build-manual/aoahid.dll": b"generated DLL",
            "build-local/libaoahid.so": b"generated shared object",
            "dist/libaoahid-source.zip": b"generated archive",
            "out/release-manifest.json": b"generated manifest",
            ".vcpkg/installed/status": b"generated cache",
            ".pytest_cache/README.md": b"generated cache",
            "bindings/python/.pytest_cache/README.md": b"nested generated cache",
            "tools/release/__pycache__/module.pyc": b"nested bytecode cache",
        }
        for relative, contents in generated.items():
            path = root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(contents)
        self.git(root, "init", "-b", "main")
        self.git(root, "config", "user.name", "Release Test")
        self.git(root, "config", "user.email", "release-test@example.invalid")
        self.git(root, "add", "-A")
        tracked = set(self.git(root, "ls-files").splitlines())
        self.assertTrue(set(files).issubset(tracked))
        self.assertTrue(set(generated).isdisjoint(tracked))
        environment = os.environ.copy()
        environment["GIT_AUTHOR_DATE"] = "2026-08-27T00:00:00Z"
        environment["GIT_COMMITTER_DATE"] = environment["GIT_AUTHOR_DATE"]
        self.git(root, "commit", "-m", "fixture", env=environment)
        source_sha = self.git(root, "rev-parse", "HEAD")
        epoch = int(self.git(root, "show", "-s", "--format=%ct", source_sha))
        return source_sha, epoch

    def test_source_archive_is_exact_and_reproducible(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source_sha, epoch = self.fixture_repository(root)
            first = package_source.create_archive_bytes(root, "0.1.0", source_sha, epoch)
            second = package_source.create_archive_bytes(root, "0.1.0", source_sha, epoch)
            self.assertEqual(first, second)
            package_source.validate_members(first, "0.1.0", source_sha)
            with tarfile.open(fileobj=BytesIO(first), mode="r:gz") as source_tar:
                archived = {member.name for member in source_tar.getmembers()}
            for generated in (
                "build-manual/",
                "build-local/",
                "dist/",
                "out/",
                ".vcpkg/",
                ".pytest_cache/",
                "bindings/python/.pytest_cache/",
                "tools/release/__pycache__/",
            ):
                self.assertFalse(
                    any(f"libaoahid-0.1.0/{generated}" in name for name in archived),
                    generated,
                )
            archive = root / package_source.source_archive_name("0.1.0")
            archive.write_bytes(first)
            package_source.validate_source_archive(
                archive, root, "0.1.0", source_sha, epoch
            )
            archive.write_bytes(first + b"tampered")
            with self.assertRaisesRegex(
                package_source.SourcePackageError, "deterministic git archive"
            ):
                package_source.validate_source_archive(
                    archive, root, "0.1.0", source_sha, epoch
                )


class ReleaseIdentityTests(unittest.TestCase):
    def test_exact_release_archive_names_and_urls(self) -> None:
        cases = {
            ("linux", "x86_64"): "libaoahid-0.1.0-linux-x86_64-ubuntu22.04.tar.gz",
            ("linux", "aarch64"): "libaoahid-0.1.0-linux-aarch64-ubuntu22.04.tar.gz",
            ("macos", "arm64"): "libaoahid-0.1.0-macos-arm64.tar.gz",
            ("macos", "x86_64"): "libaoahid-0.1.0-macos-x86_64.tar.gz",
            ("windows", "x86_64"): "libaoahid-0.1.0-windows-x86_64.zip",
            ("windows", "arm64"): "libaoahid-0.1.0-windows-arm64.zip",
        }
        self.assertEqual(
            {(platform, arch) for platform, arch, _, _ in collect_release.TARGETS},
            set(cases),
        )
        for (platform, arch), expected in cases.items():
            with self.subTest(platform=platform, arch=arch):
                self.assertEqual(
                    generate_spdx.release_archive_name("0.1.0", platform, arch),
                    expected,
                )
                self.assertEqual(
                    generate_spdx.release_download_url("0.1.0", platform, arch),
                    f"{package_source.REPOSITORY_URL}/releases/download/v0.1.0/{expected}",
                )
        with self.assertRaises(generate_spdx.SpdxError):
            generate_spdx.release_archive_name("0.1.0", "linux", "arm64")
        self.assertEqual(
            generate_spdx.release_sbom_name("0.1.0"), "libaoahid-0.1.0.spdx.json"
        )
        self.assertEqual(
            package_source.source_download_url("0.1.0"),
            f"{package_source.REPOSITORY_URL}/releases/download/v0.1.0/"
            "libaoahid-0.1.0-source.tar.gz",
        )

    def test_exact_10_asset_contract(self) -> None:
        assets = collect_release.expected_release_asset_names("0.1.0")
        self.assertEqual(len(assets), 10)
        self.assertEqual(
            sum(name.endswith((".tar.gz", ".zip")) for name in assets),
            7,
        )
        self.assertEqual(
            {name for name in assets if name.endswith(".spdx.json")},
            {"libaoahid-0.1.0.spdx.json"},
        )
        self.assertFalse(any("-runtime" in name for name in assets))
        self.assertFalse(
            any(re.search(r"-(shared|static)\.(tar\.gz|zip)$", name) for name in assets)
        )
        # A bare shared library cannot carry libusb's license or corresponding
        # source, so no asset may be one.
        self.assertEqual(sum(name.endswith((".so", ".dylib", ".dll")) for name in assets), 0)
        self.assertIn("release-manifest.json", assets)
        self.assertIn("SHA256SUMS", assets)
        readme = (REPOSITORY / "README.md").read_text(encoding="utf-8")
        self.assertIn("exactly 10 uploaded assets", readme)
        self.assertNotIn("27 uploaded assets", readme)

    def test_shared_archive_runtime_architecture_is_checked(self) -> None:
        def elf(machine: int) -> bytes:
            header = bytearray(64)
            header[0:4] = b"\x7fELF"
            header[4] = 2
            header[5] = 1
            struct.pack_into("<HH", header, 16, 3, machine)
            return bytes(header)

        root = "libaoahid-0.1.0-linux-x86_64-ubuntu22.04"
        with tempfile.TemporaryDirectory() as directory:
            for machine, accepted in ((62, True), (183, False)):
                with self.subTest(machine=machine):
                    archive = Path(directory) / f"{machine}" / (root + ".tar.gz")
                    archive.parent.mkdir()
                    with archive.open("wb") as raw:
                        with gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=0) as gz:
                            with tarfile.open(
                                mode="w", fileobj=gz, format=tarfile.PAX_FORMAT
                            ) as output:
                                payload = elf(machine)
                                info = tarfile.TarInfo(f"{root}/lib/libaoahid.so.0.1.0")
                                info.size = len(payload)
                                info.mode = 0o644
                                output.addfile(info, io.BytesIO(payload))
                                link = tarfile.TarInfo(f"{root}/lib/libaoahid.so.0")
                                link.type = tarfile.SYMTYPE
                                link.linkname = "libaoahid.so.0.1.0"
                                output.addfile(link)
                    if accepted:
                        collect_release.validate_shared_runtime(archive, "linux", "x86_64")
                    else:
                        with self.assertRaises(collect_release.CollectionError):
                            collect_release.validate_shared_runtime(
                                archive, "linux", "x86_64"
                            )

    def test_macos_runtime_is_checked_as_a_thin_mach_o_dylib(self) -> None:
        def dylib(cpu_type: int, file_type: int = 6) -> bytes:
            header = bytearray(32)
            header[0:4] = b"\xcf\xfa\xed\xfe"
            struct.pack_into("<iiI", header, 4, cpu_type, 0, file_type)
            return bytes(header)

        collect_release.validate_runtime_architecture(
            dylib(0x0100000C), "macos", "arm64"
        )
        collect_release.validate_runtime_architecture(
            dylib(0x01000007), "macos", "x86_64"
        )
        for data, arch in (
            (dylib(0x01000007), "arm64"),
            (dylib(0x0100000C), "x86_64"),
            (dylib(0x0100000C, 2), "arm64"),
            (b"\xca\xfe\xba\xbe" + bytes(28), "arm64"),
        ):
            with self.subTest(arch=arch, magic=data[:4]):
                with self.assertRaises(collect_release.CollectionError):
                    collect_release.validate_runtime_architecture(
                        data, "macos", arch
                    )

    def test_spdx_files_are_licensed_individually(self) -> None:
        self.assertEqual(
            generate_spdx.file_license("lib/libaoahid.so.0.1.0"),
            (generate_spdx.MIT_LICENSE, generate_spdx.FIRST_PARTY_COPYRIGHT),
        )
        for dependency in (
            "lib/libusb-1.0.so.0.6.0",
            "lib/libusb-1.0.0.dylib",
            "bin/libusb-1.0.dll",
            "include/libusb-1.0/libusb.h",
            "lib/pkgconfig/libusb-1.0.pc",
            "lib/dependencies/libusb-1.0.lib",
            collect_release.LIBUSB_LICENSE_NAME,
            collect_release.LIBUSB_SOURCE_NAME,
        ):
            self.assertEqual(
                generate_spdx.file_license(dependency)[0],
                generate_spdx.LIBUSB_LICENSE,
                dependency,
            )
        self.assertEqual(
            generate_spdx.file_license(collect_release.VCPKG_LICENSE_NAME),
            (generate_spdx.MIT_LICENSE, generate_spdx.MICROSOFT_COPYRIGHT),
        )

    def test_license_header_uses_each_language_comment_syntax(self) -> None:
        # A Kotlin build script rejects a '#' line outright, so the header
        # prefix has to follow the language's own comment syntax rather than
        # the file's role. Assert the mapping explicitly, then confirm no file
        # actually carries the wrong one.
        expected_prefix = {
            ".c": "// ",
            ".h": "// ",
            ".cpp": "// ",
            ".hpp": "// ",
            ".rs": "// ",
            ".cs": "// ",
            ".java": "// ",
            ".kt": "// ",
            ".kts": "// ",
            ".py": "# ",
            ".cmake": "# ",
            ".sh": "# ",
        }
        for suffix, prefix in expected_prefix.items():
            self.assertEqual(
                check_license_headers.header_prefix(Path(f"fixture{suffix}")),
                prefix,
                suffix,
            )
        self.assertEqual(
            check_license_headers.header_prefix(Path("CMakeLists.txt")), "# "
        )
        for path in check_license_headers.sources():
            # Only the header region matters; a file may legitimately mention
            # the other syntax in its body, as this test file does.
            header = "\n".join(path.read_text(encoding="utf-8").splitlines()[:3])
            wrong = (
                "# SPDX-License-Identifier"
                if check_license_headers.header_prefix(path) == "// "
                else "// SPDX-License-Identifier"
            )
            self.assertNotIn(wrong, header, path)

    def test_every_source_file_carries_the_spdx_header(self) -> None:
        files = check_license_headers.sources()
        self.assertGreater(len(files), 50)
        failures = [
            message
            for message in (check_license_headers.check(path) for path in files)
            if message
        ]
        self.assertEqual(failures, [])

    def test_pkg_config_requires_the_canonical_repository_url(self) -> None:
        fixture = b"""\
prefix=${pcfiledir}/../..
exec_prefix=${prefix}
libdir=${exec_prefix}/lib
includedir=${prefix}/include
Name: libaoahid
Description: fixture
Version: 0.1.0
URL: https://github.com/nemarpuc/libaoahid
Libs: -L${libdir} -laoahid
Cflags: -I${includedir}
"""
        pkg_config_metadata.validate_relocatable(
            fixture,
            label="fixture",
            expected_name="libaoahid",
            expected_version="0.1.0",
            expected_url=package_source.REPOSITORY_URL,
            expected_library="aoahid",
        )
        with self.assertRaisesRegex(pkg_config_metadata.PkgConfigError, "URL"):
            pkg_config_metadata.validate_relocatable(
                fixture.replace(b"nemarpuc/libaoahid", b"wrong/repository"),
                label="fixture",
                expected_name="libaoahid",
                expected_version="0.1.0",
                expected_url=package_source.REPOSITORY_URL,
                expected_library="aoahid",
            )

    def test_build_metadata_has_exact_repository_and_asset_url(self) -> None:
        arguments = argparse.Namespace(
            version="0.1.0",
            platform="windows",
            arch="arm64",
            source_sha="a" * 40,
            created_epoch=0,
            libusb_version="1.0.30",
            build_metadata=[],
        )
        metadata = package_release.metadata(arguments)
        self.assertEqual(metadata["repository"], package_source.REPOSITORY_URL)
        self.assertEqual(
            metadata["release_download_url"],
            f"{package_source.REPOSITORY_URL}/releases/download/v0.1.0/"
            "libaoahid-0.1.0-windows-arm64.zip",
        )
        self.assertEqual(metadata["components"], ["shared", "static"])
        self.assertNotIn("variant", metadata)

    def test_spdx_rejects_noncanonical_package_urls(self) -> None:
        own = {
            "SPDXID": "SPDXRef-Package-libaoahid",
            "versionInfo": "0.1.0",
            "filesAnalyzed": True,
            "homepage": "https://github.com/wrong/repository",
            "downloadLocation": "https://example.invalid/archive.zip",
            "packageFileName": "libaoahid-0.1.0-windows-arm64.zip",
        }
        document = {
            "spdxVersion": "SPDX-2.3",
            "dataLicense": "CC0-1.0",
            "name": "libaoahid-0.1.0-windows-arm64",
            "packages": [own],
        }
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with self.assertRaisesRegex(generate_spdx.SpdxError, "homepage"):
                generate_spdx.validate_document(root, document)
            own["homepage"] = package_source.REPOSITORY_URL
            with self.assertRaisesRegex(generate_spdx.SpdxError, "download location"):
                generate_spdx.validate_document(root, document)

    def test_release_workflow_has_all_native_targets_and_source(self) -> None:
        workflow = (REPOSITORY / ".github/workflows/release.yml").read_text(
            encoding="utf-8"
        )
        self.assertIn("AOAHID_EXPECTED_REPOSITORY: nemarpuc/libaoahid", workflow)
        for label in (
            "Linux x86-64 (Ubuntu 22.04 glibc)",
            "Linux AArch64 (Ubuntu 22.04 glibc)",
            "Windows x64",
            "Windows ARM64",
            "macOS ARM64",
            "macOS x86-64",
        ):
            self.assertIn(label, workflow)
        self.assertIn("os: macos-15\n", workflow)
        self.assertIn("os: macos-15-intel", workflow)
        self.assertIn("os: windows-2025-vs2026", workflow)
        self.assertIn("os: windows-11-vs2026-arm", workflow)
        self.assertIn("generator: Visual Studio 18 2026", workflow)
        self.assertIn("tools/release/package_source.py", workflow)
        self.assertIn("--source-root .", workflow)
        self.assertIn('test "${#assets[@]}" -eq 10', workflow)
        self.assertNotIn("-eq 27", workflow)
        self.assertNotRegex(workflow, r"-(shared|static)\.(tar\.gz|zip)")
        self.assertNotIn("--variant", workflow)
        self.assertIn(
            "AOAHID_RELEASE_VERSION: ${{ needs.validate.outputs.version }}", workflow
        )
        self.assertIn('os.environ["AOAHID_RELEASE_VERSION"]', workflow)
        self.assertIn(
            'Environment.GetEnvironmentVariable("AOAHID_RELEASE_VERSION")', workflow
        )
        self.assertNotIn("AOAHID_VERSION_MAJOR == 0", workflow)
        self.assertNotIn("AOAHID_VERSION_MAJOR != 0", workflow)
        self.assertIn('if ! response="$(gh api --paginate --slurp', workflow)
        self.assertEqual(workflow.count("verify_exact_asset_set"), 2)
        self.assertIn("public_release_matches()", workflow)
        self.assertIn("for publish_read_attempt in $(seq 1 5)", workflow)

    def test_all_workflow_actions_are_commit_pinned(self) -> None:
        workflows = sorted((REPOSITORY / ".github/workflows").glob("*.yml"))
        self.assertTrue(workflows)
        for workflow_path in workflows:
            workflow = workflow_path.read_text(encoding="utf-8")
            action_references = re.findall(
                r"^\s*(?:-\s+)?uses:\s*([^\s#]+)", workflow, re.MULTILINE
            )
            self.assertTrue(action_references, workflow_path.name)
            for reference in action_references:
                with self.subTest(workflow=workflow_path.name, reference=reference):
                    self.assertRegex(reference, r"^[^@]+@[0-9a-f]{40}$")

    def test_doxygen_workflows_create_the_nested_output_directory(self) -> None:
        for name in ("ci.yml", "docs.yml"):
            workflow = (REPOSITORY / ".github" / "workflows" / name).read_text(
                encoding="utf-8"
            )
            create_at = workflow.index("mkdir -p build/doxygen")
            doxygen_at = workflow.index("doxygen tools/docs/Doxyfile", create_at)
            self.assertLess(create_at, doxygen_at, name)

    def test_doxygen_1_9_1_markdown_avoids_scoped_empty_call_autolinks(self) -> None:
        """Doxygen 1.9.1 treats scoped empty calls in code spans as links."""
        markdown_inputs = [REPOSITORY / "CHANGELOG.md"]
        markdown_inputs.extend(sorted((REPOSITORY / "docs").glob("*.md")))
        scoped_empty_call = re.compile(
            r"`[^`\n]*::[A-Za-z_][A-Za-z0-9_]*\(\)[^`\n]*`"
        )
        for markdown_path in markdown_inputs:
            with self.subTest(path=markdown_path.relative_to(REPOSITORY)):
                self.assertNotRegex(
                    markdown_path.read_text(encoding="utf-8"), scoped_empty_call
                )

    def test_installed_config_has_an_explicit_success_baseline(self) -> None:
        config = (REPOSITORY / "cmake" / "aoahid-config.cmake.in").read_text(
            encoding="utf-8"
        )
        baseline_at = config.index("set(aoahid_FOUND TRUE)")
        component_check_at = config.index("check_required_components(aoahid)")
        self.assertLess(baseline_at, component_check_at)

    def test_static_installed_target_name_is_invariant_and_compatible(self) -> None:
        cmake = (REPOSITORY / "CMakeLists.txt").read_text(encoding="utf-8")
        config = (REPOSITORY / "cmake" / "aoahid-config.cmake.in").read_text(
            encoding="utf-8"
        )
        release = (REPOSITORY / ".github/workflows/release.yml").read_text(
            encoding="utf-8"
        )
        self.assertIn(
            "set_target_properties(aoahid_static PROPERTIES EXPORT_NAME aoahid_static)",
            cmake,
        )
        self.assertNotIn("EXPORT_NAME aoahid)", cmake)
        self.assertIn(
            "add_library(aoahid::aoahid ALIAS aoahid::aoahid_static)", config
        )
        self.assertIn("set(_AOAHID_BUILT_SHARED @AOAHID_BUILD_SHARED@)", config)
        self.assertIn("set(_AOAHID_BUILT_STATIC @AOAHID_BUILD_STATIC@)", config)
        self.assertIn("if(_AOAHID_BUILT_SHARED AND", config)
        self.assertIn("if(_AOAHID_BUILT_STATIC AND", config)
        # Linux, macOS, and Windows each link the static target alone and
        # together with the shared one, from the same single install.
        self.assertEqual(
            release.count("-DAOAHID_CONSUMER_TARGET=aoahid::aoahid_static"), 6
        )
        self.assertEqual(release.count("-DAOAHID_CONSUMER_EXPECTED_TYPE=STATIC_LIBRARY"), 6)
        self.assertEqual(release.count("-DAOAHID_CONSUMER_EXPECTED_TYPE=SHARED_LIBRARY"), 6)
        self.assertEqual(release.count("-DAOAHID_FIND_COMPONENTS=shared;static"), 3)
        self.assertEqual(release.count("-DAOAHID_BUILD_SHARED:BOOL=ON"), 3)
        self.assertEqual(release.count("-DAOAHID_BUILD_STATIC:BOOL=ON"), 3)
        self.assertNotIn("stage-$variant", release)
        self.assertNotIn("stage-shared", release)
        self.assertNotIn("stage-static", release)
        self.assertIn("combined package component exports are incomplete", release)
        self.assertIn("package accepted an unknown component", release)
        self.assertNotIn("CMakeCache.txt", release)
        self.assertNotIn("wrong AOAHID_BUILD_SHARED value", release)
        self.assertNotIn("wrong AOAHID_BUILD_STATIC value", release)

    def test_component_contract_test_is_architecture_independent(self) -> None:
        cmake = (REPOSITORY / "CMakeLists.txt").read_text(encoding="utf-8")
        contract = (
            REPOSITORY / "tests" / "cmake" / "package_component_contract.cmake"
        ).read_text(encoding="utf-8")
        self.assertIn("project(aoahid_component_contract LANGUAGES NONE)", contract)
        self.assertNotIn("AOAHID_GENERATOR_PLATFORM", contract)
        self.assertNotIn("AOAHID_GENERATOR_TOOLSET", contract)
        self.assertIn("-DAOAHID_PROJECT_VERSION=${PROJECT_VERSION}", cmake)
        self.assertIn(
            "-DAOAHID_PROJECT_HOMEPAGE_URL=${PROJECT_HOMEPAGE_URL}", cmake
        )

    def test_ci_pkg_config_check_derives_the_project_version(self) -> None:
        workflow = (REPOSITORY / ".github" / "workflows" / "ci.yml").read_text(
            encoding="utf-8"
        )
        self.assertIn("project_version = validate_release.source_versions", workflow)
        self.assertEqual(workflow.count("expected_version=project_version"), 2)
        self.assertNotRegex(workflow, r'expected_version="0\.1\.[0-9]+"')

    def test_support_table_check_is_run_only_with_a_catalog(self) -> None:
        for workflow_path in sorted((REPOSITORY / ".github/workflows").glob("*.yml")):
            for line in workflow_path.read_text(encoding="utf-8").splitlines():
                if "generate-support-table/generate.py" in line:
                    self.assertIn("--catalog", line, workflow_path.name)
        cmake = (REPOSITORY / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn(
            "--catalog ${CMAKE_CURRENT_BINARY_DIR}/support-manifests.tsv", cmake
        )

    def test_repository_metadata_is_consistent(self) -> None:
        validate_release.validate_repository_identity(REPOSITORY)

class DryRunTests(unittest.TestCase):
    def test_dry_run_version_is_the_newest_dated_changelog_section(self) -> None:
        self.assertEqual(
            validate_release.newest_dated_version(REPOSITORY),
            next(iter(validate_release.source_versions(REPOSITORY).values())),
        )
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            changelog = root / "CHANGELOG.md"
            changelog.write_text(
                "# Changelog\n\n## [Unreleased]\n\n- next\n\n"
                "## [0.2.0] - 2026-12-01\n\n- b\n\n## [0.1.0] - 2026-11-07\n\n- a\n",
                encoding="utf-8",
            )
            self.assertEqual(validate_release.newest_dated_version(root), "0.2.0")
            changelog.write_text(
                "## [0.1.0] - 2026-11-07\n\n- a\n\n## [0.2.0] - 2026-12-01\n\n- b\n",
                encoding="utf-8",
            )
            with self.assertRaisesRegex(validate_release.ValidationError, "highest"):
                validate_release.newest_dated_version(root)
            changelog.write_text("## [Unreleased]\n\n- a\n", encoding="utf-8")
            with self.assertRaisesRegex(validate_release.ValidationError, "no dated"):
                validate_release.newest_dated_version(root)

    def test_dry_run_publishes_nothing_and_the_tag_path_is_kept(self) -> None:
        workflow = (REPOSITORY / ".github/workflows/release.yml").read_text(
            encoding="utf-8"
        )
        self.assertIn("  workflow_dispatch:\n    inputs:\n      dry_run:", workflow)
        self.assertIn("        type: boolean\n        default: true\n", workflow)
        self.assertIn("validate_release.py --dry-run", workflow)
        self.assertIn('validate_release.py --tag "${GITHUB_REF_NAME}"', workflow)
        self.assertIn("GITHUB_STEP_SUMMARY", workflow)
        self.assertIn("A dispatched run is always a dry run", workflow)
        publish = workflow[workflow.index("\n  publish:\n") :]
        publish = publish[: publish.index("\n  publish-pypi:\n")]
        self.assertIn(
            "if: needs.validate.outputs.dry_run == 'false' && "
            "github.event_name == 'push' && github.ref_type == 'tag'",
            publish,
        )
        self.assertIn("repos/$GITHUB_REPOSITORY/releases", publish)
        for job in ("publish-pypi", "publish-nuget"):
            block = workflow[workflow.index(f"\n  {job}:\n") :]
            self.assertIn(
                "if: needs.validate.outputs.dry_run == 'false' && "
                "vars.AOAHID_PUBLISH_BINDINGS == 'true'",
                block[: block.index("steps:")],
            )
        assemble = workflow[workflow.index("\n  assemble:\n") : workflow.index("\n  publish:\n")]
        self.assertIn("contents: read", assemble)
        self.assertNotIn("contents: write", assemble)
        self.assertNotIn("gh api", assemble)
        self.assertIn("name: release-dist", assemble)
        self.assertIn("collect_release.py", assemble)
        self.assertIn("glibc_versions.py", assemble)
        self.assertIn('-eq 10', assemble)


def _elf(machine: int) -> bytes:
    header = bytearray(64)
    header[0:4] = b"\x7fELF"
    header[4] = 2
    header[5] = 1
    struct.pack_into("<HH", header, 16, 3, machine)
    return bytes(header)


def _mach_o(cpu_type: int) -> bytes:
    header = bytearray(32)
    header[0:4] = b"\xcf\xfa\xed\xfe"
    struct.pack_into("<iiI", header, 4, cpu_type, 0, 6)
    return bytes(header)


def _pe_dll(machine: int) -> bytes:
    data = bytearray(0x200)
    data[0:2] = b"MZ"
    struct.pack_into("<I", data, 0x3C, 0x80)
    data[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<H", data, 0x84, machine)
    struct.pack_into("<H", data, 0x80 + 22, 0x2002)
    struct.pack_into("<H", data, 0x80 + 24, 0x20B)
    return bytes(data)


FAKE_LIBUSB_SOURCE = b"fixture libusb source archive"
FAKE_LIBUSB_LICENSE = b"fixture libusb LGPL-2.1 text"
FAKE_VCPKG = {
    generate_spdx.VCPKG_LICENSE_NAME: b"fixture vcpkg MIT license",
    generate_spdx.VCPKG_PORT_FILES[0]: b"fixture portfile",
    generate_spdx.VCPKG_PORT_FILES[1]: b"fixture vcpkg.json",
}
SOURCE_SHA = "a" * 40
EPOCH = 1_762_473_600


def _sha256(data: bytes) -> str:
    import hashlib

    return hashlib.sha256(data).hexdigest()


def _pc(name: str, library: str, version: str, extra: str = "") -> str:
    return (
        "prefix=${pcfiledir}/../..\n"
        "exec_prefix=${prefix}\n"
        "libdir=${exec_prefix}/lib\n"
        "includedir=${prefix}/include\n\n"
        f"Name: {name}\nDescription: fixture\nVersion: {version}\n"
        + (f"URL: {package_source.REPOSITORY_URL}\n" if name.startswith("libaoahid") else "")
        + extra
        + f"Libs: -L${{libdir}} -l{library}\nCflags: -I${{includedir}}\n"
    )


def write_fixture_stage(root: Path, platform: str, arch: str, *, omit: str = "") -> None:
    """Write an install tree shaped like one combined release build."""
    files: dict[str, bytes] = {
        "include/aoahid.h": b"/* fixture */\n",
        "include/aoahid.hpp": b"/* fixture */\n",
        "include/libusb-1.0/libusb.h": b"/* fixture */\n",
        "lib/cmake/aoahid/aoahid-config.cmake": b"# fixture\n",
        "lib/cmake/aoahid/aoahid-config-version.cmake": b"# fixture\n",
        "lib/cmake/aoahid/aoahid-check-libusb.cmake": b"# fixture\n",
        generate_spdx.LIBUSB_LICENSE_NAME: FAKE_LIBUSB_LICENSE,
        generate_spdx.LIBUSB_SOURCE_NAME: FAKE_LIBUSB_SOURCE,
    }
    for component in ("shared", "static"):
        files[f"lib/cmake/aoahid/aoahid-{component}-targets.cmake"] = b"# fixture\n"
        files[f"lib/cmake/aoahid/aoahid-{component}-targets-release.cmake"] = b"# fixture\n"
    cxx = "Libs.private: -lstdc++\n" if platform != "windows" else ""
    files["lib/pkgconfig/aoahid.pc"] = _pc("libaoahid", "aoahid", "0.1.0").encode()
    files["lib/pkgconfig/aoahid-static.pc"] = _pc(
        "libaoahid static",
        "aoahid_static",
        "0.1.0",
        "Requires.private: libusb-1.0 >= 1.0.30\n" + cxx,
    ).encode()
    files["lib/pkgconfig/libusb-1.0.pc"] = _pc(
        "libusb-1.0", "libusb-1.0" if platform == "windows" else "usb-1.0", "1.0.30"
    ).encode()
    links: dict[str, str] = {}
    if platform == "linux":
        machine = {"x86_64": 62, "aarch64": 183}[arch]
        files["lib/libaoahid.so.0.1.0"] = _elf(machine)
        files["lib/libaoahid_static.a"] = b"!<arch>\n"
        files["lib/libusb-1.0.so.0.6.0"] = _elf(machine)
        links = {
            "lib/libaoahid.so.0": "libaoahid.so.0.1.0",
            "lib/libaoahid.so": "libaoahid.so.0",
            "lib/libusb-1.0.so.0": "libusb-1.0.so.0.6.0",
            "lib/libusb-1.0.so": "libusb-1.0.so.0",
        }
        glibc = [
            {"path": path, "required_versions": ["GLIBC_2.17"], "maximum_required": "GLIBC_2.17"}
            for path in ("lib/libaoahid.so.0.1.0", "lib/libusb-1.0.so.0.6.0")
        ]
        files["share/doc/libaoahid/glibc-requirements.json"] = json.dumps(
            {
                "schema": 1,
                "baseline": glibc_versions.BASELINE,
                "maximum_allowed": glibc_versions.MAXIMUM_ALLOWED,
                "required_versions": ["GLIBC_2.17"],
                "maximum_required": "GLIBC_2.17",
                "files": glibc,
            }
        ).encode()
    elif platform == "macos":
        cpu = {"x86_64": 0x01000007, "arm64": 0x0100000C}[arch]
        files["lib/libaoahid.0.1.0.dylib"] = _mach_o(cpu)
        files["lib/libaoahid_static.a"] = b"!<arch>\n"
        files["lib/libusb-1.0.0.dylib"] = _mach_o(cpu)
        links = {
            "lib/libaoahid.0.dylib": "libaoahid.0.1.0.dylib",
            "lib/libaoahid.dylib": "libaoahid.0.dylib",
            "lib/libusb-1.0.dylib": "libusb-1.0.0.dylib",
        }
    else:
        machine = {"x86_64": 0x8664, "arm64": 0xAA64}[arch]
        files["bin/aoahid.dll"] = _pe_dll(machine)
        files["bin/libusb-1.0.dll"] = _pe_dll(machine)
        files["lib/aoahid.lib"] = b"!<arch>\n"
        files["lib/aoahid_static.lib"] = b"!<arch>\n"
        files["lib/libusb-1.0.lib"] = b"!<arch>\n"
        files["lib/dependencies/libusb-1.0.lib"] = b"!<arch>\n"
        files.update(FAKE_VCPKG)
    for relative, contents in files.items():
        if relative == omit:
            continue
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(contents)
    for relative, target in links.items():
        (root / relative).symlink_to(target)


def fixture_hashes() -> mock._patch:
    """Accept the fixture's stand-ins for the pinned third-party files."""
    vcpkg = {name: _sha256(data) for name, data in FAKE_VCPKG.items()}
    patches = [
        mock.patch.multiple(
            generate_spdx,
            LIBUSB_SOURCE_SHA256=_sha256(FAKE_LIBUSB_SOURCE),
            LIBUSB_LICENSE_SHA256=_sha256(FAKE_LIBUSB_LICENSE),
            VCPKG_LICENSE_SHA256=vcpkg[generate_spdx.VCPKG_LICENSE_NAME],
        ),
        mock.patch.multiple(
            collect_release,
            LIBUSB_SOURCE_SHA256=_sha256(FAKE_LIBUSB_SOURCE),
            LIBUSB_LICENSE_SHA256=_sha256(FAKE_LIBUSB_LICENSE),
            VCPKG_LICENSE_SHA256=vcpkg[generate_spdx.VCPKG_LICENSE_NAME],
        ),
        mock.patch.dict(generate_spdx.VCPKG_FILE_SHA256, vcpkg),
        # The source archive's own tests are in SourcePackageTests; here it is
        # a placeholder whose git-tree validation would need a real tag.
        mock.patch.object(package_source, "validate_source_archive"),
    ]
    stack = contextlib.ExitStack()
    for patch in patches:
        stack.enter_context(patch)
    return stack


def run_main(module: object, arguments: list[str]) -> tuple[int, str]:
    error = io.StringIO()
    with mock.patch("sys.argv", ["tool", *arguments]), contextlib.redirect_stderr(
        error
    ), contextlib.redirect_stdout(io.StringIO()):
        code = module.main()
    return code, error.getvalue()


def package_fixture(directory: Path, platform: str, arch: str, *, omit: str = "") -> tuple[int, str]:
    stage = directory / f"stage-{platform}-{arch}"
    write_fixture_stage(stage, platform, arch, omit=omit)
    arguments = [
        "--source-root", str(REPOSITORY),
        "--stage", str(stage),
        "--out-dir", str(directory / "dist"),
        "--base-name", generate_spdx.release_archive_root("0.1.0", platform, arch),
        "--version", "0.1.0",
        "--platform", platform,
        "--arch", arch,
        "--source-sha", SOURCE_SHA,
        "--created-epoch", str(EPOCH),
        "--libusb-version", "1.0.30",
    ]
    if platform == "linux":
        arguments += [
            "--build-metadata", f"glibc_baseline={glibc_versions.BASELINE}",
            "--build-metadata", "glibc_maximum_required=GLIBC_2.17",
        ]
    return run_main(package_release, arguments)


def collect(directory: Path) -> tuple[int, str]:
    return run_main(
        collect_release,
        [
            "--directory", str(directory),
            "--source-root", str(REPOSITORY),
            "--version", "0.1.0",
            "--source-sha", SOURCE_SHA,
            "--created-epoch", str(EPOCH),
        ],
    )


class SingleArchiveReleaseTests(unittest.TestCase):
    """End to end: six combined archives become the ten release assets."""

    @classmethod
    def setUpClass(cls) -> None:
        cls.temporary = tempfile.TemporaryDirectory()
        cls.root = Path(cls.temporary.name)
        cls.hashes = fixture_hashes()
        cls.hashes.__enter__()
        for platform, arch, _, _ in collect_release.TARGETS:
            code, error = package_fixture(cls.root, platform, arch)
            if code != 0:
                raise AssertionError(f"{platform}/{arch}: {error}")
        dist = cls.root / "dist"
        (dist / package_source.source_archive_name("0.1.0")).write_bytes(b"fixture source")
        cls.pristine = cls.root / "pristine"
        shutil.copytree(dist, cls.pristine)
        code, error = collect(dist)
        if code != 0:
            raise AssertionError(error)
        cls.dist = dist

    @classmethod
    def tearDownClass(cls) -> None:
        cls.hashes.__exit__(None, None, None)
        cls.temporary.cleanup()

    def fresh(self) -> Path:
        directory = Path(tempfile.mkdtemp(dir=self.root))
        for path in self.pristine.iterdir():
            shutil.copy2(path, directory / path.name)
        return directory

    def test_release_has_exactly_the_ten_assets(self) -> None:
        names = {path.name for path in self.dist.iterdir()}
        self.assertEqual(names, collect_release.expected_release_asset_names("0.1.0"))
        self.assertEqual(len(names), 10)
        sums = (self.dist / "SHA256SUMS").read_text(encoding="utf-8").splitlines()
        self.assertEqual(len(sums), 9)
        for line in sums:
            digest, name = line.split("  ")
            self.assertEqual(digest, collect_release.sha256(self.dist / name))

    def test_each_archive_carries_both_components_and_lgpl_material(self) -> None:
        manifest = json.loads((self.dist / "release-manifest.json").read_text(encoding="utf-8"))
        self.assertEqual(len(manifest["targets"]), 6)
        for target in manifest["targets"]:
            with self.subTest(archive=target["archive"]):
                self.assertEqual(target["components"], ["shared", "static"])
                self.assertNotIn("variant", target)
                members = collect_release.archive_members(self.dist / target["archive"])
                for required in (
                    "lib/cmake/aoahid/aoahid-shared-targets.cmake",
                    "lib/cmake/aoahid/aoahid-static-targets.cmake",
                    "lib/pkgconfig/aoahid.pc",
                    "lib/pkgconfig/aoahid-static.pc",
                    "share/doc/libaoahid/LICENSE",
                    "share/doc/libaoahid/NOTICE",
                    "share/doc/libaoahid/THIRD_PARTY_NOTICES.md",
                    collect_release.LIBUSB_LICENSE_NAME,
                    collect_release.LIBUSB_SOURCE_NAME,
                    collect_release.EMBEDDED_SPDX_NAME,
                ):
                    self.assertIn(required, members)
                shared = {
                    "linux": "lib/libaoahid.so.0.1.0",
                    "macos": "lib/libaoahid.0.1.0.dylib",
                    "windows": "bin/aoahid.dll",
                }[target["platform"]]
                static = (
                    "lib/aoahid_static.lib"
                    if target["platform"] == "windows"
                    else "lib/libaoahid_static.a"
                )
                self.assertIn(shared, members)
                self.assertIn(static, members)
                if target["platform"] == "windows":
                    self.assertIn(collect_release.VCPKG_LICENSE_NAME, members)

    def test_one_aggregated_sbom_describes_every_archive(self) -> None:
        sbom_path = self.dist / "libaoahid-0.1.0.spdx.json"
        document = json.loads(sbom_path.read_text(encoding="utf-8"))
        manifest = json.loads((self.dist / "release-manifest.json").read_text(encoding="utf-8"))
        self.assertEqual(manifest["sbom"]["file"], sbom_path.name)
        self.assertEqual(manifest["sbom"]["namespace"], document["documentNamespace"])
        self.assertEqual(document["spdxVersion"], "SPDX-2.3")
        packages = {package["SPDXID"]: package for package in document["packages"]}
        archive_ids = {
            generate_spdx.release_package_id(platform, arch)
            for platform, arch, _, _ in collect_release.TARGETS
        }
        self.assertEqual(
            set(packages),
            archive_ids
            | {
                generate_spdx.RELEASE_SOURCE_PACKAGE_ID,
                "SPDXRef-Package-libusb",
                "SPDXRef-Package-vcpkg",
            },
        )
        self.assertEqual(
            set(document["documentDescribes"]),
            archive_ids | {generate_spdx.RELEASE_SOURCE_PACKAGE_ID},
        )
        relationships = {
            (item["spdxElementId"], item["relationshipType"], item["relatedSpdxElement"])
            for item in document["relationships"]
        }
        for target in manifest["targets"]:
            package_id = generate_spdx.release_package_id(
                target["platform"], target["architecture"]
            )
            package = packages[package_id]
            self.assertEqual(package["packageFileName"], target["archive"])
            self.assertEqual(
                package["checksums"],
                [{"algorithm": "SHA256", "checksumValue": target["archive_sha256"]}],
            )
            self.assertIn(generate_spdx.LIBUSB_LICENSE, package["licenseInfoFromFiles"])
            self.assertIn(
                (package_id, "GENERATED_FROM", generate_spdx.RELEASE_SOURCE_PACKAGE_ID),
                relationships,
            )
            self.assertIn((package_id, "DEPENDS_ON", "SPDXRef-Package-libusb"), relationships)
            embedded = json.loads(
                collect_release.archive_bytes(
                    self.dist / target["archive"], collect_release.EMBEDDED_SPDX_NAME
                )
            )
            own = next(p for p in embedded["packages"] if p["SPDXID"] == "SPDXRef-Package-libaoahid")
            self.assertEqual(
                own["packageVerificationCode"]["packageVerificationCodeValue"],
                package["packageVerificationCode"]["packageVerificationCodeValue"],
            )
        source = packages[generate_spdx.RELEASE_SOURCE_PACKAGE_ID]
        self.assertEqual(source["checksums"][0]["checksumValue"], manifest["source"]["archive_sha256"])

    def test_collect_is_deterministic(self) -> None:
        directory = self.fresh()
        self.assertEqual(collect(directory)[0], 0)
        for name in ("SHA256SUMS", "release-manifest.json", "libaoahid-0.1.0.spdx.json"):
            self.assertEqual((directory / name).read_bytes(), (self.dist / name).read_bytes(), name)

    def repack_without(self, archive: Path, relative: str) -> None:
        root = archive.name.removesuffix(".tar.gz").removesuffix(".zip")
        if archive.name.endswith(".zip"):
            import zipfile

            with zipfile.ZipFile(archive) as source:
                entries = [(info, source.read(info)) for info in source.infolist()]
            with zipfile.ZipFile(archive, "w") as output:
                for info, data in entries:
                    if info.filename != f"{root}/{relative}":
                        output.writestr(info, data)
            return
        with tarfile.open(archive, "r:gz") as source:
            entries = [
                (member, source.extractfile(member).read() if member.isfile() else None)
                for member in source.getmembers()
            ]
        with tarfile.open(archive, "w:gz") as output:
            for member, data in entries:
                if member.name != f"{root}/{relative}":
                    output.addfile(member, io.BytesIO(data) if data is not None else None)

    def test_every_archive_must_still_carry_the_lgpl_material(self) -> None:
        for platform, arch in (("linux", "aarch64"), ("macos", "x86_64"), ("windows", "arm64")):
            for relative in (
                collect_release.LIBUSB_LICENSE_NAME,
                collect_release.LIBUSB_SOURCE_NAME,
                "share/doc/libaoahid/NOTICE",
                "share/doc/libaoahid/THIRD_PARTY_NOTICES.md",
            ):
                with self.subTest(platform=platform, arch=arch, missing=relative):
                    directory = self.fresh()
                    archive = directory / generate_spdx.release_archive_name("0.1.0", platform, arch)
                    self.repack_without(archive, relative)
                    code, error = collect(directory)
                    self.assertEqual(code, 1)
                    self.assertIn(f"missing {relative}", error)
                    self.assertFalse((directory / "SHA256SUMS").exists())

    def test_an_archive_without_one_component_is_refused(self) -> None:
        for relative in (
            "lib/libaoahid_static.a",
            "lib/cmake/aoahid/aoahid-shared-targets.cmake",
            "lib/pkgconfig/aoahid-static.pc",
        ):
            with self.subTest(missing=relative), tempfile.TemporaryDirectory() as temporary:
                code, error = package_fixture(Path(temporary), "linux", "x86_64", omit=relative)
                self.assertEqual(code, 1)
                self.assertIn("package-release: error", error)
            with self.subTest(collect_missing=relative):
                directory = self.fresh()
                archive = directory / generate_spdx.release_archive_name("0.1.0", "linux", "x86_64")
                self.repack_without(archive, relative)
                self.assertEqual(collect(directory)[0], 1)

    def test_stale_per_flavour_assets_are_rejected(self) -> None:
        directory = self.fresh()
        (directory / "libaoahid-0.1.0-linux-x86_64-ubuntu22.04-shared.tar.gz").write_bytes(b"old")
        code, error = collect(directory)
        self.assertEqual(code, 1)
        self.assertIn("unexpected release artifacts", error)

    def records(self) -> tuple[list[generate_spdx.ArchiveRecord], generate_spdx.SourceRecord]:
        archives = [
            collect_release.archive_record(
                self.dist / generate_spdx.release_archive_name("0.1.0", platform, arch),
                platform,
                arch,
            )
            for platform, arch, _, _ in collect_release.TARGETS
        ]
        source_path = self.dist / package_source.source_archive_name("0.1.0")
        return archives, generate_spdx.SourceRecord(
            name=source_path.name, sha256=collect_release.sha256(source_path)
        )

    def validate(self, document: object) -> str:
        archives, source = self.records()
        return generate_spdx.validate_release_document(
            document,
            archives,
            source,
            version="0.1.0",
            source_sha=SOURCE_SHA,
            created_epoch=EPOCH,
            libusb_version="1.0.30",
        )

    def test_tampered_release_sbom_is_rejected(self) -> None:
        original = json.loads((self.dist / "libaoahid-0.1.0.spdx.json").read_text(encoding="utf-8"))
        self.validate(original)
        linux = generate_spdx.release_package_id("linux", "x86_64")

        def package(document: dict, identifier: str) -> dict:
            return next(p for p in document["packages"] if p["SPDXID"] == identifier)

        document = json.loads(json.dumps(original))
        package(document, linux)["checksums"][0]["checksumValue"] = "0" * 64
        with self.assertRaisesRegex(generate_spdx.SpdxError, "wrong checksum"):
            self.validate(document)

        document = json.loads(json.dumps(original))
        package(document, linux)["licenseInfoFromFiles"] = ["MIT"]
        with self.assertRaisesRegex(
            generate_spdx.SpdxError,
            "no packaged file is attributed to the bundled libusb runtime",
        ):
            self.validate(document)

        document = json.loads(json.dumps(original))
        document["packages"] = [
            p for p in document["packages"] if p["SPDXID"] != generate_spdx.release_package_id("windows", "arm64")
        ]
        with self.assertRaisesRegex(generate_spdx.SpdxError, "no package for"):
            self.validate(document)

        document = json.loads(json.dumps(original))
        document["files"] = document["files"][1:]
        with self.assertRaisesRegex(generate_spdx.SpdxError, "does not exactly describe"):
            self.validate(document)

        document = json.loads(json.dumps(original))
        for item in document["files"]:
            if item["fileName"].endswith("/" + collect_release.LIBUSB_LICENSE_NAME):
                item["licenseConcluded"] = "MIT"
                item["licenseInfoInFiles"] = ["MIT"]
        with self.assertRaisesRegex(generate_spdx.SpdxError, "does not exactly describe"):
            self.validate(document)

    def test_release_sbom_requires_libusb_attribution_per_archive(self) -> None:
        archives, source = self.records()
        unattributed = generate_spdx.ArchiveRecord(
            platform=archives[0].platform,
            arch=archives[0].arch,
            name=archives[0].name,
            sha256=archives[0].sha256,
            files={
                relative: digests
                for relative, digests in archives[0].files.items()
                if generate_spdx.file_license(relative)[0] != generate_spdx.LIBUSB_LICENSE
            },
        )
        with self.assertRaisesRegex(generate_spdx.SpdxError, "is missing or has the wrong SHA-256"):
            generate_spdx.create_release_document(
                [unattributed, *archives[1:]],
                source,
                version="0.1.0",
                source_sha=SOURCE_SHA,
                created_epoch=EPOCH,
                libusb_version="1.0.30",
            )
        with mock.patch.object(
            generate_spdx, "file_license", return_value=("MIT", generate_spdx.FIRST_PARTY_COPYRIGHT)
        ):
            with self.assertRaisesRegex(
                generate_spdx.SpdxError,
                "no packaged file is attributed to the bundled libusb runtime",
            ):
                generate_spdx.create_release_document(
                    archives,
                    source,
                    version="0.1.0",
                    source_sha=SOURCE_SHA,
                    created_epoch=EPOCH,
                    libusb_version="1.0.30",
                )


class DocReferenceTests(unittest.TestCase):
    def make_tree(self, root: Path, document: str, allowlist: str = "") -> None:
        (root / "include").mkdir()
        (root / "include" / "aoahid.h").write_text(
            "int aoahid_real_call(void);\n#define AOAHID_REAL_VALUE 1\n", encoding="utf-8"
        )
        (root / "include" / "aoahid.hpp").write_text(
            "namespace aoahid { inline int real_call() { return 0; } }\n", encoding="utf-8"
        )
        (root / "src").mkdir()
        (root / "src" / "present.cpp").write_text("// SPDX\n", encoding="utf-8")
        (root / "docs").mkdir()
        (root / "docs" / "PAGE.md").write_text(document, encoding="utf-8")
        if allowlist:
            (root / "tools").mkdir()
            (root / "tools" / "doc_reference_allowlist.txt").write_text(
                allowlist, encoding="utf-8"
            )

    def problems(self, document: str, allowlist: str = "") -> list[str]:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.make_tree(root, document, allowlist)
            return [str(problem) for problem in check_doc_references.find_problems(root)]

    def test_existing_identifiers_and_paths_pass(self) -> None:
        document = (
            "Call `aoahid_real_call`, read `AOAHID_REAL_VALUE`, use `aoahid::real_call`,\n"
            "any `AOAHID_REAL_*` or `AOAHID_` prefix, and see `src/present.cpp:3`.\n"
            "```c\naoahid_real_call();\n```\n"
        )
        self.assertEqual(self.problems(document), [])

    def test_missing_identifier_is_reported(self) -> None:
        problems = self.problems("Use `aoahid::default_device_options()`.\n")
        self.assertEqual(len(problems), 1)
        self.assertIn("docs/PAGE.md:1", problems[0])
        self.assertIn("aoahid::default_device_options", problems[0])

    def test_missing_identifier_in_fenced_code_is_reported(self) -> None:
        problems = self.problems("```c\naoahid_gone();\n```\n")
        self.assertEqual(len(problems), 1)
        self.assertIn("docs/PAGE.md:2", problems[0])

    def test_missing_path_is_reported(self) -> None:
        problems = self.problems("See `src/absent.cpp`.\n")
        self.assertEqual(len(problems), 1)
        self.assertIn("path does not exist", problems[0])

    def test_allowlist_needs_a_reason(self) -> None:
        document = "See `include/linux/hid.h` and `aoahid_external`.\n"
        self.assertEqual(
            self.problems(document, "include/linux/hid.h # kernel path\naoahid_external # other repo\n"),
            [],
        )
        problems = self.problems(document, "include/linux/hid.h\n")
        self.assertTrue(any("needs '# <reason>'" in problem for problem in problems))

    def test_repository_documents_pass(self) -> None:
        self.assertEqual(check_doc_references.find_problems(REPOSITORY), [])


if __name__ == "__main__":
    unittest.main()
