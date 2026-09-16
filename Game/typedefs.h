#pragma once
/////////////////////////////////////////////////////////////////////////////////////
class CObject3D;
class CCraft;
/////////////////////////////////////////////////////////////////////////////////////
// object's ptr containers
typedef CList<CObject3D *, CObject3D *> CObjectPtrList;
typedef CArray<CObject3D *, CObject3D *> CObjectPtrArray;
// craft's ptr containers
typedef CList<CCraft *, CCraft *> CCraftPtrList;
typedef CArray<CCraft *, CCraft *> CCraftPtrArray;
// array types
typedef CSimplyArray<D3DVERTEX, D3DVERTEX &> CVertexArray;
typedef CSimplyArray<D3DLVERTEX, D3DLVERTEX> CLVertexArray;
typedef CSimplyArray<D3DTLVERTEX, D3DTLVERTEX> CTLVertexArray;
/////////////////////////////////////////////////////////////////////////////////////
