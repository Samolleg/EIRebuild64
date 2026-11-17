"""Deterministic rigid GLB fixture; no game/content writes."""
import argparse
import copy
import json
from pathlib import Path
import struct

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    args.output.mkdir(parents=True,exist_ok=False)
    faces=[([(1,0,-1),(1,2,-1),(1,2,1),(1,0,1)],(1,0,0)),
           ([(-1,0,1),(-1,2,1),(-1,2,-1),(-1,0,-1)],(-1,0,0)),
           ([(-1,2,-1),(-1,2,1),(1,2,1),(1,2,-1)],(0,1,0)),
           ([(-1,0,1),(-1,0,-1),(1,0,-1),(1,0,1)],(0,-1,0)),
           ([(-1,0,1),(1,0,1),(1,2,1),(-1,2,1)],(0,0,1)),
           ([(1,0,-1),(-1,0,-1),(-1,2,-1),(1,2,-1)],(0,0,-1))]
    positions=[v for face,n in faces for v in face]
    normals=[n for face,n in faces for _ in face]
    indices=[4*f+i for f in range(6) for i in (0,1,2,0,2,3)]
    binary=b''.join(struct.pack('<3f',*p) for p in positions+normals)+struct.pack('<36H',*indices)
    document={'asset':{'version':'2.0','extras':{'ei':{'profile':'environment.v1','kind':'environment'}}},
        'scene':0,'scenes':[{'nodes':[0]}],
        'nodes':[{'name':'crate','mesh':0,'extras':{'ei':{'collision':{'role':'solid','min':[-1,-1,0],'max':[1,1,2]}}}}],
        'buffers':[{'byteLength':len(binary)}],
        'bufferViews':[{'buffer':0,'byteOffset':0,'byteLength':288},{'buffer':0,'byteOffset':288,'byteLength':288},
                       {'buffer':0,'byteOffset':576,'byteLength':72}],
        'accessors':[{'bufferView':0,'componentType':5126,'count':24,'type':'VEC3','min':[-1,0,-1],'max':[1,2,1]},
                     {'bufferView':1,'componentType':5126,'count':24,'type':'VEC3'},
                     {'bufferView':2,'componentType':5123,'count':36,'type':'SCALAR'}],
        'materials':[{'pbrMetallicRoughness':{'baseColorFactor':[.35,.12,.035,1],'metallicFactor':0,'roughnessFactor':1}}],
        'meshes':[{'primitives':[{'attributes':{'POSITION':0,'NORMAL':1},'indices':2,'material':0}]}]}
    def save(name,doc,payload=binary):
        text=json.dumps(doc,separators=(',',':')).encode();text+=b' '*((-len(text))%4)
        payload+=b'\0'*((-len(payload))%4)
        glb=struct.pack('<III',0x46546c67,2,28+len(text)+len(payload))+struct.pack('<II',len(text),0x4e4f534a)+text+struct.pack('<II',len(payload),0x004e4942)+payload
        (args.output/(name+'.glb')).write_bytes(glb)
    save('sgcrate01',document)
    bad=copy.deepcopy(document);del bad['nodes'][0]['extras'];save('missing-collider',bad)
    bad=copy.deepcopy(document);bad['nodes'][0]['scale']=[0,1,1];save('singular',bad)
    bad=copy.deepcopy(document);bad['skins']=[{'joints':[0]}];save('skin',bad)
    bad=copy.deepcopy(document);bad['nodes'][0]['extras']['ei']['collision']['max']=[-2,1,2];save('inverted-collider',bad)
    bad=copy.deepcopy(document);bad['materials'][0]['extensions']={'KHR_materials_unlit':{}};save('unsupported-material',bad)
    save('bad-index',document,binary[:576]+struct.pack('<H',60000)+binary[578:])
    bad=copy.deepcopy(document);bad['nodes'][0]['scale']=[2,1,3];save('scaled',bad)
    (args.output/'registry.v1.json').write_text(json.dumps({'version':1,'characters':{},'items':{},'environment':{
        'sgcrate01':{'profile':'environment.v1','resource':'models/environment/sgcrate01.glb'}}},indent=2),encoding='utf8')
    print('Created rigid crate and malformed fixtures:',args.output)
if __name__=='__main__':main()
