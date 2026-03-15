//This file is part of eMule AI
//Copyright (C)2002-2026 Merkur ( strEmail.Format("%s@%s", "devteam", "emule-project.net") / https://www.emule-project.net )
//Copyright (C)2026 eMule AI
//
//This program is free software; you can redistribute it and/or
//modify it under the terms of the GNU General Public License
//as published by the Free Software Foundation; either
//version 2 of the License, or (at your option) any later version.
//
//This program is distributed in the hope that it will be useful,
//but WITHOUT ANY WARRANTY; without even the implied warranty of
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//GNU General Public License for more details.
//
//You should have received a copy of the GNU General Public License
//along with this program; if not, write to the Free Software
//Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#include "stdafx.h"
#include "emule.h"
#include "ClientCredits.h"
#include "OtherFunctions.h"
#include "Preferences.h"
#include "SafeFile.h"
#include "Opcodes.h"
#include "ServerConnect.h"
#include "emuledlg.h"
#include "Log.h"
#include "cryptopp/base64.h"
#include "cryptopp/osrng.h"
#include "cryptopp/files.h"
#include "ClientList.h"
#include "UpDownClient.h"
#include "SharedFileList.h"
#include "KnownFile.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define CLIENTS_MET_FILENAME	_T("clients.met")
#define CLIENTS_MET_FILENAME_TMP _T("clients.met.tmp")

CClientCredits::CClientCredits(const CreditStruct &in_credits)
	: m_Credits(in_credits)
{
	InitalizeIdent();
	ClearWaitStartTime();
	m_dwWaitTimeIP = CAddress();
	m_bCheckScoreRatio = true;
}

CClientCredits::CClientCredits(const uchar *key)
	: m_Credits()
{
	md4cpy(&m_Credits.abyKey, key);
	InitalizeIdent();
	m_dwSecureWaitTime = m_dwUnSecureWaitTime = ::GetTickCount();
	m_dwWaitTimeIP = CAddress();
	m_bCheckScoreRatio = true;
}

void CClientCredits::AddDownloaded(uint32 bytes, const CAddress& dwForIP)
{
	switch (GetCurrentIdentState(dwForIP)) {
	case IS_IDFAILED:
	case IS_IDBADGUY:
	case IS_IDNEEDED:
		if (theApp.clientcredits->CryptoAvailable())
			return;
	}

	uint64 current = GetDownloadedTotal() + bytes;
	//recode
	m_Credits.nDownloadedLo = LODWORD(current);
	m_Credits.nDownloadedHi = HIDWORD(current);
	m_bCheckScoreRatio = true;
}

void CClientCredits::AddUploaded(uint32 bytes, const CAddress & dwForIP)
{
	switch (GetCurrentIdentState(dwForIP)) {
	case IS_IDFAILED:
	case IS_IDBADGUY:
	case IS_IDNEEDED:
		if (theApp.clientcredits->CryptoAvailable())
			return;
	}

	uint64 current = GetUploadedTotal() + bytes;
	//recode
	m_Credits.nUploadedLo = LODWORD(current);
	m_Credits.nUploadedHi = HIDWORD(current);
	m_bCheckScoreRatio = true;
}

uint64 CClientCredits::GetUploadedTotal() const
{
	return ((uint64)m_Credits.nUploadedHi << 32) | m_Credits.nUploadedLo;
}

uint64 CClientCredits::GetDownloadedTotal() const
{
	return ((uint64)m_Credits.nDownloadedHi << 32) | m_Credits.nDownloadedLo;
}

