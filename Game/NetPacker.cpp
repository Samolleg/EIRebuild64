#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "NetPacker.h"
#include "Notification.h"

// Простой упаковщик данных. Использует алгоритм Хаффмэна
const int BITS_PER_WORD = 32;

struct SHuffNode
{
    SHuffNode *pLeft;
    SHuffNode *pRight;
    int iWeightLeft;
    int iWeightRight;
    BYTE nByte;
    int iWeight;

    SHuffNode() { pLeft = pRight = 0; }
};

CNetPacker::CNetPacker()
{
    LinkToPool();
    root = 0;
    nNumStatBytes = 0;
}

CNetPacker::~CNetPacker()
{
    CleanUpHuffmanTree();
    root = 0;
    nNumStatBytes = 0;
    UnLinkFromPool();
}

// Начинает сбор статистики
void CNetPacker::Statistics()
{
    memset(iStatFreqs, 0, sizeof(iStatFreqs));
    nNumStatBytes = 0;
}

// Сбор статистики
void CNetPacker::Statistics(const void *data, int dataLen)
{
    BYTE *dptr = (BYTE *)data;
    for (int i = 0; i < dataLen; i++, dptr++)
        iStatFreqs[*dptr]++;
    nNumStatBytes += dataLen;
}

// Uses statistics data to initialize
void CNetPacker::Init()
{
    BuildHuffmanTree();
    BuildCodeTables();
    return;
}

// Структура для сортировки встречаемости
struct SStatInfo
{
    BYTE nByte;
    int iFreq;
};
// Функция сравнения структур SStatInfo
int __cdecl SStatInfoCompare(const void *s1, const void *s2)
{
    if (((SStatInfo *)s1)->iFreq < ((SStatInfo *)s2)->iFreq)
        return 1;
    if (((SStatInfo *)s1)->iFreq > ((SStatInfo *)s2)->iFreq)
        return -1;
    return 0;
}
// Строит дерево хафмена
void CNetPacker::BuildHuffmanTree()
{
    SStatInfo sInfo[256];
    int i;
    for (i = 0; i < 256; i++)
    {
        sInfo[i].nByte = i;
        sInfo[i].iFreq = iStatFreqs[i];
    };

    qsort(sInfo, 256, sizeof(SStatInfo), SStatInfoCompare);
    CleanUpHuffmanTree();

    root = GetHuffNode();
    assert(root);
    root->iWeight = sInfo[0].iFreq + 1;
    root->nByte = sInfo[0].nByte;
    for (i = 1; i < 256; i++)
        AddToHuffmanTree(root, sInfo[i].nByte, sInfo[i].iFreq + 1);
}
// Добавляет новое значение в дерево хафмена
void CNetPacker::AddToHuffmanTree(SHuffNode *node, BYTE nByte, int iFreq)
{
    assert(iFreq > 0); // У узла обязан быть ненулевой вес
    if (!node->pLeft)
    {
        node->pLeft = GetHuffNode();
        assert(node->pLeft);
        node->pLeft->nByte = node->nByte;
        node->pLeft->iWeight = node->iWeight;
        node->iWeightLeft = node->iWeight;

        node->pRight = GetHuffNode();
        assert(node->pRight);
        node->pRight->nByte = nByte;
        node->pRight->iWeight = iFreq;
        node->iWeightRight = iFreq;
        return;
    }
    if (node->iWeightLeft <= node->iWeightRight)
    {
        AddToHuffmanTree(node->pLeft, nByte, iFreq);
        node->iWeightLeft += iFreq;
    }
    else
    {
        AddToHuffmanTree(node->pRight, nByte, iFreq);
        node->iWeightRight += iFreq;
    }
}
// Строит таблицы кодовых последовательностей по дереву хаффмена
void CNetPacker::BuildCodeTables()
{
    memset(iCodeLens, 0, sizeof(iCodeLens));
    memset(dwCodeBits, 0, sizeof(dwCodeBits));
    BuildCodeTables(root, 0, 0);
}
// Обходит дерево и строит кодовую таблицу
void CNetPacker::BuildCodeTables(SHuffNode *node, DWORD dwCode, int iCodeLen)
{
    assert(node);
    assert(iCodeLen <= 32);
    if (!node->pLeft && !node->pRight)
    {
        dwCodeBits[node->nByte] = dwCode >> (BITS_PER_WORD - iCodeLen);
        iCodeLens[node->nByte] = iCodeLen;
    }
    else
    {
        BuildCodeTables(node->pLeft, (dwCode >> 1) | 0x80000000, iCodeLen + 1);
        BuildCodeTables(node->pRight, (dwCode >> 1) | 0, iCodeLen + 1);
    }
}

