#include "stdafx.h"
#include "LayeredWindowHelperST.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


// This function adds the WS_EX_LAYERED style to the specified window.
//
// Parameters:
//		[IN]	Handle to the window and, indirectly, the class to which the window belongs.
//				Windows 95/98/Me: The SetWindowLong function may fail if the window
//				specified by the hWnd parameter does not belong to the same process
//				as the calling thread.
//
// Return value:
//		Non zero
//			Function executed successfully.
//		Zero
//			Function failed. To get extended error information, call ::GetLastError().
//
LONG_PTR CLayeredWindowHelperST::AddLayeredStyle(HWND hWnd)
{
	return ::SetWindowLongPtr(hWnd, GWL_EXSTYLE, ::GetWindowLongPtr(hWnd, GWL_EXSTYLE) | WS_EX_LAYERED);
} // End of AddLayeredStyle

// This function removes the WS_EX_LAYERED style from the specified window.
//
// Parameters:
//		[IN]	Handle to the window and, indirectly, the class to which the window belongs.
//				Windows 95/98/Me: The SetWindowLong function may fail if the window
//				specified by the hWnd parameter does not belong to the same process
//				as the calling thread.
//
// Return value:
//		Non zero
//			Function executed successfully.
//		Zero
//			Function failed. To get extended error information, call ::GetLastError().
//
LONG_PTR CLayeredWindowHelperST::RemoveLayeredStyle(HWND hWnd)
{
	return ::SetWindowLongPtr(hWnd, GWL_EXSTYLE, ::GetWindowLongPtr(hWnd, GWL_EXSTYLE) & ~WS_EX_LAYERED);
} // End of RemoveLayeredStyle

// This function sets the percentage of opacity or transparency of a layered window.
//
// Parameters:
//		[IN]	hWnd
//				Handle to the layered window.
//		[IN]	byPercentage
//				Percentage (from 0 to 100)
//
// Return value:
//		TRUE
//			Function executed successfully.
//		FALSE
//			Function failed. To get extended error information, call ::GetLastError().
//
BOOL CLayeredWindowHelperST::SetTransparentPercentage(HWND hWnd, UINT byPercentage)
{
	// Do not accept values greater than 100%
	return ::SetLayeredWindowAttributes(hWnd, 0, (BYTE)(255 * min(byPercentage, 100) / 100), LWA_ALPHA);
} // End of SetTransparentPercentage