#pragma once
// DirElement.h: interface for the CDirElement class.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "MapEditObject.h"

enum
{
    NT_GROUP,
    NT_TEMPLATE,
    NT_PROTEMPLATE,
    NT_PREDEF_TEMPLATE,
    NT_PROINSTANCE
};
const int DEF_FOLDER = -10;

struct SMobFile
{
public:
    CString szFileName;
    bool isRange;
    bool isModify;
    int nMinIndex;
    int nMaxIndex;

public:
    SMobFile()
    {
        szFileName = "none";
        isRange = false;
        nMinIndex = nMaxIndex = 0;
        isModify = false;
    }
    SMobFile(CString szName)
    {
        szFileName = szName;
        isRange = false;
        nMinIndex = nMaxIndex = 0;
        isModify = true;
    }
};

struct STreeProxyNode
{
public:
    HTREEITEM hTreeItem;
    int nType;
    int nItem; // Number in pro/template map
public:
    STreeProxyNode(STreeProxyNode &node) { *this = node; }
    STreeProxyNode()
    {
        hTreeItem = NULL;
        nItem = -1;
        nType = NT_GROUP;
    }
    inline void operator=(const STreeProxyNode &node);
};
/*
struct SInstanseProxyNode
{
public :
        int				nTreeNode;		// Nubber in
        int				nListItem;		// Nubber in list View
        int				nObj;			// Number in map edit object map
public :
        SInstanseProxyNode( SInstanseProxyNode &node){ *this = node;}
        SInstanseProxyNode(){ nTreeNode = nListItem = nObj = -1;}
        inline void operator=( const SInstanseProxyNode& node);
};
*/
enum
{
    DE_ROOT,
    DE_FOLDER,
    DE_ELEMENT
};
enum
{
    IT_OBJECT,
    IT_LIGHT,
    IT_SOUND,
    IT_PARTICLE
};

class CDirElement
{
public:
    CString szName;
    int nType; // Elemen type
    int nInst; // Original Instant number
    HTREEITEM hTreeDirItem;
    int nParentFolder;

public:
    inline CDirElement()
    {
        nType = DE_ROOT;
        nInst = -1;
        hTreeDirItem = NULL;
        szName = "none";
        nParentFolder = -1;
    }
};

typedef CHashMap<int, STreeProxyNode *> CTreeProxyNodeMap;
// typedef CHashMap<int, SInstanseProxyNode *> CInstanseProxyNodeMap;
typedef CHashMap<int, CDirElement *> CDirElementMap;
typedef CArray<SMobFile, SMobFile &> CMobFileArray;
typedef CDWordArray CInstPtrArray; // use for store selection

inline void STreeProxyNode::operator=(const STreeProxyNode &node)
{
    hTreeItem = node.hTreeItem;
    nType = node.nType;
    nItem = node.nItem;
}
/*
inline void SInstanseProxyNode::operator=( const SInstanseProxyNode& node)
{
        nTreeNode = node.nTreeNode;
        nListItem = node.nListItem;
        nObj = node.nObj;
}
*/
