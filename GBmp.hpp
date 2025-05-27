#pragma once
#include <iostream>

#ifdef USING_SSE2
#include <emmintrin.h>
#include <tmmintrin.h>
#endif

template <uint32_t... Values>
struct DwordArray {
    static constexpr uint32_t data[sizeof...(Values)] = { Values... };
};
template <uint32_t... Indices>
constexpr auto generatePalette(std::integer_sequence<uint32_t, Indices...>)
{
    return DwordArray<(Indices * 65793)...> {};
}

class GBmp // line buffer is not align to 4.
{
#pragma pack(push, 2)
    struct GBITMAPFILEHEADER {
        unsigned short bfType;
        unsigned long bfSize;
        unsigned short bfReserved1;
        unsigned short bfReserved2;
        unsigned long bfOffBits;
    };
    struct GBITMAPINFOHEADER {
        unsigned long biSize;
        long biWidth;
        long biHeight;
        unsigned short biPlanes;
        unsigned short biBitCount;
        unsigned long biCompression;
        unsigned long biSizeImage;
        long biXPelsPerMeter;
        long biYPelsPerMeter;
        unsigned long biClrUsed;
        unsigned long biClrImportant;
    };
#pragma pack(pop)
public:
    GBmp(void)
        : m_iWidth(0)
        , m_iHeight(0)
        , m_pImage(0)
        , m_bGray(0) {};
    GBmp(const void* pBuffer, int iWid, int iHei, bool bGray)
        : m_iWidth(iWid)
        , m_iHeight(iHei)
        , m_bGray(bGray)
    {
        int iPxlBytes = bGray ? 1 : 4;
        m_pImage = new unsigned char[iWid * iHei * iPxlBytes];
        memcpy(m_pImage, pBuffer, iWid * iHei * iPxlBytes);
    }
    GBmp(const GBmp&) = delete;
    GBmp(GBmp&& o) noexcept
        : m_iWidth(0)
        , m_iHeight(0)
        , m_pImage(0)
        , m_bGray(0)
    {
        std::swap(m_iWidth, o.m_iWidth);
        std::swap(m_iHeight, o.m_iHeight);
        std::swap(m_pImage, o.m_pImage);
        std::swap(m_bGray, o.m_bGray);
    }
    ~GBmp(void) { Release(); }