float CClientCredits::GetScoreRatio(const CAddress& dwForIP)
{
	EIdentState currentIDstate = GetCurrentIdentState(dwForIP);
	bool bBadGuy = false;

	if (m_bCheckScoreRatio == false) //only refresh ScoreRatio when really need
		return m_fLastScoreRatio;

	m_bCheckScoreRatio = false;
	double result = 0.0; //everybody share one result. leuk_he:doublw to prevent underflow in CS_LOVELACE.

	switch (thePrefs.GetCreditSystem()) {

	case CS_LOVELACE: {
		if (currentIDstate != IS_IDENTIFIED && currentIDstate != IS_NOTAVAILABLE && theApp.clientcredits->CryptoAvailable()) {
			result = 0.984290578f; // use the CS' default value
			bBadGuy = true;
			break;
		}

		double cl_up, cl_down;

		cl_up = GetUploadedTotal() / (double)1048576;
		cl_down = GetDownloadedTotal() / (double)1048576;
		result = (float)(3.0 * cl_down * cl_down - cl_up * cl_up);
		if (fabs(result) > 20000.0f)
			result *= 20000.0 / fabs(result);
		result = 100.0 * pow((1 - 1 / (1.0f + exp(result * 0.001))), 6.6667);
		if (result < 0.1)
			result = 0.1;
		if (result > 10.0 && IdentState == IS_NOTAVAILABLE)
			result = 10.0;
	}
		break;
	case CS_PAWCIO: {
		if (currentIDstate != IS_IDENTIFIED && currentIDstate != IS_NOTAVAILABLE && theApp.clientcredits->CryptoAvailable()) {
			result = 1.0f;
			bBadGuy = true;
			break;
		}

		if ((GetDownloadedTotal() < 1000000) && (GetUploadedTotal() > 1000000)) {
			result = 1.0f;
			break;
		}
		else if ((GetDownloadedTotal() < 1000000) && (GetUploadedTotal() < 1000000)) {
			result = 3.0f;
			break;
		}
		result = 0.0F;
		if (GetUploadedTotal() < 1000000)
			result = 10.0f * GetDownloadedTotal() / 1000000.0f;
		else
			result = (float)(GetDownloadedTotal() * 3) / GetUploadedTotal();
		if ((GetDownloadedTotal() > 100000000) && (GetUploadedTotal() < GetDownloadedTotal() + 8000000) && (result < 50)) result = 50;
		else if ((GetDownloadedTotal() > 50000000) && (GetUploadedTotal() < GetDownloadedTotal() + 5000000) && (result < 25)) result = 25;
		else if ((GetDownloadedTotal() > 25000000) && (GetUploadedTotal() < GetDownloadedTotal() + 3000000) && (result < 12)) result = 12;
		else if ((GetDownloadedTotal() > 10000000) && (GetUploadedTotal() < GetDownloadedTotal() + 2000000) && (result < 5)) result = 5;
		if (result > 100.0f) {
			result = 100.0f;
			break;
		}
		if (result < 1.0f) {
			result = 1.0f;
			break;
		}
	}
		break;
	case CS_RATIO: { // RT.10a mod Credit
		if (currentIDstate != IS_IDENTIFIED && currentIDstate != IS_NOTAVAILABLE && theApp.clientcredits->CryptoAvailable()) {
			result = 1.0f;
			bBadGuy = true;
			break;
		}

		// check the client ident status
		double UploadedTotalMB = (double)GetUploadedTotal() / 1048576.0;
		double DownloadedTotalMB = (double)GetDownloadedTotal() / 1048576.0;
		if (DownloadedTotalMB <= 1) {
			if (UploadedTotalMB <= 1)
				result = 1;
			else
				result = (float)(1 / sqrt((double)UploadedTotalMB));
			break;
		}

		if (UploadedTotalMB > 1) {
			double Basic = (double)sqrt((double)(DownloadedTotalMB + 1));
			if (DownloadedTotalMB > UploadedTotalMB) {
				result = (Basic + (double)sqrt((double)(DownloadedTotalMB - UploadedTotalMB)));
			}
			else {
				if ((UploadedTotalMB - DownloadedTotalMB) <= 1) {
					result = Basic;
					break;
				}
				double Result = (Basic / (double)sqrt((double)(UploadedTotalMB - DownloadedTotalMB)));
				if (DownloadedTotalMB >= 9) {
					double Lowest = 0.7f + (Basic / 10);
					if (Result < Lowest)   Result = Lowest;
				}
				else {
					if (Result < 1)   Result = DownloadedTotalMB / 9;
				}
				result = Result;
			}
			break;
		}
		else
			result = DownloadedTotalMB;
	}
		break;
	case CS_EASTSHARE: {
		result = (IdentState == IS_NOTAVAILABLE) ? 80 : 100; // stay with the original - Stulle

		result += (float)((double)GetDownloadedTotal() / 174762.67 - (double)GetUploadedTotal() / 524288); //Modefied by Pretender - 20040120

		if ((double)GetDownloadedTotal() > 1048576) {
			result += 100;
			if (result < 50 && ((double)GetDownloadedTotal() * 10 > (double)GetUploadedTotal())) result = 50;
		}

		if (result < 10) {
			result = 10;
		}
		else if (result > 5000) {
			result = 5000;
		}
		result = result / 100;

	}
		break;
	case CS_MAGICANGEL:	{
		if (GetCurrentIdentState(dwForIP) != IS_IDENTIFIED && GetCurrentIdentState(dwForIP) != IS_NOTAVAILABLE && theApp.clientcredits->CryptoAvailable()) {
			result = 1.0f;
			bBadGuy = true;
			break;
		}

		uint64 uDownloadedTotal = GetDownloadedTotal();
		uint64 uUploadedTotal = GetUploadedTotal();

		if (uDownloadedTotal < 1650000 && uUploadedTotal == 0) {
			result = 1.0F;
			break;
		}
		if (!uUploadedTotal)
			result = 50.0F;
		else {
			if (uDownloadedTotal < 1048576) // 1MB
				// We give the client 1 MB upload for the modifier calculation.
				result = (float)(((double)1048576 * 2.0) / (double)uUploadedTotal);
			else
				result = (float)(((double)uDownloadedTotal * 2.0) / (double)uUploadedTotal);
		}

		// exponential calculation of the max multiplicator based on uploaded data (9.2MB = 3.34, 100MB = 10.0)
		float result2 = 0.0F;
		result2 = (float)(uDownloadedTotal / 1048576.0);
		result2 += 2.0F;
		result2 = (float)sqrt(result2);

		// linear calculation of the max multiplicator based on uploaded data for the first chunk (1MB = 1.01, 9.2MB = 3.34)
		float result3 = 10.0F;
		if (uDownloadedTotal < 9646899) {
			result3 = (((float)(uDownloadedTotal - 1048576) / 8598323.0F) * 2.34F) + 1.0F;
		}

		// take the smallest result
		result = min(result, min(result2, result3));

		if (result < 0.1F) {
			result = 0.1F;
			break;
		}
		else if (result > 50.0F) {
			result = 50.0F;
			break;
		}
	}
		break;
	case CS_MAGICANGELPLUS:	{
		if (GetCurrentIdentState(dwForIP) != IS_IDENTIFIED && GetCurrentIdentState(dwForIP) != IS_NOTAVAILABLE && theApp.clientcredits->CryptoAvailable()) {
			result = 1.0f;
			bBadGuy = true;
			break;
		}

		uint64 uDownloadedTotal = GetDownloadedTotal();
		uint64 uUploadedTotal = GetUploadedTotal();

		if (uDownloadedTotal < 1650000 && uUploadedTotal == 0) {
			result = 1.0F;
			break;
		}
		if (!uUploadedTotal)
			result = 50.0F;
		else {
			if (uDownloadedTotal < 1048576) // 1MB
				// We give the client 1 MB upload for the modifier calculation.
				result = (float)(((double)1048576 * 2.0) / (double)uUploadedTotal);
			else
				result = (float)(((double)uDownloadedTotal * 2.0) / (double)uUploadedTotal);
		}

		// exponential calculation of the max multiplicator based on uploaded data (9.2MB = 3.34, 100MB = 10.0)
		float result2 = 0.0F;
		result2 = (float)(uDownloadedTotal / 1048576.0);
		result2 += 2.0F;
		result2 = (float)sqrt(result2);

		// linear calculation of the max multiplicator based on uploaded data for the first chunk (1MB = 1.01, 9.2MB = 3.34)
		float result3 = 10.0F;
		if (uDownloadedTotal < 9646899) {
			result3 = (((float)(uDownloadedTotal - 1048576) / 8598323.0F) * 2.34F) + 1.0F;
		}

		// take the smallest result
		result = min(result, min(result2, result3));

		// add some bonus factors for Upload - sFrQlXeRt
		if (uDownloadedTotal > uUploadedTotal) {
			if (uDownloadedTotal - uUploadedTotal < 7340032) // 7MB
				result += 0.3F;
			else if (uDownloadedTotal - uUploadedTotal < 15728640) // 15MB
				result += 1.0F;
			else if (uDownloadedTotal - uUploadedTotal < 31457280) // 30MB
				result += 2.0F;
			else // uDownloadedTotal - uUploadedTotal >= 31457280 // 30MB
				result += 3.0F;
		}

		if (result < 0.1F) {
			result = 0.1F;
			break;
		}
		else if (result > 50.0F) {
			result = 50.0F;
			break;
		}
	}
		break;
	case CS_SIVKA: {
		switch (currentIDstate) {
		case IS_IDNEEDED: if (theApp.clientcredits->CryptoAvailable()) {
			result = 0.75f;
			bBadGuy = true;
			break;
		}
		case IS_IDFAILED: {
			result = 0.5f;
			bBadGuy = true;
			break;
		}
		case IS_IDBADGUY:
		default: {
			result = 0.0f;
			bBadGuy = true;
			break;
		}
		}

		if (GetDownloadedTotal() > GetUploadedTotal())
		{
			const uint64 diffTransfer = GetDownloadedTotal() - GetUploadedTotal() + 1048576;

			if (diffTransfer >= 1073741824) { // >= 1024MB
				result = 32.0f;
				break;
			}

			result = sqrtf((float)diffTransfer / 1048576.0f);
			break;
		}
		else
			result = 1.0f;
	}
		break;
	case CS_SWAT: {
		if (currentIDstate != IS_IDENTIFIED && currentIDstate != IS_NOTAVAILABLE && theApp.clientcredits->CryptoAvailable()) {
			result = 1.0f;
			bBadGuy = true;
			break;
		}

		if (GetDownloadedTotal() < 1048576) {
			result = 1;
			break;
		}

		if (!GetUploadedTotal())
			result = 10;
		else
			result = (float)(((double)GetDownloadedTotal() * 2.2) / (double)GetUploadedTotal()); //pcsl999

		float result2 = 0;

		result2 = (float)(GetDownloadedTotal() / 1048576.0);
		result2 += 2;
		result2 = (float)sqrt((double)result2);

		if (result > result2)
			result = result2;

		if (result < 1)
			result = 1;
		else if (result > 100) //pcsl999
			result = 100; //pcsl999
	}
		break;
	case CS_TK4: {
		CUpDownClient* client = theApp.clientlist->FindClientByIP(dwForIP);

		result = 10.0F;
		//if SUI failed then credit starts at 10 as for everyone else but will not go up
		if ((currentIDstate == IS_IDFAILED || currentIDstate == IS_IDBADGUY || currentIDstate == IS_IDNEEDED) && theApp.clientcredits->CryptoAvailable()) {
			float dOwnloadedSessionTotal = (float)client->GetTransferredDown();
			float uPloadedSessionTotal = (float)client->GetTransferredUp();
			float allowance = dOwnloadedSessionTotal / 4.0F;
			if (uPloadedSessionTotal > (float)(dOwnloadedSessionTotal + allowance + 1048576.0F)) {
				CKnownFile* file = theApp.sharedfiles->GetFileByID(client->GetUploadFileID());
				if (file != NULL) { //Are they requesting a file? NULL can be produced when client details calls getscoreratio() without this line eMule will crash.
					if (file->IsPartFile()) { //It's a file we are trying to obtain so we want to give to givers so we may get the file quicker.
						float MbSqd = sqrt((float)(uPloadedSessionTotal - (dOwnloadedSessionTotal + allowance)) / 1048576.0F);
						if (MbSqd > 9.0F) result = 9.0F / MbSqd;  //above 81mb values 1 - 0 9/(9 - x)
						else result = 10.0F - MbSqd; //for the first 81Mb (10 -(0-9))
					}

				}
			}
			bBadGuy = true;
			break; //partfile 10 - 0.14 complete 10
		}
		//float is 1e38 it should be sufficient given 1 Gig is 1e9 hence 1000G is 1e12....
		float dOwnloadedTotal = (float)GetDownloadedTotal();//(Given to us)
		float uPloadedTotal = (float)GetUploadedTotal(); //(Taken from us)
		/* Base allowance for a client that has given us nothing is 1Mb
		But if someone has give us 100Mb and take 130Mb they should not be penalized as someone who has give 0Mb and taken 30Mb?
		So if you've given 100Mb and taken 130Mb you will only be penalized for 5Mb*/
		float allowance = dOwnloadedTotal / 4.0F; //reward uploaders with 1 Mb allowance for every 4Mb uploaded over what they have uploaded.
		if (uPloadedTotal > (float)(dOwnloadedTotal + allowance + 1048576.0F)) //If they have taken above (1Mb + 'allowance')
		{/*They may owe us, is it on a file we want or a completed file we are sharing. If it's a completed file progrssively lowering someone score
		who cannot pay us back could make it very difficult for them to complete the file esp. if it's rare and we hold one of the few complete copies, better for everyone if
		we share completed files based on time waited + any credit thay have for giving us stuff.
		If the files a partfile we are trying to get the modifier will start to get smaller -1 to -90Mb range 9 to 1 beyond that 1 to 0 eg: -400Mb = 0.452839 */
			CKnownFile* file = theApp.sharedfiles->GetFileByID(client->GetUploadFileID());
			if (file != NULL) {//Are they requesting a file? NULL can be produced when client details calls getscoreratio() without this line eMule will crash.
				if (file->IsPartFile()) {//It's a file we are trying to obtain so we want to give to givers so we may get the file quicker.
					float MbSqd = sqrt((float)(uPloadedTotal - (dOwnloadedTotal + allowance)) / 1048576.0F);
					if (MbSqd > 9.0F) result = 9.0F / MbSqd;  //above 81mb values 1 - 0 9/(9 - x)
					else		 result = 10.0F - MbSqd; //for the first 81Mb (10 -(0-9))
				}
			}
		}
		else //We may owe them :o) give a small proportional boost to an uploader
			if (dOwnloadedTotal > uPloadedTotal) { // result =  log(2.72 + (given - taken in Mb * 4)) + given in bytes / 12Mb (eg +1 for every 12Mb +.5  6Mb etc)
				result += log(2.72F + (float)(dOwnloadedTotal - uPloadedTotal) / 262144.0F) + (float)(dOwnloadedTotal / 12582912.0F);
			}
	}
		break;
	case CS_XTREME: {
		if ((currentIDstate == IS_IDFAILED || currentIDstate == IS_IDBADGUY || currentIDstate == IS_IDNEEDED) && theApp.clientcredits->CryptoAvailable() == true) {
			result = 0.8f; //Xman 80% for non SUI-clients.. (and also bad guys)
			bBadGuy = true;
			break;
		}

		CUpDownClient* client = theApp.clientlist->FindClientByIP(dwForIP);

		#define PENALTY_UPSIZE 8388608 //8 MB
		// Cache value
		const uint64 downloadTotal = GetDownloadedTotal();

		float m_bonusfaktor = 0.0F;
		// Check if this client has any credit (sent >1.65MB)
		const float difference2 = (float)client->GetTransferredUp() - client->GetTransferredDown();
		if (downloadTotal < 1650000)
		{
			if (difference2 > (2 * PENALTY_UPSIZE))
				m_bonusfaktor = (-0.2f);
			else if (difference2 > PENALTY_UPSIZE)
				m_bonusfaktor = (-0.1f);
			else
				m_bonusfaktor = 0;

			result = (1.0f + m_bonusfaktor);
			break;
		}

		// Cache value
		const uint64 uploadTotal = GetUploadedTotal();

		// Bonus Faktor calculation
		float difference = (float)downloadTotal - uploadTotal;
		if (difference >= 0)
		{
			m_bonusfaktor = difference / 10485760.0f - (1.5f / (downloadTotal / 10485760.0f));  //pro MB difference 0.1 - pro MB download 0.1
			if (m_bonusfaktor < 0)
				m_bonusfaktor = 0;
		}
		else
		{
			difference = abs(difference);
			if (difference > (2 * PENALTY_UPSIZE) && difference2 > (2 * PENALTY_UPSIZE))
				m_bonusfaktor = (-0.2f);
			else if (difference > PENALTY_UPSIZE && difference2 > PENALTY_UPSIZE)
				m_bonusfaktor = (-0.1f);
			else
				m_bonusfaktor = 0;
		}
		// Factor 1
		result = (uploadTotal == 0) ?
			10.0f : (float)(2 * downloadTotal) / (float)uploadTotal;

		// Factor 2
		//Xman slightly changed to use linear function until half of chunk is transferred
		float trunk;
		if (downloadTotal < 4718592)  //half of a chunk and a good point to keep the function consistent
			trunk = (float)(1.0 + (double)downloadTotal / (1048576.0 * 3.0));
		else
			trunk = (float)sqrt(2.0 + (double)downloadTotal / 1048576.0);
		//Xman end


		if (result > 10.0f)
		{
			result = 10.0f;
			m_bonusfaktor = 0;
		}
		else
			result += m_bonusfaktor;
		if (result > 10.0f)
		{
			m_bonusfaktor -= (float)(result - 10.0f);
			result = 10.0f;
		}

		if (result > trunk)
		{
			result = trunk;
			m_bonusfaktor = 0;
		}

		// Trunk final result 1..10
		if (result < 1.0f)
		{
			result = (1.0f + m_bonusfaktor);
			break;
		}
		if (result > 10.0f)
		{
			result = 10.0f;
			break;
		}
	}
		break;
	case CS_ZZUL: {
		if (currentIDstate != IS_IDENTIFIED && currentIDstate != IS_NOTAVAILABLE && theApp.clientcredits->CryptoAvailable())
		{
			result = 1.0F;
			bBadGuy = true;
			break;
		}

		if (GetDownloadedTotal() < 1)
		{
			result = 1.0F;
			break;
		}
		result = 0.0F;
		if (!GetUploadedTotal())
			result = 10.0F;
		else
			result = (float)(((double)GetDownloadedTotal() * 2.0) / (double)GetUploadedTotal());
		float result2 = 0.0F;
		result2 = (float)(GetDownloadedTotal() / 1048576.0);
		result2 += 1.0F;
		result2 = (float)sqrt(result2);

		if (result > result2)
			result = result2;

		if (result < 1.0F)
		{
			result = 1.0F;
			break;
		}
		else if (result > 10.0F)
		{
			result = 10.0F;
			break;
		}
	}
		break;
	case CS_OFFICIAL:
	default: {
		// check the client ident status
		switch (GetCurrentIdentState(dwForIP)) {
		case IS_IDFAILED:
		case IS_IDBADGUY:
		case IS_IDNEEDED:
			if (theApp.clientcredits->CryptoAvailable())
				// bad guy - no credits for you
				return 1.0f;
		}

		if (GetDownloadedTotal() < 1048576)
			return 1.0f;
		float result;
		if (GetUploadedTotal())
			result = (GetDownloadedTotal() * 2) / (float)GetUploadedTotal();
		else
			result = 10.0f;

		// exponential calculation of the max multiplicator based on uploaded data (9.2MB = 3.34, 100MB = 10.0)
		float result2 = sqrt(GetDownloadedTotal() / 1048576.0f + 2.0f);

		// linear calculation of the max multiplicator based on uploaded data for the first chunk (1MB = 1.01, 9.2MB = 3.34)
		float result3;
		if (GetDownloadedTotal() < 9646899)
			result3 = (GetDownloadedTotal() - 1048576) / 8598323.0f * 2.34f + 1.0f;
		else
			result3 = 10.0f;

		// take the smallest result
		result = min(result, min(result2, result3));

		if (result < 1.0F) {
			result = 1.0F;
			break;
		}
		else if (result > 10.0F) {
			result = 10.0F;
			break;
		}
	}
		break;
	}

	if (bBadGuy)
		m_bCheckScoreRatio = true;

	// FIXED: Anti Upload Protection when the other client have not yet uploaded to us [evcz]
	if (thePrefs.IsAntiUploadProtection() && thePrefs.TransferFullChunks() && (GetDownloadedTotal() < thePrefs.GetAntiUploadProtectionLimit() * 1024))
		//fixed to handle proper default values depending on the CS... not everytime 1.0... ;)
		//code from StulleMule
		switch (thePrefs.GetCreditSystem()) {
		case CS_LOVELACE: {
			if ((float)result > 0.985f)
				return m_fLastScoreRatio = 0.985f;
		}
		case CS_PAWCIO: {
			if ((float)result > (float)3.0f)
				return m_fLastScoreRatio = 3.0f;
		}
		case CS_TK4: {
			if ((float)result > (float)10.0f)
				return m_fLastScoreRatio = 3.0f;
		}
		case CS_RATIO:
		case CS_EASTSHARE:
		case CS_SIVKA:
		case CS_SWAT:
		case CS_XTREME:
		case CS_ZZUL:
		case CS_MAGICANGEL:
		case CS_MAGICANGELPLUS:
		case CS_OFFICIAL:
		default: {
			if ((float)result > 1.0f)
				return m_fLastScoreRatio = 1.0f;
		}
		}

	return m_fLastScoreRatio = (float)result;
}

