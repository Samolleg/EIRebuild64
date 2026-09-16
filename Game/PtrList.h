#pragma once
/*==========================================================================================================

        Class to store pointers in double linked list header and implementation

        Written by Ivanov Evgeny
        Nival Interactive, 1998

==========================================================================================================*/

const int N_ALLOC_BLOCK_NUM = 1000;

struct SSimplyPtrNodeBlock;
struct SSimplyPtrNode
{
    SSimplyPtrNode *pNext;
    SSimplyPtrNode *pPrev;
    SSimplyPtrNodeBlock *pParent;
    LPVOID data;
};

struct SSimplyPtrNodeBlock
{
    int nFreeCount;
    SSimplyPtrNodeBlock *pNext, *pPrev;
    SSimplyPtrNode *pFree;
    SSimplyPtrNode root[N_ALLOC_BLOCK_NUM];
};

template <class PTYPE>
class CSimplyPtrList
{
protected:
    SSimplyPtrNode *m_pNodeHead;

protected:
    SSimplyPtrNode *NewNode(SSimplyPtrNode *, SSimplyPtrNode *);
    void FreeNode(SSimplyPtrNode *);

public:
    CSimplyPtrList();
    ~CSimplyPtrList();
    //
    BOOL IsEmpty() const { return m_pNodeHead == NULL; }
    // peek at head
    PTYPE GetHead() const;
    // remove at head
    PTYPE RemoveHead();
    // add before head
    POSITION AddHead(PTYPE newElement);
    // remove all elements
    void RemoveAll();
    // iteration
    POSITION GetHeadPosition() const;
    PTYPE GetNext(POSITION &rPosition) const;
    // getting/modifying an element at a given position
    void RemoveAt(POSITION position);
};

extern SSimplyPtrNodeBlock *pFreeBlocks;
extern SSimplyPtrNodeBlock *pUsedBlocks;

/////////////////////////////////////////////////////////////////////////////
// Node helpers
//

template <class PTYPE>
SSimplyPtrNode *CSimplyPtrList<PTYPE>::NewNode(SSimplyPtrNode *pPrev, SSimplyPtrNode *pNext)
{
    if (pFreeBlocks == NULL)
    {
        // Add new block
        pFreeBlocks = new SSimplyPtrNodeBlock;
        pFreeBlocks->pNext = NULL;
        pFreeBlocks->pPrev = NULL;
        pFreeBlocks->pFree = pFreeBlocks->root;
        pFreeBlocks->nFreeCount = N_ALLOC_BLOCK_NUM;
        SSimplyPtrNode *pNode = pFreeBlocks->root;
        for (int i = 0; i < N_ALLOC_BLOCK_NUM - 1; i++, pNode++)
        {
            pNode->pNext = pNode + 1;
            pNode->pParent = pFreeBlocks;
        }
        pNode->pNext = NULL;
        pNode->pParent = pFreeBlocks;
    }
    assert(pFreeBlocks->nFreeCount > 0);
    SSimplyPtrNode *pNode = pFreeBlocks->pFree;
    pFreeBlocks->pFree = pNode->pNext;
    if (--(pFreeBlocks->nFreeCount) == 0)
    {
        if (pFreeBlocks->pNext)
            pFreeBlocks->pNext->pPrev = NULL;
        if (pUsedBlocks)
            pUsedBlocks->pPrev = pFreeBlocks;
        SSimplyPtrNodeBlock *pNextFree = pFreeBlocks->pNext;
        pFreeBlocks->pNext = pUsedBlocks;
        pFreeBlocks->pPrev = NULL;
        pUsedBlocks = pFreeBlocks;
        pFreeBlocks = pNextFree;
    }
    pNode->pNext = pNext;
    pNode->pPrev = pPrev;
    return pNode;
}

