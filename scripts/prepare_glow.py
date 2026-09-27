"""Build a local shader variant from the user's installed game; never edits the installation."""
import copy
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / '.tools' / 'pydeps'))
import UnityPy

if len(sys.argv) < 2:
    raise SystemExit('Usage: python scripts/prepare_glow.py <path-to-game-assetbundle>')
source = Path(sys.argv[1])
raw = source.read_bytes()
offset = raw.find(b'UnityFS\0')
if offset < 0:
    raise RuntimeError('Not a UnityFS bundle')
env = UnityPy.load(raw[offset:])
changed = False
shader_id = None
for obj in env.objects:
    if obj.type.name == 'AssetBundle':
        tree = obj.read_typetree()
        tree['m_Name'] = 'scarletaim-local-glow'
        if 'm_AssetBundleName' in tree:
            tree['m_AssetBundleName'] = 'scarletaim-local-glow'
        obj.save_typetree(tree)
    elif obj.type.name == 'Shader':
        tree = obj.read_typetree()
        shader = tree.get('m_ParsedForm', {})
        if 'CharacterOverlayContour' not in shader.get('m_Name', ''):
            continue
        shader['m_Name'] = 'ScarletAim/ModelGlow'
        prop = copy.deepcopy(shader['m_PropInfo']['m_Props'][2])
        prop.update(m_Name='_ZTest', m_Description='Depth test')
        prop['m_DefValue[0]'] = 8.0
        shader['m_PropInfo']['m_Props'].append(prop)
        for sub in shader['m_SubShaders']:
            for shader_pass in sub['m_Passes']:
                state = shader_pass['m_State']
                state['zTest'].update(name='_ZTest', val=8.0)
                state['zWrite']['val'] = 0.0
                state['m_Tags']['tags'] = [
                    (key, 'LightweightForward' if key.lower() == 'lightmode'
                     else 'Transparent+100' if key.lower() == 'queue' else value)
                    for key, value in state['m_Tags']['tags']]
        obj.save_typetree(tree)
        changed = True
        shader_id = obj.path_id
if not changed:
    raise RuntimeError('Expected shader missing; check installed game build')
for obj in list(env.objects):
    if obj.type.name == 'AssetBundle':
        tree = obj.read_typetree()
        pointer = {'m_FileID': 0, 'm_PathID': shader_id}
        tree['m_PreloadTable'] = [pointer]
        tree['m_Container'] = [('scarletaim/modelglow.shader', {
            'preloadIndex': 0, 'preloadSize': 1, 'asset': pointer})]
        obj.save_typetree(tree)
    elif obj.path_id != shader_id:
        del obj.assets_file.objects[obj.path_id]
for bundle in env.files.values():
    for old_name in list(bundle.files):
        entry = bundle.files.pop(old_name)
        new_name = 'CAB-scarletaim-local-glow' + ('.resS' if old_name.endswith('.resS') else '')
        entry.name = new_name
        bundle.files[new_name] = entry
    (ROOT / 'bin' / 'glow.bundle').write_bytes(bundle.save(packer='lz4'))
print('Created bin/glow.bundle from the local game installation.')