// Печатает код для символа с
#ifdef _DEBUG
void CNetPacker::PrintCode(BYTE c)
{
    DWORD code = dwCodeBits[c];
    int len = iCodeLens[c], i, mask;
    printf("Code for character %c: ", c);

    for (i = 0, mask = 1; i < len; i++, mask <<= 1)
        printf("%i", (code & mask) >> i);
    printf("\n");
}
#endif

// Очищает дерево
void CNetPacker::CleanUpHuffmanTree()
{
    CleanUpNode(root);
    root = 0;
}
// Очищает узел дерева
void CNetPacker::CleanUpNode(SHuffNode *node)
{
    if (node)
    {
        CleanUpNode(node->pLeft);
        CleanUpNode(node->pRight);
        DelHuffNode(node);
    }
}

// Вычисляет сколько байт занимает буфер в nBits бит
int Bits2Bytes(int iBits)
{
    return (iBits >> 3) + ((iBits & 7) != 0);
}
// запаковывает блок данных,
// возвращает число _бит_ записанных в выходной буфер
// размер выходного буфера должен быть как минимум
// в 1.2 раза больше входного
const int CNetPacker::Pack(const void *_bytesIn, int bytesInTotal, void *_bitsOut)
{
    BYTE *bytesIn = (BYTE *)_bytesIn;
    DWORD *bitsOut = (DWORD *)_bitsOut;
    int bitsOutTotal = 0;
    int bitsFilled = 0;
    *bitsOut = 0;
    for (; bytesInTotal; bytesIn++, bytesInTotal--)
    {
        DWORD codeToWrite = dwCodeBits[*bytesIn];
        int bitsToWrite = iCodeLens[*bytesIn];
        bitsOutTotal += bitsToWrite;
        *bitsOut |= codeToWrite << bitsFilled;
        bitsFilled += bitsToWrite;
        if (bitsFilled >= BITS_PER_WORD)
        {
            bitsFilled -= BITS_PER_WORD;
            bitsOut++;
            *bitsOut = codeToWrite >> (BITS_PER_WORD - bitsFilled + bitsToWrite);
        }
    }
    return bitsOutTotal;
}

// распаковывает блок данных,
// возвращает число байт записанных в выходной буфер
const int CNetPacker::UnPack(const void *_bitsIn, int bitsInTotal, void *_bytesOut, int bytesOutLen)
{
    DWORD *bitsIn = (DWORD *)_bitsIn;
    BYTE *bytesOut = (BYTE *)_bytesOut;
    int bytesOutTotal = 0;
    int bitsReaded = 0;
    DWORD nCodeWord;
    for (; bitsInTotal > 0; bytesOutTotal++)
    {
        assert(root);
        SHuffNode *node = root;
        for (; node->pLeft; bitsReaded--, bitsInTotal--)
        {
            if (!bitsReaded)
            {
                bitsReaded = BITS_PER_WORD;
                nCodeWord = *bitsIn++;
            }
            if (nCodeWord & 1)
                node = node->pLeft;
            else
                node = node->pRight;
            nCodeWord >>= 1;
        }
        if (bytesOutLen)
        {
            *bytesOut = node->nByte;
            bytesOutLen--;
            bytesOut++;
        }
    }
    return bytesOutTotal;
}

// ------------------------------- Huff node pool manager
// --------------------------------------------------

SHuffNode *CNetPacker::pPool = NULL;
int CNetPacker::nPoolLinks = 0;

SHuffNode *CNetPacker::GetHuffNode()
{
    SHuffNode *rv;
    if (pPool == NULL)
        rv = dbgnew SHuffNode;
    else
    {
        rv = pPool;
        pPool = pPool->pLeft;
    }
    rv->pLeft = NULL;
    rv->pRight = NULL;
    return rv;
}

void CNetPacker::DelHuffNode(SHuffNode *pNode)
{
    pNode->pLeft = pPool;
    pPool = pNode;
}

void CNetPacker::LinkToPool()
{
    nPoolLinks++;
}
void CNetPacker::UnLinkFromPool()
{
    assert(nPoolLinks > 0);
    if (--nPoolLinks == 0)
    {
        while (pPool)
        {
            SHuffNode *pToDel = pPool;
            pPool = pPool->pLeft;
            delete pToDel;
        }
    }
}