    bool LoadBmp(const char* strFileName)
    {
        FILE* inputFile = fopen(strFileName, "rb");
        if (inputFile == 0) {
            return false;
        }

        GBITMAPFILEHEADER bf = { 0 };
        GBITMAPINFOHEADER bi = { 0 };
        bool bRet = true;
        try {
            fread(reinterpret_cast<char*>(&bf), 1, sizeof(bf), inputFile);
            if (bf.bfType != 'MB') {
                return false;
            }

            int iOldBufferLen = m_iWidth * m_iHeight * (m_bGray ? 1 : 4);
            fread(reinterpret_cast<char*>(&bi), 1, sizeof(bi), inputFile);
            switch (bi.biBitCount) {
            case 1:
            case 8:
                m_bGray = true;
                break;
            case 24:
            case 32:
                m_bGray = false;
                break;
            default:
                return false;
            }
            bool btopdown = bi.biHeight < 0;
            int iPxlBytes = m_bGray ? 1 : 4;
            m_iWidth = bi.biWidth;
            m_iHeight = btopdown ? -bi.biHeight : bi.biHeight;
            int iNewBufferLen = m_iWidth * m_iHeight * iPxlBytes;
            if (iOldBufferLen != iNewBufferLen) {
                delete[] m_pImage;
                m_pImage = new unsigned char[iNewBufferLen];
            }
            fseek(inputFile, bf.bfOffBits - bi.biSize - sizeof(GBITMAPFILEHEADER), SEEK_CUR);
            if (bi.biBitCount == 1) {
                int iBufLen = (iNewBufferLen + 7) / 8;
                char* cont = new char[iBufLen];
                int iread = fread(cont, 1, iBufLen, inputFile);
                int iWidLn = iread / m_iHeight;
                for (int i = 0; i < m_iHeight; i++) {
                    unsigned char* pDstLn = m_pImage + i * m_iWidth;
                    char* pSrcLn = cont + i * iWidLn;
                    for (int j = 0; j < m_iWidth / 8; j++) {
                        unsigned char* pImg = pDstLn + j * 8;
                        unsigned char bDatum = pSrcLn[j];
                        for (int k = 0; k < 8; k++) {
                            unsigned char bBit = 1 << (7 - k);
                            unsigned char bData = bDatum & bBit;
                            pImg[k] = (!!bData) * 0xFF;
                        }
                    }
                    int iDoGroup = m_iWidth / 8;
                    if (m_iWidth > iDoGroup * 8) {
                        unsigned char bDatum = pSrcLn[iDoGroup];
                        unsigned char* pImg = pDstLn + iDoGroup * 8;
                        for (int k = 0; k < m_iWidth - iDoGroup * 8; k++) {
                            unsigned char bBit = 1 << (7 - k);
                            unsigned char bData = bDatum & bBit;
                            pImg[k] = (!!bData) * 0xFF;
                        }
                    }
                }
                delete[] cont;
            }
            if (bi.biBitCount == 32) {
                fread(m_pImage, 1, m_iWidth * m_iHeight * 4, inputFile);
            }
            if (bi.biBitCount == 24) {
                unsigned char* pBuf = new unsigned char[iNewBufferLen];
                fread(pBuf, 1, iNewBufferLen, inputFile);
                RGB24ToRGB32(pBuf, m_pImage, m_iWidth, m_iHeight);
                delete[] pBuf;
            }
            if (bi.biBitCount == 8) {
                int iLnBytes = (m_iWidth + 3) / 4 * 4;
                unsigned char* pTemp = new unsigned char[iLnBytes];
                for (int i = 0; i < m_iHeight; i++) {
                    fread(pTemp, 1, iLnBytes, inputFile);
                    unsigned char* pDst = m_pImage + i * m_iWidth;
                    memcpy(pDst, pTemp, m_iWidth);
                }
                delete[] pTemp;
            }
            btopdown ? 0 : MirrorV();
        } catch (...) {
            if (m_pImage) {
                delete[] m_pImage;
                m_pImage = 0;
                m_iWidth = m_iHeight = 0;
            }
            bRet = false;
        }
        fclose(inputFile);

        return bRet;
    }
    bool SaveBmp(const char* strFileName)
    {
        FILE* outputFile = fopen(strFileName, "wb");
        if (outputFile == 0) {
            return false;
        }

        unsigned long iLnBytes = m_bGray ? ((m_iWidth + 3) / 4 * 4) : m_iWidth * 4;
        unsigned long iOffset = sizeof(GBITMAPFILEHEADER) + sizeof(GBITMAPINFOHEADER) + m_bGray * 256 * 4;
        GBITMAPFILEHEADER bf { 'MB', iOffset + iLnBytes * m_iHeight, 0, 0, iOffset };
        GBITMAPINFOHEADER bi { sizeof(bi), m_iWidth, -m_iHeight, 1, m_bGray ? 8u : 32u, 0, iLnBytes * m_iHeight };
        fwrite(reinterpret_cast<const char*>(&bf), 1, sizeof(bf), outputFile);
        fwrite(reinterpret_cast<const char*>(&bi), 1, sizeof(bi), outputFile);

        if (m_bGray) {
            constexpr auto MyDwordArray = generatePalette(std::make_integer_sequence<uint32_t, 256>());
            fwrite(reinterpret_cast<const char*>(MyDwordArray.data), 1, sizeof(MyDwordArray.data), outputFile);
        }

        unsigned char* pLn = new unsigned char[iLnBytes];
        memset(pLn, 0, iLnBytes);
        int iLnLen = m_iWidth * (m_bGray ? 1 : 4);
        for (int i = 0; i < m_iHeight; i++) {
            memcpy(pLn, m_pImage + i * iLnLen, iLnLen);
            fwrite(reinterpret_cast<char*>(pLn), 1, iLnBytes, outputFile);
        }
        delete[] pLn;

        return fclose(outputFile) == 0;
    }

