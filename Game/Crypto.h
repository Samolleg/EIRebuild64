#pragma once
/*==========================================================================================================

        Encryption class header
        Uses Diffie-Hellman(256 bit) alghorithm to generate session key
        and RIPEMD-128 hash function as streaming cypher

        Written by Ivanov Evgeny
        Nival Interactive, 1998

==========================================================================================================*/

const int CRY_KEY_LEN = 32;
const int CRY_HASH_LEN = 16;

class CCrypter
{
private:
    BYTE secretKey[CRY_KEY_LEN];    // secret key
    BYTE publicKey[CRY_KEY_LEN];    // public key
    BYTE sessionKey[CRY_KEY_LEN];   // session key generated with Diffie-Hellman
    BYTE hashEncrypt[CRY_HASH_LEN]; // current values for generating gamma using hash function
    BYTE hashDecrypt[CRY_HASH_LEN];
    BYTE gammaForEncrypt[CRY_HASH_LEN]; // current gamma values
    BYTE gammaForDecrypt[CRY_HASH_LEN];
    int nGammaForEncryptLeft; // gamma remainders
    int nGammaForDecryptLeft;

public:
    void GenerateKeys();
    void GetPublicKey(void *lpBuf);
    void SetRemotePublicKey(void *lpBuf);
    void EncryptBlock(void *pBuf, int nBufLen)
    {
        EncodeBlock(pBuf, nBufLen, hashEncrypt, gammaForEncrypt, nGammaForEncryptLeft);
    }
    void DecryptBlock(void *pBuf, int nBufLen)
    {
        EncodeBlock(pBuf, nBufLen, hashDecrypt, gammaForDecrypt, nGammaForDecryptLeft);
    }

private:
    void InitGamma(BYTE *pHash, BYTE *pGamma, int &nGammaLeft);
    void NextGamma(BYTE *pHash, BYTE *pGamma, int &nGammaLeft);
    void EncodeBlock(void *pBuf, int nBufLen, BYTE *pHash, BYTE *pGamma, int &nGammaLeft);
};