const float CClientCredits::GetMyScoreRatio(const CAddress& dwForIP) const
{
	// check the client ident status
	if ((GetCurrentIdentState(dwForIP) == IS_IDFAILED || GetCurrentIdentState(dwForIP) == IS_IDBADGUY || GetCurrentIdentState(dwForIP) == IS_IDNEEDED) && theApp.clientcredits->CryptoAvailable()) {
		// bad guy - no credits for... me?
		return 1.0f;
	}

	if (GetUploadedTotal() < 1048576)
		return 1.0f;
	float result = 0;
	if (!GetDownloadedTotal())
		result = 10.0f;
	else
		result = (float)(((double)GetUploadedTotal() * 2.0) / (double)GetDownloadedTotal());
	float result2 = 0;
	result2 = (float)(GetUploadedTotal() / 1048576.0);
	result2 += 2.0f;
	result2 = (float)sqrt(result2);

	// linear calcualtion of the max multiplicator based on uploaded data for the first chunk (1MB = 1.01, 9.2MB = 3.34)
	float result3 = 10.0F;
	if (GetUploadedTotal() < 9646899) {
		result3 = (((float)(GetUploadedTotal() - 1048576) / 8598323.0F) * 2.34F) + 1.0F;
	}

	// take the smallest result
	result = min(result, min(result2, result3));

	if (result < 1.0f)
		return 1.0f;
	else if (result > 10.0f)
		return 10.0f;
	return result;
}