    inline bool IsGray() { return m_bGray; }
    inline void* Data() { return m_pImage; }
    inline int GetWidth() { return m_iWidth; }
    inline int GetHeight() { return m_iHeight; }

    void SetImageSize(int iWid, int iHei, bool bGray)
    {
        if (iWid == m_iWidth && iHei == m_iHeight && bGray == m_bGray) {
            return;
        } else {
            if (m_pImage) {
                delete[] m_pImage;
                m_pImage = 0;
                m_iWidth = m_iHeight = 0;
            }
            m_iWidth = iWid;
            m_iHeight = iHei;
            m_bGray = bGray;
            m_pImage = new unsigned char[(__int64)iWid * iHei * (m_bGray ? 1 : 4)];
        }
    }

    void SetImage(const void* pImage, int iWid, int iHei, bool bGray)
    {
        SetImageSize(iWid, iHei, bGray);
        int iCount = iWid * (bGray ? 1 : 4) * iHei;
        memcpy(m_pImage, pImage, iCount);
    }

    void Release()
    {
        if (m_pImage) {
            delete[] m_pImage;
            m_pImage = 0;
            m_iWidth = m_iHeight = 0;
        }
    }

    void AttachData(void* pNewImage, int iWid, int iHei, bool bGray)
    {
        if (m_pImage) {
            delete[] m_pImage;
            m_pImage = 0;
            m_iWidth = m_iHeight = 0;
        }
        m_iWidth = iWid;
        m_iHeight = iHei;
        m_bGray = bGray;
        m_pImage = static_cast<unsigned char*>(pNewImage);
    }
    void* DetachData()
    {
        unsigned char* pImage = m_pImage;
        m_pImage = 0;
        m_iWidth = m_iHeight = 0;
        return pImage;
    }

    void MirrorV()
    {
        int iLnBytes = m_iWidth * (m_bGray ? 1 : 4);
        unsigned char* pLn = new unsigned char[iLnBytes];
        for (int i = 0; i < GetHeight() / 2; i++) {
            unsigned char* pUpLn = (unsigned char*)Data() + i * iLnBytes;
            unsigned char* pDnLn = (unsigned char*)Data() + (GetHeight() - 1 - i) * iLnBytes;
            memcpy(pLn, pUpLn, iLnBytes);
            memcpy(pUpLn, pDnLn, iLnBytes);
            memcpy(pDnLn, pLn, iLnBytes);
        }
        delete[] pLn;
    }
    void MirrorH()
    {
        int iPxlBytes = m_bGray ? 1 : 4;
        unsigned char* pTemp = new unsigned char[iPxlBytes];
        for (int i = 0; i < m_iHeight; i++) {
            unsigned char* pLn = m_pImage + i * m_iWidth * iPxlBytes;
            for (int j = 0; j < m_iWidth / 2; j++) {
                unsigned char* pSrc1 = pLn + j * iPxlBytes;
                unsigned char* pSrc2 = pLn + (m_iWidth - 1 - j) * iPxlBytes;
                memcpy(pTemp, pSrc1, iPxlBytes);
                memcpy(pSrc1, pSrc2, iPxlBytes);
                memcpy(pSrc2, pTemp, iPxlBytes);
            }
        }
        delete[] pTemp;
    }
    void ReverseImage()
    {
        if (!m_bGray) {
            unsigned long* pTarget = (unsigned long*)m_pImage;
#ifdef USING_SSE2
            int iLoop = m_iWidth * m_iHeight * 4 / 32;
            for (int i = 0; i < iLoop; i++) {
                __m128i* pm128Top = (__m128i*)(pTarget + i * 4);
                __m128i* pm128Bot = (__m128i*)(pTarget + m_iWidth * m_iHeight - i * 4 - 4);
                __m128i m128Top = _mm_loadu_si128(pm128Top);
                __m128i m128Bot = _mm_loadu_si128(pm128Bot);
                m128Top = _mm_shuffle_epi32(m128Top, _MM_SHUFFLE(0, 1, 2, 3));
                m128Bot = _mm_shuffle_epi32(m128Bot, _MM_SHUFFLE(0, 1, 2, 3));
                _mm_storeu_si128(pm128Top, m128Bot);
                _mm_storeu_si128(pm128Bot, m128Top);
            }

            for (int i = iLoop * 4; i < m_iWidth * m_iHeight / 2; i++) {
                unsigned long swap = pTarget[i];
                pTarget[i] = pTarget[m_iWidth * m_iHeight - 1 - i];
                pTarget[m_iWidth * m_iHeight - 1 - i] = swap;
            }
#else
            for (int i = 0; i < m_iWidth * m_iHeight / 2; i++) {
                unsigned long swap = pTarget[i];
                pTarget[i] = pTarget[m_iWidth * m_iHeight - 1 - i];
                pTarget[m_iWidth * m_iHeight - 1 - i] = swap;
            }
#endif
        } else {
            unsigned char* pTarget = m_pImage;
#ifdef USING_SSE2
            int iLoop = m_iWidth * m_iHeight / 32;
            for (int i = 0; i < iLoop; i++) {
                __m128i* pm128Top = (__m128i*)(pTarget + i * 16);
                __m128i* pm128Bot = (__m128i*)(pTarget + m_iWidth * m_iHeight - i * 16 - 16);
                __m128i m128Top = _mm_loadu_si128(pm128Top);
                __m128i m128Bot = _mm_loadu_si128(pm128Bot);
                m128Top = _mm_shuffle_epi8(m128Top,
                    _mm_set_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15));
                m128Bot = _mm_shuffle_epi8(m128Bot,
                    _mm_set_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15));

                _mm_storeu_si128(pm128Top, m128Bot);
                _mm_storeu_si128(pm128Bot, m128Top);
            }

