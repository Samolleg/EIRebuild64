"""Deterministic rigid GLB fixture; no game/content writes."""
import argparse
import copy
import json
from pathlib import Path
import struct
import zlib

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
    bad=copy.deepcopy(document);bad['materials'][0]['extensions']={'KHR_materials_transmission':{'transmissionFactor':1}};save('unsupported-material',bad)
    save('bad-index',document,binary[:576]+struct.pack('<H',60000)+binary[578:])
    bad=copy.deepcopy(document);bad['nodes'][0]['scale']=[2,1,3];save('scaled',bad)
    multiple=copy.deepcopy(document);multiple['meshes'][0]['primitives']*=2;save('multipart',multiple)
    shaped=copy.deepcopy(document);shaped['asset']['extras']['ei'].update(shape='trilinear8',wind={'trunkRadius':.2,'trunkHeight':.5,'height':4})
    shaped['nodes'][0]['extras']['ei'].update(foliage=True)
    collider=shaped['nodes'][0]['extras']['ei']['collision'];collider['role']='crown';collider['shapes']=[]
    payload=bytearray(binary);targets=[]
    for s in range(8):
        sx=1+(s&1);sy=1+((s>>1)&1);sz=1+((s>>2)&1)
        collider['shapes'].append({'min':[-sx,-sy,0],'max':[sx,sy,2*sz]})
        if not s:continue
        at=len(payload);values=[(x*(sx-1),y*(sz-1),z*(sy-1)) for x,y,z in positions]
        payload.extend(b''.join(struct.pack('<3f',*v) for v in values));view=len(shaped['bufferViews']);shaped['bufferViews'].append({'buffer':0,'byteOffset':at,'byteLength':len(payload)-at})
        a=len(shaped['accessors']);shaped['accessors'].append({'bufferView':view,'componentType':5126,'count':24,'type':'VEC3','min':[min(v[k] for v in values) for k in range(3)],'max':[max(v[k] for v in values) for k in range(3)]});targets.append({'POSITION':a})
    shaped['meshes'][0]['primitives'][0]['targets']=targets;shaped['buffers'][0]['byteLength']=len(payload);save('forms-wind',shaped,bytes(payload))
    pbr=copy.deepcopy(document);pbr['materials'][0].update(alphaMode='MASK',alphaCutoff=.3,doubleSided=True,emissiveFactor=[.2,0,0]);pbr['materials'][0]['pbrMetallicRoughness'].update(metallicFactor=.4,roughnessFactor=.6);save('pbr',pbr)
    tex=copy.deepcopy(document);payload=bytearray(binary)
    tex['bufferViews'].append({'buffer':0,'byteOffset':len(payload),'byteLength':192});payload.extend(struct.pack('<48f',*([0,0,1,0,1,1,0,1]*6)))
    tex['accessors'].append({'bufferView':3,'componentType':5126,'count':24,'type':'VEC2'});tex['meshes'][0]['primitives'][0]['attributes']['TEXCOORD_0']=3
    def chunk(kind,body):return struct.pack('>I',len(body))+kind+body+struct.pack('>I',zlib.crc32(kind+body)&0xffffffff)
    png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',2,2,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(bytes([0,255,0,0,255,0,255,0,0,0,0,0,255,255,255,255,255,255])))+chunk(b'IEND',b'')
    tex['bufferViews'].append({'buffer':0,'byteOffset':len(payload),'byteLength':len(png)});payload.extend(png)
    tex['images']=[{'bufferView':4,'mimeType':'image/png'}];tex['textures']=[{'source':0}]
    tex['materials'][0]['pbrMetallicRoughness']['baseColorTexture']={'index':0};tex['materials'][0].update(alphaMode='MASK',doubleSided=True)
    tex['buffers'][0]['byteLength']=len(payload);save('textured',tex,bytes(payload))
    (args.output/'registry.v1.json').write_text(json.dumps({'version':1,'characters':{},'items':{},'environment':{
        'sgcrate01':{'profile':'environment.v1','resource':'models/environment/sgcrate01.glb'}}},indent=2),encoding='utf8')
    print('Created rigid crate and malformed fixtures:',args.output)
if __name__=='__main__':main()