CClientCreditsList::CClientCreditsList()
{
	m_nLastSaved = ::GetTickCount();
	LoadList();

	InitalizeCrypting();
}

CClientCreditsList::~CClientCreditsList()
{
	SaveList();
	CCKey tmpkey;
	for (POSITION pos = m_mapClients.GetStartPosition(); pos != NULL;) {
		CClientCredits *cur_credit;
		m_mapClients.GetNextAssoc(pos, tmpkey, cur_credit);
		delete cur_credit;
	}
	delete m_pSignkey;
}

void CClientCreditsList::LoadList()
{
	const CString &sConfDir(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR));
	const CString &strFileName(sConfDir + CLIENTS_MET_FILENAME);
	const int iOpenFlags = CFile::modeRead | CFile::osSequentialScan | CFile::typeBinary | CFile::shareDenyWrite;
	CSafeBufferedFile file;
	if (!CFileOpen(file, strFileName, iOpenFlags, GetResString(_T("ERR_LOADCREDITFILE"))))
		return;
	::setvbuf(file.m_pStream, NULL, _IOFBF, 16384);

	try {
		uint8 version = file.ReadUInt8();
		if (version != CREDITFILE_VERSION && version != CREDITFILE_VERSION_29) {
			LogWarning(GetResString(_T("ERR_CREDITFILEOLD")));
			file.Close();
			return;
		}

		// everything is OK, lets see if the backup exist...
		const CString &strBakFileName(sConfDir + CLIENTS_MET_FILENAME _T(".bak"));

		BOOL bCreateBackup = TRUE;

		HANDLE hBakFile = ::CreateFile(strBakFileName, GENERIC_READ, FILE_SHARE_READ, NULL,
			OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (hBakFile != INVALID_HANDLE_VALUE) {
			// OK, the backup exist, get the size
			DWORD dwBakFileSize = ::GetFileSize(hBakFile, NULL); //debug
			if (dwBakFileSize > (DWORD)file.GetLength()) {
				// the size of the backup was larger then the orig. file, something is wrong here, don't overwrite old backup.
				bCreateBackup = FALSE;
			}
			//else: backup is smaller or the same size as orig. file, proceed with copying of file
			::CloseHandle(hBakFile);
		}
		//else: the backup doesn't exist, create it

		if (bCreateBackup) {
			file.Close(); // close the file before copying

			if (!::CopyFile(strFileName, strBakFileName, FALSE))
				LogError(GetResString(_T("ERR_MAKEBAKCREDITFILE")));

			// reopen file
			if (!CFileOpen(file, strFileName, iOpenFlags, GetResString(_T("ERR_LOADCREDITFILE"))))
				return;
			::setvbuf(file.m_pStream, NULL, _IOFBF, 16384);
			file.Seek(1, CFile::begin); //set file pointer behind file version byte
		}

		uint32 count = file.ReadUInt32();
		m_mapClients.InitHashTable(count + 5000); // TODO: should be prime number... and 20% larger

		const time_t dwExpired = time(NULL) - (thePrefs.GetClientsExpDays() < 180 ? 15552000 : thePrefs.GetClientsExpDays() * 86400);
		uint32 cDeleted = 0;
		for (uint32 i = 0; i < count; ++i) {
			CreditStruct newcstruct{};
			file.Read(&newcstruct, (version == CREDITFILE_VERSION_29) ? sizeof(CreditStruct_29a) : sizeof(CreditStruct));

			if (newcstruct.nLastSeen < (uint32)dwExpired)
				++cDeleted;
			else {
				CClientCredits *newcredits = new CClientCredits(newcstruct);
				m_mapClients[CCKey(newcredits->GetKey())] = newcredits;
			}
		}
		file.Close();

		if (cDeleted > 0)
			AddLogLine(false, GetResString(_T("CREDITFILELOADED")) + GetResString(_T("CREDITSEXPIRED2")), count - cDeleted, cDeleted, thePrefs.GetClientsExpDays());
		else
			AddLogLine(false, GetResString(_T("CREDITFILELOADED")), count);
	} catch (CFileException *ex) {
		if (ex->m_cause == CFileException::endOfFile)
			LogError(LOG_STATUSBAR, GetResString(_T("CREDITFILECORRUPT")));
		else
			LogError(LOG_STATUSBAR, GetResString(_T("ERR_CREDITFILEREAD")), (LPCTSTR)EscPercent(CExceptionStr(*ex)));
		ex->Delete();
	}
}