            for (int i = iLoop * 16; i < m_iWidth * m_iHeight / 2; i++) {
                unsigned char swap = pTarget[i];
                pTarget[i] = pTarget[m_iWidth * m_iHeight - 1 - i];
                pTarget[m_iWidth * m_iHeight - 1 - i] = swap;
            }
#else
            for (int i = 0; i < m_iWidth * m_iHeight / 2; i++) {
                unsigned char swap = pTarget[i];
                pTarget[i] = pTarget[m_iWidth * m_iHeight - 1 - i];
                pTarget[m_iWidth * m_iHeight - 1 - i] = swap;
            }
#endif
        }
    }

    operator unsigned char*() const { return m_pImage; }
    GBmp Rotate270()
    {
        GBmp bmpRot;
        bmpRot.SetImageSize(m_iHeight, m_iWidth, m_bGray);
        if (m_bGray) {
            for (int x = 0; x < m_iWidth; x++) {
                for (int y = 0; y < m_iHeight; y++) {
                    int srcIndex = (m_iHeight - 1 - y) * m_iWidth + x;
                    int dstIndex = x * m_iHeight + y;
                    bmpRot.m_pImage[dstIndex] = m_pImage[srcIndex];
                }
            }
        } else {
            for (int x = 0; x < m_iWidth; x++) {
                for (int y = 0; y < m_iHeight; y++) {
                    int srcIndex = (m_iHeight - 1 - y) * m_iWidth + x;
                    int dstIndex = x * m_iHeight + y;
                    ((unsigned long*)bmpRot.m_pImage)[dstIndex] = ((unsigned long*)m_pImage)[srcIndex];
                }
            }
        }
        return bmpRot;
    }
    GBmp Rotate90()
    {
        GBmp bmpRot;
        bmpRot.SetImageSize(m_iHeight, m_iWidth, m_bGray);
        if (m_bGray) {
            for (int y = 0; y < m_iHeight; y++) {
                for (int x = 0; x < m_iWidth; x++) {
                    int iOffSet = (m_iWidth - x - 1) * m_iHeight + y;
                    unsigned char* pLnSrc = m_pImage + (y * m_iWidth + x);
                    unsigned char* pLnDst = bmpRot.m_pImage + iOffSet;
                    *pLnDst = *pLnSrc;
                }
            }
        } else {
            for (int y = 0; y < m_iHeight; y++) {
                for (int x = 0; x < m_iWidth; x++) {
                    int iOffSet = (m_iWidth - x - 1) * m_iHeight + y;
                    unsigned long* pLnSrc = ((unsigned long*)m_pImage) + (y * m_iWidth + x);
                    unsigned long* pLnDst = ((unsigned long*)bmpRot.m_pImage) + iOffSet;
                    *pLnDst = *pLnSrc;
                }
            }
        }
        return bmpRot;
    }
