#pragma once
#include "NalaMorphResolver.h"
#include "NalaBodyAuthoring.h"
#include "NalaUpperBody.h"
// Include the accepted NalaFace and UnityBodyAppearance implementation first.
namespace NalaBodyShape {
inline UnityBodyAppearance::Profile Profile(const NalaMorphs::Values&v){namespace E=EditorSchema;return {v[E::fat],v[E::legs],v[E::arms],v[E::waist],v[E::waistDepth],v[E::breasts],.9309906028352594*v[E::height]};}
template<class FaceCorrection> inline bool Build(const UnityBody::Data&source,const NalaMorphs::Values&v,const std::vector<double>&bones,UnityBody::Data&shaped,UnityBodyAppearance::Profile&profile,FaceCorrection additionalFace){
 profile=Profile(v);UnityBodyAppearance::Morph(source,profile.fat,shaped);NalaFace::Apply(shaped);additionalFace(shaped);
 NalaUpperBody::Width=.30*v[EditorSchema::shoulders];NalaUpperBody::Apply(shaped);
 return NalaEditor::Apply(shaped,std::vector<double>(v.begin(),v.end()),bones,profile.scale);
}
inline bool Build(const UnityBody::Data&source,const NalaMorphs::Values&v,UnityBody::Data&shaped,UnityBodyAppearance::Profile&profile){return Build(source,v,std::vector<double>(EditorSchema::BoneCount*9),shaped,profile,[](UnityBody::Data&){});}
}