void CClientCreditsList::SaveList()
{
	if (thePrefs.GetLogFileSaving())
		AddDebugLogLine(false, _T("Saving clients credit list file \"%s\""), CLIENTS_MET_FILENAME);
	m_nLastSaved = ::GetTickCount();

	const CString& sConfDir(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR));
	CFile file;// no buffering needed here since we swap out the entire array
	if (!CFileOpen(file
		, sConfDir + CLIENTS_MET_FILENAME_TMP
		, CFile::modeWrite | CFile::modeCreate | CFile::typeBinary | CFile::shareDenyWrite
		, GetResString(_T("ERR_FAILED_CREDITSAVE"))))
	{
		return;
	}

	byte *pBuffer = new byte[m_mapClients.GetCount() * sizeof(CreditStruct)]; //not CreditStruct[] because of alignment
	uint32 count = 0;
	for (const CClientCreditsMap::CPair *pair = m_mapClients.PGetFirstAssoc(); pair != NULL; pair = m_mapClients.PGetNextAssoc(pair)) {
		const CClientCredits *cur_credit = pair->value;
		if (cur_credit->GetUploadedTotal() || cur_credit->GetDownloadedTotal())
			*reinterpret_cast<CreditStruct*>(&pBuffer[sizeof(CreditStruct) * count++]) = cur_credit->m_Credits;
	}

	try {
		uint8 version = CREDITFILE_VERSION;
		file.Write(&version, 1);
		file.Write(&count, 4);
		file.Write(pBuffer, (UINT)(count * sizeof(CreditStruct)));
		file.Close();
		MoveFileEx(sConfDir + CLIENTS_MET_FILENAME_TMP, sConfDir + CLIENTS_MET_FILENAME, MOVEFILE_REPLACE_EXISTING);
	} catch (CFileException *ex) {
		LogError(LOG_STATUSBAR, _T("%s%s"), (LPCTSTR)GetResString(_T("ERR_FAILED_CREDITSAVE")), (LPCTSTR)EscPercent(CExceptionStrDash(*ex)));
		ex->Delete();
	}

	delete[] pBuffer;
}