#ifdef USEING_IPP
    GBmp Transpose_ipp()
    {
        GBmp bmpRot;
        bmpRot.SetImageSize(m_iHeight, m_iWidth, m_bGray);
        IppStatus status = ippiTranspose_8u_C1R(m_pImage, m_iWidth, bmpRot, m_iHeight, { m_iWidth, m_iHeight });
        return bmpRot;
    }
#endif
    GBmp Rotate180()
    {
        GBmp bmpRot;
        bmpRot.SetImageSize(m_iWidth, m_iHeight, m_bGray);
        if (m_bGray) {
            for (int i = 0; i < m_iWidth * m_iHeight; i++) {
                bmpRot.m_pImage[i] = m_pImage[m_iWidth * m_iHeight - 1 - i];
            }
        } else {
            for (int i = 0; i < m_iWidth * m_iHeight; i++) {
                ((unsigned long*)bmpRot.m_pImage)[i] = ((unsigned long*)m_pImage)[m_iWidth * m_iHeight - 1 - i];
            }
        }
        return bmpRot;
    }

    void CropImage(GBmp& imgSrc, int left, int top, int right, int bottom)
    {
        if (left < 0) {
            left = 0;
        }
        if (top < 0) {
            top = 0;
        }
        if (right > imgSrc.GetWidth()) {
            right = imgSrc.GetWidth();
        }
        if (bottom > imgSrc.GetHeight()) {
            bottom = imgSrc.GetHeight();
        }
        SetImageSize(right - left, bottom - top, imgSrc.IsGray());
        int iBytes = imgSrc.IsGray() ? 1 : 4;
        const unsigned char* pBuffer = imgSrc;
        const unsigned char* pLnSrc = pBuffer + (top * imgSrc.GetWidth() + left) * iBytes;
        unsigned char* pLnDst = (unsigned char*)Data();
        for (int i = 0; i < (bottom - top); i++) {
            memcpy(pLnDst + i * (right - left) * iBytes, pLnSrc + i * imgSrc.GetWidth() * iBytes, (right - left) * iBytes);
        }
    }

    void CopyImage(GBmp& imgSrc)
    {
        SetImage(imgSrc.Data(), imgSrc.GetWidth(), imgSrc.GetHeight(), imgSrc.IsGray());
    }

    void ToGray()
    {
        if (IsGray()) {
            return;
        }
        unsigned char* pY = (unsigned char*)Data();
        const unsigned long* pRGB8888 = (unsigned long*)Data();
        int iLen = m_iWidth * m_iHeight;
        m_bGray = true;
#ifdef USING_SSE2
        __m128i m128xmmCo = _mm_set_epi16(0, 9798, 19235, 3736, 0, 9798, 19235, 3736);
        __m128i m128xmm0, m128xmm1, m128xmm2;
        __m128i m128Mask = _mm_set_epi32(0, -1, -1, -1);
        unsigned long* pDst = (unsigned long*)pY;
        int iLoop = iLen / 4;
        int iLast = iLen & 3;
        for (int i = 0; i < iLoop; i++) {
            m128xmm0 = _mm_loadu_si128((__m128i*)(pRGB8888 + i * 4));
            m128xmm1 = _mm_unpacklo_epi8(m128xmm0, _mm_setzero_si128());
            m128xmm2 = _mm_unpackhi_epi8(m128xmm0, _mm_setzero_si128());
            m128xmm1 = _mm_madd_epi16(m128xmm1, m128xmmCo);
            m128xmm2 = _mm_madd_epi16(m128xmm2, m128xmmCo);
            m128xmm0 = _mm_srli_epi64(m128xmm1, 32);
            m128xmm1 = _mm_add_epi32(m128xmm1, m128xmm0);
            m128xmm1 = _mm_and_si128(m128xmm1, m128Mask);
            m128xmm1 = _mm_shuffle_epi32(m128xmm1, _MM_SHUFFLE(3, 3, 2, 0));
            m128xmm0 = _mm_srli_epi64(m128xmm2, 32);
            m128xmm2 = _mm_add_epi32(m128xmm2, m128xmm0);
            m128xmm2 = _mm_and_si128(m128xmm2, m128Mask);
            m128xmm2 = _mm_shuffle_epi32(m128xmm2, _MM_SHUFFLE(2, 0, 3, 3));
            m128xmm1 = _mm_or_si128(m128xmm1, m128xmm2);
            m128xmm1 = _mm_add_epi32(m128xmm1, _mm_set1_epi32(16384));
            m128xmm1 = _mm_srli_epi32(m128xmm1, 15);
            m128xmm1 = _mm_packs_epi32(m128xmm1, _mm_setzero_si128());
            m128xmm1 = _mm_packus_epi16(m128xmm1, _mm_setzero_si128());
            pDst[i] = _mm_cvtsi128_si32(m128xmm1);
        }
        for (int i = 0; i < iLast; i++) {
            unsigned long dwRgb = pRGB8888[iLoop * 4 + i];
            int r = (unsigned char)(dwRgb >> 16);
            int g = (unsigned char)(dwRgb >> 8);
            int b = (unsigned char)dwRgb;
            pY[iLoop * 4 + i] = (r * 3736 + g * 19235 + b * 9798 + 16384) / 32768;
        }
#else
        for (int i = 0; i < iLen; i++) {
            int r = (unsigned char)(pRGB8888[i] >> 16);
            int g = (unsigned char)(pRGB8888[i] >> 8);
            int b = (unsigned char)pRGB8888[i];
            pY[i] = (r * 3736 + g * 19235 + b * 9798 + 16384) / 32768;
        }
#endif
    }

