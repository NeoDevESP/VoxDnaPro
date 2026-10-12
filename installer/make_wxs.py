#!/usr/bin/env python3
"""
Genera installer/VoxDnaPro.wxs (sintaxis WiX 3, compilable con wixl en Linux
o con candle/light en Windows) a partir de una carpeta "stage":

  stage/
    vst3/VOX DNA PRO.vst3/...      bundle VST3 completo (Contents/x86_64-win/..., moduleinfo.json)
    app/VOX DNA PRO.exe            versión Standalone
    docs/*.txt                     licencias y léeme

Uso:  python3 make_wxs.py <stage> <version> <salida.wxs>
Los GUID de componente son deterministas (uuid5 de la ruta) para que las
actualizaciones mayores sustituyan los archivos de forma limpia.
"""
import os, sys, uuid, re
from xml.sax.saxutils import quoteattr

UPGRADE_CODE = "6F1C2B7E-3D4A-4E5B-9A61-7C0D2E9B4F15"   # NO cambiar nunca: identifica el producto
NS = uuid.UUID("2b0f8a52-55f4-4c7b-a1f2-3f0b6b1d9e10")
MANUFACTURER = "VOX DNA"
PRODUCT = "VOX DNA PRO"

def gid(*parts):
    return str(uuid.uuid5(NS, "/".join(parts))).upper()

def ident(prefix, path):
    s = re.sub(r"[^A-Za-z0-9_.]", "_", path)
    h = uuid.uuid5(NS, path).hex[:8]
    return (prefix + "_" + s)[:60] + "_" + h

class Tree:
    def __init__(self):
        self.components = []   # ids
        self.out = []

    def emit_dir(self, root, rel, indent, feature_tag):
        full = os.path.join(root, rel) if rel else root
        for name in sorted(os.listdir(full)):
            p = os.path.join(full, name)
            r = os.path.join(rel, name) if rel else name
            key = feature_tag + "/" + r
            if os.path.isdir(p):
                self.out.append(f'{indent}<Directory Id="{ident("D", key)}" Name={quoteattr(name)}>')
                self.emit_dir(root, r, indent + "  ", feature_tag)
                self.out.append(f'{indent}</Directory>')
            else:
                cid = ident("C", key)
                self.components.append((feature_tag, cid))
                self.out.append(f'{indent}<Component Id="{cid}" Guid="{gid(key)}" Win64="yes">')
                self.out.append(f'{indent}  <File Id="{ident("F", key)}" Name={quoteattr(name)} Source={quoteattr(p)} KeyPath="yes"/>')
                self.out.append(f'{indent}</Component>')

def main():
    stage, version, out = sys.argv[1], sys.argv[2], sys.argv[3]
    stage = os.path.abspath(stage)
    t = Tree()
    vst = os.path.join(stage, "vst3")
    app = os.path.join(stage, "app")
    docs = os.path.join(stage, "docs")

    # --- VST3: Common Files\VST3\VOX DNA PRO.vst3\...
    t.out.append('      <Directory Id="CommonFiles64Folder">')
    t.out.append('        <Directory Id="VST3DIR" Name="VST3">')
    t.emit_dir(vst, "", "          ", "vst3")
    t.out.append('        </Directory>')
    t.out.append('      </Directory>')
    vst_xml = "\n".join(t.out); t.out = []

    # --- Standalone + documentación: Program Files\VOX DNA\VOX DNA PRO
    t.out.append('      <Directory Id="ProgramFiles64Folder">')
    t.out.append(f'        <Directory Id="MFGDIR" Name="{MANUFACTURER}">')
    t.out.append(f'          <Directory Id="INSTALLDIR" Name="{PRODUCT}">')
    exe = os.path.join(app, "VOX DNA PRO.exe")
    t.components.append(("app", "C_App"))
    t.out.append(f'            <Component Id="C_App" Guid="{gid("app/exe")}" Win64="yes">')
    t.out.append(f'              <File Id="F_AppExe" Name="VOX DNA PRO.exe" Source={quoteattr(exe)} KeyPath="yes"/>')
    t.out.append('            </Component>')
    if os.path.isdir(docs):
        t.out.append('            <Directory Id="DOCSDIR" Name="Licencias">')
        t.emit_dir(docs, "", "              ", "app")
        t.out.append('            </Directory>')
    t.out.append('          </Directory>')
    t.out.append('        </Directory>')
    t.out.append('      </Directory>')
    # acceso directo en el menú Inicio (componente con clave de registro como KeyPath)
    t.out.append('      <Directory Id="ProgramMenuFolder">')
    t.out.append(f'        <Directory Id="MENUDIR" Name="{PRODUCT}">')
    t.components.append(("app", "C_Shortcut"))
    t.out.append(f'          <Component Id="C_Shortcut" Guid="{gid("app/shortcut")}" Win64="yes">')
    t.out.append(f'            <Shortcut Id="S_App" Name="{PRODUCT}" Description="Plugin vocal (versión Standalone)" Target="[INSTALLDIR]VOX DNA PRO.exe" WorkingDirectory="INSTALLDIR"/>')
    t.out.append('            <RemoveFolder Id="RF_Menu" On="uninstall"/>')
    t.out.append(f'            <RegistryValue Root="HKLM" Key="Software\\{MANUFACTURER}\\{PRODUCT}" Name="Shortcut" Type="integer" Value="1" KeyPath="yes"/>')
    t.out.append('          </Component>')
    t.out.append('        </Directory>')
    t.out.append('      </Directory>')
    app_xml = "\n".join(t.out)

    def refs(tag):
        return "\n".join(f'      <ComponentRef Id="{c}"/>' for f, c in t.components if f == tag)

    xml = f'''<?xml version="1.0" encoding="utf-8"?>
<!-- Generado por installer/make_wxs.py — no editar a mano -->
<Wix xmlns="http://schemas.microsoft.com/wix/2006/wi">
  <Product Id="*" Name="{PRODUCT}" Language="1033" Version="{version}" Manufacturer="{MANUFACTURER}" UpgradeCode="{UPGRADE_CODE}">
    <Package InstallerVersion="500" Compressed="yes" InstallScope="perMachine"
             Description="{PRODUCT} {version} — plugin vocal VST3 + Standalone" Manufacturer="{MANUFACTURER}"/>
    <MajorUpgrade DowngradeErrorMessage="Ya hay instalada una versión más nueva de {PRODUCT}." AllowSameVersionUpgrades="yes"/>
    <Media Id="1" Cabinet="voxdna.cab" EmbedCab="yes"/>

    <Icon Id="AppIcon" SourceFile={quoteattr(exe)}/>
    <Property Id="ARPPRODUCTICON" Value="AppIcon"/>
    <Property Id="ARPCOMMENTS" Value="Plugin vocal todo-en-uno (VST3 y Standalone)"/>
    <Property Id="ARPNOMODIFY" Value="1"/>

    <Directory Id="TARGETDIR" Name="SourceDir">
{vst_xml}
{app_xml}
    </Directory>

    <Feature Id="VST3" Title="Plugin VST3" Level="1">
{refs("vst3")}
    </Feature>
    <Feature Id="Standalone" Title="Aplicación Standalone" Level="1">
{refs("app")}
    </Feature>
  </Product>
</Wix>
'''
    with open(out, "w", encoding="utf-8") as f:
        f.write(xml)
    print(f"{out}: {len(t.components)} componentes")

if __name__ == "__main__":
    main()