CClientCredits* CClientCreditsList::GetCredit(const uchar *key, bool bSetLastSeen)
{
	CCKey tkey(key);
	CClientCredits *result;
	if (!m_mapClients.Lookup(tkey, result)) {
		result = new CClientCredits(key);
		m_mapClients[CCKey(result->GetKey())] = result;
	}
	if (bSetLastSeen)
		result->SetLastSeen();
	return result;
}

void CClientCreditsList::Process()
{
	if (::GetTickCount() >= m_nLastSaved + MIN2MS(13))
		SaveList();
}

void CClientCredits::InitalizeIdent()
{
	if (m_Credits.nKeySize == 0) {
		memset(m_abyPublicKey, 0, sizeof m_abyPublicKey); // for debugging
		m_nPublicKeyLen = 0;
		IdentState = IS_NOTAVAILABLE;
	} else {
		m_nPublicKeyLen = m_Credits.nKeySize;
		memcpy(m_abyPublicKey, m_Credits.abySecureIdent, m_nPublicKeyLen);
		IdentState = IS_IDNEEDED;
	}
	m_dwCryptRndChallengeFor = 0;
	m_dwCryptRndChallengeFrom = 0;
	m_dwIdentIP = CAddress();
}

void CClientCredits::Verified(const CAddress& dwForIP)
{
	m_dwIdentIP = dwForIP;
	// client was verified, copy the key to store him if not done already
	if (m_Credits.nKeySize == 0) {
		m_Credits.nKeySize = m_nPublicKeyLen;
		memcpy(m_Credits.abySecureIdent, m_abyPublicKey, m_nPublicKeyLen);
		if (GetDownloadedTotal() > 0) {
			// for security reason, we have to delete all prior credits here
			m_Credits.nDownloadedHi = 0;
			m_Credits.nDownloadedLo = 1;
			m_Credits.nUploadedHi = 0;
			m_Credits.nUploadedLo = 1; // in order to save this client, set 1 byte
			if (thePrefs.GetVerbose())
				DEBUG_ONLY(AddDebugLogLine(false, _T("Credits deleted due to new SecureIdent")));
		}
	}
	IdentState = IS_IDENTIFIED;
}

void CClientCredits::ResetTransientSecureIdentState()
{
	m_dwIdentIP = CAddress();
	IdentState = (m_Credits.nKeySize != 0) ? IS_IDNEEDED : IS_NOTAVAILABLE;
	m_dwCryptRndChallengeFor = 0;
	m_dwCryptRndChallengeFrom = 0;
}

bool CClientCredits::SetSecureIdent(const uchar *pachIdent, uint8 nIdentLen)  // verified Public key cannot change, use only if there is no public key yet
{
	if (MAXPUBKEYSIZE < nIdentLen || m_Credits.nKeySize != 0)
		return false;
	memcpy(m_abyPublicKey, pachIdent, nIdentLen);
	m_nPublicKeyLen = nIdentLen;
	IdentState = IS_IDNEEDED;
	return true;
}