private:
    void RGB24ToRGB32(void* pSrc, void* pDst, int nWidth, int nHeight)
    {
        int iSrcLnBytes = (nWidth * 3 + 3) / 4 * 4;
        for (int i = 0; i < nHeight; i++) {
            unsigned char* pLSrc = ((unsigned char*)pSrc) + i * iSrcLnBytes;
            unsigned char* pLDst = ((unsigned char*)pDst) + i * nWidth * 4;
#ifdef USING_SSE2
            int iLoop = nWidth / 4; // = 0; = nWidth / 4;
            int iRemain = iLoop * 4;
            for (int j = 0; j < iLoop; j++) {
                __m128i* pm128Src = (__m128i*)(pLSrc + j * 12);
                __m128i* pm128Dst = (__m128i*)(pLDst + j * 16);
                __m128i m128S = _mm_loadu_si128(pm128Src);
                m128S = _mm_shuffle_epi8(m128S, _mm_set_epi8(11, 11, 10, 9, 8, 8, 7, 6, 5, 5, 4, 3, 2, 2, 1, 0));
                m128S = _mm_and_si128(m128S, _mm_set_epi32(0x00ffffff, 0x00ffffff, 0x00ffffff, 0x00ffffff));
                _mm_storeu_si128(pm128Dst, m128S);
            }
            for (int j = iRemain; j < nWidth; j++) {
                unsigned char* pSrcPxl = pLSrc + j * 3;
                unsigned char* pDstPxl = pLDst + j * 4;
                pDstPxl[0] = pSrcPxl[0];
                pDstPxl[1] = pSrcPxl[1];
                pDstPxl[2] = pSrcPxl[2];
                pDstPxl[3] = 0;
            }
#else
            for (int j = 0; j < nWidth; j++) {
                unsigned char* pSrcPxl = pLSrc + j * 3;
                unsigned char* pDstPxl = pLDst + j * 4;
                pDstPxl[0] = pSrcPxl[0];
                pDstPxl[1] = pSrcPxl[1];
                pDstPxl[2] = pSrcPxl[2];
                pDstPxl[3] = 0;
            }
#endif
        }
    }

    int m_iWidth;
    int m_iHeight;
    unsigned char* m_pImage;
    bool m_bGray;
};
