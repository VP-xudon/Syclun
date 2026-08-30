"""Package the Synth-OOP VS Code extension into a .vsix file.

把 Synth-OOP 的 VS Code 扩展打包成 .vsix 文件。

Offline by design: standard library only (`zipfile`), no network, no vsce.
This produces a standard VSIX v3 (OPC/ZIP) package that VS Code installs
directly and that `vsce publish` would accept.

离线设计：只用标准库 `zipfile`，无需联网、无需 vsce。产物是符合 VSIX v3
（OPC/ZIP）规范的标准扩展包，VS Code 可直接安装，`vsce publish` 亦可接受。

Usage / 用法：
    python vscode-synth-oop/build_vsix.py

The output is written to the project's `build/` directory as
`synth-oop-<version>.vsix`; the extension source directory is left clean.
产物写到项目 `build/` 下，名为 `synth-oop-<版本>.vsix`；扩展源目录保持干净。
"""

import datetime
import json
import os
import zipfile

# Resolve paths from this file so the script works from any clone location.
# 由本文件位置推导路径，使脚本在任何克隆位置都能工作。
EXT_DIR = os.path.dirname(os.path.abspath(__file__))
BUILD_DIR = os.path.normpath(os.path.join(EXT_DIR, "..", "build"))

# Validate package.json loads and read identity
with open(os.path.join(EXT_DIR, "package.json"), encoding="utf-8") as f:
    pkg = json.load(f)
ext_id = pkg["name"]
version = pkg["version"]
OUT_VSIX = os.path.join(BUILD_DIR, f"{ext_id}-{version}.vsix")
publisher = pkg["publisher"]
desc = pkg.get("description", "")

# Ensure the build directory exists
os.makedirs(BUILD_DIR, exist_ok=True)

# Files to bundle (relative to EXT_DIR). Dev-only .vscode/launch.json is excluded.
# 打包文件清单（相对 EXT_DIR）。仅开发用的 .vscode/launch.json 不打包。
include = [
    "package.json",
    "language-configuration.json",
    "syntaxes/synth-oop.tmLanguage.json",
    "README.md",
    "examples/demo.syn",
    "打包教程.md",
    "icons/synth-icon.png",
    "LICENSE",
]

manifest = f'''<?xml version="1.0" encoding="utf-8"?>
<PackageManifest Version="2.0.0" xmlns="http://schemas.microsoft.com/developer/vsx-schema/2011">
  <Metadata>
    <Identity Language="en-US" Id="{ext_id}" Version="{version}" Publisher="{publisher}" />
    <DisplayName>Synth-OOP</DisplayName>
    <Description xml:space="preserve">{desc}</Description>
    <Categories>Programming Languages</Categories>
    <GalleryFlags>Public</GalleryFlags>
    <InstallationTarget Id="Microsoft.VisualStudio.Code" />
    <Date>{datetime.date.today().isoformat()}</Date>
    <Assets>
      <Asset Type="Microsoft.VisualStudio.Code.Manifest" Path="extension/package.json" Addressable="true" />
    </Assets>
  </Metadata>
  <Installation>
    <InstallationTarget Id="Microsoft.VisualStudio.Code" />
  </Installation>
  <Dependencies />
  <Assets>
    <Asset Type="Microsoft.VisualStudio.Code.Manifest" Path="extension/package.json" />
  </Assets>
</PackageManifest>
'''

content_types = '''<?xml version="1.0" encoding="utf-8"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
  <Default Extension="vsixmanifest" ContentType="text/xml" />
  <Default Extension="json" ContentType="application/json" />
  <Default Extension="md" ContentType="text/markdown" />
  <Default Extension="syn" ContentType="text/plain" />
  <Default Extension="synl" ContentType="text/plain" />
  <Default Extension="syni" ContentType="text/plain" />
  <Default Extension="txt" ContentType="text/plain" />
</Types>
'''

# Pre-check all files exist
missing = [f for f in include if not os.path.isfile(os.path.join(EXT_DIR, f))]
if missing:
    raise SystemExit("MISSING FILES: " + ", ".join(missing))

# Best-effort cleanup of stale .vsix left in the extension source dir (packaging
# output now lives in build/). A failure here must not abort packaging.
# 尽力清理扩展源目录里残留的 .vsix（打包产物现在放 build/）。此步失败不应中断打包。
for fn in os.listdir(EXT_DIR):
    if fn.endswith(".vsix"):
        try:
            os.remove(os.path.join(EXT_DIR, fn))
        except OSError:
            pass

# The output is written in place: ZipFile(mode='w') truncates and replaces the
# file, so no explicit delete is needed. Avoiding the delete keeps this script
# usable in restricted/sandboxed environments where removal may be denied.
# 产物就地写入：ZipFile(mode='w') 会截断并替换文件，故无需显式删除。不做删除也让
# 本脚本在禁用删除的受限 / 沙箱环境中仍可用。
with zipfile.ZipFile(OUT_VSIX, 'w', zipfile.ZIP_DEFLATED) as z:
    z.writestr('[Content_Types].xml', content_types)
    z.writestr('extension.vsixmanifest', manifest)
    for f in include:
        full = os.path.join(EXT_DIR, f)
        z.write(full, 'extension/' + f)

size = os.path.getsize(OUT_VSIX)
print("OK wrote", OUT_VSIX, "bytes=", size)

# Verify: list contents and re-open to ensure it's a valid zip
with zipfile.ZipFile(OUT_VSIX, 'r') as z:
    bad = z.testzip()
    print("zip integrity:", "OK" if bad is None else ("BAD: " + bad))
    names = z.namelist()
    print("entries:", len(names))
    for n in names:
        print("  ", n)