EIdentState	CClientCredits::GetCurrentIdentState(const CAddress& dwForIP) const
{
	if (IdentState != IS_IDENTIFIED)
		return IdentState;
	if (dwForIP == m_dwIdentIP)
		return IS_IDENTIFIED;
	return IS_IDBADGUY;
	// mod note: clients which just reconnected after an IP change and have to ident yet will also have this state for 1-2 seconds
	//		 so don't try to spam such clients with "bad guy" messages (besides: spam messages are always bad)
}

using namespace CryptoPP;

void CClientCreditsList::InitalizeCrypting()
{
	m_nMyPublicKeyLen = 0;
	memset(m_abyMyPublicKey, 0, sizeof m_abyMyPublicKey); // not really needed; better for debugging tho
	m_pSignkey = NULL;
	if (!thePrefs.IsSecureIdentEnabled())
		return;
	// check if keyfile is there
	bool bCreateNewKey = false;
	const CString &cryptkeypath(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + _T("cryptkey.dat"));
	HANDLE hKeyFile = ::CreateFile(cryptkeypath
		, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hKeyFile != INVALID_HANDLE_VALUE) {
		if (::GetFileSize(hKeyFile, NULL) == 0)
			bCreateNewKey = true;
		::CloseHandle(hKeyFile);
	} else
		bCreateNewKey = true;
	if (bCreateNewKey)
		CreateKeyPair();

	// load key
	try {
		// load private key
		FileSource filesource((CStringA)cryptkeypath, true, new Base64Decoder);
		m_pSignkey = new RSASSA_PKCS1v15_SHA_Signer(filesource);
		// calculate and store public key
		RSASSA_PKCS1v15_SHA_Verifier pubkey(*m_pSignkey);
		ArraySink asink(m_abyMyPublicKey, sizeof m_abyMyPublicKey);
		pubkey.GetMaterial().Save(asink);
		m_nMyPublicKeyLen = (uint8)asink.TotalPutLength();
		asink.MessageEnd();
	} catch (...) {
		delete m_pSignkey;
		m_pSignkey = NULL;
		LogError(LOG_STATUSBAR, GetResString(_T("CRYPT_INITFAILED")));
		ASSERT(0);
	}
	ASSERT(Debug_CheckCrypting());
}

bool CClientCreditsList::CreateKeyPair()
{
	try {
		AutoSeededRandomPool rng;
		InvertibleRSAFunction privkey;
		privkey.Initialize(rng, RSAKEYSIZE);

		Base64Encoder privkeysink(new FileSink((CStringA)(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + _T("cryptkey.dat"))));
		privkey.DEREncode(privkeysink);
		privkeysink.MessageEnd();

		if (thePrefs.GetLogSecureIdent())
			AddDebugLogLine(false, _T("Created new RSA keypair"));
		return true;
	} catch (...) {
		if (thePrefs.GetVerbose())
			AddDebugLogLine(false, _T("Failed to create new RSA keypair"));
		ASSERT(0);
	}
	return false;
}

uint8 CClientCreditsList::CreateSignature(CClientCredits *pTarget, uchar *pachOutput, uint8 nMaxSize
	, const CAddress& ChallengeIP, uint8 byChaIPKind, CryptoPP::RSASSA_PKCS1v15_SHA_Signer *sigkey)
{
	ASSERT(pTarget != NULL && pachOutput != NULL);
	// sigkey param is used for debug only
	if (sigkey == NULL)
		sigkey = m_pSignkey;

	// create a signature of the public key from pTarget
	if (!CryptoAvailable())
		return 0;
	try {
		SecByteBlock sbbSignature(sigkey->SignatureLength());
		AutoSeededRandomPool rng;
		//byte abyBuffer[MAXPUBKEYSIZE + 9];
		byte abyBuffer[MAXPUBKEYSIZE+21]; //4+16+1
		size_t keylen = pTarget->GetSecIDKeyLen();
		memcpy(abyBuffer, pTarget->GetSecureIdent(), keylen);
		// 4 additional bytes of random data sent from this client
		uint32 challenge = pTarget->m_dwCryptRndChallengeFrom;
		ASSERT(challenge);
		PokeUInt32(&abyBuffer[keylen], challenge);
		size_t ChIpLen;
		if (byChaIPKind == 0)
			ChIpLen = 0;
		else {
			if (ChallengeIP.GetType() == CAddress::IPv6)
			{
				ChIpLen = 17; //16+1
				memcpy(abyBuffer + keylen + 4, ChallengeIP.Data(), 16);
				PokeUInt8(abyBuffer + keylen + 20, byChaIPKind); //16+4
			} else {
				ChIpLen = 5; //4+1
				PokeUInt32(abyBuffer + keylen + 4,  ChallengeIP.ToUInt32(false));
				PokeUInt8(abyBuffer + keylen + 8, byChaIPKind); //4+4
			}

		}
		sigkey->SignMessage(rng, abyBuffer, keylen + 4 + ChIpLen, sbbSignature.begin());
		ArraySink asink(pachOutput, nMaxSize);
		asink.Put(sbbSignature.begin(), sbbSignature.size());
		return (uint8)asink.TotalPutLength();
	} catch (...) {
		ASSERT(0);
	}
	return 0;
}

