#pragma once

// --------------------------------------------------------------------------------------------
// Класс для запаковки/распаковки пакетов передаваемых по сети
// Использует Huffman coder с заранее собранной статистикой.
// --------------------------------------------------------------------------------------------

// Вычисляет сколько байт занимает буфер в nBits бит
int Bits2Bytes(int iBits);
const int N_NETPACKER_STATSIZE = 256;

struct SHuffNode;

class CNetPacker
{
public:
    CNetPacker();
    ~CNetPacker();

    // Uses statistics data to initialize
    void Init();
    // запаковывает блок данных,
    // возвращает число _бит_ записанных в выходной буфер
    // размер выходного буфера должен быть как минимум в 4 раза больше входного
    const int Pack(const void *pIn, int iInLen, void *pOut);
    // распаковывает блок данных,
    // возвращает число байт записанных в выходной буфер
    // если буфера не хватает, оставшиеся данные будут потеряны
    const int UnPack(const void *pIn, int iInLen, void *pOut, int iOutLen);

    // Начинает сбор статистики
    void Statistics();
    // Сбор статистики
    void Statistics(const void *data, int dataLen);
    int GetStatisticsBytes() { return nNumStatBytes; }

#ifdef _DEBUG
    void PrintCode(BYTE c);
#endif

private:
    // Кодовые последовательности для символов,
    // хранятся в младших битах в обратном значимости порядке
    DWORD dwCodeBits[256];
    // Длины последовательностей
    int iCodeLens[256];
    // Массив встречаемости символов
    unsigned iStatFreqs[256];
    int nNumStatBytes;
    // Дерево Хаффмена
    SHuffNode *root;

    // SHuffNode pool management
    static SHuffNode *pPool;
    static int nPoolLinks;
    static SHuffNode *GetHuffNode();
    static void DelHuffNode(SHuffNode *);
    static void LinkToPool();
    static void UnLinkFromPool();

    void BuildHuffmanTree();
    void AddToHuffmanTree(SHuffNode *node, BYTE cByte, int iFreq);
    void BuildCodeTables();
    void BuildCodeTables(SHuffNode *node, DWORD dwCode, int iCodeLen);
    void CleanUpHuffmanTree();
    void CleanUpNode(SHuffNode *node);
};
