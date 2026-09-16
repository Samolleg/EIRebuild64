from pathlib import Path
r=Path(__file__).resolve().parent;p=r/'Game/ModPackage.cpp';b=p.read_bytes()
b=b.replace(b'std::numeric_limits<float>::max()',b'(std::numeric_limits<float>::max)()')
start=b.index(b'  const float expected[4][9]');end=b.index(b'\n',start)+1;b=b[:start]+b[end:]
b=b.replace(b' int scalarCases=0,shareCases=0;',b' Json shopResults=Json::array();int scalarCases=0,shareCases=0;')
b=b.replace(b'  RecalcShops(shops,lmp);',b'  RecalcShops(shops,lmp);\r\n  Json result=Json::array();for(auto&shop:shops.shops){Json row=Json::array();for(float v:shop->fKoeffs)row.push_back(v);result.push_back(row);}shopResults.push_back(result);')
b=b.replace(b'{"scalarCases",scalarCases}',b'{"shopCoefficients",shopResults},{"scalarCases",scalarCases}')
p.write_bytes(b)