bool CClientCreditsList::VerifyIdent(CClientCredits *pTarget, const uchar *pachSignature, uint8 nInputSize,
	//uint32 dwForIP, uint8 byChaIPKind)
	const CAddress& dwForIP, uint8 byChaIPKind)
{
	ASSERT(pTarget);
	ASSERT(pachSignature);
	if (!CryptoAvailable()) {
		pTarget->IdentState = IS_NOTAVAILABLE;
		return false;
	}
	bool bResult;
	try {
		StringSource ss_Pubkey((byte*)pTarget->GetSecureIdent(), pTarget->GetSecIDKeyLen(), true, 0);
		RSASSA_PKCS1v15_SHA_Verifier pubkey(ss_Pubkey);
		// 4 additional bytes random data send from this client +5 bytes v2
		byte abyBuffer[MAXPUBKEYSIZE + 9];
		memcpy(abyBuffer, m_abyMyPublicKey, m_nMyPublicKeyLen);
		uint32 challenge = pTarget->m_dwCryptRndChallengeFor;
		ASSERT(challenge);
		PokeUInt32(&abyBuffer[m_nMyPublicKeyLen], challenge);

		// v2 security improvements (not supported by 29b, not used as default by 29c)
		size_t nChIpSize;
		if (byChaIPKind == 0)
			nChIpSize = 0;
		else {
			CAddress ChallengeIP;
			switch (byChaIPKind) {
			case CRYPT_CIP_LOCALCLIENT:
				ChallengeIP = dwForIP;
				break;
			case CRYPT_CIP_REMOTECLIENT:
				if (theApp.serverconnect->GetClientID() == 0 || theApp.serverconnect->IsLowID()) {
					if (thePrefs.GetLogSecureIdent())
						AddDebugLogLine(false, _T("Warning: Maybe SecureHash Ident fails because LocalIP is unknown"));
					ChallengeIP = CAddress(theApp.serverconnect->GetLocalIP(), false);
				} else
					ChallengeIP = CAddress(theApp.serverconnect->GetClientID(), false);
				break;
			case CRYPT_CIP_NONECLIENT: // maybe not supported in future versions
				ChallengeIP = CAddress();
			}
			if (ChallengeIP.GetType() == CAddress::IPv6)
			{
				nChIpSize = 17; //16+1
				memcpy(abyBuffer + m_nMyPublicKeyLen + 4, ChallengeIP.Data(), 16);
				PokeUInt8(abyBuffer + m_nMyPublicKeyLen + 20, byChaIPKind); //16+4
			} else {
				nChIpSize = 5; //4+1
				PokeUInt32(abyBuffer + m_nMyPublicKeyLen + 4, ChallengeIP.ToUInt32(false));
				PokeUInt8(abyBuffer + m_nMyPublicKeyLen + 8, byChaIPKind); //4+4
			}
		}
		//v2 end

		bResult = pubkey.VerifyMessage(abyBuffer, m_nMyPublicKeyLen + 4 + nChIpSize, pachSignature, nInputSize);
	} catch (...) {
		if (thePrefs.GetVerbose())
			AddDebugLogLine(false, _T("Error: Unknown exception in %hs"), __FUNCTION__);
		bResult = false;
	}
	if (!bResult) {
		if (pTarget->IdentState == IS_IDNEEDED)
			pTarget->IdentState = IS_IDFAILED;
	} else
		pTarget->Verified(dwForIP);

	return bResult;
}

bool CClientCreditsList::CryptoAvailable() const
{
	return m_nMyPublicKeyLen > 0 && m_pSignkey != NULL && thePrefs.IsSecureIdentEnabled();
}

#ifdef _DEBUG
bool CClientCreditsList::Debug_CheckCrypting()
{
	// create random key
	AutoSeededRandomPool rng;

	RSASSA_PKCS1v15_SHA_Signer priv(rng, 384);
	RSASSA_PKCS1v15_SHA_Verifier pub(priv);

	byte abyPublicKey[80];
	ArraySink asink(abyPublicKey, sizeof abyPublicKey);
	pub.GetMaterial().Save(asink);
	uint8 PublicKeyLen = (uint8)asink.TotalPutLength();
	asink.MessageEnd();
	uint32 challenge = GetRandomUInt32();
	// create fake client which pretends to be this emule
	CreditStruct emptystruct{};
	CClientCredits newcredits(emptystruct);
	newcredits.SetSecureIdent(m_abyMyPublicKey, m_nMyPublicKeyLen);
	newcredits.m_dwCryptRndChallengeFrom = challenge;
	// create signature with fake priv key
	uchar pachSignature[200] = {};
	uint8 sigsize = CreateSignature(&newcredits, pachSignature, sizeof pachSignature, CAddress(), 0, &priv);

	// next fake client uses the random created public key
	CClientCredits newcredits2(emptystruct);
	newcredits2.m_dwCryptRndChallengeFor = challenge;

	// if you uncomment one of the following lines the check has to fail

	newcredits2.SetSecureIdent(abyPublicKey, PublicKeyLen);

	//now verify this signature - if it's true everything is fine
	bool bResult = VerifyIdent(&newcredits2, pachSignature, sigsize, CAddress(), 0);

	return bResult;
}
#endif

DWORD CClientCredits::GetSecureWaitStartTime(const CAddress& dwForIP)
{
	if (m_dwUnSecureWaitTime == 0 || m_dwSecureWaitTime == 0)
		SetSecWaitStartTime(dwForIP);

	if (m_Credits.nKeySize != 0) {	// this client is a SecureHash Client
		if (GetCurrentIdentState(dwForIP) == IS_IDENTIFIED) // good boy
			return m_dwSecureWaitTime;

		// not so good boy
		if (dwForIP == m_dwWaitTimeIP)
			return m_dwUnSecureWaitTime;

		// bad boy
		// this can also happen if the client has not identified himself yet, but will do later - so maybe he is not a bad boy :) .

		m_dwUnSecureWaitTime = ::GetTickCount();
		m_dwWaitTimeIP = dwForIP;
	}
	// not a SecureHash Client - handle it like before for now (no security checks)
	return m_dwUnSecureWaitTime;
}

void CClientCredits::SetSecWaitStartTime(const CAddress& dwForIP)
{
	m_dwUnSecureWaitTime = ::GetTickCount() - 1;
	m_dwSecureWaitTime = m_dwUnSecureWaitTime;
	m_dwWaitTimeIP = dwForIP;
}

void CClientCredits::ClearWaitStartTime()
{
	m_dwUnSecureWaitTime = 0;
	m_dwSecureWaitTime = 0;
}

void CClientCreditsList::ResetCheckScoreRatio() {
	CClientCredits* cur_credit;
	CCKey tempkey(0);
	POSITION pos = m_mapClients.GetStartPosition();
	while (pos)
	{
		m_mapClients.GetNextAssoc(pos, tempkey, cur_credit);
		cur_credit->m_bCheckScoreRatio = true;
	}
}