template <class PTYPE>
void CSimplyPtrList<PTYPE>::FreeNode(SSimplyPtrNode *pNode)
{
    SSimplyPtrNodeBlock *pParent = pNode->pParent;

    pNode->pNext = pParent->pFree;
    pParent->pFree = pNode;
    (pParent->nFreeCount)++;

    if (pParent->nFreeCount == 1)
    {
        if (pParent->pPrev)
            pParent->pPrev->pNext = pParent->pNext;
        else
            pUsedBlocks = pParent->pNext;
        if (pParent->pNext)
            pParent->pNext->pPrev = pParent->pPrev;

        if (pFreeBlocks)
            pFreeBlocks->pPrev = pParent;
        pParent->pNext = pFreeBlocks;
        pParent->pPrev = NULL;
        pFreeBlocks = pParent;
    }
    else if (pParent->nFreeCount == N_ALLOC_BLOCK_NUM)
    {
        if (pParent->pPrev)
            pParent->pPrev->pNext = pParent->pNext;
        else
            pFreeBlocks = pParent->pNext;
        if (pParent->pNext)
            pParent->pNext->pPrev = pParent->pPrev;
        delete pParent;
    }
}

/////////////////////////////////////////////////////////////////////////////
// CSimplyPtrList<PTYPE> inline functions
//

template <class PTYPE>
inline PTYPE CSimplyPtrList<PTYPE>::GetHead() const
{
    ASSERT(m_pNodeHead != NULL);
    return m_pNodeHead->data;
}

template <class PTYPE>
inline POSITION CSimplyPtrList<PTYPE>::GetHeadPosition() const
{
    return (POSITION)m_pNodeHead;
}

template <class PTYPE>
inline PTYPE CSimplyPtrList<PTYPE>::GetNext(POSITION &rPosition) const
{
    SSimplyPtrNode *pNode = (SSimplyPtrNode *)rPosition;
    ASSERT(AfxIsValidAddress(pNode, sizeof(SSimplyPtrNode)));
    rPosition = (POSITION)pNode->pNext;
    return PTYPE(pNode->data);
}

template <class PTYPE>
CSimplyPtrList<PTYPE>::CSimplyPtrList()
{
    m_pNodeHead = NULL;
}

template <class PTYPE>
void CSimplyPtrList<PTYPE>::RemoveAll()
{
    while (m_pNodeHead)
    {
        SSimplyPtrNode *pNextNode = m_pNodeHead->pNext;
        FreeNode(m_pNodeHead);
        m_pNodeHead = pNextNode;
    }
}

template <class PTYPE>
CSimplyPtrList<PTYPE>::~CSimplyPtrList()
{
    RemoveAll();
}

template <class PTYPE>
POSITION CSimplyPtrList<PTYPE>::AddHead(PTYPE newElement)
{
    SSimplyPtrNode *pNewNode = NewNode(NULL, m_pNodeHead);
    pNewNode->data = newElement;
    if (m_pNodeHead != NULL)
        m_pNodeHead->pPrev = pNewNode;
    m_pNodeHead = pNewNode;
    return (POSITION)pNewNode;
}

template <class PTYPE>
PTYPE CSimplyPtrList<PTYPE>::RemoveHead()
{
    ASSERT(m_pNodeHead != NULL); // don't call on empty list !!!
    ASSERT(AfxIsValidAddress(m_pNodeHead, sizeof(SSimplyPtrNode)));

    SSimplyPtrNode *pOldNode = m_pNodeHead;
    PTYPE returnValue = PTYPE(pOldNode->data);

    m_pNodeHead = pOldNode->pNext;
    if (m_pNodeHead != NULL)
        m_pNodeHead->pPrev = NULL;
    FreeNode(pOldNode);
    return returnValue;
}

template <class PTYPE>
void CSimplyPtrList<PTYPE>::RemoveAt(POSITION position)
{
    SSimplyPtrNode *pOldNode = (SSimplyPtrNode *)position;
    ASSERT(AfxIsValidAddress(pOldNode, sizeof(SSimplyPtrNode)));

    // remove pOldNode from list
    if (pOldNode == m_pNodeHead)
    {
        m_pNodeHead = pOldNode->pNext;
    }
    else
    {
        ASSERT(AfxIsValidAddress(pOldNode->pPrev, sizeof(SSimplyPtrNode)));
        pOldNode->pPrev->pNext = pOldNode->pNext;
    }
    if (pOldNode->pNext != NULL)
    {
        ASSERT(AfxIsValidAddress(pOldNode->pNext, sizeof(SSimplyPtrNode)));
        pOldNode->pNext->pPrev = pOldNode->pPrev;
    }
    FreeNode(pOldNode);
}
