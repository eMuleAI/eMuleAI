#include "stdafx.h"
#include "Quantize.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
CQuantizer::CQuantizer(UINT nMaxColors, UINT nColorBits)
	: m_pTree()
	, m_pReducibleNodes()
	, m_nLeafCount()
	, m_nMaxColors(max(nMaxColors, 16))
	, m_nOutputMaxColors(nMaxColors)
	, m_nColorBits(max(nColorBits, 8))
{
}
/////////////////////////////////////////////////////////////////////////////
CQuantizer::~CQuantizer()
{
	if (m_pTree != NULL)
		DeleteTree(&m_pTree);
}
/////////////////////////////////////////////////////////////////////////////
BOOL CQuantizer::ProcessImage(HANDLE hImage)
{
	BITMAPINFOHEADER ds;
	memcpy(&ds, hImage, sizeof(ds));
	int effwdt = ((ds.biBitCount * ds.biWidth + 31) / 32) * 4;

	int nPad = effwdt - (ds.biWidth * ds.biBitCount + 7) / 8;

	BYTE *pbBits = (BYTE*)hImage + *(DWORD*)hImage + ds.biClrUsed * sizeof(RGBQUAD);

	switch (ds.biBitCount) {
	case 1: // 1-bit DIB
	case 4: // 4-bit DIB
	case 8: // 8-bit DIB
		for (int i = 0; i < ds.biHeight; ++i)
			for (int j = 0; j < ds.biWidth; ++j) {
				BYTE idx = GetPixelIndex(j, i, ds.biBitCount, effwdt, pbBits);
				BYTE *pal = (BYTE*)hImage + sizeof(BITMAPINFOHEADER);
				size_t ldx = idx * sizeof(RGBQUAD);
				BYTE b = pal[ldx];
				BYTE g = pal[++ldx];
				BYTE r = pal[++ldx];
				BYTE a = pal[++ldx];
				AddColor(&m_pTree, r, g, b, a, m_nColorBits, 0, &m_nLeafCount, m_pReducibleNodes);
				while (m_nLeafCount > m_nMaxColors)
					ReduceTree(m_nColorBits, &m_nLeafCount, m_pReducibleNodes);
			}

		return TRUE;
	case 24: // 24-bit DIB
		for (int i = 0; i < ds.biHeight; ++i) {
			for (int j = 0; j < ds.biWidth; ++j) {
				BYTE b = *pbBits++;
				BYTE g = *pbBits++;
				BYTE r = *pbBits++;
				AddColor(&m_pTree, r, g, b, 0, m_nColorBits, 0, &m_nLeafCount, m_pReducibleNodes);
				while (m_nLeafCount > m_nMaxColors)
					ReduceTree(m_nColorBits, &m_nLeafCount, m_pReducibleNodes);
			}
			pbBits += nPad;
		}
		return TRUE;
	}
	// Unrecognized color format
	return FALSE;
}
/////////////////////////////////////////////////////////////////////////////
void CQuantizer::AddColor(NODE **ppNode, BYTE r, BYTE g, BYTE b, BYTE a,
	UINT nColorBits, UINT nLevel, UINT *pLeafCount, NODE **pReducibleNodes)
{
	static BYTE const mask[8] = { 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01 };

	// If the node doesn't exist, create it.
	if (*ppNode == NULL)
		*ppNode = (NODE*)CreateNode(nLevel, nColorBits, pLeafCount, pReducibleNodes);

	// Update color information if it's a leaf node.
	if (*ppNode != NULL)
		if ((*ppNode)->bIsLeaf) {
			(*ppNode)->nPixelCount++;
			(*ppNode)->nRedSum += r;
			(*ppNode)->nGreenSum += g;
			(*ppNode)->nBlueSum += b;
			(*ppNode)->nAlphaSum += a;
		} else { // Recurse a level deeper if the node is not a leaf.
			int shift = 7 - nLevel;
			int nIndex = (((r & mask[nLevel]) >> shift) << 2) |
				(((g & mask[nLevel]) >> shift) << 1) |
				((b & mask[nLevel]) >> shift);
			AddColor(&((*ppNode)->pChild[nIndex]), r, g, b, a, nColorBits
				, nLevel + 1, pLeafCount, pReducibleNodes);
		}
}
/////////////////////////////////////////////////////////////////////////////
void* CQuantizer::CreateNode(UINT nLevel, UINT nColorBits, UINT *pLeafCount,
	NODE **pReducibleNodes)
{
	NODE *pNode = (NODE*)calloc(1, sizeof(NODE));

	if (pNode != NULL) {
		pNode->bIsLeaf = (nLevel == nColorBits);
		if (pNode->bIsLeaf)
			++(*pLeafCount);
		else {
			pNode->pNext = pReducibleNodes[nLevel];
			pReducibleNodes[nLevel] = pNode;
		}
	}
	return pNode;
}
/////////////////////////////////////////////////////////////////////////////
void CQuantizer::ReduceTree(UINT nColorBits, UINT *pLeafCount, NODE **pReducibleNodes)
{
	int i;
	// Find the deepest level containing at least one reducible node.
	for (i = nColorBits; --i > 0 && pReducibleNodes[i] == NULL;);

	// Reduce the node most recently added to the list at level i.
	NODE *pNode = pReducibleNodes[i];
	pReducibleNodes[i] = pNode->pNext;

	UINT nRedSum = 0;
	UINT nGreenSum = 0;
	UINT nBlueSum = 0;
	UINT nAlphaSum = 0;
	UINT nChildren = 0;

	for (i = 0; i < 8; ++i) {
		if (pNode->pChild[i] != NULL) {
			nRedSum += pNode->pChild[i]->nRedSum;
			nGreenSum += pNode->pChild[i]->nGreenSum;
			nBlueSum += pNode->pChild[i]->nBlueSum;
			nAlphaSum += pNode->pChild[i]->nAlphaSum;
			pNode->nPixelCount += pNode->pChild[i]->nPixelCount;
			free(pNode->pChild[i]);
			pNode->pChild[i] = NULL;
			++nChildren;
		}
	}

	pNode->bIsLeaf = true;
	pNode->nRedSum = nRedSum;
	pNode->nGreenSum = nGreenSum;
	pNode->nBlueSum = nBlueSum;
	pNode->nAlphaSum = nAlphaSum;
	*pLeafCount -= nChildren - 1;
}
/////////////////////////////////////////////////////////////////////////////
void CQuantizer::DeleteTree(NODE **ppNode)
{
	for (int i = 0; i < 8; ++i)
		if ((*ppNode)->pChild[i] != NULL)
			DeleteTree(&((*ppNode)->pChild[i]));

	free(*ppNode);
	*ppNode = NULL;
}
/////////////////////////////////////////////////////////////////////////////
void CQuantizer::GetPaletteColors(NODE *pTree, RGBQUAD *prgb, UINT *pIndex, UINT *pSum)
{
	if (pTree) {
		if (pTree->bIsLeaf) {
			const UINT nPix = pTree->nPixelCount;
			const UINT nHalf = pTree->nPixelCount >> 1;
			prgb[*pIndex].rgbRed = (BYTE)min(255, (pTree->nRedSum + nHalf) / nPix);
			prgb[*pIndex].rgbGreen = (BYTE)min(255, (pTree->nGreenSum + nHalf) / nPix);
			prgb[*pIndex].rgbBlue = (BYTE)min(255, (pTree->nBlueSum + nHalf) / nPix);
			prgb[*pIndex].rgbReserved = (BYTE)min(255, (pTree->nAlphaSum + nHalf) / nPix);
			if (pSum)
				pSum[*pIndex] = nPix;
			++(*pIndex);
		} else
			for (int i = 0; i < 8; ++i)
				if (pTree->pChild[i] != NULL)
					GetPaletteColors(pTree->pChild[i], prgb, pIndex, pSum);
	}
}
/////////////////////////////////////////////////////////////////////////////
UINT CQuantizer::GetColorCount() const
{
	return m_nLeafCount;
}
/////////////////////////////////////////////////////////////////////////////
void CQuantizer::SetColorTable(RGBQUAD *prgb)
{
	UINT nIndex = 0;
	if (m_nOutputMaxColors < 16) {
		UINT nSum[16];
		RGBQUAD tmppal[16];
		GetPaletteColors(m_pTree, tmppal, &nIndex, nSum);
		if (m_nLeafCount > m_nOutputMaxColors) {
			for (UINT j = 0; j < m_nOutputMaxColors; ++j) {
				UINT a = (j * m_nLeafCount) / m_nOutputMaxColors;
				UINT b = ((j + 1) * m_nLeafCount) / m_nOutputMaxColors;
				UINT nr = 0, ng = 0, nb = 0, na = 0, ns = 0;
				for (UINT k = a; k < b; ++k) {
					nr += tmppal[k].rgbRed * nSum[k];
					ng += tmppal[k].rgbGreen * nSum[k];
					nb += tmppal[k].rgbBlue * nSum[k];
					na += tmppal[k].rgbReserved * nSum[k];
					ns += nSum[k];
				}
				UINT nh = ns >> 1;
				prgb[j].rgbRed = (BYTE)min(255, (nh + nr) / ns);
				prgb[j].rgbGreen = (BYTE)min(255, (nh + ng) / ns);
				prgb[j].rgbBlue = (BYTE)min(255, (nh + nb) / ns);
				prgb[j].rgbReserved = (BYTE)min(255, (nh + na) / ns);
			}
		} else
			memcpy(prgb, tmppal, m_nLeafCount * sizeof(RGBQUAD));
	} else
		GetPaletteColors(m_pTree, prgb, &nIndex, 0);
}
/////////////////////////////////////////////////////////////////////////////
BYTE CQuantizer::GetPixelIndex(long x, long y, int nbit, long effwdt, BYTE *pimage)
{
	if (nbit == 8)
		return pimage[y * effwdt + x];

	BYTE iDst = pimage[y * effwdt + ((x * nbit) >> 3)];
	if (nbit == 4) {
		BYTE pos = (BYTE)(4 * (~x & 1));
		return (iDst >> pos) & 0x0F;
	}
	if (nbit == 1) {
		BYTE pos = (BYTE)(~x & 7);
		return (iDst >> pos) & 0x01;
	}

	return 0;
}